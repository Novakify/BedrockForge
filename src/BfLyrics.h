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
    BF_GLYPH_VIEW     = 1u << 6, // camera-locked perspective (the settings panel): pos/right/up are in BF's virtual view
                                 // space, x right / y up / z forward, x and y already divided by tan(fovY/2). The vertex
                                 // shader outputs (x / aspect, y, 0, z): exact perspective, so BF can ray-pick the mouse
    BF_GLYPH_IMAGE    = 1u << 7, // colour image (cover art, icons, thumbnails): rgba sample * fill (tint), no SDF
    // Glass (2026-10-31, client UI + LyricsFlow panel). RF copies the frame before the glyphs draw and blurs it; a glass
    // surface shows that copy frosted and bent like a lens at its edge. With SOLID or PETAL:
    //   fill = tint colour, fill.a = tint amount | glowW = bevel (px, refraction zone) | glowK = refraction (px)
    //   pxRange = blur mix 0..1 (0 clear, 1 frosted) | outline = rim light (a = strength) | glow = inner edge glow
    //   (a = strength) | outlineW = corner radius fraction (SOLID) or inner/outer radius (PETAL)
    BF_GLYPH_GLASS    = 1u << 8,
    BF_GLYPH_PETAL    = 1u << 9,  // ring sector filling the quad: fill2 = centre angle | half span << 16, both in
                                  // 1/100 degree, screen space (0 = right, 9000 = down); without GLASS = flat colour
    BF_GLYPH_BACKDROP = 1u << 10, // full-screen frosted frame: fill = darkening colour (a = amount), pxRange = blur,
                                  // glowW = clear circle radius px around the quad centre, glowK = its feather px
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

inline constexpr uint32_t kBfLyricsMaxAtlases = 16; // fonts + UI image atlas + cover art

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
// ---------------------------------------------------------------- live sky (LyricsFlow 2.1 WorldSky), v1
// The dome is shaders/live_sky (parameter row + 4 floats); everything else WorldSky draws (stars, constellations,
// shooting stars, lightning, weather particles) comes as quads, 4 vertices each, camera-relative.
enum BfSkyVertFlags : uint32_t {
    BF_SKY_WORLD  = 1u << 0, // pos in blocks around the camera (weather); else a unit direction (sky distance = 1)
    BF_SKY_SPRITE = 1u << 1, // sample spritesFile (256x64: petal, snow, glow, streak) * colour; else flat colour
};
/// One quad corner. 32 bytes. Colour RGBA8 with R in the low byte, straight alpha.
struct BfSkyVertV1 {
    float    pos[3];
    float    uv[2];
    uint32_t color;
    uint32_t flags; // BfSkyVertFlags (same on all 4 corners)
    float    _pad;
};
static_assert(sizeof(BfSkyVertV1) == 32, "BfSkyVertV1 layout is shared with RF");

struct BfSkyFrameV1 {
    uint32_t size;        // sizeof(BfSkyFrameV1) (caller sets it)
    uint32_t version;     // 1
    uint32_t active;      // 1 while a LyricsFlow sky replaces Bedrock's
    uint32_t frame;       // increments every BF frame
    uint8_t  params[16][4]; // live_sky parameter row (16 x 1 RGBA8), LiveSkyShader.params byte layout
    float    info[4];     // x = shader time (UV0.x), y = brightness (Color.r * 2), z = alpha (Color.a), w = 0
    uint32_t picture;     // 1 = draw pictureFile as an equirectangular panorama instead of the dome
    float    pictureAlpha, pictureBright;
    uint32_t imageGeneration; // bumps when spritesFile / pictureFile change
    float    camYaw, camPitch; // BF's camera for these quads (Minecraft degrees)
    uint32_t vertCount;   // vertices copied (multiple of 4)
    uint32_t vertTotal;   // vertices available
    char     spritesFile[260];
    char     pictureFile[260];
};

/// Copy the current sky frame. hdr may not be null; dst may be null (header only). Returns vertices copied.
BF_LYRICS_EXPORT uint32_t BF_CopySkyFrame(BfSkyFrameV1* hdr, BfSkyVertV1* dst, uint32_t capacity);
typedef uint32_t (*BF_CopySkyFrame_t)(BfSkyFrameV1*, BfSkyVertV1*, uint32_t);
}
