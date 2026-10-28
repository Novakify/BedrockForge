// Renderer Foundation - Spatial GUI M0 capture spike (see UiCapture.h). Original code for Renderer Foundation.
#include "UiCapture.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d12.h>
#include <wrl/client.h>

#include <imgui.h>

#include "LLShim.h"
#include "Options.h"

namespace rf::uicapture {
namespace {
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

uint64_t fnv1a(void const* p, size_t n) {
    auto     b = static_cast<uint8_t const*>(p);
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; ++i) {
        h ^= b[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

std::string hex64(uint64_t v) {
    char b[24];
    snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
    return b;
}

fs::path gConfigDir, gOutDir;
std::unordered_map<uint64_t, std::string> gUiHashes; // FNV-1a 64 of a vanilla UI pixel shader -> "group material/pass platform"

// ---------------------------------------------------------------- shared state (gMutex)
std::mutex gMutex;
std::unordered_map<ID3D12PipelineState*, std::string const*>        gUiPso;   // tagged UI pipelines
std::unordered_map<uint64_t, std::pair<size_t, uint32_t>>           gSeenPs;  // every PS hash -> (size, PSOs)
std::unordered_map<SIZE_T, ID3D12Resource*>                         gRtvRes;  // RTV descriptor -> resource (not owned)
std::unordered_map<ID3D12GraphicsCommandList*, SIZE_T>              gListRt0; // while watching: list -> bound RT0
std::unordered_map<ID3D12GraphicsCommandList*, ID3D12PipelineState*> gListPso; // while watching: list -> current PSO
std::atomic<long> gPsoCreated{0}, gPsoUi{0}, gPsoStream{0};

enum State : int { Idle = 0, Tracking = 1, Capturing = 2 };
std::atomic<int>  gState{Idle};
std::atomic_bool  gRequest{false}, gTookA{false};
int               gCaptureFrames = 0;

struct Result {
    long                         uiDraws = 0;
    std::string                  firstUi;            // material of the first UI draw
    std::vector<ID3D12Resource*> uiTargets;          // distinct RT0 seen by UI draws
    std::vector<std::string>     uiTargetDescs;
    long                         uiDrawsNoTarget = 0; // UI draws whose RT0 wasn't known
    ComPtr<ID3D12Resource>       snapA;
    ID3D12Resource*              srcA = nullptr;      // not owned, only compared
    D3D12_RESOURCE_DESC          descA{};
    std::string                  noteA;
} gRes;
std::string gStatus = "idle", gVerdict;

std::string fmtName(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "R8G8B8A8_UNORM_SRGB";
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return "R8G8B8A8_TYPELESS";
    case DXGI_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return "B8G8R8A8_UNORM_SRGB";
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return "B8G8R8A8_TYPELESS";
    case DXGI_FORMAT_B8G8R8X8_UNORM: return "B8G8R8X8_UNORM";
    case DXGI_FORMAT_R10G10B10A2_UNORM: return "R10G10B10A2_UNORM";
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return "R10G10B10A2_TYPELESS";
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
    case DXGI_FORMAT_R11G11B10_FLOAT: return "R11G11B10_FLOAT";
    default: return "format " + std::to_string(int(f));
    }
}

std::string descStr(D3D12_RESOURCE_DESC const& d) {
    return std::to_string(d.Width) + "x" + std::to_string(d.Height) + " " + fmtName(d.Format) + " samples " +
           std::to_string(d.SampleDesc.Count) + " mips " + std::to_string(d.MipLevels) + " array " +
           std::to_string(d.DepthOrArraySize) + (d.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D ? "" : " (not 2D)");
}

// ---------------------------------------------------------------- snapshot A (game thread, inside the game's list)
void snapshot(ID3D12GraphicsCommandList* list, ID3D12Resource* rt) {
    D3D12_RESOURCE_DESC d = rt->GetDesc();
    gRes.srcA  = rt;
    gRes.descA = d;
    gTookA     = true; // one attempt only, whatever happens below
    if (d.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || d.SampleDesc.Count != 1) {
        gRes.noteA = "first UI target is not a plain 2D texture (" + descStr(d) + "), not copied";
        return;
    }
    ComPtr<ID3D12Device> dev;
    if (FAILED(rt->GetDevice(IID_PPV_ARGS(&dev)))) { gRes.noteA = "GetDevice failed"; return; }
    D3D12_RESOURCE_DESC sd = d;
    sd.MipLevels           = 1;
    sd.DepthOrArraySize    = 1;
    sd.Flags               = D3D12_RESOURCE_FLAG_NONE;
    sd.Alignment           = 0;
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    HRESULT hr = dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &sd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                              IID_PPV_ARGS(&gRes.snapA));
    if (FAILED(hr)) { gRes.noteA = "CreateCommittedResource failed hr=" + hex64(uint32_t(hr)); return; }
    D3D12_RESOURCE_BARRIER b{};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = rt;
    b.Transition.Subresource = 0;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1, &b);
    D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
    dst.pResource        = gRes.snapA.Get();
    dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    src.pResource        = rt;
    src.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    list->ResourceBarrier(1, &b);
    gRes.noteA = "copied";
}

// A fault here must not take the game down: give up on this capture instead.
bool safeSnapshot(ID3D12GraphicsCommandList* list, ID3D12Resource* rt) {
    __try {
        snapshot(list, rt);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gTookA = true;
        return false;
    }
}

void onDraw(ID3D12GraphicsCommandList* list) {
    std::lock_guard l(gMutex);
    auto p = gListPso.find(list);
    if (p == gListPso.end()) return;
    auto u = gUiPso.find(p->second);
    if (u == gUiPso.end()) return;
    ++gRes.uiDraws;
    ID3D12Resource* rt = nullptr;
    if (auto lt = gListRt0.find(list); lt != gListRt0.end())
        if (auto r = gRtvRes.find(lt->second); r != gRtvRes.end()) rt = r->second;
    if (!rt) { ++gRes.uiDrawsNoTarget; return; }
    if (std::find(gRes.uiTargets.begin(), gRes.uiTargets.end(), rt) == gRes.uiTargets.end()) {
        gRes.uiTargets.push_back(rt);
        gRes.uiTargetDescs.push_back(descStr(rt->GetDesc()) + "  first UI material: " + *u->second);
    }
    if (gTookA) return;
    gRes.firstUi = *u->second;
    if (!safeSnapshot(list, rt)) gRes.noteA = "fault while copying (SEH), capture abandoned";
}

// ---------------------------------------------------------------- hooks
using CreateGps_t  = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, D3D12_GRAPHICS_PIPELINE_STATE_DESC const*, REFIID, void**);
using CreatePs_t   = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, void const*, REFIID, void**);
using CreateRtv_t  = void(STDMETHODCALLTYPE*)(ID3D12Device*, ID3D12Resource*, D3D12_RENDER_TARGET_VIEW_DESC const*, D3D12_CPU_DESCRIPTOR_HANDLE);
using SetPso_t     = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12PipelineState*);
using OmSet_t      = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, D3D12_CPU_DESCRIPTOR_HANDLE const*, BOOL,
                                          D3D12_CPU_DESCRIPTOR_HANDLE const*);
using Draw_t       = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, UINT, UINT);
using DrawIdx_t    = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, UINT, INT, UINT);
constexpr int kHooks = 7;
void*         gTgt[kHooks]{};
void*         gDet[kHooks]{};
void*         gOrig[kHooks]{};
bool          gInstalled = false;

HRESULT STDMETHODCALLTYPE createGpsHook(ID3D12Device* d, D3D12_GRAPHICS_PIPELINE_STATE_DESC const* desc, REFIID riid, void** out) {
    HRESULT hr = reinterpret_cast<CreateGps_t>(gOrig[0])(d, desc, riid, out);
    if (SUCCEEDED(hr) && desc && out && *out && desc->PS.pShaderBytecode && desc->PS.BytecodeLength) {
        ++gPsoCreated;
        uint64_t h = fnv1a(desc->PS.pShaderBytecode, desc->PS.BytecodeLength);
        std::lock_guard l(gMutex);
        auto& s = gSeenPs[h];
        s.first = desc->PS.BytecodeLength;
        ++s.second;
        if (auto it = gUiHashes.find(h); it != gUiHashes.end()) {
            ID3D12PipelineState* pso = nullptr;
            if (SUCCEEDED(static_cast<IUnknown*>(*out)->QueryInterface(IID_PPV_ARGS(&pso))) && pso) {
                gUiPso[pso] = &it->second;
                pso->Release(); // only used as a key
                ++gPsoUi;
            }
        }
    }
    return hr;
}
HRESULT STDMETHODCALLTYPE createStreamPsoHook(ID3D12Device* d, void const* desc, REFIID riid, void** out) {
    ++gPsoStream; // pipeline-state streams aren't parsed in M0; a non-zero count means UI PSOs may be missed
    return reinterpret_cast<CreatePs_t>(gOrig[1])(d, desc, riid, out);
}
void STDMETHODCALLTYPE createRtvHook(ID3D12Device* d, ID3D12Resource* res, D3D12_RENDER_TARGET_VIEW_DESC const* desc,
                                     D3D12_CPU_DESCRIPTOR_HANDLE h) {
    reinterpret_cast<CreateRtv_t>(gOrig[2])(d, res, desc, h);
    std::lock_guard l(gMutex);
    if (res) gRtvRes[h.ptr] = res;
    else gRtvRes.erase(h.ptr);
}
void STDMETHODCALLTYPE setPsoHook(ID3D12GraphicsCommandList* list, ID3D12PipelineState* pso) {
    reinterpret_cast<SetPso_t>(gOrig[3])(list, pso);
    if (gState.load(std::memory_order_relaxed) == Idle) return;
    std::lock_guard l(gMutex);
    gListPso[list] = pso;
}
void STDMETHODCALLTYPE omSetHook(ID3D12GraphicsCommandList* list, UINT n, D3D12_CPU_DESCRIPTOR_HANDLE const* rts, BOOL single,
                                 D3D12_CPU_DESCRIPTOR_HANDLE const* ds) {
    reinterpret_cast<OmSet_t>(gOrig[4])(list, n, rts, single, ds);
    if (gState.load(std::memory_order_relaxed) == Idle) return;
    std::lock_guard l(gMutex);
    if (n > 0 && rts) gListRt0[list] = rts[0].ptr; // with single == TRUE rts[0] is still RT0
    else gListRt0.erase(list);
}
void STDMETHODCALLTYPE drawHook(ID3D12GraphicsCommandList* list, UINT a, UINT b, UINT c, UINT d) {
    if (gState.load(std::memory_order_relaxed) == Capturing) onDraw(list);
    reinterpret_cast<Draw_t>(gOrig[5])(list, a, b, c, d);
}
void STDMETHODCALLTYPE drawIdxHook(ID3D12GraphicsCommandList* list, UINT a, UINT b, UINT c, INT d, UINT e) {
    if (gState.load(std::memory_order_relaxed) == Capturing) onDraw(list);
    reinterpret_cast<DrawIdx_t>(gOrig[6])(list, a, b, c, d, e);
}

// ---------------------------------------------------------------- readback + files (render thread, at Present)
float half2f(uint16_t h) {
    uint32_t s = (h >> 15) & 1u, e = (h >> 10) & 31u, m = h & 1023u;
    float    v = e == 0 ? std::ldexp(float(m), -24) : e == 31 ? 65504.0f : std::ldexp(float(m | 1024u), int(e) - 25);
    return s ? -v : v;
}
float uf(uint32_t v, int mbits) {
    uint32_t e = (v >> mbits) & 31u, m = v & ((1u << mbits) - 1u);
    if (e == 0) return std::ldexp(float(m), -14 - mbits);
    if (e == 31) return 65504.0f;
    return std::ldexp(float(m | (1u << mbits)), int(e) - 15 - mbits);
}
uint8_t lin8(float x) { return uint8_t(std::lround(std::pow(std::clamp(x, 0.0f, 1.0f), 1.0f / 2.2f) * 255.0f)); }
uint8_t un8(float x) { return uint8_t(std::lround(std::clamp(x, 0.0f, 1.0f) * 255.0f)); }

// One row -> RGBA8. false = format not handled.
bool toRgba8(DXGI_FORMAT f, uint8_t const* row, UINT w, uint8_t* out) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: memcpy(out, row, size_t(w) * 4); return true;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
        for (UINT x = 0; x < w; ++x) {
            out[4 * x] = row[4 * x + 2]; out[4 * x + 1] = row[4 * x + 1]; out[4 * x + 2] = row[4 * x];
            out[4 * x + 3] = f == DXGI_FORMAT_B8G8R8X8_UNORM ? 255 : row[4 * x + 3];
        }
        return true;
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        for (UINT x = 0; x < w; ++x) {
            uint32_t v; memcpy(&v, row + 4 * x, 4);
            out[4 * x] = un8((v & 1023u) / 1023.0f); out[4 * x + 1] = un8(((v >> 10) & 1023u) / 1023.0f);
            out[4 * x + 2] = un8(((v >> 20) & 1023u) / 1023.0f); out[4 * x + 3] = un8((v >> 30) / 3.0f);
        }
        return true;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        for (UINT x = 0; x < w; ++x) {
            uint16_t c[4]; memcpy(c, row + 8 * x, 8);
            out[4 * x] = lin8(half2f(c[0])); out[4 * x + 1] = lin8(half2f(c[1])); out[4 * x + 2] = lin8(half2f(c[2]));
            out[4 * x + 3] = un8(half2f(c[3]));
        }
        return true;
    case DXGI_FORMAT_R11G11B10_FLOAT:
        for (UINT x = 0; x < w; ++x) {
            uint32_t v; memcpy(&v, row + 4 * x, 4);
            out[4 * x] = lin8(uf(v & 2047u, 6)); out[4 * x + 1] = lin8(uf((v >> 11) & 2047u, 6));
            out[4 * x + 2] = lin8(uf(v >> 22, 5)); out[4 * x + 3] = 255;
        }
        return true;
    default: return false;
    }
}

#pragma pack(push, 1)
struct BmpFile { uint16_t type; uint32_t size; uint16_t r1, r2; uint32_t off; };
struct BmpInfo { uint32_t size; int32_t w, h; uint16_t planes, bpp; uint32_t comp, img; int32_t xppm, yppm; uint32_t used, imp; };
#pragma pack(pop)

bool writeBmp(fs::path const& p, UINT w, UINT h, std::vector<uint8_t> const& rgba) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    uint32_t img = w * h * 4;
    BmpFile  fh{0x4D42, uint32_t(sizeof(BmpFile) + sizeof(BmpInfo)) + img, 0, 0, uint32_t(sizeof(BmpFile) + sizeof(BmpInfo))};
    BmpInfo  ih{uint32_t(sizeof(BmpInfo)), int32_t(w), -int32_t(h), 1, 32, 0, img, 2835, 2835, 0, 0}; // top-down BGRA
    f.write(reinterpret_cast<char const*>(&fh), sizeof fh);
    f.write(reinterpret_cast<char const*>(&ih), sizeof ih);
    std::vector<uint8_t> row(size_t(w) * 4);
    for (UINT y = 0; y < h; ++y) {
        uint8_t const* s = rgba.data() + size_t(y) * w * 4;
        for (UINT x = 0; x < w; ++x) { row[4 * x] = s[4 * x + 2]; row[4 * x + 1] = s[4 * x + 1]; row[4 * x + 2] = s[4 * x]; row[4 * x + 3] = s[4 * x + 3]; }
        f.write(reinterpret_cast<char const*>(row.data()), std::streamsize(row.size()));
    }
    return bool(f);
}

struct Readback {
    ComPtr<ID3D12Resource>             buf;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT                               rows = 0;
    UINT64                             rowBytes = 0, total = 0;
    D3D12_RESOURCE_DESC                desc{};
};

bool makeReadback(ID3D12Device* dev, D3D12_RESOURCE_DESC const& src, Readback& r) {
    r.desc               = src;
    D3D12_RESOURCE_DESC d = src;
    d.MipLevels = 1;
    d.DepthOrArraySize = 1;
    dev->GetCopyableFootprints(&d, 0, 1, 0, &r.fp, &r.rows, &r.rowBytes, &r.total);
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width            = r.total;
    bd.Height           = 1;
    bd.DepthOrArraySize = 1;
    bd.MipLevels        = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&r.buf)));
}

void copyToReadback(ID3D12GraphicsCommandList* list, ID3D12Resource* tex, Readback& r) {
    D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
    dst.pResource       = r.buf.Get();
    dst.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = r.fp;
    src.pResource        = tex;
    src.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
}

// RGBA8 image from a mapped readback; empty = unsupported format
std::vector<uint8_t> decode(Readback& r) {
    std::vector<uint8_t> img;
    void*                p = nullptr;
    D3D12_RANGE          range{0, SIZE_T(r.total)};
    if (FAILED(r.buf->Map(0, &range, &p)) || !p) return img;
    UINT w = UINT(r.desc.Width), h = r.desc.Height;
    img.resize(size_t(w) * h * 4);
    bool ok = true;
    for (UINT y = 0; y < h && ok; ++y)
        ok = toRgba8(r.desc.Format, static_cast<uint8_t const*>(p) + r.fp.Offset + size_t(y) * r.fp.Footprint.RowPitch, w, img.data() + size_t(y) * w * 4);
    D3D12_RANGE none{0, 0};
    r.buf->Unmap(0, &none);
    if (!ok) img.clear();
    return img;
}

std::string stamp() {
    std::time_t t = std::time(nullptr);
    std::tm     tm{};
    localtime_s(&tm, &t);
    char b[32];
    std::strftime(b, sizeof b, "%Y%m%d-%H%M%S", &tm);
    return b;
}

void finish(ID3D12Device* dev, ID3D12CommandQueue* q, ID3D12Resource* bb) {
    std::lock_guard l(gMutex);
    std::ostringstream rep;
    fs::path           dir = gOutDir / stamp();
    std::error_code    ec;
    fs::create_directories(dir, ec);
    rep << "Renderer Foundation - Spatial GUI M0 capture spike\n"
        << "UI shader hashes loaded: " << gUiHashes.size() << "\n"
        << "PSOs created (graphics desc): " << gPsoCreated.load() << ", tagged UI: " << gPsoUi.load()
        << ", created via pipeline-state stream (not inspected): " << gPsoStream.load() << "\n"
        << "RTVs tracked: " << gRtvRes.size() << "\n"
        << "Frames watched: " << gCaptureFrames << "\n"
        << "UI draws seen: " << gRes.uiDraws << " (render target unknown for " << gRes.uiDrawsNoTarget << ")\n"
        << "First UI draw material: " << (gRes.firstUi.empty() ? "-" : gRes.firstUi) << "\n"
        << "Distinct UI render targets: " << gRes.uiTargets.size() << "\n";
    bool anyBb = false;
    for (size_t i = 0; i < gRes.uiTargets.size(); ++i) {
        bool isBb = gRes.uiTargets[i] == bb;
        anyBb |= isBb;
        rep << "  [" << i << "] " << gRes.uiTargetDescs[i] << (isBb ? "  <- current BACK BUFFER" : "") << "\n";
    }
    rep << "Snapshot A (before first UI draw): " << (gRes.noteA.empty() ? "not taken" : gRes.noteA)
        << (gRes.srcA ? "  " + descStr(gRes.descA) + (gRes.srcA == bb ? " (back buffer)" : " (intermediate target)") : "") << "\n";

    // readback of A (if copied) and B (back buffer, before RF draws anything)
    ComPtr<ID3D12CommandAllocator>    alloc;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence>               fence;
    Readback                          ra, rb;
    bool haveA = gRes.snapA && gRes.noteA == "copied";
    bool ok    = SUCCEEDED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)))
          && SUCCEEDED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&list)))
          && SUCCEEDED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))
          && makeReadback(dev, bb->GetDesc(), rb) && (!haveA || makeReadback(dev, gRes.descA, ra));
    std::vector<uint8_t> imgA, imgB;
    if (ok) {
        D3D12_RESOURCE_BARRIER br[2]{};
        UINT                   nb = 0;
        br[nb].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        br[nb].Transition = {bb, 0, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE};
        ++nb;
        if (haveA) {
            br[nb].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            br[nb].Transition = {gRes.snapA.Get(), 0, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE};
            ++nb;
        }
        list->ResourceBarrier(nb, br);
        copyToReadback(list.Get(), bb, rb);
        if (haveA) copyToReadback(list.Get(), gRes.snapA.Get(), ra);
        std::swap(br[0].Transition.StateBefore, br[0].Transition.StateAfter);
        list->ResourceBarrier(1, br);
        list->Close();
        ID3D12CommandList* lists[] = {list.Get()};
        q->ExecuteCommandLists(1, lists);
        HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        q->Signal(fence.Get(), 1);
        bool done = fence->GetCompletedValue() >= 1;
        if (!done && ev && SUCCEEDED(fence->SetEventOnCompletion(1, ev))) done = WaitForSingleObject(ev, 2000) == WAIT_OBJECT_0;
        if (ev) CloseHandle(ev);
        if (!done) rep << "GPU readback timed out\n";
        else {
            imgB = decode(rb);
            if (haveA) imgA = decode(ra);
        }
    } else rep << "could not create readback resources\n";

    UINT w = UINT(rb.desc.Width), h = rb.desc.Height;
    rep << "Snapshot B (back buffer at Present): " << descStr(rb.desc) << (imgB.empty() ? "  NOT decoded" : "  written b.bmp") << "\n";
    if (!imgB.empty()) writeBmp(dir / "b.bmp", w, h, imgB);
    if (!imgA.empty()) {
        writeBmp(dir / "a.bmp", UINT(ra.desc.Width), ra.desc.Height, imgA);
        rep << "Snapshot A written a.bmp\n";
    } else if (haveA) rep << "Snapshot A NOT decoded (" << fmtName(ra.desc.Format) << ")\n";

    long changed = 0;
    UINT x0 = w, y0 = h, x1 = 0, y1 = 0;
    bool comparable = !imgA.empty() && !imgB.empty() && ra.desc.Width == rb.desc.Width && ra.desc.Height == rb.desc.Height;
    if (comparable) {
        std::vector<uint8_t> diff(imgB.size(), 0);
        for (UINT y = 0; y < h; ++y)
            for (UINT x = 0; x < w; ++x) {
                size_t i = (size_t(y) * w + x) * 4;
                int    m = 0;
                for (int c = 0; c < 3; ++c) m = std::max(m, std::abs(int(imgB[i + c]) - int(imgA[i + c])));
                if (m > 2) {
                    ++changed;
                    x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y);
                    // UI pixels in their own colour, rest black
                    diff[i] = imgB[i]; diff[i + 1] = imgB[i + 1]; diff[i + 2] = imgB[i + 2];
                }
                diff[i + 3] = 255;
            }
        writeBmp(dir / "diff.bmp", w, h, diff);
        rep << "Changed pixels B vs A: " << changed << " of " << (long(w) * long(h));
        if (changed) rep << ", bounding box " << x0 << "," << y0 << " - " << x1 << "," << y1;
        rep << "  (diff.bmp = B where it differs from A)\n";
    }

    if (gRes.uiDraws == 0)
        gVerdict = gPsoUi.load() == 0
                       ? "NO UI PIPELINES TAGGED - the game's pixel shaders don't match ui_shaders.txt (see seen_ps_hashes.txt"
                         + std::string(gPsoStream.load() ? "; it also builds PSOs from pipeline-state streams" : "") + ")"
                       : "no UI draw in the watched frames - open the inventory, then press F7";
    else if (!gRes.srcA) gVerdict = "UI draws seen but their render target is unknown (RTVs copied, not created?)";
    else if (comparable && changed > 0 && gRes.srcA == bb)
        gVerdict = "METHOD 1 VIABLE: UI draws straight into the back buffer; diff.bmp holds it (" + std::to_string(changed) + " px)";
    else if (gRes.srcA != bb)
        gVerdict = "UI draws into an INTERMEDIATE target (see list) - try method 2 on that target, or diff after its composite";
    else gVerdict = "UI target is the back buffer but no difference found (UI drawn earlier/later than tracked?)";
    rep << "VERDICT: " << gVerdict << "\n";

    std::ofstream(dir / "report.txt") << rep.str();
    {
        std::vector<std::pair<uint64_t, std::pair<size_t, uint32_t>>> seen(gSeenPs.begin(), gSeenPs.end());
        std::sort(seen.begin(), seen.end(), [](auto& a, auto& b) { return a.second.second > b.second.second; });
        std::ofstream s(dir / "seen_ps_hashes.txt");
        s << "# every pixel shader the game built a PSO with: <fnv1a64> <bytes> <psos> [ui tag]\n";
        for (auto& [k, v] : seen) {
            auto it = gUiHashes.find(k);
            s << hex64(k) << " " << v.first << " " << v.second << (it != gUiHashes.end() ? "  UI " + it->second : "") << "\n";
        }
    }
    gStatus = "done -> " + dir.string();
    ll_shim::info("[RF] UI capture: " + gVerdict + " (" + dir.string() + ")");
}

void resetResult() {
    std::lock_guard l(gMutex);
    gRes = Result{};
    gListRt0.clear();
    gListPso.clear();
    gTookA         = false;
    gCaptureFrames = 0;
    gVerdict.clear();
    gStatus = "watching render targets (1 frame)";
}
} // namespace

void init(fs::path const& configDir, fs::path const& outDir) {
    gConfigDir = configDir;
    gOutDir    = outDir;
    gUiHashes.clear();
    std::ifstream in(configDir / "ui_shaders.txt");
    std::string   line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto sp = line.find(' ');
        if (sp == std::string::npos) continue;
        try {
            gUiHashes[std::stoull(line.substr(0, sp), nullptr, 16)] = line.substr(sp + 1);
        } catch (...) {}
    }
}

bool install(Targets const& t) {
    if (gInstalled) return true;
    void* tg[kHooks]  = {t.createGraphicsPso, t.createStreamPso, t.createRtv, t.setPso, t.omSetRts, t.drawInstanced, t.drawIndexed};
    void* det[kHooks] = {reinterpret_cast<void*>(&createGpsHook), reinterpret_cast<void*>(&createStreamPsoHook),
                         reinterpret_cast<void*>(&createRtvHook), reinterpret_cast<void*>(&setPsoHook),
                         reinterpret_cast<void*>(&omSetHook),     reinterpret_cast<void*>(&drawHook),
                         reinterpret_cast<void*>(&drawIdxHook)};
    int ok = 0;
    for (int i = 0; i < kHooks; ++i) {
        if (!tg[i]) continue;
        gDet[i] = det[i];
        if (ll_shim::hook(tg[i], det[i], &gOrig[i]) == 0) { gTgt[i] = tg[i]; ++ok; }
        else ll_shim::warn("[RF] UI capture hook " + std::to_string(i) + " failed");
    }
    // without the create/draw hooks the spike can't work; keep whatever installed (they only pass through when idle)
    gInstalled = true;
    gStatus    = gUiHashes.empty() ? "config/ui_shaders.txt missing or empty" : "idle - open a screen and press F7";
    ll_shim::info("[RF] UI capture spike: " + std::to_string(ok) + "/" + std::to_string(kHooks) + " hooks, " +
                  std::to_string(gUiHashes.size()) + " UI shader hashes");
    return ok > 0;
}

void uninstall() {
    for (int i = 0; i < kHooks; ++i)
        if (gTgt[i]) ll_shim::unhook(gTgt[i], gDet[i]);
    gInstalled = false;
}

void request() {
    if (gInstalled) gRequest = true;
}

void beforeOverlay(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12Resource* bb) {
    if (!gInstalled || !device || !queue || !bb) return;
    int s = gState.load();
    if (s == Idle) {
        if (gRequest.exchange(false)) {
            resetResult();
            gState = Tracking; // this frame: learn which RT every list binds
        }
        return;
    }
    if (s == Tracking) {
        gState = Capturing;
        std::lock_guard l(gMutex);
        gStatus = "capturing";
        return;
    }
    // Capturing: the frame that just ended was watched
    ++gCaptureFrames;
    if (!gTookA && gCaptureFrames < 30) return; // no UI draw yet: keep watching up to 30 frames
    gState = Idle;
    finish(device, queue, bb);
}

void drawUi() {
    if (!ImGui::CollapsingHeader("Spatial GUI spike (M0)")) return;
    ImGui::Indent();
    if (!RfOptions::uiCaptureSpike) {
        ImGui::TextWrapped("Off. Set \"uiCaptureSpike\": true in RendererFoundation.json and restart the game.");
    } else {
        std::lock_guard l(gMutex);
        ImGui::Text("UI shader hashes: %zu   PSOs: %ld   UI PSOs: %ld   stream PSOs: %ld", gUiHashes.size(), gPsoCreated.load(),
                    gPsoUi.load(), gPsoStream.load());
        if (ImGui::Button("Capture next frame")) request();
        ImGui::SameLine();
        ImGui::TextDisabled("(or F7 with the inventory open)");
        ImGui::TextWrapped("Status: %s", gStatus.c_str());
        if (!gVerdict.empty()) ImGui::TextWrapped("Result: %s", gVerdict.c_str());
    }
    ImGui::Unindent();
}

} // namespace rf::uicapture
