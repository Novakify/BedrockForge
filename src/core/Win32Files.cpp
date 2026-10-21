// File-open redirection for shaderpacks (Win32 side; no game headers here).
// Hooks both the Win32 layer and the NT layer: GDK games don't always open files through CreateFileW.
#include "core/Win32Input.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>

#include <cwchar>

namespace bedrock_forge::win32::files {

namespace {
RedirectFn gRedirect = nullptr;
void*      gOrigCreateFileW = nullptr;
void*      gOrigCreateFile2 = nullptr;
void*      gOrigNtCreateFile = nullptr;
void*      gOrigNtOpenFile = nullptr;

using NtCreateFile_t = NTSTATUS(NTAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PLARGE_INTEGER, ULONG, ULONG,
                                        ULONG, ULONG, PVOID, ULONG);
using NtOpenFile_t = NTSTATUS(NTAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, ULONG, ULONG);

HANDLE WINAPI detourCreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition,
                                DWORD flags, HANDLE templ) {
    wchar_t buf[1024];
    if (gRedirect && name && gRedirect(0, name, buf, 1024)) name = buf;
    return reinterpret_cast<decltype(&CreateFileW)>(gOrigCreateFileW)(name, access, share, sa, disposition, flags, templ);
}

HANDLE WINAPI detourCreateFile2(LPCWSTR name, DWORD access, DWORD share, DWORD disposition, LPCREATEFILE2_EXTENDED_PARAMETERS ext) {
    wchar_t buf[1024];
    if (gRedirect && name && gRedirect(1, name, buf, 1024)) name = buf;
    return reinterpret_cast<decltype(&CreateFile2)>(gOrigCreateFile2)(name, access, share, disposition, ext);
}

// NT paths look like \??\C:\...; only absolute paths (no RootDirectory) are considered.
bool ntRedirect(int api, POBJECT_ATTRIBUTES oa, OBJECT_ATTRIBUTES& copy, UNICODE_STRING& us, wchar_t* buf, size_t bufLen) {
    if (!gRedirect || !oa || oa->RootDirectory || !oa->ObjectName || !oa->ObjectName->Buffer) return false;
    USHORT len = oa->ObjectName->Length / sizeof(wchar_t);
    if (len < 14 || len >= 1000) return false;
    wchar_t const* p = oa->ObjectName->Buffer;
    // cheap suffix test before anything else: ".material.bin"
    if (_wcsnicmp(p + len - 13, L".material.bin", 13) != 0) return false;
    wchar_t in[1024];
    size_t  skip = (len > 4 && p[0] == L'\\' && p[1] == L'?' && p[2] == L'?' && p[3] == L'\\') ? 4 : 0;
    wmemcpy(in, p + skip, len - skip);
    in[len - skip] = 0;
    wchar_t out[1024];
    if (!gRedirect(api, in, out, 1000)) return false;
    size_t n = wcslen(out);
    if (n + 5 > bufLen) return false;
    wmemcpy(buf, L"\\??\\", 4);
    wmemcpy(buf + 4, out, n + 1);
    us.Buffer        = buf;
    us.Length        = static_cast<USHORT>((n + 4) * sizeof(wchar_t));
    us.MaximumLength = static_cast<USHORT>((n + 5) * sizeof(wchar_t));
    copy             = *oa;
    copy.ObjectName  = &us;
    return true;
}

NTSTATUS NTAPI detourNtCreateFile(PHANDLE h, ACCESS_MASK access, POBJECT_ATTRIBUTES oa, PIO_STATUS_BLOCK io, PLARGE_INTEGER size,
                                  ULONG attrs, ULONG share, ULONG disposition, ULONG options, PVOID ea, ULONG eaLen) {
    OBJECT_ATTRIBUTES copy; UNICODE_STRING us; wchar_t buf[1100];
    if (ntRedirect(2, oa, copy, us, buf, 1100)) oa = &copy;
    return reinterpret_cast<NtCreateFile_t>(gOrigNtCreateFile)(h, access, oa, io, size, attrs, share, disposition, options, ea, eaLen);
}

NTSTATUS NTAPI detourNtOpenFile(PHANDLE h, ACCESS_MASK access, POBJECT_ATTRIBUTES oa, PIO_STATUS_BLOCK io, ULONG share, ULONG options) {
    OBJECT_ATTRIBUTES copy; UNICODE_STRING us; wchar_t buf[1100];
    if (ntRedirect(3, oa, copy, us, buf, 1100)) oa = &copy;
    return reinterpret_cast<NtOpenFile_t>(gOrigNtOpenFile)(h, access, oa, io, share, options);
}
} // namespace

int hookSites(HookSite* out, int max) {
    int n = 0;
    if (HMODULE kb = GetModuleHandleW(L"KernelBase.dll")) {
        if (void* p = reinterpret_cast<void*>(GetProcAddress(kb, "CreateFileW")); p && n < max)
            out[n++] = {"CreateFileW", p, reinterpret_cast<void*>(&detourCreateFileW), &gOrigCreateFileW};
        if (void* p = reinterpret_cast<void*>(GetProcAddress(kb, "CreateFile2")); p && n < max)
            out[n++] = {"CreateFile2", p, reinterpret_cast<void*>(&detourCreateFile2), &gOrigCreateFile2};
    }
    if (HMODULE nt = GetModuleHandleW(L"ntdll.dll")) {
        if (void* p = reinterpret_cast<void*>(GetProcAddress(nt, "NtCreateFile")); p && n < max)
            out[n++] = {"NtCreateFile", p, reinterpret_cast<void*>(&detourNtCreateFile), &gOrigNtCreateFile};
        if (void* p = reinterpret_cast<void*>(GetProcAddress(nt, "NtOpenFile")); p && n < max)
            out[n++] = {"NtOpenFile", p, reinterpret_cast<void*>(&detourNtOpenFile), &gOrigNtOpenFile};
    }
    return n;
}

void setRedirect(RedirectFn fn) { gRedirect = fn; }

} // namespace bedrock_forge::win32::files
