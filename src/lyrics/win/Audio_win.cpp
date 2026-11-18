// LyricsFlow port - [BF] what the PC is playing, for the bass-hit detector (Neon player pulse, beat glow / shake).
// WASAPI loopback on the default output device (shared mode, no exclusive access, nothing is recorded or stored):
// the mix is folded to mono and fed to BeatDetector (src/lyrics/core/Beat.h). Original code for Bedrock Forge.
// No LeviLamina headers here on purpose (like Smtc_win.cpp).
#ifdef _WIN32
#include "lyrics/win/Win.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propsys.h>
#include <string>

#include "lyrics/core/Bands.h"
#include "lyrics/core/Beat.h"

namespace bf::lyrics::win {
namespace {
template <class T> void release(T*& p) { if (p) { p->Release(); p = nullptr; } }

struct Loopback {
    std::mutex        mutex;
    BeatDetector      det;
    BandAnalyzer      bands; // [BF] Song Memory / music state (same mutex as det)
    std::atomic_bool  running{false}, wanted{false}, bassW{false}, memW{false};
    std::atomic<int64_t> lastAudioMs{0};
    std::atomic<float>   balance{0};
    std::string          device;      // [LyricsForge] the output being listened to (friendly name, under mutex)  // [BF] stereo drift: -1 left .. +1 right (smoothed, ~0.3 s)
    std::thread       thread;
};
Loopback& L() { static Loopback* l = new Loopback(); return *l; } // never freed (thread may outlive statics)

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool isFloat(WAVEFORMATEX const* f) {
    if (f->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
    if (f->wFormatTag == WAVE_FORMAT_EXTENSIBLE && f->cbSize >= 22) {
        static GUID const kFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
        return std::memcmp(&reinterpret_cast<WAVEFORMATEXTENSIBLE const*>(f)->SubFormat, &kFloat, sizeof(GUID)) == 0;
    }
    return false;
}

void loop() {
    HRESULT const co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* en = nullptr;
    IMMDevice*           dev = nullptr;
    IAudioClient*        client = nullptr;
    IAudioCaptureClient* cap = nullptr;
    WAVEFORMATEX*        fmt = nullptr;
    std::vector<float>   mono;
    int64_t              retryAt = 0, checkAt = 0, openedAt = 0, heardAt = 0;
    int                  pick = 0;
    auto teardown = [&] {
        if (client) client->Stop();
        release(cap); release(client); release(dev);
        if (fmt) { CoTaskMemFree(fmt); fmt = nullptr; }
    };
    while (L().running) {
        if (!L().wanted) { teardown(); std::this_thread::sleep_for(std::chrono::milliseconds(250)); continue; }
        int64_t const t = nowMs();
        if (!client) {
            if (t < retryAt) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }
            retryAt = t + 3000;
            if (!en && FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                               reinterpret_cast<void**>(&en)))) continue;
            // [LyricsForge] pick 0 = the default output; 1.. = the other active outputs in turn. Apps can play to a
            // device that isn't the default (Windows per-app output, a headset's virtual device), and we heard nothing.
            if (pick > 0) {
                IMMDeviceCollection* all = nullptr;
                UINT n = 0;
                LPWSTR defId = nullptr;
                IMMDevice* def = nullptr;
                if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &def))) { def->GetId(&defId); def->Release(); }
                if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &all)) && SUCCEEDED(all->GetCount(&n))) {
                    std::vector<IMMDevice*> others;
                    for (UINT i = 0; i < n; ++i) {
                        IMMDevice* d = nullptr;
                        if (FAILED(all->Item(i, &d))) continue;
                        LPWSTR id = nullptr;
                        bool const isDef = SUCCEEDED(d->GetId(&id)) && defId && wcscmp(id, defId) == 0;
                        if (id) CoTaskMemFree(id);
                        if (isDef) d->Release(); else others.push_back(d);
                    }
                    if (pick > int(others.size())) pick = 0;
                    for (size_t i = 0; i < others.size(); ++i) { if (int(i) + 1 == pick) dev = others[i]; else others[i]->Release(); }
                } else pick = 0;
                release(all);
                if (defId) CoTaskMemFree(defId);
            }
            if (!dev && FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) { teardown(); continue; }
            {
                IPropertyStore* ps = nullptr;
                std::string name = pick ? "other output" : "default output";
                if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &ps))) {
                    PROPVARIANT v; PropVariantInit(&v);
                    if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR && v.pwszVal) {
                        int const len = WideCharToMultiByte(CP_UTF8, 0, v.pwszVal, -1, nullptr, 0, nullptr, nullptr);
                        std::string u(size_t(std::max(0, len - 1)), '\0');
                        if (len > 1) WideCharToMultiByte(CP_UTF8, 0, v.pwszVal, -1, u.data(), len, nullptr, nullptr);
                        name = (pick ? "" : "default: ") + u;
                    }
                    PropVariantClear(&v);
                    ps->Release();
                }
                std::lock_guard l(L().mutex);
                L().device = name;
            }
            if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client)))) { teardown(); continue; }
            if (FAILED(client->GetMixFormat(&fmt)) ||
                FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000, 0, fmt, nullptr)) ||
                FAILED(client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&cap))) || FAILED(client->Start())) {
                teardown();
                continue;
            }
            { std::lock_guard l(L().mutex); L().det.setRate(float(fmt->nSamplesPerSec)); L().bands.setRate(float(fmt->nSamplesPerSec)); }
            checkAt = t + 4000;
            openedAt = t;
        }
        // the default output can change (headphones plugged in): reopen if it did
        if (t > checkAt) {
            checkAt = t + 4000;
            // [LyricsForge] nothing audible here for 3 s while we're wanted: try the next output (round robin, default first)
            if (t - openedAt > 3000 && t - heardAt > 3000) { ++pick; teardown(); retryAt = 0; continue; }
            IMMDevice* now = nullptr;
            if (pick == 0 && SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &now))) {
                LPWSTR a = nullptr, b = nullptr;
                bool   same = SUCCEEDED(dev->GetId(&a)) && SUCCEEDED(now->GetId(&b)) && wcscmp(a, b) == 0;
                if (a) CoTaskMemFree(a);
                if (b) CoTaskMemFree(b);
                now->Release();
                if (!same) { teardown(); retryAt = 0; continue; }
            }
        }
        UINT32  packet = 0;
        HRESULT hr     = cap->GetNextPacketSize(&packet);
        while (SUCCEEDED(hr) && packet > 0) {
            BYTE*  data   = nullptr;
            UINT32 frames = 0;
            DWORD  flags  = 0;
            if (FAILED(hr = cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
            int const ch = std::max<int>(1, fmt->nChannels);
            mono.assign(frames, 0.0f);
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data) {
                if (isFloat(fmt) && fmt->wBitsPerSample == 32) {
                    auto const* s = reinterpret_cast<float const*>(data);
                    for (UINT32 i = 0; i < frames; ++i) {
                        float m = 0;
                        for (int c = 0; c < ch; ++c) m += s[size_t(i) * ch + c];
                        mono[i] = m / float(ch);
                    }
                } else if (fmt->wBitsPerSample == 16) {
                    auto const* s = reinterpret_cast<int16_t const*>(data);
                    for (UINT32 i = 0; i < frames; ++i) {
                        float m = 0;
                        for (int c = 0; c < ch; ++c) m += float(s[size_t(i) * ch + c]) / 32768.0f;
                        mono[i] = m / float(ch);
                    }
                } else if (fmt->wBitsPerSample == 32) {
                    auto const* s = reinterpret_cast<int32_t const*>(data);
                    for (UINT32 i = 0; i < frames; ++i) {
                        float m = 0;
                        for (int c = 0; c < ch; ++c) m += float(double(s[size_t(i) * ch + c]) / 2147483648.0);
                        mono[i] = m / float(ch);
                    }
                }
                { // [LyricsForge] audible, not just a packet of zeros (some apps / devices send digital silence)
                    float pk = 0;
                    for (float m : mono) pk = std::max(pk, std::fabs(m));
                    if (pk > 1e-4f) { L().lastAudioMs = t; heardAt = t; }
                }
                if (ch >= 2 && frames > 0 && (fmt->wBitsPerSample == 16 || (isFloat(fmt) && fmt->wBitsPerSample == 32))) {
                    double sl = 0, sr = 0; // [BF] stereo drift: energy of the first two channels
                    for (UINT32 i = 0; i < frames; ++i) {
                        float l, r;
                        if (fmt->wBitsPerSample == 16) {
                            auto const* s = reinterpret_cast<int16_t const*>(data);
                            l = float(s[size_t(i) * ch]) / 32768.0f; r = float(s[size_t(i) * ch + 1]) / 32768.0f;
                        } else {
                            auto const* s = reinterpret_cast<float const*>(data);
                            l = s[size_t(i) * ch]; r = s[size_t(i) * ch + 1];
                        }
                        sl += double(l) * l; sr += double(r) * r;
                    }
                    double const el = std::sqrt(sl / frames), er = std::sqrt(sr / frames);
                    float const  b  = el + er > 1e-4 ? float((er - el) / (er + el)) : 0.0f;
                    float const  k  = std::min(1.0f, float(frames) / float(std::max<DWORD>(1, fmt->nSamplesPerSec)) / 0.3f);
                    L().balance.store(L().balance.load() + (b - L().balance.load()) * k);
                }
            }
            cap->ReleaseBuffer(frames);
            {
                int64_t const ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                std::lock_guard l(L().mutex);
                L().det.feed(mono.data(), mono.size());
                L().bands.feed(mono.data(), mono.size(), ns);
            }
            hr = cap->GetNextPacketSize(&packet);
        }
        if (FAILED(hr)) { teardown(); retryAt = t + 500; continue; } // device gone / invalidated: reopen
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    teardown();
    release(en);
    if (SUCCEEDED(co)) CoUninitialize();
}
} // namespace

void bassStart() {
    if (L().running.exchange(true)) return;
    L().thread = std::thread(loop);
}
void bassStop() {
    if (!L().running.exchange(false)) return;
    if (L().thread.joinable()) L().thread.join();
}
void bassWanted(bool on) { L().bassW = on; L().wanted = on || L().memW.load(); }
void memoryWanted(bool on) { L().memW = on; L().wanted = on || L().bassW.load(); }
void audioFrames(std::vector<BandFrame>& out) {
    std::lock_guard l(L().mutex);
    L().bands.drain(out);
}
float audioBalance() { return nowMs() - L().lastAudioMs.load() < 1500 ? L().balance.load() : 0.0f; }
std::string audioDevice() { std::lock_guard l(L().mutex); return L().device; }
BandFrame audioLatest() {
    std::lock_guard l(L().mutex);
    return L().bands.latest();
}
float bassLevel(float dt, bool* live) {
    if (live) *live = L().wanted && nowMs() - L().lastAudioMs.load() < 1500;
    std::lock_guard l(L().mutex);
    return L().det.level(dt);
}

} // namespace bf::lyrics::win
#endif
