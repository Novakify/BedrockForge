// Renderer Foundation - Glass Armor M0 probe (see GlassArmor.h). Original code for Renderer Foundation.
#include "GlassArmor.h"
#include "Dred.h"
#include "SkyPass.h"

#include <algorithm>
#include <cstdint>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <deque>
#include <memory>

#include <imgui.h>

#include "LLShim.h"
#include "BfDirector.h"
#include "D3D12Api.h"
#include "GlassArmorShaders.h"
#include "GlassBodyMath.h"
#include "GlassBodyShaders.h"
#include "SkyMaterialScan.h"
#include "UiCapture.h"
#include "WorldFx.h"

namespace rf::glassarmor {
namespace {
namespace fs = std::filesystem;

uint64_t fnv(void const* p, size_t n) { return rf::skyscan::fnv1a(static_cast<uint8_t const*>(p), n); }

struct PsoInfo {
    uint64_t    ps = 0, vs = 0;
    std::string vsOut;     // vertex shader output signature: "SV_Position0 TEXCOORD0.xy COLOR0 ..."
    std::string inputs;    // input layout semantics
    std::string targets;   // "rt R16G16B16A16_FLOAT x1 | ds D32_FLOAT less write | blend on"
    void*       root = nullptr;
};
std::mutex                                       gMutex;
std::unordered_map<ID3D12PipelineState*, PsoInfo> gPsos;
std::unordered_map<uint64_t, std::string>        gShaderNames; // ps/vs hash -> "File.material.bin pass stage"
std::unordered_map<uint64_t, std::string>        gServedNames; // the same for materials RF served from a pack / shaderpack
std::atomic_bool                                 gNamesLearned{false}, gLearning{false};
fs::path                                         gOutDir;

// capture: 0 idle, 1 armed (starts at next Present), 2 recording this frame
std::atomic<int>  gCap{0};
int               gWhich = 0; // 0 = ON (wearing armor), 1 = OFF
struct DrawStat { long draws = 0; unsigned long long prims = 0; };
std::unordered_map<ID3D12GraphicsCommandList*, ID3D12PipelineState*> gListPso;
std::map<ID3D12PipelineState*, DrawStat> gFrame[2];
// M0b: per-draw detail for actor-family shaders (Actor / Entity / Armor / ItemInHand): bound textures
struct SrvInfo { ID3D12Resource* res = nullptr; unsigned w = 0, h = 0, fmt = 0, mips = 0; };
std::unordered_map<size_t, SrvInfo> gSrvs; // CPU descriptor -> resource, recorded while a capture is armed
std::vector<std::string>             gDetail[2];
std::map<uint64_t, DrawStat>         gByPs[2];
bool        gHave[2]{};
std::string gStatus = "idle";

// ---------------------------------------------------------------- M1a: glass armor
using Microsoft::WRL::ComPtr;
std::atomic_bool gEnabled{false};
fs::path         gConfigFile;
// the vertex shader output signature of the game's Actor shaders (probe M0) - our pixel shader is built to match it
constexpr char const* kActorVsOut = "SV_Position0.xyzw@o0 COLOR1.xyzw@o1 COLOR0.xyzw@o2 COLOR2.xyzw@o3 COLOR3.xyzw@o4 TEXCOORD0.xy@o5 TEXCOORD3.xyz@o6";
// ActorGlint (enchanted armor): the same plus the glint layer UVs in TEXCOORD2 (from the 1.26.51 material files)
constexpr char const* kGlintVsOut = "SV_Position0.xyzw@o0 COLOR1.xyzw@o1 COLOR0.xyzw@o2 COLOR2.xyzw@o3 TEXCOORD2.xyzw@o4 COLOR3.xyzw@o5 TEXCOORD0.xy@o6 TEXCOORD3.xyz@o7";
struct ActorDesc {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC    d{};
    std::vector<uint8_t>                  vs;
    std::vector<D3D12_INPUT_ELEMENT_DESC> il;
    std::deque<std::string>               names;
    ComPtr<ID3D12RootSignature>           root;
    bool                                  glint = false; // ActorGlint pipeline = enchanted armor
};
std::unordered_map<ID3D12PipelineState*, std::shared_ptr<ActorDesc>> gActor;   // game Actor pipelines
// Glass Items: the game's ItemInHandColor pipelines (held in first + third person, dropped, item frames - probe
// 2026-11-17: all one material). Their vertex shader hands COLOR3 / COLOR1 over in swapped places. Kept apart from
// gActor so the armor's replay trigger (isActorPso) is exactly as before.
constexpr char const* kItemVsOut = "SV_Position0.xyzw@o0 COLOR3.xyzw@o1 COLOR0.xyzw@o2 COLOR2.xyzw@o3 COLOR1.xyzw@o4 TEXCOORD0.xy@o5 TEXCOORD3.xyz@o6";
std::unordered_map<ID3D12PipelineState*, std::shared_ptr<ActorDesc>> gItemPso;
std::map<std::pair<ID3D12PipelineState*, int>, ComPtr<ID3D12PipelineState>> gGlass; // (game PSO, variant) -> ours
std::unordered_map<ID3D12Resource*, unsigned>                    gResState; // last barrier StateAfter
// armor texture recognition: -1 not armor, -2 queued for readback, >= 0 shaders::kVariant index
std::unordered_map<ID3D12Resource*, int>                         gTexKind;
std::vector<ComPtr<ID3D12Resource>>                               gPending;
struct Readback { ComPtr<ID3D12Resource> tex, buf; int scale = 1; }; // scale 2: a 128x64 (32x pack) texture, read at full size
std::vector<Readback>              gInFlight;
ComPtr<ID3D12CommandAllocator>     gAlloc;
ComPtr<ID3D12GraphicsCommandList>  gList;
ComPtr<ID3D12Fence>                gFence;
UINT64                             gFenceVal = 0;
std::atomic<long>                  gGlassDraws{0}, gGlassDrawsShown{0}, gArmorTex{0}, gNotArmor{0}, gPsoFail{0};
std::string                        gLastClass, gLastClassInfo;
// M1b
std::unordered_map<size_t, ID3D12Resource*>                         gDsvRes;  // DSV handle -> depth resource
std::unordered_map<ID3D12RootSignature*, std::vector<uint8_t>>      gRsBlob;  // game root signature blobs
struct ExtRoot { ComPtr<ID3D12RootSignature> rs; unsigned base = 0; bool failed = false; };
std::unordered_map<ID3D12RootSignature*, ExtRoot>                   gExt;
struct FlushSlot { ComPtr<ID3D12Resource> scene, depth; UINT64 sceneSize = 0, depthSize = 0; };
constexpr int                      kSlots = 8;
FlushSlot                          gSlots[kSlots];
ComPtr<ID3D12Resource>             gParams; // upload, kSlots x 256 B
uint8_t*                           gParamsMap = nullptr;
std::atomic<unsigned>              gSlotNext{0};
std::atomic<long>                  gFlushOk{0}, gFlushFail{0};
std::string                        gFlushWhy;
std::atomic_int                    gDebugSolid{0}; // 0 off, 1 magenta, 2..5 show COLOR1/0/2/3, 6 alphas, 7 texel
std::atomic_bool                   gReact{true};
float                              gReflect = 0.45f;
float                              gShowBeat = 0;
int                                gDyeRow = 5, gDyeRowGlint = 5; // M3: ChangeColor = row 5 of b1 (1.26.51: found with debug view 8; same uniform order in ActorGlint)
int                                gDbgRow = 0;
std::atomic_bool                   gEnchFlow{true};
// frosted glass (client menu "Glass": Frosted / Glow / Shimmer / Colour; config/GlassFrost.txt, live). Defaults keep
// the approved look: frost 0 = clear crystal, no glow of its own, no shimmer, white
std::atomic<float>                 gFrost{0.0f}, gGlowK{0.0f};
std::atomic<bool>                  gShimmer{false};
std::atomic<uint32_t>              gTintPk{0x00FFFFFFu};
fs::path                           gFrostFile;
fs::file_time_type                 gFrostTime{};
void loadFrost() {
    std::error_code ec;
    auto const t = fs::last_write_time(gFrostFile, ec);
    if (ec || t == gFrostTime) return;
    gFrostTime = t;
    std::ifstream in(gFrostFile);
    std::string   k, v;
    while (in >> k >> v) {
        if (k == "frost") gFrost = std::clamp(std::strtof(v.c_str(), nullptr), 0.f, 1.f);
        else if (k == "glow") gGlowK = std::clamp(std::strtof(v.c_str(), nullptr), 0.f, 1.f);
        else if (k == "shimmer") gShimmer = v == "1" || v == "true";
        else if (k == "tint") { // RRGGBB
            if (v.size() >= 1 && v[0] == '#') v = v.substr(1);
            uint32_t const rgb = uint32_t(std::strtoul(v.c_str(), nullptr, 16));
            gTintPk = ((rgb >> 16) & 255u) | (((rgb >> 8) & 255u) << 8) | ((rgb & 255u) << 16); // shader reads r | g<<8 | b<<16
        }
    }
}
void saveFrost() {
    uint32_t const pk = gTintPk.load();
    char tint[8];
    std::snprintf(tint, sizeof tint, "%02X%02X%02X", pk & 255u, (pk >> 8) & 255u, (pk >> 16) & 255u);
    std::ofstream(gFrostFile) << "frost " << gFrost.load() << "\nglow " << gGlowK.load() << "\nshimmer " << (gShimmer ? 1 : 0)
                              << "\ntint " << tint << "\n";
    std::error_code ec;
    gFrostTime = fs::last_write_time(gFrostFile, ec);
}
std::atomic<float>                 gFusedK{0.0f}, gFilmK{1.0f}; // fused glass (F6) - off: the approved look is clear warped glass, the material read only as tint + neon outline
float                              gIdleEdge = 0.3f;  // M2.5: outline strength with no music (Awaken boots it to 1)
float                              gBeatK    = 1.0f;  // M2.5: how hard the outline breathes with the kick
std::atomic_bool                   gWordSweep{true};  // M2.5: a light sweep per sung word
bool                               gGlassLf = false;
float                              gShowAwaken = 0, gShowCover = 0;
LARGE_INTEGER                      gT0{};
int                                gDumped = 0;
// Diagnosis only (F6, off by default, not saved): write the first newly seen 64x32 textures to logs/glassarmor. On a
// server those can be the server pack's textures, so this stays off there (The Hive bans "asset downloaders").
std::atomic_bool                   gDumpReadbacks{false};
std::string gLastReport;
// ---- Glass Body / Glass Items probe (M0 extension; only while a capture records, nothing at all otherwise). For the
// player / item glass (Purxiy "Glass": You / Other players / Items / Armour) it answers: which pipelines and vertex
// outputs draw players, hands and items; what root signature they bind (where the game's own texture sits, so our
// shader can read its alpha); and whether a skin-sized texture is really a player's skin - compared with the skins
// Bedrock Forge grabbed (rf::sky::bfSkins), your own marked. Nothing is saved but the report's numbers.
std::vector<std::string>                          gSigs;     // distinct vertex output signatures of actor-family draws
std::map<ID3D12Resource*, ComPtr<ID3D12Resource>> gProbeTex; // skin-sized textures bound by actor-family draws (kept alive)
std::map<void*, int>                              gProbeRoots; // root signatures of actor-family draws -> draws
struct ProbeRb { ComPtr<ID3D12Resource> tex, buf; unsigned w = 0, h = 0, pitch = 0; bool bgra = false; };
std::vector<ProbeRb>              gProbeRb;
int                               gProbeStage = 0; // 0 idle, 1 both frames captured: read the textures back, 2 waiting
ComPtr<ID3D12CommandAllocator>    gProbeAlloc;
ComPtr<ID3D12GraphicsCommandList> gProbeList;
ComPtr<ID3D12Fence>               gProbeFence;
UINT64                            gProbeFenceVal = 0;
bool skinSized(unsigned w, unsigned h) { return (w == 64 && (h == 64 || h == 32)) || (w == 128 && h == 128); }

// ---------------------------------------------------------------- Glass Body (Purxiy "Glass": You / Other players / Armour)
// BF's Glass module writes config/GlassBody.txt ("self 1", "players 0", "armour 1"); read about once a second. All off
// (Armour on) = exactly the Glass Armor of before: none of the code below runs.
std::atomic_bool   gSelf{false}, gOthers{false}, gArmour{true}, gItems{false};
fs::path           gBodyFile;
fs::file_time_type gBodyTime{};
void loadBody() {
    std::error_code ec;
    auto const t = fs::last_write_time(gBodyFile, ec);
    if (ec || t == gBodyTime) return;
    gBodyTime = t;
    std::ifstream in(gBodyFile);
    std::string   k, v;
    while (in >> k >> v) {
        bool const on = v == "1" || v == "true";
        if (k == "self") gSelf = on;
        else if (k == "players") gOthers = on;
        else if (k == "armour") gArmour = on;
        else if (k == "items") gItems = on;
    }
}
constexpr int kBodyKind = 1000; // gGlass key for the body pipeline (armor variants are 0..kVariants-1)
// which skin-sized textures are players: read back once, compared with BF's skins, checked again every few seconds (the
// game frees textures and a new one can come at the same address - a mob's texture must never stay "a player")
struct SkinKind {
    int      slot = -2;   // -2 being read, -1 not a player's skin, >= 0 BF skin slot
    bool     self = false, requeued = false;
    uint32_t gen = 0;     // BF's generation of that slot when it matched
    unsigned w = 0, h = 0, fmt = 0;
    int64_t  at = 0, drawn = 0;
};
std::unordered_map<ID3D12Resource*, SkinKind> gSkinKind;
std::vector<ComPtr<ID3D12Resource>>           gSkinPending;
struct SkinRb { ComPtr<ID3D12Resource> tex, buf; unsigned w = 0, h = 0, pitch = 0; bool bgra = false; };
std::vector<SkinRb>               gSkinInFlight;
ComPtr<ID3D12CommandAllocator>    gSkinAlloc;
ComPtr<ID3D12GraphicsCommandList> gSkinList;
ComPtr<ID3D12Fence>               gSkinFence;
UINT64                            gSkinFenceVal = 0;
// our copy of BF's 16 skins (upload heap, 64x64 RGBA8 each, 16 KB apart): what the body shader reads (t3 space9)
ComPtr<ID3D12Resource> gSkinBuf;
uint8_t*               gSkinMap = nullptr;
uint32_t               gSkinUpGen[16]{};
bool                   gSkinUpOk[16]{};
std::atomic<int>       gMine{-1}; // your own BF slot + 1 (0 none yet, -1 BF missing)
std::atomic<long>      gSkinsSelf{0}, gSkinsOther{0}, gSkinsNone{0}, gBodyDraws{0}, gBodyDrawsShown{0}, gBodyPsoFail{0};
std::string            gSkinLast;   // the last comparison, for F6
// ---- Glass Items: the item ATLAS the ItemInHand draw samples. (In game 2026-11-17 the first version read the draw's
// first texture - 16x16, which is the LIGHTMAP: glass made from it was a solid box, swords "vanished" and particles got
// boxes.) The atlas's mip 0 is copied GPU-side, once, into a buffer the item shader reads: a 512-byte header (w, h,
// row pitch, bgra) and then the rows. Up to 4 atlases (items, blocks, a pack's) of at most 2048x2048.
constexpr int    kAtlasSlots = 4;
constexpr UINT64 kAtlasMaxBytes = 2048ull * 2048ull * 4ull;
struct Atlas {
    ID3D12Resource*        tex = nullptr;   // identity (kept alive by `keep` while the slot holds it)
    ComPtr<ID3D12Resource> keep, buf;
    UINT64                 bufBytes = 0;
    unsigned               w = 0, h = 0, fmt = 0;
    bool                   pending = false, ready = false, failed = false, bufInSrv = false;
    int64_t                drawn = 0;
};
Atlas                             gAtlas[kAtlasSlots];
std::atomic<long>                 gItemWhy[4]{};   // item pipeline draws not glassed: not ItemInHand / names pending / no atlas bound / atlas not copied yet
std::string                       gItemSeen;       // the last item draw's first textures (w x h, mips), for the panel
ComPtr<ID3D12CommandAllocator>    gItemAlloc;
ComPtr<ID3D12GraphicsCommandList> gItemList;
ComPtr<ID3D12Fence>               gItemFence;
UINT64                            gItemFenceVal = 0;
ComPtr<ID3D12Resource>            gAtlasHdr;   // upload: kAtlasSlots x 512-byte headers
uint8_t*                          gAtlasHdrMap = nullptr;
std::vector<int>                  gAtlasInFlight;
// a buffer a slot outgrew may still be read by frames the GPU hasn't finished: it is released only after the copy that
// replaced it has completed plus a few frames (never under the GPU's feet)
std::vector<std::pair<ComPtr<ID3D12Resource>, UINT64>> gAtlasRetired;
std::atomic<long>                 gItemDraws{0}, gItemDrawsShown{0}, gItemsKnown{0}, gItemPsoFail{0};
// ---- glass cost (logs/glassarmor/glass_ms.txt every 5 s while any glass is on)
std::atomic_bool gMeasureGpu{false};
std::mutex       gTimeMx;
struct TimeOpen { int64_t qpc = 0; int ts = -1; };
std::unordered_map<ID3D12GraphicsCommandList*, TimeOpen> gTimeOpen;
double                    gCpuMs = 0, gGpuMs = 0;   // summed since the last log line
long                      gFlushes = 0, gGpuPairs = 0;
ComPtr<ID3D12QueryHeap>   gTsHeap;
ComPtr<ID3D12Resource>    gTsBuf;
uint64_t const*           gTsMap = nullptr;
constexpr int             kTsPairs = 256;
uint32_t                  gTsFrame[kTsPairs]{};
bool                      gTsUsed[kTsPairs]{};
int                       gTsNext = 0;
uint32_t                  gFrameNo = 0;
UINT64                    gTsFreq = 0;
float                     gShowCpuMs = 0, gShowGpuMs = -1, gShowFlushes = 0;

char const* fmtName(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_UNKNOWN: return "-";
    case DXGI_FORMAT_R8G8B8A8_UNORM: return "RGBA8";
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "RGBA8_SRGB";
    case DXGI_FORMAT_B8G8R8A8_UNORM: return "BGRA8";
    case DXGI_FORMAT_R10G10B10A2_UNORM: return "RGB10A2";
    case DXGI_FORMAT_R11G11B10_FLOAT: return "R11G11B10F";
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return "RGBA16F";
    case DXGI_FORMAT_R32G32B32A32_FLOAT: return "RGBA32F";
    case DXGI_FORMAT_R16G16_FLOAT: return "RG16F";
    case DXGI_FORMAT_R32_FLOAT: return "R32F";
    case DXGI_FORMAT_D32_FLOAT: return "D32";
    case DXGI_FORMAT_D24_UNORM_S8_UINT: return "D24S8";
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return "D32S8";
    case DXGI_FORMAT_D16_UNORM: return "D16";
    default: return nullptr;
    }
}
std::string fmt(DXGI_FORMAT f) {
    if (auto n = fmtName(f)) return n;
    return "fmt" + std::to_string(int(f));
}
char const* cmpName(D3D12_COMPARISON_FUNC c) {
    switch (c) {
    case D3D12_COMPARISON_FUNC_NEVER: return "never";
    case D3D12_COMPARISON_FUNC_LESS: return "less";
    case D3D12_COMPARISON_FUNC_EQUAL: return "equal";
    case D3D12_COMPARISON_FUNC_LESS_EQUAL: return "less_eq";
    case D3D12_COMPARISON_FUNC_GREATER: return "greater";
    case D3D12_COMPARISON_FUNC_NOT_EQUAL: return "not_eq";
    case D3D12_COMPARISON_FUNC_GREATER_EQUAL: return "greater_eq";
    default: return "always";
    }
}

/// Output signature of a DXBC/DXIL container (OSG1 / OSG5 / OSGN part). Read straight from the bytes - no
/// d3dcompiler / dxcompiler dependency.
std::string outputSignature(void const* code, size_t n) {
    auto const* b = static_cast<uint8_t const*>(code);
    if (n < 32 || std::memcmp(b, "DXBC", 4) != 0) return "?";
    uint32_t parts = 0;
    std::memcpy(&parts, b + 28, 4);
    if (32 + size_t(parts) * 4 > n) return "?";
    for (uint32_t i = 0; i < parts; ++i) {
        uint32_t off = 0;
        std::memcpy(&off, b + 32 + i * 4, 4);
        if (size_t(off) + 8 > n) continue;
        char     fourcc[5]{};
        uint32_t size = 0;
        std::memcpy(fourcc, b + off, 4);
        std::memcpy(&size, b + off + 4, 4);
        uint8_t const* p   = b + off + 8;
        size_t         end = std::min(n, size_t(off) + 8 + size);
        bool const     v1 = !std::strcmp(fourcc, "OSG1"), v5 = !std::strcmp(fourcc, "OSG5"), v0 = !std::strcmp(fourcc, "OSGN");
        if (!v1 && !v5 && !v0) continue;
        uint32_t count = 0, first = 0;
        std::memcpy(&count, p, 4);
        std::memcpy(&first, p + 4, 4);
        size_t const stride = v1 ? 32 : v5 ? 28 : 24, nameAt = v1 || v5 ? 4 : 0, idxAt = nameAt + 4, regAt = nameAt + 16,
                     maskAt = nameAt + 20;
        std::string s;
        for (uint32_t e = 0; e < count; ++e) {
            uint8_t const* el = p + first + e * stride;
            if (el + stride > b + end) break;
            uint32_t nameOff = 0, idx = 0, reg = 0;
            std::memcpy(&nameOff, el + nameAt, 4);
            std::memcpy(&idx, el + idxAt, 4);
            std::memcpy(&reg, el + regAt, 4);
            uint8_t mask = el[maskAt];
            std::string name;
            for (uint8_t const* c = p + nameOff; c < b + end && *c; ++c) name += char(*c);
            std::string m;
            for (int k = 0; k < 4; ++k)
                if (mask & (1 << k)) m += "xyzw"[k];
            s += (s.empty() ? "" : " ") + name + std::to_string(idx) + "." + m + "@o" + std::to_string(reg);
        }
        return s;
    }
    return "(no output signature part)";
}

/// Name every shader in the game's .material.bin files (file + pass + stage), once, on a worker thread.
void learnNames() {
    if (gLearning.exchange(true)) return;
    std::thread([] {
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        fs::path dir = fs::path(exe).parent_path() / "data" / "renderer" / "materials";
        std::unordered_map<uint64_t, std::string> names;
        std::error_code ec;
        long files = 0;
        for (auto const& e : fs::directory_iterator(dir, ec)) {
            if (!e.is_regular_file() || e.path().extension() != ".bin") continue;
            std::ifstream in(e.path(), std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
            for (auto const& s : rf::skyscan::scan(bytes.data(), bytes.size()))
                names.emplace(s.hash, e.path().filename().string() + (s.pass.empty() ? "" : " " + s.pass) + " " + s.stage + " " + s.platform);
            ++files;
        }
        // Bedrock Forge serves its shaderpacks' materials by redirecting the file open (RF never sees those bytes), so
        // an item / entity material a pack replaces has a pipeline hash no vanilla file names - learn the packs too
        // (Glass Items waits for "ItemInHand" names: with the Cloud Sea pack on it found none - 2026-11-18)
        long packFiles = 0;
        for (auto const& pk : fs::directory_iterator(fs::path(exe).parent_path() / "mods" / "BedrockForgeCore" / "shaderpacks", ec)) {
            std::error_code ec2;
            if (!pk.is_directory(ec2)) continue;
            for (auto const& e : fs::directory_iterator(pk.path() / "renderer" / "materials", ec2)) {
                if (!e.is_regular_file() || e.path().extension() != ".bin") continue;
                std::ifstream in(e.path(), std::ios::binary);
                std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
                for (auto const& s : rf::skyscan::scan(bytes.data(), bytes.size()))
                    names.emplace(s.hash, e.path().filename().string() + (s.pass.empty() ? "" : " " + s.pass) + " " + s.stage + " " + s.platform + " [" + pk.path().filename().string() + "]");
                ++packFiles;
            }
        }
        {
            std::lock_guard l(gMutex);
            gShaderNames.swap(names);
        }
        gNamesLearned = true;
        ll_shim::info("[RF] glass armor probe: named shaders from " + std::to_string(files) + " material files in " + dir.string() + " + " + std::to_string(packFiles) + " shaderpack files");
    }).detach();
}

std::string nameOf(uint64_t h) { // the game's own name first: a served file can still carry unchanged vanilla blobs
    if (auto it = gShaderNames.find(h); it != gShaderNames.end()) return it->second;
    auto s = gServedNames.find(h);
    return s == gServedNames.end() ? std::string("?") : s->second;
}
std::string hex(uint64_t h) {
    char b[20];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(h));
    return b;
}

void writeReport(std::string const& extra = {}) {
    std::ostringstream s;
    std::time_t now = std::time(nullptr);
    char        ts[32];
    std::strftime(ts, sizeof ts, "%Y%m%d-%H%M%S", std::localtime(&now));
    s << "# Glass Armor M0 probe " << ts << "\n";
    s << "# pipelines that drew with armor ON but not with armor OFF (= armor candidates), then the full ON frame\n";
    std::lock_guard l(gMutex);
    auto line = [&](ID3D12PipelineState* p, DrawStat const& d, char const* tag) {
        auto it = gPsos.find(p);
        s << tag << " pso " << p << " draws " << d.draws << " prims " << d.prims;
        if (it == gPsos.end()) { s << " (created before the hooks)\n"; return; }
        auto const& i = it->second;
        s << "\n    ps " << hex(i.ps) << "  " << nameOf(i.ps) << "\n    vs " << hex(i.vs) << "  " << nameOf(i.vs)
          << "\n    vs out: " << i.vsOut << "\n    in: " << i.inputs << "\n    " << i.targets << "  root " << i.root << "\n";
    };
    s << "# M0b: totals per pixel shader, ON vs OFF (pipeline copies merged); '*' = differs\n";
    {
        std::map<uint64_t, int> all;
        for (auto const& [h, d] : gByPs[0]) all[h] |= 1;
        for (auto const& [h, d] : gByPs[1]) all[h] |= 2;
        for (auto const& [h, m] : all) {
            DrawStat on = gByPs[0].count(h) ? gByPs[0].at(h) : DrawStat{}, off = gByPs[1].count(h) ? gByPs[1].at(h) : DrawStat{};
            bool diff = on.draws != off.draws || on.prims != off.prims;
            s << (diff ? "* " : "  ") << hex(h) << " ON " << on.draws << "/" << on.prims << "  OFF " << off.draws << "/" << off.prims
              << "  " << nameOf(h) << "\n";
        }
    }
    for (int w = 0; w < 2; ++w) {
        s << "\n# M0b: actor-family draws, " << (w ? "OFF" : "ON") << " frame, in order (shader | index count | bound textures)\n";
        for (auto const& line : gDetail[w]) s << line << "\n";
    }
    s << "\n";
    if (!extra.empty()) s << extra << "\n";
    long cand = 0;
    for (auto const& [p, d] : gFrame[0])
        if (!gFrame[1].count(p)) { line(p, d, "ARMOR?"); ++cand; }
    s << "\n# full frame, armor ON (" << gFrame[0].size() << " pipelines)\n";
    for (auto const& [p, d] : gFrame[0]) line(p, d, gFrame[1].count(p) ? "both  " : "ON    ");
    s << "\n# pipelines only in the OFF frame\n";
    for (auto const& [p, d] : gFrame[1])
        if (!gFrame[0].count(p)) line(p, d, "OFF   ");
    std::error_code ec;
    fs::create_directories(gOutDir, ec);
    fs::path f = gOutDir / (std::string(ts) + ".txt");
    std::ofstream(f) << s.str();
    gLastReport = f.string();
    gStatus     = std::to_string(cand) + " armor candidate pipeline(s) - report written";
    ll_shim::info("[RF] glass armor probe: " + gStatus + ": " + gLastReport);
}

} // namespace

void init(fs::path const& outDir, fs::path const& configDir) {
    gOutDir     = outDir;
    gConfigFile = configDir / "GlassArmor.txt";
    gFrostFile  = configDir / "GlassFrost.txt";
    gBodyFile   = configDir / "GlassBody.txt";
    loadFrost();
    loadBody();
    std::ifstream in(gConfigFile);
    std::string   v;
    if (in >> v) gEnabled = v == "1";
}

namespace {
using CreateDevice_t = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
using CreateRs_t     = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, UINT, void const*, SIZE_T, REFIID, void**);
using CreateRtv_t    = void(STDMETHODCALLTYPE*)(ID3D12Device*, ID3D12Resource*, D3D12_RENDER_TARGET_VIEW_DESC const*, D3D12_CPU_DESCRIPTOR_HANDLE);
using CreateDsv_t    = void(STDMETHODCALLTYPE*)(ID3D12Device*, ID3D12Resource*, D3D12_DEPTH_STENCIL_VIEW_DESC const*, D3D12_CPU_DESCRIPTOR_HANDLE);
void*            gOrigCreateDevice = nullptr;
void*            gOrigCreateRs     = nullptr;
void*            gOrigCreateRtv    = nullptr;
void*            gOrigCreateDsv    = nullptr;
void STDMETHODCALLTYPE createRtvHook(ID3D12Device* d, ID3D12Resource* r, D3D12_RENDER_TARGET_VIEW_DESC const* desc, D3D12_CPU_DESCRIPTOR_HANDLE h) {
    reinterpret_cast<CreateRtv_t>(gOrigCreateRtv)(d, r, desc, h);
    rf::uicapture::noteRtv(h.ptr, r);
}
void STDMETHODCALLTYPE createDsvHook(ID3D12Device* d, ID3D12Resource* r, D3D12_DEPTH_STENCIL_VIEW_DESC const* desc, D3D12_CPU_DESCRIPTOR_HANDLE h) {
    reinterpret_cast<CreateDsv_t>(gOrigCreateDsv)(d, r, desc, h);
    noteDsv(h.ptr, r);
}
std::atomic_bool gRsHooked{false};
HRESULT STDMETHODCALLTYPE createRsHook(ID3D12Device* d, UINT mask, void const* blob, SIZE_T size, REFIID iid, void** out) {
    HRESULT hr = reinterpret_cast<CreateRs_t>(gOrigCreateRs)(d, mask, blob, size, iid, out);
    if (SUCCEEDED(hr) && out && *out) {
        ID3D12RootSignature* rs = nullptr;
        if (SUCCEEDED(static_cast<IUnknown*>(*out)->QueryInterface(IID_PPV_ARGS(&rs))) && rs) {
            noteRootSig(rs, blob, size);
            rs->Release();
        }
    }
    return hr;
}
std::atomic<ID3D12Device*> gGameDev{nullptr};   // the game's own device (kept alive): hook targets are read from IT, not a probe
thread_local bool          gProbing = false;      // RF's own probe device is not the game's
HRESULT WINAPI createDeviceHook(IUnknown* adapter, D3D_FEATURE_LEVEL level, REFIID riid, void** out) {
    if (!gProbing && !gGameDev.load()) rf::dred::beforeDevice();   // GPU crash report (opt-in): must be on before the device exists
    HRESULT hr = reinterpret_cast<CreateDevice_t>(gOrigCreateDevice)(adapter, level, riid, out);
    if (SUCCEEDED(hr) && out && *out && !gProbing && !gGameDev.load()) {
        ID3D12Device* d = nullptr;
        if (SUCCEEDED(static_cast<IUnknown*>(*out)->QueryInterface(IID_PPV_ARGS(&d))) && d) {
            ID3D12Device* none = nullptr;
            if (!gGameDev.compare_exchange_strong(none, d)) d->Release();   // keep the first one (with our reference)
            else rf::dred::watch(d);
        }
    }
    if (SUCCEEDED(hr) && out && *out && !gRsHooked.load()) {
        ID3D12Device* dev = nullptr;
        if (SUCCEEDED(static_cast<IUnknown*>(*out)->QueryInterface(IID_PPV_ARGS(&dev))) && dev) {
            void** vt = *reinterpret_cast<void***>(dev);
            if (!gRsHooked.exchange(true)) {
                int rc  = ll_shim::hook(vt[16], reinterpret_cast<void*>(&createRsHook), &gOrigCreateRs);
                int rc2 = ll_shim::hook(vt[20], reinterpret_cast<void*>(&createRtvHook), &gOrigCreateRtv);
                int rc3 = ll_shim::hook(vt[21], reinterpret_cast<void*>(&createDsvHook), &gOrigCreateDsv);
                ll_shim::info("[RF] glass armor: CreateRootSignature / RTV / DSV hooked at device creation (rc " + std::to_string(rc) + "/" +
                              std::to_string(rc2) + "/" + std::to_string(rc3) + ")");
                if (rc != 0) gRsHooked = false;
            }
            dev->Release();
        }
    }
    return hr;
}
} // namespace

ID3D12Device* gameDevice() { return gGameDev.load(); }
void          setProbing(bool on) { gProbing = on; }
void installEarly() {
    HMODULE m = rf::d3d12api::module();
    void*   f = m ? reinterpret_cast<void*>(GetProcAddress(m, "D3D12CreateDevice")) : nullptr;
    if (!f) { ll_shim::warn("[RF] glass armor: D3D12CreateDevice not found - root signatures won't be seen"); return; }
    int rc = ll_shim::hook(f, reinterpret_cast<void*>(&createDeviceHook), &gOrigCreateDevice);
    ll_shim::info("[RF] glass armor: D3D12CreateDevice hooked (rc " + std::to_string(rc) + ")");
}

namespace {
bool armorOn() { return gEnabled.load(std::memory_order_relaxed) && gArmour.load(std::memory_order_relaxed); }
}
bool bodyOn() { return gSelf.load(std::memory_order_relaxed) || gOthers.load(std::memory_order_relaxed); }
bool itemsOn() { return gItems.load(std::memory_order_relaxed); }
bool enabled() { return armorOn() || bodyOn() || itemsOn(); }
bool tracking() {
    return enabled() || gCap.load(std::memory_order_relaxed) != 0 || rf::worldfx::tracking();
}

void noteBarriers(unsigned n, D3D12_RESOURCE_BARRIER const* b) {
    std::lock_guard l(gMutex);
    for (unsigned i = 0; i < n; ++i)
        if (b[i].Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION && b[i].Transition.pResource)
            gResState[b[i].Transition.pResource] = unsigned(b[i].Transition.StateAfter);
}

void notePso(ID3D12PipelineState* pso, D3D12_GRAPHICS_PIPELINE_STATE_DESC const& d, uint64_t psHash) {
    PsoInfo i;
    i.ps = psHash;
    if (d.VS.pShaderBytecode && d.VS.BytecodeLength) {
        i.vs    = fnv(d.VS.pShaderBytecode, d.VS.BytecodeLength);
        i.vsOut = outputSignature(d.VS.pShaderBytecode, d.VS.BytecodeLength);
    }
    for (UINT k = 0; k < d.InputLayout.NumElements; ++k) {
        auto const& e = d.InputLayout.pInputElementDescs[k];
        i.inputs += (k ? " " : "") + std::string(e.SemanticName ? e.SemanticName : "?") + std::to_string(e.SemanticIndex) + ":" + fmt(e.Format);
    }
    std::string t = "rt";
    for (UINT k = 0; k < d.NumRenderTargets; ++k) t += " " + fmt(d.RTVFormats[k]);
    t += " | ds " + fmt(d.DSVFormat);
    if (d.DepthStencilState.DepthEnable)
        t += std::string(" ") + cmpName(d.DepthStencilState.DepthFunc) + (d.DepthStencilState.DepthWriteMask ? " write" : " nowrite");
    else t += " off";
    t += d.BlendState.RenderTarget[0].BlendEnable ? " | blend on" : " | blend off";
    t += d.RasterizerState.CullMode == D3D12_CULL_MODE_NONE ? " | cull none" : " | cull";
    i.targets = t;
    i.root    = d.pRootSignature;
    std::shared_ptr<ActorDesc> ad;
    bool const glintVs = i.vsOut == kGlintVsOut;
    bool const itemVs  = i.vsOut == kItemVsOut;
    if ((i.vsOut == kActorVsOut || glintVs || itemVs) && d.VS.pShaderBytecode && d.InputLayout.NumElements) {
        ad        = std::make_shared<ActorDesc>();
        ad->d     = d;
        ad->glint = glintVs;
        ad->vs.assign(static_cast<uint8_t const*>(d.VS.pShaderBytecode), static_cast<uint8_t const*>(d.VS.pShaderBytecode) + d.VS.BytecodeLength);
        for (UINT k = 0; k < d.InputLayout.NumElements; ++k) {
            auto e = d.InputLayout.pInputElementDescs[k];
            ad->names.emplace_back(e.SemanticName ? e.SemanticName : "");
            e.SemanticName = ad->names.back().c_str();
            ad->il.push_back(e);
        }
        ad->root             = d.pRootSignature;
        ad->d.VS             = {ad->vs.data(), ad->vs.size()};
        ad->d.InputLayout    = {ad->il.data(), UINT(ad->il.size())};
        ad->d.pRootSignature = ad->root.Get();
        ad->d.HS = ad->d.DS = ad->d.GS = {};
        ad->d.StreamOutput = {};
        ad->d.CachedPSO    = {};
    }
    std::lock_guard l(gMutex);
    gPsos[pso] = std::move(i);
    if (ad && itemVs) gItemPso[pso] = std::move(ad);
    else if (ad) gActor[pso] = std::move(ad);
}

bool capturing() { return gCap.load(std::memory_order_relaxed) == 2; }

void onSetPso(ID3D12GraphicsCommandList* list, ID3D12PipelineState* pso) {
    std::lock_guard l(gMutex);
    gListPso[list] = pso;
}

bool wantRoot() { return tracking(); }
bool wantSrvs() { return tracking(); }

void noteSrv(size_t cpu, ID3D12Resource* r) {
    SrvInfo i;
    i.res = r;
    if (r) {
        auto d = r->GetDesc();
        i.w    = unsigned(d.Width);
        i.h    = d.Height;
        i.fmt  = unsigned(d.Format);
        i.mips = d.MipLevels;
    }
    std::lock_guard l(gMutex);
    if (gSrvs.size() < 400000 || gSrvs.count(cpu)) gSrvs[cpu] = i;
}

void copySrv(size_t dst, size_t src) {
    std::lock_guard l(gMutex);
    auto it = gSrvs.find(src);
    if (it != gSrvs.end()) { if (gSrvs.size() < 400000 || gSrvs.count(dst)) gSrvs[dst] = it->second; }
    else gSrvs.erase(dst);
}

bool actorFamily(std::string const& n) {
    return n.find("Actor") != std::string::npos || n.find("Entity") != std::string::npos ||
           n.find("Armor") != std::string::npos || n.find("Item") != std::string::npos || n.find("Hand") != std::string::npos ||
           n.find("Player") != std::string::npos;   // (Item / Hand / Player: the Glass Body / Items probe)
}

void onDraw(ID3D12GraphicsCommandList* list, unsigned count, unsigned inst) {
    ID3D12PipelineState* pso = nullptr;
    uint64_t             ps  = 0;
    bool                 detail = false;
    std::string          sig = "?";
    void*                root = nullptr;
    {
        std::lock_guard l(gMutex);
        auto it = gListPso.find(list);
        if (it == gListPso.end()) return;
        pso = it->second;
        auto& d = gFrame[gWhich][pso];
        ++d.draws;
        d.prims += static_cast<unsigned long long>(count / 3) * std::max(1u, inst);
        if (auto p = gPsos.find(pso); p != gPsos.end()) {
            ps = p->second.ps;
            auto& b = gByPs[gWhich][ps];
            ++b.draws;
            b.prims += static_cast<unsigned long long>(count / 3) * std::max(1u, inst);
            detail = actorFamily(nameOf(ps)) && gDetail[gWhich].size() < 600;
            if (detail) {   // Glass Body probe: which vertex output + root signature this draw uses
                auto const& vo = p->second.vsOut;
                if (vo == kActorVsOut) sig = "ACTOR";
                else if (vo == kGlintVsOut) sig = "GLINT";
                else {
                    auto s = std::find(gSigs.begin(), gSigs.end(), vo);
                    if (s == gSigs.end() && gSigs.size() < 64) s = gSigs.insert(gSigs.end(), vo);
                    sig = s == gSigs.end() ? "?" : "S" + std::to_string(s - gSigs.begin());
                }
                root = p->second.root;
                ++gProbeRoots[root];
            }
        }
    }
    if (!detail) return;
    // bound textures: every root descriptor table, first 4 descriptors of each (UiCapture lock, then ours)
    auto tables = rf::uicapture::rootTables(list);
    std::vector<std::tuple<unsigned, int, size_t>> cpus;
    std::string                                    dbg;
    for (auto const& [ri, gpu] : tables) {
        size_t   cpu = 0;
        unsigned inc = 0;
        char     tb[64];
        if (rf::uicapture::gpuDescToCpu(gpu, cpu, inc)) {
            for (int k = 0; k < 4; ++k) cpus.emplace_back(ri, k, cpu + size_t(k) * inc);
            std::snprintf(tb, sizeof tb, " r%u:%llx->%zx", ri, gpu, cpu);
        } else std::snprintf(tb, sizeof tb, " r%u:%llx->?", ri, gpu);
        dbg += tb;
    }
    std::ostringstream s;
    s << hex(ps) << " " << nameOf(ps) << " | idx " << count << " inst " << inst << " | pso " << pso << " sig " << sig << " root " << root
      << " | tex:";
    std::lock_guard l(gMutex);
    for (auto const& [ri, k, cpu] : cpus) {
        auto it = gSrvs.find(cpu);
        if (it == gSrvs.end()) continue;
        auto const& v = it->second;
        s << " [r" << ri << "." << k << " " << v.w << "x" << v.h << " " << fmt(DXGI_FORMAT(v.fmt)) << " m" << v.mips << " " << v.res << "]";
        if (v.res && skinSized(v.w, v.h) && gProbeTex.size() < 48) gProbeTex.emplace(v.res, v.res);   // checked against BF's skins later
    }
    if (tables.empty()) s << " (no root tables tracked)";
    s << " | tables" << dbg << " | srvs known " << gSrvs.size();
    gDetail[gWhich].push_back(s.str());
}

namespace {
bool shadersBuilt() { return shaders::kVariant[0].psSize > 64; }

/// The armor texture (64x32, vanilla layout) this draw binds, if any: first table slot holding a 64x32 texture.
ID3D12Resource* boundArmorTexture(ID3D12GraphicsCommandList* list) {
    auto tables = rf::uicapture::rootTables(list);
    std::vector<size_t> cpus;
    for (auto const& [ri, gpu] : tables) {
        size_t   cpu = 0;
        unsigned inc = 0;
        if (rf::uicapture::gpuDescToCpu(gpu, cpu, inc)) cpus.push_back(cpu);
    }
    std::lock_guard l(gMutex);
    for (size_t cpu : cpus) {
        auto it = gSrvs.find(cpu);
        if (it != gSrvs.end() && it->second.res && ((it->second.w == 64 && it->second.h == 32) || (it->second.w == 128 && it->second.h == 64)))
            return it->second.res;   // 128x64: a 32x pack's armour (Cloud Sea's leather is one)
    }
    return nullptr;
}

/// Glass Body: the skin-sized texture (64x64 / 128x128) this draw binds, if any (first table slot holding one).
ID3D12Resource* boundSkinTexture(ID3D12GraphicsCommandList* list, SrvInfo* info) {
    auto tables = rf::uicapture::rootTables(list);
    std::vector<size_t> cpus;
    for (auto const& [ri, gpu] : tables) {
        size_t   cpu = 0;
        unsigned inc = 0;
        if (rf::uicapture::gpuDescToCpu(gpu, cpu, inc)) cpus.push_back(cpu);
    }
    std::lock_guard l(gMutex);
    for (size_t cpu : cpus) {
        auto it = gSrvs.find(cpu);
        if (it != gSrvs.end() && it->second.res && it->second.w == it->second.h && (it->second.w == 64 || it->second.w == 128)) {
            *info = it->second;
            return it->second.res;
        }
    }
    return nullptr;
}

/// Match a read-back 64x32 RGBA8 texture against the vanilla armor shapes (+ mean colour to split shared shapes).
int classify(uint8_t const* px, unsigned pitch) {
    uint32_t bits[64]{};
    long     sum[3]{}, cnt = 0;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 64; ++x) {
            uint8_t const* p = px + y * pitch + x * 4;
            if (p[3] == 0) continue;
            bits[y * 2 + (x >> 5)] |= 1u << (31 - (x & 31));
            sum[0] += p[0], sum[1] += p[1], sum[2] += p[2], ++cnt;
        }
    if (!cnt) { gLastClassInfo = "empty (all alpha 0)"; return -1; }
    // shape distance = differing texels (packs and versions move a few pixels); colour splits shared shapes
    // A pack can draw one armour on another's shape (Cloud Sea's netherite has diamond's outline - it was classified
    // as diamond, seen in game 2026-11-11): every shape within 6 texels of the nearest is a candidate, and the
    // mean colour picks among them.
    static_assert(shaders::kVariants <= 32, "classify: variant arrays");
    constexpr int kN = shaders::kVariants;
    int  ham[32]{};
    int64_t dist[32]{};
    int  bestHam = 1 << 30;
    for (int v = 0; v < kN; ++v) {
        for (int k = 0; k < 64; ++k) ham[v] += __popcnt(bits[k] ^ shaders::kVariant[v].mask[k]);
        for (int c = 0; c < 3; ++c) { int64_t const e = sum[c] / cnt - shaders::kVariant[v].avg[c]; dist[v] += e * e; }
        bestHam = std::min(bestHam, ham[v]);
    }
    int  best = -1;
    int64_t bestD = INT64_MAX;   // (was 1L << 40: long is 32-bit on Windows - undefined, and the optimiser took the loop's end out)
    for (int v = 0; v < kN; ++v)
        if (ham[v] <= bestHam + 6 && dist[v] < bestD) { bestD = dist[v]; best = v; }
    if (bestD > 3 * 50 * 50)   // the nearest shapes are all the wrong colour: a repaint on another armour's shape
        for (int v = 0; v < kN; ++v)
            if (ham[v] <= 130 && dist[v] < bestD) { bestD = dist[v]; best = v; }
    // the colour is that armour's: its shape may differ more (a pack's netherite on diamond's outline: 95 texels off;
    // this game's chain against the samples': 119 off - both seen in game 2026-11-11)
    int const limit = best >= 0 && dist[best] <= 3 * 30 * 30 ? 130 : 40;
    bestHam = best >= 0 ? ham[best] : bestHam;
    char b[160];
    std::snprintf(b, sizeof b, "%ld painted, mean %ld,%ld,%ld, nearest %s_%d (%d texels off)", cnt, sum[0] / cnt, sum[1] / cnt, sum[2] / cnt,
                  best >= 0 ? shaders::kVariant[best].mat : "-", best >= 0 ? shaders::kVariant[best].layer : 0, bestHam);
    gLastClassInfo = b;
    return bestHam <= limit ? best : -1;
}

ComPtr<ID3D12PipelineState> makeGlassPso(ID3D12Device* dev, ActorDesc const& ad, int v) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = ad.d;
    unsigned base = 0;
    ID3D12RootSignature* ext = extendedRoot(ad.root.Get(), &base);
    if (!ext) { ++gPsoFail; return nullptr; }
    d.pRootSignature = ext;
    d.PS = ad.glint ? D3D12_SHADER_BYTECODE{shaders::kVariant[v].psGlint, shaders::kVariant[v].psGlintSize}
                   : D3D12_SHADER_BYTECODE{shaders::kVariant[v].ps, shaders::kVariant[v].psSize};
    // the shader composites the copied scene itself: opaque output, written depth (drawn after every actor part)
    auto& rt = d.BlendState.RenderTarget[0];
    rt.BlendEnable           = FALSE;
    rt.LogicOpEnable         = FALSE;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    d.DepthStencilState.StencilEnable  = FALSE; // replayed later: the game's stencil ref may have moved on
    d.RasterizerState.CullMode         = D3D12_CULL_MODE_NONE; // like the game's armor PSO (its winding varies); depth write keeps the nearest face
    ComPtr<ID3D12PipelineState> p;
    HRESULT hr = dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&p));
    if (FAILED(hr)) {
        ++gPsoFail;
        char b[16];
        std::snprintf(b, sizeof b, "%08lx", static_cast<unsigned long>(hr));
        ll_shim::warn(std::string("[RF] glass armor: pipeline for ") + shaders::kVariant[v].mat + " failed 0x" + b);
        return nullptr;
    }
    return p;
}

bool bodyShaderBuilt() { return sizeof(shaders::kGlassBodyPs) > 64; }
bool itemShaderBuilt() { return sizeof(shaders::kGlassItemPs) > 64; }
/// Glass Items: the atlas this item draw samples = the biggest mip-mapped texture among the first descriptors of its
/// first texture table (probe: r1.0 16x16 lightmap, r1.1 1024x1024 m4 the item atlas).
/// Glass Body's pipeline: the game's Actor pipeline with our body pixel shader and the body root signature; the same
/// state changes as the armor's (opaque, depth written, replayed after the actor pass).
ComPtr<ID3D12PipelineState> makeBodyPso(ID3D12Device* dev, ActorDesc const& ad) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = ad.d;
    unsigned base = 0;
    ID3D12RootSignature* ext = extendedRootBody(ad.root.Get(), &base);
    if (!ext) { ++gBodyPsoFail; return nullptr; }
    d.pRootSignature = ext;
    d.PS             = {shaders::kGlassBodyPs, sizeof(shaders::kGlassBodyPs)};
    auto& rt = d.BlendState.RenderTarget[0];
    rt.BlendEnable           = FALSE;
    rt.LogicOpEnable         = FALSE;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    d.DepthStencilState.StencilEnable  = FALSE;
    d.RasterizerState.CullMode         = D3D12_CULL_MODE_NONE;
    ComPtr<ID3D12PipelineState> p;
    HRESULT hr = dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&p));
    if (FAILED(hr)) {
        ++gBodyPsoFail;
        char b[16];
        std::snprintf(b, sizeof b, "%08lx", static_cast<unsigned long>(hr));
        ll_shim::warn(std::string("[RF] glass body: pipeline failed 0x") + b);
        return nullptr;
    }
    return p;
}

/// Glass Items' pipeline: the game's ItemInHand pipeline with our item pixel shader (same root as the body: + t3).
ComPtr<ID3D12PipelineState> makeItemPso(ID3D12Device* dev, ActorDesc const& ad) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = ad.d;
    unsigned base = 0;
    ID3D12RootSignature* ext = extendedRootBody(ad.root.Get(), &base);
    if (!ext) { ++gItemPsoFail; return nullptr; }
    d.pRootSignature = ext;
    d.PS             = {shaders::kGlassItemPs, sizeof(shaders::kGlassItemPs)};
    auto& rt = d.BlendState.RenderTarget[0];
    rt.BlendEnable = FALSE, rt.LogicOpEnable = FALSE, rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    d.DepthStencilState.StencilEnable  = FALSE;
    d.RasterizerState.CullMode         = D3D12_CULL_MODE_NONE;
    ComPtr<ID3D12PipelineState> p;
    HRESULT hr = dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&p));
    if (FAILED(hr)) {
        ++gItemPsoFail;
        char b[16];
        std::snprintf(b, sizeof b, "%08lx", static_cast<unsigned long>(hr));
        ll_shim::warn(std::string("[RF] glass items: pipeline failed 0x") + b);
        return nullptr;
    }
    return p;
}

/// Present: read back newly seen 64x32 actor textures (a few per frame) and classify the finished ones.
void pumpReadbacks(ID3D12Device* dev, ID3D12CommandQueue* q) {
    if (!gFence && FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gFence)))) return;
    if (!gInFlight.empty()) {
        if (gFence->GetCompletedValue() < gFenceVal) return;
        for (auto& r : gInFlight) {
            int   kind = -1;
            void* m    = nullptr;
            D3D12_RANGE rr{0, size_t(256 * r.scale) * size_t(32 * r.scale)};
            if (SUCCEEDED(r.buf->Map(0, &rr, &m)) && m) {
                if (r.scale == 1) kind = classify(static_cast<uint8_t const*>(m), 256);
                else {   // a 32x pack: every second texel of every second row is the 64x32 layout
                    static uint8_t half[256 * 32];
                    auto const* src = static_cast<uint8_t const*>(m);
                    for (int y = 0; y < 32; ++y)
                        for (int x = 0; x < 64; ++x) std::memcpy(half + y * 256 + x * 4, src + (y * 2) * 512 + (x * 2) * 4, 4);
                    kind = classify(half, 256);
                }
                if (gDumpReadbacks.load(std::memory_order_relaxed) && gDumped < 24) { // raw RGBA 64x32 (pitch 256) for diagnosis
                    std::error_code ec;
                    fs::create_directories(gOutDir, ec);
                    std::ofstream(gOutDir / ("readback_" + std::to_string(gDumped++) + ".rgba"), std::ios::binary)
                        .write(static_cast<char const*>(m), 256 * 32);
                }
                ll_shim::info("[RF] glass armor: readback " + gLastClassInfo);
                D3D12_RANGE none{0, 0};
                r.buf->Unmap(0, &none);
            }
            std::lock_guard l(gMutex);
            gTexKind[r.tex.Get()] = kind;
            if (kind >= 0) {
                ++gArmorTex;
                gLastClass = std::string(shaders::kVariant[kind].mat) + "_" + std::to_string(shaders::kVariant[kind].layer);
                ll_shim::info("[RF] glass armor: recognised " + gLastClass + " texture");
            } else ++gNotArmor;
        }
        gInFlight.clear();
    }
    std::vector<ComPtr<ID3D12Resource>> todo;
    {
        std::lock_guard l(gMutex);
        while (!gPending.empty() && todo.size() < 8) {
            todo.push_back(gPending.back());
            gPending.pop_back();
        }
    }
    if (todo.empty()) return;
    if (!gAlloc && FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&gAlloc)))) return;
    if (!gList) {
        if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, gAlloc.Get(), nullptr, IID_PPV_ARGS(&gList)))) return;
    } else {
        gAlloc->Reset();
        gList->Reset(gAlloc.Get(), nullptr);
    }
    for (auto& tex : todo) {
        auto td = tex->GetDesc();
        int const sc = td.Width == 128 && td.Height == 64 ? 2 : 1;
        if (td.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || td.Width != 64u * sc || td.Height != 32u * sc ||
            (td.Format != DXGI_FORMAT_R8G8B8A8_UNORM && td.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)) {
            std::lock_guard l(gMutex);
            gTexKind[tex.Get()] = -1;
            continue;
        }
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_READBACK};
        D3D12_RESOURCE_DESC   bd{};
        bd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width            = UINT64(256 * sc) * UINT64(32 * sc);
        bd.Height           = 1;
        bd.DepthOrArraySize = 1;
        bd.MipLevels        = 1;
        bd.SampleDesc.Count = 1;
        bd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> buf;
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&buf)))) continue;
        unsigned st = 0;
        {
            std::lock_guard l(gMutex);
            auto it = gResState.find(tex.Get());
            st      = it == gResState.end() ? 0u : it->second; // unknown: COMMON (implicit promotion to COPY_SOURCE)
        }
        bool const barrier = st != 0 && st != unsigned(D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = tex.Get();
        b.Transition.Subresource = 0;
        b.Transition.StateBefore = D3D12_RESOURCE_STATES(st);
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
        if (barrier) gList->ResourceBarrier(1, &b);
        D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
        dst.pResource                          = buf.Get();
        dst.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Footprint.Format   = td.Format;
        dst.PlacedFootprint.Footprint.Width    = 64 * sc;
        dst.PlacedFootprint.Footprint.Height   = 32 * sc;
        dst.PlacedFootprint.Footprint.Depth    = 1;
        dst.PlacedFootprint.Footprint.RowPitch = 256 * sc;
        src.pResource                          = tex.Get();
        src.Type                               = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex                   = 0;
        gList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        if (barrier) {
            std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
            gList->ResourceBarrier(1, &b);
        }
        gInFlight.push_back({tex, buf, sc});
    }
    gList->Close();
    if (gInFlight.empty()) return;
    ID3D12CommandList* lists[] = {gList.Get()};
    q->ExecuteCommandLists(1, lists);
    q->Signal(gFence.Get(), ++gFenceVal);
}

/// Root signature blob embedded in a DXBC/DXIL container (RTS0 part), if the game compiled it in.
bool rts0(std::vector<uint8_t> const& code, std::vector<uint8_t>& out) {
    if (code.size() < 32 || std::memcmp(code.data(), "DXBC", 4) != 0) return false;
    uint32_t parts = 0;
    std::memcpy(&parts, code.data() + 28, 4);
    for (uint32_t i = 0; i < parts && 32 + (i + 1) * 4 <= code.size(); ++i) {
        uint32_t off = 0, size = 0;
        std::memcpy(&off, code.data() + 32 + i * 4, 4);
        if (size_t(off) + 8 > code.size()) continue;
        std::memcpy(&size, code.data() + off + 4, 4);
        if (std::memcmp(code.data() + off, "RTS0", 4) == 0 && size_t(off) + 8 + size <= code.size()) {
            out.assign(code.data() + off + 8, code.data() + off + 8 + size);
            return true;
        }
    }
    return false;
}
} // namespace

bool isActorPso(ID3D12PipelineState* pso) {
    if (!pso) return false;
    std::lock_guard l(gMutex);
    return gActor.count(pso) != 0;
}
ID3D12PipelineState* listPso(ID3D12GraphicsCommandList* list) {
    std::lock_guard l(gMutex);
    auto it = gListPso.find(list);
    return it == gListPso.end() ? nullptr : it->second;
}
void noteDsv(size_t h, ID3D12Resource* r) {
    std::lock_guard l(gMutex);
    if (r) gDsvRes[h] = r;
    else gDsvRes.erase(h);
}
ID3D12Resource* dsvResource(size_t h) {
    std::lock_guard l(gMutex);
    auto it = gDsvRes.find(h);
    return it == gDsvRes.end() ? nullptr : it->second;
}
void noteRootSig(ID3D12RootSignature* rs, void const* blob, size_t size) {
    if (!rs || !blob || !size || size > 65536) return;
    std::lock_guard l(gMutex);
    gRsBlob[rs].assign(static_cast<uint8_t const*>(blob), static_cast<uint8_t const*>(blob) + size);
}

namespace {
std::unordered_map<ID3D12RootSignature*, ExtRoot> gExtBody; // Glass Body: the same + t3 space9 (the skin)
/// body = false: exactly the armor's signature (game's + 4); true: + 5 (the skin as root SRV t3 space9)
ID3D12RootSignature* extendedRootImpl(ID3D12RootSignature* game, unsigned* base, bool body) {
    if (!game) return nullptr;
    auto&                cache = body ? gExtBody : gExt;
    int const            extra = body ? 5 : 4;
    std::vector<uint8_t> blob;
    {
        std::lock_guard l(gMutex);
        auto e = cache.find(game);
        if (e != cache.end()) {
            if (e->second.failed) return nullptr;
            *base = e->second.base;
            return e->second.rs.Get();
        }
        auto b = gRsBlob.find(game);
        if (b != gRsBlob.end()) blob = b->second;
        else
            for (auto const& [p, ad] : gActor)
                if (ad->root.Get() == game && rts0(ad->vs, blob)) break;
    }
    ExtRoot er;
    auto    fail = [&](std::string why) {
        er.failed = true;
        gFlushWhy = "root signature: " + why;
        ll_shim::warn("[RF] glass armor: " + gFlushWhy);
    };
    ComPtr<ID3D12Device> dev;
    ComPtr<ID3D12VersionedRootSignatureDeserializer> des;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC const* vd = nullptr;
    if (blob.empty()) fail("game blob not seen (created before the hooks, none embedded in the shader)");
    else if (FAILED(game->GetDevice(IID_PPV_ARGS(&dev)))) fail("no device");
    else if (FAILED(rf::d3d12api::CreateVersionedRootSignatureDeserializer(blob.data(), blob.size(), IID_PPV_ARGS(&des))) ||
             FAILED(des->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_1, &vd)) || !vd)
        fail("could not read the game blob");
    else {
        auto const& g = vd->Desc_1_1;
        std::vector<D3D12_ROOT_PARAMETER1> params(g.pParameters, g.pParameters + g.NumParameters);
        unsigned cost = 0;
        for (auto const& p : params)
            cost += p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE ? 1
                  : p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS ? p.Constants.Num32BitValues : 2;
        if (cost + 2 * extra > 64) fail("game signature too large (" + std::to_string(cost) + " DWORDs)");
        else {
            er.base = unsigned(params.size());
            for (int k = 0; k < extra; ++k) { // +0 scene t0, +1 depth t1, +2 params b0, +3 last finished frame t2 (+4 body: skin t3)
                D3D12_ROOT_PARAMETER1 p{};
                p.ParameterType             = k == 2 ? D3D12_ROOT_PARAMETER_TYPE_CBV : D3D12_ROOT_PARAMETER_TYPE_SRV;
                p.Descriptor.ShaderRegister = k < 2 ? UINT(k) : k == 2 ? 0u : k == 3 ? 2u : 3u;
                p.Descriptor.RegisterSpace  = 9;
                p.Descriptor.Flags          = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
                p.ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;
                params.push_back(p);
            }
            D3D12_VERSIONED_ROOT_SIGNATURE_DESC nd{};
            nd.Version                  = D3D_ROOT_SIGNATURE_VERSION_1_1;
            nd.Desc_1_1                 = g;
            nd.Desc_1_1.NumParameters   = UINT(params.size());
            nd.Desc_1_1.pParameters     = params.data();
            ComPtr<ID3DBlob> out, err;
            if (FAILED(rf::d3d12api::SerializeVersionedRootSignature(&nd, &out, &err)))
                fail(std::string("serialize: ") + (err ? static_cast<char const*>(err->GetBufferPointer()) : "?"));
            else if (FAILED(dev->CreateRootSignature(0, out->GetBufferPointer(), out->GetBufferSize(), IID_PPV_ARGS(&er.rs))))
                fail("create");
        }
    }
    std::lock_guard l(gMutex);
    auto& slot = cache[game];
    if (!slot.rs && !slot.failed) slot = er;
    if (slot.failed) return nullptr;
    *base = slot.base;
    return slot.rs.Get();
}
} // namespace
ID3D12RootSignature* extendedRoot(ID3D12RootSignature* game, unsigned* base) { return extendedRootImpl(game, base, false); }
ID3D12RootSignature* extendedRootBody(ID3D12RootSignature* game, unsigned* base) { return extendedRootImpl(game, base, true); }

namespace {
void barrier(ID3D12GraphicsCommandList* l, ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
    if (a == b) return;
    D3D12_RESOURCE_BARRIER x{};
    x.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    x.Transition.pResource   = r;
    x.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    x.Transition.StateBefore = a;
    x.Transition.StateAfter  = b;
    l->ResourceBarrier(1, &x);
}
D3D12_RESOURCE_STATES knownState(ID3D12Resource* r, D3D12_RESOURCE_STATES fallback) {
    std::lock_guard l(gMutex);
    auto it = gResState.find(r);
    return it == gResState.end() ? fallback : D3D12_RESOURCE_STATES(it->second);
}
bool ensureBuffer(ID3D12Device* dev, ComPtr<ID3D12Resource>& b, UINT64& have, UINT64 need) {
    if (b && have >= need) return true;
    b.Reset();
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC   d{};
    d.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width            = need;
    d.Height           = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels        = 1;
    d.SampleDesc.Count = 1;
    d.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&b)))) return false;
    b->SetName(L"RF Glass frame copy");
    have = need;
    return true;
}
struct alignas(16) GaParams { // cbuffer GA (shaders/glass_armor.hlsl), 256-byte slot
    float    res[2];
    float    time, glint;
    uint32_t scenePitch, depthPitch;
    float    swirl, ripple;
    float    lens, uiScale;
    uint32_t sceneBgra, pad;
    // M2: LyricsForge
    float    beat, bass, impact, flash;
    float    flashCol[3], active;
    float    pal[3], palAmount;
    float    exposure;
    uint32_t prevPitch;
    float    prevW, prevH;
    uint32_t prevBgra, havePrev;
    float    reflect, react;
    // M2.5: LyricsForge glass block (BfDirectorV1 size >= 256)
    float    cover[3], coverK;    // song / chapter cover colour, eased
    float    sky[3], skyK;        // LyricsForge sky colour, eased
    float    wordCol[3], wordColK;// colour word flash
    float    awaken, drop, finale, wordT; // wordT: seconds since the last sung word started (big = none)
    float    idleEdge, lf, beatK, pad2;   // lf: 1 when the glass block is live (else the M2 behaviour)
    // M3: leather dye (the game's ChangeColor, read from its own pixel constant buffer b1) + enchant
    int32_t  dyeRow, dyeRowGlint;         // float4 row of ChangeColor in the Actor / ActorGlint PS buffer (-1 = unknown)
    uint32_t dbgRow;                      // debug view 8: which row to show
    float    enchFlow;                    // 1 = enchanted pieces get the edge flow
    float    fusedK, filmK, pad3, pad4;   // fused glass: the armour's real material inside the glass, thin-film shimmer
    float    frost, glowK, shimmer;       // frosted glass (GlassFrost.txt)
    uint32_t tintPk;                      // r | g << 8 | b << 16
};
static_assert(sizeof(GaParams) <= 256, "one 256-byte slot");

// LyricsForge Director (BF): music + light state, same source as the post pass
using DirFn = BfDirectorV1 const* (*)();
DirFn dirGet() {
    static DirFn fn    = nullptr;
    static int   delay = 0;
    if (fn || delay-- > 0) return fn;
    delay = 240;
    if (HMODULE m = GetModuleHandleW(L"BedrockForgeCore.dll"))
        fn = reinterpret_cast<DirFn>(reinterpret_cast<void*>(GetProcAddress(m, "BF_GetDirector")));
    return fn;
}
// last finished frame (lyrics + sky + post included), copied at present for the reflections
ComPtr<ID3D12Resource> gPrev;
UINT64                 gPrevSize = 0;
uint32_t               gPrevPitch = 0, gPrevW = 0, gPrevH = 0, gPrevBgra = 0;
std::atomic_bool       gPrevValid{false};
} // namespace

namespace {
// M2.5: the LyricsForge glass block -> eased shader params. Called once per flush (several per frame is fine:
// everything eases on wall time).
void glassBlock(BfDirectorV1 const* b, bool fresh, GaParams& p) {
    using clk = std::chrono::steady_clock;
    static clk::time_point last = clk::now(), wordAt = clk::now() - std::chrono::seconds(100);
    static float cover[3]{1, 1, 1}, coverK = 0, sky[3]{1, 1, 1}, skyK = 0, finale = 0, awaken = 0, ramp = 0, wordColK = 0, wordCol[3]{};
    static uint32_t seq = 0;
    static bool     haveSeq = false;
    auto const now = clk::now();
    float const dt = std::clamp(std::chrono::duration<float>(now - last).count(), 0.f, 0.25f);
    last = now;
    bool const live = b->size >= 256 && fresh;
    gGlassLf = live;
    if (b->size < 256) return; // older BF: keep the M2 behaviour
    p.lf = 1;
    if (!live) { // BF paused (chat, menus): hold the last look instead of fading out
        for (int k = 0; k < 3; ++k) p.cover[k] = cover[k], p.sky[k] = sky[k], p.wordCol[k] = wordCol[k];
        p.coverK = coverK, p.skyK = skyK, p.finale = finale, p.awaken = awaken, p.wordT = 99.f;
        if (awaken > 0.01f) p.active = 1;
        return;
    }
    auto ease = [&](float& v, float want, float sec) { v += (want - v) * (1.f - std::exp(-dt / sec)); };
    bool const playing = live && b->playing;
    float const cK = playing ? std::clamp(b->coverAmount, 0.f, 1.f) : 0.f;
    if (cK > 0.f) for (int k = 0; k < 3; ++k) ease(cover[k], std::clamp(b->cover[k], 0.f, 1.f), 0.45f); // chapter: ~1 s blend
    ease(coverK, cK, 0.6f);
    float const sK = live ? std::clamp(b->skyAmount, 0.f, 1.f) : 0.f;
    if (sK > 0.f) for (int k = 0; k < 3; ++k) ease(sky[k], std::clamp(b->sky[k], 0.f, 1.f), 0.5f);
    ease(skyK, sK, 0.6f);
    ease(finale, live && b->finale > 0.5f ? 1.f : 0.f, live && b->finale > 0.5f ? 0.6f : 1.2f);
    // Awaken drives the boot-up; it already falls back over the outro (the world exhales), so the pieces power
    // down in reverse. With the music stopped and Awaken off, it simply fades.
    // our own 5 s boot ramp from the song (BF's Awaken only runs with the LyricsForge sky on); the faster of the two
    // wins, and the song's outro powers it back down (helmet first, boots last)
    ramp   = b->playing ? std::min(1.f, ramp + dt / 5.f) : std::max(0.f, ramp - dt / 4.f);
    float const o = std::clamp((b->outro - 0.25f) / 0.75f, 0.f, 1.f);
    awaken = std::max(ramp, std::clamp(b->awaken, 0.f, 1.f)) * (1.f - o * o * (3.f - 2.f * o));
    if (live && b->playing) {
        if (!haveSeq) seq = b->wordSeq, haveSeq = true;
        if (b->wordSeq != seq) {
            seq    = b->wordSeq;
            wordAt = now;
            if (b->wordColorAmount > 0.5f) { for (int k = 0; k < 3; ++k) wordCol[k] = std::clamp(b->wordColor[k], 0.f, 1.f); wordColK = 1; }
        }
    }
    wordColK *= std::exp(-dt / 0.7f);
    for (int k = 0; k < 3; ++k) p.cover[k] = cover[k], p.sky[k] = sky[k], p.wordCol[k] = wordCol[k];
    p.coverK = coverK, p.skyK = skyK, p.wordColK = wordColK, p.finale = finale, p.awaken = awaken;
    p.drop   = live ? std::clamp(b->drop, 0.f, 1.4f) : 0.f;
    p.wordT  = gWordSweep ? std::chrono::duration<float>(now - wordAt).count() : 99.f;
    if (playing) p.beat = std::clamp(b->beat, 0.f, 1.f);
    if (playing || awaken > 0.01f) p.active = 1; // follow the song even with the Director look off (and while powering down)
    gShowAwaken = awaken, gShowCover = coverK;
}
} // namespace

bool beginFlush(ID3D12GraphicsCommandList* l, ID3D12Resource* rt, ID3D12Resource* ds, FlushRes& out) {
    auto fail = [&](char const* why) { gFlushWhy = why; ++gFlushFail; return false; };
    if (!rt) return fail("colour target unknown");
    if (!ds) return fail("depth target unknown");
    ComPtr<ID3D12Device> dev;
    if (FAILED(l->GetDevice(IID_PPV_ARGS(&dev)))) return fail("no device");
    auto rd = rt->GetDesc(), dd = ds->GetDesc();
    if (rd.SampleDesc.Count > 1 || dd.SampleDesc.Count > 1) return fail("MSAA target");
    if (rd.Format != DXGI_FORMAT_R8G8B8A8_UNORM && rd.Format != DXGI_FORMAT_B8G8R8A8_UNORM && rd.Format != DXGI_FORMAT_R8G8B8A8_TYPELESS &&
        rd.Format != DXGI_FORMAT_B8G8R8A8_TYPELESS && rd.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB && rd.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
        return fail("colour format not 8-bit RGBA yet (Vibrant Visuals is M3)");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT rf{}, df{};
    UINT64 rTot = 0, dTot = 0;
    dev->GetCopyableFootprints(&rd, 0, 1, 0, &rf, nullptr, nullptr, &rTot);
    dev->GetCopyableFootprints(&dd, 0, 1, 0, &df, nullptr, nullptr, &dTot);
    if (!rTot || !dTot) return fail("footprint");
    if (!gParams) {
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_UPLOAD};
        D3D12_RESOURCE_DESC   d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER, d.Width = 256 * kSlots, d.Height = 1, d.DepthOrArraySize = 1, d.MipLevels = 1;
        d.SampleDesc.Count = 1, d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gParams))) ||
            FAILED(gParams->Map(0, nullptr, reinterpret_cast<void**>(&gParamsMap))))
            return fail("params buffer");
        QueryPerformanceCounter(&gT0);
    }
    int   slot = int(gSlotNext++ % kSlots);
    auto& S    = gSlots[slot];
    if (!ensureBuffer(dev.Get(), S.scene, S.sceneSize, rTot) || !ensureBuffer(dev.Get(), S.depth, S.depthSize, dTot)) return fail("scene buffers");
    auto const rtState = knownState(rt, D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto const dsState = knownState(ds, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    barrier(l, rt, rtState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    barrier(l, ds, dsState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = S.scene.Get(), dst.PlacedFootprint = rf, src.pResource = rt, src.SubresourceIndex = 0;
    l->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    dst.pResource = S.depth.Get(), dst.PlacedFootprint = df, src.pResource = ds, src.SubresourceIndex = 0;
    l->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    barrier(l, rt, D3D12_RESOURCE_STATE_COPY_SOURCE, rtState);
    barrier(l, ds, D3D12_RESOURCE_STATE_COPY_SOURCE, dsState);
    barrier(l, S.scene.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    barrier(l, S.depth.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    LARGE_INTEGER now{}, f{};
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&f);
    GaParams p{};
    p.res[0] = float(rd.Width), p.res[1] = float(rd.Height);
    p.time       = float(double(now.QuadPart - gT0.QuadPart) / double(f.QuadPart));
    p.glint      = 0;
    p.dyeRow = gDyeRow, p.dyeRowGlint = gDyeRowGlint, p.dbgRow = uint32_t(std::max(gDbgRow, 0)), p.enchFlow = gEnchFlow ? 1.f : 0.f;
    p.fusedK = gFusedK.load(), p.filmK = gFilmK.load();
    {   // frosted glass: re-read the config about once a second (the client menu writes it)
        static int64_t lastPoll = 0;
        int64_t const nowMs = GetTickCount64();
        if (nowMs - lastPoll > 1000) { lastPoll = nowMs; loadFrost(); }
        p.frost = gFrost.load(), p.glowK = gGlowK.load(), p.shimmer = gShimmer ? 1.f : 0.f, p.tintPk = gTintPk.load();
    }
    if (float const nk = rf::sky::novakMatchK(); nk >= 0.0f) p.pad3 = nk, p.pad4 = 1.0f;   // Novak: the armor follows the match
    else if (float const ck = rf::sky::cloudSeaMatchK(); ck >= 0.0f) p.pad3 = ck, p.pad4 = 2.0f; // Cloud Sea: the same, in sunset gold
    p.scenePitch = rf.Footprint.RowPitch, p.depthPitch = df.Footprint.RowPitch;
    p.swirl = 0.9f, p.ripple = 0.09f, p.lens = 0.8f; // approved preview: Medium
    p.uiScale   = float(rd.Height) / 760.f;
    p.pad       = uint32_t(gDebugSolid.load());
    p.sceneBgra = rd.Format == DXGI_FORMAT_B8G8R8A8_UNORM || rd.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS || rd.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    // M2: LyricsForge state (neutral when no song / Director off / reacting switched off)
    p.exposure = 1;
    p.pal[0] = p.pal[1] = p.pal[2] = 1;
    p.react   = gReact ? 1.f : 0.f;
    p.reflect = gReflect;
    p.awaken = 1, p.wordT = 99.f, p.idleEdge = gIdleEdge, p.beatK = gBeatK;
    if (gReact) {
        static uint32_t lastFrame = 0;
        static int      still     = 0;
        DirFn fn = dirGet();
        BfDirectorV1 const* b = fn ? fn() : nullptr;
        if (b && b->version >= 1 && b->size >= 128) {
            if (b->frame != lastFrame) { lastFrame = b->frame; still = 0; } else if (still < 1000) ++still;
            if (b->active && still <= 20) {
                auto c01 = [](float v) { return std::clamp(v, 0.f, 1.f); };
                p.active = 1, p.beat = c01(b->beat), p.bass = c01(b->bass), p.impact = c01(b->impact), p.flash = std::clamp(b->flash, 0.f, 0.25f);
                for (int k = 0; k < 3; ++k) p.flashCol[k] = c01(b->flashColor[k]);
                if (b->size >= 160) {
                    float t[3]{std::clamp(b->worldTint[0], .2f, 2.f), std::clamp(b->worldTint[1], .2f, 2.f), std::clamp(b->worldTint[2], .2f, 2.f)};
                    float m = std::max({t[0], t[1], t[2], 1e-3f});
                    for (int k = 0; k < 3; ++k) p.pal[k] = t[k] / m; // the sky's light colour, brightest channel = 1
                    p.palAmount = c01(b->worldAmount);
                    p.exposure  = std::clamp(b->exposure, .4f, 1.8f);
                }
                gShowBeat = p.beat;
            }
            glassBlock(b, still <= 20, p);
        }
    }
    if (gPrevValid) {
        p.havePrev = 1, p.prevPitch = gPrevPitch, p.prevW = float(gPrevW), p.prevH = float(gPrevH), p.prevBgra = gPrevBgra;
        barrier(l, gPrev.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        out.prev = gPrev->GetGPUVirtualAddress();
    } else out.prev = S.scene->GetGPUVirtualAddress(); // bound but unused (havePrev = 0)
    std::memcpy(gParamsMap + slot * 256, &p, sizeof p);
    out.scene  = S.scene->GetGPUVirtualAddress();
    out.depth  = S.depth->GetGPUVirtualAddress();
    out.params = gParams->GetGPUVirtualAddress() + slot * 256;
    out.slot   = slot;
    ++gFlushOk;
    return true;
}

void endFlush(ID3D12GraphicsCommandList* l, FlushRes const& fr) {
    if (fr.slot < 0) return;
    auto& S = gSlots[fr.slot];
    barrier(l, S.scene.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    barrier(l, S.depth.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    if (fr.prev && gPrev && fr.prev == gPrev->GetGPUVirtualAddress())
        barrier(l, gPrev.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
}

void afterLyrics(ID3D12Device* dev, ID3D12GraphicsCommandList* l, ID3D12Resource* bb) {
    if (!enabled() || !dev || !l || !bb) return;
    auto d = bb->GetDesc();
    bool const bgra = d.Format == DXGI_FORMAT_B8G8R8A8_UNORM || d.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || d.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS;
    bool const rgba = d.Format == DXGI_FORMAT_R8G8B8A8_UNORM || d.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || d.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS;
    if ((!bgra && !rgba) || d.SampleDesc.Count > 1) { gPrevValid = false; return; }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT64 tot = 0;
    dev->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &tot);
    if (!tot || !ensureBuffer(dev, gPrev, gPrevSize, tot)) { gPrevValid = false; return; }
    barrier(l, bb, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, dst.pResource = gPrev.Get(), dst.PlacedFootprint = fp;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, src.pResource = bb, src.SubresourceIndex = 0;
    l->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    barrier(l, bb, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    gPrevPitch = fp.Footprint.RowPitch, gPrevW = UINT(d.Width), gPrevH = d.Height, gPrevBgra = bgra ? 1u : 0u;
    gPrevValid = true;
}

namespace {
/// Glass Body: our pipeline if this draw binds a player's skin (yours with "You" on, anyone else's with "Other players").
ID3D12PipelineState* bodyPsoFor(ID3D12GraphicsCommandList* list, ID3D12PipelineState* pso, std::shared_ptr<ActorDesc> const& ad,
                                ID3D12PipelineState** game, GlassDraw* out) {
    if (!out || ad->glint || !bodyShaderBuilt()) return nullptr;
    SrvInfo         si;
    ID3D12Resource* tex = boundSkinTexture(list, &si);
    if (!tex) return nullptr;
    int64_t const now = int64_t(GetTickCount64());
    int           slot;
    {
        std::lock_guard l(gMutex);
        auto it = gSkinKind.find(tex);
        if (it == gSkinKind.end() || it->second.w != si.w || it->second.h != si.h || it->second.fmt != si.fmt) {
            // first sight, or a different texture at the same address now: read it (kept alive by the queue)
            SkinKind k;
            k.w = si.w, k.h = si.h, k.fmt = si.fmt, k.at = now, k.drawn = now;
            gSkinKind[tex] = k;
            gSkinPending.emplace_back(tex);
            return nullptr;
        }
        auto& k = it->second;
        k.drawn = now;
        if (k.slot == -2) return nullptr;                                              // being read
        // look again: a player's skin every 4 s (a mob must never stay glass), anything else every 15 s (perf audit
        // 2026-11-17: re-reading every mob texture every 4 s was ~4 readbacks a second for nothing)
        if (now - k.at > (k.slot >= 0 ? 4000 : 15000) && !k.requeued) { k.requeued = true; gSkinPending.emplace_back(tex); }
        if (k.slot < 0 || k.slot > 15) return nullptr;                                  // a mob, an NPC we have no skin of
        if (!gSkinUpOk[k.slot] || gSkinUpGen[k.slot] != k.gen || !gSkinBuf) return nullptr;   // that slot holds someone else now
        if (k.self ? !gSelf.load() : !gOthers.load()) return nullptr;
        slot = k.slot;
        if (auto g = gGlass.find({pso, kBodyKind}); g != gGlass.end()) {
            if (!g->second) return nullptr;
            *game = pso;
            out->body = true, out->skin = gSkinBuf->GetGPUVirtualAddress() + UINT64(slot) * 16384u;
            ++gGlassDraws, ++gBodyDraws;
            return g->second.Get();
        }
    }
    ComPtr<ID3D12Device> dev;
    if (FAILED(list->GetDevice(IID_PPV_ARGS(&dev)))) return nullptr;
    ComPtr<ID3D12PipelineState> made = makeBodyPso(dev.Get(), *ad);
    std::lock_guard l(gMutex);
    auto& g = gGlass[{pso, kBodyKind}];
    if (!g) g = made;   // (a failure stays null: no retry every draw)
    if (!g || !gSkinBuf) return nullptr;
    *game = pso;
    out->body = true, out->skin = gSkinBuf->GetGPUVirtualAddress() + UINT64(slot) * 16384u;
    ++gGlassDraws, ++gBodyDraws;
    return g.Get();
}
/// Glass Items: our pipeline for an ItemInHand draw whose sprite we hold a copy of.
ID3D12PipelineState* itemPsoFor(ID3D12GraphicsCommandList* list, ID3D12PipelineState* pso, std::shared_ptr<ActorDesc> const& ad,
                                ID3D12PipelineState** game, GlassDraw* out) {
    // The glass item shader samples the atlas from the game's own table (t1), bound again by the replay - nothing to
    // copy, no texture pointer of ours (2026-11-18: the copy through a tracked pointer crashed the game after a teleport).
    // The body root signature's extra slot (t3 space9) gets a small buffer of ours so it is never left empty.
    if (!out || !itemShaderBuilt()) return nullptr;
    ComPtr<ID3D12Device> dev;
    if (FAILED(list->GetDevice(IID_PPV_ARGS(&dev)))) return nullptr;
    static ComPtr<ID3D12Resource> dummy;
    static std::once_flag         once;
    std::call_once(once, [&] {
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_UPLOAD};
        D3D12_RESOURCE_DESC   d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER, d.Width = 256, d.Height = 1, d.DepthOrArraySize = 1, d.MipLevels = 1;
        d.SampleDesc.Count = 1, d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&dummy))))
            dummy->SetName(L"RF Glass Items slot");
    });
    if (!dummy) return nullptr;
    {
        std::lock_guard l(gMutex);
        if (auto g = gGlass.find({pso, kBodyKind + 1}); g != gGlass.end()) {
            if (!g->second) return nullptr;
            *game = pso;
            out->body = true, out->skin = dummy->GetGPUVirtualAddress();
            ++gGlassDraws, ++gItemDraws;
            return g->second.Get();
        }
    }
    ComPtr<ID3D12PipelineState> made = makeItemPso(dev.Get(), *ad);
    std::lock_guard l(gMutex);
    auto& g = gGlass[{pso, kBodyKind + 1}];
    if (!g) g = made;
    if (!g) return nullptr;   // (makeItemPso counted it)
    *game = pso;
    out->body = true, out->skin = dummy->GetGPUVirtualAddress();
    ++gGlassDraws, ++gItemDraws;
    return g.Get();
}
} // namespace

ID3D12PipelineState* glassPsoFor(ID3D12GraphicsCommandList* list, ID3D12PipelineState** game, GlassDraw* out) {
    if (!shadersBuilt()) return nullptr;
    ID3D12PipelineState*       pso = nullptr;
    std::shared_ptr<ActorDesc> ad;
    bool                       isItem = false;
    {
        std::lock_guard l(gMutex);
        auto it = gListPso.find(list);
        if (it == gListPso.end()) return nullptr;
        pso    = it->second;
        auto a = gActor.find(pso);
        if (a == gActor.end()) {
            isItem = true;
            if (!itemsOn()) return nullptr;
            auto t = gItemPso.find(pso);
            if (t == gItemPso.end()) return nullptr;
            // only the game's item materials (ItemInHand*): other pipelines with the same vertex output - particles, it
            // seems - must never turn to glass (a glass quad round a particle reads as a box). Names not learned yet: wait.
            auto pi = gPsos.find(pso);
            if (!gNamesLearned) { ++gItemWhy[1]; return nullptr; }
            if (pi == gPsos.end() || nameOf(pi->second.ps).find("ItemInHand") == std::string::npos) { ++gItemWhy[0]; return nullptr; }
            ad = t->second;
        } else ad = a->second;
    }
    if (isItem) return itemPsoFor(list, pso, ad, game, out);   // (an item pipeline: only reached with Items on)
    // Glass Armor: unchanged (armour textures are 64x32 / 128x64 - never a skin)
    ID3D12Resource* tex = armorOn() ? boundArmorTexture(list) : nullptr;
    if (!tex) return bodyOn() ? bodyPsoFor(list, pso, ad, game, out) : nullptr;
    int kind;
    {
        std::lock_guard l(gMutex);
        auto it = gTexKind.find(tex);
        if (it == gTexKind.end()) { // first sight: queue a readback (kept alive by the queue)
            gTexKind[tex] = -2;
            gPending.emplace_back(tex);
            return nullptr;
        }
        kind = it->second;
        if (kind < 0) return nullptr;
        auto g = gGlass.find({pso, kind});
        if (g != gGlass.end()) {
            *game = pso;
            ++gGlassDraws;
            return g->second.Get();
        }
    }
    // build our pipeline outside the lock (CreateGraphicsPipelineState runs through our own create hook)
    ComPtr<ID3D12Device> dev;
    if (FAILED(list->GetDevice(IID_PPV_ARGS(&dev)))) return nullptr;
    ComPtr<ID3D12PipelineState> made = makeGlassPso(dev.Get(), *ad, kind);
    std::lock_guard l(gMutex);
    auto& slot = gGlass[{pso, kind}];
    if (!slot) slot = made;
    if (!slot) return nullptr;
    *game = pso;
    ++gGlassDraws;
    return slot.Get();
}

namespace {
// ---- Glass Body / Items probe, after both capture frames (onPresent)
char const* rangeName(D3D12_DESCRIPTOR_RANGE_TYPE t) {
    return t == D3D12_DESCRIPTOR_RANGE_TYPE_SRV ? "t" : t == D3D12_DESCRIPTOR_RANGE_TYPE_UAV ? "u" : t == D3D12_DESCRIPTOR_RANGE_TYPE_CBV ? "b" : "s";
}
char const* visName(D3D12_SHADER_VISIBILITY v) {
    return v == D3D12_SHADER_VISIBILITY_ALL ? "all" : v == D3D12_SHADER_VISIBILITY_VERTEX ? "vs" : v == D3D12_SHADER_VISIBILITY_PIXEL ? "ps" : "other";
}
/// "p0 table[t0..t3 sp0 | s0 sp0] ps; p1 CBV b1 sp0 ps; ..." - where the game's own texture + sampler sit
std::string rootLayout(void* rs) {
    std::vector<uint8_t> blob;
    {
        std::lock_guard l(gMutex);
        if (auto b = gRsBlob.find(static_cast<ID3D12RootSignature*>(rs)); b != gRsBlob.end()) blob = b->second;
        else
            for (auto const& [p, ad] : gActor)
                if (ad->root.Get() == rs && rts0(ad->vs, blob)) break;
    }
    if (blob.empty()) return "(blob not seen)";
    ComPtr<ID3D12VersionedRootSignatureDeserializer> des;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC const*       vd = nullptr;
    if (FAILED(rf::d3d12api::CreateVersionedRootSignatureDeserializer(blob.data(), blob.size(), IID_PPV_ARGS(&des))) ||
        FAILED(des->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_1, &vd)) || !vd)
        return "(unreadable)";
    auto const&        g = vd->Desc_1_1;
    std::ostringstream s;
    for (UINT i = 0; i < g.NumParameters; ++i) {
        auto const& p = g.pParameters[i];
        s << "\n      p" << i << " ";
        switch (p.ParameterType) {
        case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
            s << "table[";
            for (UINT r = 0; r < p.DescriptorTable.NumDescriptorRanges; ++r) {
                auto const& q = p.DescriptorTable.pDescriptorRanges[r];
                s << (r ? " | " : "") << rangeName(q.RangeType) << q.BaseShaderRegister;
                if (q.NumDescriptors != 1) s << ".." << (q.NumDescriptors == UINT(-1) ? std::string("unbounded") : std::to_string(q.BaseShaderRegister + q.NumDescriptors - 1));
                s << " sp" << q.RegisterSpace << " @" << int(q.OffsetInDescriptorsFromTableStart);
            }
            s << "]";
            break;
        case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS: s << "consts b" << p.Constants.ShaderRegister << " sp" << p.Constants.RegisterSpace << " x" << p.Constants.Num32BitValues; break;
        case D3D12_ROOT_PARAMETER_TYPE_CBV: s << "CBV b" << p.Descriptor.ShaderRegister << " sp" << p.Descriptor.RegisterSpace; break;
        case D3D12_ROOT_PARAMETER_TYPE_SRV: s << "SRV t" << p.Descriptor.ShaderRegister << " sp" << p.Descriptor.RegisterSpace; break;
        default: s << "UAV u" << p.Descriptor.ShaderRegister << " sp" << p.Descriptor.RegisterSpace; break;
        }
        s << " " << visName(p.ShaderVisibility);
    }
    for (UINT i = 0; i < g.NumStaticSamplers; ++i)
        s << "\n      static sampler s" << g.pStaticSamplers[i].ShaderRegister << " sp" << g.pStaticSamplers[i].RegisterSpace << " filter "
          << int(g.pStaticSamplers[i].Filter) << " " << visName(g.pStaticSamplers[i].ShaderVisibility);
    return s.str();
}
/// A read-back skin-sized texture against BF's grabbed skins: alpha texels that disagree + mean colour difference.
std::string skinMatch(uint8_t const* px, ProbeRb const& r, rf::sky::BfSkinView const* skins, int mine) {
    unsigned const sc = r.w / 64, rows = r.h / sc;   // 64x64, 64x32 (legacy), 128x128 (read every second texel)
    long painted = 0, sum[3]{};
    for (unsigned y = 0; y < rows; ++y)
        for (unsigned x = 0; x < 64; ++x) {
            uint8_t const* p = px + size_t(y * sc) * r.pitch + size_t(x * sc) * 4;
            if (!p[3]) continue;
            ++painted;
            for (int c = 0; c < 3; ++c) sum[c] += p[r.bgra ? 2 - c : c];
        }
    int  best = -1;
    long bestA = 1 << 30;
    double bestD = 1e9;
    for (int i = 0; i < 16; ++i) {
        if (!skins[i].px) continue;
        long a = 0, n = 0;
        double d = 0;
        for (unsigned y = 0; y < rows; ++y)
            for (unsigned x = 0; x < 64; ++x) {
                uint8_t const* p = px + size_t(y * sc) * r.pitch + size_t(x * sc) * 4;
                uint8_t const* q = skins[i].px + (y * 64 + x) * 4;
                if ((p[3] != 0) != (q[3] != 0)) { ++a; continue; }
                if (!p[3]) continue;
                for (int c = 0; c < 3; ++c) d += std::abs(int(p[r.bgra ? 2 - c : c]) - int(q[c]));
                ++n;
            }
        d = n ? d / (3.0 * double(n)) : 255.0;
        if (a < bestA || (a == bestA && d < bestD)) best = i, bestA = a, bestD = d;
    }
    char b[256];
    if (best < 0)
        std::snprintf(b, sizeof b, "%ld painted, mean %ld,%ld,%ld | no BF skins to compare", painted, painted ? sum[0] / painted : 0,
                      painted ? sum[1] / painted : 0, painted ? sum[2] / painted : 0);
    else {
        bool const match = bestA <= 24 && bestD <= 8.0;
        std::snprintf(b, sizeof b, "%ld painted, mean %ld,%ld,%ld | nearest BF slot %d%s: %ld alpha texels off, colour off %.1f -> %s", painted,
                      painted ? sum[0] / painted : 0, painted ? sum[1] / painted : 0, painted ? sum[2] / painted : 0, best,
                      best + 1 == mine ? " (YOU)" : "", bestA, bestD, match ? (best + 1 == mine ? "MATCH: your skin" : "MATCH: another player") : "no match");
    }
    return b;
}
void probeFinish() {   // the report, with whatever the readbacks gave (none = the skin lines say so)
    std::ostringstream s;
    s << "# Glass Body / Items probe\n# vertex output signatures (ACTOR = the armor's, GLINT = enchanted armor's)\n";
    s << "  ACTOR " << kActorVsOut << "\n  GLINT " << kGlintVsOut << "\n";
    std::map<void*, int> roots;
    {
        std::lock_guard l(gMutex);
        for (size_t i = 0; i < gSigs.size(); ++i) s << "  S" << i << " " << gSigs[i] << "\n";
        roots = gProbeRoots;
    }
    s << "# root signatures of actor-family draws (both frames)\n";
    for (auto const& [rs, n] : roots) s << "  root " << rs << " (" << n << " draws)" << rootLayout(rs) << "\n";
    rf::sky::BfSkinView skins[16];
    int const mine = rf::sky::bfSkins(skins);
    s << "# skin-sized textures (64x64 / 64x32 / 128x128) bound by actor-family draws, against Bedrock Forge's skins\n";
    if (mine < 0) s << "  Bedrock Forge skins: not available (BF missing or older than MatchFx v6)\n";
    else {
        int ok = 0;
        for (auto const& k : skins) ok += k.px != nullptr;
        s << "  Bedrock Forge skins: " << ok << " grabbed, yours " << (mine ? "slot " + std::to_string(mine - 1) : std::string("not yet")) << "\n";
    }
    for (auto& r : gProbeRb) {
        s << "  " << r.tex.Get() << " " << r.w << "x" << r.h << ": ";
        void*       m = nullptr;
        D3D12_RANGE rr{0, size_t(r.pitch) * r.h};
        if (r.buf && mine >= 0 && SUCCEEDED(r.buf->Map(0, &rr, &m)) && m) {
            s << skinMatch(static_cast<uint8_t const*>(m), r, skins, mine);
            D3D12_RANGE none{0, 0};
            r.buf->Unmap(0, &none);
        } else s << (r.buf ? "not compared (no BF skins)" : "not read back (format)");
        s << "\n";
    }
    gProbeRb.clear();
    {
        std::lock_guard l(gMutex);
        gProbeTex.clear();
        gProbeRoots.clear();
        gSigs.clear();
    }
    writeReport(s.str());
}
/// Both frames are in: copy the skin-sized textures to readback buffers (one submit), then probeFinish once done.
void probePump(ID3D12Device* dev, ID3D12CommandQueue* q) {
    if (gProbeStage == 2) {
        if (gProbeFence && gProbeFence->GetCompletedValue() < gProbeFenceVal) return;
        gProbeStage = 0;
        probeFinish();
        return;
    }
    gProbeStage = 0;
    std::vector<ComPtr<ID3D12Resource>> texs;
    {
        std::lock_guard l(gMutex);
        for (auto const& [p, t] : gProbeTex) texs.push_back(t);
    }
    if (texs.empty() || !dev || !q) { probeFinish(); return; }
    if ((!gProbeFence && FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gProbeFence)))) ||
        (!gProbeAlloc && FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&gProbeAlloc))))) {
        probeFinish();
        return;
    }
    if (!gProbeList) {
        if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, gProbeAlloc.Get(), nullptr, IID_PPV_ARGS(&gProbeList)))) { probeFinish(); return; }
    } else {
        gProbeAlloc->Reset();
        gProbeList->Reset(gProbeAlloc.Get(), nullptr);
    }
    for (auto& tex : texs) {
        auto const td = tex->GetDesc();
        ProbeRb    r;
        r.tex = tex, r.w = unsigned(td.Width), r.h = td.Height;
        r.bgra = td.Format == DXGI_FORMAT_B8G8R8A8_UNORM || td.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || td.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS;
        bool const rgba = td.Format == DXGI_FORMAT_R8G8B8A8_UNORM || td.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || td.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS;
        if (td.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || (!rgba && !r.bgra) || !skinSized(r.w, r.h) || td.SampleDesc.Count > 1) {
            gProbeRb.push_back(r);   // listed, not read
            continue;
        }
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT64                             tot = 0;
        dev->GetCopyableFootprints(&td, 0, 1, 0, &fp, nullptr, nullptr, &tot);
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_READBACK};
        D3D12_RESOURCE_DESC   bd{};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER, bd.Width = tot, bd.Height = 1, bd.DepthOrArraySize = 1, bd.MipLevels = 1;
        bd.SampleDesc.Count = 1, bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (!tot || FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&r.buf)))) {
            gProbeRb.push_back(r);
            continue;
        }
        r.pitch = fp.Footprint.RowPitch;
        auto const st = knownState(tex.Get(), D3D12_RESOURCE_STATE_COMMON);   // unknown: COMMON (promotes to COPY_SOURCE)
        bool const bar = st != D3D12_RESOURCE_STATE_COMMON && st != D3D12_RESOURCE_STATE_COPY_SOURCE;
        if (bar) barrier(gProbeList.Get(), tex.Get(), st, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
        dst.pResource = r.buf.Get(), dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, dst.PlacedFootprint = fp;
        src.pResource = tex.Get(), src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, src.SubresourceIndex = 0;
        gProbeList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        if (bar) barrier(gProbeList.Get(), tex.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, st);
        gProbeRb.push_back(r);
    }
    gProbeList->Close();
    ID3D12CommandList* lists[] = {gProbeList.Get()};
    q->ExecuteCommandLists(1, lists);
    q->Signal(gProbeFence.Get(), ++gProbeFenceVal);
    gProbeStage = 2;
}
} // namespace

namespace {
/// Glass Body, once per Present while You / Other players is on: our copy of BF's skins, then the skin readbacks.
void pumpSkins(ID3D12Device* dev, ID3D12CommandQueue* q) {
    rf::sky::BfSkinView v[16];
    int const           mine = rf::sky::bfSkins(v);
    gMine = mine;
    if (mine >= 0) {
        if (!gSkinBuf) {
            D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_UPLOAD};
            D3D12_RESOURCE_DESC   d{};
            d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER, d.Width = 16 * 16384, d.Height = 1, d.DepthOrArraySize = 1, d.MipLevels = 1;
            d.SampleDesc.Count = 1, d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            ComPtr<ID3D12Resource> b;
            uint8_t*               m = nullptr;
            if (SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&b))) &&
                SUCCEEDED(b->Map(0, nullptr, reinterpret_cast<void**>(&m))) && m) {
                b->SetName(L"RF Glass Body skins");
                std::lock_guard l(gMutex);
                gSkinBuf = b, gSkinMap = m;
            }
        }
        if (gSkinMap) {
            std::lock_guard l(gMutex);
            for (int i = 0; i < 16; ++i) {
                if (!v[i].px) { gSkinUpOk[i] = false; continue; }
                if (gSkinUpOk[i] && gSkinUpGen[i] == v[i].gen) continue;
                std::memcpy(gSkinMap + size_t(i) * 16384, v[i].px, 64 * 64 * 4);   // a new skin in this slot
                gSkinUpGen[i] = v[i].gen, gSkinUpOk[i] = true;
            }
        }
    }
    if (!gSkinFence && FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gSkinFence)))) return;
    if (!gSkinInFlight.empty()) {
        if (gSkinFence->GetCompletedValue() < gSkinFenceVal) return;
        int64_t const now = int64_t(GetTickCount64());
        for (auto& r : gSkinInFlight) {
            int         slot = -1;
            bool        self = false;
            void*       m    = nullptr;
            D3D12_RANGE rr{0, size_t(r.pitch) * r.h};
            char        info[160] = "not read";
            if (r.buf && SUCCEEDED(r.buf->Map(0, &rr, &m)) && m) {
                auto const* px = static_cast<uint8_t const*>(m);
                rf::glassbody::SkinScore best;
                // yours first: someone wearing the very same skin is glass with you (Fernando, 2026-11-17)
                if (mine > 0 && v[mine - 1].px) {
                    best = rf::glassbody::scoreSkin(px, r.pitch, r.w, r.h, r.bgra, v[mine - 1].px);
                    if (rf::glassbody::sameSkin(best)) slot = mine - 1, self = true;
                }
                for (int i = 0; i < 16 && slot < 0; ++i) {
                    if (!v[i].px || i == mine - 1) continue;
                    auto const s = rf::glassbody::scoreSkin(px, r.pitch, r.w, r.h, r.bgra, v[i].px);
                    if (rf::glassbody::sameSkin(s)) slot = i, best = s;
                    else if (s.alphaOff < best.alphaOff || best.painted == 0) best = s;
                }
                std::snprintf(info, sizeof info, "%ux%u: %s (%ld texels painted, %ld alpha off, colour off %.1f)", r.w, r.h,
                              slot < 0 ? "not a player's skin" : self ? "your skin" : "another player's skin", best.painted, best.alphaOff,
                              best.colourOff);
                D3D12_RANGE none{0, 0};
                r.buf->Unmap(0, &none);
            }
            std::lock_guard l(gMutex);
            auto& k = gSkinKind[r.tex.Get()];
            k.slot = slot, k.self = self, k.gen = slot >= 0 ? v[slot].gen : 0, k.at = now, k.requeued = false;
            (slot < 0 ? gSkinsNone : self ? gSkinsSelf : gSkinsOther)++;
            gSkinLast = info;
        }
        gSkinInFlight.clear();
        // forget textures not drawn for 30 s (the game has long freed most of them)
        std::lock_guard l(gMutex);
        for (auto it = gSkinKind.begin(); it != gSkinKind.end();)
            it = now - it->second.drawn > 30000 && it->second.slot != -2 ? gSkinKind.erase(it) : std::next(it);
    }
    std::vector<ComPtr<ID3D12Resource>> todo;
    {
        std::lock_guard l(gMutex);
        while (!gSkinPending.empty() && todo.size() < 8) {
            todo.push_back(gSkinPending.back());
            gSkinPending.pop_back();
        }
    }
    if (todo.empty()) return;
    if (!gSkinAlloc && FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&gSkinAlloc)))) return;
    if (!gSkinList) {
        if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, gSkinAlloc.Get(), nullptr, IID_PPV_ARGS(&gSkinList)))) return;
    } else {
        gSkinAlloc->Reset();
        gSkinList->Reset(gSkinAlloc.Get(), nullptr);
    }
    for (auto& tex : todo) {
        auto const td = tex->GetDesc();
        SkinRb     r;
        r.tex = tex, r.w = unsigned(td.Width), r.h = td.Height;
        r.bgra = td.Format == DXGI_FORMAT_B8G8R8A8_UNORM || td.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || td.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS;
        bool const rgba = td.Format == DXGI_FORMAT_R8G8B8A8_UNORM || td.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || td.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT64                             tot = 0;
        if (td.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && (rgba || r.bgra) && td.SampleDesc.Count == 1 && r.w == r.h && (r.w == 64 || r.w == 128))
            dev->GetCopyableFootprints(&td, 0, 1, 0, &fp, nullptr, nullptr, &tot);
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_READBACK};
        D3D12_RESOURCE_DESC   bd{};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER, bd.Width = tot, bd.Height = 1, bd.DepthOrArraySize = 1, bd.MipLevels = 1;
        bd.SampleDesc.Count = 1, bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (!tot || FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&r.buf)))) {
            r.buf.Reset();
            gSkinInFlight.push_back(r);   // classified "not a player" (looked at again later)
            continue;
        }
        r.pitch = fp.Footprint.RowPitch;
        auto const st  = knownState(tex.Get(), D3D12_RESOURCE_STATE_COMMON);
        bool const bar = st != D3D12_RESOURCE_STATE_COMMON && st != D3D12_RESOURCE_STATE_COPY_SOURCE;
        if (bar) barrier(gSkinList.Get(), tex.Get(), st, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
        dst.pResource = r.buf.Get(), dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, dst.PlacedFootprint = fp;
        src.pResource = tex.Get(), src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, src.SubresourceIndex = 0;
        gSkinList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        if (bar) barrier(gSkinList.Get(), tex.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, st);
        gSkinInFlight.push_back(r);
    }
    gSkinList->Close();
    ID3D12CommandList* lists[] = {gSkinList.Get()};
    q->ExecuteCommandLists(1, lists);
    q->Signal(gSkinFence.Get(), ++gSkinFenceVal);
}

/// Glass Items, once per Present while Items is on: copy newly seen atlases (GPU to GPU, mip 0) into our buffers.
void pumpItems(ID3D12Device* dev, ID3D12CommandQueue* q) {
    if (!gItemFence && FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gItemFence)))) return;
    if (!gAtlasRetired.empty()) {   // the fence advances once per copy; "+4" keeps it a few copies past its last use
        UINT64 const done = gItemFence->GetCompletedValue();
        std::erase_if(gAtlasRetired, [&](auto const& r) { return r.second <= done; });
    }
    if (!gAtlasInFlight.empty()) {
        if (gItemFence->GetCompletedValue() < gItemFenceVal) return;
        std::lock_guard l(gMutex);
        for (int k : gAtlasInFlight) { gAtlas[k].pending = false; if (!gAtlas[k].failed) gAtlas[k].ready = true, ++gItemsKnown; }
        gAtlasInFlight.clear();
    }
    std::vector<int> todo;
    {
        std::lock_guard l(gMutex);
        for (int k = 0; k < kAtlasSlots; ++k)
            if (gAtlas[k].pending && !gAtlas[k].ready) todo.push_back(k);
    }
    if (todo.empty()) return;
    if (!gAtlasHdr) {
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_UPLOAD};
        D3D12_RESOURCE_DESC   d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER, d.Width = kAtlasSlots * 512, d.Height = 1, d.DepthOrArraySize = 1, d.MipLevels = 1;
        d.SampleDesc.Count = 1, d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gAtlasHdr))) ||
            FAILED(gAtlasHdr->Map(0, nullptr, reinterpret_cast<void**>(&gAtlasHdrMap))))
            return;
    }
    if (!gItemAlloc && FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&gItemAlloc)))) return;
    if (!gItemList) {
        if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, gItemAlloc.Get(), nullptr, IID_PPV_ARGS(&gItemList)))) return;
    } else {
        gItemAlloc->Reset();
        gItemList->Reset(gItemAlloc.Get(), nullptr);
    }
    for (int k : todo) {
        auto& A = gAtlas[k];
        auto const td = A.keep->GetDesc();
        bool const bgra = td.Format == DXGI_FORMAT_B8G8R8A8_UNORM || td.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || td.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS;
        bool const rgba = td.Format == DXGI_FORMAT_R8G8B8A8_UNORM || td.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || td.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT64                             tot = 0;
        if (td.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && (rgba || bgra) && td.SampleDesc.Count == 1)
            dev->GetCopyableFootprints(&td, 0, 1, 512, &fp, nullptr, nullptr, &tot);   // rows start at byte 512
        if (!tot || tot > kAtlasMaxBytes + 512) { std::lock_guard l(gMutex); A.failed = true; gAtlasInFlight.push_back(k); continue; }
        if (!A.buf || A.bufBytes < tot) {
            D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
            D3D12_RESOURCE_DESC   d{};
            d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER, d.Width = tot, d.Height = 1, d.DepthOrArraySize = 1, d.MipLevels = 1;
            d.SampleDesc.Count = 1, d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            ComPtr<ID3D12Resource> b;
            if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&b)))) {
                std::lock_guard l(gMutex); A.failed = true; gAtlasInFlight.push_back(k); continue;
            }
            b->SetName(L"RF Glass Items atlas copy");
            std::lock_guard l(gMutex);
            if (A.buf) gAtlasRetired.emplace_back(std::move(A.buf), gItemFenceVal + 4);   // frames may still read it
            A.buf = b, A.bufBytes = tot, A.bufInSrv = false;
        }
        if (A.bufInSrv) barrier(gItemList.Get(), A.buf.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        uint32_t const hdr[4]{uint32_t(td.Width), td.Height, fp.Footprint.RowPitch, bgra ? 1u : 0u};
        std::memcpy(gAtlasHdrMap + size_t(k) * 512, hdr, sizeof hdr);
        gItemList->CopyBufferRegion(A.buf.Get(), 0, gAtlasHdr.Get(), UINT64(k) * 512, 512);
        auto const st  = knownState(A.keep.Get(), D3D12_RESOURCE_STATE_COMMON);
        bool const bar = st != D3D12_RESOURCE_STATE_COMMON && st != D3D12_RESOURCE_STATE_COPY_SOURCE;
        if (bar) barrier(gItemList.Get(), A.keep.Get(), st, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
        dst.pResource = A.buf.Get(), dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, dst.PlacedFootprint = fp;
        src.pResource = A.keep.Get(), src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, src.SubresourceIndex = 0;
        gItemList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        if (bar) barrier(gItemList.Get(), A.keep.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, st);
        barrier(gItemList.Get(), A.buf.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        A.bufInSrv = true;
        gAtlasInFlight.push_back(k);
    }
    gItemList->Close();
    ID3D12CommandList* lists[] = {gItemList.Get()};
    q->ExecuteCommandLists(1, lists);
    q->Signal(gItemFence.Get(), ++gItemFenceVal);
}

/// The glass cost, once per Present: GPU pairs from 4+ frames ago, a log line every 5 s.
void pumpTiming(ID3D12Device* dev, ID3D12CommandQueue* q) {
    static int64_t lastLog = 0;
    static long    frames  = 0;
    ++frames;
    std::lock_guard l(gTimeMx);
    ++gFrameNo;
    if (gMeasureGpu && !gTsHeap && dev && q) {
        D3D12_QUERY_HEAP_DESC hd{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, kTsPairs * 2, 0};
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_READBACK};
        D3D12_RESOURCE_DESC   bd{};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER, bd.Width = kTsPairs * 16, bd.Height = 1, bd.DepthOrArraySize = 1, bd.MipLevels = 1;
        bd.SampleDesc.Count = 1, bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        void* m = nullptr;
        if (SUCCEEDED(dev->CreateQueryHeap(&hd, IID_PPV_ARGS(&gTsHeap))) &&
            SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&gTsBuf))) &&
            SUCCEEDED(gTsBuf->Map(0, nullptr, &m)) && m && SUCCEEDED(q->GetTimestampFrequency(&gTsFreq)) && gTsFreq)
            gTsMap = static_cast<uint64_t const*>(m);
        else gTsHeap.Reset(), gTsBuf.Reset(), gMeasureGpu = false;
    }
    if (gTsMap)
        for (int i = 0; i < kTsPairs; ++i)
            if (gTsUsed[i] && gFrameNo - gTsFrame[i] >= 4) {
                uint64_t const a = gTsMap[i * 2], b = gTsMap[i * 2 + 1];
                if (b > a && b - a < gTsFreq) gGpuMs += double(b - a) * 1000.0 / double(gTsFreq), ++gGpuPairs;
                gTsUsed[i] = false;
            }
    int64_t const now = int64_t(GetTickCount64());
    if (now - lastLog < 5000) return;
    if (gFlushes && frames) {
        gShowCpuMs   = float(gCpuMs / double(frames));
        gShowGpuMs   = gGpuPairs ? float(gGpuMs / double(frames)) : -1.f;
        gShowFlushes = float(double(gFlushes) / double(frames));
        std::error_code ec;
        fs::create_directories(gOutDir, ec);
        char line[384];
        std::snprintf(line, sizeof line, "%lld glass cpu %.3f ms/frame | gpu %s ms/frame | replays %.2f/frame | armor %s body %s (you %d, others %d) items %d"
                      " (item pipelines %zu, glassed %ld, pso fails %ld, skipped: other material %ld, names pending %ld)\n",
                      static_cast<long long>(now / 1000), gShowCpuMs, gShowGpuMs < 0 ? "-" : std::to_string(gShowGpuMs).substr(0, 6).c_str(),
                      gShowFlushes, armorOn() ? "on" : "off", bodyOn() ? "on" : "off", gSelf ? 1 : 0, gOthers ? 1 : 0, gItems ? 1 : 0,
                      gItemPso.size(), gItemDrawsShown.load(), gItemPsoFail.load(), gItemWhy[0].exchange(0), gItemWhy[1].exchange(0));
        std::ofstream(gOutDir / "glass_ms.txt", std::ios::app) << line;
    }
    gCpuMs = gGpuMs = 0, gFlushes = gGpuPairs = 0, frames = 0, lastLog = now;
}
} // namespace

void flushTimer(ID3D12GraphicsCommandList* list, bool begin) {
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    std::lock_guard l(gTimeMx);
    if (begin) {
        TimeOpen o;
        o.qpc = t.QuadPart;
        if (gMeasureGpu && gTsMap) {
            o.ts = gTsNext++ % kTsPairs;
            if (gTsUsed[o.ts]) o.ts = -1;   // ring full (a very long frame): skip this one
            else list->EndQuery(gTsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, UINT(o.ts * 2));
        }
        gTimeOpen[list] = o;
        return;
    }
    auto it = gTimeOpen.find(list);
    if (it == gTimeOpen.end()) return;
    static LARGE_INTEGER f{};
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    gCpuMs += double(t.QuadPart - it->second.qpc) * 1000.0 / double(f.QuadPart);
    ++gFlushes;
    if (int const ts = it->second.ts; ts >= 0 && gTsHeap) {
        list->EndQuery(gTsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, UINT(ts * 2 + 1));
        list->ResolveQueryData(gTsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, UINT(ts * 2), 2, gTsBuf.Get(), UINT64(ts) * 16);
        gTsFrame[ts] = gFrameNo, gTsUsed[ts] = true;
    }
    gTimeOpen.erase(it);
}

void onPresent(ID3D12Device* device, ID3D12CommandQueue* queue) {
    rf::dred::check();
    {   // the client's Glass module (You / Other players / Armour): about once a second
        static int64_t lastPoll = 0;
        int64_t const  nowMs    = int64_t(GetTickCount64());
        if (nowMs - lastPoll > 1000) { lastPoll = nowMs; loadBody(); }
    }
    if (bodyOn() && device && queue) {
        pumpSkins(device, queue);
        gBodyDrawsShown = gBodyDraws.exchange(0);
    }
    if (itemsOn() && !gNamesLearned && !gLearning) learnNames();   // the item filter goes by material name
    if (itemsOn() && device && queue) {
        pumpItems(device, queue);
        gItemDrawsShown = gItemDraws.exchange(0);
    }
    if (enabled()) pumpTiming(device, queue);
    if (enabled() && device && queue) {
        pumpReadbacks(device, queue);
        gGlassDrawsShown = gGlassDraws.exchange(0);
    }
    int c = gCap.load();
    if (c == 1) { // start recording at a frame boundary
        std::lock_guard l(gMutex);
        gFrame[gWhich].clear();
        gByPs[gWhich].clear();
        gDetail[gWhich].clear();
        gListPso.clear();
        gCap = 2;
    } else if (c == 2) {
        gCap       = 0;
        gHave[gWhich] = true;
        gStatus    = gWhich == 0 ? "ON frame captured - now take the armor off and capture OFF" : "OFF frame captured";
        if (gHave[0] && gHave[1]) { gProbeStage = 1; gStatus = "checking skin textures..."; }   // the report follows the readbacks
    }
    if (gProbeStage != 0) probePump(device, queue);
}

void drawUi() {
    if (!ImGui::CollapsingHeader("Glass Armor")) return;
    ImGui::Indent();
    bool on = gEnabled.load();
    if (ImGui::Checkbox("Glass armor on", &on)) {
        gEnabled = on;
        std::error_code ec;
        fs::create_directories(gConfigFile.parent_path(), ec);
        std::ofstream(gConfigFile) << (on ? "1" : "0");
    }
    if (!shadersBuilt()) ImGui::TextDisabled("glass shaders missing from this build");
    {
        std::lock_guard l(gMutex);
        ImGui::Text("Armor textures recognised: %ld (last: %s)  other: %ld  queued: %zu",
                    gArmorTex.load(), gLastClass.empty() ? "-" : gLastClass.c_str(), gNotArmor.load(), gPending.size() + gInFlight.size());
        ImGui::TextWrapped("Last texture: %s", gLastClassInfo.c_str());
        ImGui::Text("Flushes ok: %ld  failed: %ld %s", gFlushOk.load(), gFlushFail.load(), gFlushWhy.c_str());
        bool react = gReact.load();
        if (ImGui::Checkbox("React to LyricsForge (beat, sky colour, flashes)", &react)) gReact = react;
        ImGui::SliderFloat("Reflection of lyrics / effects", &gReflect, 0.f, 1.f, "%.2f");
        ImGui::Text("Beat now: %.2f   last frame for reflections: %s", gShowBeat, gPrevValid ? "yes" : "no");
        ImGui::Text("LyricsForge glass link: %s   Awaken %.2f   cover colour %.2f", gGlassLf ? "live" : "waiting for a song", gShowAwaken, gShowCover);
        ImGui::SliderFloat("Outline with no music", &gIdleEdge, 0.f, 1.f, "%.2f");
        ImGui::SliderFloat("Beat pulse", &gBeatK, 0.f, 2.f, "%.2f");
        bool sw = gWordSweep.load();
        if (ImGui::Checkbox("Light sweep on each sung word", &sw)) gWordSweep = sw;
        int dbg = gDebugSolid.load();
        if (ImGui::Combo("Debug view", &dbg, "off\0solid magenta\0COLOR1\0COLOR0\0COLOR2\0COLOR3\0alphas (c0, c2, c3)\0TEXCOORD3\0game PS buffer row\0")) gDebugSolid = dbg;
        bool dump = gDumpReadbacks.load();
        if (ImGui::Checkbox("Debug: save newly seen 64x32 textures to logs (single-player only)", &dump)) gDumpReadbacks = dump;
        ImGui::InputInt("Buffer row (debug view 8)", &gDbgRow);
        ImGui::InputInt("Leather dye row (Actor)", &gDyeRow);
        ImGui::InputInt("Leather dye row (enchanted)", &gDyeRowGlint);
        bool ef = gEnchFlow.load();
        if (ImGui::Checkbox("Enchanted pieces: edge flow", &ef)) gEnchFlow = ef;
        float fk = gFusedK.load(), fl = gFilmK.load();
        if (ImGui::SliderFloat("Fused material", &fk, 0.0f, 1.0f, "%.2f")) gFusedK = fk;
        if (ImGui::SliderFloat("Thin-film shimmer", &fl, 0.0f, 2.0f, "%.2f")) gFilmK = fl;
        ImGui::SeparatorText("Frosted glass");
        float fr = gFrost.load(), gw = gGlowK.load();
        bool  sh = gShimmer.load(), dirty = false;
        uint32_t const pk = gTintPk.load();
        float col[3]{float(pk & 255u) / 255.f, float((pk >> 8) & 255u) / 255.f, float((pk >> 16) & 255u) / 255.f};
        if (ImGui::SliderFloat("Frosted (0 = clear crystal)", &fr, 0.0f, 1.0f, "%.2f")) { gFrost = fr; dirty = true; }
        if (ImGui::SliderFloat("Glow", &gw, 0.0f, 1.0f, "%.2f")) { gGlowK = gw; dirty = true; }
        if (ImGui::Checkbox("Shimmer (a soft sheen glides over now and then)", &sh)) { gShimmer = sh; dirty = true; }
        if (ImGui::ColorEdit3("Frost colour", col)) {
            gTintPk = uint32_t(col[0] * 255.f + .5f) | (uint32_t(col[1] * 255.f + .5f) << 8) | (uint32_t(col[2] * 255.f + .5f) << 16);
            dirty = true;
        }
        if (ImGui::Button("Purxiy-style preset")) { gFrost = 0.35f; gGlowK = 0.2f; gShimmer = true; gTintPk = 0x00FFFFFFu; dirty = true; }
        ImGui::SameLine();
        if (ImGui::Button("Approved crystal")) { gFrost = 0.f; gGlowK = 0.f; gShimmer = false; gTintPk = 0x00FFFFFFu; dirty = true; }
        if (dirty) saveFrost();
        ImGui::Text("Glass draws last frame: %ld   actor pipelines: %zu   glass pipelines: %zu   failed: %ld",
                    gGlassDrawsShown.load(), gActor.size(), gGlass.size(), gPsoFail.load());
        ImGui::SeparatorText("Glass Body (client: Cosmetics > Glass > You / Other players / Armour)");
        ImGui::Text("You: %s   Other players: %s   Items: %s   Armour: %s%s", gSelf ? "on" : "off", gOthers ? "on" : "off", gItems ? "on" : "off",
                    gArmour ? "on" : "off", gArmour ? "" : " (armour keeps its own look)");
        if (!bodyShaderBuilt()) ImGui::TextDisabled("glass body shader missing from this build");
        int const mine = gMine.load();
        if (!bodyOn()) ImGui::TextDisabled("Bedrock Forge skins: looked at once You or Other players is on");
        else ImGui::Text("Bedrock Forge skins: %s", mine < 0 ? "not available (BF missing / too old)" : mine == 0 ? "yours not grabbed yet" : "yours grabbed");
        {   // what is recognised right now (the totals below count every check)
            int self = 0, other = 0, none = 0;
            for (auto const& [t, k] : gSkinKind) (k.slot < 0 ? none : k.self ? self : other) += k.slot == -2 ? 0 : 1;
            ImGui::Text("Skin textures now: yours %d   other players %d   not players %d   being read %zu   (checks: %ld / %ld / %ld)", self, other,
                        none, gSkinPending.size() + gSkinInFlight.size(), gSkinsSelf.load(), gSkinsOther.load(), gSkinsNone.load());
        }
        ImGui::TextWrapped("Last: %s", gSkinLast.empty() ? "-" : gSkinLast.c_str());
        ImGui::Text("Body glass draws last frame: %ld   pipeline failures: %ld", gBodyDrawsShown.load(), gBodyPsoFail.load());
        ImGui::Text("Items: atlases copied %ld   item pipelines %zu   glass draws last frame %ld   failures %ld", gItemsKnown.load(), gItemPso.size(),
                    gItemDrawsShown.load(), gItemPsoFail.load());
        // (this whole section already runs under gMutex - a second lock here froze the game: 2026-11-18)
        ImGui::TextDisabled("Item draws not glassed: other material %ld, names pending %ld", gItemWhy[0].load(), gItemWhy[1].load());
        if (!itemShaderBuilt()) ImGui::TextDisabled("glass item shader missing from this build");
        bool mg = gMeasureGpu.load();
        if (ImGui::Checkbox("Measure glass cost on the GPU (logs/glassarmor/glass_ms.txt)", &mg)) gMeasureGpu = mg;
        if (gShowGpuMs < 0) ImGui::Text("Glass cost: CPU %.3f ms/frame, %.2f replays/frame", gShowCpuMs, gShowFlushes);
        else ImGui::Text("Glass cost: CPU %.3f ms/frame   GPU %.3f ms/frame   %.2f replays/frame", gShowCpuMs, gShowGpuMs, gShowFlushes);
    }
    ImGui::Separator();
    ImGui::TextDisabled("M0 probe");
    if (!gNamesLearned && !gLearning) learnNames();
    ImGui::TextWrapped("Find the pipelines that draw armor: stand in third person (F5) with armor on, press "
                       "\"Capture ON\"; take the armor off, press \"Capture OFF\". The report lists the difference.");
    {
        std::lock_guard l(gMutex);
        ImGui::Text("Pipelines noted: %zu   shader names: %s", gPsos.size(), gNamesLearned ? "ready" : "learning...");
    }
    bool busy = gCap.load() != 0;
    if (busy) ImGui::BeginDisabled();
    if (ImGui::Button("Capture ON (wearing armor)")) { gWhich = 0; gCap = 1; gStatus = "recording ON..."; }
    ImGui::SameLine();
    if (ImGui::Button("Capture OFF (no armor)")) { gWhich = 1; gCap = 1; gStatus = "recording OFF..."; }
    if (busy) ImGui::EndDisabled();
    ImGui::Text("Status: %s", gStatus.c_str());
    if (!gLastReport.empty()) ImGui::TextWrapped("Report: %s", gLastReport.c_str());
    ImGui::Unindent();
}

// ---------------------------------------------------------------- World FX probe views (read-only)
bool psoView(ID3D12PipelineState* pso, PsoView& out) {
    std::lock_guard l(gMutex);
    auto it = gPsos.find(pso);
    if (it == gPsos.end()) return false;
    out.ps = it->second.ps, out.vs = it->second.vs;
    out.vsOut = it->second.vsOut, out.inputs = it->second.inputs, out.targets = it->second.targets;
    out.root = it->second.root;
    return true;
}
std::string shaderName(uint64_t hash) {
    std::lock_guard l(gMutex);
    return nameOf(hash); // "?" for the game's own shaders until learnShaderNames() has finished
}
void learnServedMaterial(std::filesystem::path const& file, std::wstring_view fileName, char const* source) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
    auto const           shaders = rf::skyscan::scan(bytes.data(), bytes.size());
    std::string          name;
    for (wchar_t c : fileName) name += c < 128 ? char(c) : '?';
    std::lock_guard l(gMutex);
    for (auto const& s : shaders)
        gServedNames[s.hash] = name + (s.pass.empty() ? "" : " " + s.pass) + " " + s.stage + " " + s.platform + " [" + (source ? source : "pack") + "]";
}
void learnShaderNames() {
    if (!gNamesLearned && !gLearning) learnNames();
}
bool shaderNamesReady() { return gNamesLearned.load(); }
bool srvView(size_t cpu, SrvView& out) {
    std::lock_guard l(gMutex);
    auto it = gSrvs.find(cpu);
    if (it == gSrvs.end() || !it->second.res) return false;
    out.res = it->second.res, out.w = it->second.w, out.h = it->second.h, out.fmt = it->second.fmt, out.mips = it->second.mips;
    return true;
}
std::vector<SrvView> largestTextures(size_t n) {
    std::unordered_map<ID3D12Resource*, SrvView> by;
    {
        std::lock_guard l(gMutex);
        for (auto const& [cpu, i] : gSrvs)
            if (i.res && i.w >= 16 && i.h >= 16) by[i.res] = SrvView{i.res, i.w, i.h, i.fmt, i.mips};
    }
    std::vector<SrvView> v;
    v.reserve(by.size());
    for (auto const& [r, s] : by) v.push_back(s);
    std::sort(v.begin(), v.end(), [](SrvView const& a, SrvView const& b) { return uint64_t(a.w) * a.h > uint64_t(b.w) * b.h; });
    if (v.size() > n) v.resize(n);
    return v;
}
unsigned stateOf(ID3D12Resource* r, unsigned fallback) { return unsigned(knownState(r, D3D12_RESOURCE_STATES(fallback))); }

} // namespace rf::glassarmor
