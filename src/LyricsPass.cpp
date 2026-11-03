// Renderer Foundation - LyricsFlow port: lyric glyph pass (see LyricsPass.h). Original code for Renderer Foundation.
// STAGED, NOT RUN IN GAME YET: compile-verified by CI only. Mirrors SpatialPanel's D3D12 patterns (root constants +
// SRV table, premultiplied blend, PSO per back-buffer format, graveyard for resources in flight).
#include "D3D12Api.h"
#include "LyricsPass.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <imgui.h>

#include "BfLyrics.h"
#include "LLShim.h"
#include "LyricsCamera.h"
#include "Options.h"
#include "SpatialPanel.h"

#if __has_include("GlyphShaders.h")
#include "GlyphShaders.h"
#endif

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace rf::lyrics {
namespace {
using Microsoft::WRL::ComPtr;

constexpr UINT     kSlots     = 4;     // ring of upload buffers (>= swap chain buffers)
constexpr UINT     kCap       = 16384; // glyphs per frame
constexpr UINT     kTable     = 2 + kBfLyricsMaxAtlases + 3; // t0 glyphs, t1..t16 atlases, t17 UI target, t18 frame copy, t19/t20 blur
constexpr UINT     kUiSrv     = 1 + kBfLyricsMaxAtlases;     // 17
constexpr UINT     kSceneSrv  = kUiSrv + 1;                  // 18 (glass)
constexpr UINT     kBlurSrv   = kUiSrv + 2;                  // 19 (final blur), 20 = ping-pong
constexpr uint32_t kMaxAtlas  = kBfLyricsMaxAtlases;

// ---------------------------------------------------------------- settings (F6, not persisted yet)
struct Ui {
    bool  enabled     = true;
    bool  underHud    = false; // spike S2
    int   camSource   = 0;     // 0 auto (hook, else BF estimate), 1 BF estimate only
    bool  flipForward = false; // S1 check: projection expects +Z forward
    bool  debugBounds = false;
    float opacity     = 1.0f;
} gUi;

// ---------------------------------------------------------------- BF link
BF_CopyLyricFrame_t bfCopy() {
    static BF_CopyLyricFrame_t fn = nullptr;
    static int                 delay = 0;
    if (fn || delay-- > 0) return fn;
    delay = 240; // look again in ~4 s
    if (HMODULE m = GetModuleHandleW(L"BedrockForgeCore.dll"))
        fn = reinterpret_cast<BF_CopyLyricFrame_t>(reinterpret_cast<void*>(GetProcAddress(m, "BF_CopyLyricFrame")));
    return fn;
}

// ---------------------------------------------------------------- atlas decoding (worker thread, WIC)
struct Decoded {
    UINT                 w = 0, h = 0;
    std::vector<uint8_t> rgba;
};
std::mutex           gDecMutex;
std::vector<Decoded> gDecoded;
uint32_t             gDecodedGen = 0, gRequestedGen = 0, gUploadedGen = 0;
static_assert(kSlots == 4, "gSlotGen initialiser");
uint32_t             gSlotGen[kSlots] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu}; // atlas generation each ring slot's descriptors show
std::atomic_bool     gDecoding{false};
std::string          gAtlasStatus = "none";

bool decodePng(std::wstring const& path, Decoded& out) {
    ComPtr<IWICImagingFactory> f;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return false;
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec))) return false;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(dec->GetFrame(0, &frame))) return false;
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(f->CreateFormatConverter(&conv))) return false;
    if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) return false;
    conv->GetSize(&out.w, &out.h);
    out.rgba.resize(size_t(out.w) * out.h * 4);
    return SUCCEEDED(conv->CopyPixels(nullptr, out.w * 4, UINT(out.rgba.size()), out.rgba.data()));
}

bool startDecode(BfLyricFrameV1 const& hdr) {
    if (gDecoding.exchange(true)) return false; // busy: the caller asks again next Present
    std::vector<std::wstring> files;
    for (uint32_t i = 0; i < std::min(hdr.atlasCount, kMaxAtlas); ++i) {
        int          n = MultiByteToWideChar(CP_UTF8, 0, hdr.atlasFiles[i], -1, nullptr, 0);
        std::wstring w(size_t(std::max(n, 1)), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, hdr.atlasFiles[i], -1, w.data(), n);
        w.resize(wcslen(w.c_str()));
        files.push_back(w);
    }
    uint32_t gen = hdr.atlasGeneration;
    std::thread([files, gen] {
        HRESULT              co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        std::vector<Decoded> out(files.size());
        int                  ok = 0;
        for (size_t i = 0; i < files.size(); ++i) ok += decodePng(files[i], out[i]) ? 1 : 0;
        if (SUCCEEDED(co)) CoUninitialize();
        std::lock_guard l(gDecMutex);
        gDecoded      = std::move(out);
        gDecodedGen   = gen;
        gAtlasStatus  = std::to_string(ok) + "/" + std::to_string(files.size()) + " atlases decoded";
        gDecoding     = false;
    }).detach();
    return true;
}

// ---------------------------------------------------------------- D3D12 objects (overlay thread)
ComPtr<ID3D12Device>         gDevice;
ComPtr<ID3D12RootSignature>  gRoot;
ComPtr<ID3D12PipelineState>  gPsoGlyph, gPsoComposite, gPsoDown, gPsoBlurH, gPsoBlurV;
DXGI_FORMAT                  gPsoFmt = DXGI_FORMAT_UNKNOWN;
ComPtr<ID3D12DescriptorHeap> gSrvHeap, gRtvHeap;
UINT                         gSrvInc = 0;
ComPtr<ID3D12Resource>       gGlyphBuf[kSlots];
BfGlyphV1*                   gGlyphMap[kSlots]{};
ComPtr<ID3D12Resource>       gAtlas[kMaxAtlas];
std::vector<ComPtr<ID3D12Resource>> gGraveyard;
std::vector<std::pair<ComPtr<ID3D12Resource>, uint64_t>> gUiGrave; // old S2 targets + the Present they died on
uint64_t                     gPresents = 0;
ComPtr<ID3D12Resource>       gUiTarget;
// glass (client UI / LyricsFlow panel): copy of the frame + quarter-res blur ping-pong (A = final, B = temp)
ComPtr<ID3D12Resource>       gScene, gBlurA, gBlurB;
UINT                         gQW = 0, gQH = 0;
D3D12_CPU_DESCRIPTOR_HANDLE  gRtvA{}, gRtvB{};
bool                         gGlassUsed = false;
UINT                         gW = 0, gH = 0;
DXGI_FORMAT                  gFmt = DXGI_FORMAT_UNKNOWN;
std::atomic<size_t>          gUiRtv{0};
std::atomic_bool             gUiReady{false}, gRedirected{false};
bool                         gBroken = false;
std::string                  gStatus = "waiting for Bedrock Forge";
std::vector<BfGlyphV1>       gHost(kCap);
BfLyricFrameV1               gHdr{};
std::string                  gCamStatus = "-";

void fail(std::string const& what, HRESULT hr) {
    char b[16];
    snprintf(b, sizeof b, "%08X", uint32_t(hr));
    gStatus = "off: " + what + " failed (hr=0x" + b + ")";
    gBroken = true;
    ll_shim::error("[RF] lyrics pass " + gStatus);
}
D3D12_CPU_DESCRIPTOR_HANDLE srvCpu(UINT i) { auto h = gSrvHeap->GetCPUDescriptorHandleForHeapStart(); h.ptr += SIZE_T(i) * gSrvInc; return h; }
D3D12_GPU_DESCRIPTOR_HANDLE srvGpu(UINT i) { auto h = gSrvHeap->GetGPUDescriptorHandleForHeapStart(); h.ptr += UINT64(i) * gSrvInc; return h; }

void barrier(ID3D12GraphicsCommandList* l, ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
    D3D12_RESOURCE_BARRIER x{};
    x.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    x.Transition.pResource   = r;
    x.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    x.Transition.StateBefore = a;
    x.Transition.StateAfter  = b;
    l->ResourceBarrier(1, &x);
}

void nullTextureSrv(D3D12_CPU_DESCRIPTOR_HANDLE h) {
    D3D12_SHADER_RESOURCE_VIEW_DESC s{};
    s.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    s.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    s.Texture2D.MipLevels     = 1;
    gDevice->CreateShaderResourceView(nullptr, &s, h);
}

bool createStatic(ID3D12Device* device) {
    gDevice = device;
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, kTable, 0, 0, 0};
    D3D12_ROOT_PARAMETER   params[2]{};
    params[0].ParameterType    = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants        = {0, 0, 24};
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType    = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable  = {1, &range};
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_STATIC_SAMPLER_DESC smp[2]{};
    for (UINT i = 0; i < 2; ++i) {
        smp[i].Filter           = i == 0 ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
        smp[i].AddressU = smp[i].AddressV = smp[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        smp[i].MaxLOD           = D3D12_FLOAT32_MAX;
        smp[i].ShaderRegister   = i;
        smp[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    D3D12_ROOT_SIGNATURE_DESC rs{2, params, 2, smp, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob>          blob, err;
    HRESULT                   hr = rf::d3d12api::SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    if (FAILED(hr)) { fail("D3D12SerializeRootSignature", hr); return false; }
    if (FAILED(hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&gRoot)))) {
        fail("CreateRootSignature", hr);
        return false;
    }
    D3D12_DESCRIPTOR_HEAP_DESC srv{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kSlots * kTable, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    D3D12_DESCRIPTOR_HEAP_DESC rtv{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 3, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0}; // UI target, blur A, blur B
    if (FAILED(hr = device->CreateDescriptorHeap(&srv, IID_PPV_ARGS(&gSrvHeap)))) { fail("CreateDescriptorHeap(SRV)", hr); return false; }
    if (FAILED(hr = device->CreateDescriptorHeap(&rtv, IID_PPV_ARGS(&gRtvHeap)))) { fail("CreateDescriptorHeap(RTV)", hr); return false; }
    gSrvInc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // glyph ring buffers (upload heap, persistently mapped) + their structured-buffer SRVs (t0 of each slot's table)
    D3D12_HEAP_PROPERTIES up{D3D12_HEAP_TYPE_UPLOAD};
    D3D12_RESOURCE_DESC   bd{};
    bd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width            = UINT64(kCap) * sizeof(BfGlyphV1);
    bd.Height           = 1;
    bd.DepthOrArraySize = 1;
    bd.MipLevels        = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    for (UINT s = 0; s < kSlots; ++s) {
        if (FAILED(hr = device->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                        IID_PPV_ARGS(&gGlyphBuf[s])))) {
            fail("CreateCommittedResource(glyphs)", hr);
            return false;
        }
        D3D12_RANGE none{0, 0};
        void*       p = nullptr;
        gGlyphBuf[s]->Map(0, &none, &p);
        gGlyphMap[s] = static_cast<BfGlyphV1*>(p);
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format                     = DXGI_FORMAT_UNKNOWN;
        sd.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
        sd.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Buffer.NumElements         = kCap;
        sd.Buffer.StructureByteStride = sizeof(BfGlyphV1);
        device->CreateShaderResourceView(gGlyphBuf[s].Get(), &sd, srvCpu(s * kTable));
        for (UINT t = 1; t < kTable; ++t) nullTextureSrv(srvCpu(s * kTable + t));
    }
    return true;
}

bool createPso(D3D12_SHADER_BYTECODE vs, D3D12_SHADER_BYTECODE ps, DXGI_FORMAT fmt, bool blend, ComPtr<ID3D12PipelineState>& out) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = gRoot.Get();
    d.VS             = vs;
    d.PS             = ps;
    auto& rt         = d.BlendState.RenderTarget[0];
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    if (blend) {
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D12_BLEND_ONE;      rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;      rt.BlendOp = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D12_BLEND_ONE; rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA; rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    }
    d.SampleMask                      = UINT_MAX;
    d.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable = TRUE;
    d.PrimitiveTopologyType           = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets                = 1;
    d.RTVFormats[0]                   = fmt;
    d.SampleDesc.Count                = 1;
    HRESULT hr = gDevice->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&out));
    if (FAILED(hr)) { fail("CreateGraphicsPipelineState", hr); return false; }
    return true;
}

bool ensureSized(D3D12_RESOURCE_DESC const& bb) {
#if RF_HAVE_GLYPH_SHADERS
    if (gPsoFmt != bb.Format) {
        gPsoGlyph.Reset();
        gPsoComposite.Reset();
        if (!createPso({shaders::VSGlyph, sizeof shaders::VSGlyph}, {shaders::PSGlyph, sizeof shaders::PSGlyph}, bb.Format, true, gPsoGlyph)) return false;
        if (!createPso({shaders::VSComposite, sizeof shaders::VSComposite}, {shaders::PSComposite, sizeof shaders::PSComposite}, bb.Format, true,
                       gPsoComposite))
            return false;
        gPsoDown.Reset(); gPsoBlurH.Reset(); gPsoBlurV.Reset();
        D3D12_SHADER_BYTECODE const vsFull{shaders::VSComposite, sizeof shaders::VSComposite};
        if (!createPso(vsFull, {shaders::PSDown, sizeof shaders::PSDown}, bb.Format, false, gPsoDown)) return false;
        if (!createPso(vsFull, {shaders::PSBlurH, sizeof shaders::PSBlurH}, bb.Format, false, gPsoBlurH)) return false;
        if (!createPso(vsFull, {shaders::PSBlurV, sizeof shaders::PSBlurV}, bb.Format, false, gPsoBlurV)) return false;
        gPsoFmt = bb.Format;
    }
#else
    fail("glyph shaders not compiled into this build (run tools/gen_glyph.py)", E_NOTIMPL);
    return false;
#endif
    if (gUiTarget && gW == UINT(bb.Width) && gH == bb.Height && gFmt == bb.Format) return true;
    // S2 UI target, same size/format as the back buffer (only bound when "under the HUD" is on)
    gUiReady = false;
    if (gUiTarget) gUiGrave.push_back({gUiTarget, gPresents}); // freed a few Presents later (resize used to leak one)
    for (ComPtr<ID3D12Resource>* r : {std::addressof(gScene), std::addressof(gBlurA), std::addressof(gBlurB)}) // ComPtr overloads &
        if (*r) gUiGrave.push_back({*r, gPresents});
    gUiTarget.Reset();
    gScene.Reset(); gBlurA.Reset(); gBlurB.Reset();
    gW   = UINT(bb.Width);
    gH   = bb.Height;
    gFmt = bb.Format;
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC   d{};
    d.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width            = gW;
    d.Height           = gH;
    d.DepthOrArraySize = 1;
    d.MipLevels        = 1;
    d.Format           = gFmt;
    d.SampleDesc.Count = 1;
    d.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE cv{};
    cv.Format  = gFmt;
    HRESULT hr = gDevice->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_RENDER_TARGET, &cv, IID_PPV_ARGS(&gUiTarget));
    if (FAILED(hr)) { fail("CreateCommittedResource(UI target)", hr); return false; }
    auto rtv = gRtvHeap->GetCPUDescriptorHandleForHeapStart();
    gDevice->CreateRenderTargetView(gUiTarget.Get(), nullptr, rtv);
    gUiRtv = rtv.ptr;
    D3D12_SHADER_RESOURCE_VIEW_DESC s{};
    s.Format                  = gFmt;
    s.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    s.Texture2D.MipLevels     = 1;
    for (UINT slot = 0; slot < kSlots; ++slot) gDevice->CreateShaderResourceView(gUiTarget.Get(), &s, srvCpu(slot * kTable + kUiSrv));

    // glass: frame copy (full size, starts as a shader resource) + quarter-res blur targets (start as render targets)
    d.Flags = D3D12_RESOURCE_FLAG_NONE;
    hr = gDevice->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&gScene));
    if (FAILED(hr)) { fail("CreateCommittedResource(glass frame copy)", hr); return false; }
    gQW     = std::max(1u, (gW + 3) / 4);
    gQH     = std::max(1u, (gH + 3) / 4);
    d.Width  = gQW;
    d.Height = gQH;
    d.Flags  = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    for (ComPtr<ID3D12Resource>* r : {std::addressof(gBlurA), std::addressof(gBlurB)}) {
        hr = gDevice->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_RENDER_TARGET, &cv, IID_PPV_ARGS(r->ReleaseAndGetAddressOf()));
        if (FAILED(hr)) { fail("CreateCommittedResource(glass blur)", hr); return false; }
    }
    UINT const rtvInc = gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    gRtvA = gRtvHeap->GetCPUDescriptorHandleForHeapStart();
    gRtvA.ptr += rtvInc;
    gRtvB = gRtvA;
    gRtvB.ptr += rtvInc;
    gDevice->CreateRenderTargetView(gBlurA.Get(), nullptr, gRtvA);
    gDevice->CreateRenderTargetView(gBlurB.Get(), nullptr, gRtvB);
    for (UINT slot = 0; slot < kSlots; ++slot) {
        gDevice->CreateShaderResourceView(gScene.Get(), &s, srvCpu(slot * kTable + kSceneSrv));
        gDevice->CreateShaderResourceView(gBlurA.Get(), &s, srvCpu(slot * kTable + kBlurSrv));
        gDevice->CreateShaderResourceView(gBlurB.Get(), &s, srvCpu(slot * kTable + kBlurSrv + 1));
    }
    gUiReady = true;
    return true;
}

// Create atlas textures from decoded pixels and record their upload on `list`.
void uploadAtlases(ID3D12GraphicsCommandList* list) {
    std::vector<Decoded> dec;
    uint32_t             gen;
    {
        std::lock_guard l(gDecMutex);
        if (gDecodedGen == gUploadedGen) return;
        dec = std::move(gDecoded);
        gen = gDecodedGen;
        gDecoded.clear();
    }
    gGraveyard.clear(); // previous staging buffers are long done by the time the atlases change again
    for (auto& a : gAtlas)
        if (a) gGraveyard.push_back(a), a.Reset();
    for (UINT i = 0; i < dec.size() && i < kMaxAtlas; ++i) {
        auto const& d = dec[i];
        if (!d.w || !d.h) continue;
        D3D12_HEAP_PROPERTIES def{D3D12_HEAP_TYPE_DEFAULT}, up{D3D12_HEAP_TYPE_UPLOAD};
        D3D12_RESOURCE_DESC   td{};
        td.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width            = d.w;
        td.Height           = d.h;
        td.DepthOrArraySize = 1;
        td.MipLevels        = 1;
        td.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        if (FAILED(gDevice->CreateCommittedResource(&def, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&gAtlas[i]))))
            continue;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT64                             total = 0;
        gDevice->GetCopyableFootprints(&td, 0, 1, 0, &fp, nullptr, nullptr, &total);
        D3D12_RESOURCE_DESC bd{};
        bd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width            = total;
        bd.Height           = 1;
        bd.DepthOrArraySize = 1;
        bd.MipLevels        = 1;
        bd.SampleDesc.Count = 1;
        bd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> staging;
        if (FAILED(gDevice->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&staging)))) {
            gAtlas[i].Reset();
            continue;
        }
        uint8_t* p = nullptr;
        staging->Map(0, nullptr, reinterpret_cast<void**>(&p));
        for (UINT y = 0; y < d.h; ++y) std::memcpy(p + fp.Offset + UINT64(y) * fp.Footprint.RowPitch, d.rgba.data() + size_t(y) * d.w * 4, size_t(d.w) * 4);
        staging->Unmap(0, nullptr);
        D3D12_TEXTURE_COPY_LOCATION dst{gAtlas[i].Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
        dst.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION src{staging.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
        src.PlacedFootprint = fp;
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        barrier(list, gAtlas[i].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        gGraveyard.push_back(staging);
        D3D12_SHADER_RESOURCE_VIEW_DESC s{};
        s.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
        s.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        s.Texture2D.MipLevels     = 1;
        (void)s; // descriptors: written per ring slot in record() (gSlotGen)
    }
    gUploadedGen = gen;
}

// ---------------------------------------------------------------- camera (spike S1)
struct Cam {
    double pos[3];
    float  r[3], u[3], f[3];
    float  proj[16];
    bool   fromHook;
};

void perspectiveRhZo(float fovYDeg, float aspect, float zn, float zf, float out[16]) {
    std::memset(out, 0, sizeof(float) * 16);
    float t  = std::tan(fovYDeg * 0.5f * 3.14159265f / 180.0f);
    out[0]   = 1.0f / (aspect * t);
    out[5]   = 1.0f / t;
    out[10]  = zf / (zn - zf);
    out[11]  = -1.0f;
    out[14]  = zn * zf / (zn - zf);
}

Cam pickCamera(BfLyricFrameV1 const& hdr, float aspect) {
    Cam        c{};
    CameraSnap s   = camera();
    int64_t    now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (gUi.camSource == 0 && s.valid && now - s.atMs < 250) {
        std::memcpy(c.pos, s.pos, sizeof c.pos);
        std::memcpy(c.r, s.right, sizeof c.r);
        std::memcpy(c.u, s.up, sizeof c.u);
        std::memcpy(c.f, s.forward, sizeof c.f);
        std::memcpy(c.proj, s.proj, sizeof c.proj);
        c.fromHook  = true;
        gCamStatus  = "game camera (hook), fov " + std::to_string(int(s.fovYDeg + 0.5f));
        return c;
    }
    // BF estimate: eye = origin, Minecraft yaw/pitch (degrees; yaw 0 = +Z, pitch + = down)
    float y = hdr.camYaw * 3.14159265f / 180.0f, p = hdr.camPitch * 3.14159265f / 180.0f;
    c.pos[0] = hdr.origin[0]; c.pos[1] = hdr.origin[1]; c.pos[2] = hdr.origin[2];
    c.f[0] = -std::sin(y) * std::cos(p); c.f[1] = -std::sin(p); c.f[2] = std::cos(y) * std::cos(p);
    c.r[0] = -std::cos(y); c.r[1] = 0; c.r[2] = -std::sin(y);
    c.u[0] = c.r[1] * c.f[2] - c.r[2] * c.f[1]; c.u[1] = c.r[2] * c.f[0] - c.r[0] * c.f[2]; c.u[2] = c.r[0] * c.f[1] - c.r[1] * c.f[0];
    perspectiveRhZo(hdr.camFovY > 1 ? hdr.camFovY : 70.0f, aspect, 0.05f, 1024.0f, c.proj);
    c.fromHook = false;
    gCamStatus = s.valid ? "BF estimate (hook stale)" : "BF estimate (no hook data)";
    return c;
}

// viewProj (column-major) = P * V, V = rows {right, up, -forward} (glm right-handed view, camera at the origin)
void viewProj(Cam const& c, bool flip, float out[16]) {
    float V[4][4] = {{c.r[0], c.r[1], c.r[2], 0}, {c.u[0], c.u[1], c.u[2], 0}, {-c.f[0], -c.f[1], -c.f[2], 0}, {0, 0, 0, 1}};
    if (flip)
        for (int k = 0; k < 3; ++k) V[2][k] = -V[2][k];
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += c.proj[k * 4 + row] * V[k][col];
            out[col * 4 + row] = s;
        }
}

/// Copies this Present's glyphs from BF into gHost (both renderers). draw = something to draw; gStatus says why not.
uint32_t fetchFrame(bool& draw) {
    gHdr          = {};
    gHdr.size     = sizeof(BfLyricFrameV1); // BF copies at most this much (16-atlas header)
    uint32_t n    = 0;
    auto     copy = bfCopy();
    if (copy) n = copy(&gHdr, gHost.data(), kCap);
    for (uint32_t i = 0; i < n; ++i) // the shader has kMaxAtlas texture slots: an index past them would sample t9
        if (gHost[i].atlas >= kMaxAtlas && !(gHost[i].flags & BF_GLYPH_SOLID)) gHost[i].alpha = 0.0f, gHost[i].atlas = 0;
    // BF only publishes while the HUD screen renders (in a world). If the frame counter stops moving (main menu,
    // loading screen, world left) the last frame would stay on screen forever: treat it as inactive after ~0.5 s.
    static uint32_t lastFrame = 0;
    static int      stalePresents = 0;
    if (gHdr.frame != lastFrame) { lastFrame = gHdr.frame; stalePresents = 0; }
    else if (stalePresents < 1000) ++stalePresents;
    bool const fresh = stalePresents < 30;
    draw             = copy && gHdr.active && n > 0 && fresh;
    setCameraWanted(copy && gHdr.active && fresh);
    if (!draw)
        gStatus = !copy ? "waiting for Bedrock Forge"
                : !fresh ? "paused (BF stopped publishing: menu / loading / no world)"
                : (gHdr.active ? "no lyrics on screen" : "idle (BF lyricsflow module off / nothing playing)");
    if (copy && gHdr.atlasGeneration != gRequestedGen && gHdr.atlasCount > 0 && startDecode(gHdr)) gRequestedGen = gHdr.atlasGeneration;
    return n;
}

/// The 24 shader constants (viewProj, origin - camera, time, aspect, opacity, debug, screen height).
void frameConstants(UINT w, UINT h, float c[24]) {
    float aspect = h ? float(w) / float(h) : 16.0f / 9.0f;
    Cam   cam    = pickCamera(gHdr, aspect);
    viewProj(cam, gUi.flipForward, c);
    c[16] = float(gHdr.origin[0] - cam.pos[0]);
    c[17] = float(gHdr.origin[1] - cam.pos[1]);
    c[18] = float(gHdr.origin[2] - cam.pos[2]);
    c[19] = float(double(GetTickCount64() % 3600000ull) / 1000.0);
    c[20] = aspect;
    c[21] = gUi.opacity;
    c[22] = gUi.debugBounds ? 1.0f : 0.0f;
    c[23] = float(h);
}
} // namespace

// ---------------------------------------------------------------- UiCapture tie-in (S2)
bool   wantRedirect() { return gUi.enabled && gUi.underHud && RfOptions::uiCaptureSpike && gUiReady.load() && !gBroken && !rf::panel::enabled(); }
size_t targetRtv() { return gUiRtv.load(); }
void   noteRedirected() { gRedirected.store(true, std::memory_order_relaxed); }
bool   tracking() { return gUi.enabled && gUi.underHud; }

// ---------------------------------------------------------------- Present
void record(ID3D12Device* device, ID3D12GraphicsCommandList* list, ID3D12Resource* bb, D3D12_CPU_DESCRIPTOR_HANDLE const& bbRtv, unsigned slotIn) {
    bool const redirected = gRedirected.exchange(false);
    ++gPresents;
    while (!gUiGrave.empty() && gPresents - gUiGrave.front().second > kSlots + 2) gUiGrave.erase(gUiGrave.begin());
    if (gBroken || !gUi.enabled || rf::panel::enabled()) { setCameraWanted(false); return; }
    UINT const slot = slotIn % kSlots;

    bool           draw = false;
    uint32_t const n    = fetchFrame(draw);
    auto const     copy = bfCopy();
    if (!draw && !redirected) return;
    if (!gRoot && !createStatic(device)) return;
    D3D12_RESOURCE_DESC bd = bb->GetDesc();
    if (!ensureSized(bd)) return;
    (void)copy;
    uploadAtlases(list);
    // Atlas descriptors are written per ring slot, only for the slot being recorded now (the other slots' tables
    // may still be read by frames in flight). Missing / failed atlases get a null SRV (never a stale pointer).
    if (gSlotGen[slot] != gUploadedGen) {
        for (UINT i = 0; i < kMaxAtlas; ++i) {
            if (gAtlas[i]) {
                D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
                sv.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
                sv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
                sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                sv.Texture2D.MipLevels     = 1;
                gDevice->CreateShaderResourceView(gAtlas[i].Get(), &sv, srvCpu(slot * kTable + 1 + i));
            } else nullTextureSrv(srvCpu(slot * kTable + 1 + i));
        }
        gSlotGen[slot] = gUploadedGen;
    }

    D3D12_VIEWPORT vp{0, 0, float(gW), float(gH), 0, 1};
    D3D12_RECT     sc{0, 0, LONG(gW), LONG(gH)};
    list->RSSetViewports(1, &vp);
    list->RSSetScissorRects(1, &sc);
    list->OMSetRenderTargets(1, &bbRtv, FALSE, nullptr);
    list->SetGraphicsRootSignature(gRoot.Get());
    ID3D12DescriptorHeap* heaps[] = {gSrvHeap.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootDescriptorTable(1, srvGpu(slot * kTable));
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    bool glass = false;
    if (draw)
        for (uint32_t i = 0; i < n && !glass; ++i) glass = (gHost[i].flags & (BF_GLYPH_GLASS | BF_GLYPH_BACKDROP)) != 0;
    gGlassUsed = glass;
    if (glass) { // copy the frame, blur it at quarter res (two gaussian rounds), then go back to the back buffer
        barrier(list, bb, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        barrier(list, gScene.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyResource(gScene.Get(), bb);
        barrier(list, gScene.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        barrier(list, bb, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        D3D12_VIEWPORT qvp{0, 0, float(gQW), float(gQH), 0, 1};
        D3D12_RECT     qsc{0, 0, LONG(gQW), LONG(gQH)};
        list->RSSetViewports(1, &qvp);
        list->RSSetScissorRects(1, &qsc);
        float zero[24]{};
        list->SetGraphicsRoot32BitConstants(0, 24, zero, 0);
        auto pass = [&](ID3D12PipelineState* pso, D3D12_CPU_DESCRIPTOR_HANDLE rtv) {
            list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            list->SetPipelineState(pso);
            list->DrawInstanced(3, 1, 0, 0);
        };
        auto const RT = D3D12_RESOURCE_STATE_RENDER_TARGET, SR = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        pass(gPsoDown.Get(), gRtvA);                       // A = frame / 4
        barrier(list, gBlurA.Get(), RT, SR);
        for (int round = 0; round < 2; ++round) {
            pass(gPsoBlurH.Get(), gRtvB);                  // B = blurH(A)
            barrier(list, gBlurB.Get(), RT, SR);
            barrier(list, gBlurA.Get(), SR, RT);
            pass(gPsoBlurV.Get(), gRtvA);                  // A = blurV(B)
            barrier(list, gBlurA.Get(), RT, SR);
            barrier(list, gBlurB.Get(), SR, RT);
        }
        list->RSSetViewports(1, &vp);
        list->RSSetScissorRects(1, &sc);
        list->OMSetRenderTargets(1, &bbRtv, FALSE, nullptr);
    }

    if (draw) {
        std::memcpy(gGlyphMap[slot], gHost.data(), size_t(n) * sizeof(BfGlyphV1));
        float c[24]{};
        frameConstants(gW, gH, c);
        list->SetGraphicsRoot32BitConstants(0, 24, c, 0);
        list->SetPipelineState(gPsoGlyph.Get());
        list->DrawInstanced(6, n, 0, 0);
        if (glass) barrier(list, gBlurA.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        gStatus = std::to_string(n) + " glyphs" + (gHdr.total > n ? " (truncated from " + std::to_string(gHdr.total) + ")" : "");
    }
    if (redirected) { // S2: the game's UI went to our target this frame -> put it back on top
        float c[24]{};
        list->SetGraphicsRoot32BitConstants(0, 24, c, 0);
        barrier(list, gUiTarget.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        list->SetPipelineState(gPsoComposite.Get());
        list->DrawInstanced(3, 1, 0, 0);
        barrier(list, gUiTarget.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
}

// ---------------------------------------------------------------- Direct3D 11 (game on D3D11, e.g. under Blueberry)
namespace {
struct D11 {
    ComPtr<ID3D11Device>             dev;
    ComPtr<ID3D11VertexShader>       vsGlyph, vsFull;
    ComPtr<ID3D11PixelShader>        psGlyph, psDown, psBlurH, psBlurV;
    ComPtr<ID3D11Buffer>             cb, glyphs;
    ComPtr<ID3D11ShaderResourceView> glyphSrv;
    ComPtr<ID3D11Texture2D>          atlas[kMaxAtlas];
    ComPtr<ID3D11ShaderResourceView> atlasSrv[kMaxAtlas];
    ComPtr<ID3D11BlendState>         premul, opaque;
    ComPtr<ID3D11RasterizerState>    raster;
    ComPtr<ID3D11DepthStencilState>  noDepth;
    ComPtr<ID3D11SamplerState>       lin, point;
    // glass: copy of the frame + quarter-res blur ping-pong (A = final)
    ComPtr<ID3D11Texture2D>          scene, blurA, blurB;
    ComPtr<ID3D11ShaderResourceView> sceneSrv, blurASrv, blurBSrv;
    ComPtr<ID3D11RenderTargetView>   blurARtv, blurBRtv;
    UINT                             w = 0, h = 0, qw = 0, qh = 0;
    DXGI_FORMAT                      fmt = DXGI_FORMAT_UNKNOWN;
    bool                             ready = false;
} g11;

DXGI_FORMAT viewFormat(DXGI_FORMAT f) { // swap chain buffers can be typeless; views need a concrete format
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return f;
    }
}

bool create11(ID3D11Device* dev) {
#if RF_HAVE_GLYPH_SHADERS11
    g11.dev = dev;
    HRESULT hr;
    if (FAILED(hr = dev->CreateVertexShader(shaders::VSGlyph11, sizeof shaders::VSGlyph11, nullptr, &g11.vsGlyph))) { fail("D3D11 VSGlyph", hr); return false; }
    if (FAILED(hr = dev->CreateVertexShader(shaders::VSComposite11, sizeof shaders::VSComposite11, nullptr, &g11.vsFull))) { fail("D3D11 VSComposite", hr); return false; }
    if (FAILED(hr = dev->CreatePixelShader(shaders::PSGlyph11, sizeof shaders::PSGlyph11, nullptr, &g11.psGlyph))) { fail("D3D11 PSGlyph", hr); return false; }
    if (FAILED(hr = dev->CreatePixelShader(shaders::PSDown11, sizeof shaders::PSDown11, nullptr, &g11.psDown))) { fail("D3D11 PSDown", hr); return false; }
    if (FAILED(hr = dev->CreatePixelShader(shaders::PSBlurH11, sizeof shaders::PSBlurH11, nullptr, &g11.psBlurH))) { fail("D3D11 PSBlurH", hr); return false; }
    if (FAILED(hr = dev->CreatePixelShader(shaders::PSBlurV11, sizeof shaders::PSBlurV11, nullptr, &g11.psBlurV))) { fail("D3D11 PSBlurV", hr); return false; }
    D3D11_BUFFER_DESC cbd{96, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
    if (FAILED(hr = dev->CreateBuffer(&cbd, nullptr, &g11.cb))) { fail("D3D11 constant buffer", hr); return false; }
    D3D11_BUFFER_DESC gbd{UINT(kCap * sizeof(BfGlyphV1)), D3D11_USAGE_DYNAMIC, D3D11_BIND_SHADER_RESOURCE, D3D11_CPU_ACCESS_WRITE,
                          D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, UINT(sizeof(BfGlyphV1))};
    if (FAILED(hr = dev->CreateBuffer(&gbd, nullptr, &g11.glyphs))) { fail("D3D11 glyph buffer", hr); return false; }
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format              = DXGI_FORMAT_UNKNOWN;
    sv.ViewDimension       = D3D11_SRV_DIMENSION_BUFFER;
    sv.Buffer.FirstElement = 0;
    sv.Buffer.NumElements  = kCap;
    if (FAILED(hr = dev->CreateShaderResourceView(g11.glyphs.Get(), &sv, &g11.glyphSrv))) { fail("D3D11 glyph SRV", hr); return false; }
    D3D11_BLEND_DESC bd{};
    auto& rt                 = bd.RenderTarget[0];
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    dev->CreateBlendState(&bd, &g11.opaque);
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D11_BLEND_ONE;      rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;      rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D11_BLEND_ONE; rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    dev->CreateBlendState(&bd, &g11.premul);
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode        = D3D11_FILL_SOLID;
    rd.CullMode        = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    dev->CreateRasterizerState(&rd, &g11.raster);
    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = FALSE;
    dev->CreateDepthStencilState(&dd, &g11.noDepth);
    D3D11_SAMPLER_DESC sd{};
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD   = D3D11_FLOAT32_MAX;
    sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    dev->CreateSamplerState(&sd, &g11.lin);
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    dev->CreateSamplerState(&sd, &g11.point);
    if (!g11.premul || !g11.opaque || !g11.raster || !g11.noDepth || !g11.lin || !g11.point) { fail("D3D11 state objects", E_FAIL); return false; }
    g11.ready = true;
    return true;
#else
    (void)dev;
    fail("D3D11 glyph shaders not compiled into this build (run tools/gen_glyph.py with fxc)", E_NOTIMPL);
    return false;
#endif
}

bool sized11(D3D11_TEXTURE2D_DESC const& bb) {
    if (g11.scene && g11.w == bb.Width && g11.h == bb.Height && g11.fmt == bb.Format) return true;
    g11.scene.Reset(); g11.blurA.Reset(); g11.blurB.Reset();
    g11.sceneSrv.Reset(); g11.blurASrv.Reset(); g11.blurBSrv.Reset(); g11.blurARtv.Reset(); g11.blurBRtv.Reset();
    g11.w = bb.Width; g11.h = bb.Height; g11.fmt = bb.Format;
    D3D11_TEXTURE2D_DESC d{};
    d.Width = g11.w; d.Height = g11.h; d.MipLevels = 1; d.ArraySize = 1; d.Format = bb.Format; d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    HRESULT hr = g11.dev->CreateTexture2D(&d, nullptr, &g11.scene);
    if (FAILED(hr)) { fail("D3D11 glass frame copy", hr); return false; }
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = viewFormat(bb.Format); sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1;
    g11.dev->CreateShaderResourceView(g11.scene.Get(), &sv, &g11.sceneSrv);
    g11.qw = std::max(1u, (g11.w + 3) / 4);
    g11.qh = std::max(1u, (g11.h + 3) / 4);
    d.Width = g11.qw; d.Height = g11.qh; d.Format = viewFormat(bb.Format);
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    if (FAILED(hr = g11.dev->CreateTexture2D(&d, nullptr, &g11.blurA)) || FAILED(hr = g11.dev->CreateTexture2D(&d, nullptr, &g11.blurB))) {
        fail("D3D11 glass blur", hr);
        return false;
    }
    g11.dev->CreateShaderResourceView(g11.blurA.Get(), nullptr, &g11.blurASrv);
    g11.dev->CreateShaderResourceView(g11.blurB.Get(), nullptr, &g11.blurBSrv);
    g11.dev->CreateRenderTargetView(g11.blurA.Get(), nullptr, &g11.blurARtv);
    g11.dev->CreateRenderTargetView(g11.blurB.Get(), nullptr, &g11.blurBRtv);
    return g11.sceneSrv && g11.blurASrv && g11.blurBSrv && g11.blurARtv && g11.blurBRtv;
}

void upload11() {
    std::vector<Decoded> dec;
    uint32_t             gen;
    {
        std::lock_guard l(gDecMutex);
        if (gDecodedGen == gUploadedGen) return;
        dec = std::move(gDecoded);
        gen = gDecodedGen;
        gDecoded.clear();
    }
    for (UINT i = 0; i < kMaxAtlas; ++i) {
        g11.atlas[i].Reset();
        g11.atlasSrv[i].Reset();
        if (i >= dec.size() || !dec[i].w || !dec[i].h) continue;
        D3D11_TEXTURE2D_DESC d{};
        d.Width = dec[i].w; d.Height = dec[i].h; d.MipLevels = 1; d.ArraySize = 1; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{dec[i].rgba.data(), dec[i].w * 4, 0};
        if (FAILED(g11.dev->CreateTexture2D(&d, &init, &g11.atlas[i]))) continue;
        g11.dev->CreateShaderResourceView(g11.atlas[i].Get(), nullptr, &g11.atlasSrv[i]);
    }
    gUploadedGen = gen;
}

/// Everything record11 changes, put back afterwards (the game keeps its own state across Present on D3D11).
struct Saved11 {
    ID3D11DeviceContext*      c;
    ID3D11RenderTargetView*   rtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    ID3D11DepthStencilView*   dsv = nullptr;
    UINT                      nvp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_VIEWPORT            vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    ID3D11RasterizerState*    rs = nullptr;
    ID3D11BlendState*         bs = nullptr;
    float                     bf[4]{};
    UINT                      mask = 0;
    ID3D11DepthStencilState*  ds = nullptr;
    UINT                      ref = 0;
    ID3D11VertexShader*       vs = nullptr;
    ID3D11PixelShader*        ps = nullptr;
    ID3D11GeometryShader*     gs = nullptr;
    ID3D11HullShader*         hs = nullptr;
    ID3D11DomainShader*       dsh = nullptr;
    ID3D11ShaderResourceView* psSrv[kTable]{};
    ID3D11ShaderResourceView* vsSrv = nullptr;
    ID3D11SamplerState*       smp[2]{};
    ID3D11Buffer*             vsCb = nullptr;
    ID3D11Buffer*             psCb = nullptr;
    ID3D11InputLayout*        il = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY  topo{};
    explicit Saved11(ID3D11DeviceContext* ctx) : c(ctx) {
        c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtv, &dsv);
        c->RSGetViewports(&nvp, vp);
        c->RSGetState(&rs);
        c->OMGetBlendState(&bs, bf, &mask);
        c->OMGetDepthStencilState(&ds, &ref);
        c->VSGetShader(&vs, nullptr, nullptr);
        c->PSGetShader(&ps, nullptr, nullptr);
        c->GSGetShader(&gs, nullptr, nullptr);
        c->HSGetShader(&hs, nullptr, nullptr);
        c->DSGetShader(&dsh, nullptr, nullptr);
        c->PSGetShaderResources(0, kTable, psSrv);
        c->VSGetShaderResources(0, 1, &vsSrv);
        c->PSGetSamplers(0, 2, smp);
        c->VSGetConstantBuffers(0, 1, &vsCb);
        c->PSGetConstantBuffers(0, 1, &psCb);
        c->IAGetInputLayout(&il);
        c->IAGetPrimitiveTopology(&topo);
    }
    ~Saved11() {
        c->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtv, dsv);
        c->RSSetViewports(nvp, vp);
        c->RSSetState(rs);
        c->OMSetBlendState(bs, bf, mask);
        c->OMSetDepthStencilState(ds, ref);
        c->VSSetShader(vs, nullptr, 0);
        c->PSSetShader(ps, nullptr, 0);
        c->GSSetShader(gs, nullptr, 0);
        c->HSSetShader(hs, nullptr, 0);
        c->DSSetShader(dsh, nullptr, 0);
        c->PSSetShaderResources(0, kTable, psSrv);
        c->VSSetShaderResources(0, 1, &vsSrv);
        c->PSSetSamplers(0, 2, smp);
        c->VSSetConstantBuffers(0, 1, &vsCb);
        c->PSSetConstantBuffers(0, 1, &psCb);
        c->IASetInputLayout(il);
        c->IASetPrimitiveTopology(topo);
        for (auto* p : rtv) if (p) p->Release();
        for (auto* p : psSrv) if (p) p->Release();
        for (auto* p : smp) if (p) p->Release();
        for (IUnknown* p : std::initializer_list<IUnknown*>{dsv, rs, bs, ds, vs, ps, gs, hs, dsh, vsSrv, vsCb, psCb, il})
            if (p) p->Release();
    }
};
} // namespace

void record11(ID3D11Device* device, ID3D11DeviceContext* ctx, ID3D11RenderTargetView* bbRtv) {
    ++gPresents;
    if (gBroken || !gUi.enabled || !bbRtv) { setCameraWanted(false); return; }
    bool           draw = false;
    uint32_t const n    = fetchFrame(draw);
    if (!draw) return;
    if (!g11.ready && !create11(device)) return;
    ComPtr<ID3D11Resource>  res;
    bbRtv->GetResource(&res);
    ComPtr<ID3D11Texture2D> bb;
    if (!res || FAILED(res.As(&bb))) return;
    D3D11_TEXTURE2D_DESC bd{};
    bb->GetDesc(&bd);
    if (!sized11(bd)) return;
    gW = bd.Width;
    gH = bd.Height;
    upload11();

    Saved11 saved(ctx);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx->Map(g11.glyphs.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    std::memcpy(m.pData, gHost.data(), size_t(n) * sizeof(BfGlyphV1));
    ctx->Unmap(g11.glyphs.Get(), 0);

    ID3D11SamplerState* smp[2] = {g11.lin.Get(), g11.point.Get()};
    ctx->PSSetSamplers(0, 2, smp);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->RSSetState(g11.raster.Get());
    ctx->OMSetDepthStencilState(g11.noDepth.Get(), 0);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    ID3D11ShaderResourceView* none[kTable]{};
    ctx->PSSetShaderResources(0, kTable, none);

    bool glass = false;
    for (uint32_t i = 0; i < n && !glass; ++i) glass = (gHost[i].flags & (BF_GLYPH_GLASS | BF_GLYPH_BACKDROP)) != 0;
    gGlassUsed = glass;
    if (glass) { // copy the frame, blur it at quarter res (two gaussian rounds)
        gQW = g11.qw;
        gQH = g11.qh;
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
        ctx->CopyResource(g11.scene.Get(), bb.Get());
        D3D11_VIEWPORT qvp{0, 0, float(g11.qw), float(g11.qh), 0, 1};
        ctx->RSSetViewports(1, &qvp);
        ctx->OMSetBlendState(g11.opaque.Get(), nullptr, 0xFFFFFFFFu);
        ctx->VSSetShader(g11.vsFull.Get(), nullptr, 0);
        auto pass = [&](ID3D11PixelShader* ps, ID3D11RenderTargetView* rtv, UINT slot, ID3D11ShaderResourceView* src) {
            ID3D11ShaderResourceView* clear[3]{};
            ctx->PSSetShaderResources(kSceneSrv, 3, clear); // never read and write the same texture
            ID3D11RenderTargetView* r[] = {rtv};
            ctx->OMSetRenderTargets(1, r, nullptr);
            ctx->PSSetShaderResources(slot, 1, &src);
            ctx->PSSetShader(ps, nullptr, 0);
            ctx->Draw(3, 0);
        };
        pass(g11.psDown.Get(), g11.blurARtv.Get(), kSceneSrv, g11.sceneSrv.Get()); // A = frame / 4
        for (int round = 0; round < 2; ++round) {
            pass(g11.psBlurH.Get(), g11.blurBRtv.Get(), kBlurSrv, g11.blurASrv.Get());     // B = blurH(A)
            pass(g11.psBlurV.Get(), g11.blurARtv.Get(), kBlurSrv + 1, g11.blurBSrv.Get()); // A = blurV(B)
        }
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
    }

    float c[24]{};
    frameConstants(gW, gH, c);
    D3D11_MAPPED_SUBRESOURCE cm{};
    if (SUCCEEDED(ctx->Map(g11.cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &cm))) {
        std::memcpy(cm.pData, c, sizeof c);
        ctx->Unmap(g11.cb.Get(), 0);
    }
    ID3D11Buffer* cbs[] = {g11.cb.Get()};
    ctx->VSSetConstantBuffers(0, 1, cbs);
    ctx->PSSetConstantBuffers(0, 1, cbs);
    ID3D11ShaderResourceView* srvs[kTable]{};
    srvs[0] = g11.glyphSrv.Get();
    for (UINT i = 0; i < kMaxAtlas; ++i) srvs[1 + i] = g11.atlasSrv[i].Get();
    srvs[kSceneSrv]   = g11.sceneSrv.Get();
    srvs[kBlurSrv]    = g11.blurASrv.Get();
    srvs[kBlurSrv + 1] = g11.blurBSrv.Get();
    ctx->VSSetShaderResources(0, 1, srvs);
    ctx->PSSetShaderResources(0, kTable, srvs);
    D3D11_VIEWPORT vp{0, 0, float(gW), float(gH), 0, 1};
    ctx->RSSetViewports(1, &vp);
    ctx->OMSetRenderTargets(1, &bbRtv, nullptr);
    ctx->OMSetBlendState(g11.premul.Get(), nullptr, 0xFFFFFFFFu);
    ctx->VSSetShader(g11.vsGlyph.Get(), nullptr, 0);
    ctx->PSSetShader(g11.psGlyph.Get(), nullptr, 0);
    ctx->DrawInstanced(6, n, 0, 0);
    ctx->PSSetShaderResources(0, kTable, none); // unbind before the game / ImGui uses these textures again
    gStatus = std::to_string(n) + " glyphs (D3D11)" + (gHdr.total > n ? " (truncated from " + std::to_string(gHdr.total) + ")" : "");
}

void drawUi() {
    if (!ImGui::CollapsingHeader("LyricsFlow (preview)")) return;
    ImGui::TextDisabled("Port of LyricsFlow by 374m - personal build. Text comes from Bedrock Forge's 'lyricsflow' module.");
    ImGui::Checkbox("Draw lyrics", &gUi.enabled);
    ImGui::SliderFloat("Opacity", &gUi.opacity, 0.0f, 1.0f);
    bool canS2 = RfOptions::uiCaptureSpike;
    if (!canS2) ImGui::BeginDisabled();
    ImGui::Checkbox("Under the HUD (spike S2, unverified)", &gUi.underHud);
    if (!canS2) {
        ImGui::EndDisabled();
        ImGui::TextDisabled("needs \"uiCaptureSpike\": true in RendererFoundation.json");
    }
    ImGui::RadioButton("Camera: game hook (S1)", &gUi.camSource, 0);
    ImGui::SameLine();
    ImGui::RadioButton("BF estimate", &gUi.camSource, 1);
    ImGui::Checkbox("Flip forward axis (S1 check)", &gUi.flipForward);
    ImGui::Checkbox("Show quad bounds", &gUi.debugBounds);
    ImGui::Separator();
    ImGui::Text("Status: %s", gStatus.c_str());
    ImGui::Text("Camera: %s", gCamStatus.c_str());
    ImGui::Text("Atlases: %s (gen %u)", gAtlasStatus.c_str(), gUploadedGen);
    ImGui::Text("Glass: %s (blur %ux%u)", gGlassUsed ? "drawing" : "idle", gQW, gQH);
    ImGui::Text("BF frame %u, active %u, %u/%u glyphs", gHdr.frame, gHdr.active, gHdr.count, gHdr.total);
    CameraSnap s = camera();
    ImGui::Text("Camera hook: %llu calls, %llu world", (unsigned long long)s.calls, (unsigned long long)s.chosen);
}

void install() { installCamera(); }
void uninstall() { uninstallCamera(); }

} // namespace rf::lyrics
