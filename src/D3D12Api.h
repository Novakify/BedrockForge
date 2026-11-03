// Renderer Foundation: D3D12 entry points looked up at run time instead of imported.
// Original code for Renderer Foundation.
//
// Why: clients such as Blueberry put a stub d3d12.dll next to Minecraft.Windows.exe (it exports the D3D12 functions
// by name only and every one returns E_FAIL, so the game falls back to Direct3D 11). d3d12.lib imports
// D3D12CreateDevice by ORDINAL (#101); the stub has no such ordinal, so Windows refused to load RF at all
// ("The operating system cannot run %1"). Looked up by name from whatever d3d12.dll the game has loaded, RF loads
// with the stub (the calls fail cleanly and RF runs its Direct3D 11 path) and with the real runtime.
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>

namespace rf::d3d12api {

inline HMODULE module() {
    static HMODULE m = [] {
        HMODULE h = GetModuleHandleW(L"d3d12.dll"); // the one the game loaded (real runtime or a client's stub)
        if (!h) h = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return h;
    }();
    return m;
}

inline HRESULT CreateDevice(IUnknown* adapter, D3D_FEATURE_LEVEL level, REFIID riid, void** out) {
    auto f = module() ? reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(reinterpret_cast<void*>(GetProcAddress(module(), "D3D12CreateDevice"))) : nullptr;
    return f ? f(adapter, level, riid, out) : E_NOINTERFACE;
}

inline HRESULT SerializeRootSignature(D3D12_ROOT_SIGNATURE_DESC const* desc, D3D_ROOT_SIGNATURE_VERSION v, ID3DBlob** blob, ID3DBlob** err) {
    auto f = module() ? reinterpret_cast<PFN_D3D12_SERIALIZE_ROOT_SIGNATURE>(
                            reinterpret_cast<void*>(GetProcAddress(module(), "D3D12SerializeRootSignature")))
                      : nullptr;
    return f ? f(desc, v, blob, err) : E_NOINTERFACE;
}

} // namespace rf::d3d12api
