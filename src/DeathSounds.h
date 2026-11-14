#pragma once
// Death scene sounds (by Novak): the Finishers / Sable Ragdolls .ogg files, played as each death scene reaches its cue
// (src/DeathSoundsData.h, made by tools/gen_death_tables.py). Decoded once with stb_vorbis, played with XAudio2 (loaded
// at runtime from xaudio2_9.dll, so nothing new to link), placed by the kill's position: farther = quieter, panned.
#include <cstdint>

namespace rf::deathsfx {
// the listener: camera position (world) and forward, once a frame
void setListener(float const pos[3], float const fwd[3]);
// a kill playing scene `scene` (7..15 have sounds), `age` seconds in, at `pos` (world, its feet); key = the kill's
// identity (the same each frame). volume 0..1 (F6). Cues already passed by more than a moment (a kill first seen late)
// are skipped rather than played late.
void onKill(uint64_t key, int scene, float age, float const pos[3], float volume);
} // namespace rf::deathsfx
