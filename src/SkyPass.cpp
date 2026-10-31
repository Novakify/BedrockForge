// Renderer Foundation - LyricsFlow port: live sky preview pass (see SkyPass.h). Original code for Renderer Foundation.
// STAGED, NOT RUN IN GAME YET: compile-verified by CI only. Same D3D12 patterns as LyricsPass (root constants + SRV
// table per ring slot, premultiplied blend, PSO per back-buffer format, WIC decode on a worker thread).
#include "SkyPass.h"

#include <algorithm>
#include <atomic>
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
} gUi;

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
        if (!createPso({shaders::VSSky, sizeof shaders::VSSky}, {shaders::PSSky, sizeof shaders::PSSky}, bd.Format, gPsoDome)) return;
        if (!createPso({shaders::VSSkyQuad, sizeof shaders::VSSkyQuad}, {shaders::PSSkyQuad, sizeof shaders::PSSkyQuad}, bd.Format, gPsoQuad)) return;
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

} // namespace rf::sky
