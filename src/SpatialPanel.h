// Renderer Foundation - Spatial GUI port, milestone M1 (docs/PORT_SPATIALGUI.md in Bedrock Forge).
// The M0 spike showed Bedrock's UI draws straight into the swap-chain back buffer. M1 builds on the spike's hooks:
//   * redirect (UiCapture hooks): from the first UI-pipeline draw of a frame, every bind of a back-buffer RTV is swapped
//     for our own transparent UI target -> the flat 2D screen never reaches the back buffer, and we get it with alpha
//   * composite (here, at Present before RF's overlay): world copy drawn slightly zoomed + dimmed ("FOV lerp"), then the
//     UI target as a perspective 3D quad in front of the camera (open/close animation)
//   * input: mouse messages are ray-picked through the quad and remapped to the screen position the game expects
// Toggle: F8 (uiPanelKey) or the F6 section. Needs "uiCaptureSpike": true (the hooks live in UiCapture).
#pragma once
#include <cstddef>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;
struct D3D12_CPU_DESCRIPTOR_HANDLE;

namespace rf::panel {

// ---- used by the UiCapture hooks (any thread)
/// Panel on (or still animating out) and its UI target exists: UI draws should be redirected.
bool   wantRedirect();
/// CPU handle of our UI target's RTV (valid while wantRedirect()).
size_t targetRtv();
/// A UI draw was redirected this frame.
void   noteRedirected();
/// Hooks should keep tracking lists/PSOs even when no capture runs.
bool   enabled();

// ---- overlay thread
void toggle();
/// Present, before RF's post effects/ImGui. true = recorded, back buffer left in RENDER_TARGET (skip post effects).
bool record(ID3D12Device* device, ID3D12GraphicsCommandList* list, ID3D12Resource* bb, D3D12_CPU_DESCRIPTOR_HANDLE const& bbRtv);
/// wndProc: rewrite mouse message coordinates through the panel (ray-pick). Returns true if lp was changed.
bool remapMouse(HWND hwnd, UINT msg, LPARAM& lp);
/// wndProc: true = drop this message (raw mouse input while the panel is up, experiment).
bool swallow(UINT msg, LPARAM lp);
/// F6 section.
void drawUi();
/// Mod load: hook GameInputCreate so the game's own GameInput mouse readings can be remapped (v0.8).
void installEarly();

} // namespace rf::panel
