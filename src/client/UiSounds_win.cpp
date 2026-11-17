// Bedrock Forge client - UI sounds (Purxiy "sound": volume and which sounds you hear). Original code: every sound is
// synthesised here at first use (no sound files) and played through XAudio2.
//   hover: a tiny soft tick      click: a short glassy tap        toggle on / off: two notes, rising / falling
//   open / close: a soft airy swell up / down     tab: a muted tick     slider: a very short tick per step
#include <windows.h>
#include <xaudio2.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <random>
#include <vector>

#include "client/Module.h"

namespace bedrock_forge::client::uisound {
namespace {
constexpr int kRate = 44100;
std::mutex              gMu;
IXAudio2*               gXa     = nullptr;
IXAudio2MasteringVoice* gMaster = nullptr;
bool                    gTried  = false;
std::vector<short>      gBank[8];
std::vector<IXAudio2SourceVoice*> gVoices;
int64_t                 gLastMs[8]{};

int64_t nowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::vector<short> render(float seconds, auto&& fn) {
    int const n = int(kRate * seconds);
    std::vector<short> out(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        float const t   = float(i) / kRate;
        float const att = std::min(1.f, t * kRate / 32.f);              // no click at the start
        float const end = std::min(1.f, float(n - i) / 64.f);            // nor at the end
        out[size_t(i)]  = short(std::clamp(fn(t) * att * end, -1.f, 1.f) * 30000.f);
    }
    return out;
}
float tone(float f, float t) { return std::sin(6.2831853f * f * t); }
// a soft "glass" note: a sine with a quiet fifth and a fast-decaying bright partial
float glassNote(float f, float t, float decay) {
    return (tone(f, t) + 0.30f * tone(f * 1.5f, t) * std::exp(-t * 30.f) + 0.18f * tone(f * 4.02f, t) * std::exp(-t * 60.f)) * std::exp(-t * decay);
}
void build() {
    std::mt19937 rng(77);
    std::uniform_real_distribution<float> un(-1.f, 1.f);
    gBank[0] = render(0.045f, [](float t) { return 0.22f * glassNote(2400.f, t, 90.f); });                       // hover
    gBank[1] = render(0.10f, [](float t) { return 0.55f * glassNote(1320.f, t, 45.f); });                         // click
    gBank[2] = render(0.20f, [](float t) { return 0.45f * (glassNote(880.f, t, 30.f) + (t > 0.06f ? glassNote(1318.5f, t - 0.06f, 28.f) : 0.f)); });   // on
    gBank[3] = render(0.20f, [](float t) { return 0.40f * (glassNote(1318.5f, t, 30.f) + (t > 0.06f ? glassNote(880.f, t - 0.06f, 28.f) : 0.f)); });   // off
    float lp = 0;
    std::vector<float> noise(static_cast<size_t>(kRate));
    for (auto& v : noise) v = un(rng);
    gBank[4] = render(0.32f, [&, i = 0](float t) mutable {                                                      // open: airy swell up
        lp += 0.08f * (noise[size_t(i++ % kRate)] - lp);
        float const env = std::sin(3.14159f * std::min(t / 0.32f, 1.f));
        return env * (lp * 0.9f + 0.20f * tone(520.f + 900.f * t, t));
    });
    lp = 0;
    gBank[5] = render(0.28f, [&, i = 0](float t) mutable {                                                      // close: down
        lp += 0.06f * (noise[size_t(i++ % kRate)] - lp);
        float const env = std::sin(3.14159f * std::min(t / 0.28f, 1.f));
        return env * (lp * 0.8f + 0.16f * tone(1100.f - 1200.f * t, t));
    });
    gBank[6] = render(0.06f, [](float t) { return 0.35f * glassNote(1760.f, t, 70.f); });                        // tab
    gBank[7] = render(0.025f, [](float t) { return 0.20f * glassNote(3000.f, t, 160.f); });                      // slider step
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
    build();
    return true;
}
} // namespace

/// kind: 0 hover, 1 click, 2 toggle on, 3 toggle off, 4 open, 5 close, 6 tab, 7 slider step. Client UI settings
/// "sounds" (on / off), "sound_volume", "hover_sounds", "slider_ticks".
void play(int kind) {
    if (kind < 0 || kind > 7) return;
    auto* m = Registry::get().find("client_ui");
    if (!m || !m->flag("sounds", true)) return;
    if (kind == 0 && !m->flag("hover_sounds", true)) return;
    if (kind == 7 && !m->flag("slider_ticks", true)) return;
    float const vol = std::clamp(m->num("sound_volume", 0.5f), 0.f, 1.f);
    if (vol <= 0.f) return;
    std::lock_guard l(gMu);
    int64_t const now = nowMs();
    if (now - gLastMs[kind] < (kind == 0 ? 35 : kind == 7 ? 25 : 15)) return;   // a burst of the same sound: one
    gLastMs[kind] = now;
    if (!init()) return;
    gVoices.erase(std::remove_if(gVoices.begin(), gVoices.end(), [](IXAudio2SourceVoice* v) {
        XAUDIO2_VOICE_STATE st{}; v->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        if (st.BuffersQueued == 0) { v->DestroyVoice(); return true; }
        return false;
    }), gVoices.end());
    if (gVoices.size() >= 10) return;
    WAVEFORMATEX wf{};
    wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 1; wf.nSamplesPerSec = kRate; wf.wBitsPerSample = 16;
    wf.nBlockAlign = 2; wf.nAvgBytesPerSec = kRate * 2;
    IXAudio2SourceVoice* v = nullptr;
    if (FAILED(gXa->CreateSourceVoice(&v, &wf, 0, 2.0f))) return;
    auto const& pcm = gBank[kind];
    v->SetVolume(vol);
    XAUDIO2_BUFFER b{};
    b.AudioBytes = UINT32(pcm.size() * sizeof(short));
    b.pAudioData = reinterpret_cast<BYTE const*>(pcm.data());
    b.Flags      = XAUDIO2_END_OF_STREAM;
    if (FAILED(v->SubmitSourceBuffer(&b))) { v->DestroyVoice(); return; }
    v->Start();
    gVoices.push_back(v);
}

} // namespace bedrock_forge::client::uisound
