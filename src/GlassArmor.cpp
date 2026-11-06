// Renderer Foundation - Glass Armor M0 probe (see GlassArmor.h). Original code for Renderer Foundation.
#include "GlassArmor.h"

#include <algorithm>
#include <atomic>
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
#include <d3d12.h>

#include <imgui.h>

#include "LLShim.h"
#include "SkyMaterialScan.h"
#include "UiCapture.h"

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
std::string gLastReport;

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
        {
            std::lock_guard l(gMutex);
            gShaderNames.swap(names);
        }
        gNamesLearned = true;
        ll_shim::info("[RF] glass armor probe: named shaders from " + std::to_string(files) + " material files in " + dir.string());
    }).detach();
}

std::string nameOf(uint64_t h) {
    auto it = gShaderNames.find(h);
    return it == gShaderNames.end() ? std::string("?") : it->second;
}
std::string hex(uint64_t h) {
    char b[20];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(h));
    return b;
}

void writeReport() {
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

void init(fs::path const& outDir) { gOutDir = outDir; }

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
    std::lock_guard l(gMutex);
    gPsos[pso] = std::move(i);
}

bool capturing() { return gCap.load(std::memory_order_relaxed) == 2; }

void onSetPso(ID3D12GraphicsCommandList* list, ID3D12PipelineState* pso) {
    std::lock_guard l(gMutex);
    gListPso[list] = pso;
}

bool wantRoot() { return gCap.load(std::memory_order_relaxed) != 0; }
bool wantSrvs() { return gCap.load(std::memory_order_relaxed) != 0; }

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
    if (gSrvs.size() < 200000) gSrvs[cpu] = i;
}

bool actorFamily(std::string const& n) {
    return n.find("Actor") != std::string::npos || n.find("Entity") != std::string::npos ||
           n.find("Armor") != std::string::npos || n.find("ItemInHand") != std::string::npos;
}

void onDraw(ID3D12GraphicsCommandList* list, unsigned count, unsigned inst) {
    ID3D12PipelineState* pso = nullptr;
    uint64_t             ps  = 0;
    bool                 detail = false;
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
        }
    }
    if (!detail) return;
    // bound textures: every root descriptor table, first 4 descriptors of each (UiCapture lock, then ours)
    auto tables = rf::uicapture::rootTables(list);
    std::vector<std::tuple<unsigned, int, size_t>> cpus;
    for (auto const& [ri, gpu] : tables) {
        size_t   cpu = 0;
        unsigned inc = 0;
        if (rf::uicapture::gpuDescToCpu(gpu, cpu, inc))
            for (int k = 0; k < 4; ++k) cpus.emplace_back(ri, k, cpu + size_t(k) * inc);
    }
    std::ostringstream s;
    s << hex(ps) << " " << nameOf(ps) << " | idx " << count << " inst " << inst << " | tex:";
    std::lock_guard l(gMutex);
    for (auto const& [ri, k, cpu] : cpus) {
        auto it = gSrvs.find(cpu);
        if (it == gSrvs.end()) continue;
        auto const& v = it->second;
        s << " [r" << ri << "." << k << " " << v.w << "x" << v.h << " " << fmt(DXGI_FORMAT(v.fmt)) << " m" << v.mips << " " << v.res << "]";
    }
    if (tables.empty()) s << " (no root tables tracked)";
    gDetail[gWhich].push_back(s.str());
}

void onPresent() {
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
        if (gHave[0] && gHave[1]) writeReport();
    }
}

void drawUi() {
    if (!ImGui::CollapsingHeader("Glass Armor (M0 probe)")) return;
    ImGui::Indent();
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

} // namespace rf::glassarmor
