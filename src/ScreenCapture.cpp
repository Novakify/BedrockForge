// Renderer Foundation - LyricsForge video screen: window / monitor capture. See ScreenCapture.h. Original code.
// Windows.Graphics.Capture keeps capturing a window that sits behind the game (not a minimised one), so a video can play
// in a browser / player window under Minecraft and show up on the screen in the world.
#include "ScreenCapture.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unknwn.h> // classic COM interop in C++/WinRT (.as<IDirect3DDxgiInterfaceAccess>)
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwmapi.lib")

namespace rf::screen::cap {
namespace {
using Microsoft::WRL::ComPtr;
namespace wgc = winrt::Windows::Graphics::Capture;
namespace wdx = winrt::Windows::Graphics::DirectX;
using Clock = std::chrono::steady_clock;

std::mutex              gMutex; // guards gWant, gStatus, gLatest
std::condition_variable gWake;
Want                    gWant;
Status                  gStatus;
Frame                   gLatest;
std::atomic_bool        gStop{false};
std::once_flag          gStarted;

void setStatus(State s, std::string msg, uint32_t w = 0, uint32_t h = 0, float fps = -1) {
    std::lock_guard l(gMutex);
    gStatus.state   = s;
    gStatus.message = std::move(msg);
    if (w) { gStatus.w = w; gStatus.h = h; }
    if (s != State::Live) gStatus.fps = 0;
    else if (fps >= 0) gStatus.fps = fps;
}

HMONITOR monitorAt(int index) {
    struct Ctx { int want, at; HMONITOR found; } c{index, 0, nullptr};
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR m, HDC, LPRECT, LPARAM p) -> BOOL {
        auto& c = *reinterpret_cast<Ctx*>(p);
        if (c.at++ == c.want) { c.found = m; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&c));
    return c.found;
}

/// One running capture (lives on the worker thread; frames arrive on a thread-pool thread).
struct Session {
    ComPtr<ID3D11Device>          dev;
    ComPtr<ID3D11DeviceContext>   ctx;
    wdx::Direct3D11::IDirect3DDevice rtDev{nullptr};
    wgc::GraphicsCaptureItem      item{nullptr};
    wgc::Direct3D11CaptureFramePool pool{nullptr};
    wgc::GraphicsCaptureSession   session{nullptr};
    winrt::event_token            arrivedTok{}, closedTok{};
    winrt::Windows::Graphics::SizeInt32 size{};
    std::mutex                    frameMutex; // FrameArrived can overlap itself on the free-threaded pool
    ComPtr<ID3D11Texture2D>       staging;
    UINT                          sw = 0, sh = 0;
    Frame                         work;
    HWND                          hwnd = nullptr;
    bool                          crop = true;
    double                        minGap = 1.0 / 60.0;
    Clock::time_point             lastTake{};
    // fps over the last second
    Clock::time_point             fpsAt = Clock::now();
    int                           fpsCount = 0;
    float                         fps = 0;
    std::atomic_bool              closed{false};
    std::atomic<int64_t>          lastFrameMs{0};

    ~Session() { stop(); }

    void stop() {
        try {
            if (pool && arrivedTok.value) pool.FrameArrived(arrivedTok);
            if (item && closedTok.value) item.Closed(closedTok);
            if (session) session.Close();
            if (pool) pool.Close();
        } catch (...) {}
        arrivedTok = {}; closedTok = {};
        std::lock_guard l(frameMutex); // a frame still being read back finishes first
        session = nullptr; pool = nullptr; item = nullptr;
    }

    bool start(Want const& w, std::string& why) {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL fl[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, fl, 2, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
        if (FAILED(hr)) { why = "no D3D11 device for capture"; return false; }
        try {
            ComPtr<IDXGIDevice> dxgi;
            dev.As(&dxgi);
            winrt::com_ptr<::IInspectable> insp;
            winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), insp.put()));
            rtDev = insp.as<wdx::Direct3D11::IDirect3DDevice>();
            auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
            if (w.hwnd) {
                hwnd = reinterpret_cast<HWND>(w.hwnd);
                if (!IsWindow(hwnd)) { why = "that window is gone - pick it again"; return false; }
                winrt::check_hresult(interop->CreateForWindow(hwnd, winrt::guid_of<wgc::GraphicsCaptureItem>(), winrt::put_abi(item)));
            } else {
                HMONITOR m = monitorAt(w.monitor);
                if (!m) { why = "no such monitor"; return false; }
                winrt::check_hresult(interop->CreateForMonitor(m, winrt::guid_of<wgc::GraphicsCaptureItem>(), winrt::put_abi(item)));
            }
            crop   = w.crop && hwnd;
            minGap = 1.0 / double(std::clamp<uint32_t>(w.maxFps ? w.maxFps : 60, 10, 144));
            size   = item.Size();
            pool   = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(rtDev, wdx::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
            session = pool.CreateCaptureSession(item);
            try { session.IsCursorCaptureEnabled(false); } catch (...) {}
            try { session.IsBorderRequired(false); } catch (...) {} // Windows 11: no yellow frame around the window
            closedTok  = item.Closed([this](auto&&, auto&&) { closed = true; });
            arrivedTok = pool.FrameArrived([this](wgc::Direct3D11CaptureFramePool const& p, auto&&) { onFrame(p); });
            session.StartCapture();
        } catch (winrt::hresult_error const& e) {
            char b[16];
            std::snprintf(b, sizeof b, "%08X", uint32_t(e.code()));
            why = std::string("capture refused (0x") + b + ")";
            return false;
        } catch (...) {
            why = "capture refused";
            return false;
        }
        return true;
    }

    /// The client area inside the captured frame (frame = the window's DWM bounds), in frame pixels.
    D3D11_BOX region(UINT fw, UINT fh) const {
        D3D11_BOX b{0, 0, 0, fw, fh, 1};
        if (!crop || !hwnd) return b;
        RECT fr{}, cr{};
        POINT tl{0, 0};
        if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &fr, sizeof fr)) || !GetClientRect(hwnd, &cr) ||
            !ClientToScreen(hwnd, &tl))
            return b;
        double const fwPx = double(std::max<LONG>(1, fr.right - fr.left));
        double const k    = double(fw) / fwPx; // DPI virtualisation: scale window coords to captured pixels
        long x = std::lround((tl.x - fr.left) * k), y = std::lround((tl.y - fr.top) * k);
        long w = std::lround(cr.right * k), h = std::lround(cr.bottom * k);
        x = std::clamp<long>(x, 0, long(fw) - 1); y = std::clamp<long>(y, 0, long(fh) - 1);
        w = std::clamp<long>(w, 1, long(fw) - x); h = std::clamp<long>(h, 1, long(fh) - y);
        if (w < 16 || h < 16) return b;
        return D3D11_BOX{UINT(x), UINT(y), 0, UINT(x + w), UINT(y + h), 1};
    }

    void onFrame(wgc::Direct3D11CaptureFramePool const& p) {
        std::lock_guard l(frameMutex);
        try {
            auto frame = p.TryGetNextFrame();
            if (!frame) return;
            auto const cs = frame.ContentSize();
            if (cs.Width != size.Width || cs.Height != size.Height) { // the window was resized
                size = cs;
                p.Recreate(rtDev, wdx::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, cs);
            }
            auto const now = Clock::now();
            lastFrameMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            if (std::chrono::duration<double>(now - lastTake).count() < minGap * 0.92) return; // rate cap
            lastTake = now;
            auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
            ComPtr<ID3D11Texture2D> tex;
            if (FAILED(access->GetInterface(IID_PPV_ARGS(&tex)))) return;
            D3D11_TEXTURE2D_DESC td{};
            tex->GetDesc(&td);
            UINT const fw = std::min<UINT>(td.Width, UINT(std::max(1, cs.Width)));
            UINT const fh = std::min<UINT>(td.Height, UINT(std::max(1, cs.Height)));
            D3D11_BOX const box = region(fw, fh);
            UINT const w = box.right - box.left, h = box.bottom - box.top;
            if (!staging || sw != w || sh != h) {
                staging.Reset();
                D3D11_TEXTURE2D_DESC sd{};
                sd.Width = w; sd.Height = h; sd.MipLevels = 1; sd.ArraySize = 1; sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                sd.SampleDesc.Count = 1; sd.Usage = D3D11_USAGE_STAGING; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                if (FAILED(dev->CreateTexture2D(&sd, nullptr, &staging))) return;
                sw = w; sh = h;
            }
            ctx->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, tex.Get(), 0, &box);
            D3D11_MAPPED_SUBRESOURCE m{};
            if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return;
            work.w = w; work.h = h;
            work.bgra.resize(size_t(w) * h * 4);
            for (UINT y = 0; y < h; ++y)
                std::memcpy(work.bgra.data() + size_t(y) * w * 4, static_cast<uint8_t const*>(m.pData) + size_t(y) * m.RowPitch, size_t(w) * 4);
            ctx->Unmap(staging.Get(), 0);
            ++fpsCount;
            double const span = std::chrono::duration<double>(now - fpsAt).count();
            if (span >= 1.0) { fps = float(fpsCount / span); fpsCount = 0; fpsAt = now; }
            {
                std::lock_guard g(gMutex);
                work.seq = gLatest.seq + 1;
                std::swap(work, gLatest);
                gStatus.state = State::Live;
                gStatus.w = w; gStatus.h = h; gStatus.fps = fps;
                gStatus.message = "live";
            }
        } catch (...) {}
    }
};

void worker() {
    try { winrt::init_apartment(winrt::apartment_type::multi_threaded); } catch (...) {}
    std::unique_ptr<Session> s;
    Want running;
    int64_t retryAt = 0;
    while (!gStop) {
        Want w;
        {
            std::unique_lock l(gMutex);
            gWake.wait_for(l, std::chrono::milliseconds(250));
            w = gWant;
        }
        if (gStop) break;
        int64_t const nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
        bool const target = w.on && (w.hwnd || w.monitor >= 0);
        if (s && (!target || !(w == running) || s->closed)) { // switch off / switch target / window closed
            bool const wasClosed = s->closed;
            s.reset();
            if (wasClosed && w == running) { setStatus(State::Error, "that window was closed - pick another"); retryAt = nowMs + 3000; }
        }
        if (!target) {
            if (!w.on) setStatus(State::Idle, "off");
            else setStatus(State::Idle, "pick a window");
            running = w;
            continue;
        }
        if (!s) {
            if (w == running && nowMs < retryAt) continue; // failed a moment ago: wait before trying again
            running = w;
            setStatus(State::Starting, "starting capture...");
            auto ns = std::make_unique<Session>();
            std::string why;
            if (ns->start(w, why)) s = std::move(ns);
            else { setStatus(State::Error, why); retryAt = nowMs + 3000; }
            continue;
        }
        // running: say why nothing arrives (a minimised window delivers no frames)
        if (s->hwnd && IsIconic(s->hwnd)) setStatus(State::Starting, "window is minimised - restore it (it can sit behind the game)");
        else if (nowMs - s->lastFrameMs.load() > 2000) {
            std::lock_guard l(gMutex);
            if (gStatus.state == State::Live) { gStatus.fps = 0; gStatus.message = "live (picture not changing)"; }
        }
    }
    s.reset();
}
} // namespace

void want(Want const& w) {
    std::call_once(gStarted, [] { std::thread(worker).detach(); }); // lives as long as the game
    bool changed;
    {
        std::lock_guard l(gMutex);
        changed = !(gWant == w);
        gWant   = w;
    }
    if (changed) gWake.notify_all();
}

bool latest(uint64_t haveSeq, Frame& out) {
    std::lock_guard l(gMutex);
    if (gLatest.seq <= haveSeq || gLatest.bgra.empty()) return false;
    uint64_t const seq = gLatest.seq;
    std::swap(out, gLatest);
    gLatest.seq = seq; // keep counting from here (gLatest now holds an older buffer to reuse)
    gLatest.bgra.clear();
    return true;
}

Status status() {
    std::lock_guard l(gMutex);
    return gStatus;
}

void shutdown() {
    gStop = true;
    gWake.notify_all();
}

} // namespace rf::screen::cap
