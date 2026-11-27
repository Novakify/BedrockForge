// Renderer Foundation - Vibrant Visuals Live (see VvLive.h). Original code for Renderer Foundation (by Novak).
#define WIN32_LEAN_AND_MEAN
#include "VvLive.h"
#include "Lab.h"
#include "LLShim.h"
#include "LyricsCamera.h"
#include "SkyMaterialScan.h"
#include "VvLiveTable.h"

namespace rf::sky { std::string worldFxVvStatus(); }

#include <windows.h>
#include <d3d12.h>
#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace rf::vvlive {
namespace {
namespace fs = std::filesystem;

// ------------------------------------------------------------------ what we patch
// VvLiveTable.h (generated from the game's .material.bin files, tools/gen_vvlive_table.py): one row per shader of VV's
// lighting materials (DeferredShading, the mixed-resolution family the game actually runs, DeferredWater), the sky
// probe and the fog volume, with the byte offset of every uniform RF touches. A pipeline whose shader is not in the
// table is left alone - after a game update the table has to be regenerated, nothing is guessed.
using table::Row;
namespace R = table;
constexpr int kRowCount = int(sizeof(table::kRows) / sizeof(table::kRows[0]));
enum Group : int { GLight = 0, GSky = 1, GFog = 2, GroupCount = 3 };
char const* const kGroupName[GroupCount] = {"direct / ambient light", "sky colours (lighting, sky probe, water)", "fog volume"};
int groupOf(Row const& r) { return r.compute ? GFog : (r.off[R::Zen] >= 0 || r.off[R::Hor] >= 0 ? GSky : GLight); }

// ------------------------------------------------------------------ settings (config/VvLive.txt)
struct Settings {
    bool  enabled  = true;
    float sky      = 0.85f;   // how far VV's own sky colours (reflections, the light from the sky, distance haze) turn to RF's
    float light    = 0.60f;   // the sun / moon light's colour
    float air      = 0.80f;   // the fog's colour
    bool  deck     = true;    // the cloud deck as real VV volumetric fog
    float deckExt  = 0.050f;  // its density (extinction per block) at full
    float deckThk  = 30.0f;   // blocks over which it thins out to nothing at its top
    float rise     = 22.0f;   // the final fight: how far the clouds rise
    bool  pulse    = true;    // a bed breaks: the air takes its team's colour for a moment
    float skyGain  = 1.0f;    // the Cloud Sea sky's brightness in VV's HDR target
} gSet;
std::mutex gSetMx;
fs::path   gConfig;

void saveConfig() {
    if (gConfig.empty()) return;
    Settings s;
    { std::lock_guard l(gSetMx); s = gSet; }
    std::ofstream f(gConfig);
    f << "# Renderer Foundation - Vibrant Visuals Live (F6 > Vibrant Visuals Live writes this file)\n"
      << "enabled " << (s.enabled ? 1 : 0) << "\nsky " << s.sky << "\nlight " << s.light << "\nair " << s.air << "\ndeck " << (s.deck ? 1 : 0)
      << "\ndeck_density " << s.deckExt << "\ndeck_thickness " << s.deckThk << "\nfinal_rise " << s.rise << "\npulse " << (s.pulse ? 1 : 0)
      << "\nsky_gain " << s.skyGain << "\n";
}

// ------------------------------------------------------------------ tagged pipelines
constexpr int       kSlots = 128;
std::atomic<void*>  gSlotPso[kSlots]{};
std::atomic<int>    gSlotRow[kSlots]{};
std::atomic<int>    gUsed{0};
std::atomic<int>    gTagged[GroupCount]{};

int rowOfHash(uint64_t h) {
    int lo = 0, hi = kRowCount - 1;
    while (lo <= hi) {
        int const mid = (lo + hi) / 2;
        if (table::kRows[mid].hash == h) return mid;
        if (table::kRows[mid].hash < h) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}
int rowOfPso(void* pso) {
    int const n = gUsed.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
        if (gSlotPso[i].load(std::memory_order_relaxed) == pso) return gSlotRow[i].load(std::memory_order_relaxed);
    return -1;
}
void tag(void* pso, uint64_t h) {
    if (!pso) return;
    int const row = rowOfHash(h);
    int const n = gUsed.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i)   // a new pipeline at a dead one's address: its old tag is gone
        if (gSlotPso[i].load(std::memory_order_relaxed) == pso) { gSlotRow[i].store(row, std::memory_order_relaxed); if (row >= 0) ++gTagged[groupOf(table::kRows[row])]; return; }
    if (row < 0) return;
    static std::mutex m;
    std::lock_guard l(m);
    int const i = gUsed.load();
    if (i >= kSlots) return;
    gSlotRow[i].store(row);
    gSlotPso[i].store(pso);
    gUsed.store(i + 1, std::memory_order_release);
    int const g = groupOf(table::kRows[row]);
    if (++gTagged[g] <= 3) ll_shim::info(std::string("[RF] VV Live: tagged a ") + kGroupName[g] + " pipeline (table row " + std::to_string(row) + ")");
}

// ------------------------------------------------------------------ mapped upload buffers: GPU address -> CPU pointer
struct Mapped { uint64_t va = 0, size = 0; uint8_t* cpu = nullptr; int refs = 0; };
std::shared_mutex                              gMapMx;
std::unordered_map<ID3D12Resource*, Mapped>    gByRes;
std::map<uint64_t, ID3D12Resource*>            gByVa;
std::atomic<long>                              gMapsSeen{0};

uint8_t* cpuOf(uint64_t va, uint64_t need) {
    std::shared_lock l(gMapMx);
    auto it = gByVa.upper_bound(va);
    if (it == gByVa.begin()) return nullptr;
    --it;
    auto r = gByRes.find(it->second);
    if (r == gByRes.end()) return nullptr;
    Mapped const& m = r->second;
    if (va < m.va || va + need > m.va + m.size) return nullptr;
    return m.cpu + (va - m.va);
}

using Map_t   = HRESULT(STDMETHODCALLTYPE*)(ID3D12Resource*, UINT, D3D12_RANGE const*, void**);
using Unmap_t = void(STDMETHODCALLTYPE*)(ID3D12Resource*, UINT, D3D12_RANGE const*);
using CreateCps_t = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, D3D12_COMPUTE_PIPELINE_STATE_DESC const*, REFIID, void**);
using SetAddr_t   = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
using Dispatch_t  = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
void* gOrigMap = nullptr; void* gOrigUnmap = nullptr; void* gOrigCps = nullptr; void* gOrigCCbv = nullptr; void* gOrigDispatch = nullptr;

HRESULT STDMETHODCALLTYPE mapHook(ID3D12Resource* r, UINT sub, D3D12_RANGE const* range, void** data) {
    HRESULT const hr = reinterpret_cast<Map_t>(gOrigMap)(r, sub, range, data);
    if (FAILED(hr) || !data || !*data || sub != 0 || !r) return hr;
    D3D12_RESOURCE_DESC const d = r->GetDesc();
    if (d.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || d.Width < 4096) return hr;
    D3D12_HEAP_PROPERTIES hp{};
    D3D12_HEAP_FLAGS      hf{};
    if (FAILED(r->GetHeapProperties(&hp, &hf))) return hr;
    bool const up = hp.Type == D3D12_HEAP_TYPE_UPLOAD || (hp.Type == D3D12_HEAP_TYPE_CUSTOM && hp.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE);
    if (!up) return hr;
    uint64_t const va = r->GetGPUVirtualAddress();
    if (!va) return hr;
    std::unique_lock l(gMapMx);
    auto& m = gByRes[r];
    if (m.refs++ == 0) {
        // a new resource at an old one's address range: drop whatever claimed it before (it is dead)
        auto it = gByVa.lower_bound(va);
        while (it != gByVa.end() && it->first < va + d.Width) { if (it->second != r) gByRes.erase(it->second); it = gByVa.erase(it); }
        m = {va, d.Width, static_cast<uint8_t*>(*data), 1};
        gByVa[va] = r;
        ++gMapsSeen;
    }
    return hr;
}
void STDMETHODCALLTYPE unmapHook(ID3D12Resource* r, UINT sub, D3D12_RANGE const* range) {
    if (sub == 0 && r) {
        std::unique_lock l(gMapMx);
        if (auto it = gByRes.find(r); it != gByRes.end() && --it->second.refs <= 0) {
            if (auto v = gByVa.find(it->second.va); v != gByVa.end() && v->second == r) gByVa.erase(v);
            gByRes.erase(it);
        }
    }
    reinterpret_cast<Unmap_t>(gOrigUnmap)(r, sub, range);
}

// ------------------------------------------------------------------ per-command-list state (lock-free, per thread)
struct LS { void* list = nullptr; int row = -1; uint64_t g[8]{}, c[8]{}; };
thread_local LS  tl[4];
thread_local int tlNext = 0;
LS& ls(void* list) {
    for (auto& s : tl) if (s.list == list) return s;
    LS& s = tl[tlNext++ & 3];
    s = LS{};
    s.list = list;
    return s;
}

std::atomic<bool> gArmed{false};   // enabled, an RF sky drives VV, and something is tagged
Drive             gDrive;
std::mutex        gDriveMx;
std::atomic<uint64_t> gFrame{0};
struct Stat { std::atomic<long> patched{0}, rejected{0}; std::string why; } gStat[GroupCount];
std::atomic<int> gRowIdx[kRowCount], gRowBase[kRowCount];   // where each shader's uniforms were found (root CBV, byte offset); -1 unknown
struct RowInit { RowInit() { for (auto& a : gRowIdx) a = -1; for (auto& a : gRowBase) a = -1; } } gRowInit;
std::mutex gStatMx;

// SEH-guarded copies (an upload buffer can die under us between Map bookkeeping and the patch)
bool sehRead(void* dst, void const* src, size_t n) {
    __try { std::memcpy(dst, src, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool sehWrite(void* dst, void const* src, size_t n) {
    __try { std::memcpy(dst, src, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool isUnit(float const* v) { float const l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); return std::fabs(l - 1.0f) < 0.03f; }
bool isRot(float const* m) {   // the 3x3 part of a 4x4 (either major order): orthonormal rows
    float const* r[3] = {m, m + 4, m + 8};
    for (int i = 0; i < 3; ++i) {
        if (!isUnit(r[i])) return false;
        for (int j = i + 1; j < 3; ++j)
            if (std::fabs(r[i][0] * r[j][0] + r[i][1] * r[j][1] + r[i][2] * r[j][2]) > 0.03f) return false;
    }
    return true;
}
bool matches(Row const& L, uint8_t const* blk) {
    float f[16];
    for (int r : {R::UView, R::UInvView}) if (L.off[r] >= 0) { std::memcpy(f, blk + L.off[r], 64); if (!isRot(f)) return false; }
    for (int r : {R::SunDir, R::MoonDir, R::DirLightDir}) if (L.off[r] >= 0) { std::memcpy(f, blk + L.off[r], 16); if (!isUnit(f)) return false; }
    return true;
}

float lum(float const* c) { return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; }
// turn c's hue toward t, keeping c's brightness (VV's own units and time of day stay in charge of how bright)
void hueTo(float* c, float const* t, float k) {
    if (k <= 0.0f) return;
    float const lc = lum(c), lt = lum(t);
    if (lt < 1e-6f || !(lc > 0.0f)) return;
    for (int i = 0; i < 3; ++i) c[i] = std::max(0.0f, c[i] + (t[i] * lc / lt - c[i]) * std::min(k, 1.0f));
}

/// rewrite one tagged pass's uniforms before the game records it
void patch(int row, uint64_t const* cbv) {
    Row const& L = table::kRows[row];
    auto& st = gStat[groupOf(L)];
    if (L.size > 4096) return;
    uint8_t* p = nullptr;
    int      idx = gRowIdx[row].load(), base = gRowBase[row].load();
    alignas(16) uint8_t blk[4096];
    auto readAt = [&](int i, int b) -> uint8_t* {
        if (i < 0 || i > 7 || !cbv[i]) return nullptr;
        uint8_t* q = cpuOf(cbv[i] + uint64_t(b), uint64_t(L.size));
        if (!q || !sehRead(blk, q, size_t(L.size))) return nullptr;
        return matches(L, blk) ? q : nullptr;
    };
    if (idx >= 0) p = readAt(idx, base);
    if (!p) {   // find which root constant buffer (and where in it) this shader's uniforms are
        static thread_local uint64_t lastTry[kRowCount]{};
        uint64_t const fr = gFrame.load(std::memory_order_relaxed);
        if (lastTry[row] == fr + 1) return;
        lastTry[row] = fr + 1;
        for (int i = 0; i < 8 && !p; ++i)
            for (int b = 0; b <= 512 && !p; b += 16)
                if ((p = readAt(i, b))) { idx = i, base = b; }
        if (!p) {
            ++st.rejected;
            std::lock_guard l(gStatMx);
            int n = 0; for (int i = 0; i < 8; ++i) n += cbv[i] ? 1 : 0;
            st.why = n ? "uniforms not recognised in the " + std::to_string(n) + " bound constant buffers" : "no root constant buffer bound";
            return;
        }
        if (gRowIdx[row].exchange(idx) != idx || gRowBase[row].exchange(base) != base)
            ll_shim::info(std::string("[RF] VV Live: ") + kGroupName[groupOf(L)] + " uniforms found (table row " + std::to_string(row) + ", root " + std::to_string(idx) + ", +" + std::to_string(base) + " bytes)");
    }
    {   // the same buffer twice in a frame (a pass drawn twice): patch it once - hue shifts must not stack
        static std::atomic<uint64_t> lastVa[kRowCount]{}, lastFr[kRowCount]{};
        uint64_t const va = cbv[idx] + uint64_t(base), fr = gFrame.load(std::memory_order_relaxed) + 1;
        bool const sameVa = lastVa[row].exchange(va) == va, sameFr = lastFr[row].exchange(fr) == fr;
        if (sameVa && sameFr) return;
    }
    Drive d;
    Settings s;
    { std::lock_guard l(gDriveMx); d = gDrive; }
    { std::lock_guard l(gSetMx); s = gSet; }
    auto f4 = [&](int role) { return reinterpret_cast<float*>(blk + L.off[role]); };
    std::vector<std::pair<int, int>> dirty;   // (offset, bytes)
    auto hue = [&](int role, float const* t, float kk) { if (L.off[role] < 0) return; hueTo(f4(role), t, kk); dirty.push_back({L.off[role], 12}); };
    float amb[3];   // the light the sky itself gives: between its zenith and its horizon
    for (int i = 0; i < 3; ++i) amb[i] = d.zen[i] / std::max(lum(d.zen), 1e-5f) * 0.45f + d.hor[i] / std::max(lum(d.hor), 1e-5f) * 0.55f;
    hue(R::Zen, d.zen, s.sky);
    hue(R::Hor, d.hor, s.sky);
    hue(R::FogCol, d.hor, s.sky);
    hue(R::SkyAmb, amb, s.sky * 0.8f);
    hue(R::Sun, d.key, s.light);
    hue(R::Moon, d.key, s.light * 0.6f);
    hue(R::DirLight, d.key, s.light);
    if (s.pulse && d.pulse > 0.0f) hue(R::DirLight, d.pulseCol, d.pulse * 0.30f);
    if (L.off[R::AirAlbedo] >= 0) {
        float* a = f4(R::AirAlbedo);
        hueTo(a, d.air, s.air);
        if (s.pulse && d.pulse > 0.0f) hueTo(a, d.pulseCol, d.pulse * 0.60f);
        if (s.deck) a[3] = s.deckExt * (1.0f + 0.8f * d.closeIn) * (1.0f + 0.6f * (s.pulse ? d.pulse : 0.0f));
        if (d.impact != 0.0f) {   // the impact frame in the air: every bit of fog goes flat white, then flat black
            float const w = d.impact > 0.0f ? 1.0f : 0.0f;
            a[0] = a[1] = a[2] = w;
            a[3] = std::max(a[3], 0.02f) * (d.impact > 0.0f ? 1.5f : 3.0f);
        }
        dirty.push_back({L.off[R::AirAlbedo], 16});
    }
    if (L.off[R::HeightFog] >= 0 && s.deck) {
        auto const cam = rf::lyrics::camera();
        if (cam.valid) {
            float const top = d.deckTop + d.closeIn * s.rise;
            float const T   = std::max(4.0f, s.deckThk * (1.0f + 0.5f * d.closeIn));
            float* h = f4(R::HeightFog);
            h[0] = -1.0f / T;                                   // thicker going down ...
            h[1] = (top - float(cam.pos[1])) / T;               // ... from nothing at the deck's top (y is camera-relative)
            dirty.push_back({L.off[R::HeightFog], 8});
        }
    }
    bool ok = true;
    for (auto [off, n] : dirty) ok = sehWrite(p + off, blk + off, size_t(n)) && ok;
    if (ok) ++st.patched; else ++st.rejected;
}

// ------------------------------------------------------------------ compute hooks (the fog volume is a dispatch)
HRESULT STDMETHODCALLTYPE createCpsHook(ID3D12Device* d, D3D12_COMPUTE_PIPELINE_STATE_DESC const* desc, REFIID riid, void** out) {
    HRESULT const hr = reinterpret_cast<CreateCps_t>(gOrigCps)(d, desc, riid, out);
    if (SUCCEEDED(hr) && out && *out && desc && desc->CS.pShaderBytecode && desc->CS.BytecodeLength) {
        ID3D12PipelineState* pso = nullptr;
        if (SUCCEEDED(static_cast<IUnknown*>(*out)->QueryInterface(IID_PPV_ARGS(&pso))) && pso) {
            tag(pso, rf::skyscan::fnv1a(static_cast<uint8_t const*>(desc->CS.pShaderBytecode), desc->CS.BytecodeLength));
            pso->Release();   // only a key
        }
    }
    return hr;
}
void STDMETHODCALLTYPE setComputeCbvHook(ID3D12GraphicsCommandList* l, UINT i, D3D12_GPU_VIRTUAL_ADDRESS v) {
    reinterpret_cast<SetAddr_t>(gOrigCCbv)(l, i, v);
    if (gArmed.load(std::memory_order_relaxed) && i < 8) ls(l).c[i] = v;
}
void STDMETHODCALLTYPE dispatchHook(ID3D12GraphicsCommandList* l, UINT x, UINT y, UINT z) {
    if (gArmed.load(std::memory_order_relaxed)) {
        LS& s = ls(l);
        if (s.row >= 0 && table::kRows[s.row].compute) patch(s.row, s.c);
    }
    reinterpret_cast<Dispatch_t>(gOrigDispatch)(l, x, y, z);
}

void rearm() {
    bool on;
    { std::lock_guard l(gSetMx); on = gSet.enabled; }
    { std::lock_guard l(gDriveMx); on = on && gDrive.on; }
    on = on && gUsed.load() > 0 && !rf::lab::vvTest();
    gArmed.store(on, std::memory_order_relaxed);
}
} // namespace

// ------------------------------------------------------------------ API
void learn(fs::path const& dir) {
    // how many of the table's shaders this game build really has (0 = another game version: regenerate VvLiveTable.h)
    int have = 0, files = 0;
    std::error_code ec;
    for (auto const& e : fs::directory_iterator(dir, ec)) {
        auto const name = e.path().filename().string();
        if (name.rfind("Deferred", 0) != 0 && name.rfind("SkyProbe", 0) != 0 && name.rfind("PopulateVolume", 0) != 0) continue;
        std::ifstream f(e.path(), std::ios::binary);
        if (!f) continue;
        ++files;
        std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
        for (auto const& s : rf::skyscan::scan(d.data(), d.size()))
            if (rowOfHash(s.hash) >= 0) ++have;
    }
    ll_shim::info("[RF] VV Live: " + std::to_string(have) + " shaders of this game match the table (" + std::to_string(kRowCount) + " rows, " + std::to_string(files) +
                  " material files)" + (have ? "" : " - none: the game was updated, VV Live stays off until the table is regenerated"));
}

void loadConfig(fs::path const& configDir) {
    gConfig = configDir / "VvLive.txt";
    std::ifstream f(gConfig);
    if (!f) { saveConfig(); return; }
    std::string line;
    std::lock_guard l(gSetMx);
    while (std::getline(f, line)) {
        std::istringstream is(line);
        std::string k; float v;
        if (!(is >> k >> v) || k[0] == '#') continue;
        if (k == "enabled") gSet.enabled = v != 0; else if (k == "sky") gSet.sky = v; else if (k == "light") gSet.light = v;
        else if (k == "air") gSet.air = v; else if (k == "deck") gSet.deck = v != 0; else if (k == "deck_density") gSet.deckExt = v;
        else if (k == "deck_thickness") gSet.deckThk = v; else if (k == "final_rise") gSet.rise = v; else if (k == "pulse") gSet.pulse = v != 0;
        else if (k == "sky_gain") gSet.skyGain = v;
    }
}

void onGameDevice(ID3D12Device* dev) {
    static std::atomic<bool> once{false};
    if (!dev || once.exchange(true)) return;
    void** dv = *reinterpret_cast<void***>(dev);
    int rc0 = ll_shim::hook(dv[11], reinterpret_cast<void*>(&createCpsHook), &gOrigCps);   // CreateComputePipelineState
    // a throw-away upload buffer and command list on the game's own device: their vtables are the game's
    int rc1 = -1, rc2 = -1, rc3 = -1, rc4 = -1;
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_UPLOAD};
    D3D12_RESOURCE_DESC   rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER, rd.Width = 256, rd.Height = 1, rd.DepthOrArraySize = 1, rd.MipLevels = 1;
    rd.SampleDesc.Count = 1, rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* buf = nullptr;
    if (SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buf))) && buf) {
        void** rv = *reinterpret_cast<void***>(buf);
        rc1 = ll_shim::hook(rv[8], reinterpret_cast<void*>(&mapHook), &gOrigMap);
        rc2 = ll_shim::hook(rv[9], reinterpret_cast<void*>(&unmapHook), &gOrigUnmap);
        buf->Release();
    }
    ID3D12CommandAllocator*    alloc = nullptr;
    ID3D12GraphicsCommandList* list  = nullptr;
    if (SUCCEEDED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))) && alloc &&
        SUCCEEDED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, IID_PPV_ARGS(&list))) && list) {
        void** lv = *reinterpret_cast<void***>(list);
        rc3 = ll_shim::hook(lv[37], reinterpret_cast<void*>(&setComputeCbvHook), &gOrigCCbv);   // SetComputeRootConstantBufferView
        rc4 = ll_shim::hook(lv[14], reinterpret_cast<void*>(&dispatchHook), &gOrigDispatch);    // Dispatch
        list->Close();
    }
    if (list) list->Release();
    if (alloc) alloc->Release();
    ll_shim::info("[RF] VV Live: hooks at device creation (compute pso " + std::to_string(rc0) + ", map " + std::to_string(rc1) + "/" +
                  std::to_string(rc2) + ", compute cbv " + std::to_string(rc3) + ", dispatch " + std::to_string(rc4) + ")");
}

void onCreatedGraphicsPso(ID3D12PipelineState* pso, uint64_t h) { tag(pso, h); }
void onSetPso(ID3D12GraphicsCommandList* l, ID3D12PipelineState* pso) {
    if (!gArmed.load(std::memory_order_relaxed)) return;
    ls(l).row = rowOfPso(pso);
}
void onSetGraphicsCbv(ID3D12GraphicsCommandList* l, unsigned i, uint64_t va) {
    if (gArmed.load(std::memory_order_relaxed) && i < 8) ls(l).g[i] = va;
}
void beforeDraw(ID3D12GraphicsCommandList* l) {
    if (!gArmed.load(std::memory_order_relaxed)) return;
    LS& s = ls(l);
    if (s.row >= 0 && !table::kRows[s.row].compute) patch(s.row, s.g);
}
void logStatus();
int64_t nowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
std::atomic<int64_t> gDriveAt{0};
void setDrive(Drive const& d) {
    { std::lock_guard l(gDriveMx); gDrive = d; }
    gDriveAt = nowMs();
    rearm();
}
void onPresent() {
    ++gFrame;
    // no RF sky drew for half a second (switched off, a menu, the classic renderer): stop patching
    if (nowMs() - gDriveAt.load() > 500) { std::lock_guard l(gDriveMx); gDrive.on = false; }
    rearm();
    static int64_t lastLog = 0;
    static int     logs = 0;
    if (logs < 30 && nowMs() - gDriveAt.load() < 500 && nowMs() - lastLog > 10000) { lastLog = nowMs(); ++logs; logStatus(); }
}
void logStatus() {
    Drive d;
    { std::lock_guard l(gDriveMx); d = gDrive; }
    std::string s = std::string("[RF] VV Live: ") + (gArmed.load() ? "patching" : "idle") + ", drive " + (d.on ? "on" : "off") + ", phase " + std::to_string(d.phase) +
                    ", deck top " + std::to_string(d.deckTop) + ", final " + std::to_string(d.closeIn) + ", pulse " + std::to_string(d.pulse) +
                    ", upload buffers " + std::to_string(gMapsSeen.load());
    for (int k = 0; k < GroupCount; ++k) {
        std::string why;
        { std::lock_guard l(gStatMx); why = gStat[k].why; }
        s += std::string(" | ") + kGroupName[k] + ": pipelines " + std::to_string(gTagged[k].load()) + ", patched " + std::to_string(gStat[k].patched.load()) +
             ", missed " + std::to_string(gStat[k].rejected.load()) + (why.empty() ? "" : " (" + why + ")");
    }
    ll_shim::info(s + " | match pass: " + rf::sky::worldFxVvStatus());
}
bool  active() { return gArmed.load(std::memory_order_relaxed); }
float skyGain() { std::lock_guard l(gSetMx); return gSet.skyGain; }

void gui() {
    if (!ImGui::CollapsingHeader("Vibrant Visuals Live")) return;
    ImGui::TextDisabled("VV's own light, reflections and fog follow the Cloud Sea sky and the match (by Novak).");
    bool changed = false;
    Settings s;
    { std::lock_guard l(gSetMx); s = gSet; }
    changed |= ImGui::Checkbox("On", &s.enabled);
    changed |= ImGui::SliderFloat("Cloud Sea sky brightness (VV)", &s.skyGain, 0.01f, 1000.0f, "%.3g", ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::SliderFloat("Sky colours -> VV light + reflections", &s.sky, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Sun / moon light colour", &s.light, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Fog colour", &s.air, 0.0f, 1.0f);
    changed |= ImGui::Checkbox("Cloud deck as volumetric fog", &s.deck);
    changed |= ImGui::SliderFloat("Deck density", &s.deckExt, 0.0f, 0.5f, "%.4f", ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::SliderFloat("Deck thickness (blocks)", &s.deckThk, 4.0f, 160.0f, "%.0f");
    changed |= ImGui::SliderFloat("Final fight: clouds rise (blocks)", &s.rise, 0.0f, 60.0f, "%.0f");
    changed |= ImGui::Checkbox("Bed break: the air takes the team's colour", &s.pulse);
    if (changed) { { std::lock_guard l(gSetMx); gSet = s; } saveConfig(); rearm(); }
    Drive d;
    { std::lock_guard l(gDriveMx); d = gDrive; }
    ImGui::Text("%s - driven by %s, phase %.2f, deck top y %.0f, final fight %.2f, pulse %.2f", gArmed.load() ? "patching" : "idle",
                d.on ? "the Cloud Sea" : "no RF sky (Cloud Sea in VV turns it on)", d.phase, d.deckTop, d.closeIn, d.pulse);
    ImGui::Text("Upload buffers mapped: %ld", gMapsSeen.load());
    for (int k = 0; k < GroupCount; ++k) {
        std::string why;
        { std::lock_guard l(gStatMx); why = gStat[k].why; }
        ImGui::Text("%s: %d pipelines, %ld patched, %ld missed%s%s", kGroupName[k], gTagged[k].load(), gStat[k].patched.load(), gStat[k].rejected.load(),
                    why.empty() ? "" : " - ", why.c_str());
    }
    ImGui::Text("Match pass (beacons, shards, kills): %s", rf::sky::worldFxVvStatus().c_str());
    if (rf::lab::vvTest()) ImGui::TextColored(ImVec4(1, .7f, .3f, 1), "Lab vvtest is on: off this session");
}

} // namespace rf::vvlive
