// Death scene sounds - see DeathSounds.h.
#include "DeathSounds.h"
#include "DeathSoundsData.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xaudio2.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <mutex>
#include <vector>

#define STB_VORBIS_HEADER_ONLY
#include "../third_party/stb/stb_vorbis.c"

namespace rf::deathsfx {
namespace {
struct Pcm { std::vector<short> s; int ch = 1, rate = 44100; bool ok = false; };
std::mutex gMu;
IXAudio2* gXa = nullptr;
IXAudio2MasteringVoice* gMaster = nullptr;
UINT32 gOutCh = 2;
bool gTried = false;
Pcm gPcm[std::size(kSounds)];
float gLis[3]{}, gFwd[3]{0, 0, 1};
struct Voice { IXAudio2SourceVoice* v; uint64_t key; int sound; };
std::vector<Voice> gVoices;
struct Kill { uint64_t key = 0; uint64_t fired = 0; int punch = 0; };
Kill gKills[32];
int gNk = 0;

bool init() {
    if (gTried) return gXa != nullptr;
    gTried = true;
    (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);   // XAudio2 wants COM on this thread (already there: harmless)
    HMODULE m = LoadLibraryW(L"xaudio2_9.dll");
    if (!m) return false;
    using Create = HRESULT(WINAPI*)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR);
    auto create = reinterpret_cast<Create>(GetProcAddress(m, "XAudio2Create"));
    if (!create || FAILED(create(&gXa, 0, XAUDIO2_DEFAULT_PROCESSOR))) { gXa = nullptr; return false; }
    if (FAILED(gXa->CreateMasteringVoice(&gMaster))) { gXa->Release(); gXa = nullptr; return false; }
    XAUDIO2_VOICE_DETAILS d{};
    gMaster->GetVoiceDetails(&d);
    gOutCh = std::max<UINT32>(1, d.InputChannels);
    for (size_t i = 0; i < std::size(kSounds); ++i) {   // decode everything once (~1 MB of PCM in all)
        short* out = nullptr; int ch = 0, rate = 0;
        int const n = stb_vorbis_decode_memory(kSounds[i].ogg, int(kSounds[i].size), &ch, &rate, &out);
        if (n > 0 && out) { gPcm[i].s.assign(out, out + size_t(n) * size_t(ch)); gPcm[i].ch = ch; gPcm[i].rate = rate; gPcm[i].ok = true; }
        free(out);
    }
    return true;
}
void reap() {   // voices that have finished
    gVoices.erase(std::remove_if(gVoices.begin(), gVoices.end(), [](Voice const& v) {
        XAUDIO2_VOICE_STATE st{}; v.v->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        if (st.BuffersQueued == 0) { v.v->DestroyVoice(); return true; }
        return false;
    }), gVoices.end());
}
void play(uint64_t key, int sound, float vol, float pitch, float const pos[3]) {
    Pcm const& p = gPcm[sound];
    if (!p.ok || gVoices.size() >= 24) return;
    // distance: full within 3 blocks, gone at 28 (Minecraft-ish); pan: where it is across the view
    float d[3] = {pos[0] - gLis[0], pos[1] - gLis[1], pos[2] - gLis[2]};
    float const dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    float const att = std::pow(std::clamp(1.0f - (dist - 3.0f) / 25.0f, 0.0f, 1.0f), 1.6f);
    if (att * vol < 0.01f) return;
    float rx = -gFwd[2], rz = gFwd[0];   // right of the view (y up)
    float const rl = std::sqrt(rx * rx + rz * rz) + 1e-6f;
    float const pan = dist > 0.5f ? std::clamp((d[0] * rx + d[2] * rz) / (rl * dist), -1.0f, 1.0f) : 0.0f;
    WAVEFORMATEX wf{};
    wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = WORD(p.ch); wf.nSamplesPerSec = DWORD(p.rate); wf.wBitsPerSample = 16;
    wf.nBlockAlign = WORD(2 * p.ch); wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
    IXAudio2SourceVoice* v = nullptr;
    if (FAILED(gXa->CreateSourceVoice(&v, &wf, 0, 4.0f))) return;
    XAUDIO2_BUFFER b{};
    b.AudioBytes = UINT32(p.s.size() * sizeof(short)); b.pAudioData = reinterpret_cast<BYTE const*>(p.s.data()); b.Flags = XAUDIO2_END_OF_STREAM;
    v->SubmitSourceBuffer(&b);
    v->SetFrequencyRatio(std::clamp(pitch, 0.25f, 4.0f));
    v->SetVolume(std::clamp(vol * att, 0.0f, 1.0f));
    if (gOutCh >= 2) {   // equal-power pan into the first two speakers (others silent)
        float const a = (pan + 1.0f) * 0.7853982f;
        std::vector<float> mx(size_t(p.ch) * gOutCh, 0.0f);
        for (int c = 0; c < p.ch; ++c) { mx[size_t(0) * p.ch + c] = std::cos(a); mx[size_t(1) * p.ch + c] = std::sin(a); }
        v->SetOutputMatrix(nullptr, UINT32(p.ch), gOutCh, mx.data());
    }
    v->Start();
    gVoices.push_back({v, key, sound});
}
void stop(uint64_t key, int sound) {
    for (auto& v : gVoices) if (v.key == key && v.sound == sound) v.v->Stop();
    for (auto& v : gVoices) if (v.key == key && v.sound == sound) v.v->FlushSourceBuffers();
}
} // namespace

void setListener(float const pos[3], float const fwd[3]) {
    std::lock_guard l(gMu);
    std::memcpy(gLis, pos, 12); std::memcpy(gFwd, fwd, 12);
}

void onKill(uint64_t key, int scene, float age, float const pos[3], float volume) {
    if (scene < 7 || volume <= 0.0f) return;
    std::lock_guard l(gMu);
    if (!init()) return;
    reap();
    Kill* k = nullptr;
    for (auto& e : gKills) if (e.key == key) { k = &e; break; }
    if (!k) {
        k = &gKills[gNk++ % 32];
        *k = Kill{key, 0, int((key * 2654435761u) >> 7) & 3};
    }
    for (size_t i = 0; i < std::size(kCues) && i < 64; ++i) {
        Cue const& c = kCues[i];
        if (c.scene != scene || age < c.t || (k->fired >> i) & 1) continue;
        k->fired |= uint64_t(1) << i;
        if (age - c.t > 0.25f) continue;   // seen late: don't play it late
        int const snd = c.sound < 0 ? kPunchSet[k->punch] : c.sound;
        if (c.vol < 0.0f) stop(key, snd);
        else play(key, snd, c.vol * volume, c.pitch, pos);
    }
}
} // namespace rf::deathsfx
