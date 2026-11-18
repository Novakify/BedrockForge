// Renderer Foundation - GPU crash report (DRED), opt-in.
//
// Why: since 2026-11-13 the game sometimes dies inside the graphics driver (nvoglv64.dll, always the same game call),
// e.g. a few seconds after joining a world. That is the driver tripping over a GPU fault ("device removed") that
// happened earlier: the call that crashes is not the one at fault. D3D12's Device Removed Extended Data records which
// GPU work was running and which allocation a page fault hit - written to logs/rf_gpu_crash.txt.
//
// Off unless mods/RendererFoundation/config/Dred.txt exists (breadcrumbs cost a little CPU on every command list).
#include "Dred.h"
#include "LLShim.h"
#include <windows.h>
#include <d3d12.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace rf::dred {
namespace {
namespace fs = std::filesystem;
std::atomic<bool>           gOn{false}, gReported{false};
std::atomic<ID3D12Device*>  gDev{nullptr};
PVOID                       gVeh = nullptr;

fs::path versionDir() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return fs::path(exe).parent_path();
}
std::string narrow(wchar_t const* w) {
    std::string s;
    if (!w) return "-";
    for (; *w; ++w) s += *w < 128 ? char(*w) : '?';
    return s;
}
std::string narrow(char const* c) { return c ? std::string(c) : std::string("-"); }

void report(char const* why) {
    if (gReported.exchange(true)) return;
    ID3D12Device* dev = gDev.load();
    std::ostringstream o;
    o << "Renderer Foundation GPU crash report (" << why << ")\n";
    if (!dev) { o << "no device\n"; }
    else {
        HRESULT const r = dev->GetDeviceRemovedReason();
        o << "device removed reason: 0x" << std::hex << unsigned(r) << std::dec << (r == S_OK ? " (not removed)" : "") << "\n";
        ID3D12DeviceRemovedExtendedData1* dred = nullptr;
        if (SUCCEEDED(dev->QueryInterface(IID_PPV_ARGS(&dred))) && dred) {
            D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 bc{};
            if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput1(&bc))) {
                int n = 0;
                for (auto const* node = bc.pHeadAutoBreadcrumbNode; node && n < 64; node = node->pNext, ++n) {
                    UINT const done  = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0;
                    UINT const total = node->BreadcrumbCount;
                    if (done >= total) continue;   // finished lists say nothing
                    o << "list '" << narrow(node->pCommandListDebugNameW) << "' queue '" << narrow(node->pCommandQueueDebugNameW) << "': " << done
                      << " of " << total << " ops done; next ops:";
                    for (UINT i = done; i < total && i < done + 12; ++i) o << " " << int(node->pCommandHistory[i]);
                    o << "\n";
                }
            }
            D3D12_DRED_PAGE_FAULT_OUTPUT1 pf{};
            if (SUCCEEDED(dred->GetPageFaultAllocationOutput1(&pf))) {
                o << "page fault VA: 0x" << std::hex << pf.PageFaultVA << std::dec << "\n";
                int n = 0;
                for (auto const* a = pf.pHeadExistingAllocationNode; a && n < 16; a = a->pNext, ++n)
                    o << "  existing allocation at the VA: '" << narrow(a->ObjectNameW) << "' type " << int(a->AllocationType) << "\n";
                n = 0;
                for (auto const* a = pf.pHeadRecentFreedAllocationNode; a && n < 16; a = a->pNext, ++n)
                    o << "  recently FREED allocation at the VA: '" << narrow(a->ObjectNameW) << "' type " << int(a->AllocationType) << "\n";
            }
            dred->Release();
        } else o << "DRED not available (was it on before the device was made?)\n";
    }
    std::error_code ec;
    fs::create_directories(versionDir() / "logs", ec);
    std::ofstream(versionDir() / "logs" / "rf_gpu_crash.txt", std::ios::app) << o.str() << "\n";
}

LONG CALLBACK onException(EXCEPTION_POINTERS* e) {
    if (!e || !e->ExceptionRecord || e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || gReported.load()) return EXCEPTION_CONTINUE_SEARCH;
    HMODULE m = nullptr;
    char    name[MAX_PATH]{};
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(e->ExceptionRecord->ExceptionAddress), &m) && m)
        GetModuleFileNameA(m, name, MAX_PATH);
    std::string const mod = name;
    if (mod.find("nv") == std::string::npos && mod.find("d3d12") == std::string::npos) return EXCEPTION_CONTINUE_SEARCH;   // only driver faults
    report(("access violation in " + mod).c_str());
    return EXCEPTION_CONTINUE_SEARCH;
}
} // namespace

void beforeDevice() {
    std::error_code ec;
    if (gOn.load() || !fs::exists(versionDir() / "mods" / "RendererFoundation" / "config" / "Dred.txt", ec)) return;
    using GetDbg_t = HRESULT(WINAPI*)(REFIID, void**);
    HMODULE d3d = GetModuleHandleW(L"d3d12.dll");
    if (!d3d) d3d = LoadLibraryW(L"d3d12.dll");
    auto get = d3d ? reinterpret_cast<GetDbg_t>(GetProcAddress(d3d, "D3D12GetDebugInterface")) : nullptr;
    ID3D12DeviceRemovedExtendedDataSettings* s = nullptr;
    if (!get || FAILED(get(IID_PPV_ARGS(&s))) || !s) { ll_shim::warn("[RF] GPU crash report: DRED settings not available"); return; }
    s->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    s->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    s->Release();
    gOn = true;
    ll_shim::info("[RF] GPU crash report on (config/Dred.txt): a driver crash writes logs/rf_gpu_crash.txt");
}
void watch(ID3D12Device* dev) {
    if (!gOn.load() || !dev) return;
    ID3D12Device* none = nullptr;
    if (gDev.compare_exchange_strong(none, dev)) dev->AddRef();
    if (!gVeh) gVeh = AddVectoredExceptionHandler(1, &onException);
}
void check() {
    if (!gOn.load() || gReported.load()) return;
    ID3D12Device* dev = gDev.load();
    if (dev && dev->GetDeviceRemovedReason() != S_OK) report("device removed, seen at Present");
}
bool on() { return gOn.load(); }
} // namespace rf::dred
