// Renderer Foundation - MaterialBinLoader for 1.26 (see MaterialLoader.h, Notice.h).
#include "MaterialLoader.h"

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>

#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/service/Bedrock.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/resources/ResourcePackManager.h"

#include "Options.h"

namespace rf::materials {

namespace {
namespace fs = std::filesystem;

struct State {
    std::wstring dataDir;  // <game>\data\renderer\materials\ (lower case)
    std::wstring packDir;  // selected shaderpack, "" = none
    std::wstring cacheDir; // where resource-pack materials are written so the open can be redirected
};
std::atomic<std::shared_ptr<State const>> gState{std::make_shared<State const>()};
fs::path                                  gModDir;
std::atomic<long>                         gSeen{0}, gFromPacks{0}, gFromShaderpack{0};
std::mutex                                gLogMutex;
std::deque<std::string>                   gServed;
thread_local int                          tInHook = 0; // ResourcePackManager::load opens files itself

std::wstring lower(std::wstring_view s) {
    std::wstring r(s);
    for (auto& c : r) c = static_cast<wchar_t>(std::towlower(c));
    return r;
}
void note(std::string s) {
    std::lock_guard l(gLogMutex);
    gServed.push_front(std::move(s));
    if (gServed.size() > 24) gServed.pop_back();
}

bool copyOut(std::wstring const& path, wchar_t* out, size_t outLen) {
    if (path.size() + 1 >= outLen) return false;
    std::copy(path.begin(), path.end(), out);
    out[path.size()] = 0;
    return true;
}

// Ask the resource pack stack for renderer/materials/<name>; write it to the cache and return its path.
bool fromResourcePacks(State const& st, std::wstring const& name, wchar_t* out, size_t outLen) {
    if (!RfOptions::redirectShaders || st.cacheDir.empty()) return false;
    auto ci = ll::service::getClientInstance();
    if (!ci) return false; // too early in boot: the stack isn't there yet
    std::string bytes;
    try {
        ResourceLocation loc(Core::PathView(fs::path(L"renderer/materials/" + name).string()));
        if (!ci->getResourcePackManager().load(loc, bytes) || bytes.size() < 16) return false;
    } catch (...) {
        return false;
    }
    fs::path        dst = fs::path(st.cacheDir) / name;
    std::error_code ec;
    fs::create_directories(dst.parent_path(), ec);
    std::string old;
    if (std::ifstream in{dst, std::ios::binary}; in) old.assign(std::istreambuf_iterator<char>(in), {});
    if (old != bytes) std::ofstream{dst, std::ios::binary | std::ios::trunc}.write(bytes.data(), std::streamsize(bytes.size()));
    if (!copyOut(dst.wstring(), out, outLen)) return false;
    ++gFromPacks;
    note("pack: " + fs::path(name).string());
    return true;
}

bool fromShaderpack(State const& st, std::wstring const& name, wchar_t* out, size_t outLen) {
    if (st.packDir.empty()) return false;
    for (auto const& cand : {st.packDir + L"\\renderer\\materials\\" + name, st.packDir + L"\\" + name}) {
        std::error_code ec;
        if (fs::is_regular_file(cand, ec) && copyOut(cand, out, outLen)) {
            ++gFromShaderpack;
            note("shaderpack: " + fs::path(name).string());
            return true;
        }
    }
    return false;
}

bool redirect(wchar_t const* path, wchar_t* out, size_t outLen) {
    if (tInHook || !RfOptions::materialBinLoaderEnabled) return false;
    std::wstring_view p(path);
    if (p.size() < 14 || _wcsnicmp(p.data() + p.size() - 13, L".material.bin", 13) != 0) return false;
    auto st = gState.load();
    if (st->dataDir.empty()) return false;
    std::wstring lp = lower(p);
    for (auto& c : lp) if (c == L'/') c = L'\\';
    if (lp.find(st->dataDir) == std::wstring::npos) return false; // only the game's own materials are replaced
    ++gSeen;
    std::wstring name(p.substr(p.find_last_of(L"\\/") + 1));
    ++tInHook;
    bool hit = fromResourcePacks(*st, name, out, outLen) || fromShaderpack(*st, name, out, outLen);
    --tInHook;
    return hit;
}

// ---- Win32 / NT file-open hooks (GDK builds don't always go through CreateFileW) ----
void* gOrigCreateFileW  = nullptr;
void* gOrigCreateFile2  = nullptr;
void* gOrigNtCreateFile = nullptr;
using NtCreateFile_t    = NTSTATUS(NTAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PLARGE_INTEGER, ULONG, ULONG,
                                        ULONG, ULONG, PVOID, ULONG);

HANDLE WINAPI detourCreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition, DWORD flags,
                                HANDLE templ) {
    wchar_t buf[1024];
    if (name && redirect(name, buf, 1024)) name = buf;
    return reinterpret_cast<decltype(&CreateFileW)>(gOrigCreateFileW)(name, access, share, sa, disposition, flags, templ);
}
HANDLE WINAPI detourCreateFile2(LPCWSTR name, DWORD access, DWORD share, DWORD disposition, LPCREATEFILE2_EXTENDED_PARAMETERS ext) {
    wchar_t buf[1024];
    if (name && redirect(name, buf, 1024)) name = buf;
    return reinterpret_cast<decltype(&CreateFile2)>(gOrigCreateFile2)(name, access, share, disposition, ext);
}
NTSTATUS NTAPI detourNtCreateFile(PHANDLE h, ACCESS_MASK access, POBJECT_ATTRIBUTES oa, PIO_STATUS_BLOCK io, PLARGE_INTEGER size,
                                  ULONG attrs, ULONG share, ULONG disposition, ULONG options, PVOID ea, ULONG eaLen) {
    OBJECT_ATTRIBUTES copy;
    UNICODE_STRING    us;
    wchar_t           buf[1100];
    if (oa && !oa->RootDirectory && oa->ObjectName && oa->ObjectName->Buffer) {
        USHORT len = oa->ObjectName->Length / sizeof(wchar_t);
        wchar_t const* src = oa->ObjectName->Buffer;
        if (len > 14 && len < 1000 && _wcsnicmp(src + len - 13, L".material.bin", 13) == 0) {
            size_t  skip = (src[0] == L'\\' && src[1] == L'?' && src[2] == L'?' && src[3] == L'\\') ? 4 : 0;
            wchar_t in[1024], outp[1024];
            wmemcpy(in, src + skip, len - skip);
            in[len - skip] = 0;
            if (redirect(in, outp, 1000)) {
                size_t n = wcslen(outp);
                wmemcpy(buf, L"\\??\\", 4);
                wmemcpy(buf + 4, outp, n + 1);
                us.Buffer        = buf;
                us.Length        = USHORT((n + 4) * sizeof(wchar_t));
                us.MaximumLength = USHORT((n + 5) * sizeof(wchar_t));
                copy             = *oa;
                copy.ObjectName  = &us;
                oa               = &copy;
            }
        }
    }
    return reinterpret_cast<NtCreateFile_t>(gOrigNtCreateFile)(h, access, oa, io, size, attrs, share, disposition, options, ea, eaLen);
}

struct Site {
    char const* name;
    void*       target;
    void*       detour;
    void**      original;
};
std::vector<Site> gHooked;

void publish(std::wstring packDir) {
    auto cur = gState.load();
    gState.store(std::make_shared<State const>(State{cur->dataDir, std::move(packDir), cur->cacheDir}));
}
} // namespace

std::vector<std::string> listShaderpacks() {
    std::vector<std::string> r;
    std::error_code          ec;
    for (auto& e : fs::directory_iterator(gModDir / "shaderpacks", ec))
        if (e.is_directory()) r.push_back(e.path().filename().string());
    std::sort(r.begin(), r.end());
    return r;
}

void setShaderpack(std::string const& name) {
    std::error_code ec;
    auto            dir = gModDir / "shaderpacks" / name;
    publish(!name.empty() && fs::is_directory(dir, ec) ? dir.wstring() : std::wstring{});
}

Stats stats() { return {gSeen.load(), gFromPacks.load(), gFromShaderpack.load()}; }

std::vector<std::string> lastServed() {
    std::lock_guard l(gLogMutex);
    return {gServed.begin(), gServed.end()};
}

void install(fs::path const& modDir) {
    gModDir = modDir;
    std::error_code ec;
    fs::create_directories(modDir / "shaderpacks", ec);
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring data = lower((fs::path(exe).parent_path() / "data" / "renderer" / "materials").wstring()) + L"\\";
    gState.store(std::make_shared<State const>(State{data, {}, (modDir / "cache" / "rp").wstring()}));
    setShaderpack(RfOptions::shaderPack.get());

    auto& log = ll::mod::NativeMod::current()->getLogger();
    if (HMODULE kb = GetModuleHandleW(L"KernelBase.dll")) {
        gHooked.push_back({"CreateFileW", reinterpret_cast<void*>(GetProcAddress(kb, "CreateFileW")),
                           reinterpret_cast<void*>(&detourCreateFileW), &gOrigCreateFileW});
        gHooked.push_back({"CreateFile2", reinterpret_cast<void*>(GetProcAddress(kb, "CreateFile2")),
                           reinterpret_cast<void*>(&detourCreateFile2), &gOrigCreateFile2});
    }
    if (HMODULE nt = GetModuleHandleW(L"ntdll.dll"))
        gHooked.push_back({"NtCreateFile", reinterpret_cast<void*>(GetProcAddress(nt, "NtCreateFile")),
                           reinterpret_cast<void*>(&detourNtCreateFile), &gOrigNtCreateFile});
    std::erase_if(gHooked, [&](Site const& s) {
        if (!s.target) return true;
        int rc = ll::memory::hook(s.target, s.detour, s.original, ll::memory::HookPriority::Normal);
        log.info("[RF] MaterialBinLoader: hooked {} (rc {})", s.name, rc);
        return rc != 0;
    });
}

void uninstall() {
    for (auto& s : gHooked) ll::memory::unhook(s.target, s.detour);
    gHooked.clear();
}

} // namespace rf::materials
