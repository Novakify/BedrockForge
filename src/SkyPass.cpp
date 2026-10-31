// Renderer Foundation - LyricsFlow port: live sky preview pass (see SkyPass.h). Original code for Renderer Foundation.
// STAGED, NOT RUN IN GAME YET: compile-verified by CI only. Same D3D12 patterns as LyricsPass (root constants + SRV
// table per ring slot, premultiplied blend, PSO per back-buffer format, WIC decode on a worker thread).
#include "SkyPass.h"

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
#include <unordered_map>

#if __has_include("SkyShaders.h")
#include "SkyShaders.h"
#endif

namespace rf::sky {
namespace {
using Microsoft::WRL::ComPtr;

constexpr UINT kSlots    = 4;
constexpr UINT kMaxVerts = 4 * 6000;
constexpr UINT kTable    = 4; // t0 params, t1 sprites, t2 picture, t3 verts
constexpr UINT kVertOff  = 256; // bytes: params row at 0, vertices from here (multiple of 32)

struct Ui {
    bool  enabled    = false; // preview off until switched on in F6
    int   where      = 0;     // 0 corner window, 1 full screen
    float opacity    = 1.0f;
    float fovY       = 70.0f;
    bool  layers     = true;
    bool  replace    = true;  // stage 2: draw in place of Bedrock's sky
    bool  hideClouds = true;  // WorldSky hides vanilla clouds by default
} gUi;
std::atomic_bool gActive{false};  // BF publishes an active sky (updated every Present)

BF_CopySkyFrame_t bfCopy() {
    static BF_CopySkyFrame_t fn = nullptr;
    static int               delay = 0;
    if (fn || delay-- > 0) return fn;
    delay = 240;
    if (HMODULE m = GetModuleHandleW(L"BedrockForgeCore.dll"))
        fn = reinterpret_cast<BF_CopySkyFrame_t>(reinterpret_cast<void*>(GetProcAddress(m, "BF_CopySkyFrame")));
    return fn;
}

// ---------------------------------------------------------------- images (worker thread, WIC)
struct Decoded {
    UINT                 w = 0, h = 0;
    std::vector<uint8_t> rgba;
};
std::mutex       gDecMutex;
Decoded          gDecSprites, gDecPicture;
uint32_t         gDecodedGen = 0, gRequestedGen = 0, gUploadedGen = 0;
std::atomic_bool gDecoding{false};
std::string      gImgStatus = "none";

std::wstring wide(char const* s) {
    int          n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(size_t(std::max(n, 1)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
    w.resize(wcslen(w.c_str()));
    return w;
}

bool decode(std::wstring const& path, Decoded& out, UINT maxW) {
    if (path.empty()) return false;
    ComPtr<IWICImagingFactory> f;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return false;
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec))) return false;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(dec->GetFrame(0, &frame))) return false;
    UINT w = 0, h = 0;
    frame->GetSize(&w, &h);
    ComPtr<IWICBitmapSource> src = frame;
    if (maxW && w > maxW) { // SkyImage: width clamped to 4096 (2:1)
        ComPtr<IWICBitmapScaler> sc;
        if (FAILED(f->CreateBitmapScaler(&sc)) || FAILED(sc->Initialize(frame.Get(), maxW, maxW / 2, WICBitmapInterpolationModeFant))) return false;
        src = sc;
    }
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(f->CreateFormatConverter(&conv))) return false;
    if (FAILED(conv->Initialize(src.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) return false;
    conv->GetSize(&out.w, &out.h);
    out.rgba.resize(size_t(out.w) * out.h * 4);
    return SUCCEEDED(conv->CopyPixels(nullptr, out.w * 4, UINT(out.rgba.size()), out.rgba.data()));
}

void startDecode(BfSkyFrameV1 const& h) {
    if (gDecoding.exchange(true)) return;
    std::wstring sp = wide(h.spritesFile), pic = wide(h.pictureFile);
    uint32_t     gen = h.imageGeneration;
    gRequestedGen    = gen;
    std::thread([sp, pic, gen] {
        HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Decoded s, p;
        bool    okS = decode(sp, s, 0), okP = decode(pic, p, 4096);
        if (SUCCEEDED(co)) CoUninitialize();
        std::lock_guard l(gDecMutex);
        gDecSprites = std::move(s);
        gDecPicture = std::move(p);
        gDecodedGen = gen;
        gImgStatus  = std::string("sprites ") + (okS ? "ok" : "missing") + ", picture " + (pic.empty() ? "-" : okP ? "ok" : "failed");
        gDecoding   = false;
    }).detach();
}

// ---------------------------------------------------------------- D3D12 (overlay thread)
ComPtr<ID3D12Device>         gDevice;
ComPtr<ID3D12RootSignature>  gRoot;
ComPtr<ID3D12PipelineState>  gPsoDome, gPsoQuad;
DXGI_FORMAT                  gPsoFmt = DXGI_FORMAT_UNKNOWN;
ComPtr<ID3D12DescriptorHeap> gHeap;
UINT                         gInc = 0;
ComPtr<ID3D12Resource>       gBuf[kSlots];
uint8_t*                     gMap[kSlots]{};
ComPtr<ID3D12Resource>       gSprites, gPicture;
std::vector<ComPtr<ID3D12Resource>> gGrave;
uint32_t                     gSlotGen[kSlots] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
bool                         gBroken = false;
std::string                  gStatus = "off (F6 to preview)";
BfSkyFrameV1                 gHdr{};
std::vector<BfSkyVertV1>     gHost(kMaxVerts);
std::mutex                   gStage2Mutex;
std::string                  gStage2 = "waiting for a sky (pick one in the L panel)";
void setStage2(std::string v) { std::lock_guard l(gStage2Mutex); gStage2 = std::move(v); }
std::string stage2() { std::lock_guard l(gStage2Mutex); return gStage2; }

void fail(std::string const& what, HRESULT hr) {
    char b[16];
    snprintf(b, sizeof b, "%08X", uint32_t(hr));
    gStatus = "off: " + what + " failed (hr=0x" + b + ")";
    gBroken = true;
    ll_shim::error("[RF] sky pass " + gStatus);
}
D3D12_CPU_DESCRIPTOR_HANDLE cpu(UINT i) { auto h = gHeap->GetCPUDescriptorHandleForHeapStart(); h.ptr += SIZE_T(i) * gInc; return h; }
D3D12_GPU_DESCRIPTOR_HANDLE gpu(UINT i) { auto h = gHeap->GetGPUDescriptorHandleForHeapStart(); h.ptr += UINT64(i) * gInc; return h; }

void nullTex(D3D12_CPU_DESCRIPTOR_HANDLE h) {
    D3D12_SHADER_RESOURCE_VIEW_DESC s{};
    s.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    s.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    s.Texture2D.MipLevels     = 1;
    gDevice->CreateShaderResourceView(nullptr, &s, h);
}
void texSrv(ID3D12Resource* r, D3D12_CPU_DESCRIPTOR_HANDLE h) {
    if (!r) { nullTex(h); return; }
    D3D12_SHADER_RESOURCE_VIEW_DESC s{};
    s.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    s.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    s.Texture2D.MipLevels     = 1;
    gDevice->CreateShaderResourceView(r, &s, h);
}

bool createStatic(ID3D12Device* device) {
    gDevice = device;
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, kTable, 0, 0, 0};
    D3D12_ROOT_PARAMETER   params[2]{};
    params[0].ParameterType    = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants        = {0, 0, 20};
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType    = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable  = {1, &range};
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_STATIC_SAMPLER_DESC smp[2]{};
    for (UINT i = 0; i < 2; ++i) {
        smp[i].Filter   = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        smp[i].AddressU = i == 1 ? D3D12_TEXTURE_ADDRESS_MODE_WRAP : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        smp[i].AddressV = smp[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
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
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kSlots * kTable, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    if (FAILED(hr = device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&gHeap)))) { fail("CreateDescriptorHeap", hr); return false; }
    gInc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_HEAP_PROPERTIES up{D3D12_HEAP_TYPE_UPLOAD};
    D3D12_RESOURCE_DESC   bd{};
    bd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width            = kVertOff + UINT64(kMaxVerts) * sizeof(BfSkyVertV1);
    bd.Height           = 1;
    bd.DepthOrArraySize = 1;
    bd.MipLevels        = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    for (UINT s = 0; s < kSlots; ++s) {
        if (FAILED(hr = device->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gBuf[s])))) {
            fail("CreateCommittedResource(sky)", hr);
            return false;
        }
        D3D12_RANGE none{0, 0};
        void*       p = nullptr;
        gBuf[s]->Map(0, &none, &p);
        gMap[s] = static_cast<uint8_t*>(p);
        D3D12_SHADER_RESOURCE_VIEW_DESC pv{}; // t0: 16 x RGBA8_UNORM
        pv.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
        pv.ViewDimension           = D3D12_SRV_DIMENSION_BUFFER;
        pv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        pv.Buffer.NumElements      = 16;
        device->CreateShaderResourceView(gBuf[s].Get(), &pv, cpu(s * kTable + 0));
        nullTex(cpu(s * kTable + 1));
        nullTex(cpu(s * kTable + 2));
        D3D12_SHADER_RESOURCE_VIEW_DESC vv{}; // t3: vertices
        vv.Format                     = DXGI_FORMAT_UNKNOWN;
        vv.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
        vv.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        vv.Buffer.FirstElement        = kVertOff / sizeof(BfSkyVertV1);
        vv.Buffer.NumElements         = kMaxVerts;
        vv.Buffer.StructureByteStride = sizeof(BfSkyVertV1);
        device->CreateShaderResourceView(gBuf[s].Get(), &vv, cpu(s * kTable + 3));
    }
    return true;
}

bool createPso(D3D12_SHADER_BYTECODE vs, D3D12_SHADER_BYTECODE ps, DXGI_FORMAT fmt, ComPtr<ID3D12PipelineState>& out) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = gRoot.Get();
    d.VS = vs;
    d.PS = ps;
    auto& rt = d.BlendState.RenderTarget[0];
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D12_BLEND_ONE;      rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;      rt.BlendOp = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D12_BLEND_ONE; rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA; rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    d.SampleMask                      = UINT_MAX;
    d.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable = FALSE; // sky quads sit on the far plane
    d.PrimitiveTopologyType           = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets                = 1;
    d.RTVFormats[0]                   = fmt;
    d.SampleDesc.Count                = 1;
    HRESULT hr = gDevice->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&out));
    if (FAILED(hr)) { fail("CreateGraphicsPipelineState(sky)", hr); return false; }
    return true;
}

ComPtr<ID3D12Resource> upload(ID3D12GraphicsCommandList* list, Decoded const& d) {
    if (!d.w || !d.h) return nullptr;
    D3D12_HEAP_PROPERTIES def{D3D12_HEAP_TYPE_DEFAULT}, up{D3D12_HEAP_TYPE_UPLOAD};
    D3D12_RESOURCE_DESC   td{};
    td.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width            = d.w;
    td.Height           = d.h;
    td.DepthOrArraySize = 1;
    td.MipLevels        = 1;
    td.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    ComPtr<ID3D12Resource> tex, staging;
    if (FAILED(gDevice->CreateCommittedResource(&def, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex)))) return nullptr;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT64                             total = 0;
    gDevice->GetCopyableFootprints(&td, 0, 1, 0, &fp, nullptr, nullptr, &total);
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = total; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(gDevice->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&staging)))) return nullptr;
    uint8_t* p = nullptr;
    staging->Map(0, nullptr, reinterpret_cast<void**>(&p));
    for (UINT y = 0; y < d.h; ++y) std::memcpy(p + fp.Offset + UINT64(y) * fp.Footprint.RowPitch, d.rgba.data() + size_t(y) * d.w * 4, size_t(d.w) * 4);
    staging->Unmap(0, nullptr);
    D3D12_TEXTURE_COPY_LOCATION dst{tex.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
    dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src{staging.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
    src.PlacedFootprint = fp;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    D3D12_RESOURCE_BARRIER b{};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = tex.Get();
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    list->ResourceBarrier(1, &b);
    gGrave.push_back(staging);
    return tex;
}

void uploadImages(ID3D12GraphicsCommandList* list) {
    Decoded  s, p;
    uint32_t gen;
    {
        std::lock_guard l(gDecMutex);
        if (gDecodedGen == gUploadedGen) return;
        s = std::move(gDecSprites); p = std::move(gDecPicture);
        gen = gDecodedGen;
    }
    gGrave.clear(); // the previous staging buffers finished long ago
    if (gSprites) gGrave.push_back(gSprites);
    if (gPicture) gGrave.push_back(gPicture);
    gSprites     = upload(list, s);
    gPicture     = upload(list, p);
    gUploadedGen = gen;
}

void basis(float yawDeg, float pitchDeg, float f[3], float r[3], float u[3]) { // Minecraft yaw / pitch
    double y = yawDeg * 3.14159265358979 / 180.0, p = pitchDeg * 3.14159265358979 / 180.0;
    f[0] = float(-std::sin(y) * std::cos(p)); f[1] = float(-std::sin(p)); f[2] = float(std::cos(y) * std::cos(p));
    r[0] = float(-std::cos(y)); r[1] = 0; r[2] = float(-std::sin(y));
    u[0] = r[1] * f[2] - r[2] * f[1]; u[1] = r[2] * f[0] - r[0] * f[2]; u[2] = r[0] * f[1] - r[1] * f[0];
}
} // namespace

void record(ID3D12Device* device, ID3D12GraphicsCommandList* list, ID3D12Resource* bb, D3D12_CPU_DESCRIPTOR_HANDLE const& bbRtv, unsigned slotIn) {
    { // stage 2 needs to know whether a sky is on (cheap header-only copy) + the game camera
        static uint32_t lf = 0;
        static int      st = 0;
        BfSkyFrameV1    h{};
        h.size = sizeof h;
        auto c = bfCopy();
        if (c) c(&h, nullptr, 0);
        if (h.frame != lf) { lf = h.frame; st = 0; } else if (st < 1000) ++st;
        gActive = c && h.active && st < 30;
        if (gActive && gUi.replace) rf::lyrics::setCameraWanted(true);
    }
    if (!gUi.enabled || gBroken) return;
#if !RF_HAVE_SKY_SHADERS
    gStatus = "sky shaders not in this build (CI generates SkyShaders.h)";
    (void)device; (void)list; (void)bb; (void)bbRtv; (void)slotIn;
    return;
#else
    UINT const slot = slotIn % kSlots;
    auto       copy = bfCopy();
    if (!copy) { gStatus = "waiting for Bedrock Forge (needs a BF build with BF_CopySkyFrame)"; return; }
    gHdr      = {};
    gHdr.size = sizeof gHdr;
    uint32_t n = copy(&gHdr, gHost.data(), kMaxVerts);
    static uint32_t lastFrame = 0;
    static int      stale     = 0;
    if (gHdr.frame != lastFrame) { lastFrame = gHdr.frame; stale = 0; } else if (stale < 1000) ++stale;
    if (!gHdr.active || stale >= 30) {
        gStatus = stale >= 30 ? "paused (no world / menu)" : "idle (pick a sky in the L panel, or the lyricsflow module is off)";
        return;
    }
    if (!gRoot && !createStatic(device)) return;
    D3D12_RESOURCE_DESC bd = bb->GetDesc();
    if (gPsoFmt != bd.Format) {
        gPsoDome.Reset(); gPsoQuad.Reset();
        if (!createPso({rf::lyrics::shaders::VSSky, sizeof rf::lyrics::shaders::VSSky}, {rf::lyrics::shaders::PSSky, sizeof rf::lyrics::shaders::PSSky}, bd.Format, gPsoDome)) return;
        if (!createPso({rf::lyrics::shaders::VSSkyQuad, sizeof rf::lyrics::shaders::VSSkyQuad}, {rf::lyrics::shaders::PSSkyQuad, sizeof rf::lyrics::shaders::PSSkyQuad}, bd.Format, gPsoQuad)) return;
        gPsoFmt = bd.Format;
    }
    if (gHdr.imageGeneration != gRequestedGen && !gDecoding) startDecode(gHdr);
    uploadImages(list);
    if (gSlotGen[slot] != gUploadedGen) { // only this slot's table: the others may still be in flight
        texSrv(gSprites.Get(), cpu(slot * kTable + 1));
        texSrv(gPicture.Get(), cpu(slot * kTable + 2));
        gSlotGen[slot] = gUploadedGen;
    }
    std::memcpy(gMap[slot], gHdr.params, sizeof gHdr.params);
    n = std::min(n / 4 * 4, kMaxVerts);
    if (n) std::memcpy(gMap[slot] + kVertOff, gHost.data(), size_t(n) * sizeof(BfSkyVertV1));

    float W = float(bd.Width), H = float(bd.Height);
    float vx = 0, vy = 0, vw = W, vh = H;
    if (gUi.where == 0) { vw = std::floor(W * 0.32f); vh = std::floor(vw * 9.0f / 16.0f); vx = W - vw - 16; vy = H - vh - 16; }
    D3D12_VIEWPORT vp{vx, vy, vw, vh, 0, 1};
    D3D12_RECT     sc{LONG(vx), LONG(vy), LONG(vx + vw), LONG(vy + vh)};
    list->RSSetViewports(1, &vp);
    list->RSSetScissorRects(1, &sc);
    list->OMSetRenderTargets(1, &bbRtv, FALSE, nullptr);
    list->SetGraphicsRootSignature(gRoot.Get());
    ID3D12DescriptorHeap* heaps[] = {gHeap.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootDescriptorTable(1, gpu(slot * kTable));
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    float c[20]{};
    float ty = std::tan(gUi.fovY * 0.5f * 3.14159265f / 180.0f), tx = ty * vw / std::max(vh, 1.0f);
    basis(gHdr.camYaw, gHdr.camPitch, c + 0, c + 4, c + 8);
    c[3] = tx; c[7] = ty; c[11] = gHdr.picture ? 1.0f : 0.0f;
    c[12] = gHdr.info[0]; c[13] = gHdr.info[1]; c[14] = gHdr.info[2]; c[15] = gUi.opacity;
    c[16] = gHdr.pictureAlpha; c[17] = gHdr.pictureBright;
    list->SetGraphicsRoot32BitConstants(0, 20, c, 0);
    list->SetPipelineState(gPsoDome.Get());
    list->DrawInstanced(3, 1, 0, 0);
    if (gUi.layers && n) {
        list->SetPipelineState(gPsoQuad.Get());
        list->DrawInstanced(n / 4 * 6, 1, 0, 0);
    }
    gStatus = std::string(gHdr.picture ? "picture" : "live dome") + ", " + std::to_string(n / 4) + " quads" +
              (gHdr.vertTotal > n ? " (truncated)" : "");
    // the overlay restores its own viewport-independent state; give ImGui the full target back
    D3D12_VIEWPORT full{0, 0, W, H, 0, 1};
    D3D12_RECT     fsc{0, 0, LONG(W), LONG(H)};
    list->RSSetViewports(1, &full);
    list->RSSetScissorRects(1, &fsc);
#endif
}

void drawUi() {
    if (!ImGui::CollapsingHeader("LyricsFlow sky (preview)")) return;
    ImGui::TextDisabled("Port of LyricsFlow 2.1's sky by 374m - personal build. Pick a sky in the L panel (Sky tab).");
    ImGui::TextDisabled("Stage 1: drawn over the frame from BF's camera, not yet in place of Bedrock's sky.");
    ImGui::Checkbox("Replace Bedrock's sky (stage 2)", &gUi.replace);
    ImGui::SameLine();
    ImGui::Checkbox("Hide vanilla clouds", &gUi.hideClouds);
    ImGui::Text("Stage 2: %s", stage2().c_str());
    ImGui::Checkbox("Show sky preview", &gUi.enabled);
    ImGui::RadioButton("Corner window", &gUi.where, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Full screen", &gUi.where, 1);
    ImGui::Checkbox("Stars / weather layers", &gUi.layers);
    ImGui::SliderFloat("Opacity##sky", &gUi.opacity, 0.0f, 1.0f);
    ImGui::SliderFloat("Vertical FOV", &gUi.fovY, 30.0f, 130.0f);
    ImGui::Separator();
    ImGui::Text("Status: %s", gStatus.c_str());
    ImGui::Text("Images: %s (gen %u)", gImgStatus.c_str(), gUploadedGen);
    ImGui::Text("BF frame %u, active %u, %u/%u verts, preset #%u", gHdr.frame, gHdr.active, gHdr.vertCount, gHdr.vertTotal,
                unsigned(gHdr.params[7][2]));
}


// ================================================================ stage 2: in place of Bedrock's sky
namespace {
struct GamePso {
    int                          kind = 0;
    ComPtr<ID3D12RootSignature>  root;
    DXGI_FORMAT                  rtv[8]{};
    UINT                         numRt = 0;
    DXGI_FORMAT                  dsv = DXGI_FORMAT_UNKNOWN;
    DXGI_SAMPLE_DESC             sample{1, 0};
    ComPtr<ID3D12PipelineState>  dome, quads; // ours, made on first use
    bool                         failed = false;
};
std::mutex                                              gPsoMutex;
std::unordered_map<ID3D12PipelineState*, GamePso>       gGame;
std::atomic<long>                                       gInjected{0}, gHidden{0}, gSkipped{0};

#pragma pack(push, 1)
struct DomeVert {
    float    pos[2];
    uint32_t p[13];
    float    f[3], r[3], u[3], txy[2], info[4];
};
struct QuadVert {
    float    pos[3];
    uint32_t col;
    float    f[3], r[3], u[3], txy[2];
};
#pragma pack(pop)
constexpr UINT kRing = 8, kMaxQuadVerts = 6 * 4000;
constexpr UINT kRingBytes = 3 * sizeof(DomeVert) + kMaxQuadVerts * sizeof(QuadVert);
ComPtr<ID3D12Resource> gRing[kRing];
uint8_t*               gRingMap[kRing]{};
UINT                   gRingNext = 0;

bool makeGamePsos(ID3D12Device* dev, GamePso& g) {
#if RF_HAVE_SKY_SHADERS
    for (int which = 0; which < 2; ++which) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
        d.pRootSignature = g.root.Get();
        std::vector<D3D12_INPUT_ELEMENT_DESC> il;
        static char const* const kP = "PARAM";
        if (which == 0) {
            d.VS = {rf::lyrics::shaders::VSSkyGame, sizeof rf::lyrics::shaders::VSSkyGame};
            d.PS = {rf::lyrics::shaders::PSSkyGame, sizeof rf::lyrics::shaders::PSSkyGame};
            il.push_back({"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(DomeVert, pos), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
            for (UINT i = 0; i < 13; ++i)
                il.push_back({kP, i, DXGI_FORMAT_R8G8B8A8_UNORM, 0, UINT(offsetof(DomeVert, p) + 4 * i), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
            il.push_back({"CAMF", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(DomeVert, f), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
            il.push_back({"CAMR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(DomeVert, r), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
            il.push_back({"CAMU", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(DomeVert, u), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
            il.push_back({"CAMT", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(DomeVert, txy), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
            il.push_back({"INFO", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(DomeVert, info), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
        } else {
            d.VS = {rf::lyrics::shaders::VSSkyQuadGame, sizeof rf::lyrics::shaders::VSSkyQuadGame};
            d.PS = {rf::lyrics::shaders::PSSkyQuadGame, sizeof rf::lyrics::shaders::PSSkyQuadGame};
            il.push_back({"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(QuadVert, pos), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
            il.push_back({"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(QuadVert, col), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
            il.push_back({"CAMF", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(QuadVert, f), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
            il.push_back({"CAMR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(QuadVert, r), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
            il.push_back({"CAMU", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(QuadVert, u), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
            il.push_back({"CAMT", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(QuadVert, txy), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
        }
        d.InputLayout = {il.data(), UINT(il.size())};
        for (UINT i = 0; i < 8; ++i) {
            auto& rt = d.BlendState.RenderTarget[i];
            rt.RenderTargetWriteMask = i == 0 ? D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE : 0;
            rt.BlendEnable = i == 0;
            rt.SrcBlend = D3D12_BLEND_SRC_ALPHA; rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA; rt.BlendOp = D3D12_BLEND_OP_ADD;
            rt.SrcBlendAlpha = D3D12_BLEND_ZERO; rt.DestBlendAlpha = D3D12_BLEND_ONE; rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            rt.LogicOp = D3D12_LOGIC_OP_NOOP;
        }
        d.BlendState.IndependentBlendEnable = g.numRt > 1;
        d.SampleMask                      = UINT_MAX;
        d.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
        d.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
        d.RasterizerState.DepthClipEnable = FALSE;
        d.DepthStencilState.DepthEnable   = FALSE;
        d.DepthStencilState.StencilEnable = FALSE;
        d.PrimitiveTopologyType           = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        d.NumRenderTargets                = g.numRt;
        for (UINT i = 0; i < g.numRt; ++i) d.RTVFormats[i] = g.rtv[i];
        d.DSVFormat  = g.dsv;
        d.SampleDesc = g.sample;
        HRESULT hr = dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(which == 0 ? &g.dome : &g.quads));
        if (FAILED(hr)) {
            char b[16];
            snprintf(b, sizeof b, "%08X", uint32_t(hr));
            setStage2(std::string("our pipeline failed on the game's root signature (hr=0x") + b + ")");
            ll_shim::error("[RF] sky stage 2: " + stage2());
            return false;
        }
    }
    return true;
#else
    (void)dev; (void)g;
    setStage2("sky shaders not in this build");
    return false;
#endif
}

bool ensureRing(ID3D12Device* dev) {
    if (gRing[0]) return true;
    D3D12_HEAP_PROPERTIES up{D3D12_HEAP_TYPE_UPLOAD};
    D3D12_RESOURCE_DESC   bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = kRingBytes; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    for (UINT i = 0; i < kRing; ++i) {
        if (FAILED(dev->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gRing[i])))) {
            for (auto& r : gRing) r.Reset();
            setStage2("could not create the upload ring");
            return false;
        }
        D3D12_RANGE none{0, 0};
        void*       p = nullptr;
        gRing[i]->Map(0, &none, &p);
        gRingMap[i] = static_cast<uint8_t*>(p);
    }
    return true;
}

struct CamB { float f[3], r[3], u[3], tx, ty; };
CamB pickCam(BfSkyFrameV1 const& h) {
    CamB       c{};
    auto       s   = rf::lyrics::camera();
    int64_t    now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (s.valid && now - s.atMs < 250 && s.proj[0] != 0 && s.proj[5] != 0) {
        std::memcpy(c.f, s.forward, sizeof c.f);
        std::memcpy(c.r, s.right, sizeof c.r);
        std::memcpy(c.u, s.up, sizeof c.u);
        c.tx = 1.0f / std::fabs(s.proj[0]);
        c.ty = 1.0f / std::fabs(s.proj[5]);
        return c;
    }
    basis(h.camYaw, h.camPitch, c.f, c.r, c.u);
    c.ty = std::tan(gUi.fovY * 0.5f * 3.14159265f / 180.0f);
    c.tx = c.ty * 16.0f / 9.0f;
    return c;
}
} // namespace

int kindOfHash(uint64_t h) {
    switch (h) {
    case 0xd9972838e1f3a16full: case 0xe12627ef9fbf72f5ull: return Dome;   // Sky (Opaque)
    case 0x98a771a0871307d3ull: case 0x21dd4e6159f0b6d7ull:                // Stars
    case 0xeb7192a1099bd0a4ull: case 0x58b1282fc2ca3f98ull: return Hide;   // SunMoon
    case 0x23a6297867ab6cc7ull: case 0x8480faaf71cfdfcaull: return Clouds; // Clouds
    default: return None;
    }
}

void notePso(ID3D12PipelineState* pso, int kind, D3D12_GRAPHICS_PIPELINE_STATE_DESC const& d) {
    if (!pso || !kind) return;
    GamePso g;
    g.kind  = kind;
    g.root  = d.pRootSignature;
    g.numRt = std::min<UINT>(d.NumRenderTargets, 8);
    for (UINT i = 0; i < g.numRt; ++i) g.rtv[i] = d.RTVFormats[i];
    g.dsv    = d.DSVFormat;
    g.sample = d.SampleDesc;
    std::lock_guard l(gPsoMutex);
    gGame[pso] = std::move(g);
}

int kindOf(ID3D12PipelineState* pso) {
    if (!pso) return None;
    std::lock_guard l(gPsoMutex);
    auto it = gGame.find(pso);
    if (it == gGame.end()) return None;
    if (it->second.kind == Clouds && !gUi.hideClouds) return None;
    return it->second.kind;
}

bool wanted() { return gUi.replace && gActive.load(std::memory_order_relaxed) && !gBroken; }

bool afterGameSky(ID3D12GraphicsCommandList* list, GameState const& st, Calls const& calls) {
    if (!st.nVb || !st.vb || !st.haveTopo) { ++gSkipped; setStage2("skipped a sky draw (list state not tracked yet)"); return false; }
    ComPtr<ID3D12Device> dev;
    if (FAILED(list->GetDevice(IID_PPV_ARGS(&dev)))) return false;
    ID3D12PipelineState* domePso = nullptr;
    ID3D12PipelineState* quadPso = nullptr;
    GamePso g; // copy: our CreateGraphicsPipelineState goes through UiCapture's hook (takes its mutex) -> never
               // create pipelines while holding gPsoMutex (UiCapture's draw hook takes gMutex, then gPsoMutex)
    {
        std::lock_guard l(gPsoMutex);
        auto it = gGame.find(st.pso);
        if (it == gGame.end() || it->second.failed) return false;
        g = it->second;
    }
    if (!g.dome) {
        bool ok = makeGamePsos(dev.Get(), g);
        std::lock_guard l(gPsoMutex);
        auto it = gGame.find(st.pso);
        if (it == gGame.end()) return false;
        if (!ok) { it->second.failed = true; return false; }
        if (!it->second.dome) { it->second.dome = g.dome; it->second.quads = g.quads; }
        g = it->second;
    }
    domePso = g.dome.Get();
    quadPso = g.quads.Get();
    {
        static std::mutex ringMutex;
        std::lock_guard   l(ringMutex);
        if (!ensureRing(dev.Get())) return false;
    }
    auto copy = bfCopy();
    if (!copy) return false;
    static thread_local std::vector<BfSkyVertV1> host(kMaxVerts);
    BfSkyFrameV1 h{};
    h.size     = sizeof h;
    uint32_t n = copy(&h, host.data(), kMaxVerts);
    if (!h.active || h.picture) return false; // My picture: stage 1 only for now

    CamB cam = pickCam(h);
    UINT slot;
    {
        std::lock_guard l(gPsoMutex);
        slot = gRingNext++ % kRing;
    }
    uint8_t*  base = gRingMap[slot];
    DomeVert* dv   = reinterpret_cast<DomeVert*>(base);
    float const tri[3][2] = {{-1, -1}, {-1, 3}, {3, -1}};
    for (int k = 0; k < 3; ++k) {
        DomeVert& v = dv[k];
        v.pos[0] = tri[k][0]; v.pos[1] = tri[k][1];
        for (int i = 0; i < 13; ++i) std::memcpy(&v.p[i], h.params[i], 4);
        std::memcpy(v.f, cam.f, 12); std::memcpy(v.r, cam.r, 12); std::memcpy(v.u, cam.u, 12);
        v.txy[0] = cam.tx; v.txy[1] = cam.ty;
        v.info[0] = h.info[0]; v.info[1] = h.info[1]; v.info[2] = h.info[2]; v.info[3] = 0;
    }
    // sky-layer quads only (stars, constellations, shooting stars, lightning); weather needs depth: not here yet
    QuadVert* qv  = reinterpret_cast<QuadVert*>(base + 3 * sizeof(DomeVert));
    UINT      nq  = 0;
    static int const kCorner[6] = {0, 1, 2, 0, 2, 3};
    for (uint32_t q = 0; q + 3 < n && nq + 6 <= kMaxQuadVerts; q += 4) {
        if (host[q].flags & (BF_SKY_WORLD | BF_SKY_SPRITE)) continue;
        for (int k = 0; k < 6; ++k) {
            auto const& s = host[q + uint32_t(kCorner[k])];
            QuadVert&   v = qv[nq++];
            std::memcpy(v.pos, s.pos, 12);
            v.col = s.color;
            std::memcpy(v.f, cam.f, 12); std::memcpy(v.r, cam.r, 12); std::memcpy(v.u, cam.u, 12);
            v.txy[0] = cam.tx; v.txy[1] = cam.ty;
        }
    }
    D3D12_GPU_VIRTUAL_ADDRESS gpu = gRing[slot]->GetGPUVirtualAddress();
    D3D12_VERTEX_BUFFER_VIEW  vd{gpu, UINT(3 * sizeof(DomeVert)), UINT(sizeof(DomeVert))};
    calls.setPso(list, domePso);
    calls.setTopo(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    calls.setVb(list, 0, 1, &vd);
    calls.draw(list, 3, 1, 0, 0);
    if (nq) {
        D3D12_VERTEX_BUFFER_VIEW vq{gpu + 3 * sizeof(DomeVert), UINT(nq * sizeof(QuadVert)), UINT(sizeof(QuadVert))};
        calls.setPso(list, quadPso);
        calls.setVb(list, 0, 1, &vq);
        calls.draw(list, nq, 1, 0, 0);
    }
    // the game's state back
    calls.setPso(list, st.pso);
    calls.setTopo(list, st.topo);
    calls.setVb(list, 0, st.nVb, st.vb);
    ++gInjected;
    if ((gInjected.load() & 63) == 1) setStage2("drawing in place of Bedrock's sky (" + std::to_string(gInjected.load()) + " frames, " + std::to_string(nq / 6) +
              " quads, vanilla sun/moon/stars hidden: " + std::to_string(gHidden.load()) + ")");
    return true;
}

void noteHidden() { ++gHidden; }

} // namespace rf::sky
