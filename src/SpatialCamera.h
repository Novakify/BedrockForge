// Renderer Foundation - Spatial GUI: third-person "look at the screen" camera, ported 1:1 from the Java mod
// Spatial GUI (tastytrash, CameraMixin.calculateCameraTransform / applyCameraUpdate, default config).
// Original code for Renderer Foundation (no LeviLamina types here; see SpatialCamera.cpp).
#pragma once
#include <filesystem>

namespace rf::spatialcam {

// All options of the Java mod's config screen (SpatialGUIConfig), same names and defaults. Saved to
// config/SpatialGUI.json, edited in the F6 menu.
enum class Easing : int { Back = 0, Cubic, Elastic, Bounce, Expo, Quad, Quart };
struct Config {
    // General
    bool  enabled           = true;
    bool  fpInventory       = false; // firstPersonModeInventory
    bool  fpContainers      = false; // firstPersonModeContainers
    bool  autoScaleByFov    = true;
    bool  mirrorThirdPerson = false;
    // Rendering
    int   screenAlpha = 255;
    // TP Screen
    float screenDistance = 2.5f, screenSide = -0.6f, screenHeight = -0.4f, screenYaw = 160.0f, screenPitch = 0.0f, screenScale = 3.5f;
    // FP Screen
    float fpDistance = 1.5f, fpSide = 0.0f, fpHeight = 0.0f, fpYaw = 180.0f, fpPitch = 0.0f, fpScale = 1.8f;
    // TP Camera
    float camDistance = 1.7f, camSide = -1.2f, camHeight = 1.5f, camPitch = 10.0f;
    int   transitionMs = 300, transitionSkipPct = 15;
    float tpSensYaw = 0.1f, tpSensPitch = 0.03f;
    bool  tpParallaxOff = false, lerpXRot = true;
    // FP Camera
    float fpSensYaw = 0.4f, fpSensPitch = 0.12f;
    bool  fpParallaxOff = false;
    int   fpPitchClamp = 40;
    // Animations
    bool   fade = true;
    int    fadeMs = 150;
    bool   scaleAnim = true;
    int    openMs = 300;
    Easing easing = Easing::Back;
    int    startScalePct = 75;
    // Auto FOV tuning
    float baselineFov = 70.0f, screenPower = 1.2f, sideMult = -0.3f, distMult = 0.8f, tpScreenMult = 0.82f;
};
Config& cfg();
void    loadConfig(std::filesystem::path const& configDir);
void    saveConfig();
void    resetConfig();
float   applyEasing(Easing e, float t);

/// What the panel needs from the camera this frame (render thread).
struct View {
    bool  valid    = false; // world camera seen (projection known)
    bool  overridden = false; // our third-person pose is in effect
    float tanHalf  = 0.7f;  // tan(vertical fov / 2) of the world camera
    float aspect   = 16.0f / 9.0f;
    float fovYDeg  = 70.0f;
    float yawRel   = 0.0f;  // camera yaw minus player yaw (degrees, + = turned right)
    float pitch    = 0.0f;  // camera pitch (degrees, + = down)
    float guiScale = 0.0f;  // Bedrock UI pixels per GUI unit (GuiData::mGuiScale), 0 = unknown
    bool  firstPerson = false; // first person mode (screen placed along the look direction at open)
    float fpYaw0 = 0.0f, fpPitch0 = 0.0f; // first person: player yaw / clamped pitch when the screen opened (degrees)
    float playerYaw = 0.0f;
};

/// Java SpatialGUIConfig.calculateAutoGuiScale(width, height): largest scale keeping >= 320 x 240 GUI units.
inline int javaAutoGuiScale(int w, int h) {
    if (w <= 0 || h <= 0) return 1;
    int s = 1;
    while (w / (s + 1) >= 320 && h / (s + 1) >= 240) ++s;
    if (s == 1 && h >= 430 && w >= 640) s = 2;
    return s;
}

void install();
void uninstall();
/// Client thread: a container screen opened/closed (inventory = the player inventory screen). Third person mode
/// switches the game to third person and starts the glide; first person mode keeps the view and only turns it.
void setActive(bool on, bool inventory = false);
bool active();
View view();
/// Diagnostics text (panel_diag.txt).
void appendDiag(char* buf, unsigned long long cap);

} // namespace rf::spatialcam
