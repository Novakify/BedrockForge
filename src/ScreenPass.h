// Renderer Foundation - LyricsForge video screen. Original code for Renderer Foundation.
// Bedrock Forge picks the window and places the screen (glyph quads with BF_GLYPH_LIVE, BfLyrics.h); RF captures the
// window (ScreenCapture.cpp), optionally cleans up + upscales the picture with Anime4K (shaders/anime4k.hlsl, D3D12),
// letterboxes it into a fixed 1920x1080 texture with mips, and the glyph pass samples that texture at t21.
#pragma once
#include <cstdint>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;

namespace rf::screen {

inline constexpr uint32_t kOutW = 1920, kOutH = 1080, kOutMips = 4;

/// Every Present, before anything else (cheap): BF's settings -> the capture thread, our status -> BF.
void poll(bool d3d11);
/// D3D12, inside the lyric pass before the glyphs draw: when a new captured frame is in, upload it and rebuild the
/// screen texture. Returns the screen texture (all mips in PIXEL_SHADER_RESOURCE state) or null when there is none.
/// Changes the root signature, descriptor heaps, render target and viewport (the caller sets its own afterwards).
ID3D12Resource* record12(ID3D12Device* device, ID3D12GraphicsCommandList* list, unsigned slot, uint64_t present);
/// D3D11 (no Anime4K there): same screen texture, state is restored by the caller (Saved11).
ID3D11ShaderResourceView* record11(ID3D11Device* device, ID3D11DeviceContext* ctx);
void drawUi(); // F6 section

} // namespace rf::screen
