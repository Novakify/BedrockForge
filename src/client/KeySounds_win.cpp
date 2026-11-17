// Bedrock Forge client - Creamy Keys (Purxiy "your keyboard, with a sound of its own"). Original code: the sounds are
// synthesised here at start-up (no sound files), played through XAudio2 so fast typing overlaps like a real board.
//   Creamy: a soft, deep "thock" (low body + a muted top)   Thock: deeper and rounder   Clicky: a bright tick on top
// Space / Enter / Backspace get their own bigger, lower voice; every press is pitched and levelled a little differently.
#include <windows.h>
#include <xaudio2.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <random>
#include <vector>

#include "client/Module.h"

namespace bedrock_forge::client::keysounds {
namespace {
constexpr int kRate = 44100;
std::mutex              gMu;
IXAudio2*               gXa     = nullptr;
IXAudio2MasteringVoice* gMaster = nullptr;
bool                    gTried  = false;
std::vector<short>      gBank[3][2];   // [profile][0 key, 1 big key]
std::vector<IXAudio2SourceVoice*> gVoices;

// one press: a filtered noise click + a damped body resonance (+ a bright tick for Clicky)
std::vector<short> synth(int profile, bool big, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> un(-1.f, 1.f);
    float const body  = (profile == 1 ? 150.f : 210.f) * (big ? 0.72f : 1.f);
    float const decay = (profile == 1 ? 34.f : 46.f) * (big ? 0.8f : 1.f);   // 1/s
    int const   n     = int(kRate * (big ? 0.16f : 0.11f));
    std::vector<short> out(size_t(n));
    float lp = 0, lp2 = 0;
    for (int i = 0; i < n; ++i) {
        float const t = float(i) / kRate;
        float const noise = un(rng);
        float const cut = profile == 2 ? 0.55f : 0.22f;                       // how bright the click is
        lp += cut * (noise - lp); lp2 += cut * (lp - lp2);
        float const click = lp2 * std::exp(-t * 420.f) * 1.4f;
        float const thock = std::sin(6.2831853f * body * t + 2.2f * std::exp(-t * 60.f)) * std::exp(-t * decay)
                          + 0.35f * std::sin(6.2831853f * body * 2.03f * t) * std::exp(-t * decay * 1.8f);
        float tick = 0;
        if (profile == 2) tick = std::sin(6.2831853f * 3200.f * t) * std::exp(-t * 900.f) * 0.5f;
        float const att = std::min(1.f, t * kRate / 24.f);                 // no pop at the start
        float const v = att * (click * 0.55f + thock * (profile == 1 ? 0.75f : 0.55f) + tick);
        out[size_t(i)] = short(std::clamp(v * 0.55f, -1.f, 1.f) * 32000.f);
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
    for (int p = 0; p < 3; ++p) { gBank[p][0] = synth(p, false, 7u + uint32_t(p)); gBank[p][1] = synth(p, true, 99u + uint32_t(p)); }
    return true;
}
void reap() {
    gVoices.erase(std::remove_if(gVoices.begin(), gVoices.end(), [](IXAudio2SourceVoice* v) {
        XAUDIO2_VOICE_STATE st{}; v->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        if (st.BuffersQueued == 0) { v->DestroyVoice(); return true; }
        return false;
    }), gVoices.end());
}
} // namespace

/// A key went down (edge, not repeats). Plays when the Creamy Keys module is on.
void press(int vk) {
    auto* m = Registry::get().find("creamy_keys");
    if (!m || !m->enabled) return;
    std::lock_guard l(gMu);
    if (!init()) return;
    reap();
    if (gVoices.size() >= 12) return;
    int const  profile = std::clamp(int(m->num("profile", 0)), 0, 2);
    bool const big     = vk == 0x20 || vk == 0x0D || vk == 0x08;
    auto const& pcm    = gBank[profile][big ? 1 : 0];
    WAVEFORMATEX wf{};
    wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 1; wf.nSamplesPerSec = kRate; wf.wBitsPerSample = 16;
    wf.nBlockAlign = 2; wf.nAvgBytesPerSec = kRate * 2;
    IXAudio2SourceVoice* v = nullptr;
    if (FAILED(gXa->CreateSourceVoice(&v, &wf, 0, 2.0f))) return;
    static uint32_t r = 12345;
    r = r * 1103515245u + 12345u;
    float const jitter = float((r >> 16) & 1023u) / 1023.f;
    v->SetFrequencyRatio(0.94f + 0.12f * jitter);          // no two presses alike
    v->SetVolume(std::clamp(m->num("volume", 0.6f), 0.f, 1.f) * (0.85f + 0.15f * jitter));
    XAUDIO2_BUFFER b{};
    b.AudioBytes = UINT32(pcm.size() * sizeof(short));
    b.pAudioData = reinterpret_cast<BYTE const*>(pcm.data());
    b.Flags      = XAUDIO2_END_OF_STREAM;
    if (FAILED(v->SubmitSourceBuffer(&b))) { v->DestroyVoice(); return; }
    v->Start();
    gVoices.push_back(v);
}

} // namespace bedrock_forge::client::keysounds
