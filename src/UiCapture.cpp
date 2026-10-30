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
#include <unordered_set>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d12.h>
#include <wrl/client.h>

#include <imgui.h>

#include "LLShim.h"
#include "Options.h"
#include "LyricsPass.h"
#include "SpatialPanel.h"

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

// M0.5: RTV handle resolution beyond CreateRenderTargetView
struct HeapInfo {
    ID3D12DescriptorHeap*      heap = nullptr; // not owned
    D3D12_DESCRIPTOR_HEAP_TYPE type{};
    SIZE_T                     start = 0;
    UINT                       count = 0, inc = 0, flags = 0, order = 0;
};
std::vector<HeapInfo>                                            gHeaps;      // every heap created while hooked
std::unordered_map<SIZE_T, ID3D12Resource*>                      gRtvGuess;   // handle -> resource guessed from barriers
std::unordered_map<ID3D12GraphicsCommandList*, ID3D12Resource*>  gListLastRt; // while watching: last resource barriered INTO render-target state
std::unordered_map<ID3D12GraphicsCommandList*, std::unordered_map<ID3D12Resource*, UINT>> gListResState; // while watching: last StateAfter per resource
std::atomic<long> gRtvCreated{0}, gRtvCopied{0}, gCopyCalls{0}, gHeapsRtv{0};
std::atomic<UINT> gRtvInc{0};
// M0.5b: handles made before RF hooked (the swap-chain RTVs) are learned at Present: during the tracking frames, a frame
// whose UI draws all used ONE unknown handle maps that handle to the back buffer presented at the end of that frame.
std::unordered_map<SIZE_T, long> gFrameUnknownUi; // this frame: unknown RT0 handle -> UI draws
long                             gFrameUiDraws = 0, gLearned = 0;
int                              gTrackFrames  = 0;
thread_local bool                gInSnapshot   = false; // our own barriers/copies inside snapshot() must not re-enter the hooks (gMutex is held)
std::vector<std::string>         gLearnLog;
// M1 panel redirect
std::unordered_set<SIZE_T>                                   gBbHandles;   // RTV handles known to be swap-chain buffers
std::unordered_map<ID3D12GraphicsCommandList*, SIZE_T>       gListDsv;     // list -> bound DSV (0 = none)
std::unordered_set<ID3D12GraphicsCommandList*>               gListOnPanel; // lists currently bound to the panel target
std::unordered_set<ID3D12GraphicsCommandList*>               gListUi;      // lists that drew UI this frame (their re-binds of a back buffer stay on the panel)
std::atomic<long>                                            gSparedBinds{0}; // back-buffer binds of lists with no UI this frame, left alone
std::atomic_bool                                             gFrameRedirect{false}, gLearnOnly{false};
bool tracking();
// Who receives the redirected UI this frame: the Spatial GUI panel / Cinematic scene or, when neither is up, the
// LyricsFlow "under the HUD" pass (spike S2, opt-in). Same mechanism, different target.
bool   redirectWanted() { return rf::panel::wantRedirect() || rf::lyrics::wantRedirect(); }
size_t redirectRtv() { return rf::panel::wantRedirect() ? rf::panel::targetRtv() : rf::lyrics::targetRtv(); }
void   redirectNoted() {
    if (rf::panel::wantRedirect()) rf::panel::noteRedirected();
    else rf::lyrics::noteRedirected();
}
void callOmSet(ID3D12GraphicsCommandList* list, UINT n, D3D12_CPU_DESCRIPTOR_HANDLE const* rts, BOOL single,
               D3D12_CPU_DESCRIPTOR_HANDLE const* ds);

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
    std::string                  srcAHow;            // "created" / "copied" / "barrier guess"
    long                         uiDrawsGuessed = 0; // UI draws resolved via the barrier guess
    std::vector<std::pair<SIZE_T, long>> unknown;    // distinct unknown RT0 handles -> UI draws
} gRes;

// "heap #3 RTV non-shader-visible start 0x.. count 64, slot 5" or why not
std::string heapOf(SIZE_T h) {
    for (auto const& hi : gHeaps)
        if (hi.inc && h >= hi.start && h < hi.start + SIZE_T(hi.count) * hi.inc) {
            static char const* kT[] = {"CBV_SRV_UAV", "SAMPLER", "RTV", "DSV"};
            SIZE_T off = h - hi.start;
            return "heap #" + std::to_string(hi.order) + " " + (int(hi.type) < 4 ? kT[hi.type] : "type?") +
                   ((hi.flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) ? " shader-visible" : "") + " start 0x" +
                   hex64(hi.start) + " count " + std::to_string(hi.count) + " inc " + std::to_string(hi.inc) + ", slot " +
                   std::to_string(off / hi.inc) + (off % hi.inc ? " (MISALIGNED +" + std::to_string(off % hi.inc) + ")" : "");
        }
    return "NOT in any heap created while hooked (heap made before RF's hooks?)";
}
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
bool safeSnapshotInner(ID3D12GraphicsCommandList* list, ID3D12Resource* rt) {
    __try {
        snapshot(list, rt);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gTookA = true;
        return false;
    }
}
bool safeSnapshot(ID3D12GraphicsCommandList* list, ID3D12Resource* rt) {
    gInSnapshot = true;
    bool ok     = safeSnapshotInner(list, rt);
    gInSnapshot = false;
    return ok;
}

std::atomic<long> gLeakUi{0}, gLeakOther{0}, gLeakFrames{0}, gLeakFrameNow{0}, gPanelUiDraws{0};
std::atomic<long> gLastFrameUiDraws{0}; // UI draws that went to the panel target in the last finished frame
long gDrawHist[256] = {}; int gDrawHistN = 0;
void onDraw(ID3D12GraphicsCommandList* list) {
    std::lock_guard l(gMutex);
    auto p = gListPso.find(list);
    bool isUi = p != gListPso.end() && gUiPso.count(p->second);
    // flicker diagnostics: a draw that reaches the screen while the UI is meant to go to the panel target
    if (rf::panel::enabled() && !gListOnPanel.count(list))
        if (auto lt = gListRt0.find(list); lt != gListRt0.end() && gBbHandles.count(lt->second)) {
            if (isUi && !rf::panel::wantRedirect()) { ++gLeakUi; gLeakFrameNow = 1; }
            else if (!isUi && gFrameRedirect.load(std::memory_order_relaxed)) { ++gLeakOther; gLeakFrameNow = 1; }
        }
    if (p == gListPso.end()) return;
    auto u = gUiPso.find(p->second);
    if (u == gUiPso.end()) return;
    // M1 panel: first UI draw on a back buffer -> clear the panel target and bind it for the rest of the frame
    if (redirectWanted() && !gListOnPanel.count(list))
        if (auto lt = gListRt0.find(list); lt != gListRt0.end() && gBbHandles.count(lt->second)) {
            D3D12_CPU_DESCRIPTOR_HANDLE h{redirectRtv()};
            if (!gFrameRedirect.exchange(true)) {
                gInSnapshot = true; // its barriers / copy go to our hooks too: they must not lock gMutex again
                rf::panel::beforeUiClear(list);
                gInSnapshot = false;
                float const clear[4] = {0, 0, 0, 0};
                list->ClearRenderTargetView(h, clear, 0, nullptr);
            }
            auto                        ds = gListDsv.find(list);
            D3D12_CPU_DESCRIPTOR_HANDLE d{ds != gListDsv.end() ? ds->second : 0};
            callOmSet(list, 1, &h, FALSE, d.ptr ? &d : nullptr);
            gListOnPanel.insert(list);
        }
    if (gListOnPanel.count(list)) { redirectNoted(); ++gPanelUiDraws; gListUi.insert(list); }
    if (gState.load(std::memory_order_relaxed) == Idle) return;
    if (gState.load(std::memory_order_relaxed) == Tracking) {
        ++gFrameUiDraws;
        if (auto lt = gListRt0.find(list); lt != gListRt0.end() && !gRtvRes.count(lt->second)) ++gFrameUnknownUi[lt->second];
        return;
    }
    ++gRes.uiDraws;
    ID3D12Resource* rt = nullptr;
    char const*     how = "created";
    auto            lt  = gListRt0.find(list);
    if (lt != gListRt0.end()) {
        if (auto r = gRtvRes.find(lt->second); r != gRtvRes.end()) rt = r->second;
        else if (auto g = gRtvGuess.find(lt->second); g != gRtvGuess.end()) {
            // only trust the guess while this list's last barrier left it in RENDER_TARGET state (snapshot relies on it)
            auto ls = gListResState.find(list);
            if (ls != gListResState.end())
                if (auto st = ls->second.find(g->second); st != ls->second.end() && (st->second & D3D12_RESOURCE_STATE_RENDER_TARGET)) {
                    rt  = g->second;
                    how = "barrier guess";
                    ++gRes.uiDrawsGuessed;
                }
        }
    }
    if (rt && std::string(how) == "barrier guess") { // report only: the M0.5 run showed guesses pick the wrong target
        if (std::find(gRes.uiTargets.begin(), gRes.uiTargets.end(), rt) == gRes.uiTargets.end()) {
            gRes.uiTargets.push_back(rt);
            gRes.uiTargetDescs.push_back(descStr(rt->GetDesc()) + "  first UI material: " + *u->second + "  (barrier GUESS, not copied)");
        }
        rt = nullptr;
    }
    if (!rt) {
        ++gRes.uiDrawsNoTarget;
        SIZE_T h  = lt != gListRt0.end() ? lt->second : 0;
        auto   it = std::find_if(gRes.unknown.begin(), gRes.unknown.end(), [&](auto& e) { return e.first == h; });
        if (it != gRes.unknown.end()) ++it->second;
        else if (gRes.unknown.size() < 16) gRes.unknown.push_back({h, 1});
        return;
    }
    if (std::find(gRes.uiTargets.begin(), gRes.uiTargets.end(), rt) == gRes.uiTargets.end()) {
        gRes.uiTargets.push_back(rt);
        gRes.uiTargetDescs.push_back(descStr(rt->GetDesc()) + "  first UI material: " + *u->second + "  (resolved: " + how + ")");
    }
    if (gTookA) return;
    gRes.firstUi = *u->second;
    gRes.srcAHow = how;
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
using CreateHeap_t = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, D3D12_DESCRIPTOR_HEAP_DESC const*, REFIID, void**);
using CopyDescs_t  = void(STDMETHODCALLTYPE*)(ID3D12Device*, UINT, D3D12_CPU_DESCRIPTOR_HANDLE const*, UINT const*, UINT,
                                             D3D12_CPU_DESCRIPTOR_HANDLE const*, UINT const*, D3D12_DESCRIPTOR_HEAP_TYPE);
using CopySimple_t = void(STDMETHODCALLTYPE*)(ID3D12Device*, UINT, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE,
                                             D3D12_DESCRIPTOR_HEAP_TYPE);
using Barrier_t    = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, D3D12_RESOURCE_BARRIER const*);
constexpr int kHooks = 11;
void*         gTgt[kHooks]{};
void*         gDet[kHooks]{};
void*         gOrig[kHooks]{};
bool          gInstalled = false;

bool tracking() { return gState.load(std::memory_order_relaxed) != Idle || rf::panel::enabled() || rf::lyrics::tracking(); }
void callOmSet(ID3D12GraphicsCommandList* list, UINT n, D3D12_CPU_DESCRIPTOR_HANDLE const* rts, BOOL single,
               D3D12_CPU_DESCRIPTOR_HANDLE const* ds) {
    reinterpret_cast<OmSet_t>(gOrig[4])(list, n, rts, single, ds);
}

// Spatial GUI M1 v0.12: UI pipelines blend colour with SRC_ALPHA / INV_SRC_ALPHA but leave the alpha channel to
// whatever the game set; drawn into the panel's transparent target that loses coverage (dark UI pixels end up nearly
// transparent and the world shows through: washed-out panel). Make UI pipelines accumulate alpha as "over"
// (ONE / INV_SRC_ALPHA) so the target holds proper premultiplied colour + coverage. The swap chain ignores alpha,
// so the flat UI looks the same.
std::atomic<long> gUiAlphaFixed{0};
HRESULT STDMETHODCALLTYPE createGpsHook(ID3D12Device* d, D3D12_GRAPHICS_PIPELINE_STATE_DESC const* desc, REFIID riid, void** out) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC patched;
    if (desc && desc->PS.pShaderBytecode && desc->PS.BytecodeLength) {
        uint64_t h    = fnv1a(desc->PS.pShaderBytecode, desc->PS.BytecodeLength);
        bool     isUi = false;
        {
            std::lock_guard l(gMutex);
            isUi = gUiHashes.count(h) != 0;
        }
        if (isUi) {
            patched     = *desc;
            bool change = false;
            UINT n      = patched.BlendState.IndependentBlendEnable ? 8u : 1u;
            for (UINT i = 0; i < n; ++i) {
                auto& rt = patched.BlendState.RenderTarget[i];
                if (!rt.BlendEnable || rt.LogicOpEnable) continue;
                rt.SrcBlendAlpha  = D3D12_BLEND_ONE;
                rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                rt.BlendOpAlpha   = D3D12_BLEND_OP_ADD;
                rt.RenderTargetWriteMask |= D3D12_COLOR_WRITE_ENABLE_ALPHA;
                change = true;
            }
            if (change) {
                desc = &patched;
                ++gUiAlphaFixed;
            }
        }
    }
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
    ++gRtvCreated;
    if (res) gRtvRes[h.ptr] = res;
    else gRtvRes.erase(h.ptr);
    gRtvGuess.erase(h.ptr);
}
void STDMETHODCALLTYPE setPsoHook(ID3D12GraphicsCommandList* list, ID3D12PipelineState* pso) {
    reinterpret_cast<SetPso_t>(gOrig[3])(list, pso);
    if (gInSnapshot || !tracking()) return;
    std::lock_guard l(gMutex);
    gListPso[list] = pso;
}
void STDMETHODCALLTYPE omSetHook(ID3D12GraphicsCommandList* list, UINT n, D3D12_CPU_DESCRIPTOR_HANDLE const* rts, BOOL single,
                                 D3D12_CPU_DESCRIPTOR_HANDLE const* ds) {
    if (gInSnapshot || !tracking()) { reinterpret_cast<OmSet_t>(gOrig[4])(list, n, rts, single, ds); return; }
    std::lock_guard l(gMutex);
    gListDsv[list] = ds ? ds->ptr : 0;
    // M1: once the frame's UI pass started, binds of a back buffer go to the panel target instead
    // (only lists that drew UI this frame: another thread may already record the next frame's world composite into a
    // back buffer while this frame's UI is under way - taking that to the panel target made the scene flicker)
    bool bbBind  = n > 0 && rts && gFrameRedirect.load(std::memory_order_relaxed) && redirectWanted() && gBbHandles.count(rts[0].ptr);
    bool toPanel = bbBind && gListUi.count(list);
    if (bbBind && !toPanel) ++gSparedBinds;
    if (toPanel) {
        D3D12_CPU_DESCRIPTOR_HANDLE h{redirectRtv()};
        reinterpret_cast<OmSet_t>(gOrig[4])(list, 1, &h, FALSE, ds);
        gListOnPanel.insert(list);
    } else {
        reinterpret_cast<OmSet_t>(gOrig[4])(list, n, rts, single, ds);
        gListOnPanel.erase(list);
    }
    if (n > 0 && rts) {
        SIZE_T h       = rts[0].ptr; // with single == TRUE rts[0] is still RT0
        gListRt0[list] = h;
        if (!gRtvRes.count(h))
            if (auto lr = gListLastRt.find(list); lr != gListLastRt.end() && lr->second) gRtvGuess[h] = lr->second;
    } else gListRt0.erase(list);
}
void STDMETHODCALLTYPE drawHook(ID3D12GraphicsCommandList* list, UINT a, UINT b, UINT c, UINT d) {
    if (!gInSnapshot && tracking()) onDraw(list);
    reinterpret_cast<Draw_t>(gOrig[5])(list, a, b, c, d);
}
void STDMETHODCALLTYPE drawIdxHook(ID3D12GraphicsCommandList* list, UINT a, UINT b, UINT c, INT d, UINT e) {
    if (!gInSnapshot && tracking()) onDraw(list);
    reinterpret_cast<DrawIdx_t>(gOrig[6])(list, a, b, c, d, e);
}

HRESULT STDMETHODCALLTYPE createHeapHook(ID3D12Device* d, D3D12_DESCRIPTOR_HEAP_DESC const* desc, REFIID riid, void** out) {
    HRESULT hr = reinterpret_cast<CreateHeap_t>(gOrig[7])(d, desc, riid, out);
    if (SUCCEEDED(hr) && desc && out && *out) {
        ID3D12DescriptorHeap* heap = nullptr;
        if (SUCCEEDED(static_cast<IUnknown*>(*out)->QueryInterface(IID_PPV_ARGS(&heap))) && heap) {
            HeapInfo hi;
            hi.heap  = heap;
            hi.type  = desc->Type;
            hi.start = heap->GetCPUDescriptorHandleForHeapStart().ptr;
            hi.count = desc->NumDescriptors;
            hi.inc   = d->GetDescriptorHandleIncrementSize(desc->Type);
            hi.flags = UINT(desc->Flags);
            heap->Release(); // only described
            if (desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV) ++gHeapsRtv;
            std::lock_guard l(gMutex);
            hi.order = UINT(gHeaps.size());
            // a new heap can reuse an old heap's address range: drop overlapping old entries
            gHeaps.erase(std::remove_if(gHeaps.begin(), gHeaps.end(), [&](HeapInfo const& o) {
                return o.inc && hi.inc && o.start < hi.start + SIZE_T(hi.count) * hi.inc && hi.start < o.start + SIZE_T(o.count) * o.inc;
            }), gHeaps.end());
            if (gHeaps.size() < 4096) gHeaps.push_back(hi);
        }
    }
    return hr;
}

// copy dst[i] <- src[i] in the handle -> resource maps (caller holds gMutex)
void propagate(SIZE_T dst, SIZE_T src) {
    if (auto r = gRtvRes.find(src); r != gRtvRes.end()) { gRtvRes[dst] = r->second; ++gRtvCopied; }
    else gRtvRes.erase(dst);
    gRtvGuess.erase(dst);
}
UINT rtvInc(ID3D12Device* d) {
    UINT v = gRtvInc.load(std::memory_order_relaxed);
    if (!v) gRtvInc = v = d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    return v;
}

void STDMETHODCALLTYPE copyDescsHook(ID3D12Device* d, UINT nDst, D3D12_CPU_DESCRIPTOR_HANDLE const* dst, UINT const* dstSz,
                                     UINT nSrc, D3D12_CPU_DESCRIPTOR_HANDLE const* src, UINT const* srcSz,
                                     D3D12_DESCRIPTOR_HEAP_TYPE type) {
    reinterpret_cast<CopyDescs_t>(gOrig[8])(d, nDst, dst, dstSz, nSrc, src, srcSz, type);
    if (type != D3D12_DESCRIPTOR_HEAP_TYPE_RTV || !dst || !src) return;
    ++gCopyCalls;
    UINT inc = rtvInc(d);
    std::lock_guard l(gMutex);
    // walk both range lists in lockstep (a null size array means ranges of 1)
    UINT di = 0, dOff = 0, si = 0, sOff = 0;
    while (di < nDst && si < nSrc) {
        UINT dN = dstSz ? dstSz[di] : 1, sN = srcSz ? srcSz[si] : 1;
        if (dOff >= dN) { ++di; dOff = 0; continue; }
        if (sOff >= sN) { ++si; sOff = 0; continue; }
        propagate(dst[di].ptr + SIZE_T(dOff) * inc, src[si].ptr + SIZE_T(sOff) * inc);
        ++dOff;
        ++sOff;
    }
}
void STDMETHODCALLTYPE copySimpleHook(ID3D12Device* d, UINT n, D3D12_CPU_DESCRIPTOR_HANDLE dst, D3D12_CPU_DESCRIPTOR_HANDLE src,
                                      D3D12_DESCRIPTOR_HEAP_TYPE type) {
    reinterpret_cast<CopySimple_t>(gOrig[9])(d, n, dst, src, type);
    if (type != D3D12_DESCRIPTOR_HEAP_TYPE_RTV) return;
    ++gCopyCalls;
    UINT inc = rtvInc(d);
    std::lock_guard l(gMutex);
    for (UINT i = 0; i < n; ++i) propagate(dst.ptr + SIZE_T(i) * inc, src.ptr + SIZE_T(i) * inc);
}
void STDMETHODCALLTYPE barrierHook(ID3D12GraphicsCommandList* list, UINT n, D3D12_RESOURCE_BARRIER const* b) {
    reinterpret_cast<Barrier_t>(gOrig[10])(list, n, b);
    if (gInSnapshot || gState.load(std::memory_order_relaxed) == Idle || !b) return;
    std::lock_guard l(gMutex);
    auto& st = gListResState[list];
    for (UINT i = 0; i < n; ++i) {
        if (b[i].Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION || !b[i].Transition.pResource) continue;
        UINT after                        = UINT(b[i].Transition.StateAfter);
        st[b[i].Transition.pResource]     = after;
        if (after & D3D12_RESOURCE_STATE_RENDER_TARGET) gListLastRt[list] = b[i].Transition.pResource;
    }
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
        << "UI pipelines with alpha fixed (v0.12): " << gUiAlphaFixed.load() << "\n"
        << "PSOs created (graphics desc): " << gPsoCreated.load() << ", tagged UI: " << gPsoUi.load()
        << ", created via pipeline-state stream (not inspected): " << gPsoStream.load() << "\n"
        << "RTVs tracked: " << gRtvRes.size() << " (CreateRenderTargetView calls " << gRtvCreated.load() << ", copied via CopyDescriptors* "
        << gRtvCopied.load() << " in " << gCopyCalls.load() << " RTV copy calls, barrier guesses " << gRtvGuess.size() << ")\n"
        << "Learned at Present (pre-hook handles -> back buffer): " << gLearned << "\n"
        << "Descriptor heaps seen: " << gHeaps.size() << " (RTV heaps created: " << gHeapsRtv.load() << ")\n"
        << "Frames watched: " << gCaptureFrames << "\n"
        << "UI draws seen: " << gRes.uiDraws << " (render target unknown for " << gRes.uiDrawsNoTarget << ", resolved by barrier guess "
        << gRes.uiDrawsGuessed << ")\n"
        << "First UI draw material: " << (gRes.firstUi.empty() ? "-" : gRes.firstUi) << "\n"
        << "Distinct UI render targets: " << gRes.uiTargets.size() << "\n";
    bool anyBb = false;
    for (size_t i = 0; i < gRes.uiTargets.size(); ++i) {
        bool isBb = gRes.uiTargets[i] == bb;
        anyBb |= isBb;
        rep << "  [" << i << "] " << gRes.uiTargetDescs[i] << (isBb ? "  <- current BACK BUFFER" : "") << "\n";
    }
    if (!gRes.unknown.empty()) {
        rep << "Unknown UI render-target handles (first = FIRST UNKNOWN):\n";
        for (auto& [h, n] : gRes.unknown) rep << "  0x" << hex64(h) << "  UI draws " << n << "  " << (h ? heapOf(h) : "no RT bound") << "\n";
    }
    rep << "Tracking frames:\n";
    for (auto& ln : gLearnLog) rep << "  " << ln << "\n";
    rep << "RTV heaps:\n";
    for (auto const& hi : gHeaps)
        if (hi.type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV)
            rep << "  #" << hi.order << " start 0x" << hex64(hi.start) << " count " << hi.count << " inc " << hi.inc << "\n";
    if (!gRes.srcAHow.empty()) rep << "Snapshot A target resolved by: " << gRes.srcAHow << "\n";
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
    gListLastRt.clear();
    gListResState.clear();
    gRtvGuess.clear();
    gFrameUnknownUi.clear();
    gFrameUiDraws = 0;
    gTrackFrames  = 0;
    gLearnLog.clear();
    gTookA         = false;
    gCaptureFrames = 0;
    gVerdict.clear();
    gStatus = "learning render targets (up to 12 frames)";
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
    void* tg[kHooks]  = {t.createGraphicsPso, t.createStreamPso, t.createRtv,  t.setPso,          t.omSetRts,     t.drawInstanced,
                         t.drawIndexed,       t.createHeap,      t.copyDescs, t.copyDescsSimple, t.resourceBarrier};
    void* det[kHooks] = {reinterpret_cast<void*>(&createGpsHook), reinterpret_cast<void*>(&createStreamPsoHook),
                         reinterpret_cast<void*>(&createRtvHook), reinterpret_cast<void*>(&setPsoHook),
                         reinterpret_cast<void*>(&omSetHook),     reinterpret_cast<void*>(&drawHook),
                         reinterpret_cast<void*>(&drawIdxHook),   reinterpret_cast<void*>(&createHeapHook),
                         reinterpret_cast<void*>(&copyDescsHook), reinterpret_cast<void*>(&copySimpleHook),
                         reinterpret_cast<void*>(&barrierHook)};
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

void writeText(char const* name, char const* text) {
    std::error_code ec;
    fs::create_directories(gOutDir, ec);
    std::ofstream(gOutDir / name) << text;
}

long uiAlphaFixedCount() { return gUiAlphaFixed.load(); }
void requestLearn() {
    if (!gInstalled) return;
    {
        std::lock_guard l(gMutex);
        if (!gBbHandles.empty()) return;
    }
    gLearnOnly = true;
    gRequest   = true;
}

void ownDraws(bool on) { gInSnapshot = on; }
long lastFrameUiDraws() { return gLastFrameUiDraws.load(); }
std::string leakNote() {
    char b[400];
    long mn = 1 << 30, mx = 0, zero = 0, low = 0;
    int  n  = std::min(gDrawHistN, 256);
    for (int i = 0; i < n; ++i) { mn = std::min(mn, gDrawHist[i]); mx = std::max(mx, gDrawHist[i]); }
    for (int i = 0; i < n; ++i) { if (gDrawHist[i] == 0) ++zero; else if (gDrawHist[i] * 10 < mx * 9) ++low; }
    snprintf(b, sizeof b, "UI draws to the screen while redirecting: %ld, other draws to the screen after the UI began: %ld, frames with either: %ld, back-buffer binds of non-UI lists left alone: %ld\n"
             "UI draws per frame (last %d frames): min %ld max %ld, frames with none %ld, frames under 90%% of the max %ld",
             gLeakUi.load(), gLeakOther.load(), gLeakFrames.load(), gSparedBinds.load(), n, n ? mn : 0, mx, zero, low);
    return b;
}
void beforeOverlay(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12Resource* bb) {
    if (!gInstalled || !device || !queue || !bb) return;
    if (gLeakFrameNow.exchange(0)) ++gLeakFrames;
    {
        long n = gPanelUiDraws.exchange(0);
        gLastFrameUiDraws = n;
        gDrawHist[gDrawHistN++ & 255] = n;
    }
    gFrameRedirect = false;
    {
        std::lock_guard l(gMutex);
        gListOnPanel.clear(); // the game re-binds its targets every frame
        gListUi.clear();
    }
    int s = gState.load();
    if (s == Idle) {
        if (gRequest.exchange(false)) {
            resetResult();
            gState = Tracking; // this frame: learn which RT every list binds
        }
        return;
    }
    if (s == Tracking) {
        std::lock_guard l(gMutex);
        ++gTrackFrames;
        std::string line = "frame " + std::to_string(gTrackFrames) + ": UI draws " + std::to_string(gFrameUiDraws) + ", unknown handles " +
                           std::to_string(gFrameUnknownUi.size());
        if (gFrameUnknownUi.size() == 1) {
            SIZE_T h   = gFrameUnknownUi.begin()->first;
            gRtvRes[h] = bb;
            gBbHandles.insert(h);
            ++gLearned;
            line += " -> learned 0x" + hex64(h) + " = back buffer " + descStr(bb->GetDesc());
        }
        if (gLearnLog.size() < 16) gLearnLog.push_back(line);
        bool clean = gFrameUiDraws > 0 && gFrameUnknownUi.empty();
        gFrameUnknownUi.clear();
        gFrameUiDraws = 0;
        if ((clean && gTrackFrames >= 2) || gTrackFrames >= 12) {
            if (gLearnOnly.exchange(false)) {
                gState  = Idle;
                gStatus = "learned " + std::to_string(gBbHandles.size()) + " back-buffer RTVs (panel)";
            } else {
                gState  = Capturing;
                gStatus = "capturing";
            }
        }
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
