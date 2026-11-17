// Bedrock Forge client - Weather sounds (for the Renderer Foundation Weather module). Original code: synthesised at
// first use (no sound files), played through XAudio2.
//   rain: a 6 s loop of soft filtered noise with scattered drip ticks, faded in / out with the module
//   thunder: a crack + a long low rumble, played a moment after each lightning strike (sound travels slower than light)
#include <windows.h>
#include <xaudio2.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <random>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>

namespace bedrock_forge::client::weathersounds {
namespace {
constexpr int kRate = 44100;
std::mutex              gMu;
IXAudio2*               gXa     = nullptr;
IXAudio2MasteringVoice* gMaster = nullptr;
bool                    gTried  = false;
std::vector<short>      gRain, gThunder[3];
IXAudio2SourceVoice*    gRainV  = nullptr;
float                   gRainVol = 0.f;
std::vector<IXAudio2SourceVoice*> gOneShots;
std::atomic<int64_t>    gLastCall{0};
int64_t nowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::vector<short> synthRain() {
    std::mt19937 rng(4242);
    std::uniform_real_distribution<float> un(-1.f, 1.f), u01(0.f, 1.f);
    int const n = kRate * 6;
    std::vector<float> f(size_t(n), 0.f);
    float b0 = 0, b1 = 0, b2 = 0, hp = 0, prev = 0;
    for (int i = 0; i < n; ++i) {                      // pinkish noise, the low end taken off: a steady hiss of rain
        float const w = un(rng);
        b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f;
        float const p = (b0 + b1 + b2 + w * 0.1848f) * 0.18f;
        hp = 0.985f * (hp + p - prev); prev = p;
        f[size_t(i)] = hp * 0.55f;
    }
    for (int d = 0; d < 220; ++d) {                    // drips: tiny pitched ticks here and there
        int const at = int(u01(rng) * float(n - 4000));
        float const fr = 1800.f + 2600.f * u01(rng), a = 0.10f + 0.22f * u01(rng);
        for (int i = 0; i < 3000; ++i) { float const t = float(i) / kRate; f[size_t(at + i)] += a * std::sin(6.2831853f * fr * t) * std::exp(-t * 160.f); }
    }
    int const fade = kRate / 4;                        // a seamless loop: cross-fade the end into the start
    for (int i = 0; i < fade; ++i) { float const k = float(i) / fade; f[size_t(i)] = f[size_t(i)] * k + f[size_t(n - fade + i)] * (1.f - k); }
    std::vector<short> out(size_t(n - fade));
    for (size_t i = 0; i < out.size(); ++i) out[i] = short(std::clamp(f[i], -1.f, 1.f) * 30000.f);
    return out;
}
std::vector<short> synthThunder(uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> un(-1.f, 1.f), u01(0.f, 1.f);
    int const n = int(kRate * (4.5f + 2.f * u01(rng)));
    std::vector<short> out(static_cast<size_t>(n));
    float lp = 0, lp2 = 0, lp3 = 0, crk = 0;
    float const crack = 0.25f + 0.5f * u01(rng);
    for (int i = 0; i < n; ++i) {
        float const t = float(i) / kRate;
        float const w = un(rng);
        crk += 0.6f * (w - crk);
        lp += 0.035f * (w - lp); lp2 += 0.035f * (lp - lp2); lp3 += 0.02f * (lp2 - lp3);
        float const env = std::min(1.f, t * 12.f) * std::exp(-t * 0.75f) * (0.75f + 0.25f * std::sin(t * 5.3f + 1.7f * std::sin(t * 1.3f)));
        float const v = crack * crk * std::exp(-t * 9.f) + lp3 * 9.f * env + lp2 * 2.5f * env * std::exp(-t * 1.6f);
        out[size_t(i)] = short(std::clamp(v * 0.9f, -1.f, 1.f) * 31000.f);
    }
    return out;
}
bool init() {
    if (gTried) return gXa != nullptr;
    gTried = true;
    (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HMODULE m = LoadLibraryW(L"xaudio2_9.dll");
    if (!m) return false;
    using Create = HRESULT(WINAPI*)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR);
    auto create = reinterpret_cast<Create>(GetProcAddress(m, "XAudio2Create"));
    if (!create || FAILED(create(&gXa, 0, XAUDIO2_DEFAULT_PROCESSOR))) { gXa = nullptr; return false; }
    if (FAILED(gXa->CreateMasteringVoice(&gMaster))) { gXa->Release(); gXa = nullptr; return false; }
    gRain = synthRain();
    for (int k = 0; k < 3; ++k) gThunder[k] = synthThunder(77u + uint32_t(k) * 31u);
    return true;
}
WAVEFORMATEX fmt() {
    WAVEFORMATEX wf{};
    wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 1; wf.nSamplesPerSec = kRate; wf.wBitsPerSample = 16;
    wf.nBlockAlign = 2; wf.nAvgBytesPerSec = kRate * 2;
    return wf;
}
} // namespace

void rainLocked(float level);
/// Every atmos tick (in-world HUD frames): the rain loop's target level (0 = off), 0..1. Fades toward it. Leaving the
/// world stops the ticks; a watchdog then fades the loop out so it never keeps playing on the menus.
void rain(float level) {
    gLastCall = nowMs();
    static std::once_flag once;
    if (level > 0.f) std::call_once(once, [] {
        std::thread([] {
            for (;;) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (nowMs() - gLastCall.load() > 600) { std::lock_guard l(gMu); if (gRainV) rainLocked(0.f); }
            }
        }).detach();
    });
    std::lock_guard l(gMu);
    rainLocked(level);
}
void rainLocked(float level) {
    if (level <= 0.f && !gRainV) return;
    if (!init()) return;
    if (!gRainV && level > 0.f) {
        WAVEFORMATEX wf = fmt();
        if (FAILED(gXa->CreateSourceVoice(&gRainV, &wf))) { gRainV = nullptr; return; }
        XAUDIO2_BUFFER b{};
        b.AudioBytes = UINT32(gRain.size() * sizeof(short)); b.pAudioData = reinterpret_cast<BYTE const*>(gRain.data());
        b.LoopCount = XAUDIO2_LOOP_INFINITE; b.Flags = XAUDIO2_END_OF_STREAM;
        if (FAILED(gRainV->SubmitSourceBuffer(&b))) { gRainV->DestroyVoice(); gRainV = nullptr; return; }
        gRainVol = 0.f; gRainV->SetVolume(0.f); gRainV->Start();
    }
    gRainVol += std::clamp(level - gRainVol, -0.04f, 0.04f);   // ~1 s fades at the tick rate
    if (gRainV) {
        gRainV->SetVolume(gRainVol);
        if (gRainVol <= 0.001f && level <= 0.f) { gRainV->Stop(); gRainV->DestroyVoice(); gRainV = nullptr; }
    }
}
/// A thunderclap (near = louder, brighter).
void thunder(float volume, float closeK) {
    std::lock_guard l(gMu);
    if (volume <= 0.f || !init()) return;
    gOneShots.erase(std::remove_if(gOneShots.begin(), gOneShots.end(), [](IXAudio2SourceVoice* v) {
        XAUDIO2_VOICE_STATE st{}; v->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        if (st.BuffersQueued == 0) { v->DestroyVoice(); return true; }
        return false;
    }), gOneShots.end());
    if (gOneShots.size() >= 3) return;
    static uint32_t r = 9;
    r = r * 1103515245u + 12345u;
    auto const& pcm = gThunder[(r >> 16) % 3];
    WAVEFORMATEX wf = fmt();
    IXAudio2SourceVoice* v = nullptr;
    if (FAILED(gXa->CreateSourceVoice(&v, &wf, 0, 2.0f))) return;
    v->SetFrequencyRatio(0.8f + 0.35f * closeK);
    v->SetVolume(std::clamp(volume * (0.45f + 0.55f * closeK), 0.f, 1.f));
    XAUDIO2_BUFFER b{};
    b.AudioBytes = UINT32(pcm.size() * sizeof(short)); b.pAudioData = reinterpret_cast<BYTE const*>(pcm.data()); b.Flags = XAUDIO2_END_OF_STREAM;
    if (FAILED(v->SubmitSourceBuffer(&b))) { v->DestroyVoice(); return; }
    v->Start();
    gOneShots.push_back(v);
}

} // namespace bedrock_forge::client::weathersounds
