// Bedrock Forge -> Renderer Foundation contract for LyricsFlow world text (v1). Original code for Bedrock Forge.
// Same pattern as BF_GetPostFx (client/PostFx.h): BF owns the data, RF pulls it once per frame at its draw point.
// BF builds a list of glyph quads (positions relative to `origin`, so floats stay precise far from 0,0); RF copies
// them with BF_CopyLyricFrame() into its upload buffer and draws them in ONE instanced draw with shaders/glyph.hlsl.
// Keep this header byte-identical in both repos (RF copy: src/BfLyrics.h on the lyricsflow/rf-scaffold branch).
#pragma once
#include <cstdint>

#if defined(_WIN32)
#define BF_LYRICS_EXPORT __declspec(dllexport)
#else
#define BF_LYRICS_EXPORT
#endif

extern "C" {

enum BfGlyphFlags : uint32_t {
    BF_GLYPH_SCREEN   = 1u << 0, // pos/right/up are screen units: x 0..aspect, y 0..1 (down); z unused
    BF_GLYPH_BITMAP   = 1u << 1, // atlas is a plain alpha bitmap (pixel font): nearest sampling, no SDF maths
    BF_GLYPH_MSDF     = 1u << 2, // atlas is multi-channel SDF (median of rgb); otherwise single-channel SDF in .r (grey PNG)
    BF_GLYPH_LETTER   = 1u << 3, // falling letter (physics) - informational
    BF_GLYPH_GRADIENT = 1u << 4, // vertical gradient fill -> fill2 at the bottom of the quad
    BF_GLYPH_SOLID    = 1u << 5, // no atlas: a rounded rectangle. outlineW = corner radius as a fraction of the
                                 // smaller half-extent (1 = pill/circle), glowW = edge softness in screen px (shadows)
};

/// One glyph quad. 112 bytes, 16-byte aligned rows (HLSL StructuredBuffer friendly). Colours are RGBA8 with R in the
/// low byte (0xAABBGGRR), straight (not premultiplied) alpha.
struct BfGlyphV1 {
    float    pos[3];     // quad centre, world units relative to BfLyricFrameV1::origin
    float    _pad0;
    float    right[3];   // half-extent vector along the glyph's +x
    float    _pad1;
    float    up[3];      // half-extent vector along the glyph's +y
    float    _pad2;
    float    uv[4];      // atlas rect u0, v0 (top), u1, v1 (bottom), 0..1
    uint32_t fill;       // main colour (top colour when BF_GLYPH_GRADIENT)
    uint32_t fill2;      // gradient bottom colour
    uint32_t outline;    // outline colour (alpha 0 = no outline)
    uint32_t glow;       // glow colour (alpha 0 = no glow)
    float    alpha;      // overall opacity 0..1
    float    outlineW;   // outline width in SDF distance units (0..0.35)
    float    glowW;      // glow radius in SDF distance units (0..0.5)
    float    glowK;      // glow strength 0..1
    uint32_t atlas;      // index into BfLyricFrameV1::atlasFiles
    uint32_t flags;      // BfGlyphFlags
    float    pxRange;    // SDF distance range of the atlas in atlas pixels (msdf-atlas-gen "distanceRange")
    float    _pad3;
};
static_assert(sizeof(BfGlyphV1) == 112, "BfGlyphV1 layout is shared with RF / glyph.hlsl");

inline constexpr uint32_t kBfLyricsMaxAtlases = 8;

struct BfLyricFrameV1 {
    uint32_t size;        // sizeof(BfLyricFrameV1)
    uint32_t version;     // 1
    uint32_t active;      // 1 while lyrics should be drawn this frame
    uint32_t frame;       // increments every BF frame that rebuilt the list
    double   origin[3];   // world position the glyph positions are relative to (BF uses the camera eye)
    uint32_t count;       // glyphs copied
    uint32_t total;       // glyphs available (count < total = caller's buffer was too small)
    float    camYaw, camPitch, camFovY; // BF's camera estimate (debug / fallback for RF's S1 camera)
    uint32_t atlasCount;
    uint32_t atlasGeneration; // bumps when atlasFiles change (RF reloads its textures)
    char     atlasFiles[kBfLyricsMaxAtlases][260]; // UTF-8 paths of the atlas PNGs
};

/// Copy the current frame. hdr may not be null; dst may be null (header only). Returns glyphs copied.
BF_LYRICS_EXPORT uint32_t BF_CopyLyricFrame(BfLyricFrameV1* hdr, BfGlyphV1* dst, uint32_t capacity);
typedef uint32_t (*BF_CopyLyricFrame_t)(BfLyricFrameV1*, BfGlyphV1*, uint32_t);
}
