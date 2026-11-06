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

void init(fs::path const& outDir, fs::path const& configDir) {
    gOutDir     = outDir;
    gConfigFile = configDir / "GlassArmor.txt";
    std::ifstream in(gConfigFile);
    std::string   v;
    if (in >> v) gEnabled = v == "1";
}

bool enabled() { return gEnabled.load(std::memory_order_relaxed); }
bool tracking() { return gEnabled.load(std::memory_order_relaxed) || gCap.load(std::memory_order_relaxed) != 0; }

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
    std::lock_guard l(gMutex);
    gPsos[pso] = std::move(i);
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
    s << hex(ps) << " " << nameOf(ps) << " | idx " << count << " inst " << inst << " | tex:";
    std::lock_guard l(gMutex);
    for (auto const& [ri, k, cpu] : cpus) {
        auto it = gSrvs.find(cpu);
        if (it == gSrvs.end()) continue;
        auto const& v = it->second;
        s << " [r" << ri << "." << k << " " << v.w << "x" << v.h << " " << fmt(DXGI_FORMAT(v.fmt)) << " m" << v.mips << " " << v.res << "]";
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
        if (it != gSrvs.end() && it->second.res && it->second.w == 64 && it->second.h == 32) return it->second.res;
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
    if (!cnt) return -1;
    int best = -1;
    long bestD = 1L << 40;
    for (int v = 0; v < shaders::kVariants; ++v) {
        if (std::memcmp(bits, shaders::kVariant[v].mask, sizeof bits) != 0) continue;
        long d = 0;
        for (int c = 0; c < 3; ++c) { long e = sum[c] / cnt - shaders::kVariant[v].avg[c]; d += e * e; }
        if (d < bestD) { bestD = d; best = v; }
    }
    return best;
}

ComPtr<ID3D12PipelineState> makeGlassPso(ID3D12Device* dev, ActorDesc const& ad, int v) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = ad.d;
    d.PS = {shaders::kVariant[v].ps, shaders::kVariant[v].psSize};
    auto& rt = d.BlendState.RenderTarget[0];
    rt.BlendEnable           = TRUE;
    rt.LogicOpEnable         = FALSE;
    rt.SrcBlend              = D3D12_BLEND_SRC_ALPHA;
    rt.DestBlend             = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOp               = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha         = D3D12_BLEND_ONE;
    rt.DestBlendAlpha        = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // glass: the world behind stays visible
    d.RasterizerState.CullMode         = D3D12_CULL_MODE_NONE;
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

/// Present: read back newly seen 64x32 actor textures (a few per frame) and classify the finished ones.
void pumpReadbacks(ID3D12Device* dev, ID3D12CommandQueue* q) {
    if (!gFence && FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gFence)))) return;
    if (!gInFlight.empty()) {
        if (gFence->GetCompletedValue() < gFenceVal) return;
        for (auto& r : gInFlight) {
            int   kind = -1;
            void* m    = nullptr;
            D3D12_RANGE rr{0, 256 * 32};
            if (SUCCEEDED(r.buf->Map(0, &rr, &m)) && m) {
                kind = classify(static_cast<uint8_t const*>(m), 256);
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
        if (td.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || td.Width != 64 || td.Height != 32 ||
            (td.Format != DXGI_FORMAT_R8G8B8A8_UNORM && td.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)) {
            std::lock_guard l(gMutex);
            gTexKind[tex.Get()] = -1;
            continue;
        }
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_READBACK};
        D3D12_RESOURCE_DESC   bd{};
        bd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width            = 256 * 32;
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
        dst.PlacedFootprint.Footprint.Width    = 64;
        dst.PlacedFootprint.Footprint.Height   = 32;
        dst.PlacedFootprint.Footprint.Depth    = 1;
        dst.PlacedFootprint.Footprint.RowPitch = 256;
        src.pResource                          = tex.Get();
        src.Type                               = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex                   = 0;
        gList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        if (barrier) {
            std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
            gList->ResourceBarrier(1, &b);
        }
        gInFlight.push_back({tex, buf});
    }
    gList->Close();
    if (gInFlight.empty()) return;
    ID3D12CommandList* lists[] = {gList.Get()};
    q->ExecuteCommandLists(1, lists);
    q->Signal(gFence.Get(), ++gFenceVal);
}
} // namespace

ID3D12PipelineState* glassPsoFor(ID3D12GraphicsCommandList* list, ID3D12PipelineState** game) {
    if (!shadersBuilt()) return nullptr;
    ID3D12PipelineState*       pso = nullptr;
    std::shared_ptr<ActorDesc> ad;
    {
        std::lock_guard l(gMutex);
        auto it = gListPso.find(list);
        if (it == gListPso.end()) return nullptr;
        pso    = it->second;
        auto a = gActor.find(pso);
        if (a == gActor.end()) return nullptr;
        ad = a->second;
    }
    ID3D12Resource* tex = boundArmorTexture(list);
    if (!tex) return nullptr;
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

void onPresent(ID3D12Device* device, ID3D12CommandQueue* queue) {
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
        if (gHave[0] && gHave[1]) writeReport();
    }
}

void drawUi() {
    if (!ImGui::CollapsingHeader("Glass Armor")) return;
    ImGui::Indent();
    bool on = gEnabled.load();
    if (ImGui::Checkbox("Glass armor on (M1a test: see-through + neon outline)", &on)) {
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
        ImGui::Text("Glass draws last frame: %ld   actor pipelines: %zu   glass pipelines: %zu   failed: %ld",
                    gGlassDrawsShown.load(), gActor.size(), gGlass.size(), gPsoFail.load());
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

} // namespace rf::glassarmor
