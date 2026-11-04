// Bedrock Forge -> Renderer Foundation contract for the LyricsForge Director's full-screen look (v1).
// Original code for Bedrock Forge. Same pattern as BF_GetPostFx: BF owns the data, RF pulls it once per frame.
// RF applies it in its post pass (shaders/postfx.hlsl), which runs BEFORE the lyric glyphs, so words stay ungraded.
// Keep this header byte-identical in both repos (RF copy: src/BfDirector.h).
#pragma once
#include <cstdint>

extern "C" {

struct BfDirectorV1 {
    uint32_t size;       // sizeof(BfDirectorV1)
    uint32_t version;    // 1
    uint32_t active;     // 1 while a song plays with the Director on and a world on screen
    uint32_t frame;      // increments every BF frame
    // music state (Conductor), 0..1
    float budget, impact, build, beat, bass, mid, treble, timeScale;
    // grade (already crossfaded and scaled by the reactivity; amount 0 = untouched)
    float tint[3];       // linear rgb multiplier target (1,1,1 = neutral)
    float gradeAmount;   // 0..1 mix towards the tinted image
    float saturation;    // 1 = unchanged
    float contrast;      // 1 = unchanged (pivot 0.5 in display space)
    float vignette;      // 0..1 corner darkening
    float flash;         // 0..1 additive light flash (impact frame), already capped for comfort
    float flashColor[3]; // rgb 0..1
    float letterbox;     // fraction of the screen height covered top + bottom each (0..0.075)
    float bloomBoost;    // added to the pack's bloom mix
    float caBoost;       // added to the pack's chromatic aberration
    float reserved[8];
};
static_assert(sizeof(BfDirectorV1) == 128, "BfDirectorV1 layout is shared with RF");

#if defined(_WIN32)
__declspec(dllexport)
#endif
BfDirectorV1 const* BF_GetDirector();
}
