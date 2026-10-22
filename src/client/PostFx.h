#pragma once
// Bedrock Forge -> Renderer Foundation bridge for the full-screen effects a RenderDragon material can't do
// (they need the whole frame): bloom, chromatic aberration, sharpening (CAS) and camera motion blur.
//
// The active shaderpack may ship  shaderpacks/<Pack>/postfx.json  e.g.
//   { "bloom": 0.12, "bloom_tiles": 6, "ca": 0.0025, "ca_style": 0, "cas": 0.5, "motion_blur": 1.25 }
// BF exports the parameters (plus the camera rotation / FOV that motion blur needs) through BF_GetPostFx();
// Renderer Foundation's Present hook reads them every frame and runs the passes on the swapchain image.
#include <cstdint>
#include <filesystem>

extern "C" {
struct BfPostFxV1 {
    uint32_t size;     // sizeof(BfPostFxV1)
    uint32_t version;  // 1
    uint32_t active;   // 1 while a world is on screen (HUD visible) and the pack asked for any effect
    uint32_t frame;    // increments every HUD frame
    float    bloom;    // mix weight of the blurred scene (Hysteria: 0.12 * BLOOM_INTENSITY)
    float    bloomTiles; // number of mip tiles averaged (Hysteria 6)
    float    ca;       // chromatic aberration strength (Cursed Fog 0.0025, Insanity 0.004)
    float    caStyle;  // 0 = Cursed Fog (r/b offset by distance from centre), 1 = Insanity (4 taps toward the centre)
    float    cas;      // AMD CAS sharpening (Hysteria 0.5)
    float    motionBlur; // Cursed Fog MOTION_BLURRING_STRENGTH (0 = off)
    float    yaw, pitch; // camera rotation, degrees
    float    fov;      // vertical field of view, degrees
    float    reserved[5];
};
__declspec(dllexport) BfPostFxV1 const* BF_GetPostFx();
}

namespace bedrock_forge::client::postfx {
/// Read shaderpacks/<active pack>/postfx.json (call after shaderpacks::install).
void load(std::filesystem::path const& packsDir, std::string const& activePack);
/// Called once per HUD frame with the local player's rotation (degrees).
void hudFrame(float pitch, float yaw);
/// Called from the FOV hook with the final FOV.
void noteFov(float fov);
} // namespace bedrock_forge::client::postfx
