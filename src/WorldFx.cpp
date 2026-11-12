// Renderer Foundation - World FX M0 world probe (see WorldFx.h). Original code for Renderer Foundation.
#include "WorldFx.h"
#include "SkyPass.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <imgui.h>

#include "GlassArmor.h"
#include "LLShim.h"
#include "LyricsCamera.h"
#include "MaterialLoader.h"
#include "UiCapture.h"

namespace rf::worldfx {
namespace {
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

// ---------------------------------------------------------------- tag test pack fingerprints ("WFX1")
// Bedrock Forge tools/WorldFx/make_tag_pack.py stamps every tagged texture; keep both sides in sync.
// A stamp = 4 texels in a row whose RGB low 3 bits spell a 9-bit code each (r<<6 | g<<3 | b):
//   top-left  (0..3, 0):        A=342 (5,2,6)  B=241 (3,6,1)  id  check   alpha kept      ("soft")
//   bottom-right (w-4..w-1, h-1): A=405 (6,2,5)  B=115 (1,6,3)  id  check   alpha forced 255 ("hard")
// check = (id * 37 + 11) & 511. Soft vs hard tells whether the game keeps RGB exact under alpha < 255.
constexpr int kTlA = 342, kTlB = 241, kBrA = 405, kBrB = 115;
constexpr int stampCheck(int id) { return (id * 37 + 11) & 511; }

enum Phase : int { Idle = 0, Warm = 1, Rec = 2, Wait = 3, Write = 4 };
std::atomic<int> gPhase{Idle};
int              gWarmFrames = 0, gWarmWait = 0, gWaitFrames = 0;
fs::path         gOutDir;
// The terrain atlas is always searched for our own stamps in memory, but its PICTURE is only written to disk when this
// is ticked (F6, not saved): on a server the atlas holds the server pack's textures, and saving those is the server's
// call, not ours (The Hive bans "asset downloaders"). Single-player test worlds only.
std::atomic_bool gSaveAtlas{false};
bool             gSaveAtlasRun = false; // latched when the probe is pressed
std::atomic<int> gKeepFrames{0};        // Presents until the last probe's readback buffers are released

std::mutex  gMutex; // everything below
std::string gStatus = "idle", gLastDir, gLastSummary;

struct Stat { long draws = 0; unsigned long long prims = 0; };
struct Rts { size_t rt0 = 0, dsv = 0; };
struct Cls {
    uint64_t    ps = 0;
    std::string name;  // pixel shader: "RenderChunk.material.bin Opaque Fragment Direct3D_SM65"; "?" = not one of the game's
    std::string vsName;
    std::string file;  // the material file the pipeline belongs to, from the pixel shader or else the vertex shader
    std::string label; // short text for the report
    std::string key;   // label + pipeline state: consecutive draws with the same key form one timeline run
    bool        terrain = false, ui = false, known = false;
    bool        psReplaced = false; // a shaderpack / resource pack replaced the pixel shader (the VS still names it)
    glassarmor::PsoView v;
};
using ClsPtr = std::shared_ptr<Cls const>;
std::unordered_map<ID3D12GraphicsCommandList*, ID3D12PipelineState*> gListPso;
std::unordered_map<ID3D12GraphicsCommandList*, Rts>                  gListRts;
std::unordered_map<ID3D12PipelineState*, ClsPtr>                     gCls;
std::unordered_map<ID3D12PipelineState*, Stat>                       gPsoStat;
long gDraws = 0, gUiDraws = 0, gTerrainDraws = 0, gUiBeforeTerrain = 0, gUnknownPso = 0, gTerrainNoTables = 0;

struct TexUse {
    ComPtr<ID3D12Resource> res;
    D3D12_RESOURCE_DESC    desc{};
    long                   draws = 0;
    std::string            slots; // first few "r<root>.<k>" places it was bound at
};
std::unordered_map<ID3D12Resource*, TexUse> gTex;
struct TgtUse {
    D3D12_RESOURCE_DESC desc{}; // no reference kept: a held swap-chain buffer would make the game's ResizeBuffers fail
    long                draws = 0;
};
std::unordered_map<ID3D12Resource*, TgtUse> gRt, gDs;
ID3D12Resource*  gLastRt = nullptr; // RT0 / depth of the last terrain draw (in use this frame, not referenced)
ID3D12Resource*  gLastDs = nullptr;
std::atomic_bool gBoundary{false};
std::string      gBoundaryText;

// Frame timeline: runs of consecutive draws with the same shader + state + targets, per command list, numbered in the
// order they began. It shows where the world ends (hand pass, the game's blit, the HUD) - the hook points World FX needs.
struct Run {
    ID3D12PipelineState* pso = nullptr;
    std::string          key;
    Rts                  rts;
    long                 draws = 0;
    unsigned long long   prims = 0;
    uint64_t             seq = 0;
    unsigned             tid = 0;
};
struct Ev {
    uint64_t                   seq = 0;
    ID3D12GraphicsCommandList* list = nullptr;
    unsigned                   tid = 0;
    Rts                        rts;
    std::string                text;
};
std::unordered_map<ID3D12GraphicsCommandList*, Run> gRun;
std::vector<Ev>                                     gTimeline;
uint64_t                                            gSeq = 0;
long                                                gTimelineDropped = 0;
constexpr size_t                                    kMaxTimeline = 700;
// Stage snapshots: colour + depth right before the first draw of a material family that follows the terrain. The hand
// pass and the game's own blit always get one; up to kMaxOtherStages other families show how the frame builds up.
std::vector<std::string> gStageFamilies;
int                      gStagesOther = 0;
std::string              gStageText;
constexpr int            kMaxOtherStages = 3;

struct Copy {
    std::string                        name, note;
    ComPtr<ID3D12Resource>             buf; // ours (readback heap); the game resource is not kept
    D3D12_RESOURCE_DESC                desc{};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT                               rows = 0;
    UINT64                             rowBytes = 0, total = 0;
    bool                               atlas = false;
    int                                slice = 0;
};
std::vector<Copy> gCopies;
std::vector<Copy> gKeep; // last probe's readback buffers: released when the next probe starts (GPU long done by then)
std::string       gReport;
fs::path          gRunDir;

ComPtr<ID3D12Fence>               gFence;
UINT64                            gFenceVal = 0, gWaitFor = 0;
ComPtr<ID3D12CommandAllocator>    gAlloc;
ComPtr<ID3D12GraphicsCommandList> gList;

// ---------------------------------------------------------------- helpers
std::string hex(uint64_t v) {
    char b[24];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
    return b;
}
std::string ptr(void const* p) {
    char b[24];
    std::snprintf(b, sizeof b, "%p", p);
    return b;
}
std::string fmtStr(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_UNKNOWN: return "UNKNOWN";
    case DXGI_FORMAT_R8G8B8A8_UNORM: return "RGBA8";
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "RGBA8_SRGB";
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return "RGBA8_TYPELESS";
    case DXGI_FORMAT_B8G8R8A8_UNORM: return "BGRA8";
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return "BGRA8_SRGB";
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return "BGRA8_TYPELESS";
    case DXGI_FORMAT_B8G8R8X8_UNORM: return "BGRX8";
    case DXGI_FORMAT_R10G10B10A2_UNORM: return "RGB10A2";
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return "RGB10A2_TYPELESS";
    case DXGI_FORMAT_R11G11B10_FLOAT: return "R11G11B10F";
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return "RGBA16F";
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return "RGBA16_TYPELESS";
    case DXGI_FORMAT_R32G32B32A32_FLOAT: return "RGBA32F";
    case DXGI_FORMAT_D24_UNORM_S8_UINT: return "D24S8";
    case DXGI_FORMAT_R24G8_TYPELESS: return "R24G8_TYPELESS";
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: return "R24X8";
    case DXGI_FORMAT_D32_FLOAT: return "D32F";
    case DXGI_FORMAT_R32_TYPELESS: return "R32_TYPELESS";
    case DXGI_FORMAT_R32_FLOAT: return "R32F";
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return "D32S8";
    case DXGI_FORMAT_R32G8X24_TYPELESS: return "R32G8X24_TYPELESS";
    case DXGI_FORMAT_D16_UNORM: return "D16";
    case DXGI_FORMAT_BC1_UNORM: return "BC1";
    case DXGI_FORMAT_BC3_UNORM: return "BC3";
    case DXGI_FORMAT_BC7_UNORM: return "BC7";
    case DXGI_FORMAT_BC7_UNORM_SRGB: return "BC7_SRGB";
    default: return "fmt" + std::to_string(int(f));
    }
}
std::string descStr(D3D12_RESOURCE_DESC const& d) {
    std::string s = std::to_string(d.Width) + "x" + std::to_string(d.Height) + " " + fmtStr(d.Format) + " mips " +
                    std::to_string(d.MipLevels) + " array " + std::to_string(d.DepthOrArraySize);
    if (d.SampleDesc.Count > 1) s += " MSAA x" + std::to_string(d.SampleDesc.Count);
    if (d.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) s += " (not 2D: dim " + std::to_string(int(d.Dimension)) + ")";
    return s;
}
enum class Layout { Rgba8, Bgra8, Rgb10a2, Rgba16f, R11g11b10f, Depth24, Depth32f, Other };
Layout layoutOf(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return Layout::Rgba8;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM: return Layout::Bgra8;
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return Layout::Rgb10a2;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return Layout::Rgba16f;
    case DXGI_FORMAT_R11G11B10_FLOAT: return Layout::R11g11b10f;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: return Layout::Depth24;
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32G8X24_TYPELESS: return Layout::Depth32f;
    default: return Layout::Other;
    }
}
char const* layoutName(Layout l) {
    switch (l) {
    case Layout::Rgba8: return "rgba8";
    case Layout::Bgra8: return "bgra8";
    case Layout::Rgb10a2: return "rgb10a2";
    case Layout::Rgba16f: return "rgba16f";
    case Layout::R11g11b10f: return "r11g11b10f";
    case Layout::Depth24: return "depth24";
    case Layout::Depth32f: return "depth32f";
    default: return "other";
    }
}
bool isTerrain(std::string const& n) { return n.find("RenderChunk") != std::string::npos; }
bool looksUi(std::string const& n) { return n.rfind("UI", 0) == 0 || n.find("Gameface") != std::string::npos; }

// A pointer from the trackers may be stale (descriptor reused, resource freed): touch it under SEH only.
bool safeDescInner(ID3D12Resource* r, D3D12_RESOURCE_DESC* out) {
    __try {
        *out = r->GetDesc();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool safeAddRefInner(ID3D12Resource* r) {
    __try {
        r->AddRef();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
/// Hold a resource the game is using right now (ComPtr takes the reference safeAddRef made).
bool hold(ID3D12Resource* r, ComPtr<ID3D12Resource>& out, D3D12_RESOURCE_DESC& desc) {
    if (!r || !safeDescInner(r, &desc) || !safeAddRefInner(r)) return false;
    out.Attach(r);
    return true;
}
void transition(ID3D12GraphicsCommandList* l, ID3D12Resource* r, unsigned before, unsigned after) {
    if (before == after) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATES(before);
    b.Transition.StateAfter  = D3D12_RESOURCE_STATES(after);
    l->ResourceBarrier(1, &b);
}
bool copyInner(ID3D12GraphicsCommandList* l, ID3D12Resource* src, UINT sub, ID3D12Resource* dst,
               D3D12_PLACED_SUBRESOURCE_FOOTPRINT const* fp, unsigned before, bool barrier) {
    __try {
        if (barrier) transition(l, src, before, unsigned(D3D12_RESOURCE_STATE_COPY_SOURCE));
        D3D12_TEXTURE_COPY_LOCATION d{}, s{};
        d.pResource        = dst;
        d.Type             = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        d.PlacedFootprint  = *fp;
        s.pResource        = src;
        s.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        s.SubresourceIndex = sub;
        l->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
        if (barrier) transition(l, src, unsigned(D3D12_RESOURCE_STATE_COPY_SOURCE), before);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
/// Copy one subresource of `src` into a new readback buffer, recorded on `l`. `before` = the state `src` is in;
/// barrier = false: no transition (state unknown -> COMMON, implicit promotion, like Glass Armor's readbacks).
bool queueCopy(ID3D12Device* dev, ID3D12GraphicsCommandList* l, ComPtr<ID3D12Resource> const& src, D3D12_RESOURCE_DESC const& desc,
               UINT sub, unsigned before, bool barrier, Copy& out) {
    out.desc = desc;
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) { out.note = "not a 2D texture"; return false; }
    if (desc.SampleDesc.Count > 1) { out.note = "MSAA target, not copied"; return false; }
    dev->GetCopyableFootprints(&desc, sub, 1, 0, &out.fp, &out.rows, &out.rowBytes, &out.total);
    if (!out.total || out.total > (UINT64(768) << 20)) { out.note = "footprint empty or over 768 MB"; return false; }
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC   bd{};
    bd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width            = out.total;
    bd.Height           = 1;
    bd.DepthOrArraySize = 1;
    bd.MipLevels        = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&out.buf)))) {
        out.note = "readback buffer failed";
        return false;
    }
    if (!copyInner(l, src.Get(), sub, out.buf.Get(), &out.fp, before, barrier)) {
        out.note = "fault while recording the copy (SEH)";
        out.buf.Reset();
        return false;
    }
    char b[96];
    std::snprintf(b, sizeof b, "queued (state before 0x%x%s)", before, barrier ? "" : ", no barrier");
    out.note = b;
    return true;
}

// ---------------------------------------------------------------- per-draw classification
std::string stripPlatform(std::string n) { // "X.material.bin Opaque Fragment Direct3D_SM65" -> "X.material.bin Opaque Fragment"
    auto p = n.rfind(" Direct3D");
    return p == std::string::npos ? n : n.substr(0, p);
}
ClsPtr classify(ID3D12PipelineState* pso) {
    auto c = std::make_shared<Cls>();
    c->name = c->vsName = "?";
    if (glassarmor::psoView(pso, c->v)) {
        c->known  = true;
        c->ps     = c->v.ps;
        c->name   = glassarmor::shaderName(c->ps);
        c->vsName = glassarmor::shaderName(c->v.vs);
    }
    // A shaderpack or resource pack that replaces a shader changes its hash, so the game's own material files no
    // longer name it (LyricsLight replaces RenderChunk's pixel and vertex shaders). MaterialLoader names the shaders
    // of every material it serves (" [shaderpack]" / " [pack]"); failing that, an unreplaced vertex shader still
    // says which material the pipeline belongs to.
    std::string const& fam = c->name != "?" ? c->name : c->vsName;
    c->psReplaced = c->known && (c->name.find(" [") != std::string::npos || (c->name == "?" && c->vsName != "?"));
    c->file       = fam == "?" ? std::string() : fam.substr(0, fam.find(' '));
    c->terrain    = isTerrain(fam);
    c->ui         = uicapture::isUiPipeline(pso) || looksUi(c->name) || looksUi(c->vsName);
    if (!c->known) c->label = "(pipeline made before the hooks)";
    else if (c->name != "?") c->label = stripPlatform(c->name) + (c->psReplaced ? " (REPLACED by a pack)" : "") + "  ps " + hex(c->ps);
    else if (c->vsName != "?") c->label = stripPlatform(c->vsName) + " + REPLACED pixel shader  ps " + hex(c->ps);
    else c->label = "unnamed shaders  ps " + hex(c->ps);
    c->key = c->label + "  | " + c->v.targets;
    return c;
}

// ---- timeline (gMutex held)
void closeRun(ID3D12GraphicsCommandList* list, Run const& r) {
    if (r.draws <= 0) return;
    if (gTimeline.size() >= kMaxTimeline) { ++gTimelineDropped; return; }
    Ev e;
    e.seq = r.seq, e.list = list, e.tid = r.tid, e.rts = r.rts;
    e.text = "x" + std::to_string(r.draws) + " (" + std::to_string(r.prims) + " tris)  " + r.key;
    gTimeline.push_back(std::move(e));
}
void noteRun(ID3D12GraphicsCommandList* list, ID3D12PipelineState* pso, Cls const& c, Rts rts, unsigned long long prims) {
    auto&      r    = gRun[list];
    bool const same = r.draws > 0 && r.rts.rt0 == rts.rt0 && r.rts.dsv == rts.dsv && (r.pso == pso || r.key == c.key);
    if (!same) {
        closeRun(list, r);
        r     = Run{};
        r.key = c.key, r.rts = rts, r.seq = ++gSeq, r.tid = GetCurrentThreadId();
    }
    r.pso = pso;
    ++r.draws;
    r.prims += prims;
}
/// 0 = no snapshot for this material file, 1 = always (the likely "world is finished" moments), 2 = while budget is left.
int stageKind(std::string const& file) {
    if (file.empty()) return 0;
    if (file.rfind("ItemInHand", 0) == 0 || file.rfind("Blit", 0) == 0) return 1;
    return 2;
}
std::string stageFamily(std::string const& file) { // ItemInHandColor / ItemInHandColorGlint / ... are one family
    if (file.rfind("ItemInHand", 0) == 0) return "ItemInHand";
    return file.substr(0, file.find('.'));
}

void noteTextures(ID3D12GraphicsCommandList* list) {
    auto tables = uicapture::rootTables(list);
    if (tables.empty()) {
        std::lock_guard l(gMutex);
        ++gTerrainNoTables;
        return;
    }
    struct Seen { ID3D12Resource* r; std::string slot; };
    std::vector<Seen> seen;
    for (auto const& [ri, gpu] : tables) {
        size_t   cpu = 0;
        unsigned inc = 0;
        if (!uicapture::gpuDescToCpu(gpu, cpu, inc)) continue;
        for (int k = 0; k < 8; ++k) {
            glassarmor::SrvView v;
            if (glassarmor::srvView(cpu + size_t(k) * inc, v) && v.res && v.w >= 16 && v.h >= 16)
                seen.push_back({v.res, "r" + std::to_string(ri) + "." + std::to_string(k)});
        }
    }
    for (auto const& s : seen) {
        bool fresh = false;
        {
            std::lock_guard l(gMutex);
            auto it = gTex.find(s.r);
            if (it != gTex.end()) {
                ++it->second.draws;
                if (it->second.slots.find(s.slot) == std::string::npos && it->second.slots.size() < 60) it->second.slots += " " + s.slot;
                continue;
            }
            fresh = true;
        }
        if (!fresh) continue;
        TexUse u;
        if (!hold(s.r, u.res, u.desc)) continue;
        u.draws = 1;
        u.slots = s.slot;
        std::lock_guard l(gMutex);
        if (!gTex.count(s.r) && gTex.size() < 256) gTex.emplace(s.r, std::move(u));
    }
}

void noteTarget(std::unordered_map<ID3D12Resource*, TgtUse>& m, ID3D12Resource* r, ID3D12Resource*& last) {
    if (!r) return;
    {
        std::lock_guard l(gMutex);
        auto it = m.find(r);
        if (it != m.end()) { ++it->second.draws; last = r; return; }
    }
    TgtUse u;
    if (!safeDescInner(r, &u.desc)) return;
    u.draws = 1;
    std::lock_guard l(gMutex);
    if (!m.count(r)) m.emplace(r, std::move(u));
    last = r;
}

/// First UI draw after the world: copy what the world left behind (colour + depth) right before the UI draws.
void boundary(ID3D12GraphicsCommandList* list, Cls const& c) {
    Rts             rts;
    ID3D12Resource* lastRt = nullptr;
    ID3D12Resource* lastDs = nullptr;
    {
        std::lock_guard l(gMutex);
        if (auto it = gListRts.find(list); it != gListRts.end()) rts = it->second;
        lastRt = gLastRt, lastDs = gLastDs;
    }
    // references only while the copies are recorded (all three are in use by this very frame)
    ComPtr<ID3D12Resource> worldRt, worldDs;
    D3D12_RESOURCE_DESC    worldRtDesc{}, worldDsDesc{};
    if (lastRt && !hold(lastRt, worldRt, worldRtDesc)) worldRt.Reset();
    if (lastDs && !hold(lastDs, worldDs, worldDsDesc)) worldDs.Reset();
    std::ostringstream s;
    s << "first UI draw after the world: " << c.label << "\n";
    ComPtr<ID3D12Device> dev;
    if (FAILED(list->GetDevice(IID_PPV_ARGS(&dev)))) {
        s << "  no device - nothing copied\n";
        std::lock_guard l(gMutex);
        gBoundaryText = s.str();
        return;
    }
    std::vector<Copy> made;
    // 1) the target the UI is about to draw into: holds the finished world image (no HUD yet)
    ComPtr<ID3D12Resource> uiRt;
    D3D12_RESOURCE_DESC    uiDesc{};
    ID3D12Resource*        uiRaw = rts.rt0 ? uicapture::rtvResourceOf(rts.rt0) : nullptr;
    if (uiRaw && hold(uiRaw, uiRt, uiDesc)) {
        s << "  UI target: " << descStr(uiDesc) << "  " << ptr(uiRaw) << "\n";
        Copy cp;
        cp.name = "colour_at_ui";
        queueCopy(dev.Get(), list, uiRt, uiDesc, 0, unsigned(D3D12_RESOURCE_STATE_RENDER_TARGET), true, cp);
        made.push_back(std::move(cp));
    } else s << "  UI target: unknown RTV handle 0x" << hex(rts.rt0) << " - not copied\n";
    s << "  UI depth binding: " << (rts.dsv ? "0x" + hex(rts.dsv) : std::string("none")) << "\n";
    // 2) the world's own colour target, when the world was drawn somewhere else first (state known only)
    if (worldRt) {
        s << "  world colour target (last terrain draw): " << descStr(worldRtDesc) << "  " << ptr(worldRt.Get())
          << (worldRt.Get() == uiRaw ? "  = the UI target\n" : "\n");
        if (worldRt.Get() != uiRaw) {
            unsigned st = glassarmor::stateOf(worldRt.Get(), 0xFFFFFFFFu);
            Copy     cp;
            cp.name = "world_colour";
            if (st == 0xFFFFFFFFu) cp.note = "state unknown - not copied";
            else queueCopy(dev.Get(), list, worldRt, worldRtDesc, 0, st, true, cp);
            made.push_back(std::move(cp));
        }
    } else s << "  world colour target: none seen (no terrain draw with a known RTV)\n";
    // 3) the world's depth buffer (cel outlines, refraction depth test)
    if (worldDs) {
        unsigned st = glassarmor::stateOf(worldDs.Get(), unsigned(D3D12_RESOURCE_STATE_DEPTH_WRITE));
        s << "  world depth (last terrain draw): " << descStr(worldDsDesc) << "  " << ptr(worldDs.Get()) << "  state 0x" << std::hex << st
          << std::dec << "\n";
        Copy cp;
        cp.name = "world_depth";
        queueCopy(dev.Get(), list, worldDs, worldDsDesc, 0, st, true, cp);
        made.push_back(std::move(cp));
    } else s << "  world depth: none seen (no terrain draw with a known DSV)\n";
    std::lock_guard l(gMutex);
    gBoundaryText = s.str();
    for (auto& m : made) gCopies.push_back(std::move(m));
}

/// Colour + depth bound right now, copied before the first draw of a material family that follows the terrain.
/// Comparing the stages shows how far the world's depth survives (does the hand pass clear it?) and what the game's
/// blit reads - so the World FX pass can be placed where the world is complete and its depth still intact.
void snapshotStage(ID3D12GraphicsCommandList* list, Cls const& c, Rts rts, int n, std::string const& family) {
    std::string const  base = "stage" + std::to_string(n) + "_before_" + family;
    std::ostringstream s;
    s << base << ": first draw of " << c.label << " after the terrain began\n";
    std::vector<Copy>    made;
    ComPtr<ID3D12Device> dev;
    if (SUCCEEDED(list->GetDevice(IID_PPV_ARGS(&dev)))) {
        ComPtr<ID3D12Resource> rt, ds;
        D3D12_RESOURCE_DESC    rd{}, dd{};
        ID3D12Resource*        rtRaw = rts.rt0 ? uicapture::rtvResourceOf(rts.rt0) : nullptr;
        ID3D12Resource*        dsRaw = rts.dsv ? glassarmor::dsvResource(rts.dsv) : nullptr;
        if (rtRaw && hold(rtRaw, rt, rd)) { // bound and about to be drawn into: it is in RENDER_TARGET state
            s << "  colour bound: " << descStr(rd) << "  " << ptr(rtRaw) << "\n";
            Copy cp;
            cp.name = base + "_colour";
            queueCopy(dev.Get(), list, rt, rd, 0, unsigned(D3D12_RESOURCE_STATE_RENDER_TARGET), true, cp);
            made.push_back(std::move(cp));
        } else s << "  colour: " << (rts.rt0 ? "unknown RTV handle 0x" + hex(rts.rt0) : std::string("none bound")) << " - not copied\n";
        if (dsRaw && hold(dsRaw, ds, dd)) {
            unsigned st = glassarmor::stateOf(dsRaw, unsigned(D3D12_RESOURCE_STATE_DEPTH_WRITE));
            s << "  depth bound: " << descStr(dd) << "  " << ptr(dsRaw) << "  state 0x" << std::hex << st << std::dec << "\n";
            Copy cp;
            cp.name = base + "_depth";
            queueCopy(dev.Get(), list, ds, dd, 0, st, true, cp);
            made.push_back(std::move(cp));
        } else s << "  depth: " << (rts.dsv ? "unknown DSV handle 0x" + hex(rts.dsv) : std::string("none bound")) << " - not copied\n";
    } else s << "  no device - nothing copied\n";
    std::lock_guard l(gMutex);
    gStageText += s.str();
    for (auto& m : made) gCopies.push_back(std::move(m));
}

// ---------------------------------------------------------------- files (worker thread)
float half2f(uint16_t h) {
    uint32_t sgn = (h >> 15) & 1u, e = (h >> 10) & 31u, m = h & 1023u;
    float    v = e == 0 ? std::ldexp(float(m), -24) : e == 31 ? 65504.0f : std::ldexp(float(m | 1024u), int(e) - 25);
    return sgn ? -v : v;
}
float smallFloat(uint32_t bits, int mBits) {
    uint32_t m = bits & ((1u << mBits) - 1u), e = (bits >> mBits) & 31u;
    if (e == 0) return std::ldexp(float(m), -14 - mBits);
    if (e == 31) return 65000.f;
    return std::ldexp(float(m | (1u << mBits)), int(e) - 15 - mBits);
}
uint8_t toByte(float v) {
    v = v / (1.f + v); // Reinhard, previews only
    v = std::pow(std::clamp(v, 0.f, 1.f), 1.f / 2.2f);
    return uint8_t(std::lround(v * 255.f));
}

/// RGB preview of one texel (row pointer + x).
void texel(Layout L, uint8_t const* row, unsigned x, uint8_t rgb[3]) {
    switch (L) {
    case Layout::Rgba8: rgb[0] = row[x * 4], rgb[1] = row[x * 4 + 1], rgb[2] = row[x * 4 + 2]; break;
    case Layout::Bgra8: rgb[0] = row[x * 4 + 2], rgb[1] = row[x * 4 + 1], rgb[2] = row[x * 4]; break;
    case Layout::Rgb10a2: {
        uint32_t v;
        std::memcpy(&v, row + x * 4, 4);
        rgb[0] = uint8_t((v & 1023u) >> 2), rgb[1] = uint8_t(((v >> 10) & 1023u) >> 2), rgb[2] = uint8_t(((v >> 20) & 1023u) >> 2);
        break;
    }
    case Layout::Rgba16f: {
        uint16_t h[3];
        std::memcpy(h, row + x * 8, 6);
        for (int k = 0; k < 3; ++k) rgb[k] = toByte(std::max(half2f(h[k]), 0.f));
        break;
    }
    case Layout::R11g11b10f: {
        uint32_t v;
        std::memcpy(&v, row + x * 4, 4);
        rgb[0] = toByte(smallFloat(v & 2047u, 6)), rgb[1] = toByte(smallFloat((v >> 11) & 2047u, 6)), rgb[2] = toByte(smallFloat((v >> 22) & 1023u, 5));
        break;
    }
    default: rgb[0] = rgb[1] = rgb[2] = 0; break;
    }
}

bool writeBmp(fs::path const& p, unsigned w, unsigned h, std::vector<uint8_t> const& rgb) { // rgb: top-down rows, w*3 each
    unsigned const rowSz = (w * 3 + 3) & ~3u;
    uint32_t const size  = 54 + rowSz * h;
    std::ofstream  f(p, std::ios::binary);
    if (!f) return false;
    uint8_t hd[54]{'B', 'M'};
    auto put32 = [&](int at, uint32_t v) { std::memcpy(hd + at, &v, 4); };
    put32(2, size), put32(10, 54), put32(14, 40), put32(18, w), put32(22, h);
    hd[26] = 1, hd[28] = 24;
    put32(34, rowSz * h);
    f.write(reinterpret_cast<char const*>(hd), 54);
    std::vector<uint8_t> line(rowSz, 0);
    for (unsigned y = 0; y < h; ++y) {
        uint8_t const* src = rgb.data() + size_t(h - 1 - y) * w * 3; // BMP rows go bottom-up
        for (unsigned x = 0; x < w; ++x) line[x * 3] = src[x * 3 + 2], line[x * 3 + 1] = src[x * 3 + 1], line[x * 3 + 2] = src[x * 3];
        f.write(reinterpret_cast<char const*>(line.data()), rowSz);
    }
    return bool(f);
}

struct Hit { int id = 0, x = 0, y = 0, slice = 0; bool br = false; int a[4]{}; };
std::vector<Hit> findStamps(uint8_t const* data, UINT64 pitch, unsigned w, unsigned h, bool bgra, int slice) {
    std::vector<Hit> hits;
    std::vector<int> code(w);
    for (unsigned y = 0; y < h && hits.size() < 4000; ++y) {
        uint8_t const* row = data + size_t(y) * pitch;
        for (unsigned x = 0; x < w; ++x) {
            uint8_t const* t = row + x * 4;
            int r = bgra ? t[2] : t[0], g = t[1], b = bgra ? t[0] : t[2];
            code[x] = ((r & 7) << 6) | ((g & 7) << 3) | (b & 7);
        }
        for (unsigned x = 0; x + 3 < w; ++x) {
            int  a  = code[x], b = code[x + 1];
            bool tl = a == kTlA && b == kTlB, br = a == kBrA && b == kBrB;
            if (!tl && !br) continue;
            int id = code[x + 2];
            if (code[x + 3] != stampCheck(id)) continue;
            Hit hit;
            hit.id = id, hit.x = int(x), hit.y = int(y), hit.slice = slice, hit.br = br;
            for (int k = 0; k < 4; ++k) hit.a[k] = row[(x + k) * 4 + 3];
            hits.push_back(hit);
            x += 3;
        }
    }
    return hits;
}

void writeFiles(fs::path dir, std::vector<Copy> copies, std::string report, bool saveAtlas) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::ostringstream s;
    s << report << "\n## Copies\n";
    std::ostringstream hitsJson;
    hitsJson << "[";
    bool   firstHit = true;
    size_t nHits    = 0;
    for (auto& c : copies) {
        s << c.name << ": " << descStr(c.desc) << " | copy " << c.note;
        if (!c.buf) { s << "\n"; continue; }
        void*       m = nullptr;
        D3D12_RANGE rr{0, SIZE_T(c.total)};
        if (FAILED(c.buf->Map(0, &rr, &m)) || !m) { s << " | MAP FAILED\n"; continue; }
        auto const*  base  = static_cast<uint8_t const*>(m) + c.fp.Offset;
        unsigned     w     = c.fp.Footprint.Width, h = c.fp.Footprint.Height;
        UINT64       pitch = c.fp.Footprint.RowPitch;
        Layout       L     = layoutOf(c.desc.Format); // the resource decides (D24S8's depth plane copies as a 32-bit footprint)
        if (L == Layout::Other) L = layoutOf(c.fp.Footprint.Format);
        bool const save = !c.atlas || saveAtlas; // an atlas is searched for our stamps below, its picture stays in memory
        { // raw, rows packed (rowBytes each) + header
            if (save) {
                std::ofstream f(dir / (c.name + ".raw"), std::ios::binary);
                for (unsigned y = 0; y < c.rows; ++y) f.write(reinterpret_cast<char const*>(base + size_t(y) * pitch), std::streamsize(c.rowBytes));
            }
            std::ofstream(dir / (c.name + ".json")) << "{\"name\": \"" << c.name << "\", \"width\": " << w << ", \"height\": " << h
                                                    << ", \"rowBytes\": " << c.rowBytes << ", \"rows\": " << c.rows << ", \"copyFormat\": \""
                                                    << fmtStr(c.fp.Footprint.Format) << "\", \"resourceFormat\": \"" << fmtStr(c.desc.Format)
                                                    << "\", \"layout\": \"" << layoutName(L) << "\", \"mips\": " << c.desc.MipLevels
                                                    << ", \"array\": " << c.desc.DepthOrArraySize << ", \"slice\": " << c.slice
                                                    << ", \"atlas\": " << (c.atlas ? "true" : "false") << ", \"saved\": " << (save ? "true" : "false")
                                                    << "}\n";
        }
        // preview (skipped for huge atlases: the raw file has everything)
        bool const           preview = save && uint64_t(w) * h <= uint64_t(4096) * 4096 && L != Layout::Other;
        std::vector<uint8_t> rgb(preview ? size_t(w) * h * 3 : 0, 0);
        if (!preview) {
        } else if (L == Layout::Depth24 || L == Layout::Depth32f) {
            std::vector<float> d(size_t(w) * h);
            float lo = 1e30f, hi = -1e30f;
            long  far1 = 0, near0 = 0;
            for (unsigned y = 0; y < h; ++y)
                for (unsigned x = 0; x < w; ++x) {
                    uint32_t v;
                    std::memcpy(&v, base + size_t(y) * pitch + x * 4, 4);
                    float z = L == Layout::Depth24 ? float(v & 0xFFFFFFu) / 16777215.f : 0.f;
                    if (L == Layout::Depth32f) std::memcpy(&z, &v, 4);
                    d[size_t(y) * w + x] = z;
                    if (z >= 0.999999f) { ++far1; continue; }
                    if (z <= 0.000001f) { ++near0; continue; }
                    lo = std::min(lo, z), hi = std::max(hi, z);
                }
            for (size_t i = 0; i < d.size(); ++i) {
                float z = d[i];
                uint8_t g = z >= 0.999999f ? 20 : z <= 0.000001f ? 0 : uint8_t(255.f - 235.f * std::clamp((z - lo) / std::max(hi - lo, 1e-9f), 0.f, 1.f));
                rgb[i * 3] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = g;
            }
            s << " | depth range " << lo << " .. " << hi << ", texels at 1.0: " << far1 << ", at 0.0: " << near0;
        } else if (L != Layout::Other) {
            for (unsigned y = 0; y < h; ++y)
                for (unsigned x = 0; x < w; ++x) texel(L, base + size_t(y) * pitch, x, &rgb[(size_t(y) * w + x) * 3]);
        }
        if (preview) writeBmp(dir / (c.name + ".bmp"), w, h, rgb);
        if (save) s << " | written " << c.name << (preview ? ".raw/.json/.bmp (" : ".raw/.json (") << layoutName(L) << ")";
        else s << " | picture NOT saved (tick the box in F6 for single-player tests), header only (" << layoutName(L) << ")";
        if (c.atlas && (L == Layout::Rgba8 || L == Layout::Bgra8)) {
            auto hits = findStamps(base, pitch, w, h, L == Layout::Bgra8, c.slice);
            s << " | WFX1 stamps: " << hits.size();
            nHits += hits.size();
            for (auto const& hit : hits) {
                hitsJson << (firstHit ? "\n" : ",\n") << "  {\"copy\": \"" << c.name << "\", \"id\": " << hit.id << ", \"x\": " << hit.x << ", \"y\": " << hit.y
                         << ", \"slice\": " << hit.slice << ", \"corner\": \"" << (hit.br ? "br" : "tl") << "\", \"alpha\": [" << hit.a[0] << ", "
                         << hit.a[1] << ", " << hit.a[2] << ", " << hit.a[3] << "]}";
                firstHit = false;
            }
        }
        s << "\n";
        D3D12_RANGE none{0, 0};
        c.buf->Unmap(0, &none);
    }
    hitsJson << "\n]\n";
    std::ofstream(dir / "stamps.json") << hitsJson.str();
    s << "\n## Tag test pack (WFX1 stamps found in the atlas candidates): " << nHits
      << (nHits ? " - see stamps.json; tools/WorldFx/analyze_probe.py maps ids to textures\n"
                : " - none (pack not active, overridden by a pack above it, or the game changes the texels)\n");
    std::ofstream(dir / "report.txt") << s.str();
    std::lock_guard l(gMutex);
    gLastDir     = dir.string();
    gLastSummary = "stamps found: " + std::to_string(nHits) + (saveAtlas ? " (atlas picture saved)" : " (atlas picture not saved)");
    gStatus      = "done - report written";
    gKeep        = std::move(copies); // released a few seconds later on the render thread (a late game list may still copy)
    gKeepFrames  = 300;
    gPhase       = Idle;
    ll_shim::info("[RF] world fx probe: report " + (dir / "report.txt").string());
}

/// End of the recording frame: the report body (everything but the copies, which come back later). gMutex held.
std::string buildReport() {
    std::ostringstream s;
    std::time_t        now = std::time(nullptr);
    char               ts[32];
    std::strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    s << "# World FX M0 probe v2 " << ts << "\n";
    s << "draws " << gDraws << " | UI draws " << gUiDraws << " (" << gUiBeforeTerrain << " before any terrain draw) | terrain draws " << gTerrainDraws
      << " | draws with an unknown pipeline " << gUnknownPso << " | terrain draws without root tables " << gTerrainNoTables << "\n";
    s << "UI shader list loaded: " << (uicapture::haveUiShaderList() ? "yes" : "NO (UI found by material name)") << " | shader names: "
      << (glassarmor::shaderNamesReady() ? "ready" : "NOT READY (probe again in a few seconds)") << " | atlas picture: "
      << (gSaveAtlasRun ? "saved (single-player test)" : "not saved") << "\n";
    s << "(one frame = the draws RECORDED between two Presents; RenderDragon records ahead on several threads, so a frame\n"
         " can hold the tail of the previous one or the head of the next)\n";
    s << "\n## World -> UI boundary\n" << (gBoundaryText.empty() ? "no UI draw after a terrain draw this frame - nothing copied\n" : gBoundaryText);
    s << "\n## Stage snapshots (colour + depth before the first draw of a material family that follows the terrain)\n"
      << (gStageText.empty() ? "none\n" : gStageText);
    // timeline
    std::sort(gTimeline.begin(), gTimeline.end(), [](Ev const& a, Ev const& b) { return a.seq < b.seq; });
    std::vector<size_t> rtIds, dsIds;
    auto idOf = [](std::vector<size_t>& v, size_t h) {
        if (!h) return size_t(0);
        auto it = std::find(v.begin(), v.end(), h);
        if (it == v.end()) { v.push_back(h); return v.size(); }
        return size_t(it - v.begin()) + 1;
    };
    s << "\n## Frame timeline (" << gTimeline.size() << " runs" << (gTimelineDropped ? ", " + std::to_string(gTimelineDropped) + " more dropped" : std::string())
      << "; a run = consecutive draws on one command list with the same shader, state and targets)\n";
    for (auto const& e : gTimeline) {
        size_t const r = idOf(rtIds, e.rts.rt0), d = idOf(dsIds, e.rts.dsv);
        s << "#" << e.seq << " list " << ptr(e.list) << " t" << e.tid << "  " << (r ? "RT" + std::to_string(r) : std::string("RT-")) << " "
          << (d ? "DS" + std::to_string(d) : std::string("DS-")) << "  " << e.text << "\n";
    }
    s << "targets:\n";
    for (size_t i = 0; i < rtIds.size(); ++i) {
        ID3D12Resource*     r = uicapture::rtvResourceOf(rtIds[i]);
        D3D12_RESOURCE_DESC d{};
        s << "  RT" << i + 1 << " = view 0x" << hex(rtIds[i]) << "  " << ptr(r) << "  " << (r && safeDescInner(r, &d) ? descStr(d) : std::string("unknown resource")) << "\n";
    }
    for (size_t i = 0; i < dsIds.size(); ++i) {
        ID3D12Resource*     r = glassarmor::dsvResource(dsIds[i]);
        D3D12_RESOURCE_DESC d{};
        s << "  DS" << i + 1 << " = view 0x" << hex(dsIds[i]) << "  " << ptr(r) << "  " << (r && safeDescInner(r, &d) ? descStr(d) : std::string("unknown resource")) << "\n";
    }
    // pipelines
    std::vector<std::pair<ID3D12PipelineState*, Stat>> terr;
    std::map<std::string, std::pair<Stat, int>>        fam; // material file -> draws, pipelines
    struct PsRow { ClsPtr c; Stat st; int psos = 0; };
    std::map<std::string, PsRow> byKey; // every other pipeline, merged by shader + state
    for (auto const& [p, st] : gPsoStat) {
        auto it = gCls.find(p);
        if (it == gCls.end()) continue;
        Cls const&  c    = *it->second;
        std::string file = c.file.empty() ? (c.known ? "(not a game material)" : "(pipeline made before the hooks)") : c.file;
        auto&       f    = fam[file];
        f.first.draws += st.draws, f.first.prims += st.prims, ++f.second;
        if (c.terrain) terr.emplace_back(p, st);
        else {
            auto& row = byKey[c.key];
            row.c     = it->second;
            row.st.draws += st.draws, row.st.prims += st.prims, ++row.psos;
        }
    }
    std::sort(terr.begin(), terr.end(), [](auto const& a, auto const& b) { return a.second.draws > b.second.draws; });
    s << "\n## Terrain (RenderChunk) pipelines this frame: " << terr.size() << "\n";
    for (auto const& [p, st] : terr) {
        Cls const& c = *gCls[p];
        s << "pso " << ptr(p) << "  draws " << st.draws << "  prims " << st.prims << "\n    ps " << hex(c.ps) << "  "
          << (c.name == "?" ? std::string("REPLACED by a pack (not one of the game's shaders)") : c.name) << "\n    vs " << hex(c.v.vs) << "  " << c.vsName
          << "\n    vs out: " << c.v.vsOut << "\n    in: " << c.v.inputs << "\n    " << c.v.targets << "  root " << c.v.root << "\n";
    }
    s << "\n## Other pipelines this frame (merged by shader + state): " << byKey.size() << "\n";
    for (auto const& [k, row] : byKey) {
        Cls const& c = *row.c;
        s << c.label << "  draws " << row.st.draws << "  prims " << row.st.prims << "  pipelines " << row.psos << (c.ui ? "  [UI]" : "") << "\n    vs "
          << hex(c.v.vs) << "  " << c.vsName << "\n    vs out: " << c.v.vsOut << "\n    in: " << c.v.inputs << "\n    " << c.v.targets << "  root "
          << c.v.root << "\n";
    }
    s << "\n## Every material this frame (material file -> draws / prims / pipelines)\n";
    for (auto const& [f, v] : fam) s << "  " << f << "  " << v.first.draws << " / " << v.first.prims << " / " << v.second << "\n";
    // textures
    std::vector<TexUse const*> tex;
    for (auto const& [r, u] : gTex) tex.push_back(&u);
    std::sort(tex.begin(), tex.end(), [](TexUse const* a, TexUse const* b) {
        return a->draws != b->draws ? a->draws > b->draws : uint64_t(a->desc.Width) * a->desc.Height > uint64_t(b->desc.Width) * b->desc.Height;
    });
    s << "\n## Textures bound by terrain draws: " << tex.size() << " (most-used first)\n";
    for (auto const* u : tex) s << "  " << ptr(u->res.Get()) << "  " << descStr(u->desc) << "  draws " << u->draws << "  at" << u->slots << "\n";
    s << "\n## Biggest textures that ever got an SRV (report only)\n";
    for (auto const& v : glassarmor::largestTextures(12))
        s << "  " << ptr(v.res) << "  " << v.w << "x" << v.h << " " << fmtStr(DXGI_FORMAT(v.fmt)) << " mips " << v.mips << "\n";
    s << "\n## Colour targets of terrain draws\n";
    for (auto const& [r, u] : gRt) s << "  " << ptr(r) << "  " << descStr(u.desc) << "  draws " << u.draws << (r == gLastRt ? "  (last)" : "") << "\n";
    s << "\n## Depth buffers of terrain draws\n";
    for (auto const& [r, u] : gDs) s << "  " << ptr(r) << "  " << descStr(u.desc) << "  draws " << u.draws << (r == gLastDs ? "  (last)" : "") << "\n";
    // camera + replaced materials: what a look needs to rebuild world positions, and which shaders are not vanilla
    auto const cam = rf::lyrics::camera();
    s << "\n## Camera (RF's world camera capture - only valid while LyricsForge draws lyrics)\n";
    if (cam.valid) {
        s << "pos " << cam.pos[0] << " " << cam.pos[1] << " " << cam.pos[2] << " | forward " << cam.forward[0] << " " << cam.forward[1] << " "
          << cam.forward[2] << " | up " << cam.up[0] << " " << cam.up[1] << " " << cam.up[2] << " | right " << cam.right[0] << " " << cam.right[1]
          << " " << cam.right[2] << "\nfovY " << cam.fovYDeg << " aspect " << cam.aspect << "\nproj (column-major):";
        for (float v : cam.proj) s << " " << v;
        s << "\n";
    } else s << "not valid\n";
    auto const ms = rf::materials::stats();
    s << "\n## Materials served by RF (seen " << ms.seen << ", from resource packs " << ms.fromPacks << ", from the shaderpack " << ms.fromShaderpack << ")\n";
    for (auto const& l : rf::materials::lastServed()) s << "  " << l << "\n";
    return s.str();
}

/// Atlas candidates: 2D textures bound by terrain draws, most draws first, at least 256x256. Mip 0 of up to 4 slices.
void queueAtlas(ID3D12Device* dev) {
    std::vector<TexUse const*> tex;
    for (auto const& [r, u] : gTex)
        if (u.desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && u.desc.Width >= 256 && u.desc.Height >= 256 && u.desc.SampleDesc.Count == 1)
            tex.push_back(&u);
    std::sort(tex.begin(), tex.end(), [](TexUse const* a, TexUse const* b) {
        return a->draws != b->draws ? a->draws > b->draws : uint64_t(a->desc.Width) * a->desc.Height > uint64_t(b->desc.Width) * b->desc.Height;
    });
    if (tex.size() > 3) tex.resize(3);
    if (tex.empty()) return;
    if (!gAlloc && FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&gAlloc)))) return;
    if (!gList) {
        if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, gAlloc.Get(), nullptr, IID_PPV_ARGS(&gList)))) return;
    } else {
        gAlloc->Reset();
        gList->Reset(gAlloc.Get(), nullptr);
    }
    UINT64 budget = UINT64(512) << 20;
    int    n      = 0;
    for (auto const* u : tex) {
        unsigned slices = std::min<unsigned>(u->desc.DepthOrArraySize, 4u);
        for (unsigned sl = 0; sl < slices; ++sl) {
            Copy cp;
            cp.name  = "atlas" + std::to_string(n) + (u->desc.DepthOrArraySize > 1 ? "_slice" + std::to_string(sl) : "");
            cp.atlas = true;
            cp.slice = int(sl);
            unsigned st  = glassarmor::stateOf(u->res.Get(), 0xFFFFFFFFu);
            bool     bar = st != 0xFFFFFFFFu && st != 0u;
            UINT     sub = sl * u->desc.MipLevels; // mip 0 of slice sl
            if (budget < UINT64(u->desc.Width) * u->desc.Height * 8) cp.note = "skipped: readback budget";
            else if (queueCopy(dev, gList.Get(), u->res, u->desc, sub, bar ? st : 0u, bar, cp)) budget -= cp.total;
            gCopies.push_back(std::move(cp));
        }
        ++n;
    }
    gList->Close();
}

} // namespace

// ---------------------------------------------------------------- API
void init(fs::path const& outDir) { gOutDir = outDir; }

bool tracking() {
    int p = gPhase.load(std::memory_order_relaxed);
    return p == Warm || p == Rec;
}
bool recording() { return gPhase.load(std::memory_order_relaxed) == Rec; }

void onSetPso(ID3D12GraphicsCommandList* list, ID3D12PipelineState* pso) {
    std::lock_guard l(gMutex);
    gListPso[list] = pso;
}
void onSetRts(ID3D12GraphicsCommandList* list, size_t rt0, size_t dsv) {
    std::lock_guard l(gMutex);
    gListRts[list] = {rt0, dsv};
}
namespace {
std::atomic_bool gTargetsWanted{false};
}
void setTargetsWanted(bool on) { gTargetsWanted = on; }
bool targetsWanted() { return gTargetsWanted.load(std::memory_order_relaxed); }
bool listTargets(ID3D12GraphicsCommandList* list, size_t& rt0, size_t& dsv) {
    std::lock_guard l(gMutex);
    auto it = gListRts.find(list);
    if (it == gListRts.end()) return false;
    rt0 = it->second.rt0;
    dsv = it->second.dsv;
    return rt0 && dsv;
}

void onDraw(ID3D12GraphicsCommandList* list, unsigned count, unsigned inst) {
    ID3D12PipelineState*     pso = nullptr;
    ClsPtr                   c;
    Rts                      rts;
    unsigned long long const prims = static_cast<unsigned long long>(count / 3) * std::max(1u, inst);
    {
        std::lock_guard l(gMutex);
        ++gDraws;
        auto it = gListPso.find(list);
        if (it == gListPso.end() || !it->second) { ++gUnknownPso; return; }
        pso = it->second;
        auto& st = gPsoStat[pso];
        ++st.draws;
        st.prims += prims;
        if (auto ci = gCls.find(pso); ci != gCls.end()) c = ci->second;
        if (auto ri = gListRts.find(list); ri != gListRts.end()) rts = ri->second;
    }
    if (!c) { // first sight of this pipeline this probe (outside our lock: the views take their own locks)
        c = classify(pso);
        std::lock_guard l(gMutex);
        gCls.emplace(pso, c);
    }
    bool        afterWorld = false, stage = false;
    int         stageNo = 0;
    std::string family;
    {
        std::lock_guard l(gMutex);
        noteRun(list, pso, *c, rts, prims);
        afterWorld = gTerrainDraws > 0;
        if (c->ui) {
            ++gUiDraws;
            if (!afterWorld) ++gUiBeforeTerrain;
        } else if (c->terrain) ++gTerrainDraws;
        else if (afterWorld) {
            if (int const kind = stageKind(c->file)) {
                family = stageFamily(c->file);
                if (std::find(gStageFamilies.begin(), gStageFamilies.end(), family) == gStageFamilies.end() &&
                    (kind == 1 || gStagesOther < kMaxOtherStages)) {
                    gStageFamilies.push_back(family);
                    if (kind == 2) ++gStagesOther;
                    stageNo = int(gStageFamilies.size());
                    stage   = true;
                }
            }
        }
    }
    if (c->ui) {
        if (afterWorld && !gBoundary.exchange(true)) boundary(list, *c);
        return;
    }
    if (stage) snapshotStage(list, *c, rts, stageNo, family);
    if (!c->terrain) return;
    noteTextures(list);
    if (rts.rt0) noteTarget(gRt, uicapture::rtvResourceOf(rts.rt0), gLastRt);
    if (rts.dsv) noteTarget(gDs, glassarmor::dsvResource(rts.dsv), gLastDs);
}

namespace {
/// gMutex held: forget what a probe frame collected (and the references it holds on game textures).
/// listsToo = also the per-list pipeline / target state the warm-up frames learned (kept when recording starts:
/// a list that bound its pipeline or targets before that moment keeps drawing with them).
void dropFrame(bool copiesToo, bool listsToo) {
    gPsoStat.clear(), gTex.clear(), gRt.clear(), gDs.clear(), gCls.clear(), gRun.clear();
    if (listsToo) gListPso.clear(), gListRts.clear();
    gLastRt = gLastDs = nullptr;
    if (copiesToo) gCopies.clear();
}
} // namespace

void onPresent(ID3D12Device* device, ID3D12CommandQueue* queue) {
    int p = gPhase.load();
    if (p == Idle) { // a finished probe's readback buffers go a few seconds after the files were written
        if (gKeepFrames.load(std::memory_order_relaxed) > 0 && --gKeepFrames == 0) {
            std::lock_guard l(gMutex);
            gKeep.clear();
        }
        return;
    }
    if (p == Write || !device || !queue) return;
    if (p == Warm) { // trackers learn heaps / descriptor copies / list targets for a couple of frames first
        // without the shader names nothing can be told apart: wait for them (a few seconds at most, then go anyway)
        if (!glassarmor::shaderNamesReady() && ++gWarmWait < 900) return;
        if (++gWarmFrames < 3) return;
        std::lock_guard l(gMutex);
        dropFrame(true, false);
        gTimeline.clear(), gStageFamilies.clear(), gStageText.clear(), gBoundaryText.clear();
        gSeq = 0, gTimelineDropped = 0, gStagesOther = 0;
        gDraws = gUiDraws = gTerrainDraws = gUiBeforeTerrain = gUnknownPso = gTerrainNoTables = 0;
        gBoundary = false;
        gStatus   = "recording one frame...";
        gPhase    = Rec;
        return;
    }
    if (p == Rec) { // the frame is recorded: stop listening, queue the atlas readback, wait for the GPU
        gPhase = Wait;
        std::lock_guard l(gMutex);
        for (auto const& [list, r] : gRun) closeRun(list, r);
        gRun.clear();
        gReport = buildReport();
        queueAtlas(device);
        if (gList) {
            bool any = false;
            for (auto const& c : gCopies) any |= c.atlas && c.buf;
            if (any) {
                ID3D12CommandList* lists[] = {gList.Get()};
                queue->ExecuteCommandLists(1, lists);
            }
        }
        std::time_t now = std::time(nullptr);
        char        ts[32];
        std::strftime(ts, sizeof ts, "%Y%m%d-%H%M%S", std::localtime(&now));
        gRunDir     = gOutDir / ts;
        gWaitFrames = 0;
        gStatus     = "waiting for the GPU copies...";
        return;
    }
    if (p == Wait) {
        // lists recorded during the probe frame may be submitted a frame or two later (RenderDragon records ahead):
        // signal only after a few more Presents, then wait for that fence
        if (!gFence && FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gFence)))) {
            std::lock_guard l(gMutex);
            gStatus = "no fence - probe abandoned";
            dropFrame(true, true);
            gPhase = Idle;
            return;
        }
        ++gWaitFrames;
        if (gWaitFrames == 3) {
            gWaitFor = ++gFenceVal;
            queue->Signal(gFence.Get(), gWaitFor);
            return;
        }
        if (gWaitFrames < 3 || gFence->GetCompletedValue() < gWaitFor) {
            if (gWaitFrames > 600) {
                std::lock_guard l(gMutex);
                gStatus = "GPU copies never finished - probe abandoned";
                gKeep.swap(gCopies); // the game's lists may still write into them: release later, not now
                gCopies.clear();
                gKeepFrames = 600;
                dropFrame(false, true);
                gPhase = Idle;
            }
            return;
        }
        std::vector<Copy> copies;
        std::string       report;
        fs::path          dir;
        bool              saveAtlas = false;
        {
            std::lock_guard l(gMutex);
            copies.swap(gCopies);
            report    = gReport;
            dir       = gRunDir;
            saveAtlas = gSaveAtlasRun;
            gStatus   = "writing files...";
            dropFrame(false, true); // the frame's references to game resources go now (the copies are ours)
        }
        gPhase = Write;
        std::thread(writeFiles, dir, std::move(copies), std::move(report), saveAtlas).detach();
    }
}

void drawUi() {
    if (!ImGui::CollapsingHeader("World FX")) return;
    ImGui::Indent();
    ImGui::TextWrapped("M0 probe: what the World FX looks need from one frame - the pipelines that draw blocks, the order the frame "
                       "is drawn in, the world's colour + depth at each stage (before the hand, the game's blit and the HUD), and "
                       "where the World FX tag test pack's textures sit in the terrain atlas. Stand in a world with glass in view, "
                       "HUD on, then probe.");
    glassarmor::learnShaderNames();
    int  p    = gPhase.load();
    bool busy = p != Idle;
    bool save = gSaveAtlas.load();
    if (ImGui::Checkbox("Also save the block texture sheet as a picture", &save)) gSaveAtlas = save;
    ImGui::TextDisabled("Single-player test worlds only. Leave it OFF on servers: a server pack's textures are the server's.");
    if (busy) ImGui::BeginDisabled();
    if (ImGui::Button("Probe the next frame")) {
        std::lock_guard l(gMutex);
        gKeep.clear();
        gKeepFrames   = 0;
        gSaveAtlasRun = gSaveAtlas.load();
        gWarmFrames = gWarmWait = 0;
        gStatus     = "warming up the trackers...";
        gPhase      = Warm;
    }
    if (busy) ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", glassarmor::shaderNamesReady() ? "shader names ready" : "learning shader names...");
    std::lock_guard l(gMutex);
    ImGui::Text("Status: %s", gStatus.c_str());
    if (!gLastSummary.empty()) ImGui::Text("Last probe: %s", gLastSummary.c_str());
    if (!gLastDir.empty()) ImGui::TextWrapped("Folder: %s", gLastDir.c_str());
    ImGui::SeparatorText("Cloud Sea");
    bool cs = rf::sky::worldSky() == 1;
    if (ImGui::Checkbox("Cloud Sea sky", &cs)) { rf::sky::setWorldSky(cs ? 1 : 0); rf::sky::saveWorldFxSettings(); }
    ImGui::SameLine();
    ImGui::TextDisabled("(sunset sky + sea of clouds under the map; works without LyricsForge)");
    bool saveFx = false;
    float deck = rf::sky::worldSkyDeck();
    if (ImGui::SliderFloat("Cloud deck height", &deck, -80.0f, 80.0f, "%+.0f blocks")) rf::sky::setWorldSkyDeck(deck);
    saveFx |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::Text("Sky: %s", rf::sky::worldSkyStatus().c_str());
    if (rf::uicapture::pipelinesSeen() == 0)
        ImGui::TextColored(ImVec4(1, .55f, .3f, 1), "RF saw none of the game's pipelines this launch: no sky, no World FX. Restart Minecraft.");
    auto& look = rf::sky::worldFxLook();
    saveFx |= ImGui::Checkbox("Sun shadows + soft occlusion", &look.shadows);
    ImGui::SliderFloat("Shadow strength", &look.shadow, 0.0f, 1.0f, "%.2f");
    saveFx |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Occlusion strength", &look.ao, 0.0f, 1.0f, "%.2f");
    saveFx |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Rim light", &look.rim, 0.0f, 2.0f, "%.2f");
    saveFx |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("God rays", &look.rays, 0.0f, 3.0f, "%.2f");
    saveFx |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Cloud shadows", &look.clouds, 0.0f, 1.0f, "%.2f");
    saveFx |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Ink look", &look.ink, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Novak Glass rim", &look.glass, 0.0f, 1.5f, "%.2f");
    ImGui::SameLine();
    ImGui::TextDisabled("(step 1: every block cast in glass; 0 = off, 1 = the preview's Medium)");
    saveFx |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    ImGui::TextDisabled("(inked block lines, painted flats, cel light)");
    ImGui::SliderFloat("Living world", &look.air, 0.0f, 1.5f, "%.2f");
    saveFx |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    ImGui::TextDisabled("(the cloud sea lights the islands from below; pollen, fireflies, stardust with the hour)");
    ImGui::SeparatorText("The match (Bedrock Forge)");
    saveFx |= ImGui::Checkbox("Living match sky", &look.living);
    ImGui::SameLine();
    ImGui::TextDisabled("(golden hour -> dusk after 4 beds -> night for the final fight)");
    ImGui::SliderFloat("Preview sky phase", &look.phase, -1.0f, 2.0f, look.phase < 0 ? "follow the match" : "%.2f");
    ImGui::SliderFloat("Preview tension", &look.tension, -0.05f, 1.0f, look.tension < 0 ? "follow the match" : "%.2f");
    ImGui::SameLine();
    ImGui::TextDisabled("(teams going out: far storm, the frame closes in)");
    {   // review: watch the fast moments (a block placed, a hit, a kill, the fireworks) slowed down. Not saved.
        static int slowSel = 0;
        if (ImGui::Combo("Review speed", &slowSel, "Real time\0Slow motion 1/4\0Slow motion 1/10\0")) look.slow = slowSel == 0 ? 1.0f : (slowSel == 1 ? 0.25f : 0.1f);
    }
    saveFx |= ImGui::Checkbox("Cosmetic beds", &look.beds);
    ImGui::SameLine();
    ImGui::TextDisabled("(drawn over the server's bed, on your screen only)");
    if (look.beds) {
        ImGui::SetNextItemWidth(200.0f);
        saveFx |= ImGui::Combo("Bed style", &look.bedStyle, "Novak bed\0Your Figura, sitting on it\0");
        if (ImGui::Button("Test bed")) rf::sky::testBed();
        ImGui::SameLine();
        ImGui::TextDisabled("(3 blocks ahead: stands 10 s, attacked 10 s, then breaks)");
    }
    saveFx |= ImGui::Checkbox("Kill and respawn moments", &look.kills);
    ImGui::SetNextItemWidth(200.0f);
    {
        int ds = look.deathScene + 1;
        if (ImGui::Combo("Death scene", &ds, "Shuffle (never the same twice)\0Chains of the Deep\0Blackjack\0Tentacles\0Lightning Ash\0Giant Hand\0Hungry Chest\0Executioner\0")) { look.deathScene = ds - 1; saveFx = true; }
    }
    if (ImGui::Button("Test kill")) rf::sky::testKill();
    ImGui::SameLine();
    if (ImGui::Button("Test crit")) rf::sky::testImpact();
    ImGui::SameLine();
    if (ImGui::Button("Test victory")) rf::sky::testVictory();
    ImGui::SameLine();
    ImGui::TextDisabled("(a kill 6 blocks ahead / the fireworks)");
    ImGui::SliderFloat("Team lights + bed beacons", &look.teams, 0.0f, 2.0f, "%.2f");
    saveFx |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Block shards", &look.shards, 0.0f, 1.0f, "%.2f");
    saveFx |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Bed-break shockwave", &look.waves, 0.0f, 2.0f, "%.2f");
    saveFx |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Fresh-block trails", &look.trails, 0.0f, 1.0f, "%.2f");
    saveFx |= ImGui::IsItemDeactivatedAfterEdit();
    saveFx |= ImGui::Checkbox("Kill run warmth", &look.vitals);
    ImGui::SameLine();
    if (ImGui::Button("Test kill run")) rf::sky::testVitals();
    ImGui::SameLine();
    ImGui::TextDisabled("(2+ kills in one life: the edges warm, and flare on each new kill)");
    ImGui::Text("Match: %s", rf::sky::matchStatus().c_str());
    ImGui::RadioButton("Normal", &look.debug, 0); ImGui::SameLine();
    ImGui::RadioButton("Show shadows", &look.debug, 1); ImGui::SameLine();
    ImGui::RadioButton("Show occlusion", &look.debug, 2);
    ImGui::Text("Pass: %s", rf::sky::worldFxStatus().c_str());
    if (saveFx) rf::sky::saveWorldFxSettings();
    ImGui::Unindent();
}

} // namespace rf::worldfx
