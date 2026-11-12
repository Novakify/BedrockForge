// Renderer Foundation - IrisBridge runtime M1 (see IrisBridge.h). Original code for Renderer Foundation (Novak).
// IrisShaders.h is shaders/iris_import.hlsl compiled by DXC 1.8 (signed): dxc -T vs_6_0 -E VSImport / -T ps_6_0 -E PSImport.
#include "IrisBridge.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <cctype>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <imgui.h>

#include "D3D12Api.h"
#include "IrisBundle.h"
#include "IrisShaders.h"
#include "LyricsCamera.h"
#include "WorldFx.h"

namespace rf::iris {
namespace {
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

constexpr UINT kSlots     = 48;   // t0..t47 / s0..s47: tools/IrisBridge fixed slots (colortex 0-15, depth 16-18, shadow 19-22, noise 23, atlas 24-27, custom 32+)
constexpr UINT kRing      = 3;    // frames in flight
constexpr UINT kSrvPerFrame = 4096;
constexpr UINT64 kCbPerFrame = 4u << 20;
constexpr int  kColortex  = 16;

// ---------------------------------------------------------------- small GL-style math (column-major, m[c*4+r])
using Mat = std::array<float, 16>;
Mat ident() { Mat m{}; m[0] = m[5] = m[10] = m[15] = 1; return m; }
Mat mul(Mat const& a, Mat const& b) {
    Mat r{};
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + rr] * b[c * 4 + k];
            r[c * 4 + rr] = s;
        }
    return r;
}
Mat inverse(Mat const& m) {
    Mat inv{};
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1e-20f) return ident();
    for (float& v : inv) v /= det;
    return inv;
}
std::array<float, 3> xform(Mat const& m, float x, float y, float z, float w) {
    return {m[0] * x + m[4] * y + m[8] * z + m[12] * w, m[1] * x + m[5] * y + m[9] * z + m[13] * w, m[2] * x + m[6] * y + m[10] * z + m[14] * w};
}
std::array<float, 3> norm3(std::array<float, 3> v) {
    float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    return l > 1e-12f ? std::array<float, 3>{v[0] / l, v[1] / l, v[2] / l} : v;
}
std::array<float, 3> cross3(std::array<float, 3> a, std::array<float, 3> b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
/// GL view matrix (rotation only - Iris "player space" is world minus cameraPosition): rows = right, up, -forward.
Mat viewFromBasis(std::array<float, 3> f, std::array<float, 3> up) {
    f = norm3(f);
    auto r = norm3(cross3(f, up));   // right-handed: forward x up = right
    auto u = cross3(r, f);
    Mat m = ident();
    m[0] = r[0], m[4] = r[1], m[8] = r[2];
    m[1] = u[0], m[5] = u[1], m[9] = u[2];
    m[2] = -f[0], m[6] = -f[1], m[10] = -f[2];
    return m;
}
/// GL perspective, infinite far (Bedrock's depth is 1 - near / z: window depth then matches GL's exactly), with Y
/// flipped: every IrisBridge buffer is stored top-down (D3D), so uv (0,0) = screen top-left; flipping Y in the
/// projection keeps the pack's uv <-> NDC <-> view reconstruction consistent with that.
Mat projInfinite(float sx, float sy, float n) {
    Mat m{};
    m[0] = sx; m[5] = -sy; m[10] = -1.0f; m[11] = -1.0f; m[14] = -2.0f * n;
    return m;
}
Mat ortho(float l, float r, float b, float t, float n, float f) {
    Mat m = ident();
    m[0] = 2 / (r - l); m[5] = 2 / (t - b); m[10] = -2 / (f - n);
    m[12] = -(r + l) / (r - l); m[13] = -(t + b) / (t - b); m[14] = -(f + n) / (f - n);
    return m;
}

// ---------------------------------------------------------------- formats
DXGI_FORMAT dxgiOf(std::string f) {
    static std::map<std::string, DXGI_FORMAT> const t = {
        {"R8", DXGI_FORMAT_R8_UNORM}, {"RG8", DXGI_FORMAT_R8G8_UNORM}, {"RGB8", DXGI_FORMAT_R8G8B8A8_UNORM}, {"RGBA8", DXGI_FORMAT_R8G8B8A8_UNORM},
        {"R8_SNORM", DXGI_FORMAT_R8_SNORM}, {"RG8_SNORM", DXGI_FORMAT_R8G8_SNORM}, {"RGB8_SNORM", DXGI_FORMAT_R8G8B8A8_SNORM},
        {"RGBA8_SNORM", DXGI_FORMAT_R8G8B8A8_SNORM}, {"R16", DXGI_FORMAT_R16_UNORM}, {"RG16", DXGI_FORMAT_R16G16_UNORM},
        {"RGB16", DXGI_FORMAT_R16G16B16A16_UNORM}, {"RGBA16", DXGI_FORMAT_R16G16B16A16_UNORM}, {"R16_SNORM", DXGI_FORMAT_R16_SNORM},
        {"RG16_SNORM", DXGI_FORMAT_R16G16_SNORM}, {"RGB16_SNORM", DXGI_FORMAT_R16G16B16A16_SNORM}, {"RGBA16_SNORM", DXGI_FORMAT_R16G16B16A16_SNORM},
        {"R16F", DXGI_FORMAT_R16_FLOAT}, {"RG16F", DXGI_FORMAT_R16G16_FLOAT}, {"RGB16F", DXGI_FORMAT_R16G16B16A16_FLOAT},
        {"RGBA16F", DXGI_FORMAT_R16G16B16A16_FLOAT}, {"R32F", DXGI_FORMAT_R32_FLOAT}, {"RG32F", DXGI_FORMAT_R32G32_FLOAT},
        {"RGB32F", DXGI_FORMAT_R32G32B32A32_FLOAT}, {"RGBA32F", DXGI_FORMAT_R32G32B32A32_FLOAT}, {"R11F_G11F_B10F", DXGI_FORMAT_R11G11B10_FLOAT},
        {"RGB10_A2", DXGI_FORMAT_R10G10B10A2_UNORM}, {"RGB9_E5", DXGI_FORMAT_R16G16B16A16_FLOAT}, {"RGB5_A1", DXGI_FORMAT_R8G8B8A8_UNORM},
        {"R3_G3_B2", DXGI_FORMAT_R8G8B8A8_UNORM}, {"R32UI", DXGI_FORMAT_R32_UINT}, {"R32I", DXGI_FORMAT_R32_SINT}, {"R16UI", DXGI_FORMAT_R16_UINT},
        {"R8UI", DXGI_FORMAT_R8_UINT}, {"RGBA8UI", DXGI_FORMAT_R8G8B8A8_UINT}, {"RGBA16UI", DXGI_FORMAT_R16G16B16A16_UINT},
        {"RGBA32UI", DXGI_FORMAT_R32G32B32A32_UINT}};
    f.erase(std::remove_if(f.begin(), f.end(), [](char c) { return c == ' '; }), f.end());
    auto it = t.find(f);
    return it == t.end() ? DXGI_FORMAT_R8G8B8A8_UNORM : it->second; // Iris default: RGBA8
}
DXGI_FORMAT typedOf(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return f;
    }
}

// ---------------------------------------------------------------- state
struct Settings {
    bool  on = false;
    int   pack = 0;           // index into gPacks
    int   world = 0;          // 0 overworld, 1 nether, 2 end
    float timeOfDay = 1000.0f; // ticks (Bedrock's real time is not bridged yet)
    bool  cycle = false;      // let the time run (20 ticks / s)
    float farBlocks = 256.0f;       // render distance in blocks (Iris "far")
    bool  decodeGamma = true; // the game's colour is display gamma; packs expect linear light in colortex0
    float exposure = 1.0f;
} gSet;
std::mutex                    gMutex;   // everything below
std::vector<std::string>      gPacks;   // folders under <mod>/irisbridge with a runtime.txt
fs::path                      gRoot;
std::string                   gStatus = "off";
std::atomic_bool              gOn{false};

struct PassGpu {
    Pass const*                 pass = nullptr;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pso;
    std::vector<int>            outs;      // colortex per SV_Target (-1 = the game's target: final)
    bool                        usable = false;
    std::string                 error;
};
struct Tex {
    ComPtr<ID3D12Resource> r;
    D3D12_RESOURCE_STATES  state = D3D12_RESOURCE_STATE_COMMON;
    DXGI_FORMAT            fmt = DXGI_FORMAT_UNKNOWN;
};
struct Gpu {
    ComPtr<ID3D12Device>         dev;
    std::unique_ptr<Bundle>      bundle;
    std::string                  loadedName, loadedWorld;
    std::vector<PassGpu>         passes;    // deferred*, composite*, final of the chosen world, in order
    std::map<std::string, std::vector<uint8_t>> blobs;
    ComPtr<ID3D12RootSignature>  importRoot;
    ComPtr<ID3D12PipelineState>  importPso;
    ComPtr<ID3D12DescriptorHeap> srvHeap;   // shader visible, kRing * kSrvPerFrame
    ComPtr<ID3D12DescriptorHeap> cpuHeap;   // staging SRVs (not shader visible)
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    UINT                         srvInc = 0, rtvInc = 0;
    ComPtr<ID3D12Resource>       cb[kRing];
    uint8_t*                     cbMap[kRing]{};
    ComPtr<ID3D12Resource>       vb;
    UINT                         w = 0, h = 0;
    DXGI_FORMAT                  sceneFmt = DXGI_FORMAT_UNKNOWN;
    std::array<Tex, kColortex>   ct[2];     // [0] = current (read), [1] = the one a pass writes; swapped after it
    std::array<bool, kColortex>  used{};
    Tex                          scene, depth, shadowDepth, shadowColor, noise, white;
    std::vector<uint8_t>         noisePx;
    bool                         noiseUp = false;
    uint64_t                     frame = 0;
    // CPU staging slots: per colortex [0]/[1], depth, shadow depth, shadow colour, noise, white, null, scene, game rt
    UINT cpuCt(int i, int k) const { return UINT(i * 2 + k); }
    static constexpr UINT kCpuDepth = 32, kCpuShadowDepth = 33, kCpuShadowColor = 34, kCpuNoise = 35, kCpuWhite = 36,
                          kCpuNull = 37, kCpuScene = 38, kCpuCount = 40;
    static constexpr UINT kRtvGame = 32, kRtvCount = 34;
} gGpu;

// per-frame uniform state
struct FrameU {
    std::unordered_map<std::string, std::array<float, 16>> v; // floats, ints written as float then converted by type
    Mat    prevView = ident(), prevProj = ident();
    double prevCam[3]{};
    bool   havePrev = false;
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now(), last = t0;
    double timeCounter = 0;
    int    frameCounter = 0;
} gU;

void setStatus(std::string s) { gStatus = std::move(s); }

std::vector<uint8_t> readFile(fs::path const& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void scanPacks() {
    gPacks.clear();
    std::error_code ec;
    for (auto const& e : fs::directory_iterator(gRoot, ec))
        if (e.is_directory() && fs::exists(e.path() / "runtime.txt")) gPacks.push_back(e.path().filename().string());
    std::sort(gPacks.begin(), gPacks.end());
}

char const* worldName(int w) { return w == 1 ? "world-1" : w == 2 ? "world1" : "world0"; }

void barrier(ID3D12GraphicsCommandList* l, Tex& t, D3D12_RESOURCE_STATES to) {
    if (!t.r || t.state == to) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = t.r.Get(); b.Transition.StateBefore = t.state; b.Transition.StateAfter = to;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    l->ResourceBarrier(1, &b);
    t.state = to;
}
void barrierRaw(ID3D12GraphicsCommandList* l, ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    if (from == to) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r; b.Transition.StateBefore = from; b.Transition.StateAfter = to;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    l->ResourceBarrier(1, &b);
}

D3D12_CPU_DESCRIPTOR_HANDLE cpuAt(UINT i) {
    auto h = gGpu.cpuHeap->GetCPUDescriptorHandleForHeapStart(); h.ptr += size_t(i) * gGpu.srvInc; return h;
}
D3D12_CPU_DESCRIPTOR_HANDLE rtvAt(UINT i) {
    auto h = gGpu.rtvHeap->GetCPUDescriptorHandleForHeapStart(); h.ptr += size_t(i) * gGpu.rtvInc; return h;
}

bool makeTex(Tex& t, UINT w, UINT h, DXGI_FORMAT fmt, bool rt, D3D12_RESOURCE_STATES state, D3D12_CLEAR_VALUE const* cv = nullptr) {
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC   d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = w; d.Height = h; d.DepthOrArraySize = 1; d.MipLevels = 1;
    d.Format = fmt; d.SampleDesc.Count = 1; d.Flags = rt ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET : D3D12_RESOURCE_FLAG_NONE;
    t.r.Reset();
    if (FAILED(gGpu.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, state, cv, IID_PPV_ARGS(&t.r)))) return false;
    t.state = state; t.fmt = fmt;
    return true;
}
void srv(ID3D12Resource* r, DXGI_FORMAT f, UINT slot) {
    D3D12_SHADER_RESOURCE_VIEW_DESC s{};
    s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    s.Format = f; s.Texture2D.MipLevels = 1;
    gGpu.dev->CreateShaderResourceView(r, &s, cpuAt(slot));
}

// ---------------------------------------------------------------- pipelines
ComPtr<ID3D12RootSignature> makeRoot(std::vector<Resource> const& res, std::string& err) {
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, kSlots, 0, 0, 0};
    D3D12_ROOT_PARAMETER p[3]{};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; p[0].Descriptor = {0, 0}; p[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; p[1].Descriptor = {0, 0}; p[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    p[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[2].DescriptorTable = {1, &range}; p[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    std::array<bool, kSlots> cmp{};
    for (auto const& r : res) if (r.kind == "samplercmp" && r.slot < kSlots) cmp[r.slot] = true;
    D3D12_STATIC_SAMPLER_DESC ss[kSlots]{};
    for (UINT i = 0; i < kSlots; ++i) {
        auto& s = ss[i];
        bool const point = (i >= 16 && i <= 22); // depth + shadow maps: no filtering (Iris shadowtexNearest default for depth)
        s.Filter = cmp[i] ? D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT : point ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU = s.AddressV = s.AddressW = (i == 23) ? D3D12_TEXTURE_ADDRESS_MODE_WRAP : D3D12_TEXTURE_ADDRESS_MODE_CLAMP; // noisetex repeats
        s.ComparisonFunc = cmp[i] ? D3D12_COMPARISON_FUNC_LESS_EQUAL : D3D12_COMPARISON_FUNC_NEVER;
        s.MaxLOD = D3D12_FLOAT32_MAX; s.ShaderRegister = i; s.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        s.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    }
    D3D12_ROOT_SIGNATURE_DESC rs{3, p, kSlots, ss, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    ComPtr<ID3DBlob> blob, e;
    if (FAILED(rf::d3d12api::SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &e))) {
        err = e ? std::string(static_cast<char const*>(e->GetBufferPointer()), e->GetBufferSize()) : "root signature";
        return nullptr;
    }
    ComPtr<ID3D12RootSignature> root;
    if (FAILED(gGpu.dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)))) { err = "CreateRootSignature"; return nullptr; }
    return root;
}

// The vertex buffer: 6 vertices (two triangles) of a full-screen quad, every attribute IrisBridge pins
// (tools/IrisBridge/translate.py: Position 16, Color 17, Normal 18, UV0 19, UV1 20, UV2 21) at 16 bytes each.
struct QuadVert { float pos[4], col[4], nrm[4], uv0[4], uv1[4], uv2[4], zero[4]; };
UINT attrOffset(unsigned texcoord) {
    switch (texcoord) {
    case 16: return offsetof(QuadVert, pos); case 17: return offsetof(QuadVert, col); case 18: return offsetof(QuadVert, nrm);
    case 19: return offsetof(QuadVert, uv0); case 20: return offsetof(QuadVert, uv1); case 21: return offsetof(QuadVert, uv2);
    default: return offsetof(QuadVert, zero);
    }
}
DXGI_FORMAT attrFormat(VsInput const& in) {
    size_t n = 0;
    for (char c : in.mask) if (c == 'x' || c == 'y' || c == 'z' || c == 'w') ++n;
    n = std::max<size_t>(n, 1);
    bool const i = in.fmt == "int", u = in.fmt == "uint";
    static DXGI_FORMAT const F[4] = {DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32G32B32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT};
    static DXGI_FORMAT const I[4] = {DXGI_FORMAT_R32_SINT, DXGI_FORMAT_R32G32_SINT, DXGI_FORMAT_R32G32B32_SINT, DXGI_FORMAT_R32G32B32A32_SINT};
    static DXGI_FORMAT const U[4] = {DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32G32_UINT, DXGI_FORMAT_R32G32B32_UINT, DXGI_FORMAT_R32G32B32A32_UINT};
    return (i ? I : u ? U : F)[n - 1];
}

int colortexOf(std::string const& name) {
    static std::map<std::string, int> const alias = {{"gcolor", 0}, {"gdepth", 1}, {"gnormal", 2}, {"composite", 3}, {"gaux1", 4},
                                                     {"gaux2", 5}, {"gaux3", 6}, {"gaux4", 7}};
    if (name.rfind("colortex", 0) == 0) { int i = std::atoi(name.c_str() + 8); return i >= 0 && i < kColortex ? i : -1; }
    auto it = alias.find(name);
    return it == alias.end() ? -1 : it->second;
}

bool buildPass(PassGpu& pg, DXGI_FORMAT gameFmt) {
    Pass const& p = *pg.pass;
    auto vs = p.stages.find("vert"), ps = p.stages.find("frag");
    if (vs == p.stages.end() || ps == p.stages.end()) { pg.error = "needs a vertex + fragment stage"; return false; }
    for (auto const* st : {&vs->second, &ps->second})
        for (auto const& r : st->resources)
            if (r.kind == "UAV") { pg.error = "uses images / storage buffers (not in M1)"; return false; }
    std::vector<Resource> all = vs->second.resources;
    all.insert(all.end(), ps->second.resources.begin(), ps->second.resources.end());
    pg.root = makeRoot(all, pg.error);
    if (!pg.root) return false;
    auto& vb = gGpu.blobs[vs->second.dxil];
    auto& pb = gGpu.blobs[ps->second.dxil];
    if (vb.empty()) vb = readFile(gRoot / gGpu.loadedName / vs->second.dxil);
    if (pb.empty()) pb = readFile(gRoot / gGpu.loadedName / ps->second.dxil);
    if (vb.empty() || pb.empty()) { pg.error = "missing DXIL"; return false; }
    std::vector<D3D12_INPUT_ELEMENT_DESC> il;
    for (auto const& in : vs->second.inputs) {
        if (in.sem.rfind("SV_", 0) == 0) continue;
        il.push_back({"TEXCOORD", in.index, attrFormat(in), 0, attrOffset(in.index), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
    }
    // outputs: SV_Target n -> the pack's RENDERTARGETS entry n; final -> the game's colour target
    size_t nOut = 0;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = pg.root.Get();
    d.VS = {vb.data(), vb.size()};
    d.PS = {pb.data(), pb.size()};
    d.InputLayout = {il.data(), UINT(il.size())};
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable = TRUE;
    d.SampleMask = UINT_MAX; d.SampleDesc.Count = 1;
    pg.outs.clear();
    if (p.kind == "final") { pg.outs = {-1}; d.RTVFormats[0] = gameFmt; nOut = 1; }
    else {
        for (int t : p.targets) if (t >= 0 && t < kColortex && pg.outs.size() < 8) pg.outs.push_back(t);
        if (pg.outs.empty()) pg.outs = {0};
        nOut = pg.outs.size();
        for (size_t i = 0; i < nOut; ++i) d.RTVFormats[i] = gGpu.ct[0][size_t(pg.outs[i])].fmt;
    }
    d.NumRenderTargets = UINT(nOut);
    for (size_t i = 0; i < nOut; ++i) {
        auto& b = d.BlendState.RenderTarget[i];
        b.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        // composite passes blend only when the pack asks (blend.<program>=...); M1 keeps them opaque
        b.BlendEnable = FALSE;
    }
    if (FAILED(gGpu.dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&pg.pso)))) { pg.error = "CreateGraphicsPipelineState failed"; return false; }
    pg.usable = true;
    return true;
}

bool buildImport(DXGI_FORMAT ct0) {
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    D3D12_ROOT_PARAMETER p[2]{};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p[0].Constants = {0, 0, 4}; p[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[1].DescriptorTable = {1, &range}; p[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rs{2, p, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob, e;
    if (FAILED(rf::d3d12api::SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &e))) return false;
    if (FAILED(gGpu.dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&gGpu.importRoot)))) return false;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = gGpu.importRoot.Get();
    d.VS = {shaders::VSImport, sizeof shaders::VSImport};
    d.PS = {shaders::PSImport, sizeof shaders::PSImport};
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.SampleMask = UINT_MAX; d.SampleDesc.Count = 1; d.NumRenderTargets = 1; d.RTVFormats[0] = ct0;
    return SUCCEEDED(gGpu.dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&gGpu.importPso)));
}

// ---------------------------------------------------------------- device + bundle setup
bool ensureDevice(ID3D12Device* dev) {
    if (gGpu.dev.Get() == dev && gGpu.srvHeap) return true;
    gGpu = Gpu{};
    gGpu.dev = dev;
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kRing * kSrvPerFrame, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&gGpu.srvHeap)))) return false;
    hd = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, Gpu::kCpuCount, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&gGpu.cpuHeap)))) return false;
    hd = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, Gpu::kRtvCount, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&gGpu.rtvHeap)))) return false;
    gGpu.srvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    gGpu.rtvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_HEAP_PROPERTIES up{D3D12_HEAP_TYPE_UPLOAD};
    D3D12_RESOURCE_DESC   bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    for (UINT i = 0; i < kRing; ++i) {
        bd.Width = kCbPerFrame;
        if (FAILED(dev->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gGpu.cb[i])))) return false;
        D3D12_RANGE none{0, 0};
        gGpu.cb[i]->Map(0, &none, reinterpret_cast<void**>(&gGpu.cbMap[i]));
    }
    bd.Width = sizeof(QuadVert) * 6;
    if (FAILED(dev->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gGpu.vb)))) return false;
    QuadVert* q = nullptr;
    D3D12_RANGE none{0, 0};
    gGpu.vb->Map(0, &none, reinterpret_cast<void**>(&q));
    float const xy[6][2] = {{0, 0}, {1, 0}, {0, 1}, {0, 1}, {1, 0}, {1, 1}};
    for (int i = 0; i < 6; ++i) {
        QuadVert v{};
        v.pos[0] = xy[i][0]; v.pos[1] = xy[i][1]; v.pos[3] = 1;
        for (float& c : v.col) c = 1;
        v.nrm[2] = 1;
        v.uv0[0] = xy[i][0]; v.uv0[1] = 1.0f - xy[i][1]; v.uv0[3] = 1; // texture rows are top-down (see projInfinite)
        v.uv1[3] = v.uv2[3] = 1; v.uv2[0] = v.uv2[1] = 240;            // lightmap: full light
        q[i] = v;
    }
    gGpu.vb->Unmap(0, nullptr);
    // fixed textures: 1x1 shadow depth = 1 (nothing casts: fully lit), shadow colour white, white, null
    makeTex(gGpu.shadowDepth, 1, 1, DXGI_FORMAT_R32_FLOAT, false, D3D12_RESOURCE_STATE_COPY_DEST);
    makeTex(gGpu.shadowColor, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, false, D3D12_RESOURCE_STATE_COPY_DEST);
    makeTex(gGpu.white, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, false, D3D12_RESOURCE_STATE_COPY_DEST);
    srv(gGpu.shadowDepth.r.Get(), DXGI_FORMAT_R32_FLOAT, Gpu::kCpuShadowDepth);
    srv(gGpu.shadowColor.r.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, Gpu::kCpuShadowColor);
    srv(gGpu.white.r.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, Gpu::kCpuWhite);
    srv(nullptr, DXGI_FORMAT_R8G8B8A8_UNORM, Gpu::kCpuNull);
    return true;
}

bool loadBundle(std::string const& name, std::string const& world) {
    auto b = std::make_unique<Bundle>();
    if (!b->load((gRoot / name / "runtime.txt").string())) { setStatus("bundle: " + b->error); return false; }
    gGpu.bundle = std::move(b);
    gGpu.loadedName = name; gGpu.loadedWorld = world;
    gGpu.passes.clear(); gGpu.blobs.clear(); gGpu.w = gGpu.h = 0; gGpu.used = {};
    gGpu.used[0] = true;
    for (char const* kind : {"deferred", "composite", "final"})
        for (Pass const* p : gGpu.bundle->chain(world, kind)) {
            PassGpu pg; pg.pass = p; gGpu.passes.push_back(pg);
            for (int t : p->targets) if (t >= 0 && t < kColortex) gGpu.used[size_t(t)] = true;
            for (auto const& [sn, st] : p->stages)
                for (auto const& r : st.resources) { int c = colortexOf(r.name); if (c >= 0) gGpu.used[size_t(c)] = true; }
        }
    gU = FrameU{};
    if (gGpu.passes.empty()) { setStatus("no deferred / composite / final programs for " + world); return false; }
    return true;
}

bool ensureTargets(ID3D12GraphicsCommandList* list, UINT w, UINT h, DXGI_FORMAT sceneFmt, ID3D12Resource* gameRt) {
    if (gGpu.w == w && gGpu.h == h && gGpu.sceneFmt == sceneFmt && gGpu.importPso) {
        // the game's target can be a different resource every frame (swap chain style): refresh its RTV
        D3D12_RENDER_TARGET_VIEW_DESC rv{}; rv.Format = typedOf(sceneFmt); rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        gGpu.dev->CreateRenderTargetView(gameRt, &rv, rtvAt(Gpu::kRtvGame));
        return true;
    }
    Bundle const& B = *gGpu.bundle;
    for (int i = 0; i < kColortex; ++i) {
        for (int k = 0; k < 2; ++k) gGpu.ct[k][size_t(i)] = Tex{};
        if (!gGpu.used[size_t(i)]) continue;
        DXGI_FORMAT f = dxgiOf(B.constOr("colortex" + std::to_string(i) + "Format", i == 0 ? "RGBA16F" : "RGBA8"));
        for (int k = 0; k < 2; ++k) {
            D3D12_CLEAR_VALUE cv{}; cv.Format = f;
            if (!makeTex(gGpu.ct[k][size_t(i)], w, h, f, true, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cv)) { setStatus("colortex alloc failed"); return false; }
            srv(gGpu.ct[k][size_t(i)].r.Get(), f, gGpu.cpuCt(i, k));
            gGpu.dev->CreateRenderTargetView(gGpu.ct[k][size_t(i)].r.Get(), nullptr, rtvAt(UINT(i * 2 + k)));
        }
    }
    // depth copy (same format family as Bedrock's D24S8, read as R24_UNORM_X8) + scene copy for the import
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = w; d.Height = h; d.DepthOrArraySize = 1; d.MipLevels = 1;
    d.Format = DXGI_FORMAT_R24G8_TYPELESS; d.SampleDesc.Count = 1;
    gGpu.depth = Tex{};
    if (FAILED(gGpu.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&gGpu.depth.r)))) return false;
    gGpu.depth.state = D3D12_RESOURCE_STATE_COPY_DEST; gGpu.depth.fmt = d.Format;
    srv(gGpu.depth.r.Get(), DXGI_FORMAT_R24_UNORM_X8_TYPELESS, Gpu::kCpuDepth);
    auto const sd = gameRt->GetDesc();
    d.Format = sd.Format;
    gGpu.scene = Tex{};
    if (FAILED(gGpu.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&gGpu.scene.r)))) return false;
    gGpu.scene.state = D3D12_RESOURCE_STATE_COPY_DEST; gGpu.scene.fmt = d.Format;
    srv(gGpu.scene.r.Get(), typedOf(sceneFmt), Gpu::kCpuScene);
    D3D12_RENDER_TARGET_VIEW_DESC rv{}; rv.Format = typedOf(sceneFmt); rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    gGpu.dev->CreateRenderTargetView(gameRt, &rv, rtvAt(Gpu::kRtvGame));
    // noisetex: random RGBA, noiseTextureResolution (default 256)
    int const nr = std::clamp(std::atoi(B.constOr("noiseTextureResolution", "256").c_str()), 16, 2048);
    makeTex(gGpu.noise, UINT(nr), UINT(nr), DXGI_FORMAT_R8G8B8A8_UNORM, false, D3D12_RESOURCE_STATE_COPY_DEST);
    srv(gGpu.noise.r.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, Gpu::kCpuNoise);
    gGpu.noisePx.resize(size_t(nr) * nr * 4);
    std::mt19937 rng(0x1715);
    for (auto& b : gGpu.noisePx) b = uint8_t(rng() & 255);
    gGpu.noiseUp = false;
    gGpu.w = w; gGpu.h = h; gGpu.sceneFmt = sceneFmt;
    // pipelines (formats are known now)
    int ok = 0;
    for (auto& pg : gGpu.passes) { pg.usable = false; pg.pso.Reset(); pg.root.Reset(); pg.error.clear(); ok += buildPass(pg, typedOf(sceneFmt)) ? 1 : 0; }
    if (!buildImport(gGpu.ct[0][0].fmt)) { setStatus("import pipeline failed"); return false; }
    (void)list;
    return ok > 0;
}

/// One-time uploads of the small fixed textures (needs a list; done on the first frame).
std::vector<ComPtr<ID3D12Resource>> gUploadKeep; // upload buffers kept alive a few frames
void uploadPixels(ID3D12GraphicsCommandList* list, Tex& t, void const* px, UINT w, UINT h, UINT bpp) {
    D3D12_RESOURCE_DESC d = t.r->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT64 total = 0;
    gGpu.dev->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &total);
    D3D12_HEAP_PROPERTIES up{D3D12_HEAP_TYPE_UPLOAD};
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = total; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
    bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> buf;
    if (FAILED(gGpu.dev->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buf)))) return;
    uint8_t* m = nullptr;
    buf->Map(0, nullptr, reinterpret_cast<void**>(&m));
    for (UINT y = 0; y < h; ++y) std::memcpy(m + fp.Offset + size_t(y) * fp.Footprint.RowPitch, static_cast<uint8_t const*>(px) + size_t(y) * w * bpp, size_t(w) * bpp);
    buf->Unmap(0, nullptr);
    barrier(list, t, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
    dst.pResource = t.r.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.pResource = buf.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = fp;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    barrier(list, t, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    gUploadKeep.push_back(buf);
}

// ---------------------------------------------------------------- uniforms
void put(std::string const& n, std::initializer_list<float> v) {
    auto& a = gU.v[n]; a.fill(0); size_t i = 0; for (float x : v) if (i < 16) a[i++] = x;
}
void putM(std::string const& n, Mat const& m) { auto& a = gU.v[n]; std::copy(m.begin(), m.end(), a.begin()); }

void frameUniforms(rf::lyrics::CameraSnap const& cam) {
    auto now = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double>(now - gU.last).count();
    if (dt <= 0 || dt > 1) dt = 1.0 / 60.0;
    gU.last = now;
    gU.timeCounter = std::fmod(gU.timeCounter + dt, 3600.0);
    gU.frameCounter = (gU.frameCounter + 1) % 720720;
    if (gSet.cycle) gSet.timeOfDay = float(std::fmod(gSet.timeOfDay + dt * 20.0, 24000.0));
    float const W = float(gGpu.w), H = float(gGpu.h), n = 0.025f, farB = gSet.farBlocks;
    Mat const view = viewFromBasis({cam.forward[0], cam.forward[1], cam.forward[2]}, {cam.up[0], cam.up[1], cam.up[2]});
    Mat const proj = projInfinite(std::fabs(cam.proj[0]), std::fabs(cam.proj[5]), n);
    if (!gU.havePrev) { gU.prevView = view; gU.prevProj = proj; std::copy(cam.pos, cam.pos + 3, gU.prevCam); gU.havePrev = true; }
    put("viewWidth", {W}); put("viewHeight", {H}); put("aspectRatio", {W / std::max(H, 1.0f)});
    put("near", {n}); put("far", {farB});
    put("frameCounter", {float(gU.frameCounter)}); put("frameTime", {float(dt)}); put("frameTimeCounter", {float(gU.timeCounter)});
    float const t = gSet.timeOfDay;
    put("worldTime", {std::floor(t)}); put("worldDay", {0}); put("moonPhase", {0});
    double c = t / 24000.0 - 0.25; c -= std::floor(c);
    c = c + ((1.0 - (std::cos(c * 3.14159265358979) + 1.0) / 2.0) - c) / 3.0;   // Minecraft's celestial angle easing
    float const sunAngle = float(c < 0.75 ? c + 0.25 : c - 0.75);
    put("sunAngle", {sunAngle}); put("shadowAngle", {sunAngle > 0.5f ? sunAngle - 0.5f : sunAngle});
    float const spr = std::stof(gGpu.bundle->constOr("sunPathRotation", "0")) * 3.14159265f / 180.0f;
    float const a = float(c * 2.0 * 3.14159265358979);
    std::array<float, 3> sun = {-std::sin(a), std::cos(a), 0.0f};               // east at sunrise, overhead at noon
    sun = {sun[0], sun[1] * std::cos(spr), sun[1] * std::sin(spr)};             // the path tilts by sunPathRotation
    auto sp = xform(view, sun[0] * 100, sun[1] * 100, sun[2] * 100, 0);
    auto up = xform(view, 0, 100, 0, 0);
    put("sunPosition", {sp[0], sp[1], sp[2]}); put("moonPosition", {-sp[0], -sp[1], -sp[2]});
    bool const day = sunAngle <= 0.5f;
    if (day) put("shadowLightPosition", {sp[0], sp[1], sp[2]}); else put("shadowLightPosition", {-sp[0], -sp[1], -sp[2]});
    put("upPosition", {up[0], up[1], up[2]});
    putM("gbufferModelView", view); putM("gbufferModelViewInverse", inverse(view));
    putM("gbufferProjection", proj); putM("gbufferProjectionInverse", inverse(proj));
    putM("gbufferPreviousModelView", gU.prevView); putM("gbufferPreviousProjection", gU.prevProj);
    // shadow camera: looking down the light, ortho over shadowDistance (no shadow map in M1: lookups read "lit")
    std::array<float, 3> L = day ? sun : std::array<float, 3>{-sun[0], -sun[1], -sun[2]};
    Mat const sview = viewFromBasis({-L[0], -L[1], -L[2]}, std::fabs(L[1]) > 0.99f ? std::array<float, 3>{0, 0, 1} : std::array<float, 3>{0, 1, 0});
    float const sd = std::stof(gGpu.bundle->constOr("shadowDistance", "160"));
    Mat const sproj = ortho(-sd, sd, -sd, sd, -sd * 2, sd * 2);
    putM("shadowModelView", sview); putM("shadowModelViewInverse", inverse(sview));
    putM("shadowProjection", sproj); putM("shadowProjectionInverse", inverse(sproj));
    put("cameraPosition", {float(cam.pos[0]), float(cam.pos[1]), float(cam.pos[2])});
    put("previousCameraPosition", {float(gU.prevCam[0]), float(gU.prevCam[1]), float(gU.prevCam[2])});
    put("cameraPositionInt", {std::floor(float(cam.pos[0])), std::floor(float(cam.pos[1])), std::floor(float(cam.pos[2]))});
    put("cameraPositionFract", {float(cam.pos[0] - std::floor(cam.pos[0])), float(cam.pos[1] - std::floor(cam.pos[1])), float(cam.pos[2] - std::floor(cam.pos[2]))});
    put("eyeAltitude", {float(cam.pos[1])}); put("relativeEyePosition", {0, 0, 0});
    put("skyColor", {0.47f, 0.65f, 1.0f}); put("fogColor", {0.75f, 0.85f, 1.0f});
    put("rainStrength", {0}); put("wetness", {0}); put("isEyeInWater", {0});
    put("eyeBrightness", {0, 240}); put("eyeBrightnessSmooth", {0, 240}); put("screenBrightness", {1});
    put("nightVision", {0}); put("blindness", {0}); put("darknessFactor", {0}); put("darknessLightFactor", {0}); put("playerMood", {0});
    put("heldItemId", {0}); put("heldItemId2", {0}); put("heldBlockLightValue", {0}); put("heldBlockLightValue2", {0});
    put("entityId", {0}); put("blockEntityId", {0}); put("currentRenderedItemId", {0}); put("renderStage", {0});
    put("atlasSize", {0, 0}); put("lightningBoltPosition", {0, 0, 0, 0}); put("entityColor", {0, 0, 0, 0});
    put("biome", {0}); put("biome_precipitation", {0}); put("is_on_ground", {1});
    // full-screen programs: identity model-view, ortho 0..1 projection (what Iris gives composite / final)
    Mat const o = ortho(0, 1, 0, 1, -1, 1);
    putM("iris_ModelViewMat", ident()); putM("iris_ProjMat", o); putM("iris_TextureMat0", ident()); putM("iris_TextureMat1", ident());
    putM("modelViewMatrix", ident()); putM("projectionMatrix", o); putM("textureMatrix", ident());
    put("iris_NormalMat", {1, 0, 0, 0, 1, 0, 0, 0, 1}); put("normalMatrix", {1, 0, 0, 0, 1, 0, 0, 0, 1});
    // custom uniforms (shaders.properties)
    UniformVM& vm = gGpu.bundle->vm;
    std::vector<double> in(vm.inputs.size(), 0.0);
    for (size_t i = 0; i < in.size(); ++i) {
        std::string const& nm = vm.inputs[i];
        auto dot = nm.find('.');
        std::string base = dot == std::string::npos ? nm : nm.substr(0, dot);
        int comp = dot == std::string::npos ? 0 : std::string("xyzw").find(nm[dot + 1]);
        auto it = gU.v.find(base);
        if (it != gU.v.end() && comp >= 0 && comp < 16) in[i] = it->second[size_t(comp)];
    }
    vm.run(in, dt);
    for (size_t s = 0; s < vm.slots.size(); ++s)
        if (vm.slots[s].exported) gU.v[vm.slots[s].uniform][size_t(std::clamp(vm.slots[s].comp, 0, 15))] = float(vm.value(s));
    gU.prevView = view; gU.prevProj = proj; std::copy(cam.pos, cam.pos + 3, gU.prevCam);
}

/// Fill one stage's $Globals from the uniform table (types from DXC's reflection), returns the GPU address.
D3D12_GPU_VIRTUAL_ADDRESS writeGlobals(Stage const& st, UINT64& cbOff, UINT ring) {
    UINT64 const size = (std::max<UINT64>(st.globalsSize, 16) + 255) & ~UINT64(255);
    if (cbOff + size > kCbPerFrame) return 0;
    uint8_t* base = gGpu.cbMap[ring] + cbOff;
    std::memset(base, 0, size_t(size));
    for (auto const& g : st.globals) {
        std::array<float, 16> v{};
        auto it = gU.v.find(g.name);
        if (it != gU.v.end()) v = it->second;
        else if (auto d = st.defaults.find(g.name); d != st.defaults.end()) {
            std::string s = d->second; // "192.0f" / "float3(1.0f,2.0f,3.0f)"
            for (char& ch : s) if (ch == '(' || ch == ')' || ch == ',' || ch == 'f') ch = ' ';
            std::istringstream is(s.substr(s.find_first_of("-0123456789.") == std::string::npos ? 0 : s.find_first_of("-0123456789.")));
            for (float& x : v) if (!(is >> x)) break;
        }
        std::string const& t = g.type;
        uint8_t* p = base + g.offset;
        bool const isInt = t.rfind("int", 0) == 0, isUint = t.rfind("uint", 0) == 0 || t.rfind("bool", 0) == 0;
        if (t == "float4x4") { std::memcpy(p, v.data(), 64); continue; }
        if (t == "float3x3") { for (int col = 0; col < 3; ++col) std::memcpy(p + col * 16, v.data() + col * 3, 12); continue; }
        if (t == "float2x2") { for (int col = 0; col < 2; ++col) std::memcpy(p + col * 16, v.data() + col * 2, 8); continue; }
        int n = 1;
        if (!t.empty() && std::isdigit(static_cast<unsigned char>(t.back()))) n = t.back() - '0';
        for (int i = 0; i < n && i < 4; ++i) {
            if (isInt) { int32_t x = int32_t(std::lround(v[size_t(i)])); std::memcpy(p + 4 * i, &x, 4); }
            else if (isUint) { uint32_t x = uint32_t(std::max(0L, std::lround(v[size_t(i)]))); std::memcpy(p + 4 * i, &x, 4); }
            else std::memcpy(p + 4 * i, &v[size_t(i)], 4);
        }
    }
    D3D12_GPU_VIRTUAL_ADDRESS a = gGpu.cb[ring]->GetGPUVirtualAddress() + cbOff;
    cbOff += size;
    return a;
}

UINT cpuSlotFor(UINT slot) {
    if (slot < UINT(kColortex)) return gGpu.used[slot] ? gGpu.cpuCt(int(slot), 0) : Gpu::kCpuNull;
    switch (slot) {
    case 16: case 17: case 18: return Gpu::kCpuDepth;                   // depthtex0/1/2: the world before translucents (M1 hooks there)
    case 19: case 20: case 28: case 29: return Gpu::kCpuShadowDepth;
    case 21: case 22: return Gpu::kCpuShadowColor;
    case 23: return Gpu::kCpuNoise;
    case 24: case 25: case 26: case 27: return Gpu::kCpuWhite;          // atlas / normals / specular / lightmap: not in post
    default: return Gpu::kCpuWhite;
    }
}

} // namespace

// ---------------------------------------------------------------- public
void init(fs::path const& modDir) {
    std::lock_guard l(gMutex);
    gRoot = modDir / "irisbridge";
    std::error_code ec;
    fs::create_directories(gRoot, ec);
    scanPacks();
    std::ifstream in(gRoot / "settings.txt");
    std::string k; float v;
    while (in >> k >> v) {
        if (k == "on") gSet.on = v != 0;
        else if (k == "pack") gSet.pack = int(v);
        else if (k == "world") gSet.world = std::clamp(int(v), 0, 2);
        else if (k == "time") gSet.timeOfDay = std::clamp(v, 0.f, 24000.f);
        else if (k == "cycle") gSet.cycle = v != 0;
        else if (k == "far") gSet.farBlocks = std::clamp(v, 32.f, 2048.f);
        else if (k == "gamma") gSet.decodeGamma = v != 0;
        else if (k == "exposure") gSet.exposure = std::clamp(v, 0.1f, 8.f);
    }
    gOn = gSet.on && !gPacks.empty();
    if (gOn) rf::worldfx::setTargetsWanted(true);
}

bool enabled() { return gOn.load(std::memory_order_relaxed); }

bool run(ID3D12GraphicsCommandList* list, rf::sky::GameState const& st, rf::sky::Calls const& calls, FrameTargets const& t) {
    std::lock_guard l(gMutex);
    if (!gSet.on || gPacks.empty()) return false;
    if (!st.rootKnown) { setStatus("waiting for the game's root state"); return false; }
    auto cam = rf::lyrics::camera();
    int64_t const nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (!cam.valid || nowMs - cam.atMs > 250 || cam.proj[0] == 0 || cam.proj[5] == 0) { setStatus("waiting for the game camera"); return false; }
    auto const rd = t.rt->GetDesc(), dd = t.ds->GetDesc();
    if (rd.SampleDesc.Count > 1) { setStatus("MSAA target: not supported"); return false; }
    if (dd.Format != DXGI_FORMAT_D24_UNORM_S8_UINT && dd.Format != DXGI_FORMAT_R24G8_TYPELESS) { setStatus("depth is not D24S8"); return false; }
    ComPtr<ID3D12Device> dev;
    if (FAILED(list->GetDevice(IID_PPV_ARGS(&dev))) || !ensureDevice(dev.Get())) { setStatus("device setup failed"); return false; }
    std::string const want = gPacks[size_t(std::clamp(gSet.pack, 0, int(gPacks.size()) - 1))];
    if (!gGpu.bundle || gGpu.loadedName != want || gGpu.loadedWorld != worldName(gSet.world))
        if (!loadBundle(want, worldName(gSet.world))) return false;
    if (!ensureTargets(list, UINT(rd.Width), rd.Height, rd.Format, t.rt)) { if (gStatus.find("failed") == std::string::npos) setStatus("no usable pass"); return false; }
    if (!gGpu.noiseUp) {
        uploadPixels(list, gGpu.noise, gGpu.noisePx.data(), UINT(std::sqrt(double(gGpu.noisePx.size() / 4))), UINT(std::sqrt(double(gGpu.noisePx.size() / 4))), 4);
        float one = 1.0f; uploadPixels(list, gGpu.shadowDepth, &one, 1, 1, 4);
        uint32_t w = 0xFFFFFFFFu; uploadPixels(list, gGpu.shadowColor, &w, 1, 1, 4); uploadPixels(list, gGpu.white, &w, 1, 1, 4);
        gGpu.noiseUp = true;
    } else if (gUploadKeep.size() && gGpu.frame > 8) gUploadKeep.clear();

    UINT const ring = UINT(gGpu.frame++ % kRing);
    UINT64 cbOff = 0;
    UINT   srvNext = ring * kSrvPerFrame;
    frameUniforms(cam);

    // 1) copy the world: colour (for the import) + depth
    auto const rtState = D3D12_RESOURCE_STATES(t.rtState), dsState = D3D12_RESOURCE_STATES(t.dsState);
    barrierRaw(list, t.rt, rtState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    barrierRaw(list, t.ds, dsState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    barrier(list, gGpu.scene, D3D12_RESOURCE_STATE_COPY_DEST);
    barrier(list, gGpu.depth, D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyResource(gGpu.scene.r.Get(), t.rt);
    list->CopyResource(gGpu.depth.r.Get(), t.ds);
    barrierRaw(list, t.ds, D3D12_RESOURCE_STATE_COPY_SOURCE, dsState);
    barrierRaw(list, t.rt, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    barrier(list, gGpu.scene, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    barrier(list, gGpu.depth, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    ID3D12DescriptorHeap* heaps[] = {gGpu.srvHeap.Get()};
    calls.setHeaps(list, 1, heaps);
    D3D12_VIEWPORT vp{0, 0, float(gGpu.w), float(gGpu.h), 0, 1};
    D3D12_RECT     sc{0, 0, LONG(gGpu.w), LONG(gGpu.h)};
    auto gpuAt = [&](UINT i) { auto h = gGpu.srvHeap->GetGPUDescriptorHandleForHeapStart(); h.ptr += UINT64(i) * gGpu.srvInc; return h; };
    auto cpuRingAt = [&](UINT i) { auto h = gGpu.srvHeap->GetCPUDescriptorHandleForHeapStart(); h.ptr += size_t(i) * gGpu.srvInc; return h; };

    // 2) clears (colortexNClear, Iris default true) + import the world into colortex0
    Bundle const& B = *gGpu.bundle;
    for (int i = 1; i < kColortex; ++i) {
        if (!gGpu.used[size_t(i)] || B.constOr("colortex" + std::to_string(i) + "Clear", "true") == "false") continue;
        float const cc[4] = {i == 1 ? 1.0f : 0.0f, i == 1 ? 1.0f : 0.0f, i == 1 ? 1.0f : 0.0f, i == 1 ? 1.0f : 0.0f}; // Iris: colortex1 clears to white
        barrier(list, gGpu.ct[0][size_t(i)], D3D12_RESOURCE_STATE_RENDER_TARGET);
        list->ClearRenderTargetView(rtvAt(UINT(i * 2)), cc, 0, nullptr);
        barrier(list, gGpu.ct[0][size_t(i)], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    {
        barrier(list, gGpu.ct[0][0], D3D12_RESOURCE_STATE_RENDER_TARGET);
        auto rtv = rtvAt(0);
        list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        list->RSSetViewports(1, &vp); list->RSSetScissorRects(1, &sc);
        gGpu.dev->CopyDescriptorsSimple(1, cpuRingAt(srvNext), cpuAt(Gpu::kCpuScene), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        calls.setRootSig(list, gGpu.importRoot.Get());
        float const c0[4] = {gSet.decodeGamma ? 1.0f : 0.0f, gSet.exposure, 0, 0};
        calls.setConsts(list, 0, 4, c0, 0);
        calls.setTable(list, 1, gpuAt(srvNext).ptr);
        srvNext += 1;
        calls.setPso(list, gGpu.importPso.Get());
        calls.setTopo(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        calls.draw(list, 3, 1, 0, 0);
        barrier(list, gGpu.ct[0][0], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    // 3) the pack's chain
    D3D12_VERTEX_BUFFER_VIEW vbv{gGpu.vb->GetGPUVirtualAddress(), UINT(sizeof(QuadVert) * 6), UINT(sizeof(QuadVert))};
    int drawn = 0;
    bool finalDone = false;
    for (auto& pg : gGpu.passes) {
        if (!pg.usable) continue;
        if (srvNext + kSlots > (ring + 1) * kSrvPerFrame) break;
        Pass const& p = *pg.pass;
        // descriptor table: every fixed slot (reads see the CURRENT buffers: a pass writing colortexN reads the old one)
        for (UINT s = 0; s < kSlots; ++s) {
            UINT src = cpuSlotFor(s);
            gGpu.dev->CopyDescriptorsSimple(1, cpuRingAt(srvNext + s), cpuAt(src), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
        D3D12_CPU_DESCRIPTOR_HANDLE rtvs[8]{};
        UINT nrt = 0;
        bool toGame = false;
        for (int o : pg.outs) {
            if (o < 0) {
                toGame = true;
                rtvs[nrt++] = rtvAt(Gpu::kRtvGame);
            } else {
                barrier(list, gGpu.ct[1][size_t(o)], D3D12_RESOURCE_STATE_RENDER_TARGET);
                rtvs[nrt++] = rtvAt(UINT(o * 2 + 1));
            }
        }
        list->OMSetRenderTargets(nrt, rtvs, FALSE, nullptr);
        list->RSSetViewports(1, &vp); list->RSSetScissorRects(1, &sc);
        calls.setRootSig(list, pg.root.Get());
        D3D12_GPU_VIRTUAL_ADDRESS vcb = writeGlobals(p.stages.at("vert"), cbOff, ring);
        D3D12_GPU_VIRTUAL_ADDRESS pcb = writeGlobals(p.stages.at("frag"), cbOff, ring);
        if (!vcb || !pcb) break;
        list->SetGraphicsRootConstantBufferView(0, vcb);
        list->SetGraphicsRootConstantBufferView(1, pcb);
        calls.setTable(list, 2, gpuAt(srvNext).ptr);
        srvNext += kSlots;
        calls.setPso(list, pg.pso.Get());
        calls.setTopo(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        calls.setVb(list, 0, 1, &vbv);
        calls.draw(list, 6, 1, 0, 0);
        ++drawn;
        // the written buffers become current (Iris "flip")
        for (int o : pg.outs) {
            if (o < 0) continue;
            barrier(list, gGpu.ct[1][size_t(o)], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            std::swap(gGpu.ct[0][size_t(o)], gGpu.ct[1][size_t(o)]);
            // keep the staging SRVs / RTVs pointing at [0] = current, [1] = next write
            srv(gGpu.ct[0][size_t(o)].r.Get(), gGpu.ct[0][size_t(o)].fmt, gGpu.cpuCt(o, 0));
            srv(gGpu.ct[1][size_t(o)].r.Get(), gGpu.ct[1][size_t(o)].fmt, gGpu.cpuCt(o, 1));
            gGpu.dev->CreateRenderTargetView(gGpu.ct[0][size_t(o)].r.Get(), nullptr, rtvAt(UINT(o * 2)));
            gGpu.dev->CreateRenderTargetView(gGpu.ct[1][size_t(o)].r.Get(), nullptr, rtvAt(UINT(o * 2 + 1)));
        }
        if (toGame) { finalDone = true; break; }
    }
    if (!finalDone && drawn) {
        // no final program: Iris shows colortex0 - copy it back through the import pipeline's inverse is not here yet
        setStatus("pack has no usable final program");
    }

    // 4) give the game its state back
    calls.restoreRoot(list);
    calls.setPso(list, st.pso);
    if (st.haveTopo) calls.setTopo(list, st.topo);
    if (st.nVb && st.vb) calls.setVb(list, 0, st.nVb, st.vb);
    D3D12_CPU_DESCRIPTOR_HANDLE grt{t.rtv}, gds{t.dsv};
    list->OMSetRenderTargets(1, &grt, FALSE, t.dsv ? &gds : nullptr);
    D3D12_VIEWPORT gvp{0, 0, float(rd.Width), float(rd.Height), 0, 1};
    D3D12_RECT     gsc{0, 0, LONG(rd.Width), LONG(rd.Height)};
    list->RSSetViewports(1, &gvp); list->RSSetScissorRects(1, &gsc);
    if (rtState != D3D12_RESOURCE_STATE_RENDER_TARGET) barrierRaw(list, t.rt, D3D12_RESOURCE_STATE_RENDER_TARGET, rtState);
    if ((gGpu.frame & 63) == 1)
        setStatus(gGpu.loadedName + " / " + gGpu.loadedWorld + ": " + std::to_string(drawn) + " passes, " + std::to_string(gGpu.w) + "x" + std::to_string(gGpu.h));
    return drawn > 0;
}

void drawUi() {
    if (!ImGui::CollapsingHeader("IrisBridge (Java shaderpacks)")) return;
    std::lock_guard l(gMutex);
    ImGui::TextWrapped("Runs an Iris / OptiFine shaderpack translated by Bedrock Forge's tools/IrisBridge. "
                       "Bundles: %s", gRoot.string().c_str());
    bool changed = false;
    if (ImGui::Button("Rescan bundles")) { scanPacks(); gGpu.loadedName.clear(); }
    if (gPacks.empty()) { ImGui::TextDisabled("no bundle found (a folder with runtime.txt)"); return; }
    changed |= ImGui::Checkbox("On (classic renderer, at the cloud draw)", &gSet.on);
    std::vector<char const*> names;
    for (auto const& s : gPacks) names.push_back(s.c_str());
    changed |= ImGui::Combo("Pack", &gSet.pack, names.data(), int(names.size()));
    char const* worlds[] = {"Overworld (world0)", "Nether (world-1)", "End (world1)"};
    changed |= ImGui::Combo("Dimension", &gSet.world, worlds, 3);
    changed |= ImGui::SliderFloat("Time of day (ticks)", &gSet.timeOfDay, 0, 24000, "%.0f");
    changed |= ImGui::Checkbox("Let time run", &gSet.cycle);
    changed |= ImGui::SliderFloat("Render distance (far)", &gSet.farBlocks, 32, 1024, "%.0f blocks");
    changed |= ImGui::Checkbox("Decode game gamma into colortex0", &gSet.decodeGamma);
    changed |= ImGui::SliderFloat("Import exposure", &gSet.exposure, 0.25f, 4.0f, "%.2f");
    ImGui::Text("Status: %s", gStatus.c_str());
    if (ImGui::TreeNode("Passes")) {
        for (auto const& pg : gGpu.passes)
            ImGui::Text("%s %-12s %s", pg.usable ? "+" : "-", pg.pass->program.c_str(), pg.usable ? "" : pg.error.c_str());
        ImGui::TreePop();
    }
    if (changed) {
        gOn = gSet.on;
        if (gSet.on) rf::worldfx::setTargetsWanted(true);
        std::ofstream out(gRoot / "settings.txt");
        out << "on " << gSet.on << "\npack " << gSet.pack << "\nworld " << gSet.world << "\ntime " << gSet.timeOfDay << "\ncycle " << gSet.cycle
            << "\nfar " << gSet.farBlocks << "\ngamma " << gSet.decodeGamma << "\nexposure " << gSet.exposure << "\n";
    }
}

} // namespace rf::iris
