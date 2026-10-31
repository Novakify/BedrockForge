// Renderer Foundation - LyricsFlow port: live sky (BF_CopySkyFrame, BfLyrics.h) drawn with shaders/sky.hlsl.
// Original code for Renderer Foundation.
//
// STAGE 1 (this file): a PREVIEW at Present - F6 > "LyricsFlow sky (preview)" draws the dome / picture plus the
// star, lightning and weather quads into a window in the corner (or full screen), from BF's camera. It proves the
// BF -> RF data, the shaders and the images in game without touching the game's frame.
// STAGE 2 (needs the PC): draw the same thing where Bedrock draws its sky (tag the Sky material's pipelines like
// UiCapture tags UI ones, record the dome right after the game's sky draw, restore the game's state).
#pragma once

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;
struct D3D12_CPU_DESCRIPTOR_HANDLE;

namespace rf::sky {

void record(ID3D12Device* device, ID3D12GraphicsCommandList* list, ID3D12Resource* bb, D3D12_CPU_DESCRIPTOR_HANDLE const& bbRtv, unsigned slot);
void drawUi(); // F6 section

} // namespace rf::sky
