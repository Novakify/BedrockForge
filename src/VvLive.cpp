// Renderer Foundation - Vibrant Visuals Live (see VvLive.h). Original code for Renderer Foundation (by Novak).
#define WIN32_LEAN_AND_MEAN
#include "VvLive.h"
#include "Lab.h"
#include "LLShim.h"
#include "LyricsCamera.h"
#include "SkyMaterialScan.h"

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

// ------------------------------------------------------------------ what we patch (bgfx uniform table, MC 1.26.51)
enum Kind : int { None = 0, Deferred = 1, Probe = 2, Populate = 3, KindCount = 4 };
char const* const kKindName[KindCount] = {"-", "lighting (DeferredShading)", "sky probe (SkyProbeDeferredShading)", "fog volume (PopulateVolume)"};
struct Layout {
    int size;                    // bytes of the stage's uniforms
    int mat[2];                  // offsets of rotation matrices (u_view / u_invView) used to recognise the buffer, -1 none
    int unit[2];                 // offsets of unit vectors (SunDir, MoonDir, light direction), -1 none
    int zen, hor, sunCol, moonCol, dirLight, heightFog, airAlbedo;   // -1: not in this stage
};
constexpr Layout kLayout[KindCount] = {
    {0, {-1, -1}, {-1, -1}, -1, -1, -1, -1, -1, -1, -1},
    // DoDeferredShading fragment: u_view 0, SunDir 64, u_invView 80, MoonDir 1040, DirectionalLightSourceDiffuseColorAndIlluminance
    // 1712, MoonColor 1936, SkyHorizonColor 2016, SkyZenithColor 2048, SunColor 2096 (2176 bytes)
    {2176, {0, 80}, {64, 1040}, 2048, 2016, 2096, 1936, 1712, -1, -1},
    // SkyProbe DoDeferredShading fragment: u_invView 0, SkyZenithColor 192, MoonDir 272, MoonColor 368, SunDir 384,
    // SkyHorizonColor 448, SunColor 480 (560 bytes)
    {560, {0, -1}, {272, 384}, 192, 448, 480, 368, -1, -1, -1},
    // Populate compute: u_view 80, DirectionalLightSourceWorldSpaceDirection 208, HeightFogScaleBias 272, AirAlbedoExtinction 288,
    // DirectionalLightSourceDiffuseColorAndIlluminance 368 (528 bytes). density = AirAlbedoExtinction.w *
    // saturate(HeightFogScaleBias.x * y + HeightFogScaleBias.y), y = height relative to the camera (read off the shader)
    {528, {80, -1}, {208, -1}, -1, -1, -1, -1, 368, 272, 288},
};

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

// ------------------------------------------------------------------ learned shaders + tagged pipelines
std::unordered_map<uint64_t, int> gHashKind;   // written at boot (learn), read-only afterwards
std::atomic<int>                 gLearned[KindCount]{};
constexpr int                    kSlots = 64;
std::atomic<void*>               gSlotPso[kSlots]{};
std::atomic<int>                 gSlotKind[kSlots]{};
std::atomic<int>                 gUsed{0};
std::atomic<int>                 gTagged[KindCount]{};

int kindOfPso(void* pso) {
    int const n = gUsed.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
        if (gSlotPso[i].load(std::memory_order_relaxed) == pso) return gSlotKind[i].load(std::memory_order_relaxed);
    return None;
}
void tag(void* pso, uint64_t h) {
    if (!pso) return;
    int const k = [&] { auto it = gHashKind.find(h); return it == gHashKind.end() ? 0 : it->second; }();
    int const n = gUsed.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i)   // a new pipeline at a dead one's address: its old tag is gone
        if (gSlotPso[i].load(std::memory_order_relaxed) == pso) { gSlotKind[i].store(k, std::memory_order_relaxed); if (k) ++gTagged[k]; return; }
    if (!k) return;
    static std::mutex m;
    std::lock_guard l(m);
    int const i = gUsed.load();
    if (i >= kSlots) return;
    gSlotKind[i].store(k);
    gSlotPso[i].store(pso);
    gUsed.store(i + 1, std::memory_order_release);
    ++gTagged[k];
    ll_shim::info(std::string("[RF] VV Live: tagged the game's ") + kKindName[k] + " pipeline");
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
struct LS { void* list = nullptr; int kind = 0; uint64_t g[8]{}, c[8]{}; };
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
struct Stat { std::atomic<long> patched{0}, rejected{0}; std::atomic<int> idx{-1}, base{-1}; std::string why; } gStat[KindCount];
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
bool matches(int k, uint8_t const* blk) {
    Layout const& L = kLayout[k];
    float f[16];
    for (int m : L.mat) if (m >= 0) { std::memcpy(f, blk + m, 64); if (!isRot(f)) return false; }
    for (int u : L.unit) if (u >= 0) { std::memcpy(f, blk + u, 16); if (!isUnit(f)) return false; }
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
void patch(int k, uint64_t const* cbv) {
    Layout const& L = kLayout[k];
    auto& st = gStat[k];
    uint8_t* p = nullptr;
    int      idx = st.idx.load(), base = st.base.load();
    alignas(16) uint8_t blk[2304];
    auto readAt = [&](int i, int b) -> uint8_t* {
        if (i < 0 || i > 7 || !cbv[i]) return nullptr;
        uint8_t* q = cpuOf(cbv[i] + uint64_t(b), uint64_t(L.size));
        if (!q || !sehRead(blk, q, size_t(L.size))) return nullptr;
        return matches(k, blk) ? q : nullptr;
    };
    if (idx >= 0) p = readAt(idx, base);
    if (!p) {   // find which root constant buffer (and where in it) this stage's uniforms are
        static thread_local uint64_t lastTry[KindCount]{};
        uint64_t const fr = gFrame.load(std::memory_order_relaxed);
        if (lastTry[k] == fr) return;
        lastTry[k] = fr;
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
        if (st.idx.exchange(idx) != idx || st.base.exchange(base) != base)
            ll_shim::info(std::string("[RF] VV Live: ") + kKindName[k] + " uniforms found (root " + std::to_string(idx) + ", +" + std::to_string(base) + " bytes)");
    }
    {   // the same buffer twice in a frame (a pass drawn twice): patch it once - hue shifts must not stack
        static std::atomic<uint64_t> lastVa[KindCount]{}, lastFr[KindCount]{};
        uint64_t const va = cbv[idx] + uint64_t(base), fr = gFrame.load(std::memory_order_relaxed);
        if (lastVa[k].exchange(va) == va && lastFr[k].exchange(fr) == fr) return;
        lastFr[k] = fr;
    }
    Drive d;
    Settings s;
    { std::lock_guard l(gDriveMx); d = gDrive; }
    { std::lock_guard l(gSetMx); s = gSet; }
    auto f4 = [&](int off) { return reinterpret_cast<float*>(blk + off); };
    std::vector<std::pair<int, int>> dirty;   // (offset, bytes)
    auto hue = [&](int off, float const* t, float kk) { if (off < 0) return; hueTo(f4(off), t, kk); dirty.push_back({off, 12}); };
    hue(L.zen, d.zen, s.sky);
    hue(L.hor, d.hor, s.sky);
    hue(L.sunCol, d.key, s.light);
    hue(L.moonCol, d.key, s.light * 0.6f);
    hue(L.dirLight, d.key, s.light);
    if (s.pulse && d.pulse > 0.0f) hue(L.dirLight, d.pulseCol, d.pulse * 0.30f);
    if (L.airAlbedo >= 0) {
        float* a = f4(L.airAlbedo);
        hueTo(a, d.air, s.air);
        if (s.pulse && d.pulse > 0.0f) hueTo(a, d.pulseCol, d.pulse * 0.60f);
        if (s.deck) a[3] = s.deckExt * (1.0f + 0.8f * d.closeIn) * (1.0f + 0.6f * (s.pulse ? d.pulse : 0.0f));
        dirty.push_back({L.airAlbedo, 16});
    }
    if (L.heightFog >= 0 && s.deck) {
        auto const cam = rf::lyrics::camera();
        if (cam.valid) {
            float const top = d.deckTop + d.closeIn * s.rise;
            float const T   = std::max(4.0f, s.deckThk * (1.0f + 0.5f * d.closeIn));
            float* h = f4(L.heightFog);
            h[0] = -1.0f / T;                                   // thicker going down ...
            h[1] = (top - float(cam.pos[1])) / T;               // ... from nothing at the deck's top (y is camera-relative)
            dirty.push_back({L.heightFog, 8});
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
        if (s.kind == Populate) patch(Populate, s.c);
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
    struct M { char const* file; char const* pass; char const* stage; int kind; };
    static constexpr M kM[] = {{"DeferredShading", "DoDeferredShading", "Fragment", Deferred},
                               {"SkyProbeDeferredShading", "DoDeferredShading", "Fragment", Probe},
                               {"PopulateVolume", "Populate", "Compute", Populate}};
    for (auto const& m : kM) {
        std::ifstream f(dir / (std::string(m.file) + ".material.bin"), std::ios::binary);
        if (!f) { ll_shim::warn(std::string("[RF] VV Live: ") + m.file + ".material.bin not found"); continue; }
        std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
        int n = 0;
        for (auto const& s : rf::skyscan::scan(d.data(), d.size()))
            if (s.stage == m.stage && s.pass == m.pass && (s.platform == "Direct3D_SM60" || s.platform == "Direct3D_SM65")) {
                gHashKind[s.hash] = m.kind;
                ++n;
            }
        gLearned[m.kind] += n;
        ll_shim::info(std::string("[RF] VV Live: learned ") + std::to_string(n) + " " + kKindName[m.kind] + " shaders");
    }
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
        rc3 = ll_shim::hook(lv[39], reinterpret_cast<void*>(&setComputeCbvHook), &gOrigCCbv);   // SetComputeRootConstantBufferView
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
    ls(l).kind = kindOfPso(pso);
}
void onSetGraphicsCbv(ID3D12GraphicsCommandList* l, unsigned i, uint64_t va) {
    if (gArmed.load(std::memory_order_relaxed) && i < 8) ls(l).g[i] = va;
}
void beforeDraw(ID3D12GraphicsCommandList* l) {
    if (!gArmed.load(std::memory_order_relaxed)) return;
    LS& s = ls(l);
    if (s.kind == Deferred || s.kind == Probe) patch(s.kind, s.g);
}
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
    for (int k = 1; k < KindCount; ++k) {
        std::string why;
        { std::lock_guard l(gStatMx); why = gStat[k].why; }
        ImGui::Text("%s: %d shaders learned, %d pipelines, %ld patched, %ld missed%s%s", kKindName[k], gLearned[k].load(), gTagged[k].load(),
                    gStat[k].patched.load(), gStat[k].rejected.load(), why.empty() ? "" : " - ", why.c_str());
    }
    if (rf::lab::vvTest()) ImGui::TextColored(ImVec4(1, .7f, .3f, 1), "Lab vvtest is on: off this session");
}

} // namespace rf::vvlive
