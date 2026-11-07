// Renderer Foundation - LyricsForge video screen (see ScreenPass.h). Original code for Renderer Foundation.
// D3D12 patterns follow LyricsPass.cpp: one ring slot per back buffer (upload buffer + descriptor tables written only for
// the slot being recorded), resources replaced on a resize go to a graveyard for a few Presents.
#include "ScreenPass.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <imgui.h>

#include "Anime4KPasses.h"
#include "BfLyrics.h"
#include "D3D12Api.h"
#include "ScreenCapture.h"

#if __has_include("ScreenShaders.h")
#include "ScreenShaders.h"
#endif

namespace rf::screen {
namespace {
using Microsoft::WRL::ComPtr;

constexpr UINT kSlots     = 4;
constexpr UINT kBind      = 8;                               // t0..t7 per pass
constexpr UINT kFitTable  = kA4kPassCount;                   // table index of the fit pass
constexpr UINT kMipTable  = kA4kPassCount + 1;               // + i for mip i+1
constexpr UINT kTables    = kA4kPassCount + 1 + (kOutMips - 1);
constexpr UINT kRtvCount  = 64;
constexpr DXGI_FORMAT kMid = DXGI_FORMAT_R16G16B16A16_FLOAT; // Anime4K intermediates
constexpr DXGI_FORMAT kOut = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT kSrc = DXGI_FORMAT_B8G8R8A8_UNORM;     // what Windows.Graphics.Capture hands us

// ---------------------------------------------------------------- settings / status (BF link)
BfScreenConfigV1 gCfg{};
cap::Status      gCap;
std::string      gWhy = "waiting for Bedrock Forge"; // GPU side problems (shown after the capture status)
uint32_t         gRan = 0;                           // what ran on the last frame (BfScreenUpscale, +4 = x2 stage)
float            gBuildMs = 0;
bool             gD3D11 = false;

BF_GetScreenConfig_t bfGet() {
    static BF_GetScreenConfig_t fn = nullptr;
    static int                  delay = 0;
    if (fn || delay-- > 0) return fn;
    delay = 240;
    if (HMODULE m = GetModuleHandleW(L"BedrockForgeCore.dll"))
        fn = reinterpret_cast<BF_GetScreenConfig_t>(reinterpret_cast<void*>(GetProcAddress(m, "BF_GetScreenConfig")));
    return fn;
}
BF_PutScreenStatus_t bfPut() {
    static BF_PutScreenStatus_t fn = nullptr;
    static int                  delay = 0;
    if (fn || delay-- > 0) return fn;
    delay = 240;
    if (HMODULE m = GetModuleHandleW(L"BedrockForgeCore.dll"))
        fn = reinterpret_cast<BF_PutScreenStatus_t>(reinterpret_cast<void*>(GetProcAddress(m, "BF_PutScreenStatus")));
    return fn;
}

/// Where the picture sits on the 16:9 screen (uv x0, y0, w, h): as big as it fits, bars on the other axis.
void letterbox(UINT w, UINT h, float r[4]) {
    float const s  = std::min(float(kOutW) / float(std::max(1u, w)), float(kOutH) / float(std::max(1u, h)));
    float const rw = float(w) * s / float(kOutW), rh = float(h) * s / float(kOutH);
    r[0] = (1.0f - rw) * 0.5f; r[1] = (1.0f - rh) * 0.5f; r[2] = rw; r[3] = rh;
}

// ---------------------------------------------------------------- D3D12
struct Tex {
    ComPtr<ID3D12Resource> r;
    UINT                   w = 0, h = 0;
    DXGI_FORMAT            fmt = kMid;
    D3D12_RESOURCE_STATES  state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    UINT                   rtv = 0;
};

struct D12 {
    ComPtr<ID3D12Device>         dev;
    ComPtr<ID3D12RootSignature>  root;
    ComPtr<ID3D12PipelineState>  a4k[kA4kPassCount];
    ComPtr<ID3D12PipelineState>  fit;
    ComPtr<ID3D12DescriptorHeap> srv, rtv;
    UINT                         srvInc = 0, rtvInc = 0, rtvNext = kOutMips;
    Tex                          out;                 // 1920x1080, kOutMips mips (rtv 0..kOutMips-1 = its mips)
    D3D12_RESOURCE_STATES        outState[kOutMips]{};
    Tex                          src;                 // the captured frame (BGRA)
    std::map<std::string, Tex>   tex;                 // Anime4K intermediates, "RS.conv2d_tf", "UM.MAIN", ...
    ComPtr<ID3D12Resource>       upload[kSlots];
    uint8_t*                     uploadMap[kSlots]{};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT64                       uploadSize = 0;
    std::vector<std::pair<ComPtr<ID3D12Resource>, uint64_t>> grave;
    cap::Frame                   frame;
    uint64_t                     seq = 0;
    bool                         haveOut = false;     // the screen texture holds a picture
    bool                         broken = false;
} g;

void fail(std::string const& what, HRESULT hr) {
    char b[16];
    std::snprintf(b, sizeof b, "%08X", uint32_t(hr));
    gWhy     = "off: " + what + " failed (hr=0x" + b + ")";
    g.broken = true;
}
D3D12_CPU_DESCRIPTOR_HANDLE srvCpu(UINT i) { auto h = g.srv->GetCPUDescriptorHandleForHeapStart(); h.ptr += SIZE_T(i) * g.srvInc; return h; }
D3D12_GPU_DESCRIPTOR_HANDLE srvGpu(UINT i) { auto h = g.srv->GetGPUDescriptorHandleForHeapStart(); h.ptr += UINT64(i) * g.srvInc; return h; }
D3D12_CPU_DESCRIPTOR_HANDLE rtvCpu(UINT i) { auto h = g.rtv->GetCPUDescriptorHandleForHeapStart(); h.ptr += SIZE_T(i) * g.rtvInc; return h; }

void barrier(ID3D12GraphicsCommandList* l, ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b,
             UINT sub = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) {
    if (a == b) return;
    D3D12_RESOURCE_BARRIER x{};
    x.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    x.Transition.pResource   = r;
    x.Transition.Subresource = sub;
    x.Transition.StateBefore = a;
    x.Transition.StateAfter  = b;
    l->ResourceBarrier(1, &x);
}
void toState(ID3D12GraphicsCommandList* l, Tex& t, D3D12_RESOURCE_STATES s) { barrier(l, t.r.Get(), t.state, s); t.state = s; }

void srvOf(ID3D12Resource* r, DXGI_FORMAT f, UINT mip, UINT mips, D3D12_CPU_DESCRIPTOR_HANDLE h) {
    D3D12_SHADER_RESOURCE_VIEW_DESC s{};
    s.Format                    = f;
    s.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
    s.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    s.Texture2D.MostDetailedMip = mip;
    s.Texture2D.MipLevels       = mips;
    g.dev->CreateShaderResourceView(r, &s, h);
}
void nullSrv(D3D12_CPU_DESCRIPTOR_HANDLE h) { srvOf(nullptr, kOut, 0, 1, h); }

void bury(ComPtr<ID3D12Resource>& r, uint64_t present) {
    if (r) g.grave.push_back({r, present});
    r.Reset();
}

bool createStatic(ID3D12Device* dev) {
    g.dev = dev;
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, kBind, 0, 0, 0};
    D3D12_ROOT_PARAMETER   params[2]{};
    params[0].ParameterType    = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants        = {0, 0, 8};
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].ParameterType    = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable  = {1, &range};
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC smp[2]{};
    for (UINT i = 0; i < 2; ++i) {
        smp[i].Filter           = i == 0 ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        smp[i].AddressU = smp[i].AddressV = smp[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        smp[i].MaxLOD           = D3D12_FLOAT32_MAX;
        smp[i].ShaderRegister   = i;
        smp[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    D3D12_ROOT_SIGNATURE_DESC rs{2, params, 2, smp, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob>          blob, err;
    HRESULT                   hr = rf::d3d12api::SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    if (FAILED(hr)) { fail("screen root signature", hr); return false; }
    if (FAILED(hr = dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&g.root)))) {
        fail("screen CreateRootSignature", hr);
        return false;
    }
    D3D12_DESCRIPTOR_HEAP_DESC sd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kSlots * kTables * kBind, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    D3D12_DESCRIPTOR_HEAP_DESC rd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kRtvCount, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    if (FAILED(hr = dev->CreateDescriptorHeap(&sd, IID_PPV_ARGS(&g.srv)))) { fail("screen SRV heap", hr); return false; }
    if (FAILED(hr = dev->CreateDescriptorHeap(&rd, IID_PPV_ARGS(&g.rtv)))) { fail("screen RTV heap", hr); return false; }
    g.srvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    g.rtvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // the screen texture: fixed size, never replaced, starts black
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC   d{};
    d.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width            = kOutW;
    d.Height           = kOutH;
    d.DepthOrArraySize = 1;
    d.MipLevels        = UINT16(kOutMips);
    d.Format           = kOut;
    d.SampleDesc.Count = 1;
    d.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE cv{};
    cv.Format   = kOut;
    cv.Color[3] = 1.0f;
    if (FAILED(hr = dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_RENDER_TARGET, &cv, IID_PPV_ARGS(&g.out.r)))) {
        fail("screen texture", hr);
        return false;
    }
    g.out.w = kOutW; g.out.h = kOutH; g.out.fmt = kOut;
    for (UINT m = 0; m < kOutMips; ++m) {
        D3D12_RENDER_TARGET_VIEW_DESC v{};
        v.Format             = kOut;
        v.ViewDimension      = D3D12_RTV_DIMENSION_TEXTURE2D;
        v.Texture2D.MipSlice = m;
        dev->CreateRenderTargetView(g.out.r.Get(), &v, rtvCpu(m));
        g.outState[m] = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
    return true;
}

bool createPso(D3D12_SHADER_BYTECODE ps, DXGI_FORMAT fmt, ComPtr<ID3D12PipelineState>& out) {
#if RF_HAVE_SCREEN_SHADERS
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = g.root.Get();
    d.VS             = fmt == kOut ? D3D12_SHADER_BYTECODE{shaders::VSFit, sizeof shaders::VSFit} : D3D12_SHADER_BYTECODE{shaders::A4K_VS, sizeof shaders::A4K_VS};
    d.PS             = ps;
    d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.SampleMask                      = UINT_MAX;
    d.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable = TRUE;
    d.PrimitiveTopologyType           = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets                = 1;
    d.RTVFormats[0]                   = fmt;
    d.SampleDesc.Count                = 1;
    HRESULT hr = g.dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&out));
    if (FAILED(hr)) { fail("screen pipeline", hr); return false; }
    return true;
#else
    (void)ps; (void)fmt; (void)out;
    fail("screen shaders not compiled into this build (run tools/gen_screen.py)", E_NOTIMPL);
    return false;
#endif
}

bool makeTex(Tex& t, UINT w, UINT h, DXGI_FORMAT fmt, bool rt, D3D12_RESOURCE_STATES initial) {
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC   d{};
    d.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width            = w;
    d.Height           = h;
    d.DepthOrArraySize = 1;
    d.MipLevels        = 1;
    d.Format           = fmt;
    d.SampleDesc.Count = 1;
    d.Flags            = rt ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET : D3D12_RESOURCE_FLAG_NONE;
    D3D12_CLEAR_VALUE cv{};
    cv.Format = fmt;
    HRESULT hr = g.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, initial, rt ? &cv : nullptr, IID_PPV_ARGS(t.r.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) { fail("screen texture " + std::to_string(w) + "x" + std::to_string(h), hr); return false; }
    t.w = w; t.h = h; t.fmt = fmt; t.state = initial;
    if (rt) {
        if (g.rtvNext >= kRtvCount) { fail("screen RTV heap full", E_OUTOFMEMORY); return false; }
        t.rtv = g.rtvNext++;
        g.dev->CreateRenderTargetView(t.r.Get(), nullptr, rtvCpu(t.rtv));
    }
    return true;
}

/// A new picture size: every size-bound resource is replaced (old ones die a few Presents later).
bool resize(UINT w, UINT h, uint64_t present) {
    for (auto& [k, t] : g.tex) bury(t.r, present);
    g.tex.clear();
    g.rtvNext = kOutMips;
    bury(g.src.r, present);
    for (UINT s = 0; s < kSlots; ++s) { bury(g.upload[s], present); g.uploadMap[s] = nullptr; }
    if (!makeTex(g.src, w, h, kSrc, false, D3D12_RESOURCE_STATE_COPY_DEST)) return false;
    D3D12_RESOURCE_DESC td = g.src.r->GetDesc();
    g.dev->GetCopyableFootprints(&td, 0, 1, 0, &g.fp, nullptr, nullptr, &g.uploadSize);
    D3D12_HEAP_PROPERTIES up{D3D12_HEAP_TYPE_UPLOAD};
    D3D12_RESOURCE_DESC   bd{};
    bd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width            = g.uploadSize;
    bd.Height           = 1;
    bd.DepthOrArraySize = 1;
    bd.MipLevels        = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    for (UINT s = 0; s < kSlots; ++s) {
        HRESULT hr = g.dev->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g.upload[s]));
        if (FAILED(hr)) { fail("screen upload buffer", hr); return false; }
        D3D12_RANGE none{0, 0};
        void*       p = nullptr;
        g.upload[s]->Map(0, &none, &p);
        g.uploadMap[s] = static_cast<uint8_t*>(p);
    }
    return true;
}

Tex* named(std::string const& key, UINT w, UINT h) {
    auto& t = g.tex[key];
    if (!t.r || t.w != w || t.h != h)
        if (!makeTex(t, w, h, kMid, true, D3D12_RESOURCE_STATE_RENDER_TARGET)) return nullptr;
    return &t;
}

void constants(ID3D12GraphicsCommandList* l, UINT w, UINT h, float const rect[4]) {
    float c[8] = {float(w), float(h), 1.0f / float(w), 1.0f / float(h), rect[0], rect[1], rect[2], rect[3]};
    l->SetGraphicsRoot32BitConstants(0, 8, c, 0);
    D3D12_VIEWPORT vp{0, 0, float(w), float(h), 0, 1};
    D3D12_RECT     sc{0, 0, LONG(w), LONG(h)};
    l->RSSetViewports(1, &vp);
    l->RSSetScissorRects(1, &sc);
}

/// One Anime4K stage ("RS", "US", "RM" or "UM") on `in`; returns its MAIN output (or null on failure).
Tex* stage(ID3D12GraphicsCommandList* l, char const* id, Tex* in, UINT slot) {
#if RF_HAVE_SCREEN_SHADERS
    Tex* mainOut = nullptr;
    for (UINT p = 0; p < kA4kPassCount; ++p) {
        A4kPass const& ps = kA4kPasses[p];
        if (std::strcmp(ps.stage, id) != 0) continue;
        if (!g.a4k[p] && !createPso({shaders::kA4kCode[p].code, shaders::kA4kCode[p].size}, kMid, g.a4k[p])) return nullptr;
        // inputs
        UINT const base = (slot * kTables + p) * kBind;
        for (UINT k = 0; k < kBind; ++k) {
            if (k >= ps.bindCount) { nullSrv(srvCpu(base + k)); continue; }
            Tex* t = std::strcmp(ps.bind[k], "MAIN") == 0 ? in : nullptr;
            if (!t) {
                auto it = g.tex.find(std::string(id) + "." + ps.bind[k]);
                if (it == g.tex.end() || !it->second.r) { fail(std::string("Anime4K input ") + ps.bind[k] + " missing", E_FAIL); return nullptr; }
                t = &it->second;
            }
            toState(l, *t, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            srvOf(t->r.Get(), t->fmt, 0, 1, srvCpu(base + k));
        }
        // output: convs keep the stage input's size, MAIN of the x2 stage doubles it
        bool const isMain = std::strcmp(ps.save, "MAIN") == 0;
        UINT const ow = in->w * ps.scale, oh = in->h * ps.scale;
        Tex* o = named(std::string(id) + "." + ps.save, ow, oh);
        if (!o) return nullptr;
        toState(l, *o, D3D12_RESOURCE_STATE_RENDER_TARGET);
        auto rtv = rtvCpu(o->rtv);
        l->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        l->SetPipelineState(g.a4k[p].Get());
        l->SetGraphicsRootDescriptorTable(1, srvGpu(base));
        float const zero[4]{};
        constants(l, ow, oh, zero);
        l->DrawInstanced(3, 1, 0, 0);
        toState(l, *o, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        if (isMain) mainOut = o;
    }
    return mainOut;
#else
    (void)l; (void)id; (void)in; (void)slot;
    return nullptr;
#endif
}

void buildFrame(ID3D12GraphicsCommandList* l, UINT slot) {
    // 1. upload the captured frame
    UINT const w = g.frame.w, h = g.frame.h;
    uint8_t* dst = g.uploadMap[slot];
    if (!dst) return;
    for (UINT y = 0; y < h; ++y)
        std::memcpy(dst + g.fp.Offset + UINT64(y) * g.fp.Footprint.RowPitch, g.frame.bgra.data() + size_t(y) * w * 4, size_t(w) * 4);
    toState(l, g.src, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION to{g.src.r.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
    to.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION from{g.upload[slot].Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
    from.PlacedFootprint = g.fp;
    l->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    toState(l, g.src, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    l->SetGraphicsRootSignature(g.root.Get());
    ID3D12DescriptorHeap* heaps[] = {g.srv.Get()};
    l->SetDescriptorHeaps(1, heaps);
    l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // 2. Anime4K: clean the lines, then double the size when the picture is clearly smaller than the screen
    Tex* pic = &g.src;
    gRan     = BF_SCREEN_PLAIN;
    uint32_t const mode = std::min<uint32_t>(gCfg.upscale, BF_SCREEN_MAX);
    if (mode != BF_SCREEN_PLAIN) {
        bool const small = std::min(float(kOutW) / float(w), float(kOutH) / float(h)) > 1.2f;
        char const* restore = mode == BF_SCREEN_MAX ? "RM" : "RS";
        char const* up      = mode == BF_SCREEN_MAX ? "UM" : "US";
        if (Tex* r = stage(l, restore, pic, slot)) {
            pic  = r;
            gRan = mode;
            if (small)
                if (Tex* u = stage(l, up, pic, slot)) { pic = u; gRan += 4; }
        }
        if (g.broken) return;
    }

    // 3. fit onto the 1920x1080 screen (letterbox), then the mips (a far screen in the sky stays calm)
    UINT const fb = (slot * kTables + kFitTable) * kBind;
    srvOf(pic->r.Get(), pic->fmt, 0, 1, srvCpu(fb));
    for (UINT k = 1; k < kBind; ++k) nullSrv(srvCpu(fb + k));
    float rect[4];
    letterbox(pic->w, pic->h, rect);
    barrier(l, g.out.r.Get(), g.outState[0], D3D12_RESOURCE_STATE_RENDER_TARGET, 0);
    g.outState[0] = D3D12_RESOURCE_STATE_RENDER_TARGET;
    auto rtv0 = rtvCpu(0);
    l->OMSetRenderTargets(1, &rtv0, FALSE, nullptr);
    l->SetPipelineState(g.fit.Get());
    l->SetGraphicsRootDescriptorTable(1, srvGpu(fb));
    constants(l, kOutW, kOutH, rect);
    l->DrawInstanced(3, 1, 0, 0);
    float const full[4] = {0, 0, 1, 1};
    for (UINT m = 1; m < kOutMips; ++m) {
        barrier(l, g.out.r.Get(), g.outState[m - 1], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, m - 1);
        g.outState[m - 1] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier(l, g.out.r.Get(), g.outState[m], D3D12_RESOURCE_STATE_RENDER_TARGET, m);
        g.outState[m] = D3D12_RESOURCE_STATE_RENDER_TARGET;
        UINT const mb = (slot * kTables + kMipTable + m - 1) * kBind;
        srvOf(g.out.r.Get(), kOut, m - 1, 1, srvCpu(mb));
        for (UINT k = 1; k < kBind; ++k) nullSrv(srvCpu(mb + k));
        auto rtv = rtvCpu(m);
        l->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        l->SetGraphicsRootDescriptorTable(1, srvGpu(mb));
        constants(l, std::max(1u, kOutW >> m), std::max(1u, kOutH >> m), full);
        l->DrawInstanced(3, 1, 0, 0);
    }
    barrier(l, g.out.r.Get(), g.outState[kOutMips - 1], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, kOutMips - 1);
    g.outState[kOutMips - 1] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    g.haveOut = true;
}

// ---------------------------------------------------------------- D3D11 (plain fit, no Anime4K)
struct D11 {
    ComPtr<ID3D11Device>             dev;
    ComPtr<ID3D11VertexShader>       vs;
    ComPtr<ID3D11PixelShader>        ps;
    ComPtr<ID3D11Buffer>             cb;
    ComPtr<ID3D11SamplerState>       pt, lin;
    ComPtr<ID3D11BlendState>         opaque;
    ComPtr<ID3D11RasterizerState>    raster;
    ComPtr<ID3D11DepthStencilState>  noDepth;
    ComPtr<ID3D11Texture2D>          src, out;
    ComPtr<ID3D11ShaderResourceView> srcSrv, outSrv;
    ComPtr<ID3D11RenderTargetView>   outRtv;
    UINT                             sw = 0, sh = 0;
    cap::Frame                       frame;
    uint64_t                         seq = 0;
    bool                             ready = false, haveOut = false, broken = false;
} g11;

bool create11(ID3D11Device* dev) {
#if RF_HAVE_SCREEN_SHADERS11
    g11.dev = dev;
    HRESULT hr;
    if (FAILED(hr = dev->CreateVertexShader(shaders::VSFit11, sizeof shaders::VSFit11, nullptr, &g11.vs)) ||
        FAILED(hr = dev->CreatePixelShader(shaders::PSFit11, sizeof shaders::PSFit11, nullptr, &g11.ps))) {
        fail("D3D11 screen shaders", hr);
        g11.broken = true;
        return false;
    }
    D3D11_BUFFER_DESC cbd{32, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
    dev->CreateBuffer(&cbd, nullptr, &g11.cb);
    D3D11_SAMPLER_DESC sd{};
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD   = D3D11_FLOAT32_MAX;
    sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_POINT;
    dev->CreateSamplerState(&sd, &g11.pt);
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    dev->CreateSamplerState(&sd, &g11.lin);
    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    dev->CreateBlendState(&bd, &g11.opaque);
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
    dev->CreateRasterizerState(&rd, &g11.raster);
    D3D11_DEPTH_STENCIL_DESC dd{};
    dev->CreateDepthStencilState(&dd, &g11.noDepth);
    D3D11_TEXTURE2D_DESC td{};
    td.Width = kOutW; td.Height = kOutH; td.MipLevels = kOutMips; td.ArraySize = 1; td.Format = kOut; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    if (FAILED(hr = dev->CreateTexture2D(&td, nullptr, &g11.out))) { fail("D3D11 screen texture", hr); g11.broken = true; return false; }
    dev->CreateShaderResourceView(g11.out.Get(), nullptr, &g11.outSrv);
    D3D11_RENDER_TARGET_VIEW_DESC rv{};
    rv.Format = kOut; rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D; rv.Texture2D.MipSlice = 0;
    dev->CreateRenderTargetView(g11.out.Get(), &rv, &g11.outRtv);
    if (!g11.cb || !g11.pt || !g11.lin || !g11.opaque || !g11.raster || !g11.noDepth || !g11.outSrv || !g11.outRtv) {
        fail("D3D11 screen state", E_FAIL);
        g11.broken = true;
        return false;
    }
    g11.ready = true;
    return true;
#else
    (void)dev;
    gWhy       = "off: D3D11 screen shaders not compiled into this build";
    g11.broken = true;
    return false;
#endif
}
} // namespace

// ---------------------------------------------------------------- public
void poll(bool d3d11) {
    gD3D11 = d3d11;
    BfScreenConfigV1 c{};
    c.size = sizeof c;
    if (auto get = bfGet()) get(&c);
    gCfg = c;
    cap::Want w;
    w.on      = c.enabled != 0;
    w.hwnd    = c.hwnd;
    w.monitor = c.monitor;
    w.crop    = c.cropClient != 0;
    w.maxFps  = c.maxFps ? c.maxFps : 60;
    cap::want(w);
    gCap = cap::status();

    static auto last = std::chrono::steady_clock::now();
    auto const  now  = std::chrono::steady_clock::now();
    if (now - last < std::chrono::milliseconds(250)) return;
    last = now;
    auto put = bfPut();
    if (!put) return;
    BfScreenStatusV1 s{};
    s.size     = sizeof s;
    s.version  = 1;
    s.state    = uint32_t(gCap.state);
    s.srcW     = gCap.w;
    s.srcH     = gCap.h;
    s.fpsX10   = uint32_t(std::lround(gCap.fps * 10.0f));
    s.upscaled = gRan;
    s.gpuMs    = gBuildMs;
    std::string msg = gCap.message;
    bool const broken = d3d11 ? g11.broken : g.broken;
    if (broken) msg = gWhy;
    else if (d3d11 && c.upscale != BF_SCREEN_PLAIN && gCap.state == cap::State::Live) msg += " (Anime4K needs D3D12)";
    std::snprintf(s.message, sizeof s.message, "%s", msg.c_str());
    put(&s);
}

ID3D12Resource* record12(ID3D12Device* device, ID3D12GraphicsCommandList* list, unsigned slotIn, uint64_t present) {
    while (!g.grave.empty() && present - g.grave.front().second > kSlots + 2) g.grave.erase(g.grave.begin());
    if (g.broken) return nullptr;
    if (!gCfg.enabled) return g.haveOut ? g.out.r.Get() : nullptr;
    if (!g.root && !createStatic(device)) return nullptr;
#if RF_HAVE_SCREEN_SHADERS
    if (!g.fit && !createPso({shaders::PSFit, sizeof shaders::PSFit}, kOut, g.fit)) return nullptr;
#else
    fail("screen shaders not compiled into this build (run tools/gen_screen.py)", E_NOTIMPL);
    return nullptr;
#endif
    UINT const slot = slotIn % kSlots;
    if (cap::latest(g.seq, g.frame)) {
        g.seq = g.frame.seq;
        if (g.frame.w && g.frame.h && g.frame.bgra.size() >= size_t(g.frame.w) * g.frame.h * 4) {
            auto const t0 = std::chrono::steady_clock::now();
            if ((g.src.w != g.frame.w || g.src.h != g.frame.h || !g.src.r) && !resize(g.frame.w, g.frame.h, present)) return nullptr;
            buildFrame(list, slot);
            gBuildMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count(); // CPU record time
        }
    }
    if (!g.haveOut) { // nothing captured yet: a black screen (the frame / glow still show where it is)
        // the texture was created as a render target: clear it once so it isn't garbage
        float const black[4] = {0, 0, 0, 1};
        for (UINT m = 0; m < kOutMips; ++m) {
            barrier(list, g.out.r.Get(), g.outState[m], D3D12_RESOURCE_STATE_RENDER_TARGET, m);
            g.outState[m] = D3D12_RESOURCE_STATE_RENDER_TARGET;
            auto r = rtvCpu(m);
            list->ClearRenderTargetView(r, black, 0, nullptr);
            barrier(list, g.out.r.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, m);
            g.outState[m] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
        g.haveOut = true;
    }
    return g.out.r.Get();
}

ID3D11ShaderResourceView* record11(ID3D11Device* device, ID3D11DeviceContext* ctx) {
    if (g11.broken) return nullptr;
    if (!gCfg.enabled) return g11.haveOut ? g11.outSrv.Get() : nullptr;
    if (!g11.ready && !create11(device)) return nullptr;
    if (!g11.haveOut) {
        float const black[4] = {0, 0, 0, 1};
        ctx->ClearRenderTargetView(g11.outRtv.Get(), black);
        ctx->GenerateMips(g11.outSrv.Get());
        g11.haveOut = true;
    }
    if (!cap::latest(g11.seq, g11.frame)) return g11.outSrv.Get();
    g11.seq = g11.frame.seq;
    UINT const w = g11.frame.w, h = g11.frame.h;
    if (!w || !h || g11.frame.bgra.size() < size_t(w) * h * 4) return g11.outSrv.Get();
    if (!g11.src || g11.sw != w || g11.sh != h) {
        g11.src.Reset();
        g11.srcSrv.Reset();
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = kSrc; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(g11.dev->CreateTexture2D(&td, nullptr, &g11.src))) return g11.outSrv.Get();
        g11.dev->CreateShaderResourceView(g11.src.Get(), nullptr, &g11.srcSrv);
        g11.sw = w; g11.sh = h;
    }
    ctx->UpdateSubresource(g11.src.Get(), 0, nullptr, g11.frame.bgra.data(), w * 4, 0);
    float c[8] = {float(kOutW), float(kOutH), 1.0f / kOutW, 1.0f / kOutH, 0, 0, 0, 0};
    letterbox(w, h, c + 4);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (SUCCEEDED(ctx->Map(g11.cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        std::memcpy(m.pData, c, sizeof c);
        ctx->Unmap(g11.cb.Get(), 0);
    }
    ID3D11ShaderResourceView* none[kBind]{};
    ctx->PSSetShaderResources(0, kBind, none);
    ID3D11RenderTargetView* rt[] = {g11.outRtv.Get()};
    ctx->OMSetRenderTargets(1, rt, nullptr);
    D3D11_VIEWPORT vp{0, 0, float(kOutW), float(kOutH), 0, 1};
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetState(g11.raster.Get());
    ctx->OMSetBlendState(g11.opaque.Get(), nullptr, 0xFFFFFFFFu);
    ctx->OMSetDepthStencilState(g11.noDepth.Get(), 0);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(g11.vs.Get(), nullptr, 0);
    ctx->PSSetShader(g11.ps.Get(), nullptr, 0);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    ID3D11Buffer* cbs[] = {g11.cb.Get()};
    ctx->PSSetConstantBuffers(0, 1, cbs);
    ID3D11SamplerState* smp[] = {g11.pt.Get(), g11.lin.Get()};
    ctx->PSSetSamplers(0, 2, smp);
    ID3D11ShaderResourceView* srv[] = {g11.srcSrv.Get()};
    ctx->PSSetShaderResources(0, 1, srv);
    ctx->Draw(3, 0);
    ctx->PSSetShaderResources(0, kBind, none);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ctx->GenerateMips(g11.outSrv.Get());
    gRan = BF_SCREEN_PLAIN;
    return g11.outSrv.Get();
}

void drawUi() {
    if (!ImGui::CollapsingHeader("LyricsForge video screen")) return;
    ImGui::TextDisabled("Settings live in the LyricsForge panel (More > Video screen). Anime4K by bloc97 (MIT).");
    static char const* const st[] = {"idle", "starting", "live", "error"};
    ImGui::Text("Capture: %s - %s", st[std::min<uint32_t>(uint32_t(gCap.state), 3)], gCap.message.c_str());
    ImGui::Text("Picture %ux%u @ %.1f fps, renderer %s", gCap.w, gCap.h, double(gCap.fps), gD3D11 ? "D3D11" : "D3D12");
    static char const* const up[] = {"plain", "Anime4K Sharp", "Anime4K Max"};
    ImGui::Text("Upscale: wanted %s, ran %s%s", up[std::min<uint32_t>(gCfg.upscale, 2)], up[gRan & 3], (gRan & 4) ? " + x2" : "");
    ImGui::Text("Record time %.2f ms (CPU)", double(gBuildMs));
    if ((gD3D11 ? g11.broken : g.broken)) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", gWhy.c_str());
}

} // namespace rf::screen
