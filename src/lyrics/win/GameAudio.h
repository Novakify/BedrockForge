#pragma once
// [BF] Minecraft's own sound (process loopback): loudness and booms for the sky. See GameAudio_win.cpp.
namespace bf::lyrics {
struct GameAudio {
    float level = 0;   // 0..1 how loud the game is right now (smoothed)
    float low   = 0;   // 0..1 rumble (explosions, thunder, music bass)
    float boom  = 0;   // 0..1 a fresh loud low hit, decaying
    int   booms = 0;   // counter: a new boom happened when it changes
    int   status = 0;  // 0 off, 1 listening, -1 not supported on this Windows
};
void      gameAudioWant(bool on);   // start / pause listening (cheap to call every frame)
GameAudio gameAudio();
void      gameAudioPeaks(float& level, float& low);   // loudest raw block since the last call (resets)
} // namespace bf::lyrics
