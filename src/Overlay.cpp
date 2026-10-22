// Renderer Foundation - ImGui overlay (see Overlay.h, Notice.h).
#include "Overlay.h"

#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include "LLShim.h"
#include "PostFx.h"

#include "GUI.h"
#include "Options.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace rf::overlay {

namespace {
using Microsoft::WRL::ComPtr;

std::mutex       gImGuiMutex; // window thread (input) vs render thread (frames)
std::atomic_bool gInitialized{false}, gFailed{false};
std::string      gGpuName, gRendererType = "unknown";
HWND             gWindow   = nullptr;
WNDPROC          gOrigProc = nullptr;

void captureGpuName(IDXGISwapChain* sc) {
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IUnknown>    dev;
    if (FAILED(sc->GetDevice(IID_PPV_ARGS(&dev)))) return;
    LUID luid{};
    ComPtr<ID3D12Device> d12;
    if (SUCCEEDED(dev.As(&d12))) luid = d12->GetAdapterLuid();
    else if (SUCCEEDED(dev.As(&dxgiDevice))) {
        ComPtr<IDXGIAdapter> ad;
        DXGI_ADAPTER_DESC    d{};
        if (SUCCEEDED(dxgiDevice->GetAdapter(&ad)) && SUCCEEDED(ad->GetDesc(&d))) luid = d.AdapterLuid;
    }
    ComPtr<IDXGIFactory4> f;
    ComPtr<IDXGIAdapter1> a;
    DXGI_ADAPTER_DESC1    desc{};
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&f))) && SUCCEEDED(f->EnumAdapterByLuid(luid, IID_PPV_ARGS(&a)))
        && SUCCEEDED(a->GetDesc1(&desc))) {
        char buf[256]{};
        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, buf, sizeof(buf) - 1, nullptr, nullptr);
        gGpuName = buf;
    }
}

// ---------------------------------------------------------------- input
LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && !(lp & (1 << 30))) {
        if (int(wp) == RfOptions::uiKey.get()) RfOptions::showImGui = !RfOptions::showImGui;
        else if (RfOptions::reloadShadersKey.get() != 0 && int(wp) == RfOptions::reloadShadersKey.get()) RfOptions::reloadShaders = true;
    }
    if (gInitialized && RfOptions::showImGui) {
        std::lock_guard l(gImGuiMutex);
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp);
        auto& io = ImGui::GetIO();
        bool  mouseMsg = msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST;
        bool  keyMsg   = (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) && int(wp) != RfOptions::uiKey.get();
        if ((mouseMsg && io.WantCaptureMouse) || (keyMsg && io.WantCaptureKeyboard)) return 0;
    }
    return CallWindowProcW(gOrigProc, hwnd, msg, wp, lp);
}

void attachWindow(IDXGISwapChain* sc) {
    if (gWindow) return;
    DXGI_SWAP_CHAIN_DESC d{};
    HWND                 hwnd = nullptr;
    ComPtr<IDXGISwapChain1> sc1;
    if (SUCCEEDED(sc->QueryInterface(IID_PPV_ARGS(&sc1)))) sc1->GetHwnd(&hwnd);
    if (!hwnd && SUCCEEDED(sc->GetDesc(&d))) hwnd = d.OutputWindow;
    if (!hwnd) return;
    gWindow   = hwnd;
    gOrigProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&wndProc)));
}

void createContext() {
    ImGui::CreateContext();
    auto& io       = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui_ImplWin32_Init(gWindow);
    initializeImGui();
}

// ---------------------------------------------------------------- Direct3D 12 (upstream ImGuiD3D12, adapted)
namespace d3d12 {
ComPtr<ID3D12Device>         device;
ComPtr<ID3D12DescriptorHeap> rtvHeap, srvHeap;
std::atomic<ID3D12CommandQueue*> queue{nullptr};
struct BackBuffer {
    ComPtr<ID3D12CommandAllocator>    allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Resource>            resource;
    D3D12_CPU_DESCRIPTOR_HANDLE       rtv{};
};
UINT                          count = 0;
std::unique_ptr<BackBuffer[]> buffers;

void createRT(IDXGISwapChain* sc) {
    UINT inc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    auto h   = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < count; ++i) {
        buffers[i].rtv = h;
        if (SUCCEEDED(sc->GetBuffer(i, IID_PPV_ARGS(&buffers[i].resource))))
            device->CreateRenderTargetView(buffers[i].resource.Get(), nullptr, h);
        h.ptr += inc;
    }
}
void releaseRT() {
    rf::postfx::release();
    for (UINT i = 0; i < count; ++i) buffers[i].resource.Reset();
}

ComPtr<ID3D12CommandQueue> queueRef; // the game's DIRECT queue, captured in ExecuteCommandLists (see executeHook)
std::mutex                 queueMutex;

void srvAlloc(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
    *cpu = srvHeap->GetCPUDescriptorHandleForHeapStart(); // ImGui only needs one SRV (the font atlas)
    *gpu = srvHeap->GetGPUDescriptorHandleForHeapStart();
}
void srvFree(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {}

std::string gWhy;
#define RF_FAIL(what, hr) do { gWhy = std::string(what) + " hr=0x" + hex32(uint32_t(hr)); return false; } while (0)
std::string hex32(uint32_t v) { char b[16]; snprintf(b, sizeof b, "%08X", v); return b; }
bool init(IDXGISwapChain* sc) {
    if (HRESULT hr = sc->GetDevice(IID_PPV_ARGS(&device)); FAILED(hr)) RF_FAIL("swapchain->GetDevice(D3D12)", hr);
    {
        std::lock_guard l(queueMutex);
        if (!queueRef) { gWhy = "no D3D12 command queue captured yet"; return false; }
    }
    queue = queueRef.Get();
    DXGI_SWAP_CHAIN_DESC desc{};
    sc->GetDesc(&desc);
    count   = desc.BufferCount;
    buffers = std::make_unique<BackBuffer[]>(count);
    for (UINT i = 0; i < count; ++i) {
        if (HRESULT hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&buffers[i].allocator)); FAILED(hr))
            RF_FAIL("CreateCommandAllocator", hr);
        if (HRESULT hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, buffers[i].allocator.Get(), nullptr,
                                                   IID_PPV_ARGS(&buffers[i].list));
            FAILED(hr))
            RF_FAIL("CreateCommandList", hr);
        if (HRESULT hr = buffers[i].list->Close(); FAILED(hr)) RF_FAIL("CommandList::Close", hr);
    }
    D3D12_DESCRIPTOR_HEAP_DESC srv{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    D3D12_DESCRIPTOR_HEAP_DESC rtv{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, count, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    if (HRESULT hr = device->CreateDescriptorHeap(&srv, IID_PPV_ARGS(&srvHeap)); FAILED(hr)) RF_FAIL("CreateDescriptorHeap(SRV)", hr);
    if (HRESULT hr = device->CreateDescriptorHeap(&rtv, IID_PPV_ARGS(&rtvHeap)); FAILED(hr)) RF_FAIL("CreateDescriptorHeap(RTV)", hr);
    createRT(sc);
    createContext();
    gRendererType = "Direct3D 12";
    ImGui_ImplDX12_InitInfo info{};
    info.Device               = device.Get();
    info.CommandQueue         = queueRef.Get(); // 1.91.x uploads the font atlas through this queue
    info.NumFramesInFlight    = int(count);
    info.RTVFormat            = desc.BufferDesc.Format;
    info.DSVFormat            = DXGI_FORMAT_UNKNOWN;
    info.SrvDescriptorHeap    = srvHeap.Get();
    info.SrvDescriptorAllocFn = &srvAlloc;
    info.SrvDescriptorFreeFn  = &srvFree;
    if (!ImGui_ImplDX12_Init(&info)) { gWhy = "ImGui_ImplDX12_Init returned false"; return false; }
    return true;
}

void render(IDXGISwapChain3* sc) {
    auto* q = queue.load();
    if (!q) return;
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    updateImGui();
    auto& bb = buffers[sc->GetCurrentBackBufferIndex()];
    if (!bb.resource) return;
    bb.allocator->Reset();
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = bb.resource.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    ID3D12DescriptorHeap* heaps[]  = {srvHeap.Get()};
    bb.list->Reset(bb.allocator.Get(), nullptr);
    // full-screen post effects first (they leave the back buffer in RENDER_TARGET), then ImGui on top
    if (!rf::postfx::record(device.Get(), bb.list.Get(), bb.resource.Get(), bb.rtv)) bb.list->ResourceBarrier(1, &barrier);
    bb.list->OMSetRenderTargets(1, &bb.rtv, FALSE, nullptr);
    bb.list->SetDescriptorHeaps(1, heaps);
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), bb.list.Get());
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    bb.list->ResourceBarrier(1, &barrier);
    bb.list->Close();
    ID3D12CommandList* lists[] = {bb.list.Get()};
    q->ExecuteCommandLists(1, lists);
    rf::postfx::submitted(q);
}
} // namespace d3d12

// ---------------------------------------------------------------- Direct3D 11 (upstream ImGuiD3D11, adapted)
namespace d3d11 {
ComPtr<ID3D11Device>           device;
ComPtr<ID3D11DeviceContext>    context;
ComPtr<ID3D11RenderTargetView> rtv;

void createRT(IDXGISwapChain* sc) {
    ComPtr<ID3D11Texture2D> bb;
    if (SUCCEEDED(sc->GetBuffer(0, IID_PPV_ARGS(&bb)))) device->CreateRenderTargetView(bb.Get(), nullptr, &rtv);
}
bool init(IDXGISwapChain* sc) {
    if (HRESULT hr = sc->GetDevice(IID_PPV_ARGS(&device)); FAILED(hr)) { d3d12::gWhy = "D3D11 GetDevice hr=0x" + d3d12::hex32(uint32_t(hr)); return false; }
    device->GetImmediateContext(&context);
    if (!context) { d3d12::gWhy = "no D3D11 immediate context"; return false; }
    createRT(sc);
    createContext();
    gRendererType = "Direct3D 11";
    return ImGui_ImplDX11_Init(device.Get(), context.Get());
}
void render() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    updateImGui();
    ID3D11RenderTargetView* v[] = {rtv.Get()};
    context->OMSetRenderTargets(1, v, nullptr);
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}
} // namespace d3d11

bool gIs12 = false;
int  gInitTries = 0;

void onPresent(IDXGISwapChain* sc) {
    if (gFailed) return;
    std::lock_guard l(gImGuiMutex);
    if (!gInitialized) {
        // D3D12 swapchains return their command queue from GetDevice; D3D11 ones return the device.
        // D3D12 swapchains answer GetDevice(ID3D12Device); D3D11 ones GetDevice(ID3D11Device).
        {
            ComPtr<ID3D12Device> d12;
            gIs12 = SUCCEEDED(sc->GetDevice(IID_PPV_ARGS(&d12)));
        }
        if (gIs12) {
            std::lock_guard l(d3d12::queueMutex);
            if (!d3d12::queueRef) return; // wait until the game has submitted on its direct queue
        }
        attachWindow(sc);
        if (!gWindow) {
            static bool once = false;
            if (!once) { once = true; ll_shim::warn("[RF] overlay: swapchain has no window (composition swapchain?)"); }
            return;
        }
        captureGpuName(sc);
        if (ImGui::GetCurrentContext() == nullptr) d3d12::gWhy.clear();
        bool ok = gIs12 ? d3d12::init(sc) : d3d11::init(sc);
        if (!ok) {
            ++gInitTries;
            std::string why = d3d12::gWhy.empty() ? std::string("unknown") : d3d12::gWhy;
            ll_shim::error("[RF] overlay init failed on " + std::string(gIs12 ? "D3D12" : "D3D11") + ": " + why + " (try "
                           + std::to_string(gInitTries) + "/3)");
            // tear down the partial context so a later frame can try again from scratch
            if (ImGui::GetCurrentContext()) {
                ImGui_ImplWin32_Shutdown();
                ImGui::DestroyContext();
            }
            if (gInitTries >= 3) gFailed = true;
            return;
        }
        gInitialized = true;
        ll_shim::info("[RF] overlay ready on " + gRendererType + " (" + gGpuName + ")");
    }
    ComPtr<IDXGISwapChain3> sc3;
    if (gIs12) {
        if (SUCCEEDED(sc->QueryInterface(IID_PPV_ARGS(&sc3)))) d3d12::render(sc3.Get());
    } else d3d11::render();
}

// A crash inside the overlay must never take the game down: on any SEH fault, turn the overlay off for this run.
std::atomic_bool gFaulted{false};
void safePresent(IDXGISwapChain* sc) {
    __try {
        onPresent(sc);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gFailed  = true;
        gFaulted = true;
    }
}

// ---------------------------------------------------------------- hooks
using Present_t       = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1_t      = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, DXGI_PRESENT_PARAMETERS const*);
using Resize_t        = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using Execute_t       = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
void* gOrigPresent    = nullptr;
void* gOrigPresent1   = nullptr;
void* gOrigResize     = nullptr;
void* gOrigExecute    = nullptr;
void* gTargets[4]{};
void* gDetours[4]{};

std::atomic<long> gPresentCalls{0}, gPresent1Calls{0};
void notePresent(char const* which, std::atomic<long>& n, IDXGISwapChain* sc) {
    long c = ++n;
    if (c == 1 || c == 600) {
        ComPtr<ID3D12Device> q;
        ComPtr<ID3D11Device> d;
        bool is12 = SUCCEEDED(sc->GetDevice(IID_PPV_ARGS(&q)));
        bool is11 = !is12 && SUCCEEDED(sc->GetDevice(IID_PPV_ARGS(&d)));
        ll_shim::info(std::string("[RF] ") + which + " call #" + std::to_string(c) + " swapchain " +
                      (is12 ? "D3D12" : is11 ? "D3D11" : "unknown device") + (gWindow ? "" : ", no window yet"));
    }
}
HRESULT STDMETHODCALLTYPE presentHook(IDXGISwapChain* sc, UINT sync, UINT flags) {
    notePresent("Present", gPresentCalls, sc);
    if (!(flags & DXGI_PRESENT_TEST)) safePresent(sc);
    return reinterpret_cast<Present_t>(gOrigPresent)(sc, sync, flags);
}
HRESULT STDMETHODCALLTYPE present1Hook(IDXGISwapChain1* sc, UINT sync, UINT flags, DXGI_PRESENT_PARAMETERS const* p) {
    notePresent("Present1", gPresent1Calls, sc);
    if (!(flags & DXGI_PRESENT_TEST)) safePresent(sc);
    return reinterpret_cast<Present1_t>(gOrigPresent1)(sc, sync, flags, p);
}
HRESULT STDMETHODCALLTYPE resizeHook(IDXGISwapChain* sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT fl) {
    {
        std::lock_guard l(gImGuiMutex);
        if (gInitialized) {
            if (gIs12) d3d12::releaseRT();
            else d3d11::rtv.Reset();
        }
    }
    HRESULT hr = reinterpret_cast<Resize_t>(gOrigResize)(sc, n, w, h, f, fl);
    std::lock_guard l(gImGuiMutex);
    if (gInitialized) {
        if (gIs12) d3d12::createRT(sc);
        else d3d11::createRT(sc);
    }
    return hr;
}
void STDMETHODCALLTYPE executeHook(ID3D12CommandQueue* q, UINT n, ID3D12CommandList* const* lists) {
    // Keep the game's DIRECT queue: D3D12 swapchains present through it and ImGui must submit on it.
    if (q && !d3d12::queueRef) {
        D3D12_COMMAND_QUEUE_DESC d = q->GetDesc();
        if (d.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
            std::lock_guard l(d3d12::queueMutex);
            if (!d3d12::queueRef) d3d12::queueRef = q;
        }
    }
    reinterpret_cast<Execute_t>(gOrigExecute)(q, n, lists);
}

// Throw-away devices: only used to read the dxgi / d3d12 vtables. D3D12 swapchain first; if that fails (e.g. too early
// in startup), a D3D11 swapchain gives the same dxgi Present/ResizeBuffers code and a bare D3D12 queue gives
// ExecuteCommandLists.
std::string gProbeLog;
void probeNote(char const* what, HRESULT hr) {
    char buf[96];
    snprintf(buf, sizeof(buf), "%s=0x%08lX ", what, static_cast<unsigned long>(hr));
    gProbeLog += buf;
}

bool findTargets() {
    gProbeLog.clear();
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"RendererFoundationProbe";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) { gProbeLog += "CreateWindow failed "; return false; }
    HRESULT hr;
    ComPtr<ID3D12Device>       dev;
    ComPtr<ID3D12CommandQueue> q;
    D3D12_COMMAND_QUEUE_DESC   qd{};
    if (FAILED(hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev)))) probeNote("D3D12CreateDevice", hr);
    else if (FAILED(hr = dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&q)))) probeNote("CreateCommandQueue", hr);
    if (q) gTargets[3] = (*reinterpret_cast<void***>(q.Get()))[10]; // ID3D12CommandQueue::ExecuteCommandLists

    if (q) {
        ComPtr<IDXGIFactory4>   f;
        ComPtr<IDXGISwapChain1> sc;
        DXGI_SWAP_CHAIN_DESC1   sd{};
        sd.Width = 64; sd.Height = 64; sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if (FAILED(hr = CreateDXGIFactory1(IID_PPV_ARGS(&f)))) probeNote("CreateDXGIFactory1", hr);
        else if (FAILED(hr = f->CreateSwapChainForHwnd(q.Get(), hwnd, &sd, nullptr, nullptr, &sc))) probeNote("CreateSwapChainForHwnd(12)", hr);
        else {
            void** v   = *reinterpret_cast<void***>(sc.Get());
            gTargets[0] = v[8]; gTargets[1] = v[22]; gTargets[2] = v[13];
        }
    }
    if (!gTargets[0]) { // D3D11 fallback for the dxgi swapchain functions
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 2; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain>      sc;
        ComPtr<ID3D11Device>        d11;
        ComPtr<ID3D11DeviceContext> ctx;
        if (FAILED(hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &d11, nullptr, &ctx)))
            probeNote("D3D11CreateDeviceAndSwapChain", hr);
        else {
            void** v = *reinterpret_cast<void***>(sc.Get());
            gTargets[0] = v[8]; gTargets[2] = v[13];
            ComPtr<IDXGISwapChain1> sc1;
            if (SUCCEEDED(sc.As(&sc1))) gTargets[1] = (*reinterpret_cast<void***>(sc1.Get()))[22];
        }
    }
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return gTargets[0] != nullptr;
}
} // namespace

bool gHooked = false;

bool install() {
    if (gHooked) return true;
    if (!findTargets()) {
        ll_shim::warn("[RF] overlay: could not probe D3D12/DXGI (" + gProbeLog + "), will retry when the mod is enabled");
        return false;
    }
    if (!gProbeLog.empty()) ll_shim::info("[RF] overlay probe notes: " + gProbeLog);
    gDetours[0]    = reinterpret_cast<void*>(&presentHook);
    gDetours[1]    = reinterpret_cast<void*>(&present1Hook);
    gDetours[2]    = reinterpret_cast<void*>(&resizeHook);
    gDetours[3]    = reinterpret_cast<void*>(&executeHook);
    void** orig[4] = {&gOrigPresent, &gOrigPresent1, &gOrigResize, &gOrigExecute};
    for (int i = 0; i < 4; ++i) {
        if (!gTargets[i]) continue;
        int rc = ll_shim::hook(gTargets[i], gDetours[i], orig[i]);
        if (rc != 0) ll_shim::warn("[RF] overlay hook " + std::to_string(i) + " failed (rc " + std::to_string(rc) + ")");
    }
    gHooked = true;
    ll_shim::info("[RF] overlay hooks installed");
    return true;
}

void uninstall() {
    for (int i = 0; i < 4; ++i)
        if (gTargets[i]) ll_shim::unhook(gTargets[i], gDetours[i]);
    if (gWindow && gOrigProc) SetWindowLongPtrW(gWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(gOrigProc));
}

std::string gpuName() { return gGpuName; }
std::string rendererType() { return gRendererType; }

} // namespace rf::overlay
