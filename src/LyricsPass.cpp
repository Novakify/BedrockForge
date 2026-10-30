// Renderer Foundation - LyricsFlow port: lyric glyph pass (see LyricsPass.h). Original code for Renderer Foundation.
// STAGED, NOT RUN IN GAME YET: compile-verified by CI only. Mirrors SpatialPanel's D3D12 patterns (root constants +
// SRV table, premultiplied blend, PSO per back-buffer format, graveyard for resources in flight).
#include "LyricsPass.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
constexpr UINT     kTable     = 2 + kBfLyricsMaxAtlases; // t0 glyphs, t1..t16 atlases, t17 UI target
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
ComPtr<ID3D12PipelineState>  gPsoGlyph, gPsoComposite;
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
    HRESULT                   hr = D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    if (FAILED(hr)) { fail("D3D12SerializeRootSignature", hr); return false; }
    if (FAILED(hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&gRoot)))) {
        fail("CreateRootSignature", hr);
        return false;
    }
    D3D12_DESCRIPTOR_HEAP_DESC srv{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kSlots * kTable, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    D3D12_DESCRIPTOR_HEAP_DESC rtv{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
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
    gUiTarget.Reset();
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
    for (UINT slot = 0; slot < kSlots; ++slot) gDevice->CreateShaderResourceView(gUiTarget.Get(), &s, srvCpu(slot * kTable + kTable - 1));
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
    bool const draw  = copy && gHdr.active && n > 0 && fresh;
    setCameraWanted(copy && gHdr.active && fresh);
    if (!draw && !redirected) {
        gStatus = !copy ? "waiting for Bedrock Forge"
                : !fresh ? "paused (BF stopped publishing: menu / loading / no world)"
                : (gHdr.active ? "no lyrics on screen" : "idle (BF lyricsflow module off / nothing playing)");
        return;
    }
    if (!gRoot && !createStatic(device)) return;
    D3D12_RESOURCE_DESC bd = bb->GetDesc();
    if (!ensureSized(bd)) return;
    if (copy && gHdr.atlasGeneration != gRequestedGen && gHdr.atlasCount > 0 && startDecode(gHdr)) gRequestedGen = gHdr.atlasGeneration;
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

    if (draw) {
        std::memcpy(gGlyphMap[slot], gHost.data(), size_t(n) * sizeof(BfGlyphV1));
        float aspect = gH ? float(gW) / float(gH) : 16.0f / 9.0f;
        Cam   cam    = pickCamera(gHdr, aspect);
        float c[24]{};
        viewProj(cam, gUi.flipForward, c);
        c[16] = float(gHdr.origin[0] - cam.pos[0]);
        c[17] = float(gHdr.origin[1] - cam.pos[1]);
        c[18] = float(gHdr.origin[2] - cam.pos[2]);
        c[19] = float(double(GetTickCount64() % 3600000ull) / 1000.0);
        c[20] = aspect;
        c[21] = gUi.opacity;
        c[22] = gUi.debugBounds ? 1.0f : 0.0f;
        list->SetGraphicsRoot32BitConstants(0, 24, c, 0);
        list->SetPipelineState(gPsoGlyph.Get());
        list->DrawInstanced(6, n, 0, 0);
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
    ImGui::Text("BF frame %u, active %u, %u/%u glyphs", gHdr.frame, gHdr.active, gHdr.count, gHdr.total);
    CameraSnap s = camera();
    ImGui::Text("Camera hook: %llu calls, %llu world", (unsigned long long)s.calls, (unsigned long long)s.chosen);
}

void install() { installCamera(); }
void uninstall() { uninstallCamera(); }

} // namespace rf::lyrics
