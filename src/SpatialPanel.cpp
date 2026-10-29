// Renderer Foundation - Spatial GUI M1 panel (see SpatialPanel.h, shaders/panel.hlsl). Original code for Renderer Foundation.
#include "SpatialPanel.h"

#include <algorithm>
#include <atomic>
#include <cctype>
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

// GameInput API v2 header: fetched from the Microsoft.GameInput NuGet package by CI into deps/ (not vendored)
#if __has_include("gameinput-v2/GameInput.h")
#include "gameinput-v2/GameInput.h"
#define RF_HAVE_GAMEINPUT 1
#else
#define RF_HAVE_GAMEINPUT 0
#endif

#include "LLShim.h"
#include "Options.h"
#include "PanelShaders.h"
#include "SpatialCamera.h"
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
    bool  autoPanel  = true; // v0.12: panel up automatically for container screens only
    bool  javaStyle  = true; // v0.13: Spatial GUI (Java) look: third-person camera, world-placed screen, pop-in
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

// v0.13: the Java mod's screen placement (RenderUtil.applyScreenTransform + InventoryRenderer.updateQuadBasis, config
// from SpatialGUI.json), expressed in view space. Third person: the screen hangs off the CAMERA in the player's yaw
// frame (distance forward, -side to the right, height up), turned (180 - yaw) degrees. First person: it sits along the
// look direction at open (pitch clamped), facing the player. Height = scale (x FOV factor) for the full-window texture.
Geom computeGeomJava(spatialcam::View const& v, float uiAspect, float anim, float guiMag) {
    using namespace spatialcam;
    constexpr float kD = 0.017453292f;
    Config const& c = cfg();
    Geom g{};
    g.valid   = true;
    g.tanHalf = v.valid ? v.tanHalf : std::tan(35.0f * kD);
    g.aspect  = v.valid ? v.aspect : uiAspect;
    float fm       = c.autoScaleByFov ? (v.valid ? v.fovYDeg : c.baselineFov) / c.baselineFov : 1.0f;
    float fovScale = std::pow(fm, c.screenPower); // RenderUtil.calculateFovScaleMultiplier
    float scale;
    float P[3], EX[3], EY[3];
    float pt;
    if (v.firstPerson) {
        scale    = c.fpScale * fovScale;
        float p0 = v.fpPitch0 * kD;
        float dx = 0.0f, dy = -std::sin(p0), dz = std::cos(p0); // look direction at open (player yaw frame)
        P[0] = -c.fpSide; P[1] = dy * c.fpDistance + c.fpHeight; P[2] = dz * c.fpDistance;
        (void)dx;
        float a = (180.0f - c.fpYaw) * kD;  // rotY(-yaw + yawOffset): 180 = facing the player
        float pp = p0 - c.fpPitch * kD;     // rotX(-pitch + pitchOffset)
        EX[0] = std::cos(a); EX[1] = 0.0f; EX[2] = -std::sin(a);
        EY[0] = 0.0f; EY[1] = std::cos(pp); EY[2] = std::sin(pp);
        pt = v.pitch * kD;
    } else {
        float tpMult = 1.0f + (fovScale - 1.0f) * c.tpScreenMult;
        scale        = c.screenScale * tpMult;
        float side = c.mirrorThirdPerson ? -c.screenSide : c.screenSide;
        float yaw  = c.mirrorThirdPerson ? -c.screenYaw : c.screenYaw;
        float a    = (180.0f - yaw) * kD;
        P[0] = -side; P[1] = c.screenHeight; P[2] = c.screenDistance;
        float pp = -c.screenPitch * kD;
        EX[0] = std::cos(a); EX[1] = 0.0f; EX[2] = -std::sin(a);
        EY[0] = 0.0f; EY[1] = std::cos(pp); EY[2] = std::sin(pp);
        pt = (v.valid ? v.pitch : c.camPitch) * kD;
    }
    scale *= anim * guiMag;
    g.hh = 0.5f * scale;
    g.hw = g.hh * uiAspect;
    float yr = v.yawRel * kD;
    auto toView = [&](float const* q, float* o) {
        float x1 = q[0] * std::cos(yr) - q[2] * std::sin(yr);
        float z1 = q[0] * std::sin(yr) + q[2] * std::cos(yr);
        o[0]     = x1;
        o[1]     = q[1] * std::cos(pt) + z1 * std::sin(pt);
        o[2]     = z1 * std::cos(pt) - q[1] * std::sin(pt);
    };
    float cc[3];
    toView(P, cc);
    g.cx = cc[0]; g.cy = cc[1]; g.cz = cc[2];
    toView(EX, g.ex);
    toView(EY, g.ey);
    g.n[0] = g.ex[1] * g.ey[2] - g.ex[2] * g.ey[1];
    g.n[1] = g.ex[2] * g.ey[0] - g.ex[0] * g.ey[2];
    g.n[2] = g.ex[0] * g.ey[1] - g.ex[1] * g.ey[0];
    return g;
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
LARGE_INTEGER gOpenQpc{};
float         gGuiMag = 1.0f;
float         gDrawFrac[2] = {1.0f, 1.0f};
void toggle() {
    bool on = !gEnabled.load();
    if (on) QueryPerformanceCounter(&gOpenQpc);
    if (on) { installCursorHook(); installSourceHooks(); tryGameMouse(); }
    else writeDiag();
    gEnabled = on;
    if (on) uicapture::requestLearn(); // make sure the back-buffer RTV handles are known
    ll_shim::info(std::string("[RF] spatial panel ") + (on ? "on" : "off"));
}

// ---------------------------------------------------------------- Present
// ---------------------------------------------------------------- automatic panel (v0.12)
// SceneStack::update (ScreenWatch.cpp) reports each stack's top screen every tick. The panel comes up when a container
// screen is on top of any stack and goes away when none is; HUD, pause, settings and chat stay flat. Edge-triggered,
// so F8 still overrides until the next screen change. Applied on the render thread at the next Present.
std::mutex               gScreenMutex;
struct StackTop { void const* stack; std::string name; };
std::vector<StackTop>    gStackTops;
std::vector<std::string> gScreensSeen;
std::atomic<int>         gWanted{-1};
bool                     gLastWant = false;

// SpatialGUIClient.shouldHookScreen, mapped onto Bedrock screen names
bool isPanelScreen(std::string const& n) {
    std::string l = n;
    for (auto& ch : l) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    auto has = [&](char const* k) { return l.find(k) != std::string::npos; };
    auto const& c = spatialcam::cfg();
    if (!c.enabled || l.empty()) return false;
    // never: title / loading / chat / the HUD layers
    if (has("start_screen") || has("loading") || has("progress") || has("chat") || has("hud") || has("toast") ||
        has("hbui") || has("ui://"))
        return false;
    if (c.allScreens) return true;
    if (has("book") || has("lectern")) return has("lectern") ? c.lectern : c.books;
    if (has("pause")) return c.pauseScreen;
    if (has("setting")) return false;
    if (has("crafting") || has("crafter")) return c.crafting;
    if (has("furnace") || has("smoker")) return c.furnaces;
    if (has("anvil")) return c.anvils;
    if (has("enchant")) return c.enchanting;
    if (has("beacon")) return c.beacons;
    if (has("brewing")) return c.brewing;
    if (has("trade") || has("villager")) return c.villagerTrading;
    if (has("shulker")) return c.shulkerBoxes;
    if (has("chest") || has("barrel")) return c.chests;
    if (has("hopper")) return c.hoppers;
    if (has("dispenser") || has("dropper")) return c.dispensers;
    if (has("grindstone")) return c.grindstone;
    if (has("smithing")) return c.smithing;
    if (has("cartography")) return c.cartography;
    if (has("loom")) return c.loom;
    if (has("stonecutter")) return c.stonecutter;
    if (has("creative")) return c.creativeInventory;
    if (has("inventory")) return c.inventory;
    if (has("horse") || has("container") || has("chest_boat") || has("minecart")) return c.mostContainers;
    return false;
}

void applyWanted() {
    int w = gWanted.exchange(-1);
    if (w < 0 || !RfOptions::uiCaptureSpike || !gS.autoPanel) return;
    if ((w == 1) != gEnabled.load()) toggle();
}

std::atomic_bool gRecipeBook{false};
void noteScreen(void const* stack, std::string const& name) {
    std::lock_guard l(gScreenMutex);
    StackTop* st = nullptr;
    for (auto& t : gStackTops)
        if (t.stack == stack) st = &t;
    if (!st) {
        if (gStackTops.size() >= 8) return;
        gStackTops.push_back({stack, {}});
        st = &gStackTops.back();
    }
    if (st->name == name) return;
    st->name = name;
    if (gScreensSeen.size() < 40 && std::find(gScreensSeen.begin(), gScreensSeen.end(), name) == gScreensSeen.end()) gScreensSeen.push_back(name);
    bool want = false;
    for (auto& t : gStackTops) want = want || isPanelScreen(t.name);
    if (want != gLastWant) {
        gLastWant = want;
        gWanted   = want ? 1 : 0;
        bool inv = false, book = false;
        for (auto& t : gStackTops) {
            inv  = inv || t.name.find("inventory_screen") != std::string::npos;
            // Bedrock shows the recipe book next to these screens (Java: recipe book open -> shrink)
            for (char const* k : {"crafting_screen", "furnace_screen", "smoker_screen", "blast_furnace_screen"})
                book = book || t.name.find(k) != std::string::npos;
        }
        gRecipeBook = book;
        spatialcam::setWideScreen(book);
        if (gS.autoPanel && gS.javaStyle && RfOptions::uiCaptureSpike) spatialcam::setActive(want, inv); // client thread
    }
}

bool record(ID3D12Device* device, ID3D12GraphicsCommandList* list, ID3D12Resource* bb, D3D12_CPU_DESCRIPTOR_HANDLE const& bbRtv) {
    LARGE_INTEGER now, f;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&f);
    float dt = gLast.QuadPart ? float(double(now.QuadPart - gLast.QuadPart) / double(f.QuadPart)) : 0.0f;
    gLast    = now;
    dt       = std::clamp(dt, 0.0f, 0.1f);
    applyWanted();
    {
        static ULONGLONG lastDiag = 0;
        ULONGLONG        nowMs    = GetTickCount64();
        if (gEnabled && nowMs - lastDiag > 1000) { lastDiag = nowMs; writeDiag(); } // live diagnostics while open
    }
    float target = gEnabled ? 1.0f : 0.0f;
    float step   = gS.seconds > 0.01f ? dt / gS.seconds : 1.0f;
    if (gS.javaStyle) gT = target; // Java: the screen pops in (scale + fade below) and is gone when it closes
    else gT = gT < target ? std::min(target, gT + step) : std::max(target, gT - step);
    float e      = gS.javaStyle ? gT : ease(gT);
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
    float fade   = e;
    Geom  g;
    if (gS.javaStyle) {
        // AnimationUtil.calculateAnimatedScale (Back easing from 75 %) + fade-in
        float ms    = float(double(now.QuadPart - gOpenQpc.QuadPart) * 1000.0 / double(f.QuadPart));
        auto const& sc  = spatialcam::cfg();
        float       tt  = std::min(1.0f, ms / float(std::max(1, sc.openMs)));
        float       s0  = sc.startScalePct / 100.0f;
        float scale = sc.scaleAnim ? s0 + (1.0f - s0) * spatialcam::applyEasing(sc.easing, tt) : 1.0f;
        if (gRecipeBook.load()) scale /= std::max(1.0f, sc.recipeBookShrink); // recipe book open: wider screen
        fade        = (sc.fade ? std::min(1.0f, ms / float(std::max(1, sc.fadeMs))) : 1.0f) * (sc.screenAlpha / 255.0f);
        // The Java mod renders the screen at its own auto GUI scale (>= 320 x 240 units); Bedrock's UI is smaller in
        // the window, so magnify the texture by java/bedrock scale to give the GUI the same world size.
        spatialcam::View v = spatialcam::view();
        float jgs = spatialcam::cfg().autoGuiScale ? float(spatialcam::javaAutoGuiScale(int(bd.Width), int(bd.Height))) : float(std::clamp(spatialcam::cfg().guiScale, 1, 6));
        float mag = v.guiScale > 0.5f ? jgs / v.guiScale : 1.0f;
        gGuiMag   = mag;
        g         = computeGeomJava(v, aspect, scale, mag);
    } else {
        g = computeGeom(e, aspect);
    }
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

    // v0.13e: only draw the part of the (huge, magnified) quad that lies well in front of the camera - a triangle
    // crossing the camera plane gets wrong texture mapping. a/b = drawn fraction of the half-width/half-height; the
    // GUI sits in the middle of the texture. The ray-pick still uses the full plane.
    float a = 1.0f, bb2 = 1.0f;
    auto  zAt = [&](float lx, float ly) { return g.cz + g.ex[2] * lx + g.ey[2] * ly; };
    for (int it = 0; it < 60; ++it) {
        float zmin = std::min(std::min(zAt(-a * g.hw, bb2 * g.hh), zAt(a * g.hw, bb2 * g.hh)), std::min(zAt(-a * g.hw, -bb2 * g.hh), zAt(a * g.hw, -bb2 * g.hh)));
        if (zmin >= 0.3f) break;
        a *= 0.92f;
        bb2 *= 0.92f;
    }
    gDrawFrac[0] = a;
    gDrawFrac[1] = bb2;
    float c[32]{};
    toClip(g, -a * g.hw, bb2 * g.hh, c + 0);   // TL
    toClip(g, a * g.hw, bb2 * g.hh, c + 4);    // TR
    toClip(g, -a * g.hw, -bb2 * g.hh, c + 8);  // BL
    toClip(g, a * g.hw, -bb2 * g.hh, c + 12);  // BR
    c[16] = gS.javaStyle ? 1.0f : 1.0f + gS.zoom * e;
    c[17] = fade;
    c[18] = gS.javaStyle ? 0.0f : gS.dim * e;
    c[19] = gS.alphaBoost ? 1.0f : 0.0f;
    c[20] = gS.javaStyle ? 0.0f : 1.0f; // panel frame line
    c[21] = gS.javaStyle ? 1.0f : 0.0f; // drop the dim background
    c[22] = a;                          // drawn uv fraction (VSPanel)
    c[23] = bb2;

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
// Real cursor (not remapped) in [-1, 1] over the client area, y down - parallax for the Java camera (SpatialCamera.cpp).
std::atomic<unsigned long long> gCrosshairFrame{0};
bool cursorNdc(float& x, float& y) {
    HWND h = gHwnd.load(std::memory_order_relaxed);
    if (!h) return false;
    POINT p{};
    if (gGcpOrig) reinterpret_cast<GetCursorPos_t>(gGcpOrig)(&p);
    else GetCursorPos(&p);
    RECT rc;
    if (!ScreenToClient(h, &p) || !GetClientRect(h, &rc) || rc.right < 1 || rc.bottom < 1) return false;
    x = 2.0f * float(p.x) / float(rc.right) - 1.0f;
    y = 2.0f * float(p.y) / float(rc.bottom) - 1.0f;
    return true;
}
// Java crosshair mode (useCrosshairForFirstPerson): the mouse is grabbed at the window centre, its motion turns the
// camera and the crosshair is the pointer. Returns the motion since the last call (client pixels) and re-centres.
bool grabCursorDelta(float& dx, float& dy, bool reset) {
    dx = dy = 0.0f;
    HWND h = gHwnd.load(std::memory_order_relaxed);
    if (!h || GetForegroundWindow() != h) return false;
    RECT rc;
    if (!GetClientRect(h, &rc) || rc.right < 2 || rc.bottom < 2) return false;
    POINT c{rc.right / 2, rc.bottom / 2};
    POINT p{};
    if (gGcpOrig) reinterpret_cast<GetCursorPos_t>(gGcpOrig)(&p);
    else GetCursorPos(&p);
    POINT cs = c;
    ClientToScreen(h, &cs);
    if (!reset) { dx = float(p.x - cs.x); dy = float(p.y - cs.y); }
    if (p.x != cs.x || p.y != cs.y) SetCursorPos(cs.x, cs.y);
    gCrosshairFrame = GetTickCount64();
    return true;
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
        if (fromGame(__builtin_return_address(0))) {
            ++gStPhys.game;
            if (remapScreen(*p)) ++gStPhys.remapped;
        }
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

// ---------------------------------------------------------------- GameInput v2 (v0.11)
// The game talks to GameInput API v2 (its binary carries the v2 IGameInput IID). Microsoft's loader picks
// System32\GameInput.dll or GameInputRedist.dll (System32, or RedistDir from HKLM\SOFTWARE\Microsoft\GameInput),
// whichever is newer, and calls GameInputInitialize(IID_IGameInput). JSON UI screens (inventory, chest, crafting)
// take the menu cursor from the mouse reading's absolutePositionX/Y.
// We create our own v2 IGameInput from every candidate module at panel toggle: COM vtables are per class, so detouring
// IGameInputReading::GetMouseState through our own reading also catches the game's readings. The absolute position is
// then ray-picked through the panel like the WM_* coordinates.
std::string gGiNote = "not tried yet";

#if RF_HAVE_GAMEINPUT
namespace gi = GameInput::v2;

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
template <class M> int vindex(M m) { return MemFn<M>::index(m); }

// mouse readings seen while the panel is on
std::atomic<long> gMsCalls{0}, gMsGame{0}, gMsAbs{0}, gMsRemapped{0}, gMsMismatch{0};
int       gMsSpace = 0; // absolute position space: 0 unknown, 1 screen, 2 client
long      gMatchScreen = 0, gMatchClient = 0;
unsigned  gMsButtons = 0, gMsPositions = 0;
long long gMsAbsX = 0, gMsAbsY = 0, gMsOutX = 0, gMsOutY = 0, gMsRelX = 0, gMsRelY = 0;
POINT     gMsCursor{}, gMsClient{};

POINT realCursor() {
    POINT p{};
    if (gGcpOrig) reinterpret_cast<GetCursorPos_t>(gGcpOrig)(&p);
    else GetCursorPos(&p);
    return p;
}

bool remapAbs(int64_t& ax, int64_t& ay) {
    HWND h = gHwnd.load(std::memory_order_relaxed);
    if (!h) return false;
    if (gMsSpace == 0) {
        // learn which space absolutePosition is in by comparing it with the real cursor
        POINT cur = realCursor(), cl = cur;
        ScreenToClient(h, &cl);
        gMsCursor = cur;
        gMsClient = cl;
        bool same = cur.x == cl.x && cur.y == cl.y;
        if (std::llabs(ax - cl.x) <= 2 && std::llabs(ay - cl.y) <= 2) ++gMatchClient;
        else if (!same && std::llabs(ax - cur.x) <= 2 && std::llabs(ay - cur.y) <= 2) ++gMatchScreen;
        else ++gMsMismatch;
        if (gMatchClient >= 3) gMsSpace = 2;
        else if (gMatchScreen >= 3) gMsSpace = 1;
        if (gMsSpace == 0) return false;
    }
    if (gMsSpace == 2) {
        RECT rc;
        if (!GetClientRect(h, &rc) || ax < 0 || ay < 0 || ax >= rc.right || ay >= rc.bottom) return false;
        int x = int(ax), y = int(ay);
        if (!remapClient(h, x, y)) return false;
        ax = x;
        ay = y;
        return true;
    }
    POINT p{LONG(ax), LONG(ay)};
    if (!remapScreen(p)) return false;
    ax = p.x;
    ay = p.y;
    return true;
}

// last distinct readings (buttons / absolute in -> out), for click diagnostics
struct MsLog { unsigned buttons, positions; long long inX, inY, outX, outY; long seq; };
MsLog             gMsLog[24]{};
std::atomic<unsigned> gMsLogIdx{0};
MsLog             gMsPrev{};
std::atomic<long> gMsSeq{0};

void onMouse(gi::GameInputMouseState* st, bool game) {
    if (!st) return;
    long seq = ++gMsSeq;
    long long inX = st->absolutePositionX, inY = st->absolutePositionY;
    ++gMsCalls;
    if (game) ++gMsGame;
    gMsButtons   = unsigned(st->buttons);
    gMsPositions = unsigned(st->positions);
    gMsRelX      = st->positionX;
    gMsRelY      = st->positionY;
    gMsAbsX      = st->absolutePositionX;
    gMsAbsY      = st->absolutePositionY;
    if (gMsPositions & unsigned(gi::GameInputMouseAbsolutePosition)) {
        ++gMsAbs;
        if (gS.remap && remapAbs(st->absolutePositionX, st->absolutePositionY)) ++gMsRemapped;
    }
    gMsOutX = st->absolutePositionX;
    gMsOutY = st->absolutePositionY;
    MsLog e{unsigned(st->buttons), unsigned(st->positions), inX, inY, st->absolutePositionX, st->absolutePositionY, seq};
    if (e.buttons != gMsPrev.buttons || e.inX != gMsPrev.inX || e.inY != gMsPrev.inY || e.outX != gMsPrev.outX || e.outY != gMsPrev.outY ||
        e.positions != gMsPrev.positions) {
        gMsPrev = e;
        gMsLog[gMsLogIdx.fetch_add(1) % 24] = e;
    }
}
template <class T> void onMouse(T, bool) {}
// Testing aid (uiDebugUseKey, off by default): remote input tools can only send instant right clicks, which the game
// misses because it samples GameInput state. While the key is held the game's mouse reading reports the right button.
void debugUse(gi::GameInputMouseState* st) {
    int k = RfOptions::uiDebugUseKey.get();
    if (st && k > 0 && k < 256 && (GetAsyncKeyState(k) & 0x8000)) st->buttons = st->buttons | gi::GameInputMouseRightButton;
}
template <class T> void debugUse(T) {}

template <int Id, class M> struct MouseHook;
template <int Id, class C, class R, bool NE, class... A> struct MouseHook<Id, R (C::*)(A...) noexcept(NE)> {
    static inline void* orig = nullptr;
    static R STDMETHODCALLTYPE fn(C* self, A... a) {
        R r = reinterpret_cast<R (*)(C*, A...)>(orig)(self, a...);
        if (r) {
            (debugUse(a), ...);
            if (gEnabled.load(std::memory_order_relaxed)) {
                bool game = fromGame(__builtin_return_address(0));
                (onMouse(a, game), ...);
            }
        }
        return r;
    }
};
using MouseM = decltype(&gi::IGameInputReading::GetMouseState);

struct GiMod {
    const char*       label;
    std::wstring      path;
    HMODULE           mod   = nullptr;
    gi::IGameInput*   input = nullptr;
    bool              hooked = false;
    std::string       note  = "not tried";
};
GiMod gGiMods[3] = {{"System32 GameInput.dll"}, {"System32 GameInputRedist.dll"}, {"RedistDir GameInputRedist.dll"}};
bool  gGiPathsDone = false;

void giPaths() {
    if (gGiPathsDone) return;
    gGiPathsDone = true;
    wchar_t sys[MAX_PATH]{};
    UINT    n = GetSystemDirectoryW(sys, MAX_PATH);
    if (n && n < MAX_PATH) {
        gGiMods[0].path = std::wstring(sys) + L"\\GameInput.dll";
        gGiMods[1].path = std::wstring(sys) + L"\\GameInputRedist.dll";
    }
    wchar_t dir[MAX_PATH]{};
    DWORD   sz = sizeof dir;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\GameInput", L"RedistDir", RRF_RT_REG_SZ, nullptr, dir, &sz) == ERROR_SUCCESS) {
        std::wstring d = dir;
        if (!d.empty() && d.back() != L'\\') d += L'\\';
        gGiMods[2].path = d + L"GameInputRedist.dll";
    }
}

template <int Id> bool hookMouse(void* fn) {
    return ll_shim::hook(fn, reinterpret_cast<void*>(&MouseHook<Id, MouseM>::fn), &MouseHook<Id, MouseM>::orig) == 0;
}

void tryModule(int i) {
    GiMod& m = gGiMods[i];
    if (m.hooked) return;
    if (m.path.empty()) { m.note = "no path"; return; }
    if (!m.mod) {
        if (GetFileAttributesW(m.path.c_str()) == INVALID_FILE_ATTRIBUTES) { m.note = "not installed"; return; }
        m.mod = LoadLibraryW(m.path.c_str());
        if (!m.mod) { m.note = "LoadLibrary failed " + std::to_string(GetLastError()); return; }
    }
    if (!m.input) {
        using InitFn = HRESULT(WINAPI*)(REFIID, void**);
        auto init = reinterpret_cast<InitFn>(GetProcAddress(m.mod, "GameInputInitialize"));
        if (!init) { m.note = "no GameInputInitialize export"; return; }
        HRESULT hr = init(gi::IID_IGameInput, reinterpret_cast<void**>(&m.input));
        if (FAILED(hr) || !m.input) {
            char b[80];
            snprintf(b, sizeof b, "GameInputInitialize(v2) hr 0x%08lX", long(hr));
            m.note = b;
            m.input = nullptr;
            return;
        }
    }
    gi::IGameInputReading* rd = nullptr;
    HRESULT hr = m.input->GetCurrentReading(gi::GameInputKindMouse, nullptr, &rd);
    if (FAILED(hr) || !rd) hr = m.input->GetCurrentReading(gi::GameInputKindKeyboard, nullptr, &rd);
    if (FAILED(hr) || !rd) {
        char b[80];
        snprintf(b, sizeof b, "no reading yet (hr 0x%08lX), retry next toggle", long(hr));
        m.note = b;
        return;
    }
    void** vt  = *reinterpret_cast<void***>(rd);
    int    idx = vindex(&gi::IGameInputReading::GetMouseState);
    bool   ok  = false;
    if (idx >= 0) {
        switch (i) {
        case 0: ok = hookMouse<0>(vt[idx]); break;
        case 1: ok = hookMouse<1>(vt[idx]); break;
        default: ok = hookMouse<2>(vt[idx]); break;
        }
    }
    rd->Release();
    m.hooked = ok;
    m.note   = ok ? "GetMouseState hooked (slot " + std::to_string(idx) + ")" : "GetMouseState hook failed (slot " + std::to_string(idx) + ")";
}

void tryGameMouse() {
    initGameMod();
    giPaths();
    for (int i = 0; i < 3; ++i) tryModule(i);
    gGiNote = "v2";
}
#else
void tryGameMouse() { gGiNote = "built without the GameInput v2 header"; }
#endif

void installEarly() { initGameMod(); }

void writeDiag() {
    char b[12000];
    snprintf(b, sizeof b,
             "Spatial GUI panel diag v0.13\n%s\nGetCursorPos calls (panel on, our window known): %ld, inside client: %ld, remapped: %ld\n"
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
        {
            std::lock_guard l(gScreenMutex);
            len += snprintf(b + len, sizeof b - len, "screens (auto panel %s):", gS.autoPanel ? "on" : "off");
            for (auto& t : gStackTops) len += snprintf(b + len, sizeof b - len, " [top %s%s]", t.name.c_str(), isPanelScreen(t.name) ? " -> panel" : "");
            len += snprintf(b + len, sizeof b - len, "\n  seen:");
            for (auto& n : gScreensSeen) len += snprintf(b + len, sizeof b - len, " %s%s", n.c_str(), isPanelScreen(n) ? "*" : "");
            len += snprintf(b + len, sizeof b - len, "\n");
        }
        len += snprintf(b + len, sizeof b - len, "UI pipelines with alpha fixed: %ld\n", uicapture::uiAlphaFixedCount());
        len += snprintf(b + len, sizeof b - len, "GUI magnification (java auto scale / bedrock gui scale): %.3f; drawn fraction %.3f x %.3f\n", gGuiMag, gDrawFrac[0], gDrawFrac[1]);
        spatialcam::appendDiag(b, sizeof b);
        len = strlen(b);
        len += snprintf(b + len, sizeof b - len, "GameInput: %s\n", gGiNote.c_str());
#if RF_HAVE_GAMEINPUT
        for (auto& m : gGiMods) len += snprintf(b + len, sizeof b - len, "  %-30s %s\n", m.label, m.note.c_str());
        len += snprintf(b + len, sizeof b - len,
                        "  GetMouseState calls %ld (from game %ld), with absolute position %ld, remapped %ld\n"
                        "  absolute space: %s (matches client %ld, screen %ld, mismatched %ld)\n"
                        "  last: buttons 0x%x positions 0x%x abs %lld,%lld -> %lld,%lld rel %lld,%lld  cursor screen %ld,%ld client %ld,%ld\n",
                        gMsCalls.load(), gMsGame.load(), gMsAbs.load(), gMsRemapped.load(), gMsSpace == 2 ? "client" : gMsSpace == 1 ? "screen" : "unknown",
                        gMatchClient, gMatchScreen, gMsMismatch.load(), gMsButtons, gMsPositions, gMsAbsX, gMsAbsY, gMsOutX, gMsOutY, gMsRelX, gMsRelY,
                        gMsCursor.x, gMsCursor.y, gMsClient.x, gMsClient.y);
        len += snprintf(b + len, sizeof b - len, "  distinct readings (oldest first): seq buttons positions in -> out\n");
        {
            unsigned end = gMsLogIdx.load();
            for (unsigned k = 0; k < 24; ++k) {
                auto& e = gMsLog[(end + k) % 24];
                if (!e.seq) continue;
                len += snprintf(b + len, sizeof b - len, "   %ld b%x p%x %lld,%lld -> %lld,%lld\n", e.seq, e.buttons, e.positions, e.inX, e.inY, e.outX, e.outY);
            }
        }
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
// The Java mod's config screen (SpatialGUIConfig), same groups and ranges. Saved to config/SpatialGUI.json.
void drawJavaConfig() {
    auto& c = spatialcam::cfg();
    if (!ImGui::TreeNode("Spatial GUI config (Java mod options)")) return;
    if (ImGui::TreeNode("General")) {
        ImGui::Checkbox("Enabled", &c.enabled);
        ImGui::Checkbox("First Person Mode (Inventory)", &c.fpInventory);
        ImGui::Checkbox("First Person Mode (Other)", &c.fpContainers);
        ImGui::Checkbox("Auto Scale by FOV", &c.autoScaleByFov);
        ImGui::Checkbox("Mirror Third Person", &c.mirrorThirdPerson);
        ImGui::Checkbox("Auto Calculate GUI Scale", &c.autoGuiScale);
        if (!c.autoGuiScale) ImGui::SliderInt("GUI Scale", &c.guiScale, 1, 6);
        ImGui::Checkbox("Hide Hotbar", &c.hideHotbar);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Screens")) {
        ImGui::Checkbox("Pause Screen", &c.pauseScreen);
        ImGui::Checkbox("All Screens", &c.allScreens);
        ImGui::Checkbox("Most Containers", &c.mostContainers);
        ImGui::Checkbox("Books", &c.books);
        ImGui::Checkbox("Crafting", &c.crafting);
        ImGui::Checkbox("Furnaces", &c.furnaces);
        ImGui::Checkbox("Anvils", &c.anvils);
        ImGui::Checkbox("Enchanting", &c.enchanting);
        ImGui::Checkbox("Beacons", &c.beacons);
        ImGui::Checkbox("Brewing", &c.brewing);
        ImGui::Checkbox("Villager Trading", &c.villagerTrading);
        ImGui::Checkbox("Chests", &c.chests);
        ImGui::Checkbox("Shulker Boxes", &c.shulkerBoxes);
        ImGui::Checkbox("Hoppers", &c.hoppers);
        ImGui::Checkbox("Dispensers", &c.dispensers);
        ImGui::Checkbox("Grindstone", &c.grindstone);
        ImGui::Checkbox("Smithing", &c.smithing);
        ImGui::Checkbox("Cartography", &c.cartography);
        ImGui::Checkbox("Loom", &c.loom);
        ImGui::Checkbox("Stonecutter", &c.stonecutter);
        ImGui::Checkbox("Lectern", &c.lectern);
        ImGui::Checkbox("Inventory", &c.inventory);
        ImGui::Checkbox("Creative Inventory", &c.creativeInventory);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Rendering")) {
        ImGui::SliderInt("Screen Alpha", &c.screenAlpha, 0, 255);
        ImGui::SliderInt("Render Scale (%)", &c.renderScalePct, 25, 200);
        ImGui::Checkbox("Use Linear Filtering", &c.linearFiltering);
        ImGui::Checkbox("Use Anisotropic Filtering", &c.anisotropicFiltering);
        ImGui::SliderFloat("Recipe Book Shrink Factor", &c.recipeBookShrink, 1.0f, 3.0f, "%.2f");
        ImGui::Checkbox("Hide Hands in First Person", &c.hideHandsFp);
        ImGui::Checkbox("Hide Shield in First Person", &c.hideShieldFp);
        ImGui::Checkbox("Swing Arm on First Person Click", &c.swingArmFp);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("TP Screen")) {
        ImGui::SliderFloat("Screen Distance", &c.screenDistance, 0.5f, 6.0f, "%.2f");
        ImGui::SliderFloat("Screen Side Offset", &c.screenSide, -4.0f, 4.0f, "%.2f");
        ImGui::SliderFloat("Screen Height Offset", &c.screenHeight, -4.0f, 4.0f, "%.2f");
        ImGui::SliderFloat("Screen Yaw Offset", &c.screenYaw, 0.0f, 360.0f, "%.1f");
        ImGui::SliderFloat("Screen Pitch Offset", &c.screenPitch, -90.0f, 90.0f, "%.1f");
        ImGui::SliderFloat("Screen Scale", &c.screenScale, 0.5f, 8.0f, "%.2f");
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("FP Screen")) {
        ImGui::SliderFloat("Screen Distance##fp", &c.fpDistance, 0.3f, 4.0f, "%.2f");
        ImGui::SliderFloat("Screen Side Offset##fp", &c.fpSide, -4.0f, 4.0f, "%.2f");
        ImGui::SliderFloat("Screen Height Offset##fp", &c.fpHeight, -4.0f, 4.0f, "%.2f");
        ImGui::SliderFloat("Screen Yaw Offset##fp", &c.fpYaw, 0.0f, 360.0f, "%.1f");
        ImGui::SliderFloat("Screen Pitch Offset##fp", &c.fpPitch, -90.0f, 90.0f, "%.1f");
        ImGui::SliderFloat("Screen Scale##fp", &c.fpScale, 0.3f, 6.0f, "%.2f");
        ImGui::Checkbox("Curved Screen", &c.curvedScreen);
        if (c.curvedScreen) ImGui::SliderInt("Curved Screen Arc (degrees)", &c.curvedArc, 0, 180);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("TP Camera")) {
        ImGui::SliderFloat("Distance", &c.camDistance, -4.0f, 4.0f, "%.2f");
        ImGui::SliderFloat("Horizontal Offset", &c.camSide, -4.0f, 4.0f, "%.2f");
        ImGui::SliderFloat("Vertical Offset", &c.camHeight, -4.0f, 4.0f, "%.2f");
        ImGui::SliderFloat("Target Pitch", &c.camPitch, -90.0f, 90.0f, "%.1f");
        ImGui::SliderInt("Transition Duration (ms)", &c.transitionMs, 0, 2000);
        ImGui::SliderInt("Transition Skip Percentage", &c.transitionSkipPct, 0, 100);
        ImGui::SliderFloat("Mouse Sensitivity Yaw", &c.tpSensYaw, 0.0f, 1.0f, "%.3f");
        ImGui::SliderFloat("Mouse Sensitivity Pitch", &c.tpSensPitch, 0.0f, 1.0f, "%.3f");
        ImGui::Checkbox("Disable Parallax", &c.tpParallaxOff);
        ImGui::Checkbox("Lerp X Rotation", &c.lerpXRot);
        ImGui::SliderFloat("Avatar Base Yaw Offset", &c.avatarBaseYaw, -180.0f, 180.0f, "%.1f");
        ImGui::SliderFloat("Avatar Body Rotation Offset", &c.avatarBodyRot, -180.0f, 180.0f, "%.1f");
        ImGui::SliderFloat("Wide Screen Player Shift (Bedrock)", &c.wideScreenShift, 0.0f, 2.0f, "%.2f");
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("FP Camera")) {
        ImGui::SliderFloat("Mouse Sensitivity Yaw##fp", &c.fpSensYaw, 0.0f, 1.0f, "%.3f");
        ImGui::SliderFloat("Mouse Sensitivity Pitch##fp", &c.fpSensPitch, 0.0f, 1.0f, "%.3f");
        ImGui::Checkbox("Disable Parallax##fp", &c.fpParallaxOff);
        ImGui::SliderInt("First Person Pitch Clamp", &c.fpPitchClamp, 0, 90);
        ImGui::Checkbox("Use Crosshair for First Person", &c.crosshairFp);
        ImGui::Checkbox("Keep First Person Camera Angle", &c.keepFpAngle);
        ImGui::Checkbox("Inherit Screen Origin on Swap", &c.inheritOrigin);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Animations")) {
        ImGui::Checkbox("Enable Fade Animation", &c.fade);
        ImGui::SliderInt("Fade Duration (ms)", &c.fadeMs, 0, 2000);
        ImGui::Checkbox("Enable Scale Animation", &c.scaleAnim);
        ImGui::SliderInt("Scale Duration (ms)", &c.openMs, 0, 2000);
        const char* eases[] = {"Back", "Cubic", "Elastic", "Bounce", "Expo", "Quad", "Quart"};
        int e = int(c.easing);
        if (ImGui::Combo("Scale Easing", &e, eases, 7)) c.easing = spatialcam::Easing(e);
        ImGui::SliderInt("Start Scale (%)", &c.startScalePct, 0, 100);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Auto FOV Tuning")) {
        ImGui::SliderFloat("Auto Scale Baseline FOV", &c.baselineFov, 30.0f, 150.0f, "%.1f");
        ImGui::SliderFloat("Auto Scale Screen Power", &c.screenPower, 0.0f, 3.0f, "%.2f");
        ImGui::SliderFloat("Auto Scale Side Offset Multiplier", &c.sideMult, -2.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Auto Scale Distance Multiplier", &c.distMult, -2.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Auto Scale Third Person Screen Multiplier", &c.tpScreenMult, 0.0f, 2.0f, "%.2f");
        ImGui::TreePop();
    }
    if (ImGui::Button("Save")) spatialcam::saveConfig();
    ImGui::SameLine();
    if (ImGui::Button("Reset to defaults")) spatialcam::resetConfig();
    ImGui::TreePop();
}

void drawOverlay() {
    // crosshair while Java crosshair mode drives the first person camera (Bedrock hides its own under screens)
    if (GetTickCount64() - gCrosshairFrame.load() > 200) return;
    auto*  dl = ImGui::GetForegroundDrawList();
    ImVec2 d  = ImGui::GetIO().DisplaySize;
    float  cx = d.x * 0.5f, cy = d.y * 0.5f, r = std::max(6.0f, d.y / 90.0f), w = std::max(2.0f, d.y / 540.0f);
    dl->AddLine(ImVec2(cx - r, cy), ImVec2(cx + r, cy), IM_COL32(0, 0, 0, 120), w + 2.0f);
    dl->AddLine(ImVec2(cx, cy - r), ImVec2(cx, cy + r), IM_COL32(0, 0, 0, 120), w + 2.0f);
    dl->AddLine(ImVec2(cx - r, cy), ImVec2(cx + r, cy), IM_COL32(235, 235, 235, 230), w);
    dl->AddLine(ImVec2(cx, cy - r), ImVec2(cx, cy + r), IM_COL32(235, 235, 235, 230), w);
}

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
        ImGui::Checkbox("Auto: panel for inventory / chest / crafting screens", &gS.autoPanel);
        ImGui::Checkbox("Java Spatial GUI look (third-person camera, world screen)", &gS.javaStyle);
        drawJavaConfig();
        ImGui::TextWrapped("M1 prototype: everything the UI draws (HUD included) goes onto the panel while it is on.");
    }
    ImGui::Unindent();
}

} // namespace rf::panel
