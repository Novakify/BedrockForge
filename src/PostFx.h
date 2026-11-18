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
struct BfFxEventsV1 {   // = Bedrock Forge's (src/client/Modules.cpp)
    uint32_t size, version, count, frame;
    struct E { float x, y, z; uint32_t kind; int64_t atMs; float a, b; } e[32];   // kind: 1 jump, 2 hit, 3 crit, 4 landing (a = fall speed); atMs = steady_clock ms
    // version 2: you (trail / aura / halo)
    float    px, py, pz, pyaw;   // your feet (world), yaw (degrees)
    uint32_t pflags;             // 1 = known, 2 = on the ground
    uint32_t pad2;
    int64_t  pAtMs;              // when it was written
    // version 3: the block you aim at (Block Outline)
    int32_t  bx, by, bz;
    uint32_t bface;              // 0..5 (Bedrock facing: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east), 255 = none
    int64_t  bAtMs;
    // version 4: px..pz / pyaw are where the game DRAWS you (between ticks, body yaw); how fast they move, so RF can
    // carry them over the few ms between BF's write and this pass (blocks / s, degrees / s)
    float    pvx, pvy, pvz, pyawRate;
};
/// Bedrock Forge's hit / jump events (Jump Circles, Hit Splash, Ripple, Shockwave), or null
BfFxEventsV1 const* fxEvents();   // (needs size >= v1; the v2 fields only when e->version >= 2)
bool showcasePose(float& blend, float& yaw, float& pitch, float& radius);

} // namespace rf::postfx
