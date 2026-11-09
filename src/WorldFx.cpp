// Renderer Foundation - World FX M0 world probe (see WorldFx.h). Original code for Renderer Foundation.
#include "WorldFx.h"

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
int              gWarmFrames = 0, gWaitFrames = 0;
fs::path         gOutDir;

std::mutex  gMutex; // everything below
std::string gStatus = "idle", gLastDir, gLastSummary;

struct Stat { long draws = 0; unsigned long long prims = 0; };
struct Rts { size_t rt0 = 0, dsv = 0; };
struct Cls {
    uint64_t    ps = 0;
    std::string name;  // pixel shader: "RenderChunk.material.bin Transparent Fragment ..." or "?"
    bool        terrain = false, ui = false;
    glassarmor::PsoView v;
    bool        known = false;
};
std::unordered_map<ID3D12GraphicsCommandList*, ID3D12PipelineState*> gListPso;
std::unordered_map<ID3D12GraphicsCommandList*, Rts>                  gListRts;
std::unordered_map<ID3D12PipelineState*, Cls>                        gCls;
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
Cls classify(ID3D12PipelineState* pso) {
    Cls c;
    if (glassarmor::psoView(pso, c.v)) {
        c.known = true;
        c.ps    = c.v.ps;
        c.name  = glassarmor::shaderName(c.ps);
    } else c.name = "(pipeline made before the hooks)";
    c.terrain = isTerrain(c.name);
    c.ui      = uicapture::isUiPipeline(pso) || (!uicapture::haveUiShaderList() && looksUi(c.name));
    return c;
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
    s << "first UI draw after the world: " << c.name << " (ps " << hex(c.ps) << ")\n";
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

void writeFiles(fs::path dir, std::vector<Copy> copies, std::string report) {
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
        { // raw, rows packed (rowBytes each) + header
            std::ofstream f(dir / (c.name + ".raw"), std::ios::binary);
            for (unsigned y = 0; y < c.rows; ++y) f.write(reinterpret_cast<char const*>(base + size_t(y) * pitch), std::streamsize(c.rowBytes));
            std::ofstream(dir / (c.name + ".json")) << "{\"name\": \"" << c.name << "\", \"width\": " << w << ", \"height\": " << h
                                                    << ", \"rowBytes\": " << c.rowBytes << ", \"rows\": " << c.rows << ", \"copyFormat\": \""
                                                    << fmtStr(c.fp.Footprint.Format) << "\", \"resourceFormat\": \"" << fmtStr(c.desc.Format)
                                                    << "\", \"layout\": \"" << layoutName(L) << "\", \"mips\": " << c.desc.MipLevels
                                                    << ", \"array\": " << c.desc.DepthOrArraySize << ", \"slice\": " << c.slice
                                                    << ", \"atlas\": " << (c.atlas ? "true" : "false") << "}\n";
        }
        // preview (skipped for huge atlases: the raw file has everything)
        bool const           preview = uint64_t(w) * h <= uint64_t(4096) * 4096 && L != Layout::Other;
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
        s << " | written " << c.name << (preview ? ".raw/.json/.bmp (" : ".raw/.json (") << layoutName(L) << ")";
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
    gLastSummary = "stamps found: " + std::to_string(nHits);
    gStatus      = "done - report written";
    gKeep = std::move(copies); // readback buffers stay alive until the next probe (a late game list may still copy)
    gPhase = Idle;
    ll_shim::info("[RF] world fx probe: report " + (dir / "report.txt").string());
}

/// End of the recording frame: the report body (everything but the copies, which come back later).
std::string buildReport() {
    std::ostringstream s;
    std::time_t        now = std::time(nullptr);
    char               ts[32];
    std::strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    s << "# World FX M0 probe " << ts << "\n";
    s << "draws " << gDraws << " | UI draws " << gUiDraws << " (" << gUiBeforeTerrain << " before any terrain draw) | terrain draws " << gTerrainDraws
      << " | draws with an unknown pipeline " << gUnknownPso << " | terrain draws without root tables " << gTerrainNoTables << "\n";
    s << "UI shader list loaded: " << (uicapture::haveUiShaderList() ? "yes" : "NO (UI found by material name)") << " | shader names: "
      << (glassarmor::shaderNamesReady() ? "ready" : "NOT READY (press the probe again in a few seconds)") << "\n";
    s << "\n## World -> UI boundary\n" << (gBoundaryText.empty() ? "no UI draw after a terrain draw this frame - nothing copied\n" : gBoundaryText);
    // pipelines
    std::vector<std::pair<ID3D12PipelineState*, Stat>> terr;
    std::map<std::string, std::pair<Stat, int>>        fam; // material file -> draws, pipelines
    for (auto const& [p, st] : gPsoStat) {
        auto        it   = gCls.find(p);
        std::string name = it == gCls.end() ? "?" : it->second.name;
        std::string file = name.substr(0, name.find(' '));
        auto&       f    = fam[file];
        f.first.draws += st.draws, f.first.prims += st.prims, ++f.second;
        if (it != gCls.end() && it->second.terrain) terr.emplace_back(p, st);
    }
    std::sort(terr.begin(), terr.end(), [](auto const& a, auto const& b) { return a.second.draws > b.second.draws; });
    s << "\n## Terrain (RenderChunk) pipelines this frame: " << terr.size() << "\n";
    for (auto const& [p, st] : terr) {
        auto const& c = gCls[p];
        s << "pso " << ptr(p) << "  draws " << st.draws << "  prims " << st.prims << "\n    ps " << hex(c.ps) << "  " << c.name << "\n    vs "
          << hex(c.v.vs) << "  " << glassarmor::shaderName(c.v.vs) << "\n    vs out: " << c.v.vsOut << "\n    in: " << c.v.inputs << "\n    "
          << c.v.targets << "  root " << c.v.root << "\n";
    }
    s << "\n## Every material this frame (pixel shader's .material.bin -> draws / prims / pipelines)\n";
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
    UINT64 budget = UINT64(1) << 30;
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

void onDraw(ID3D12GraphicsCommandList* list, unsigned count, unsigned inst) {
    ID3D12PipelineState* pso = nullptr;
    Cls                  c;
    bool                 have = false;
    Rts                  rts;
    {
        std::lock_guard l(gMutex);
        ++gDraws;
        auto it = gListPso.find(list);
        if (it == gListPso.end() || !it->second) { ++gUnknownPso; return; }
        pso = it->second;
        auto& st = gPsoStat[pso];
        ++st.draws;
        st.prims += static_cast<unsigned long long>(count / 3) * std::max(1u, inst);
        if (auto ci = gCls.find(pso); ci != gCls.end()) c = ci->second, have = true;
        if (auto ri = gListRts.find(list); ri != gListRts.end()) rts = ri->second;
    }
    if (!have) { // first sight of this pipeline this probe (outside our lock: the views take their own locks)
        c = classify(pso);
        std::lock_guard l(gMutex);
        gCls.emplace(pso, c);
    }
    if (c.ui) {
        bool afterWorld;
        {
            std::lock_guard l(gMutex);
            ++gUiDraws;
            afterWorld = gTerrainDraws > 0;
            if (!afterWorld) ++gUiBeforeTerrain;
        }
        if (afterWorld && !gBoundary.exchange(true)) boundary(list, c);
        return;
    }
    if (!c.terrain) return;
    {
        std::lock_guard l(gMutex);
        ++gTerrainDraws;
    }
    noteTextures(list);
    if (rts.rt0) noteTarget(gRt, uicapture::rtvResourceOf(rts.rt0), gLastRt);
    if (rts.dsv) noteTarget(gDs, glassarmor::dsvResource(rts.dsv), gLastDs);
}

void onPresent(ID3D12Device* device, ID3D12CommandQueue* queue) {
    int p = gPhase.load();
    if (p == Idle || p == Write || !device || !queue) return;
    if (p == Warm) { // trackers learn heaps / descriptor copies / list targets for a couple of frames first
        if (++gWarmFrames < 3) return;
        std::lock_guard l(gMutex);
        gPsoStat.clear(), gTex.clear(), gRt.clear(), gDs.clear(), gCopies.clear(), gCls.clear();
        gLastRt = gLastDs = nullptr;
        gDraws = gUiDraws = gTerrainDraws = gUiBeforeTerrain = gUnknownPso = gTerrainNoTables = 0;
        gBoundaryText.clear();
        gBoundary = false;
        gStatus   = "recording one frame...";
        gPhase    = Rec;
        return;
    }
    if (p == Rec) { // the frame is recorded: stop listening, queue the atlas readback, wait for the GPU
        gPhase = Wait;
        std::lock_guard l(gMutex);
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
        gRunDir      = gOutDir / ts;
        gWaitFrames  = 0;
        gStatus      = "waiting for the GPU copies...";
        return;
    }
    if (p == Wait) {
        // lists recorded during the probe frame may be submitted a frame or two later (RenderDragon records ahead):
        // signal only after a few more Presents, then wait for that fence
        if (!gFence && FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gFence)))) {
            std::lock_guard l(gMutex);
            gStatus = "no fence - probe abandoned";
            gCopies.clear();
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
                gCopies.clear();
                gPhase = Idle;
            }
            return;
        }
        std::vector<Copy> copies;
        std::string       report;
        fs::path          dir;
        {
            std::lock_guard l(gMutex);
            copies.swap(gCopies);
            report = gReport;
            dir    = gRunDir;
            gStatus = "writing files...";
            // drop the frame's references to game resources now (copies keep what they still need)
            gTex.clear(), gRt.clear(), gDs.clear();
            gLastRt = gLastDs = nullptr;
            gListPso.clear(), gListRts.clear();
        }
        gPhase = Write;
        std::thread(writeFiles, dir, std::move(copies), std::move(report)).detach();
    }
}

void drawUi() {
    if (!ImGui::CollapsingHeader("World FX")) return;
    ImGui::Indent();
    ImGui::TextWrapped("M0 probe: what cel outlines, refraction glass and the other World FX looks need from one frame - the "
                       "pipelines that draw blocks, the terrain atlas (searched for the World FX tag test pack) and the world's "
                       "colour + depth right before the HUD. Stand in a world with glass in view, HUD on, then probe.");
    glassarmor::learnShaderNames();
    int  p    = gPhase.load();
    bool busy = p != Idle;
    if (busy) ImGui::BeginDisabled();
    if (ImGui::Button("Probe the next frame")) {
        std::lock_guard l(gMutex);
        gKeep.clear();
        gWarmFrames = 0;
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
    ImGui::Unindent();
}

} // namespace rf::worldfx
