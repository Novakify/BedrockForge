// Renderer Foundation - Spatial GUI M1 panel (see SpatialPanel.h, shaders/panel.hlsl). Original code for Renderer Foundation.
#include "SpatialPanel.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include <imgui.h>

#if __has_include(<GameInput.h>)
#include <GameInput.h>
#define RF_HAVE_GAMEINPUT 1
#else
#define RF_HAVE_GAMEINPUT 0
#endif

#include "LLShim.h"
#include "Options.h"
#include "PanelShaders.h"
#include "UiCapture.h"

namespace rf::panel {
namespace {
using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------- settings (F6)
struct Settings {
    float distance = 1.0f;  // view units; only the ratio to size matters for the look
    float size     = 0.78f; // panel half-height as a fraction of the view's half-height at that distance
    float tilt     = -6.0f; // degrees, negative = top leans away
    float yaw      = 0.0f;  // degrees
    float offsetX  = 0.0f;  // in panel half-heights
    float offsetY  = -0.05f;
    float zoom     = 0.08f; // world zoom at full open ("FOV lerp")
    float dim      = 0.30f; // world dim at full open
    float seconds  = 0.22f; // open/close animation
    bool  alphaBoost = true;
    bool  eatRaw     = true; // experiment: hide raw mouse input from the game while the panel is up
    bool  remap      = true;
    bool  pumpRemap  = true; // v0.10: remap mouse MSGs as the game pulls them from the queue
} gS;

std::atomic_bool   gEnabled{false};
std::atomic_bool   gReady{false};      // UI target exists
std::atomic<size_t> gRtv{0};
std::atomic_bool   gRedirected{false}; // this frame
std::atomic<float> gAnim{0.0f};        // eased 0..1
float              gT = 0.0f;          // linear 0..1
LARGE_INTEGER      gLast{};
long               gFramesShown = 0, gFramesNoUi = 0;
std::string        gStatus = "off";

// geometry of the last composite (for ray-picking on the window thread)
struct Geom {
    bool  valid = false;
    float cx, cy, cz, hw, hh, tanHalf, aspect;
    float ex[3], ey[3], n[3];
} gGeom;
std::mutex gGeomMutex;

// ---------------------------------------------------------------- D3D12 objects (overlay thread)
ComPtr<ID3D12Device>         gDevice;
ComPtr<ID3D12RootSignature>  gRoot;
ComPtr<ID3D12PipelineState>  gPsoWorld, gPsoPanel;
DXGI_FORMAT                  gPsoFmt = DXGI_FORMAT_UNKNOWN;
ComPtr<ID3D12DescriptorHeap> gSrvHeap, gRtvHeap;
UINT                         gSrvInc = 0;
ComPtr<ID3D12Resource>       gScene, gUi;
std::vector<ComPtr<ID3D12Resource>> gGraveyard; // old targets may still be referenced by in-flight game lists
UINT                         gW = 0, gH = 0;
DXGI_FORMAT                  gFmt = DXGI_FORMAT_UNKNOWN;
bool                         gBroken = false;

void fail(std::string const& what, HRESULT hr) {
    char b[16];
    snprintf(b, sizeof b, "%08X", uint32_t(hr));
    gStatus = "off: " + what + " failed (hr=0x" + b + ")";
    gBroken = true;
    ll_shim::error("[RF] spatial panel " + gStatus);
}

D3D12_CPU_DESCRIPTOR_HANDLE srvCpu(UINT i) { auto h = gSrvHeap->GetCPUDescriptorHandleForHeapStart(); h.ptr += SIZE_T(i) * gSrvInc; return h; }
D3D12_GPU_DESCRIPTOR_HANDLE srvGpu(UINT i) { auto h = gSrvHeap->GetGPUDescriptorHandleForHeapStart(); h.ptr += UINT64(i) * gSrvInc; return h; }

bool createStatic(ID3D12Device* device) {
    gDevice = device;
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0};
    D3D12_ROOT_PARAMETER   params[2]{};
    params[0].ParameterType   = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants       = {0, 0, 32};
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType   = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable = {1, &range};
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC smp{};
    smp.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    smp.AddressU = smp.AddressV = smp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    smp.MaxLOD           = D3D12_FLOAT32_MAX;
    smp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rs{2, params, 1, &smp, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob, err;
    HRESULT          hr = D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    if (FAILED(hr)) { fail("D3D12SerializeRootSignature", hr); return false; }
    if (FAILED(hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&gRoot)))) {
        fail("CreateRootSignature", hr);
        return false;
    }
    D3D12_DESCRIPTOR_HEAP_DESC srv{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    D3D12_DESCRIPTOR_HEAP_DESC rtv{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    if (FAILED(hr = device->CreateDescriptorHeap(&srv, IID_PPV_ARGS(&gSrvHeap)))) { fail("CreateDescriptorHeap(SRV)", hr); return false; }
    if (FAILED(hr = device->CreateDescriptorHeap(&rtv, IID_PPV_ARGS(&gRtvHeap)))) { fail("CreateDescriptorHeap(RTV)", hr); return false; }
    gSrvInc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
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
        rt.SrcBlend = D3D12_BLEND_ONE;       rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;      rt.BlendOp = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D12_BLEND_ONE;  rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA; rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
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

bool makeTex(ComPtr<ID3D12Resource>& t, UINT w, UINT h, DXGI_FORMAT fmt, bool rt) {
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
    cv.Format = fmt; // (0,0,0,0) = what the hooks clear to
    HRESULT hr = gDevice->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d,
                                                  rt ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                  rt ? &cv : nullptr, IID_PPV_ARGS(&t));
    if (FAILED(hr)) { fail("CreateCommittedResource", hr); return false; }
    return true;
}

bool ensureSized(D3D12_RESOURCE_DESC const& bb) {
    if (gUi && gW == UINT(bb.Width) && gH == bb.Height && gFmt == bb.Format) return true;
    gReady = false;
    if (gUi) gGraveyard.push_back(gUi);
    if (gScene) gGraveyard.push_back(gScene);
    gUi.Reset();
    gScene.Reset();
    gW = UINT(bb.Width);
    gH = bb.Height;
    gFmt = bb.Format;
    if (!makeTex(gScene, gW, gH, gFmt, false) || !makeTex(gUi, gW, gH, gFmt, true)) return false;
    D3D12_SHADER_RESOURCE_VIEW_DESC s{};
    s.Format                  = gFmt;
    s.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    s.Texture2D.MipLevels     = 1;
    gDevice->CreateShaderResourceView(gScene.Get(), &s, srvCpu(0));
    gDevice->CreateShaderResourceView(gUi.Get(), &s, srvCpu(1));
    auto rtv = gRtvHeap->GetCPUDescriptorHandleForHeapStart();
    gDevice->CreateRenderTargetView(gUi.Get(), nullptr, rtv);
    gRtv = rtv.ptr;
    if (gPsoFmt != gFmt) {
        gPsoWorld.Reset();
        gPsoPanel.Reset();
        if (!createPso({shaders::VSFull, sizeof shaders::VSFull}, {shaders::PSWorld, sizeof shaders::PSWorld}, gFmt, false, gPsoWorld)) return false;
        if (!createPso({shaders::VSPanel, sizeof shaders::VSPanel}, {shaders::PSPanel, sizeof shaders::PSPanel}, gFmt, true, gPsoPanel)) return false;
        gPsoFmt = gFmt;
    }
    gReady = true;
    return true;
}

void barrier(ID3D12GraphicsCommandList* l, ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
    D3D12_RESOURCE_BARRIER x{};
    x.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    x.Transition.pResource   = r;
    x.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    x.Transition.StateBefore = a;
    x.Transition.StateAfter  = b;
    l->ResourceBarrier(1, &x);
}

float ease(float t) { return t * t * (3.0f - 2.0f * t); }

// panel frame in view space (x right, y up, z forward) for the current animation value
Geom computeGeom(float e, float aspect) {
    Geom g{};
    g.valid   = true;
    g.tanHalf = std::tan(35.0f * 0.01745329f);
    g.aspect  = aspect;
    float s   = gS.size * (0.85f + 0.15f * e);
    g.hh      = s * gS.distance * g.tanHalf;
    g.hw      = g.hh * aspect;
    g.cx      = gS.offsetX * g.hh;
    g.cy      = gS.offsetY * g.hh;
    g.cz      = gS.distance;
    float p = gS.tilt * 0.01745329f, y = gS.yaw * 0.01745329f;
    float cp = std::cos(p), sp = std::sin(p), cy = std::cos(y), sy = std::sin(y);
    // R = RotY(yaw) * RotX(tilt): columns are the panel's x, y, normal axes
    g.ex[0] = cy;       g.ex[1] = 0;   g.ex[2] = -sy;
    g.ey[0] = sy * sp;  g.ey[1] = cp;  g.ey[2] = cy * sp;
    g.n[0]  = sy * cp;  g.n[1]  = -sp; g.n[2]  = cy * cp;
    return g;
}

void toClip(Geom const& g, float lx, float ly, float out[4]) {
    float x = g.cx + g.ex[0] * lx + g.ey[0] * ly;
    float y = g.cy + g.ex[1] * lx + g.ey[1] * ly;
    float z = g.cz + g.ex[2] * lx + g.ey[2] * ly;
    z       = std::max(z, 0.05f);
    out[0]  = x / (g.tanHalf * g.aspect);
    out[1]  = y / g.tanHalf;
    out[2]  = 0.5f * z;
    out[3]  = z;
}
} // namespace

// ---------------------------------------------------------------- hook side
bool   enabled() { return gEnabled.load(std::memory_order_relaxed) || gAnim.load(std::memory_order_relaxed) > 0.0f; }
bool   wantRedirect() { return enabled() && gReady.load(std::memory_order_relaxed); }
size_t targetRtv() { return gRtv.load(std::memory_order_relaxed); }
void   noteRedirected() { gRedirected.store(true, std::memory_order_relaxed); }

void installCursorHook();
void installSourceHooks();
void tryGameMouse();
void writeDiag();
void toggle() {
    bool on = !gEnabled.load();
    if (on) { installCursorHook(); installSourceHooks(); tryGameMouse(); }
    else writeDiag();
    gEnabled = on;
    if (on) uicapture::requestLearn(); // make sure the back-buffer RTV handles are known
    ll_shim::info(std::string("[RF] spatial panel ") + (on ? "on" : "off"));
}

// ---------------------------------------------------------------- Present
bool record(ID3D12Device* device, ID3D12GraphicsCommandList* list, ID3D12Resource* bb, D3D12_CPU_DESCRIPTOR_HANDLE const& bbRtv) {
    LARGE_INTEGER now, f;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&f);
    float dt = gLast.QuadPart ? float(double(now.QuadPart - gLast.QuadPart) / double(f.QuadPart)) : 0.0f;
    gLast    = now;
    dt       = std::clamp(dt, 0.0f, 0.1f);
    float target = gEnabled ? 1.0f : 0.0f;
    float step   = gS.seconds > 0.01f ? dt / gS.seconds : 1.0f;
    gT           = gT < target ? std::min(target, gT + step) : std::max(target, gT - step);
    float e      = ease(gT);
    gAnim        = e;
    bool redirected = gRedirected.exchange(false);
    if (gBroken || !RfOptions::uiCaptureSpike) { gAnim = 0; return false; }
    if (!gEnabled && gT <= 0.0f) {
        gStatus = "off";
        std::lock_guard l(gGeomMutex);
        gGeom.valid = false;
        return false;
    }
    if (!gRoot && !createStatic(device)) return false;
    D3D12_RESOURCE_DESC bd = bb->GetDesc();
    if (!ensureSized(bd)) return false;
    if (redirected) ++gFramesShown;
    else ++gFramesNoUi;

    float aspect = float(bd.Width) / float(bd.Height);
    Geom  g      = computeGeom(e, aspect);
    {
        std::lock_guard l(gGeomMutex);
        gGeom = g;
    }

    barrier(list, bb, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    barrier(list, gScene.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyResource(gScene.Get(), bb);
    barrier(list, gScene.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    barrier(list, bb, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    barrier(list, gUi.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    float c[32]{};
    toClip(g, -g.hw, g.hh, c + 0);   // TL
    toClip(g, g.hw, g.hh, c + 4);    // TR
    toClip(g, -g.hw, -g.hh, c + 8);  // BL
    toClip(g, g.hw, -g.hh, c + 12);  // BR
    c[16] = 1.0f + gS.zoom * e;
    c[17] = e;
    c[18] = gS.dim * e;
    c[19] = gS.alphaBoost ? 1.0f : 0.0f;

    ID3D12DescriptorHeap* heaps[] = {gSrvHeap.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootSignature(gRoot.Get());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D12_VIEWPORT vp{0, 0, float(bd.Width), float(bd.Height), 0, 1};
    D3D12_RECT     sc{0, 0, LONG(bd.Width), LONG(bd.Height)};
    list->OMSetRenderTargets(1, &bbRtv, FALSE, nullptr);
    list->RSSetViewports(1, &vp);
    list->RSSetScissorRects(1, &sc);
    list->SetGraphicsRoot32BitConstants(0, 32, c, 0);
    list->SetGraphicsRootDescriptorTable(1, srvGpu(0));
    list->SetPipelineState(gPsoWorld.Get());
    list->DrawInstanced(3, 1, 0, 0);
    if (redirected) {
        list->SetPipelineState(gPsoPanel.Get());
        list->DrawInstanced(6, 1, 0, 0);
    }
    barrier(list, gUi.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    char b[160];
    snprintf(b, sizeof b, "%s  anim %.2f  frames with UI %ld, without %ld  (%ux%u)", gEnabled ? "on" : "closing", e,
             gFramesShown, gFramesNoUi, gW, gH);
    gStatus = b;
    return true;
}

// ---------------------------------------------------------------- input
std::atomic<HWND> gHwnd{nullptr};
std::atomic<long> gGcpCalls{0}, gGcpRemapped{0}, gGcpInClient{0}, gWmRemapped{0}, gWmSeen{0}, gRawSeen{0}, gRawEaten{0};
std::string       gHookNote = "not installed";

// client pixel -> client pixel the game should see (ray-pick through the panel)
bool remapClient(HWND hwnd, int& ix, int& iy) {
    if (!gS.remap || !gEnabled || gAnim.load() < 0.5f || RfOptions::showImGui) return false;
    Geom g;
    {
        std::lock_guard l(gGeomMutex);
        g = gGeom;
    }
    if (!g.valid) return false;
    RECT rc;
    if (!GetClientRect(hwnd, &rc)) return false;
    float W = float(rc.right - rc.left), H = float(rc.bottom - rc.top);
    if (W < 1 || H < 1) return false;
    float px = float(ix), py = float(iy);
    // ray through the pixel (view space, z forward)
    float d[3] = {(2.0f * (px + 0.5f) / W - 1.0f) * g.tanHalf * g.aspect, (1.0f - 2.0f * (py + 0.5f) / H) * g.tanHalf, 1.0f};
    float c[3] = {g.cx, g.cy, g.cz};
    float dn   = d[0] * g.n[0] + d[1] * g.n[1] + d[2] * g.n[2];
    if (std::fabs(dn) < 1e-5f) return false;
    float t = (c[0] * g.n[0] + c[1] * g.n[1] + c[2] * g.n[2]) / dn;
    float h[3] = {d[0] * t - c[0], d[1] * t - c[1], d[2] * t - c[2]};
    float lx = h[0] * g.ex[0] + h[1] * g.ex[1] + h[2] * g.ex[2];
    float ly = h[0] * g.ey[0] + h[1] * g.ey[1] + h[2] * g.ey[2];
    float u  = std::clamp((lx / g.hw + 1.0f) * 0.5f, 0.0f, 1.0f);
    float v  = std::clamp((1.0f - ly / g.hh) * 0.5f, 0.0f, 1.0f);
    ix = std::min(int(u * W), int(W) - 1);
    iy = std::min(int(v * H), int(H) - 1);
    return true;
}

struct ModRange {
    uintptr_t lo = 0, hi = 0;
    bool      has(void* p) const { auto a = reinterpret_cast<uintptr_t>(p); return a >= lo && a < hi; }
};
ModRange gGameMod;
void initGameMod() {
    if (gGameMod.lo) return;
    auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!base) return;
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt  = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    gGameMod = {base, base + nt->OptionalHeader.SizeOfImage};
}
bool fromGame(void* ra) { return gGameMod.has(ra); }

// screen point -> screen point the game should see
bool remapScreen(POINT& p) {
    HWND h = gHwnd.load(std::memory_order_relaxed);
    if (!h || !gEnabled.load(std::memory_order_relaxed)) return false;
    POINT c = p;
    if (!ScreenToClient(h, &c)) return false;
    RECT rc;
    if (!GetClientRect(h, &rc) || c.x < 0 || c.y < 0 || c.x >= rc.right || c.y >= rc.bottom) return false;
    int x = c.x, y = c.y;
    if (!remapClient(h, x, y)) return false;
    c = {x, y};
    if (!ClientToScreen(h, &c)) return false;
    p = c;
    return true;
}

struct SrcStat {
    std::atomic<long> calls{0}, game{0}, remapped{0};
};
SrcStat gStCursorInfo, gStPhys, gStMsgPos, gStPump, gStGcpGame;
std::atomic<long> gPumpSkippedWnd{0};

// In-world screens read the cursor with GetCursorPos (the title screen uses the WM_* coordinates): remap both.
using GetCursorPos_t = BOOL(WINAPI*)(LPPOINT);
struct GcpPair { long inX, inY, outX, outY; };
GcpPair gGcpRing[4]{};
std::atomic<unsigned> gGcpRingIdx{0};
void* gGcpTarget = nullptr;
void* gGcpOrig   = nullptr;
BOOL WINAPI getCursorPosHook(LPPOINT p) {
    BOOL r = reinterpret_cast<GetCursorPos_t>(gGcpOrig)(p);
    HWND h = gHwnd.load(std::memory_order_relaxed);
    if (r && p && h && gEnabled.load(std::memory_order_relaxed)) {
        ++gGcpCalls;
        if (fromGame(__builtin_return_address(0))) ++gStGcpGame.game;
        POINT in = *p;
        POINT c = *p;
        if (ScreenToClient(h, &c)) {
            int  x = c.x, y = c.y;
            RECT rc;
            if (GetClientRect(h, &rc) && x >= 0 && y >= 0 && x < rc.right && y < rc.bottom) {
                ++gGcpInClient;
                if (remapClient(h, x, y)) {
                    c = {x, y};
                    if (ClientToScreen(h, &c)) { *p = c; ++gGcpRemapped; }
                }
            }
        }
        unsigned slot = gGcpRingIdx.fetch_add(1) % 4;
        gGcpRing[slot] = {in.x, in.y, p->x, p->y};
    }
    return r;
}
// v0.4 diagnostics: which mouse source the in-world menus actually read.
// GetRawInputData (polled raw mouse), ClipCursor (cursor capture) and the last few GetCursorPos in/out pairs.
using GetRawInputData_t = UINT(WINAPI*)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
using ClipCursor_t      = BOOL(WINAPI*)(const RECT*);
void* gRawTarget = nullptr;
void* gRawOrig   = nullptr;
void* gClipTarget = nullptr;
void* gClipOrig   = nullptr;
std::atomic<long> gRawReads{0}, gRawMouseReads{0}, gClipCalls{0};
std::atomic<long> gRawDx{0}, gRawDy{0};
std::atomic<unsigned> gRawBtn{0};
RECT gLastClip{};
UINT WINAPI rawInputHook(HRAWINPUT h, UINT cmd, LPVOID data, PUINT size, UINT hdrSize) {
    UINT r = reinterpret_cast<GetRawInputData_t>(gRawOrig)(h, cmd, data, size, hdrSize);
    if (data && r != UINT(-1) && gEnabled.load(std::memory_order_relaxed) && cmd == RID_INPUT) {
        ++gRawReads;
        auto* ri = reinterpret_cast<RAWINPUT*>(data);
        if (ri->header.dwType == RIM_TYPEMOUSE) {
            ++gRawMouseReads;
            gRawDx = ri->data.mouse.lLastX;
            gRawDy = ri->data.mouse.lLastY;
            gRawBtn = ri->data.mouse.usButtonFlags;
        }
    }
    return r;
}

BOOL WINAPI clipCursorHook(const RECT* r) {
    if (gEnabled.load(std::memory_order_relaxed)) {
        ++gClipCalls;
        if (r) gLastClip = *r;
    }
    return reinterpret_cast<ClipCursor_t>(gClipOrig)(r);
}

void installCursorHook() {
    if (gGcpTarget) return;
    HMODULE u = GetModuleHandleW(L"user32.dll");
    void*   t = u ? reinterpret_cast<void*>(GetProcAddress(u, "GetCursorPos")) : nullptr;
    if (!t) return;
    int rc = ll_shim::hook(t, reinterpret_cast<void*>(&getCursorPosHook), &gGcpOrig);
    if (rc == 0) {
        gGcpTarget = t;
        gHookNote  = "GetCursorPos hooked";
        ll_shim::info("[RF] spatial panel: GetCursorPos hooked");
    } else {
        gHookNote = "GetCursorPos hook failed rc " + std::to_string(rc);
        ll_shim::warn("[RF] spatial panel: " + gHookNote);
    }
    if (!gRawTarget && u) {
        void* rt = reinterpret_cast<void*>(GetProcAddress(u, "GetRawInputData"));
        if (rt && ll_shim::hook(rt, reinterpret_cast<void*>(&rawInputHook), &gRawOrig) == 0) gRawTarget = rt;
    }
    if (!gClipTarget && u) {
        void* ct = reinterpret_cast<void*>(GetProcAddress(u, "ClipCursor"));
        if (ct && ll_shim::hook(ct, reinterpret_cast<void*>(&clipCursorHook), &gClipOrig) == 0) gClipTarget = ct;
    }
}

// ---------------------------------------------------------------- cursor sources (v0.10)
// v0.9 showed the in-world (JSON UI) screens do not read the cursor through our GetCursorPos hook (its ~1 call per
// frame is ImGui) and only 3 WM_* mouse messages reached the window procedure. Candidates hooked here, each counted
// separately for calls made from the game's own module, and each remapped through the panel:
//   * the message pump: PeekMessageW/GetMessageW handing a mouse MSG (pt + lParam) to the game before dispatch
//   * GetCursorInfo, GetPhysicalCursorPos, GetMessagePos
//   * GameInput through the real v0 header (the game binary carries the v0 IGameInput IID); vtable slots are
//     computed from the header at runtime instead of hard-coded (v0.6-v0.9 used wrong slots)

using GetCursorInfo_t        = BOOL(WINAPI*)(PCURSORINFO);
using GetPhysicalCursorPos_t = BOOL(WINAPI*)(LPPOINT);
using GetMessagePos_t        = DWORD(WINAPI*)();
using PeekMessageW_t         = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);
using GetMessageW_t          = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT);
void *gCiOrig = nullptr, *gPhysOrig = nullptr, *gMpOrig = nullptr, *gPeekOrig = nullptr, *gGetMsgOrig = nullptr;
bool  gSrcHooked = false;

BOOL WINAPI getCursorInfoHook(PCURSORINFO pci) {
    BOOL r = reinterpret_cast<GetCursorInfo_t>(gCiOrig)(pci);
    if (r && pci && gEnabled.load(std::memory_order_relaxed)) {
        ++gStCursorInfo.calls;
        if (fromGame(__builtin_return_address(0))) ++gStCursorInfo.game;
        if (remapScreen(pci->ptScreenPos)) ++gStCursorInfo.remapped;
    }
    return r;
}
BOOL WINAPI getPhysicalCursorPosHook(LPPOINT p) {
    BOOL r = reinterpret_cast<GetPhysicalCursorPos_t>(gPhysOrig)(p);
    if (r && p && gEnabled.load(std::memory_order_relaxed)) {
        ++gStPhys.calls;
        if (fromGame(__builtin_return_address(0))) ++gStPhys.game;
        if (remapScreen(*p)) ++gStPhys.remapped;
    }
    return r;
}
DWORD WINAPI getMessagePosHook() {
    DWORD r = reinterpret_cast<GetMessagePos_t>(gMpOrig)();
    if (gEnabled.load(std::memory_order_relaxed)) {
        ++gStMsgPos.calls;
        if (fromGame(__builtin_return_address(0))) ++gStMsgPos.game;
        POINT p{short(LOWORD(r)), short(HIWORD(r))};
        if (remapScreen(p)) { r = MAKELONG(short(p.x), short(p.y)); ++gStMsgPos.remapped; }
    }
    return r;
}

// Messages the pump already remapped: the window procedure must not remap them a second time.
struct PumpOut { UINT msg; LPARAM lp; };
PumpOut              gPumpRing[32]{};
std::atomic<unsigned> gPumpIdx{0};
std::mutex           gPumpMutex;
bool isClientMouseMsg(UINT m) {
    switch (m) {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK: return true;
    default: return false;
    }
}
void pumpRemap(MSG* m, void* ra) {
    if (!m || !gEnabled.load(std::memory_order_relaxed)) return;
    bool client = isClientMouseMsg(m->message);
    if (!client && m->message != WM_MOUSEWHEEL && m->message != WM_MOUSEHWHEEL) return;
    HWND h = gHwnd.load(std::memory_order_relaxed);
    if (!h || m->hwnd != h) return;
    ++gStPump.calls;
    if (fromGame(ra)) ++gStPump.game;
    if (!gS.pumpRemap) return;
    POINT pt = m->pt;
    if (!remapScreen(pt)) return;
    m->pt = pt;
    if (client) {
        POINT c = pt;
        if (!ScreenToClient(h, &c)) return;
        m->lParam = MAKELPARAM(short(c.x), short(c.y));
    } else {
        m->lParam = MAKELPARAM(short(pt.x), short(pt.y)); // wheel messages carry screen coordinates
    }
    ++gStPump.remapped;
    std::lock_guard l(gPumpMutex);
    gPumpRing[gPumpIdx.fetch_add(1) % 32] = {m->message, m->lParam};
}
bool pumpAlreadyRemapped(UINT msg, LPARAM lp) {
    std::lock_guard l(gPumpMutex);
    for (auto& e : gPumpRing) {
        if (e.msg == msg && e.lp == lp && msg != 0) { e.msg = 0; return true; }
    }
    return false;
}
BOOL WINAPI peekMessageWHook(LPMSG m, HWND h, UINT lo, UINT hi, UINT rm) {
    BOOL r = reinterpret_cast<PeekMessageW_t>(gPeekOrig)(m, h, lo, hi, rm);
    if (r) pumpRemap(m, __builtin_return_address(0));
    return r;
}
BOOL WINAPI getMessageWHook(LPMSG m, HWND h, UINT lo, UINT hi) {
    BOOL r = reinterpret_cast<GetMessageW_t>(gGetMsgOrig)(m, h, lo, hi);
    if (r > 0) pumpRemap(m, __builtin_return_address(0));
    return r;
}

void hookExport(HMODULE m, const char* name, void* detour, void** orig) {
    void* t = m ? reinterpret_cast<void*>(GetProcAddress(m, name)) : nullptr;
    if (t && ll_shim::hook(t, detour, orig) != 0) *orig = nullptr;
}
void installSourceHooks() {
    if (gSrcHooked) return;
    gSrcHooked = true;
    initGameMod();
    HMODULE u = GetModuleHandleW(L"user32.dll");
    hookExport(u, "GetCursorInfo", reinterpret_cast<void*>(&getCursorInfoHook), &gCiOrig);
    hookExport(u, "GetPhysicalCursorPos", reinterpret_cast<void*>(&getPhysicalCursorPosHook), &gPhysOrig);
    hookExport(u, "GetMessagePos", reinterpret_cast<void*>(&getMessagePosHook), &gMpOrig);
    hookExport(u, "PeekMessageW", reinterpret_cast<void*>(&peekMessageWHook), &gPeekOrig);
    hookExport(u, "GetMessageW", reinterpret_cast<void*>(&getMessageWHook), &gGetMsgOrig);
}

// ---------------------------------------------------------------- GameInput (v0 header)
std::string gGiNote = "not installed";
void*       gGicOrig   = nullptr;
std::atomic<long> gGicCalls{0};
void*       gGameInput = nullptr; // the game's IGameInput

#if RF_HAVE_GAMEINPUT
// vtable slot of a virtual method, read from the header: call the method on a fake object whose vtable records
// which slot was used.
int gVtIdx = -1;
template <int N> int64_t vtSlot() { gVtIdx = N; return 0; }
template <int... N> void fillSlots(void** t, std::integer_sequence<int, N...>) { ((t[N] = reinterpret_cast<void*>(&vtSlot<N>)), ...); }
void* const* slotTable() {
    static void* t[96];
    static bool  init = [] { fillSlots(t, std::make_integer_sequence<int, 96>{}); return true; }();
    (void)init;
    return t;
}
template <class M> struct MemFn;
template <class C, class R, bool NE, class... A> struct MemFn<R (C::*)(A...) noexcept(NE)> {
    static int index(R (C::*m)(A...) noexcept(NE)) {
        struct Fake { void* const* vt; } f{slotTable()};
        gVtIdx = -1;
        (reinterpret_cast<C*>(&f)->*m)(A{}...);
        return gVtIdx;
    }
};
template <class C, class R, bool NE, class... A> struct MemFn<R (C::*)(A...) const noexcept(NE)> {
    static int index(R (C::*m)(A...) const noexcept(NE)) {
        struct Fake { void* const* vt; } f{slotTable()};
        gVtIdx = -1;
        (reinterpret_cast<const C*>(&f)->*m)(A{}...);
        return gVtIdx;
    }
};
template <class M> int vindex(M m) { return MemFn<M>::index(m); }

int  gIdxCur = -1, gIdxNext = -1, gIdxPrev = -1, gIdxReg = -1, gIdxMouse = -1;
bool gMouseHooked = false;
std::string gMouseNote = "no reading seen yet";

// mouse state seen by the game
std::atomic<long> gMsCalls{0}, gMsGame{0}, gMsRemapped{0};
long long gMsPosX = 0, gMsPosY = 0, gMsWheel = 0;
unsigned  gMsButtons = 0;
POINT     gMsCursor{}; // real cursor at the same moment
bool      gMsHasAbs = false;
double    gMsAbsX = 0, gMsAbsY = 0;

template <class S> void recordMouse(S* st) {
    if (!st) return;
    if constexpr (requires { st->buttons; }) gMsButtons = unsigned(st->buttons);
    if constexpr (requires { st->positionX; }) { gMsPosX = (long long)st->positionX; gMsPosY = (long long)st->positionY; }
    if constexpr (requires { st->wheelY; }) gMsWheel = (long long)st->wheelY;
    if constexpr (requires { st->absolutePositionX; }) {
        gMsHasAbs = true;
        gMsAbsX   = double(st->absolutePositionX);
        gMsAbsY   = double(st->absolutePositionY);
    }
    if (gGcpOrig) reinterpret_cast<GetCursorPos_t>(gGcpOrig)(&gMsCursor);
    else GetCursorPos(&gMsCursor);
}

template <class M> struct MouseHook;
template <class C, class R, bool NE, class... A> struct MouseHook<R (C::*)(A...) noexcept(NE)> {
    static inline void* orig = nullptr;
    static R STDMETHODCALLTYPE fn(C* self, A... a) {
        R r = reinterpret_cast<R (*)(C*, A...)>(orig)(self, a...);
        if (gEnabled.load(std::memory_order_relaxed)) {
            ++gMsCalls;
            if (fromGame(__builtin_return_address(0))) ++gMsGame;
            (recordMouse(a), ...);
        }
        return r;
    }
};
template <class C, class R, bool NE, class... A> struct MouseHook<R (C::*)(A...) const noexcept(NE)> {
    static inline void* orig = nullptr;
    static R STDMETHODCALLTYPE fn(const C* self, A... a) {
        R r = reinterpret_cast<R (*)(const C*, A...)>(orig)(self, a...);
        if (gEnabled.load(std::memory_order_relaxed)) {
            ++gMsCalls;
            if (fromGame(__builtin_return_address(0))) ++gMsGame;
            (recordMouse(a), ...);
        }
        return r;
    }
};
using MouseM = decltype(&IGameInputReading::GetMouseState);

void noteReading(IGameInputReading* rd) {
    if (gMouseHooked || !rd) return;
    gMouseHooked = true;
    gIdxMouse    = vindex(&IGameInputReading::GetMouseState);
    void** vt    = *reinterpret_cast<void***>(rd);
    if (gIdxMouse < 0) { gMouseNote = "GetMouseState slot not found"; return; }
    if (ll_shim::hook(vt[gIdxMouse], reinterpret_cast<void*>(&MouseHook<MouseM>::fn), &MouseHook<MouseM>::orig) == 0)
        gMouseNote = "GetMouseState hooked (slot " + std::to_string(gIdxMouse) + ")";
    else gMouseNote = "GetMouseState hook failed";
}

// counters per IGameInput method, keyed by Id
struct GiStat {
    std::atomic<long> calls{0}, game{0}, ok{0};
    std::atomic<uint32_t> kinds{0}; // OR of the kinds asked for
};
GiStat gGiStat[4];
const char* gGiName[4] = {"GetCurrentReading", "GetNextReading", "GetPreviousReading", "RegisterReadingCallback"};

inline void scanArg(uint32_t& kind, IGameInputReading**&, GameInputKind k) { kind |= uint32_t(k); }
inline void scanArg(uint32_t&, IGameInputReading**& out, IGameInputReading** r) { out = r; }
template <class T> void scanArg(uint32_t&, IGameInputReading**&, T) {}

template <int Id, class M> struct GiHook;
template <int Id, class C, class R, bool NE, class... A> struct GiHook<Id, R (C::*)(A...) noexcept(NE)> {
    static inline void* orig = nullptr;
    static R STDMETHODCALLTYPE fn(C* self, A... a) {
        R r = reinterpret_cast<R (*)(C*, A...)>(orig)(self, a...);
        auto& st = gGiStat[Id];
        ++st.calls;
        if (fromGame(__builtin_return_address(0))) ++st.game;
        uint32_t kind = 0;
        IGameInputReading** out = nullptr;
        (scanArg(kind, out, a), ...);
        st.kinds.fetch_or(kind);
        bool ok;
        if constexpr (std::is_same_v<R, bool>) ok = r;
        else ok = SUCCEEDED(r);
        if (ok) ++st.ok;
        if (ok && out && *out) noteReading(*out);
        return r;
    }
};
using CurM  = decltype(&IGameInput::GetCurrentReading);
using NextM = decltype(&IGameInput::GetNextReading);
using PrevM = decltype(&IGameInput::GetPreviousReading);
using RegM  = decltype(&IGameInput::RegisterReadingCallback);

template <int Id, class M> void hookSlot(void** vt, int idx) {
    if (idx < 0) return;
    if (ll_shim::hook(vt[idx], reinterpret_cast<void*>(&GiHook<Id, M>::fn), &GiHook<Id, M>::orig) != 0) GiHook<Id, M>::orig = nullptr;
}

void hookGameInput(IGameInput* gi) {
    initGameMod();
    gIdxCur  = vindex(&IGameInput::GetCurrentReading);
    gIdxNext = vindex(&IGameInput::GetNextReading);
    gIdxPrev = vindex(&IGameInput::GetPreviousReading);
    gIdxReg  = vindex(&IGameInput::RegisterReadingCallback);
    void** vt = *reinterpret_cast<void***>(gi);
    hookSlot<0, CurM>(vt, gIdxCur);
    hookSlot<1, NextM>(vt, gIdxNext);
    hookSlot<2, PrevM>(vt, gIdxPrev);
    hookSlot<3, RegM>(vt, gIdxReg);
    gGiNote = "game IGameInput hooked (slots cur " + std::to_string(gIdxCur) + " next " + std::to_string(gIdxNext) + " prev " +
              std::to_string(gIdxPrev) + " reg " + std::to_string(gIdxReg) + ")";
}

// The game may only use callbacks: ask its IGameInput for one reading so the reading vtable gets hooked.
void tryGameMouse() {
    if (gMouseHooked || !gGameInput || !GiHook<0, CurM>::orig) return;
    auto cur = reinterpret_cast<HRESULT (*)(IGameInput*, GameInputKind, IGameInputDevice*, IGameInputReading**)>(GiHook<0, CurM>::orig);
    for (GameInputKind k : {GameInputKindMouse, GameInputKindKeyboard}) {
        IGameInputReading* rd = nullptr;
        HRESULT hr = cur(static_cast<IGameInput*>(gGameInput), k, nullptr, &rd);
        if (SUCCEEDED(hr) && rd) {
            noteReading(rd);
            rd->Release();
            return;
        }
        char b[96];
        snprintf(b, sizeof b, "own GetCurrentReading(kind 0x%x) hr 0x%08lX", unsigned(k), long(hr));
        gMouseNote = b;
    }
}
#else
void hookGameInput(void*) { gGiNote = "built without GameInput.h"; }
void tryGameMouse() {}
#endif

using GiCreate_t = HRESULT(WINAPI*)(void**);
HRESULT WINAPI gicHook(void** out) {
    HRESULT hr = reinterpret_cast<GiCreate_t>(gGicOrig)(out);
    ++gGicCalls;
    if (SUCCEEDED(hr) && out && *out && !gGameInput) {
        gGameInput = *out;
#if RF_HAVE_GAMEINPUT
        hookGameInput(static_cast<IGameInput*>(gGameInput));
#else
        hookGameInput(gGameInput);
#endif
    }
    return hr;
}

void installEarly() {
    if (gGicOrig) return;
    initGameMod();
    HMODULE gi = LoadLibraryW(L"GameInput.dll");
    if (!gi) { gGiNote = "GameInput.dll not loadable"; return; }
    void* create = reinterpret_cast<void*>(GetProcAddress(gi, "GameInputCreate"));
    if (!create) { gGiNote = "GameInputCreate missing"; return; }
    if (ll_shim::hook(create, reinterpret_cast<void*>(&gicHook), &gGicOrig) == 0) gGiNote = "GameInputCreate hooked (waiting for the game)";
    else gGiNote = "GameInputCreate hook failed";
}

void writeDiag() {
    char b[6000];
    snprintf(b, sizeof b,
             "Spatial GUI panel diag v0.10\n%s\nGetCursorPos calls (panel on, our window known): %ld, inside client: %ld, remapped: %ld\n"
             "Mouse WM_* seen while on: %ld, remapped: %ld\nWM_INPUT mouse seen: %ld, swallowed: %ld\n"
             "GetRawInputData reads: %ld, mouse: %ld (last dx %ld dy %ld buttons 0x%x) hook %s\n"
             "ClipCursor calls: %ld (last rect %ld,%ld-%ld,%ld) hook %s\n"
             "last GetCursorPos in->out (oldest->newest):\n%s"
             "status: %s\n",
             gHookNote.c_str(), gGcpCalls.load(), gGcpInClient.load(), gGcpRemapped.load(), gWmSeen.load(), gWmRemapped.load(),
             gRawSeen.load(), gRawEaten.load(), gRawReads.load(), gRawMouseReads.load(), gRawDx.load(), gRawDy.load(),
             gRawBtn.load(), gRawTarget ? "on" : "off", gClipCalls.load(), gLastClip.left, gLastClip.top, gLastClip.right,
             gLastClip.bottom, gClipTarget ? "on" : "off",
             [] {
                 static char s[4][80];
                 std::string out;
                 unsigned end = gGcpRingIdx.load();
                 for (unsigned k = 0; k < 4; ++k) {
                     unsigned i = (end + k) % 4;
                     snprintf(s[k], sizeof s[k], "  in %ld,%ld -> out %ld,%ld\n", gGcpRing[i].inX, gGcpRing[i].inY, gGcpRing[i].outX, gGcpRing[i].outY);
                     out += s[k];
                 }
                 return out;
             }().c_str(),
             gStatus.c_str());
    // v0.5: project the four drawn panel corners to client pixels and run them through the ray-pick.
    // A correct remap sends each corner to the matching client corner (0,0), (W,0), (0,H), (W,H).
    {
        HWND h = gHwnd.load();
        Geom g;
        {
            std::lock_guard l(gGeomMutex);
            g = gGeom;
        }
        RECT rc{};
        size_t len = strlen(b);
        if (h && g.valid && GetClientRect(h, &rc)) {
            float W = float(rc.right - rc.left), H = float(rc.bottom - rc.top);
            float cs[4][2] = {{-g.hw, g.hh}, {g.hw, g.hh}, {-g.hw, -g.hh}, {g.hw, -g.hh}};
            const char* nm[4] = {"TL", "TR", "BL", "BR"};
            len += snprintf(b + len, sizeof b - len, "client %gx%g  panel hw %.3f hh %.3f cz %.2f  tanHalf %.3f aspect %.3f\n", W, H, g.hw, g.hh, g.cz, g.tanHalf, g.aspect);
            for (int k = 0; k < 4; ++k) {
                float c4[4];
                toClip(g, cs[k][0], cs[k][1], c4);
                float nx = c4[0] / c4[3], ny = c4[1] / c4[3];
                int px = int((nx + 1.0f) * 0.5f * W), py = int((1.0f - ny) * 0.5f * H);
                int rx = px, ry = py;
                bool ok = remapClient(h, rx, ry);
                len += snprintf(b + len, sizeof b - len, "corner %s: drawn %d,%d -> remap %d,%d (%s)\n", nm[k], px, py, rx, ry, ok ? "ok" : "no");
            }
        }
    }
    {
        size_t len = strlen(b);
        auto line = [&](const char* nm, SrcStat& st) {
            len += snprintf(b + len, sizeof b - len, "  %-22s calls %ld, from game %ld, remapped %ld\n", nm, st.calls.load(), st.game.load(), st.remapped.load());
        };
        len += snprintf(b + len, sizeof b - len, "cursor sources while on (v0.10):\n  GetCursorPos from game %ld\n", gStGcpGame.game.load());
        line("GetCursorInfo", gStCursorInfo);
        line("GetPhysicalCursorPos", gStPhys);
        line("GetMessagePos", gStMsgPos);
        line("pump mouse MSG", gStPump);
        len += snprintf(b + len, sizeof b - len, "  wndproc skipped (already remapped in pump) %ld; pump remap %s\n", gPumpSkippedWnd.load(), gS.pumpRemap ? "on" : "off");
        len += snprintf(b + len, sizeof b - len, "GameInput: %s (GameInputCreate calls %ld)\n", gGiNote.c_str(), gGicCalls.load());
#if RF_HAVE_GAMEINPUT
        for (int k = 0; k < 4; ++k)
            len += snprintf(b + len, sizeof b - len, "  %-24s calls %ld, from game %ld, ok %ld, kinds 0x%x\n", gGiName[k], gGiStat[k].calls.load(), gGiStat[k].game.load(),
                            gGiStat[k].ok.load(), unsigned(gGiStat[k].kinds.load()));
        len += snprintf(b + len, sizeof b - len, "  mouse: %s; GetMouseState calls %ld, from game %ld\n  last: buttons 0x%x pos %lld,%lld wheel %lld abs %s %.1f,%.1f  cursor %ld,%ld\n",
                        gMouseNote.c_str(), gMsCalls.load(), gMsGame.load(), gMsButtons, gMsPosX, gMsPosY, gMsWheel, gMsHasAbs ? "yes" : "no", gMsAbsX, gMsAbsY,
                        gMsCursor.x, gMsCursor.y);
#endif
        (void)len;
    }
    uicapture::writeText("panel_diag.txt", b);
}

bool swallow(UINT msg, LPARAM lp) {
    if (msg != WM_INPUT || !gEnabled.load(std::memory_order_relaxed) || gAnim.load() < 0.5f || RfOptions::showImGui) return false;
    RAWINPUTHEADER hdr{};
    UINT           sz = sizeof hdr;
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_HEADER, &hdr, &sz, sizeof(RAWINPUTHEADER)) == UINT(-1)) return false;
    if (hdr.dwType != RIM_TYPEMOUSE) return false;
    ++gRawSeen;
    if (!gS.eatRaw) return false;
    ++gRawEaten;
    return true;
}

bool remapMouse(HWND hwnd, UINT msg, LPARAM& lp) {
    gHwnd = hwnd;
    switch (msg) {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK: break;
    default: return false;
    }
    if (gEnabled.load(std::memory_order_relaxed)) ++gWmSeen;
    if (gEnabled.load(std::memory_order_relaxed) && pumpAlreadyRemapped(msg, lp)) { ++gPumpSkippedWnd; return false; }
    int x = short(LOWORD(lp)), y = short(HIWORD(lp));
    if (!remapClient(hwnd, x, y)) return false;
    ++gWmRemapped;
    lp = MAKELPARAM(x, y);
    return true;
}

// ---------------------------------------------------------------- F6
void drawUi() {
    if (!ImGui::CollapsingHeader("Spatial GUI panel (M1)")) return;
    ImGui::Indent();
    if (!RfOptions::uiCaptureSpike) {
        ImGui::TextWrapped("Needs \"uiCaptureSpike\": true in RendererFoundation.json (restart the game).");
    } else {
        bool on = gEnabled;
        if (ImGui::Checkbox("Panel on (F8)", &on)) toggle();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", gStatus.c_str());
        ImGui::SliderFloat("Size", &gS.size, 0.3f, 1.0f, "%.2f");
        ImGui::SliderFloat("Tilt", &gS.tilt, -30.0f, 30.0f, "%.1f deg");
        ImGui::SliderFloat("Yaw", &gS.yaw, -30.0f, 30.0f, "%.1f deg");
        ImGui::SliderFloat("Offset X", &gS.offsetX, -1.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Offset Y", &gS.offsetY, -1.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("World zoom (FOV lerp)", &gS.zoom, 0.0f, 0.3f, "%.2f");
        ImGui::SliderFloat("World dim", &gS.dim, 0.0f, 0.9f, "%.2f");
        ImGui::SliderFloat("Animation", &gS.seconds, 0.0f, 1.0f, "%.2f s");
        ImGui::Checkbox("Alpha boost", &gS.alphaBoost);
        ImGui::SameLine();
        ImGui::Checkbox("Remap mouse (ray-pick)", &gS.remap);
        ImGui::Checkbox("Hide raw mouse input from the game (experiment)", &gS.eatRaw);
        ImGui::Checkbox("Remap mouse messages in the message pump", &gS.pumpRemap);
        ImGui::TextWrapped("M1 prototype: everything the UI draws (HUD included) goes onto the panel while it is on.");
    }
    ImGui::Unindent();
}

} // namespace rf::panel
