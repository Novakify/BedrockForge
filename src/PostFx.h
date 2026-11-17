#pragma once
// Renderer Foundation - full-screen post effects on the D3D12 swapchain image (bloom, chromatic aberration, CAS,
// camera motion blur). Bedrock Forge shaderpacks request them through BF_GetPostFx() (BedrockForgeCore.dll);
// RF reads that every frame, records the passes into the overlay's command list before ImGui, and shows them in F6.
#include <d3d12.h>
#include <cstdint>

namespace rf::postfx {

/// Record the post passes. `bb` is in PRESENT state on entry; when this returns true it left `bb` in RENDER_TARGET
/// with `bbRtv` bound (the caller draws ImGui and transitions back to PRESENT), false = nothing was recorded.
bool record(ID3D12Device* device, ID3D12GraphicsCommandList* list, ID3D12Resource* bb, D3D12_CPU_DESCRIPTOR_HANDLE bbRtv);
/// After the list that `record` wrote into was executed on `queue`.
void submitted(ID3D12CommandQueue* queue);
/// Free the size-dependent resources (swapchain resize / shutdown). Waits for the GPU to finish with them.
void release();
/// F6 section.
void drawUi();
/// [LyricsForge] Showcase camera from BF_GetDirector(): blend 0..1 and the sway pose (Minecraft yaw / pitch, degrees;
/// radius in blocks behind the eyes). false = not active.
/// Motion blur asked for right now (the Bedrock Forge Motion Blur module, a pack's postfx.json, or the F6 slider); 0 = off.
/// World FX draws it on the world's targets when its pass runs (depth, no HUD) and says so with noteWorldBlur(): the
/// Present-time blur then stays out of the way.
float motionBlurWanted();
void  noteWorldBlur();
/// The Bedrock Forge Motion Blur module's "moving" share (0..1.5), -1 = it does not say (F6 World FX value applies).
float motionBlurMove();
/// What moves by itself near you (players, mobs, dropped items), from Bedrock Forge's BF_GetBlurActors(): the world
/// motion blur leaves these sharp. nullptr = Bedrock Forge does not send them (older build) or nothing fresh.
struct BfBlurActorsV1 {
    uint32_t size, version, count, frame;
    struct A { float x, y, z; float r, h; float pad[3]; } a[32];   // feet centre (world), radius, height
};
BfBlurActorsV1 const* blurActors();
bool showcasePose(float& blend, float& yaw, float& pitch, float& radius);

} // namespace rf::postfx
