// File-open redirection for shaderpacks (Win32 side; no game headers here).
#include "core/Win32Input.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace bedrock_forge::win32::files {

namespace {
RedirectFn gRedirect = nullptr;
void*      gOrigCreateFileW = nullptr;
void*      gOrigCreateFile2 = nullptr;

HANDLE WINAPI detourCreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition,
                                DWORD flags, HANDLE templ) {
    wchar_t buf[1024];
    if (gRedirect && name && gRedirect(name, buf, 1024)) name = buf;
    return reinterpret_cast<decltype(&CreateFileW)>(gOrigCreateFileW)(name, access, share, sa, disposition, flags, templ);
}

HANDLE WINAPI detourCreateFile2(LPCWSTR name, DWORD access, DWORD share, DWORD disposition, LPCREATEFILE2_EXTENDED_PARAMETERS ext) {
    wchar_t buf[1024];
    if (gRedirect && name && gRedirect(name, buf, 1024)) name = buf;
    return reinterpret_cast<decltype(&CreateFile2)>(gOrigCreateFile2)(name, access, share, disposition, ext);
}
} // namespace

int hookSites(HookSite* out, int max) {
    HMODULE kb = GetModuleHandleW(L"KernelBase.dll");
    if (!kb || max < 2) return 0;
    int n = 0;
    if (void* p = reinterpret_cast<void*>(GetProcAddress(kb, "CreateFileW")))
        out[n++] = {"CreateFileW", p, reinterpret_cast<void*>(&detourCreateFileW), &gOrigCreateFileW};
    if (void* p = reinterpret_cast<void*>(GetProcAddress(kb, "CreateFile2")))
        out[n++] = {"CreateFile2", p, reinterpret_cast<void*>(&detourCreateFile2), &gOrigCreateFile2};
    return n;
}

void setRedirect(RedirectFn fn) { gRedirect = fn; }

} // namespace bedrock_forge::win32::files
