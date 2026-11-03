// Renderer Foundation - LyricsFlow port: draws Bedrock Forge's lyric glyphs (BF_CopyLyricFrame, BfLyrics.h) with
// shaders/glyph.hlsl in one instanced draw. Original code for Renderer Foundation.
//
// Where it draws (F6 > "LyricsFlow (preview)"):
//   * default ("S5-safe"): at Present, after RF's post effects, before ImGui -> on top of everything incl. the HUD.
//     Works without any spike, so a word can be put on screen on day one.
//   * "Under the HUD" (spike S2, STAGED/UNVERIFIED): needs "uiCaptureSpike": true. UiCapture redirects the game's UI
//     draws into our own target (the Spatial GUI M1 mechanism), we draw the glyphs on the back buffer and composite
//     the UI back on top -> lyrics sit "in" the world, hotbar/crosshair stay above them.
// Camera (spike S1, STAGED/UNVERIFIED): LyricsCamera's hook gives the real camera; F6 can switch to Bedrock Forge's
// yaw/pitch/FOV estimate, flip the forward axis, or show quad bounds while verifying.
// Occlusion (spike S3): BF fades text behind terrain with CPU raycasts (LyricsFlow's own method); per-pixel depth is
// a later option (bind the scene DSV UiCapture already tracks).
#pragma once
#include <cstddef>
#include <cstdint>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;
struct D3D12_CPU_DESCRIPTOR_HANDLE;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;

namespace rf::lyrics {

// ---- UiCapture tie-in (spike S2), any thread
bool   wantRedirect(); // "under the HUD" on, UI target ready, Spatial GUI panel not using the redirect
size_t targetRtv();
void   noteRedirected();
bool   tracking(); // keep UiCapture's list/PSO tracking on

// ---- overlay thread (Present). slot = back buffer index (ring slot for the upload buffer)
void record(ID3D12Device* device, ID3D12GraphicsCommandList* list, ID3D12Resource* bb, D3D12_CPU_DESCRIPTOR_HANDLE const& bbRtv, unsigned slot);
/// Direct3D 11 (the game runs on D3D11 when a client such as Blueberry blocks D3D12): same glyphs, glass and atlases,
/// drawn on the back buffer at Present with the D3D11 immediate context (state saved and restored around it).
void record11(ID3D11Device* device, ID3D11DeviceContext* ctx, ID3D11RenderTargetView* bbRtv);
void drawUi(); // F6 section
void install(); // mod enable
void uninstall();

} // namespace rf::lyrics
