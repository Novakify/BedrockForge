// Renderer Foundation - Spatial GUI: third-person "look at the screen" camera, ported 1:1 from the Java mod
// Spatial GUI (tastytrash, CameraMixin.calculateCameraTransform / applyCameraUpdate, default config).
// Original code for Renderer Foundation (no LeviLamina types here; see SpatialCamera.cpp).
#pragma once

namespace rf::spatialcam {

// Java defaults (SpatialGUIConfig): third-person camera + screen placement
inline constexpr float kCamDistance    = 1.7f;  // cameraDistance (behind the player's feet, along the facing)
inline constexpr float kCamSide        = -1.2f; // cameraSideOffset (+ = player's left)
inline constexpr float kCamHeight      = 1.5f;  // cameraHeightOffset (above the feet)
inline constexpr float kCamPitch       = 10.0f; // cameraTargetPitch (degrees, + = down)
inline constexpr float kTransitionMs   = 300.0f;
inline constexpr float kTransitionSkip = 0.15f;
inline constexpr float kParallaxYaw    = 0.1f;  // thirdPersonMouseSensitivityYaw   (x 90 deg)
inline constexpr float kParallaxPitch  = 0.03f; // thirdPersonMouseSensitivityPitch (x 180 deg)
inline constexpr float kScreenDistance = 2.5f;  // screenDistance (from the camera, along the player's facing)
inline constexpr float kScreenSide     = -0.6f; // screenSideOffset (+ = player's left)
inline constexpr float kScreenHeight   = -0.4f; // screenHeightOffset
inline constexpr float kScreenYaw      = 160.0f; // screenYawOffset (180 = squarely facing the player)
inline constexpr float kScreenScale    = 3.5f;  // screenScale (world height of the full-window GUI texture)
inline constexpr float kBaselineFov    = 70.0f; // autoFovTuning
inline constexpr float kScreenPower    = 1.2f;
inline constexpr float kSideMult       = -0.3f;
inline constexpr float kDistMult       = 0.8f;
inline constexpr float kTpScreenMult   = 0.82f;
inline constexpr float kOpenMs         = 300.0f; // openAnimationDurationMs, easing Back
inline constexpr float kStartScale     = 0.75f;  // animationStartScalePercent
inline constexpr float kFadeMs         = 150.0f; // fadeDurationMs

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
/// Client thread: a container screen opened/closed. Switches the game to third person and starts the glide.
void setActive(bool on);
bool active();
View view();
/// Diagnostics text (panel_diag.txt).
void appendDiag(char* buf, unsigned long long cap);

} // namespace rf::spatialcam
