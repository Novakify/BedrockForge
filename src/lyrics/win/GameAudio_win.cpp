// [BF] Sky reacts to Minecraft itself: what the GAME is playing (explosions, hits, rain, its music), not the whole PC.
// WASAPI process loopback (Windows 10 2004+): only this process's own sound - Spotify / Discord never move the sky.
// Shared mode, nothing is recorded or stored: each 10 ms block becomes a loudness number and a "boom" detector.
// Original code for Bedrock Forge. No LeviLamina headers here (like Audio_win.cpp).
#ifdef _WIN32
#include "lyrics/win/GameAudio.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>

#include <windows.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <wrl/implements.h>

namespace bf::lyrics {
namespace {
using Microsoft::WRL::ComPtr;

struct State {
    std::atomic<bool>  want{false}, running{false};
    std::atomic<float> level{0}, low{0}, boom{0};
    std::atomic<float> peakLv{0}, peakLo{0};   // loudest raw block since the log last read them (for tuning)
    std::atomic<int>   booms{0};
    std::atomic<int>   status{0};   // 0 off, 1 listening, -1 not supported here
    std::thread        th;
    std::mutex         m;
};
State& S() { static State* s = new State(); return *s; }   // never freed (the thread may outlive statics)

class Done : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, Microsoft::WRL::FtmBase,
                                                 IActivateAudioInterfaceCompletionHandler> {
public:
    HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ~Done() { if (ev) CloseHandle(ev); }
    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation*) override { SetEvent(ev); return S_OK; }
};

ComPtr<IAudioClient> activate() {
    AUDIOCLIENT_ACTIVATION_PARAMS ap{};
    ap.ActivationType                                = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    ap.ProcessLoopbackParams.TargetProcessId         = GetCurrentProcessId();
    ap.ProcessLoopbackParams.ProcessLoopbackMode     = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    PROPVARIANT pv{};
    pv.vt             = VT_BLOB;
    pv.blob.cbSize    = sizeof ap;
    pv.blob.pBlobData = reinterpret_cast<BYTE*>(&ap);
    auto done = Microsoft::WRL::Make<Done>();
    ComPtr<IActivateAudioInterfaceAsyncOperation> op;
    if (FAILED(ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &pv, done.Get(), &op))) return nullptr;
    if (WaitForSingleObject(done->ev, 3000) != WAIT_OBJECT_0) return nullptr;
    HRESULT              hr = E_FAIL;
    ComPtr<IUnknown>     unk;
    if (FAILED(op->GetActivateResult(&hr, &unk)) || FAILED(hr) || !unk) return nullptr;
    ComPtr<IAudioClient> c;
    unk.As(&c);
    return c;
}

void loop() {
    auto& s = S();
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> cap;
    auto teardown = [&] { if (client) client->Stop(); cap.Reset(); client.Reset(); };
    float avg = 0, avgLo = 0, lowState = 0;   // slow loudness (the room tone) and a one-pole low pass (rumble)
    int   fails = 0;
    while (s.running.load()) {
        if (!s.want.load()) {
            teardown();
            s.status = 0;
            s.level = 0, s.low = 0, s.boom = 0;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }
        if (!client) {
            client = activate();
            WAVEFORMATEX f{};
            f.wFormatTag = WAVE_FORMAT_IEEE_FLOAT, f.nChannels = 2, f.nSamplesPerSec = 48000, f.wBitsPerSample = 32;
            f.nBlockAlign = 8, f.nAvgBytesPerSec = 48000 * 8;
            if (!client ||
                FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM, 2000000, 0, &f, nullptr)) ||
                FAILED(client->GetService(IID_PPV_ARGS(&cap))) || FAILED(client->Start())) {
                teardown();
                if (++fails >= 3) { s.status = -1; std::this_thread::sleep_for(std::chrono::seconds(30)); fails = 0; }   // older Windows: no process loopback
                else std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            fails = 0;
            s.status = 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        UINT32 n = 0;
        double sum = 0, lowSum = 0;
        long   cnt = 0;
        while (SUCCEEDED(cap->GetNextPacketSize(&n)) && n) {
            BYTE*  data = nullptr;
            UINT32 frames = 0;
            DWORD  flags = 0;
            if (FAILED(cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) { teardown(); break; }
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data) {
                auto const* p = reinterpret_cast<float const*>(data);
                for (UINT32 i = 0; i < frames; ++i) {
                    float const mono = 0.5f * (p[2 * i] + p[2 * i + 1]);
                    lowState += (mono - lowState) * 0.013f;   // ~100 Hz at 48 kHz: explosions, thunder, the bass of its music
                    sum += double(mono) * mono, lowSum += double(lowState) * lowState;
                }
            }
            cnt += long(frames);
            cap->ReleaseBuffer(frames);
        }
        if (!client) continue;
        float const rms  = cnt ? float(std::sqrt(sum / double(cnt))) : 0.0f;
        float const lrms = cnt ? float(std::sqrt(lowSum / double(cnt))) : 0.0f;
        float const lv   = std::clamp(rms * 3.0f, 0.0f, 1.0f);
        float const lo   = std::clamp(lrms * 5.0f, 0.0f, 1.0f);
        if (!cnt) continue;                                               // no new audio this 10 ms: keep the state as is
        avg   += (lv - avg) * 0.01f;                                      // ~1 s room tone
        avgLo += (lo - avgLo) * 0.01f;                                    // ...and its rumble
        s.level = s.level.load() + (lv - s.level.load()) * 0.25f;
        s.low   = s.low.load() + (lo - s.low.load()) * 0.3f;
        if (lv > s.peakLv.load()) s.peakLv = lv;
        if (lo > s.peakLo.load()) s.peakLo = lo;
        // a boom: a sudden low hit well above the room tone (TNT, a creeper, thunder, an end crystal) - not footsteps.
        // Relative to the room tone (game volume and distance change how loud a boom is in the capture), with a small floor.
        float b = s.boom.load() * 0.94f;
        if (lo > 0.06f && lo > avgLo * 4.0f + 0.03f && lv > avg * 2.0f + 0.03f && b < 0.3f) {
            b = std::clamp(lo * 3.0f, 0.3f, 1.0f);
            ++s.booms;
        }
        s.boom = b;
    }
    teardown();
    CoUninitialize();
}
} // namespace

void gameAudioWant(bool on) {
    auto& s = S();
    s.want = on;
    if (on && !s.running.exchange(true)) {
        std::lock_guard l(s.m);
        s.th = std::thread(loop);
        s.th.detach();
    }
}
GameAudio gameAudio() {
    auto& s = S();
    return {s.level.load(), s.low.load(), s.boom.load(), s.booms.load(), s.status.load()};
}
void gameAudioPeaks(float& lv, float& lo) {   // loudest raw block since the last call (the Director log's tuning line)
    auto& s = S();
    lv = s.peakLv.exchange(0), lo = s.peakLo.exchange(0);
}
} // namespace bf::lyrics
#endif
