// Renderer Foundation - full-screen post effects (see PostFx.h, shaders/postfx.hlsl).
#include "PostFx.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <wrl/client.h>

#include <imgui.h>

#include "LLShim.h"
#include "PostFxShaders.h"

namespace rf::postfx {
namespace {
using Microsoft::WRL::ComPtr;

// Must match src/client/PostFx.h in Bedrock Forge (main branch).
struct BfPostFxV1 {
    uint32_t size, version, active, frame;
    float    bloom, bloomTiles, ca, caStyle, cas, motionBlur, yaw, pitch, fov;
    float    reserved[5];
};
using GetFn = BfPostFxV1 const* (*)();

GetFn bfGet() {
    static GetFn fn    = nullptr;
    static int   delay = 0;
    if (fn || delay-- > 0) return fn;
    delay = 240; // look for BedrockForgeCore.dll again in ~4 s
    if (HMODULE m = GetModuleHandleW(L"BedrockForgeCore.dll"))
        fn = reinterpret_cast<GetFn>(reinterpret_cast<void*>(GetProcAddress(m, "BF_GetPostFx")));
    return fn;
}

struct Ui {
    bool  enabled = true;
    bool  custom  = false;  // use the sliders instead of the pack's postfx.json
    float bloom = 0.12f, tiles = 6.f, ca = 0.0f, caStyle = 0.f, cas = 0.0f, motionBlur = 0.0f;
} gUi;

struct Params {
    float bloom, tiles, ca, caStyle, cas, motionBlur, yaw, pitch, fov;
    bool  any() const { return bloom > 0 || ca > 0 || cas > 0 || motionBlur > 0; }
};

constexpr int kTiles = 6;
constexpr DXGI_FORMAT kBloomFmt = DXGI_FORMAT_R16G16B16A16_FLOAT;

struct Tex {
    ComPtr<ID3D12Resource> res;
    UINT                   w = 0, h = 0;
};

ComPtr<ID3D12Device>         gDevice;
ComPtr<ID3D12RootSignature>  gRoot;
ComPtr<ID3D12PipelineState>  gDownFirst, gDown, gBlur, gComposite;
DXGI_FORMAT                  gCompositeFmt = DXGI_FORMAT_UNKNOWN;
ComPtr<ID3D12DescriptorHeap> gSrvHeap, gRtvHeap;
UINT                         gSrvInc = 0, gRtvInc = 0;
Tex                          gScene, gL[kTiles], gT[kTiles];
DXGI_FORMAT                  gSceneFmt = DXGI_FORMAT_UNKNOWN;
ComPtr<ID3D12Fence>          gFence;
UINT64                       gFenceValue = 0;
HANDLE                       gFenceEvent = nullptr;
bool                         gRecorded   = false; // this frame's list carries post work (signal the fence after it)
bool                         gBroken     = false; // creation failed: stay off for this run
std::string                  gStatus     = "waiting for Bedrock Forge";
float                        gPrevYaw = 0, gPrevPitch = 0;
bool                         gHavePrev = false;
uint32_t                     gFrame    = 0;

constexpr UINT kSrvSlots = 24; // 0 scene, 1 scene (t1), 2..7 L0..L5, 8..13 T0..T5, rest null (tables are 8 wide)
UINT slotL(int k) { return 2u + UINT(k); }
UINT slotT(int k) { return 8u + UINT(k); }

D3D12_CPU_DESCRIPTOR_HANDLE srvCpu(UINT i) { auto h = gSrvHeap->GetCPUDescriptorHandleForHeapStart(); h.ptr += SIZE_T(i) * gSrvInc; return h; }
D3D12_GPU_DESCRIPTOR_HANDLE srvGpu(UINT i) { auto h = gSrvHeap->GetGPUDescriptorHandleForHeapStart(); h.ptr += UINT64(i) * gSrvInc; return h; }
D3D12_CPU_DESCRIPTOR_HANDLE rtvCpu(UINT i) { auto h = gRtvHeap->GetCPUDescriptorHandleForHeapStart(); h.ptr += SIZE_T(i) * gRtvInc; return h; }

void fail(std::string const& what, HRESULT hr) {
    char b[16];
    snprintf(b, sizeof b, "%08X", uint32_t(hr));
    gStatus = "off: " + what + " failed (hr=0x" + b + ")";
    gBroken = true;
    ll_shim::error("[RF] post effects " + gStatus);
}

bool createPso(D3D12_SHADER_BYTECODE ps, DXGI_FORMAT fmt, ComPtr<ID3D12PipelineState>& out) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = gRoot.Get();
    d.VS             = {shaders::VSMain, sizeof shaders::VSMain};
    d.PS             = ps;
    for (auto& rt : d.BlendState.RenderTarget) rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.SampleMask                       = UINT_MAX;
    d.RasterizerState.FillMode         = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode         = D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable  = TRUE;
    d.DepthStencilState.DepthEnable    = FALSE;
    d.DepthStencilState.StencilEnable  = FALSE;
    d.PrimitiveTopologyType            = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets                 = 1;
    d.RTVFormats[0]                    = fmt;
    d.SampleDesc.Count                 = 1;
    HRESULT hr = gDevice->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&out));
    if (FAILED(hr)) { fail("CreateGraphicsPipelineState", hr); return false; }
    return true;
}

bool createStatic(ID3D12Device* device) {
    gDevice = device;
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0, 0, 0};
    D3D12_ROOT_PARAMETER   params[2]{};
    params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants                = {0, 0, 24};
    params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable          = {1, &range};
    params[1].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC smp{};
    smp.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    smp.AddressU = smp.AddressV = smp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    smp.MaxLOD           = D3D12_FLOAT32_MAX;
    smp.ShaderRegister   = 0;
    smp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rs{2, params, 1, &smp, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob, err;
    HRESULT          hr = D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    if (FAILED(hr)) { fail("D3D12SerializeRootSignature", hr); return false; }
    hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&gRoot));
    if (FAILED(hr)) { fail("CreateRootSignature", hr); return false; }
    if (!createPso({shaders::PSDownFirst, sizeof shaders::PSDownFirst}, kBloomFmt, gDownFirst)) return false;
    if (!createPso({shaders::PSDown, sizeof shaders::PSDown}, kBloomFmt, gDown)) return false;
    if (!createPso({shaders::PSBlur, sizeof shaders::PSBlur}, kBloomFmt, gBlur)) return false;
    D3D12_DESCRIPTOR_HEAP_DESC srv{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kSrvSlots, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    D3D12_DESCRIPTOR_HEAP_DESC rtv{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2 * kTiles, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    if (FAILED(hr = device->CreateDescriptorHeap(&srv, IID_PPV_ARGS(&gSrvHeap)))) { fail("CreateDescriptorHeap(SRV)", hr); return false; }
    if (FAILED(hr = device->CreateDescriptorHeap(&rtv, IID_PPV_ARGS(&gRtvHeap)))) { fail("CreateDescriptorHeap(RTV)", hr); return false; }
    gSrvInc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    gRtvInc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    if (FAILED(hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gFence)))) { fail("CreateFence", hr); return false; }
    gFenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    // null SRVs everywhere first, so every slot an 8-wide table can reach is valid
    D3D12_SHADER_RESOURCE_VIEW_DESC nul{};
    nul.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    nul.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    nul.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nul.Texture2D.MipLevels     = 1;
    for (UINT i = 0; i < kSrvSlots; ++i) device->CreateShaderResourceView(nullptr, &nul, srvCpu(i));
    return true;
}

bool makeTex(Tex& t, UINT w, UINT h, DXGI_FORMAT fmt, bool rt, UINT srvSlot, int rtvSlot) {
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
    HRESULT hr = gDevice->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                  nullptr, IID_PPV_ARGS(&t.res));
    if (FAILED(hr)) { fail("CreateCommittedResource", hr); return false; }
    t.w = w;
    t.h = h;
    D3D12_SHADER_RESOURCE_VIEW_DESC s{};
    s.Format                  = fmt;
    s.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    s.Texture2D.MipLevels     = 1;
    gDevice->CreateShaderResourceView(t.res.Get(), &s, srvCpu(srvSlot));
    if (rtvSlot >= 0) gDevice->CreateRenderTargetView(t.res.Get(), nullptr, rtvCpu(UINT(rtvSlot)));
    return true;
}

void waitIdle() {
    if (gFence && gFence->GetCompletedValue() < gFenceValue) {
        gFence->SetEventOnCompletion(gFenceValue, gFenceEvent);
        WaitForSingleObject(gFenceEvent, 2000);
    }
}

bool ensureSized(D3D12_RESOURCE_DESC const& bb) {
    if (gScene.res && gScene.w == UINT(bb.Width) && gScene.h == bb.Height && gSceneFmt == bb.Format) return true;
    waitIdle();
    gScene = {};
    for (auto& t : gL) t = {};
    for (auto& t : gT) t = {};
    UINT w = UINT(bb.Width), h = bb.Height;
    gSceneFmt = bb.Format;
    if (!makeTex(gScene, w, h, bb.Format, false, 0, -1)) return false;
    // t1 = scene too (unused today)
    D3D12_SHADER_RESOURCE_VIEW_DESC s{};
    s.Format = bb.Format; s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; s.Texture2D.MipLevels = 1;
    gDevice->CreateShaderResourceView(gScene.res.Get(), &s, srvCpu(1));
    for (int k = 0; k < kTiles; ++k) {
        UINT tw = std::max(1u, w >> (k + 1)), th = std::max(1u, h >> (k + 1));
        if (!makeTex(gL[k], tw, th, kBloomFmt, true, slotL(k), k)) return false;
        if (!makeTex(gT[k], tw, th, kBloomFmt, true, slotT(k), kTiles + k)) return false;
    }
    if (gCompositeFmt != bb.Format) {
        gComposite.Reset();
        if (!createPso({shaders::PSComposite, sizeof shaders::PSComposite}, bb.Format, gComposite)) return false;
        gCompositeFmt = bb.Format;
    }
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

struct Consts { float v[24]; };

void draw(ID3D12GraphicsCommandList* l, ID3D12PipelineState* pso, D3D12_CPU_DESCRIPTOR_HANDLE rtv, UINT w, UINT h,
          UINT srcSlot, Consts const& c) {
    D3D12_VIEWPORT vp{0, 0, float(w), float(h), 0, 1};
    D3D12_RECT     sc{0, 0, LONG(w), LONG(h)};
    l->SetPipelineState(pso);
    l->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    l->RSSetViewports(1, &vp);
    l->RSSetScissorRects(1, &sc);
    l->SetGraphicsRoot32BitConstants(0, 24, c.v, 0);
    l->SetGraphicsRootDescriptorTable(1, srvGpu(srcSlot));
    l->DrawInstanced(3, 1, 0, 0);
}

// camera-to-world rotation from Bedrock yaw / pitch (degrees); rows of R(prev)^T * R(cur)
void relRotation(float yaw0, float pitch0, float yaw1, float pitch1, float out[9]) {
    auto rot = [](float yaw, float pitch, float m[9]) {
        float a = yaw * 0.01745329f, b = pitch * 0.01745329f;
        float ca = std::cos(a), sa = std::sin(a), cb = std::cos(b), sb = std::sin(b);
        // RotY(a) * RotX(b)
        float r[9] = {ca, sa * sb, sa * cb, 0, cb, -sb, -sa, ca * sb, ca * cb};
        for (int i = 0; i < 9; ++i) m[i] = r[i];
    };
    float p[9], c[9];
    rot(yaw0, pitch0, p);
    rot(yaw1, pitch1, c);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) out[i * 3 + j] = p[0 * 3 + i] * c[0 * 3 + j] + p[1 * 3 + i] * c[1 * 3 + j] + p[2 * 3 + i] * c[2 * 3 + j];
}

bool linearizable(DXGI_FORMAT f) {
    return f == DXGI_FORMAT_R8G8B8A8_UNORM || f == DXGI_FORMAT_B8G8R8A8_UNORM || f == DXGI_FORMAT_R10G10B10A2_UNORM
        || f == DXGI_FORMAT_B8G8R8X8_UNORM;
}

BfPostFxV1 gLast{};
bool       gLinked = false;
} // namespace

bool record(ID3D12Device* device, ID3D12GraphicsCommandList* list, ID3D12Resource* bb, D3D12_CPU_DESCRIPTOR_HANDLE bbRtv) {
    gRecorded = false;
    if (gBroken || !gUi.enabled) return false;
    GetFn fn = bfGet();
    BfPostFxV1 const* bf = fn ? fn() : nullptr;
    gLinked = bf && bf->version >= 1;
    if (gLinked) gLast = *bf;
    if (!gLinked) { gStatus = "waiting for Bedrock Forge"; gHavePrev = false; return false; }
    if (!bf->active && !gUi.custom) { gStatus = "idle (no world on screen or the pack has no postfx.json)"; gHavePrev = false; return false; }
    if (gUi.custom && !bf->active && bf->frame == 0) { gHavePrev = false; return false; }
    Params p = gUi.custom ? Params{gUi.bloom, gUi.tiles, gUi.ca, gUi.caStyle, gUi.cas, gUi.motionBlur, bf->yaw, bf->pitch, bf->fov}
                          : Params{bf->bloom, bf->bloomTiles, bf->ca, bf->caStyle, bf->cas, bf->motionBlur, bf->yaw, bf->pitch, bf->fov};
    if (!p.any()) { gStatus = "idle (all effects 0)"; return false; }
    if (!gRoot && !createStatic(device)) return false;
    D3D12_RESOURCE_DESC bd = bb->GetDesc();
    if (!ensureSized(bd)) return false;
    ++gFrame;

    barrier(list, bb, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    barrier(list, gScene.res.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyResource(gScene.res.Get(), bb);
    barrier(list, gScene.res.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    barrier(list, bb, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    ID3D12DescriptorHeap* heaps[] = {gSrvHeap.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootSignature(gRoot.Get());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    float lin = linearizable(bd.Format) ? 1.0f : 0.0f;

    int tiles = std::clamp(int(p.tiles + 0.5f), 1, kTiles);
    if (p.bloom > 0) {
        for (int k = 0; k < tiles; ++k) {
            Tex const& src = k == 0 ? gScene : gL[k - 1];
            Consts c{};
            c.v[0] = 1.0f / float(src.w);
            c.v[1] = 1.0f / float(src.h);
            c.v[11] = lin;
            barrier(list, gL[k].res.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
            draw(list, k == 0 ? gDownFirst.Get() : gDown.Get(), rtvCpu(UINT(k)), gL[k].w, gL[k].h, k == 0 ? 0u : slotL(k - 1), c);
            barrier(list, gL[k].res.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            Consts h{};
            h.v[0] = 1.0f / float(gL[k].w);
            barrier(list, gT[k].res.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
            draw(list, gBlur.Get(), rtvCpu(UINT(kTiles + k)), gT[k].w, gT[k].h, slotL(k), h);
            barrier(list, gT[k].res.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            Consts v{};
            v.v[1] = 1.0f / float(gL[k].h);
            barrier(list, gL[k].res.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
            draw(list, gBlur.Get(), rtvCpu(UINT(k)), gL[k].w, gL[k].h, slotT(k), v);
            barrier(list, gL[k].res.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
    }

    Consts c{};
    c.v[0] = 1.0f / float(bd.Width);
    c.v[1] = 1.0f / float(bd.Height);
    c.v[2] = p.bloom;
    c.v[3] = float(tiles);
    c.v[4] = p.ca;
    c.v[5] = p.caStyle;
    c.v[6] = p.cas;
    c.v[7] = p.motionBlur;
    c.v[8] = float(gFrame % 1024);
    c.v[9] = float(bd.Width) / float(bd.Height);
    c.v[10] = std::tan(std::clamp(p.fov, 10.0f, 170.0f) * 0.5f * 0.01745329f);
    c.v[11] = lin;
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    if (p.motionBlur > 0 && gHavePrev) {
        float dy = std::fabs(std::remainder(p.yaw - gPrevYaw, 360.0f)), dp = std::fabs(p.pitch - gPrevPitch);
        if (dy < 45.0f && dp < 45.0f) relRotation(gPrevYaw, gPrevPitch, p.yaw, p.pitch, m);
    }
    for (int r = 0; r < 3; ++r) {
        c.v[12 + r * 4 + 0] = m[r * 3 + 0];
        c.v[12 + r * 4 + 1] = m[r * 3 + 1];
        c.v[12 + r * 4 + 2] = m[r * 3 + 2];
    }
    gPrevYaw   = p.yaw;
    gPrevPitch = p.pitch;
    gHavePrev  = true;
    draw(list, gComposite.Get(), bbRtv, UINT(bd.Width), bd.Height, 0, c);

    char b[160];
    snprintf(b, sizeof b, "on: bloom %.2f x%d  CA %.4f  CAS %.2f  motion blur %.2f  (%ux%u)", p.bloom, tiles, p.ca, p.cas,
             p.motionBlur, UINT(bd.Width), bd.Height);
    gStatus   = b;
    gRecorded = true;
    return true;
}

void submitted(ID3D12CommandQueue* queue) {
    if (!gRecorded || !gFence || !queue) return;
    queue->Signal(gFence.Get(), ++gFenceValue);
}

void release() {
    waitIdle();
    gScene = {};
    for (auto& t : gL) t = {};
    for (auto& t : gT) t = {};
    gHavePrev = false;
}

void drawUi() {
    if (!ImGui::CollapsingHeader("Post effects", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::Indent();
    ImGui::Checkbox("Enabled", &gUi.enabled);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", gStatus.c_str());
    if (gLinked)
        ImGui::Text("Shaderpack asks for: bloom %.2f  CA %.4f (%s)  CAS %.2f  motion blur %.2f", gLast.bloom, gLast.ca,
                    gLast.caStyle < 0.5f ? "Cursed Fog" : "Insanity", gLast.cas, gLast.motionBlur);
    ImGui::Checkbox("Override with my own values", &gUi.custom);
    if (gUi.custom) {
        ImGui::SliderFloat("Bloom", &gUi.bloom, 0.0f, 0.5f, "%.2f");
        ImGui::SliderFloat("Bloom tiles", &gUi.tiles, 1.0f, 6.0f, "%.0f");
        ImGui::SliderFloat("Chromatic aberration", &gUi.ca, 0.0f, 0.01f, "%.4f");
        int style = int(gUi.caStyle);
        if (ImGui::Combo("CA style", &style, "Cursed Fog\0Insanity\0")) gUi.caStyle = float(style);
        ImGui::SliderFloat("Sharpen (CAS)", &gUi.cas, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Motion blur", &gUi.motionBlur, 0.0f, 2.0f, "%.2f");
    }
    ImGui::TextDisabled("Motion blur follows camera turning only (Bedrock gives no depth buffer at this point).");
    ImGui::Unindent();
}

} // namespace rf::postfx
