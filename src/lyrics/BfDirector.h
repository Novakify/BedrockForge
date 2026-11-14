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
    // Showcase (was reserved[0..4] before; old RF ignores them): RF moves the camera when showcase > 0
    float showcase;      // 0..1 blend from the player's eyes to the sway pose
    float orbitYaw, orbitPitch; // camera pose looking at the player's eyes, degrees (Minecraft yaw / pitch)
    float orbitRadius;   // blocks behind the eyes
    float focus;         // 0..1 soft focus towards the screen edges (no depth buffer: a lens-style falloff)
    float reserved[1];
    // [LyricsForge] world light (size >= 160; an RF that only knows 128 bytes ignores it): the sky lights the world.
    // RF has no depth buffer in its post pass, so it applies this fully below the horizon line (ground, terrain),
    // skyKeep of it above, and less on very bright pixels (the sky itself, highlights).
    float worldTint[3];  // display-space rgb multiplier the sky casts (1,1,1 = none); luminance-neutral
    float worldAmount;   // 0..1 mix towards the tinted world
    float exposure;      // world brightness multiplier (1 = unchanged): dark moments dim it, lightning / drops lift it
    float skyKeep;       // 0..1 share applied above the horizon
    float worldSat;      // saturation of the lit world (1 = unchanged): drops make it vivid for a moment
    float reserved2[1];
    // [LyricsForge] Glass Armor (size >= 256; older readers stop at 160): what the glass reflects and reacts to.
    // Filled whatever the Director look setting is, so the armor follows the music even with the grade off.
    uint32_t playing;     // 1 while a song plays
    uint32_t wordSeq;     // bumps each time a new lyric word starts being sung (RF runs one light sweep per bump)
    float awaken;         // 0..1 Awaken progress: 0 = no music (plain glass), rising over the ~5 s startup
    float outro;          // 0..1 the song's outro (the armor powers down in reverse)
    float finale;         // 1 while the constellation outro (the final line rising as stars) is showing
    float drop;           // 1 on a drop / chorus hit, decaying (~0.5 s); bigger on the song's big drops (up to 1.35)
    float cover[3];       // the song / chapter cover colour, display rgb 0..1 (coverAmount 0 = none)
    float coverAmount;    // 0..1
    float wordColor[3];   // the colour the newest sung word names ("pink", "blue"), display rgb 0..1
    float wordColorAmount;// 1 when the newest word is a colour word, else 0
    float sky[3];         // the LyricsForge sky's colour (hue, brightest channel = 1)
    float skyAmount;      // 0..1 (0 = vanilla sky / no LyricsForge sky)
    float reserved3[6];
};
static_assert(sizeof(BfDirectorV1) == 256, "BfDirectorV1 layout is shared with RF");

#if defined(_WIN32)
__declspec(dllexport)
#endif
BfDirectorV1 const* BF_GetDirector();
}
