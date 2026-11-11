// Renderer Foundation - LyricsFlow port: live sky preview pass (see SkyPass.h). Original code for Renderer Foundation.
// STAGED, NOT RUN IN GAME YET: compile-verified by CI only. Same D3D12 patterns as LyricsPass (root constants + SRV
// table per ring slot, premultiplied blend, PSO per back-buffer format, WIC decode on a worker thread).
#include "D3D12Api.h"
#include "SkyPass.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cwctype>
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
#include <fstream>
#include <iterator>

#include "SkyMaterialScan.h"
#include "UiCapture.h"
#include "GlassArmor.h"
#include "WorldFx.h"

#if __has_include("SkyShaders.h")
#include "SkyShaders.h"
#endif
#if __has_include("WorldFxShaders.h")
#include "WorldFxShaders.h"
#define RF_HAVE_WORLDFX_SHADERS 1
#else
#define RF_HAVE_WORLDFX_SHADERS 0
#endif
#include <memory>

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
    bool  vv         = true;  // Vibrant Visuals: replace the deferred sky too
    float vvGain     = 1.0f;  // Vibrant Visuals: brightness of our (sRGB -> linear) sky in the game's HDR target
} gUi;
std::atomic<uint64_t> gPresentNo{0}, gVvClaimed{~0ull}; // Vibrant Visuals: one dome per frame
std::atomic_bool gActive{false};  // BF publishes an active sky (updated every Present)
std::atomic_bool gPictureOn{false}; // ... and it is "My picture"
std::atomic<int> gWorldSky{0};     // World FX standalone sky (0 off, 1 Cloud Sea), see the World FX section below
void worldSkyPrepare(ID3D12Device* dev, ID3D12GraphicsCommandList* list);
// sky shaders learned from the game's own materials (learnGameMaterials / learnMaterialFile)
std::mutex                        gLearnMutex;
std::unordered_map<uint64_t, int> gLearned;            // pixel-shader hash -> Kind
int                               gLearnKind[5]{};     // learned shaders per Kind
int                               gLearnFiles = 0, gLearnMissing = 0, gLearnPack = 0;
std::atomic<long>                 gTagged[5]{};        // game pipelines tagged per Kind
std::atomic<long>                 gTaggedBuiltin{0};   // ... found only by the built-in 1.26.51 table

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
std::mutex                   gImgMutex; // gPicture is also read by stage 2 on the game's render threads
std::vector<ComPtr<ID3D12Resource>> gGrave;
uint32_t                     gSlotGen[kSlots] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
bool                         gBroken = false;
std::string                  gStatus = "off (F6 to preview)";
BfSkyFrameV1                 gHdr{};
std::vector<BfSkyVertV1>     gHost(kMaxVerts);
std::atomic<long>            gWeatherFrames{0}, gWeatherNoCam{0};
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
    HRESULT                   hr = rf::d3d12api::SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
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
        pv.Buffer.NumElements      = 32; // [LyricsForge] the sky now + the one being swept away
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
    auto ns = upload(list, s), np = upload(list, p);
    {
        std::lock_guard l(gImgMutex);
        gSprites = ns;
        gPicture = np;
    }
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
    ++gPresentNo;
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
        if (gWorldSky.load()) { rf::lyrics::setCameraWanted(true); worldSkyPrepare(device, list); }
        gPictureOn = gActive && h.picture;
        // stage 2 "My picture" needs the decoded panorama even with the preview off
        if (!gUi.enabled && !gBroken && gUi.replace && gPictureOn) {
            if (!gRoot && !createStatic(device)) return;
            if (h.imageGeneration != gRequestedGen && !gDecoding) startDecode(h);
            uploadImages(list);
            return;
        }
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
    std::memcpy(gMap[slot] + sizeof gHdr.params, gHdr.prevParams, sizeof gHdr.prevParams); // [LyricsForge] sky sweep
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
    ImGui::Checkbox("Vibrant Visuals too", &gUi.vv);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160);
    ImGui::SliderFloat("VV sky brightness", &gUi.vvGain, 0.01f, 1000.0f, "%.3g", ImGuiSliderFlags_Logarithmic);
    ImGui::Text("Stage 2: %s", stage2().c_str());
    {
        std::lock_guard l(gLearnMutex);
        ImGui::Text("Sky shaders learned from the game: %d files (%d from packs), sky %d, sun/stars %d, clouds %d, Vibrant Visuals %d",
                    gLearnFiles, gLearnPack, gLearnKind[Dome], gLearnKind[Hide], gLearnKind[Clouds], gLearnKind[VvSky]);
        if (gLearnMissing) ImGui::TextColored(ImVec4(1, .7f, .3f, 1), "%d sky material files could not be read", gLearnMissing);
        if (!gLearnKind[VvSky])
            ImGui::TextColored(ImVec4(1, .7f, .3f, 1), "No Vibrant Visuals sky shaders found - the game update may have changed the materials");
    }
    ImGui::Text("Game sky pipelines tagged: sky %ld, sun/stars %ld, clouds %ld, Vibrant Visuals %ld (%ld by the old table only)",
                gTagged[Dome].load(), gTagged[Hide].load(), gTagged[Clouds].load(), gTagged[VvSky].load(), gTaggedBuiltin.load());
    ImGui::Text("Weather: %ld frames drawn, %ld without the game camera", gWeatherFrames.load(), gWeatherNoCam.load());
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
    bool                         vv   = false; // Vibrant Visuals pass: depth-test against the scene, HDR target
    float                        farZ = 1.0f;  // depth of "nothing there" (0 with reversed Z)
    ComPtr<ID3D12RootSignature>  root;
    DXGI_FORMAT                  rtv[8]{};
    UINT                         numRt = 0;
    DXGI_FORMAT                  dsv = DXGI_FORMAT_UNKNOWN;
    DXGI_SAMPLE_DESC             sample{1, 0};
    D3D12_DEPTH_STENCIL_DESC     dss{};
    ComPtr<ID3D12PipelineState>  dome, quads; // ours, made on first use (sky kind)
    ComPtr<ID3D12PipelineState>  picture;     // sky kind, own root signature
    ComPtr<ID3D12PipelineState>  weather;     // clouds kind, game root signature + the clouds' depth test
    bool                         failed = false;
};
std::mutex                                              gPsoMutex;
std::unordered_map<ID3D12PipelineState*, GamePso>       gGame;
std::atomic<long>                                       gInjected{0}, gHidden{0}, gSkipped{0};

#pragma pack(push, 1)
struct DomeVert {
    float    pos[2];
    uint32_t p[16]; // [LyricsForge] 13: the music right now (lightning, kick, beat, chorus wave), 14: sky sweep, 15: spectrum
    uint32_t prev[16]; // [LyricsForge] the sky being swept away (BfSkyFrameV1.prevParams)
    float    f[3], r[3], u[3], txy[2], info[4];
    float    extra[4]; // x = far depth, y = HDR gain, z = 1: sRGB -> linear (Vibrant Visuals), w = 0
};
struct QuadVert {
    float    pos[3];
    uint32_t col;
    float    f[3], r[3], u[3], txy[2];
    float    extra[4];
};
#pragma pack(pop)
constexpr UINT kRing = 8, kMaxQuadVerts = 6 * 4000;
constexpr UINT kRingBytes = 3 * sizeof(DomeVert) + kMaxQuadVerts * sizeof(QuadVert);
ComPtr<ID3D12Resource> gRing[kRing];
uint8_t*               gRingMap[kRing]{};
UINT                   gRingNext = 0;

/// Vibrant Visuals: only where the scene depth is still "far" (nothing drawn there = sky). The vanilla star / sun
/// pass tells us the depth convention: GREATER(_EQUAL) = reversed Z (far = 0), else far = 1. No depth writes.
void vvDepth(D3D12_GRAPHICS_PIPELINE_STATE_DESC& d, GamePso const& g) {
    bool const rev = g.dss.DepthFunc == D3D12_COMPARISON_FUNC_GREATER || g.dss.DepthFunc == D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    d.DepthStencilState.DepthEnable    = TRUE;
    d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    d.DepthStencilState.DepthFunc      = rev ? D3D12_COMPARISON_FUNC_GREATER_EQUAL : D3D12_COMPARISON_FUNC_LESS_EQUAL;
    d.DepthStencilState.StencilEnable  = FALSE;
}

bool makeGamePsos(ID3D12Device* dev, GamePso& g) {
    if (g.vv && (g.dsv == DXGI_FORMAT_UNKNOWN || !g.dss.DepthEnable)) {
        setStage2("Vibrant Visuals: the vanilla star / sun pass has no depth buffer bound - can't find the sky pixels");
        return false;
    }
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
            for (UINT i = 0; i < 16; ++i) // [LyricsForge] all 16 texels: 15 = the 8-band spectrum (was missing: the sky never heard it)
                il.push_back({kP, i, DXGI_FORMAT_R8G8B8A8_UNORM, 0, UINT(offsetof(DomeVert, p) + 4 * i), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
            for (UINT i = 0; i < 4; ++i)
                il.push_back({"PREV", i, DXGI_FORMAT_R32G32B32A32_UINT, 0, UINT(offsetof(DomeVert, prev) + 16 * i), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
            il.push_back({"CAMF", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(DomeVert, f), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
            il.push_back({"CAMR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(DomeVert, r), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
            il.push_back({"CAMU", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(DomeVert, u), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
            il.push_back({"CAMT", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(DomeVert, txy), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
            il.push_back({"INFO", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(DomeVert, info), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
            il.push_back({"EXTRA", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(DomeVert, extra), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
        } else {
            d.VS = {rf::lyrics::shaders::VSSkyQuadGame, sizeof rf::lyrics::shaders::VSSkyQuadGame};
            d.PS = {rf::lyrics::shaders::PSSkyQuadGame, sizeof rf::lyrics::shaders::PSSkyQuadGame};
            il.push_back({"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(QuadVert, pos), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
            il.push_back({"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(QuadVert, col), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
            il.push_back({"CAMF", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(QuadVert, f), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
            il.push_back({"CAMR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(QuadVert, r), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
            il.push_back({"CAMU", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(QuadVert, u), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
            il.push_back({"CAMT", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(QuadVert, txy), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
            il.push_back({"EXTRA", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(QuadVert, extra), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
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
        if (g.vv) vvDepth(d, g);
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


ComPtr<ID3D12RootSignature>  gPicRoot;
ComPtr<ID3D12DescriptorHeap> gPicHeap;
ComPtr<ID3D12Resource>       gPicKeep[kRing];
UINT                         gPicNext = 0, gPicInc = 0;
std::mutex                   gPicMutex;

bool makePictureRoot(ID3D12Device* dev) {
    if (gPicRoot) return true;
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    D3D12_ROOT_PARAMETER   prm[2]{};
    prm[0].ParameterType    = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    prm[0].Constants        = {0, 0, 16};
    prm[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    prm[1].ParameterType    = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    prm[1].DescriptorTable  = {1, &range};
    prm[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC smp{};
    smp.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    smp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    smp.AddressV = smp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    smp.MaxLOD           = D3D12_FLOAT32_MAX;
    smp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rs{2, prm, 1, &smp, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob>          blob, err;
    if (FAILED(rf::d3d12api::SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err))) return false;
    if (FAILED(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&gPicRoot)))) return false;
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kRing, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&gPicHeap)))) { gPicRoot.Reset(); return false; }
    gPicInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return true;
}

/// which: 2 = picture (own root signature, no input layout), 3 = weather (game root signature, clouds depth test)
ComPtr<ID3D12PipelineState> makeExtraPso(ID3D12Device* dev, GamePso const& g, int which) {
    ComPtr<ID3D12PipelineState> out;
#if RF_HAVE_SKY_SHADERS
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    std::vector<D3D12_INPUT_ELEMENT_DESC> il;
    if (which == 2) {
        if (g.vv) vvDepth(d, g);
        d.pRootSignature = gPicRoot.Get();
        d.VS = {rf::lyrics::shaders::VSSkyPicGame, sizeof rf::lyrics::shaders::VSSkyPicGame};
        d.PS = {rf::lyrics::shaders::PSSkyPicGame, sizeof rf::lyrics::shaders::PSSkyPicGame};
    } else {
        d.pRootSignature = g.root.Get();
        d.VS = {rf::lyrics::shaders::VSWeatherGame, sizeof rf::lyrics::shaders::VSWeatherGame};
        d.PS = {rf::lyrics::shaders::PSWeatherGame, sizeof rf::lyrics::shaders::PSWeatherGame};
        il.push_back({"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
        il.push_back({"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
        il.push_back({"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
        for (UINT r = 0; r < 4; ++r)
            il.push_back({"VPROW", r, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24 + 16 * r, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
        d.InputLayout = {il.data(), UINT(il.size())};
        d.DepthStencilState                = g.dss; // the clouds' own depth test (same convention as the terrain)
        d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        d.DepthStencilState.StencilEnable  = FALSE;
    }
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
    d.RasterizerState.DepthClipEnable = which == 3;
    d.PrimitiveTopologyType           = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets                = g.numRt;
    for (UINT i = 0; i < g.numRt; ++i) d.RTVFormats[i] = g.rtv[i];
    d.DSVFormat  = g.dsv;
    d.SampleDesc = g.sample;
    HRESULT hr   = dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&out));
    if (FAILED(hr)) {
        char b[16];
        snprintf(b, sizeof b, "%08X", uint32_t(hr));
        setStage2(std::string(which == 2 ? "picture" : "weather") + " pipeline failed (hr=0x" + b + ")");
        out.Reset();
    }
#else
    (void)dev; (void)g; (void)which;
#endif
    return out;
}

// ================================================================ World FX: standalone sky (Cloud Sea)
// Textures are made once on a worker (same maths as the Cloud Sea lab), uploaded at the next Present on the overlay's
// list, then the sky is drawn with its own root signature (16+4 constants, 2 SRVs, linear wrap sampler).
std::atomic<float> gDeckOffset{0.0f};
std::mutex         gWsMutex;
std::string        gWsStatus = "off";
void wsStatus(std::string s) { std::lock_guard l(gWsMutex); gWsStatus = std::move(s); }
struct WsTexData {
    std::vector<uint8_t>               noise; // 256 x 256 RGBA8
    std::vector<std::vector<uint16_t>> puff;  // 1024 x 1024 R16, all mips
};
std::mutex                   gWsTexMutex;
std::unique_ptr<WsTexData>   gWsTexReady;
std::atomic_bool             gWsTexStarted{false}, gWsTexUp{false};
ComPtr<ID3D12Resource>       gWsNoise, gWsPuff;
ComPtr<ID3D12RootSignature>  gWsRoot;
ComPtr<ID3D12DescriptorHeap> gWsHeap;
std::atomic<long>            gWsFrames{0};
auto const                   gWsT0 = std::chrono::steady_clock::now();
double                       gAnchor[3]{};
bool                         gAnchored = false;

std::unique_ptr<WsTexData> makeWsTextures() {
    auto d = std::make_unique<WsTexData>();
    uint32_t seed = 1234567u; // the lab's noise: same generator, same bytes
    d->noise.resize(256 * 256 * 4);
    for (auto& b : d->noise) { seed = seed * 1664525u + 1013904223u; b = uint8_t(seed >> 24); }
    constexpr int PN = 1024;
    auto domes = [](int N, double rMin, double rMax, uint32_t sd) {
        struct P { double x, y, r, h; };
        std::vector<P> pts;
        uint32_t s2 = sd;
        auto r2 = [&] { s2 = s2 * 1664525u + 1013904223u; return s2 / 4294967296.0; };
        for (int j = 0; j < N; ++j)
            for (int i = 0; i < N; ++i) {
                P p;
                p.x = (i + 0.15 + 0.7 * r2()) / N;
                p.y = (j + 0.15 + 0.7 * r2()) / N;
                p.r = (rMin + (rMax - rMin) * r2()) / N;
                p.h = 0.55 + 0.45 * r2();
                pts.push_back(p);
            }
        std::vector<float> o(size_t(PN) * PN);
        for (int y = 0; y < PN; ++y)
            for (int x = 0; x < PN; ++x) {
                double u = (x + 0.5) / PN, v = (y + 0.5) / PN, m = 0;
                int    ci = int(std::floor(u * N)), cj = int(std::floor(v * N));
                for (int dj = -1; dj <= 1; ++dj)
                    for (int di = -1; di <= 1; ++di) {
                        P const& p = pts[size_t(((cj + dj + N) % N) * N + ((ci + di + N) % N))];
                        double dx = std::fabs(u - p.x), dy = std::fabs(v - p.y);
                        if (dx > 0.5) dx = 1 - dx;
                        if (dy > 0.5) dy = 1 - dy;
                        double q = (dx * dx + dy * dy) / (p.r * p.r);
                        if (q < 1) m = std::max(m, std::pow(1 - q, 0.8) * p.h);
                    }
                o[size_t(y) * PN + x] = float(m);
            }
        return o;
    };
    auto d1 = domes(6, 0.62, 0.98, 11), d2 = domes(15, 0.60, 0.95, 23), d3 = domes(37, 0.55, 0.95, 37), d4 = domes(83, 0.5, 0.95, 51);
    std::vector<float> cur(size_t(PN) * PN);
    for (size_t i = 0; i < cur.size(); ++i)
        cur[i] = d1[i] * 0.60f + d2[i] * 0.26f * (0.35f + 0.65f * d1[i]) + d3[i] * 0.10f * (0.3f + 0.7f * d2[i]) + d4[i] * 0.04f;
    for (int n = PN;; n >>= 1) {
        std::vector<uint16_t> lv(cur.size());
        for (size_t i = 0; i < cur.size(); ++i) lv[i] = uint16_t(std::lround(std::clamp(cur[i], 0.0f, 1.0f) * 65535.0f));
        d->puff.push_back(std::move(lv));
        if (n == 1) break;
        int const          m = n >> 1;
        std::vector<float> nx(size_t(m) * m);
        for (int y = 0; y < m; ++y)
            for (int x = 0; x < m; ++x)
                nx[size_t(y) * m + x] = (cur[size_t(2 * y) * n + 2 * x] + cur[size_t(2 * y) * n + 2 * x + 1] +
                                         cur[size_t(2 * y + 1) * n + 2 * x] + cur[size_t(2 * y + 1) * n + 2 * x + 1]) * 0.25f;
        cur = std::move(nx);
    }
    return d;
}

/// texture with all its mips in one copy (levels[i] = tightly packed rows, bpp bytes per texel)
ComPtr<ID3D12Resource> uploadLevels(ID3D12Device* dev, ID3D12GraphicsCommandList* list, DXGI_FORMAT fmt, UINT w, UINT h, UINT bpp,
                                    std::vector<uint8_t const*> const& levels) {
    D3D12_HEAP_PROPERTIES def{D3D12_HEAP_TYPE_DEFAULT}, up{D3D12_HEAP_TYPE_UPLOAD};
    D3D12_RESOURCE_DESC   td{};
    td.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width            = w;
    td.Height           = h;
    td.DepthOrArraySize = 1;
    td.MipLevels        = UINT16(levels.size());
    td.Format           = fmt;
    td.SampleDesc.Count = 1;
    ComPtr<ID3D12Resource> tex, staging;
    if (FAILED(dev->CreateCommittedResource(&def, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex)))) return nullptr;
    UINT const n = UINT(levels.size());
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> fp(n);
    std::vector<UINT>   rows(n);
    std::vector<UINT64> rowBytes(n);
    UINT64              total = 0;
    dev->GetCopyableFootprints(&td, 0, n, 0, fp.data(), rows.data(), rowBytes.data(), &total);
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = total; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(dev->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&staging)))) return nullptr;
    uint8_t* p = nullptr;
    if (FAILED(staging->Map(0, nullptr, reinterpret_cast<void**>(&p)))) return nullptr;
    for (UINT m = 0; m < n; ++m) {
        UINT const lw = std::max(1u, w >> m);
        for (UINT y = 0; y < rows[m]; ++y)
            std::memcpy(p + fp[m].Offset + UINT64(y) * fp[m].Footprint.RowPitch, levels[m] + size_t(y) * lw * bpp, size_t(lw) * bpp);
    }
    staging->Unmap(0, nullptr);
    for (UINT m = 0; m < n; ++m) {
        D3D12_TEXTURE_COPY_LOCATION dst{tex.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
        dst.SubresourceIndex = m;
        D3D12_TEXTURE_COPY_LOCATION src{staging.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
        src.PlacedFootprint = fp[m];
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }
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

bool makeWsRoot(ID3D12Device* dev) {
    if (gWsRoot) return true;
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0};
    D3D12_ROOT_PARAMETER   prm[2]{};
    prm[0].ParameterType    = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    prm[0].Constants        = {0, 0, 20};
    prm[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    prm[1].ParameterType    = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    prm[1].DescriptorTable  = {1, &range};
    prm[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC smp{};
    smp.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    smp.AddressU = smp.AddressV = smp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    smp.MaxLOD           = D3D12_FLOAT32_MAX;
    smp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rs{2, prm, 1, &smp, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob>          blob, err;
    if (FAILED(rf::d3d12api::SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err))) return false;
    return SUCCEEDED(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&gWsRoot)));
}

/// once per Present while the Cloud Sea is on (overlay thread): textures, heap, root signature
void worldSkyPrepare(ID3D12Device* dev, ID3D12GraphicsCommandList* list) {
    if (gWsTexUp.load()) return;
    if (!gWsTexStarted.exchange(true)) {
        wsStatus("making the cloud textures");
        std::thread([] {
            auto t = makeWsTextures();
            std::lock_guard l(gWsTexMutex);
            gWsTexReady = std::move(t);
        }).detach();
        return;
    }
    std::unique_ptr<WsTexData> d;
    {
        std::lock_guard l(gWsTexMutex);
        d = std::move(gWsTexReady);
    }
    if (!d) return;
    if (!makeWsRoot(dev)) { wsStatus("root signature failed"); return; }
    gWsNoise = uploadLevels(dev, list, DXGI_FORMAT_R8G8B8A8_UNORM, 256, 256, 4, {d->noise.data()});
    std::vector<uint8_t const*> lv;
    for (auto& l : d->puff) lv.push_back(reinterpret_cast<uint8_t const*>(l.data()));
    gWsPuff = uploadLevels(dev, list, DXGI_FORMAT_R16_UNORM, 1024, 1024, 2, lv);
    if (!gWsNoise || !gWsPuff) { wsStatus("texture upload failed"); return; }
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&gWsHeap)))) { wsStatus("descriptor heap failed"); return; }
    UINT const inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto       h   = gWsHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sv.Texture2D.MipLevels     = 1;
    dev->CreateShaderResourceView(gWsNoise.Get(), &sv, h);
    h.ptr += inc;
    sv.Format              = DXGI_FORMAT_R16_UNORM;
    sv.Texture2D.MipLevels = UINT(lv.size());
    dev->CreateShaderResourceView(gWsPuff.Get(), &sv, h);
    gWsTexUp = true;
    wsStatus("ready - waiting for the game's sky draw");
}

ComPtr<ID3D12PipelineState> makeWsPso(ID3D12Device* dev, GamePso const& g) {
    ComPtr<ID3D12PipelineState> out;
#if RF_HAVE_WORLDFX_SHADERS
    if (!gWsRoot) return out;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = gWsRoot.Get();
    d.VS = {rf::worldfx::shaders::VSCloudSea, sizeof rf::worldfx::shaders::VSCloudSea};
    d.PS = {rf::worldfx::shaders::PSCloudSea, sizeof rf::worldfx::shaders::PSCloudSea};
    for (UINT i = 0; i < 8; ++i) {
        auto& rt = d.BlendState.RenderTarget[i];
        rt.RenderTargetWriteMask = i == 0 ? D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE : 0;
        rt.SrcBlend = D3D12_BLEND_ONE; rt.DestBlend = D3D12_BLEND_ZERO; rt.BlendOp = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D12_BLEND_ONE; rt.DestBlendAlpha = D3D12_BLEND_ZERO; rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        rt.LogicOp = D3D12_LOGIC_OP_NOOP;
    }
    d.BlendState.IndependentBlendEnable = g.numRt > 1;
    d.SampleMask                        = UINT_MAX;
    d.RasterizerState.FillMode          = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode          = D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable   = FALSE;
    d.PrimitiveTopologyType             = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets                  = g.numRt;
    for (UINT i = 0; i < g.numRt; ++i) d.RTVFormats[i] = g.rtv[i];
    d.DSVFormat  = g.dsv;
    d.SampleDesc = g.sample;
    HRESULT hr   = dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&out));
    if (FAILED(hr)) {
        char b[16];
        snprintf(b, sizeof b, "%08X", uint32_t(hr));
        wsStatus(std::string("pipeline failed (hr=0x") + b + ")");
        out.Reset();
    }
#else
    (void)dev; (void)g;
    wsStatus("World FX shaders not in this build (CI generates WorldFxShaders.h)");
#endif
    return out;
}

// ---------------------------------------------------------------- World FX screen pass (shadows, occlusion, rim)
ComPtr<ID3D12RootSignature> gWfxRoot;
std::mutex                  gWfxMutex;
std::string                 gWfxStatus = "off";
void wfxStatus(std::string s) { std::lock_guard l(gWfxMutex); gWfxStatus = std::move(s); }
bool makeWfxRoot(ID3D12Device* dev) {
    std::lock_guard l(gWfxMutex);
    if (gWfxRoot) return true;
    D3D12_ROOT_PARAMETER prm[5]{};
    prm[0].ParameterType    = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    prm[0].Constants        = {0, 0, 40};
    prm[4].ParameterType    = D3D12_ROOT_PARAMETER_TYPE_CBV; // b1: the match (team lights, beacons, shards)
    prm[4].Descriptor       = {1, 0};
    prm[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    prm[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    prm[1].ParameterType    = D3D12_ROOT_PARAMETER_TYPE_SRV;
    prm[1].Descriptor       = {0, 0};
    prm[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    prm[2].ParameterType    = D3D12_ROOT_PARAMETER_TYPE_SRV;
    prm[2].Descriptor       = {1, 0};
    prm[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 2, 0, 0}; // t2 noise, t3 cloud tops (the sky's textures)
    prm[3].ParameterType    = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    prm[3].DescriptorTable  = {1, &range};
    prm[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC smp{};
    smp.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    smp.AddressU = smp.AddressV = smp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    smp.MaxLOD           = D3D12_FLOAT32_MAX;
    smp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rs{5, prm, 1, &smp, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob>          blob, err;
    if (FAILED(rf::d3d12api::SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err))) return false;
    return SUCCEEDED(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&gWfxRoot)));
}
ComPtr<ID3D12PipelineState> makeWfxPso(ID3D12Device* dev, GamePso const& g) {
    ComPtr<ID3D12PipelineState> out;
#if RF_HAVE_WORLDFX_SHADERS
    if (!makeWfxRoot(dev)) { wfxStatus("root signature failed"); return out; }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = gWfxRoot.Get();
    d.VS = {rf::worldfx::shaders::VSWorldFx, sizeof rf::worldfx::shaders::VSWorldFx};
    d.PS = {rf::worldfx::shaders::PSWorldFx, sizeof rf::worldfx::shaders::PSWorldFx};
    for (UINT i = 0; i < 8; ++i) {
        auto& rt = d.BlendState.RenderTarget[i];
        rt.RenderTargetWriteMask = i == 0 ? D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE : 0;
        rt.SrcBlend = D3D12_BLEND_ONE; rt.DestBlend = D3D12_BLEND_ZERO; rt.BlendOp = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D12_BLEND_ONE; rt.DestBlendAlpha = D3D12_BLEND_ZERO; rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        rt.LogicOp = D3D12_LOGIC_OP_NOOP;
    }
    d.BlendState.IndependentBlendEnable = g.numRt > 1;
    d.SampleMask                        = UINT_MAX;
    d.RasterizerState.FillMode          = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode          = D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable   = FALSE;
    d.PrimitiveTopologyType             = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets                  = g.numRt;
    for (UINT i = 0; i < g.numRt; ++i) d.RTVFormats[i] = g.rtv[i];
    d.DSVFormat  = g.dsv;
    d.SampleDesc = g.sample;
    HRESULT hr   = dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&out));
    if (FAILED(hr)) {
        char b[16];
        snprintf(b, sizeof b, "%08X", uint32_t(hr));
        wfxStatus(std::string("pipeline failed (hr=0x") + b + ")");
        out.Reset();
    }
#else
    (void)dev; (void)g;
#endif
    return out;
}

/// get-or-make one of our pipelines for a tagged game pipeline (never while holding gPsoMutex, see afterGameSky)
// [RF] Our pipelines are built from the game's sky / clouds pipeline state. They used to be made on the render thread
// the first time each *game pipeline object* drew: on servers (Hive) the game keeps creating new pipeline objects with
// the same state, and every new one cost a ~400 ms shader build mid-frame (seen in game as a freeze whenever the sky
// or a weather moment changed). Now they are cached by the state that matters, built once on a worker thread, and
// the frame never waits: until one is ready the game draws its own sky / no weather for those few frames.
std::string psoKey(GamePso const& g, int which) {
    std::string k;
    auto put = [&](void const* p, size_t n) { k.append(static_cast<char const*>(p), n); };
    put(&which, sizeof which);
    ID3D12RootSignature* root = (which == 2 || which >= 4) ? nullptr : g.root.Get();
    put(&root, sizeof root);
    put(&g.numRt, sizeof g.numRt);
    put(g.rtv, sizeof(DXGI_FORMAT) * g.numRt);
    put(&g.dsv, sizeof g.dsv);
    put(&g.sample, sizeof g.sample);
    put(&g.dss, sizeof g.dss);
    put(&g.vv, sizeof g.vv);
    put(&g.farZ, sizeof g.farZ);
    return k;
}
std::mutex                                                   gDerMutex;
std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> gDer;     // key -> ready pipeline (null = failed)
std::unordered_map<std::string, bool>                        gDerBusy;  // being built
std::atomic<int>                                             gDerBuilt{0}, gDerMsMax{0};

ID3D12PipelineState* derived(ID3D12Device* dev, GamePso const& g, int which) { // 0 dome, 1 quads, 2 picture, 3 weather, 4 World FX sky
    std::string const key = psoKey(g, which);
    {
        std::lock_guard l(gDerMutex);
        if (auto it = gDer.find(key); it != gDer.end()) return it->second.Get();
        if (gDerBusy.count(key)) return nullptr;
        gDerBusy[key] = true;
    }
    ComPtr<ID3D12Device> d(dev);
    std::thread([d, g, which, key] {
        auto const t0 = std::chrono::steady_clock::now();
        GamePso work = g;
        std::vector<std::pair<std::string, ComPtr<ID3D12PipelineState>>> made;
        if (which <= 1) {
            work.dome.Reset(); work.quads.Reset();
            bool const ok = makeGamePsos(d.Get(), work);
            made.push_back({psoKey(g, 0), ok ? work.dome : nullptr});
            made.push_back({psoKey(g, 1), ok ? work.quads : nullptr});
        } else
            made.push_back({key, which == 5 ? makeWfxPso(d.Get(), g) : which == 4 ? makeWsPso(d.Get(), g) : makeExtraPso(d.Get(), g, which)});
        int const ms = int(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
        {
            std::lock_guard l(gDerMutex);
            for (auto& [k, p] : made) { gDer[k] = p; gDerBusy.erase(k); }
        }
        ++gDerBuilt;
        if (ms > gDerMsMax.load()) gDerMsMax = ms;
        ll_shim::info("[RF] sky pipeline " + std::to_string(which) + " built on a worker in " + std::to_string(ms) + " ms (" +
                      std::to_string(gDerBuilt.load()) + " so far)");
    }).detach();
    return nullptr;
}

ID3D12PipelineState* extraPso(ID3D12Device* dev, ID3D12PipelineState* game, int which) {
    GamePso g;
    {
        std::lock_guard l(gPsoMutex);
        auto it = gGame.find(game);
        if (it == gGame.end()) return nullptr;
        g = it->second;
    }
    return derived(dev, g, which);
}

void vpRows(float const f[3], float const r[3], float const u[3], float const proj[16], float rows[4][4]) {
    float V[4][4] = {{r[0], r[1], r[2], 0}, {u[0], u[1], u[2], 0}, {-f[0], -f[1], -f[2], 0}, {0, 0, 0, 1}};
    float out[16];
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += proj[k * 4 + row] * V[k][col];
            out[col * 4 + row] = s;
        }
    for (int i = 0; i < 4; ++i)
        for (int c = 0; c < 4; ++c) rows[i][c] = out[c * 4 + i];
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

namespace {

int builtinKind(uint64_t h) {
    switch (h) {
    case 0xd9972838e1f3a16full: case 0xe12627ef9fbf72f5ull: return Dome;   // Sky (Opaque)
    case 0x98a771a0871307d3ull: case 0x21dd4e6159f0b6d7ull:                // Stars
    case 0xeb7192a1099bd0a4ull: case 0x58b1282fc2ca3f98ull: return Hide;   // SunMoon
    case 0x23a6297867ab6cc7ull: case 0x8480faaf71cfdfcaull: return Clouds; // Clouds
    // Vibrant Visuals (deferred): StarsForwardPBR / SunMoonForwardPBR pass ForwardPBRTransparent (main view; the
    // ForwardPBRTransparentSkyProbe variants draw the reflection probe and stay vanilla), CloudsForwardPBR the same
    case 0x27e7fe771b768d6full: case 0x273041e66dbfeec4ull:                // StarsForwardPBR (SM65 / SM60)
    case 0x96b9d91fc5b00e36ull: case 0x7d76328b74749755ull: return VvSky;  // SunMoonForwardPBR
    case 0x5e8488d4230fb66bull: case 0x525642266eb1f729ull: return Clouds; // CloudsForwardPBR
    default: return None;
    }
}

std::string_view canonical(std::wstring_view fileName) { // "starsforwardpbr.material.bin" -> "StarsForwardPBR"
    constexpr std::wstring_view ext = L".material.bin";
    if (fileName.size() <= ext.size()) return {};
    auto stem = fileName.substr(0, fileName.size() - ext.size());
    for (auto m : rf::skyscan::kMaterials) {
        if (m.size() != stem.size()) continue;
        bool eq = true;
        for (size_t i = 0; i < m.size() && eq; ++i) eq = towlower(stem[i]) == towlower(wchar_t(m[i]));
        if (eq) return m;
    }
    return {};
}
} // namespace

void learnMaterialFile(std::filesystem::path const& file, std::wstring_view fileName, char const* source) {
    auto name = canonical(fileName);
    if (name.empty()) return;
    std::ifstream f(file, std::ios::binary);
    if (!f) { std::lock_guard l(gLearnMutex); ++gLearnMissing; return; }
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    auto shaders = rf::skyscan::scan(d.data(), d.size());
    std::lock_guard l(gLearnMutex);
    ++gLearnFiles;
    if (source && *source) ++gLearnPack;
    for (auto& s : shaders)
        if (int k = rf::skyscan::kindFor(name, s); k > 0 && k < 5)
            if (gLearned.emplace(s.hash, k).second) ++gLearnKind[k];
}

void learnGameMaterials(std::filesystem::path const& dir) {
    for (auto m : rf::skyscan::kMaterials) {
        std::wstring fn(m.begin(), m.end());
        fn += L".material.bin";
        learnMaterialFile(dir / fn, fn, nullptr);
    }
}

int kindOfHash(uint64_t h) {
    {
        std::lock_guard l(gLearnMutex);
        if (auto it = gLearned.find(h); it != gLearned.end()) return it->second;
    }
    int k = builtinKind(h);
    if (k) ++gTaggedBuiltin;
    return k;
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
    g.dss    = d.DepthStencilState;
    g.vv     = kind == VvSky;
    g.farZ   = (d.DepthStencilState.DepthFunc == D3D12_COMPARISON_FUNC_GREATER || d.DepthStencilState.DepthFunc == D3D12_COMPARISON_FUNC_GREATER_EQUAL)
                   ? 0.0f : 1.0f;
    if (kind > 0 && kind < 5) ++gTagged[kind];
    std::lock_guard l(gPsoMutex);
    gGame[pso] = std::move(g);
}

int kindOf(ID3D12PipelineState* pso) {
    if (!pso) return None;
    std::lock_guard l(gPsoMutex);
    auto it = gGame.find(pso);
    if (it == gGame.end()) return None;
    return it->second.kind;
}

bool wanted() { return (gUi.replace && gActive.load(std::memory_order_relaxed) && !gBroken) || gWorldSky.load(std::memory_order_relaxed) != 0; }
bool wantRoot() { return wanted() && (gPictureOn.load(std::memory_order_relaxed) || gWorldSky.load(std::memory_order_relaxed) != 0); }
bool hideClouds() { return gUi.hideClouds || gWorldSky.load(std::memory_order_relaxed) != 0; }

namespace {
bool drawPicture(ID3D12GraphicsCommandList* list, GameState const& st, Calls const& calls, ID3D12Device* dev, GamePso const& g,
                 BfSkyFrameV1 const& h);
} // namespace

// ---- the match (Bedrock Forge MatchFx, BF_GetMatchFx): must match src/client/MatchFx.h in Bedrock Forge
namespace {
struct BfMatchTeamV1 { float pos[3]; float col[3]; uint32_t alive; int64_t changedMs; };
struct BfMatchEventV1 { int32_t x, y, z; uint32_t kind; int64_t atMs; float col[4]; uint32_t seed; uint32_t floorDy; };
struct BfMatchFxV1 {
    uint32_t size, version, active, frame;
    float phase, phaseTarget, timeOfDay;
    uint32_t bedsBroken;
    int64_t nowMs;
    uint32_t nTeams, nEvents;
    BfMatchTeamV1 teams[16];
    BfMatchEventV1 events[256];
    struct TeamEx { float threat; uint32_t elim; uint32_t mine; int64_t elimMs; } teamEx[16]; // v3
};
using MatchGet = BfMatchFxV1 const* (*)();
using MatchUse = void (*)(int);
using MatchPreview = void (*)(float);
struct MatchCb {
    float m0[4]; float tPos[16][4]; float tCol[16][4]; float eA[8][4]; float eB[8][4]; float eC[8][4];
    float m1[4]; float tAge[16][4]; float trMin[4], trMax[4]; float gA[48][4]; float gB[48][4];
};
static_assert(sizeof(MatchCb) == (57 + 1 + 16 + 2 + 96) * 16, "cbuffer M layout");
struct MatchFrame { float phase = 0; MatchCb cb{}; int beds = 0; float bedMinY = 0, bedCx = 0, bedCz = 0; };
std::mutex  gMatchMutex;
MatchFrame  gMatch;
int64_t     gMatchAt = -1;
std::string gMatchStatus = "Bedrock Forge not found (living sky stays golden)";
MatchPreview gMatchPreview = nullptr;
void matchFns(MatchGet& g, MatchUse& u) {
    static MatchGet sg = nullptr;
    static MatchUse su = nullptr;
    static int      delay = 0;
    if (!sg && delay-- <= 0) {
        delay = 240;
        if (HMODULE m = GetModuleHandleW(L"BedrockForgeCore.dll")) {
            sg = reinterpret_cast<MatchGet>(reinterpret_cast<void*>(GetProcAddress(m, "BF_GetMatchFx")));
            su = reinterpret_cast<MatchUse>(reinterpret_cast<void*>(GetProcAddress(m, "BF_MatchFxConsumer")));
            gMatchPreview = reinterpret_cast<MatchPreview>(reinterpret_cast<void*>(GetProcAddress(m, "BF_MatchFxPreview")));
        }
    }
    g = sg, u = su;
}
int64_t steadyMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
} // namespace
// once per frame (cheap after the first call): the match as the World FX shaders want it
// F6 "Test kill": a fake kill 6 blocks in front of you (normal / final kill alternate)
std::atomic<int> gTestKillReq{0};
struct TestKill { float p[3]; int64_t at = -100000; int kind = 3; } gTestKill;
void testKill() { ++gTestKillReq; }
static MatchFrame matchFrame(WorldFxLook const& look) {
    int64_t const now = steadyMs();
    std::lock_guard l(gMatchMutex);
    if (gMatchAt >= 0 && now - gMatchAt < 4) return gMatch;
    gMatchAt = now;
    MatchFrame f;
    MatchGet get; MatchUse use;
    matchFns(get, use);
    BfMatchFxV1 const* b = get ? get() : nullptr;
    bool const ok = b && b->version >= 3 && b->size >= sizeof(BfMatchFxV1);
    if (use) use(look.living && ok && gWorldSky.load() != 0 ? 1 : 0);
    if (gMatchPreview) gMatchPreview(look.phase); // the game's time of day follows the preview too
    if (ok) {
        f.phase = look.living ? std::clamp(b->phase, 0.0f, 2.0f) : 0.0f;
        uint32_t nt = std::min<uint32_t>(b->nTeams, 16), k = 0;
        for (uint32_t i = 0; i < nt; ++i) {
            auto const& t = b->teams[i];
            float const a = float(now - t.changedMs) / 1000.0f;
            float st = 0, fl = 0;
            if (t.alive) st = std::clamp(a / 1.5f, 0.0f, 1.0f);
            else {
                st = std::max(0.0f, 1.0f - a / 1.4f) * (0.75f + 0.25f * std::sin(a * 41.0f)); // the light gutters out
                fl = a < 0.18f ? a / 0.18f : std::exp(-(a - 0.18f) * 3.2f);
            }
            auto const& x = b->teamEx[i];
            auto& c = f.cb;
            std::memcpy(c.tPos[k], t.pos, 12); c.tPos[k][3] = st;
            std::memcpy(c.tCol[k], t.col, 12); c.tCol[k][3] = fl;
            c.tAge[k][0] = t.alive ? -1.0f : a;
            c.tAge[k][1] = x.threat;                                                            // enemies at this bed
            c.tAge[k][2] = x.elim ? std::clamp(float(now - x.elimMs) / 4000.0f, 0.0f, 1.0f) : 0.0f; // the island going dark
            c.tAge[k][3] = x.mine ? 1.0f : 0.0f;
            ++k;
        }
        f.cb.m0[0] = float(k);
        // the map, from its beds (every Bed Wars island has one): the cloud deck goes under the lowest island
        for (uint32_t i = 0; i < nt; ++i) {
            auto const& t = b->teams[i];
            f.bedMinY = f.beds ? std::min(f.bedMinY, t.pos[1]) : t.pos[1];
            f.bedCx += t.pos[0], f.bedCz += t.pos[2], ++f.beds;
        }
        if (f.beds) f.bedCx /= float(f.beds), f.bedCz /= float(f.beds);
        // the newest block events that are still playing (8 at most)
        uint32_t ne = 0;
        uint32_t const total = b->nEvents;
        for (uint32_t j = 0; j < 256 && j < total && ne < 8; ++j) {
            auto const& e = b->events[(total - 1 - j) % 256];
            float const age = float(now - e.atMs) / 1000.0f;
            if (age < 0) continue;
            if (age > 6.5f) break; // older ones are older still
            if ((e.kind == 2 && age > 0.4f) || (e.kind == 1 && age > 4.2f) || (e.kind == 3 && age > 3.6f) || (e.kind == 4 && age > 4.2f) ||
                (e.kind == 5 && age > 1.6f) || (e.kind == 6 && age > 6.5f) || e.kind > 6) continue;
            if (e.kind >= 3) f.cb.m1[2] = 1.0f;
            auto& c = f.cb;
            c.eA[ne][0] = float(e.x); c.eA[ne][1] = float(e.y); c.eA[ne][2] = float(e.z); c.eA[ne][3] = age;
            std::memcpy(c.eB[ne], e.col, 12); c.eB[ne][3] = float(e.kind);
            c.eC[ne][0] = float(e.seed % 100000u);
            c.eC[ne][1] = e.floorDy > 64 ? -1.0f : float(e.floorDy);
            ++ne;
        }
        if (float const ta = float(now - gTestKill.at) / 1000.0f; ta >= 0.0f && ta < 4.2f && ne < 8) {
            auto& c = f.cb;
            c.eA[ne][0] = std::floor(gTestKill.p[0]); c.eA[ne][1] = std::floor(gTestKill.p[1]); c.eA[ne][2] = std::floor(gTestKill.p[2]); c.eA[ne][3] = ta;
            c.eB[ne][0] = 0.30f; c.eB[ne][1] = 0.62f; c.eB[ne][2] = 1.0f; c.eB[ne][3] = float(gTestKill.kind);
            c.eC[ne][0] = 4242.0f; c.eC[ne][1] = -1.0f;
            f.cb.m1[2] = 1.0f;
            ++ne;
        }
        f.cb.m0[1] = float(ne);
        // fresh blocks for the trails: the newest 48 placements younger than 9 s
        uint32_t ng = 0;
        float    lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
        for (uint32_t j = 0; j < 256 && j < total && ng < 48; ++j) {
            auto const& e = b->events[(total - 1 - j) % 256];
            float const age = float(now - e.atMs) / 1000.0f;
            if (age > 9.0f) break;
            if (e.kind != 2 || age < 0) continue;
            auto& c = f.cb;
            c.gA[ng][0] = float(e.x); c.gA[ng][1] = float(e.y); c.gA[ng][2] = float(e.z); c.gA[ng][3] = age;
            std::memcpy(c.gB[ng], e.col, 12);
            float const p[3] = {float(e.x), float(e.y), float(e.z)};
            for (int a = 0; a < 3; ++a) lo[a] = std::min(lo[a], p[a]), hi[a] = std::max(hi[a], p[a] + 1.0f);
            ++ng;
        }
        f.cb.m1[0] = float(ng);
        std::memcpy(f.cb.trMin, lo, 12); std::memcpy(f.cb.trMax, hi, 12);
        gMatchStatus = "phase " + std::to_string(b->phase).substr(0, 4) + " (target " + std::to_string(int(b->phaseTarget)) + "), beds " +
                       std::to_string(b->nTeams) + " (" + std::to_string(b->bedsBroken) + " broken), block events " + std::to_string(total);
    } else if (get) gMatchStatus = "Bedrock Forge match bridge is older than this RF (update Bedrock Forge)";
    if (look.phase >= 0) f.phase = std::clamp(look.phase, 0.0f, 2.0f);
    f.cb.m0[2] = look.teams;
    f.cb.m0[3] = look.shards;
    f.cb.m1[1] = look.waves;
    if (look.trails <= 0.0f) f.cb.m1[0] = 0;
    gMatch = f;
    return f;
}
std::string matchStatus() { std::lock_guard l(gMatchMutex); return gMatchStatus; }
// per-frame constant buffer ring (upload heap) for cbuffer M
constexpr UINT kMcbSlots = 16, kMcbStride = 4096;
ComPtr<ID3D12Resource> gMcb;
uint8_t*               gMcbMap = nullptr;
UINT                   gMcbNext = 0;
static D3D12_GPU_VIRTUAL_ADDRESS uploadMatch(ID3D12Device* dev, MatchCb const& cb) {
    std::lock_guard l(gMatchMutex);
    if (!gMcb) {
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_UPLOAD};
        D3D12_RESOURCE_DESC   d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER, d.Width = kMcbSlots * kMcbStride, d.Height = 1, d.DepthOrArraySize = 1, d.MipLevels = 1;
        d.SampleDesc.Count = 1, d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gMcb)))) return 0;
        void* p = nullptr;
        D3D12_RANGE none{0, 0};
        if (FAILED(gMcb->Map(0, &none, &p))) { gMcb.Reset(); return 0; }
        gMcbMap = static_cast<uint8_t*>(p);
    }
    UINT const slot = gMcbNext++ % kMcbSlots;
    std::memcpy(gMcbMap + slot * kMcbStride, &cb, sizeof cb);
    return gMcb->GetGPUVirtualAddress() + UINT64(slot) * kMcbStride;
}

std::atomic<float> gQuality{1.0f}; // World FX quality 0..1 (adaptQuality, set from the World FX pass)

// The cloud deck: under the map. Anchored where you stand when you arrive in a world; a teleport (a new world, a server
// sending you to a match, a respawn) re-anchors it, a fall into the void never does (falls are continuous).
double  gLastPos[3]{};
int64_t gLastPosMs = 0;
static void updateAnchor(double const* pos) {
    int64_t const now = steadyMs();
    double const  jump = std::sqrt((pos[0] - gLastPos[0]) * (pos[0] - gLastPos[0]) + (pos[1] - gLastPos[1]) * (pos[1] - gLastPos[1]) +
                                   (pos[2] - gLastPos[2]) * (pos[2] - gLastPos[2]));
    bool const tp = gLastPosMs != 0 && now - gLastPosMs < 1000 && jump > 30.0;
    if (!gAnchored || tp || now - gLastPosMs > 5000 || std::hypot(pos[0] - gAnchor[0], pos[2] - gAnchor[2]) > 1500.0) {
        gAnchor[0] = pos[0]; gAnchor[1] = pos[1]; gAnchor[2] = pos[2];
        gAnchored  = true;
    }
    std::memcpy(gLastPos, pos, sizeof gLastPos);
    gLastPosMs = now;
}
// a Bed Wars map (beds known): the deck sits under the lowest island and the towers keep clear of the map's centre
static void deckOf(MatchFrame const& m, float& y, float& cx, float& cz) {
    if (m.beds >= 2) { y = float(std::floor(m.bedMinY) - 55.0 + gDeckOffset.load()); cx = m.bedCx; cz = m.bedCz; return; }
    y  = float(std::floor(gAnchor[1]) - 56.0 + gDeckOffset.load());
    cx = float(gAnchor[0]); cz = float(gAnchor[2] - 25.0);
}

bool drawWorldSky(ID3D12GraphicsCommandList* list, GameState const& st, Calls const& calls);
bool afterGameSky(ID3D12GraphicsCommandList* list, GameState const& st, Calls const& calls) {
    if (gWorldSky.load(std::memory_order_relaxed)) return drawWorldSky(list, st, calls);
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
    domePso = derived(dev.Get(), g, 0);
    quadPso = derived(dev.Get(), g, 1);
    if (!domePso || !quadPso) { ++gSkipped; setStage2("preparing the sky pipeline (built in the background)"); return false; }
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
    if (!h.active) return false;
    if (h.picture) return drawPicture(list, st, calls, dev.Get(), g, h);

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
        for (int i = 0; i < 16; ++i) std::memcpy(&v.p[i], h.params[i], 4);
        std::memcpy(v.prev, h.prevParams, sizeof v.prev);
        std::memcpy(v.f, cam.f, 12); std::memcpy(v.r, cam.r, 12); std::memcpy(v.u, cam.u, 12);
        v.txy[0] = cam.tx; v.txy[1] = cam.ty;
        v.info[0] = h.info[0]; v.info[1] = h.info[1]; v.info[2] = h.info[2]; v.info[3] = h.info[3]; // w: music pulse
        v.extra[0] = g.vv ? g.farZ : 1.0f; v.extra[1] = g.vv ? gUi.vvGain : 1.0f; v.extra[2] = g.vv ? 1.0f : 0.0f; v.extra[3] = 0;
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
            v.extra[0] = g.vv ? g.farZ : 1.0f; v.extra[1] = g.vv ? gUi.vvGain : 1.0f; v.extra[2] = g.vv ? 1.0f : 0.0f; v.extra[3] = 0;
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
    if ((gInjected.load() & 63) == 1) setStage2(std::string(g.vv ? "Vibrant Visuals: " : "") + "drawing in place of Bedrock's sky (" + std::to_string(gInjected.load()) + " frames, " + std::to_string(nq / 6) +
              " quads, vanilla sun/moon/stars hidden: " + std::to_string(gHidden.load()) + ")");
    return true;
}


namespace {
bool drawPicture(ID3D12GraphicsCommandList* list, GameState const& st, Calls const& calls, ID3D12Device* dev, GamePso const& g,
                 BfSkyFrameV1 const& h) {
    if (!st.rootKnown) { setStage2("My picture: waiting for the game's root state (tracking starts with the picture)"); return false; }
    ComPtr<ID3D12Resource> pic;
    {
        std::lock_guard l(gImgMutex);
        pic = gPicture;
    }
    if (!pic) { setStage2("My picture: loading the image"); return false; }
    ID3D12PipelineState* pso = nullptr;
    UINT                 slot;
    {
        std::lock_guard l(gPicMutex);
        if (!makePictureRoot(dev)) { setStage2("My picture: root signature failed"); return false; }
        slot = gPicNext++ % kRing;
        gPicKeep[slot] = pic; // alive while frames that use it are in flight
        D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
        sv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sv.Texture2D.MipLevels     = 1;
        auto cpuH = gPicHeap->GetCPUDescriptorHandleForHeapStart();
        cpuH.ptr += SIZE_T(slot) * gPicInc;
        dev->CreateShaderResourceView(pic.Get(), &sv, cpuH);
    }
    pso = extraPso(dev, st.pso, 2);
    if (!pso) return false;
    CamB  cam = pickCam(h);
    float c[16]{};
    std::memcpy(c + 0, cam.f, 12); c[3] = cam.tx;
    std::memcpy(c + 4, cam.r, 12); c[7] = cam.ty;
    std::memcpy(c + 8, cam.u, 12); c[11] = h.pictureAlpha;
    c[12] = h.pictureBright;
    c[13] = g.vv ? g.farZ : 1.0f;   // far depth
    c[14] = g.vv ? gUi.vvGain : 1.0f; // HDR gain
    c[15] = g.vv ? 1.0f : 0.0f;     // sRGB -> linear
    auto gpuH = gPicHeap->GetGPUDescriptorHandleForHeapStart();
    gpuH.ptr += UINT64(slot) * gPicInc;
    ID3D12DescriptorHeap* heaps[] = {gPicHeap.Get()};
    calls.setHeaps(list, 1, heaps);
    calls.setRootSig(list, gPicRoot.Get());
    calls.setConsts(list, 0, 16, c, 0);
    calls.setTable(list, 1, gpuH.ptr);
    calls.setPso(list, pso);
    calls.setTopo(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    calls.draw(list, 3, 1, 0, 0);
    calls.restoreRoot(list);
    calls.setPso(list, st.pso);
    calls.setTopo(list, st.topo);
    ++gInjected;
    if ((gInjected.load() & 63) == 1) setStage2("My picture in place of Bedrock's sky (" + std::to_string(gInjected.load()) + " frames)");
    return true;
}
} // namespace

bool drawWorldFxPass(ID3D12GraphicsCommandList* list, GameState const& st, Calls const& calls);
bool afterGameClouds(ID3D12GraphicsCommandList* list, GameState const& st, Calls const& calls) {
    if (gWorldSky.load(std::memory_order_relaxed)) return drawWorldFxPass(list, st, calls); // the World FX look rides here
    if (!st.nVb || !st.vb || !st.haveTopo) return false;
    auto copy = bfCopy();
    if (!copy) return false;
    static thread_local std::vector<BfSkyVertV1> host(kMaxVerts);
    BfSkyFrameV1 h{};
    h.size     = sizeof h;
    uint32_t n = copy(&h, host.data(), kMaxVerts);
    if (!h.active || h.picture || !n) return false;
    auto snap = rf::lyrics::camera(); // weather needs the real projection (depth must match the terrain)
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (!snap.valid || now - snap.atMs > 250) { ++gWeatherNoCam; return false; }
    ComPtr<ID3D12Device> dev;
    if (FAILED(list->GetDevice(IID_PPV_ARGS(&dev)))) return false;
    ID3D12PipelineState* pso = extraPso(dev.Get(), st.pso, 3);
    if (!pso) return false;
    {
        static std::mutex ringMutex;
        std::lock_guard   l(ringMutex);
        if (!ensureRing(dev.Get())) return false;
    }
    float rows[4][4];
    vpRows(snap.forward, snap.right, snap.up, snap.proj, rows);
    UINT slot;
    {
        std::lock_guard l(gPsoMutex);
        slot = gRingNext++ % kRing;
    }
    struct WVert { float pos[3], uv[2]; uint32_t col; float m[4][4]; };
    static_assert(sizeof(WVert) == 88, "weather vertex = input layout");
    WVert* wv  = reinterpret_cast<WVert*>(gRingMap[slot]);
    UINT   max = UINT(kRingBytes / sizeof(WVert));
    UINT   nv  = 0;
    static int const kCorner[6] = {0, 1, 2, 0, 2, 3};
    for (uint32_t q = 0; q + 3 < n && nv + 6 <= max; q += 4) {
        if (!(host[q].flags & BF_SKY_WORLD)) continue;
        for (int k = 0; k < 6; ++k) {
            auto const& s = host[q + uint32_t(kCorner[k])];
            WVert&      v = wv[nv++];
            std::memcpy(v.pos, s.pos, 12);
            v.uv[0] = s.uv[0]; v.uv[1] = s.uv[1];
            v.col   = s.color;
            std::memcpy(v.m, rows, sizeof rows);
        }
    }
    if (!nv) return true;
    D3D12_VERTEX_BUFFER_VIEW vw{gRing[slot]->GetGPUVirtualAddress(), UINT(nv * sizeof(WVert)), UINT(sizeof(WVert))};
    calls.setPso(list, pso);
    calls.setTopo(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    calls.setVb(list, 0, 1, &vw);
    calls.draw(list, nv, 1, 0, 0);
    calls.setPso(list, st.pso);
    calls.setTopo(list, st.topo);
    calls.setVb(list, 0, st.nVb, st.vb);
    ++gWeatherFrames;
    return true;
}

void noteHidden() { ++gHidden; }

bool drawWorldSky(ID3D12GraphicsCommandList* list, GameState const& st, Calls const& calls) {
    GamePso g;
    {
        std::lock_guard l(gPsoMutex);
        auto it = gGame.find(st.pso);
        if (it == gGame.end()) return false;
        g = it->second;
    }
    if (g.vv) { wsStatus("Vibrant Visuals is on: the Cloud Sea draws on the classic renderer for now (turn VV off)"); return false; }
    if (!st.rootKnown) { wsStatus("waiting for the game's root state"); return false; }
    if (!gWsTexUp.load()) return false;
    auto    snap = rf::lyrics::camera();
    int64_t now  = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (!snap.valid || now - snap.atMs > 250 || snap.proj[0] == 0 || snap.proj[5] == 0) { wsStatus("waiting for the game camera"); return false; }
    ComPtr<ID3D12Device> dev;
    if (FAILED(list->GetDevice(IID_PPV_ARGS(&dev)))) return false;
    ID3D12PipelineState* pso = derived(dev.Get(), g, 4);
    if (!pso) { wsStatus("building the sky pipeline (in the background)"); return false; }
    double const* pos = snap.pos;
    updateAnchor(pos);
    float const t = float(std::fmod(std::chrono::duration<double>(std::chrono::steady_clock::now() - gWsT0).count(), 20000.0));
    float c[20]{};
    std::memcpy(c + 0, snap.forward, 12); c[3] = 1.0f / std::fabs(snap.proj[0]);
    std::memcpy(c + 4, snap.right, 12);   c[7] = 1.0f / std::fabs(snap.proj[5]);
    std::memcpy(c + 8, snap.up, 12);      c[11] = t;
    c[12] = float(pos[0]); c[13] = float(pos[1]); c[14] = float(pos[2]);
    MatchFrame const mfs = matchFrame(worldFxLook());
    deckOf(mfs, c[15], c[16], c[17]);
    c[18] = gQuality.load(); c[19] = mfs.phase;
    ID3D12DescriptorHeap* heaps[] = {gWsHeap.Get()};
    calls.setHeaps(list, 1, heaps);
    calls.setRootSig(list, gWsRoot.Get());
    calls.setConsts(list, 0, 20, c, 0);
    calls.setTable(list, 1, gWsHeap->GetGPUDescriptorHandleForHeapStart().ptr);
    calls.setPso(list, pso);
    calls.setTopo(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    calls.draw(list, 3, 1, 0, 0);
    calls.restoreRoot(list);
    calls.setPso(list, st.pso);
    if (st.haveTopo) calls.setTopo(list, st.topo);
    long const n = ++gWsFrames;
    if ((n & 63) == 1) wsStatus("drawing (" + std::to_string(n) + " frames, deck at y " + std::to_string(int(c[15])) + ")");
    return true;
}

namespace {
WorldFxLook           gLook;
std::filesystem::path gWfxConfig;
struct WfxBufs { ComPtr<ID3D12Resource> scene, depth; UINT64 sceneSize = 0, depthSize = 0; } gWfxBufs;
std::atomic<long>     gWfxFrames{0};
bool wfxBuffer(ID3D12Device* dev, ComPtr<ID3D12Resource>& b, UINT64& have, UINT64 need) {
    if (b && have >= need) return true;
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC   d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER, d.Width = need, d.Height = 1, d.DepthOrArraySize = 1, d.MipLevels = 1;
    d.SampleDesc.Count = 1, d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> nb;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&nb)))) return false;
    if (b) gGrave.push_back(b); // still referenced by frames in flight
    b = nb, have = need;
    return true;
}
void wfxBarrier(ID3D12GraphicsCommandList* l, ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
    if (a == b) return;
    D3D12_RESOURCE_BARRIER x{};
    x.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    x.Transition.pResource = r, x.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    x.Transition.StateBefore = a, x.Transition.StateAfter = b;
    l->ResourceBarrier(1, &x);
}
} // namespace

// The pass gives back quality to hold ~150+ FPS: frame time measured between our own draws (one per frame); above
// 1/150 s the quality falls quickly, under 1/175 s it creeps back. 0 = cheapest look, 1 = full.
float adaptQuality() {
    static int64_t lastUs = 0;
    static float   ema = 1.0f / 160.0f;
    int64_t const  now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    float q = gQuality.load();
    if (lastUs) {
        float dt = float(now - lastUs) * 1e-6f;
        if (dt > 0.0f && dt < 0.25f) {
            ema = ema * 0.95f + dt * 0.05f;
            if (ema > 1.0f / 150.0f) q = std::max(0.0f, q - 0.01f);
            else if (ema < 1.0f / 175.0f) q = std::min(1.0f, q + 0.002f);
        }
    }
    lastUs = now;
    gQuality = q;
    return q;
}

bool drawWorldFxPass(ID3D12GraphicsCommandList* list, GameState const& st, Calls const& calls) {
    WorldFxLook const look = gLook;
    // the pass also carries the cloud sea (sky pixels + terrain under the deck), so it runs with the block look off too
    if (!st.rootKnown) { wfxStatus("waiting for the game's root state"); return false; }
    if (!gWsTexUp.load() || !gWsHeap) { wfxStatus("waiting for the cloud textures"); return false; }
    GamePso g;
    {
        std::lock_guard l(gPsoMutex);
        auto it = gGame.find(st.pso);
        if (it == gGame.end()) return false;
        g = it->second;
    }
    if (g.vv) { wfxStatus("Vibrant Visuals: not yet (classic renderer only)"); return false; }
    size_t rt0 = 0, dsv = 0;
    if (!rf::worldfx::listTargets(list, rt0, dsv)) { wfxStatus("waiting for the frame's targets"); return false; }
    ID3D12Resource* rt = rf::uicapture::rtvResourceOf(rt0);
    ID3D12Resource* ds = rf::glassarmor::dsvResource(dsv);
    if (!rt || !ds) { wfxStatus("frame targets unknown"); return false; }
    auto rd = rt->GetDesc(), dd = ds->GetDesc();
    if (rd.SampleDesc.Count > 1 || dd.SampleDesc.Count > 1) { wfxStatus("MSAA target: not supported"); return false; }
    bool const bgra = rd.Format == DXGI_FORMAT_B8G8R8A8_UNORM || rd.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS || rd.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    bool const rgba = rd.Format == DXGI_FORMAT_R8G8B8A8_UNORM || rd.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS || rd.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    if (!bgra && !rgba) { wfxStatus("colour target is not 8-bit RGBA"); return false; }
    if (dd.Format != DXGI_FORMAT_D24_UNORM_S8_UINT && dd.Format != DXGI_FORMAT_R24G8_TYPELESS) { wfxStatus("depth is not D24S8"); return false; }
    auto    snap = rf::lyrics::camera();
    int64_t now  = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (!snap.valid || now - snap.atMs > 250 || snap.proj[0] == 0 || snap.proj[5] == 0) { wfxStatus("waiting for the game camera"); return false; }
    ComPtr<ID3D12Device> dev;
    if (FAILED(list->GetDevice(IID_PPV_ARGS(&dev)))) return false;
    ID3D12PipelineState* pso = derived(dev.Get(), g, 5);
    if (!pso) { wfxStatus("building the pass pipeline (in the background)"); return false; }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fpC{}, df{};
    UINT64 rTot = 0, dTot = 0;
    dev->GetCopyableFootprints(&rd, 0, 1, 0, &fpC, nullptr, nullptr, &rTot);
    dev->GetCopyableFootprints(&dd, 0, 1, 0, &df, nullptr, nullptr, &dTot);
    {
        std::lock_guard l(gWfxMutex);
        if (!wfxBuffer(dev.Get(), gWfxBufs.scene, gWfxBufs.sceneSize, rTot) || !wfxBuffer(dev.Get(), gWfxBufs.depth, gWfxBufs.depthSize, dTot)) {
            gWfxStatus = "buffers failed"; return false;
        }
    }
    ID3D12Resource* sb = gWfxBufs.scene.Get();
    ID3D12Resource* db = gWfxBufs.depth.Get();
    auto const rtState = D3D12_RESOURCE_STATES(rf::glassarmor::stateOf(rt, D3D12_RESOURCE_STATE_RENDER_TARGET));
    auto const dsState = D3D12_RESOURCE_STATES(rf::glassarmor::stateOf(ds, D3D12_RESOURCE_STATE_DEPTH_WRITE));
    wfxBarrier(list, rt, rtState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    wfxBarrier(list, ds, dsState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = sb, dst.PlacedFootprint = fpC, src.pResource = rt, src.SubresourceIndex = 0;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    dst.pResource = db, dst.PlacedFootprint = df, src.pResource = ds, src.SubresourceIndex = 0;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    wfxBarrier(list, rt, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    wfxBarrier(list, ds, D3D12_RESOURCE_STATE_COPY_SOURCE, dsState);
    wfxBarrier(list, sb, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    wfxBarrier(list, db, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    float c[40]{};
    std::memcpy(c + 0, snap.forward, 12); c[3] = 1.0f / std::fabs(snap.proj[0]);
    std::memcpy(c + 4, snap.right, 12);   c[7] = 1.0f / std::fabs(snap.proj[5]);
    std::memcpy(c + 8, snap.up, 12);      c[11] = float(std::fmod(std::chrono::duration<double>(std::chrono::steady_clock::now() - gWsT0).count(), 1000.0));
    c[12] = 0.025f; c[13] = 0; c[14] = 0; c[15] = bgra ? 1.0f : 0.0f; // Bedrock world near plane (probe-verified)
    c[16] = float(rd.Width); c[17] = float(rd.Height); c[18] = float(fpC.Footprint.RowPitch); c[19] = float(df.Footprint.RowPitch);
    c[20] = 0.17605f; c[21] = 0.11247f; c[22] = -0.97794f; c[23] = look.shadows ? look.shadow : 0.0f;
    if (gTestKillReq.exchange(0)) {
        for (int a = 0; a < 3; ++a) gTestKill.p[a] = float(snap.pos[a]) + snap.forward[a] * 6.0f;
        gTestKill.p[1] = float(snap.pos[1]) - 1.62f;
        gTestKill.kind = gTestKill.kind == 3 ? 4 : 3;
        gTestKill.at = steadyMs();
    }
    MatchFrame const mf = matchFrame(look);
    updateAnchor(snap.pos);
    D3D12_GPU_VIRTUAL_ADDRESS const mcb = uploadMatch(dev.Get(), mf.cb);
    if (!mcb) { wfxStatus("match buffer failed"); return false; }
    c[24] = look.shadows ? look.ao : 0.0f; c[25] = look.shadows ? look.rim : 0.0f; c[26] = float(look.debug); c[27] = mf.phase;
    // the cloud sea, depth-tested against the terrain: same deck + clock as the sky pass (drawWorldSky)
    c[28] = float(snap.pos[0]); c[29] = float(snap.pos[1]); c[30] = float(snap.pos[2]);
    deckOf(mf, c[31], c[32], c[33]);
    c[34] = float(std::fmod(std::chrono::duration<double>(std::chrono::steady_clock::now() - gWsT0).count(), 20000.0));
    c[35] = gAnchored ? 1.0f : 0.0f;
    c[36] = look.rays; c[37] = look.clouds; c[38] = adaptQuality(); c[39] = look.ink;
    ID3D12DescriptorHeap* heaps[] = {gWsHeap.Get()};
    calls.setHeaps(list, 1, heaps);
    calls.setRootSig(list, gWfxRoot.Get());
    calls.setConsts(list, 0, 40, c, 0);
    calls.setTable(list, 3, gWsHeap->GetGPUDescriptorHandleForHeapStart().ptr);
    list->SetGraphicsRootShaderResourceView(1, sb->GetGPUVirtualAddress());
    list->SetGraphicsRootShaderResourceView(2, db->GetGPUVirtualAddress());
    list->SetGraphicsRootConstantBufferView(4, mcb);
    calls.setPso(list, pso);
    calls.setTopo(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    calls.draw(list, 3, 1, 0, 0);
    wfxBarrier(list, sb, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    wfxBarrier(list, db, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    calls.restoreRoot(list);
    calls.setPso(list, st.pso);
    if (st.haveTopo) calls.setTopo(list, st.topo);
    long const n = ++gWfxFrames;
    if ((n & 63) == 1) wfxStatus("drawing (" + std::to_string(n) + " frames, " + std::to_string(rd.Width) + "x" + std::to_string(rd.Height) +
                                 ", quality " + std::to_string(int(gQuality.load() * 100.0f)) + "%)");
    return true;
}

WorldFxLook& worldFxLook() { return gLook; }
std::string  worldFxStatus() { std::lock_guard l(gWfxMutex); return gWfxStatus; }
void loadWorldFxSettings(std::filesystem::path const& configDir) {
    gWfxConfig = configDir / "WorldFx.txt";
    std::ifstream in(gWfxConfig);
    std::string   k;
    float         v;
    while (in >> k >> v) {
        if (k == "cloudsea") { gWorldSky = int(v); rf::worldfx::setTargetsWanted(v != 0); }
        else if (k == "shadows") gLook.shadows = v != 0;
        else if (k == "shadow") gLook.shadow = std::clamp(v, 0.f, 1.f);
        else if (k == "ao") gLook.ao = std::clamp(v, 0.f, 1.f);
        else if (k == "rim") gLook.rim = std::clamp(v, 0.f, 2.f);
        else if (k == "deck") gDeckOffset = std::clamp(v, -80.f, 80.f);
        else if (k == "rays") gLook.rays = std::clamp(v, 0.f, 3.f);
        else if (k == "clouds") gLook.clouds = std::clamp(v, 0.f, 1.f);
        else if (k == "ink") gLook.ink = std::clamp(v, 0.f, 1.f);
        else if (k == "living") gLook.living = v != 0;
        else if (k == "teams") gLook.teams = std::clamp(v, 0.f, 2.f);
        else if (k == "shards") gLook.shards = std::clamp(v, 0.f, 1.f);
        else if (k == "waves") gLook.waves = std::clamp(v, 0.f, 2.f);
        else if (k == "trails") gLook.trails = std::clamp(v, 0.f, 1.f);
    }
}
void saveWorldFxSettings() {
    if (gWfxConfig.empty()) return;
    std::ofstream out(gWfxConfig);
    out << "cloudsea " << gWorldSky.load() << "\nshadows " << (gLook.shadows ? 1 : 0) << "\nshadow " << gLook.shadow << "\nao " << gLook.ao
        << "\nrim " << gLook.rim << "\ndeck " << gDeckOffset.load() << "\nrays " << gLook.rays << "\nclouds " << gLook.clouds << "\nink " << gLook.ink
        << "\nliving " << (gLook.living ? 1 : 0) << "\nteams " << gLook.teams << "\nshards " << gLook.shards << "\nwaves " << gLook.waves << "\ntrails " << gLook.trails << "\n";
}

void setWorldSky(int kind) {
    gWorldSky = kind;
    rf::worldfx::setTargetsWanted(kind != 0);
    if (!kind) wsStatus("off");
    else if (!gWsTexUp.load()) wsStatus("starting (textures are made at the next frame)");
}
int         worldSky() { return gWorldSky.load(); }
void        setWorldSkyDeck(float o) { gDeckOffset = o; }
float       worldSkyDeck() { return gDeckOffset.load(); }
std::string worldSkyStatus() { std::lock_guard l(gWsMutex); return gWsStatus; }
bool vvWanted() { return gUi.vv && gWorldSky.load(std::memory_order_relaxed) == 0; }
bool claimVvFrame() {
    uint64_t const now = gPresentNo.load(std::memory_order_relaxed);
    return gVvClaimed.exchange(now) != now;
}

} // namespace rf::sky
