// LyricsFlow port - [BF] what the PC is playing, for the bass-hit detector (Neon player pulse, beat glow / shake).
// WASAPI loopback on the default output device (shared mode, no exclusive access, nothing is recorded or stored):
// the mix is folded to mono and fed to BeatDetector (src/lyrics/core/Beat.h). Original code for Bedrock Forge.
// No LeviLamina headers here on purpose (like Smtc_win.cpp).
#ifdef _WIN32
#include "lyrics/win/Win.h"

#include <atomic>
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
    int64_t              retryAt = 0, checkAt = 0;
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
            if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) { teardown(); continue; }
            if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client)))) { teardown(); continue; }
            if (FAILED(client->GetMixFormat(&fmt)) ||
                FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000, 0, fmt, nullptr)) ||
                FAILED(client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&cap))) || FAILED(client->Start())) {
                teardown();
                continue;
            }
            { std::lock_guard l(L().mutex); L().det.setRate(float(fmt->nSamplesPerSec)); L().bands.setRate(float(fmt->nSamplesPerSec)); }
            checkAt = t + 4000;
        }
        // the default output can change (headphones plugged in): reopen if it did
        if (t > checkAt) {
            checkAt = t + 4000;
            IMMDevice* now = nullptr;
            if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &now))) {
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
                L().lastAudioMs = t;
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
