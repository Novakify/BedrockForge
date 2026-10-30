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
    bool  autoGuiScale      = true; // autoCalculateGuiScale
    int   guiScale          = 4;
    bool  hideHotbar        = false;
    // Screens (SpatialGUIClient.shouldHookScreen)
    bool  allScreens = false, pauseScreen = false, mostContainers = true, books = true, crafting = true, furnaces = true,
         anvils = true, enchanting = true, beacons = true, brewing = true, villagerTrading = true, chests = true,
         shulkerBoxes = true, hoppers = true, dispensers = true, grindstone = true, smithing = true, cartography = true,
         loom = true, stonecutter = true, lectern = true, inventory = true, creativeInventory = true;
    // Rendering
    int   renderScalePct = 100;
    bool  linearFiltering = true, anisotropicFiltering = true;
    float recipeBookShrink = 1.5f;
    bool  hideHandsFp = false, hideShieldFp = true, swingArmFp = false;
    int   screenAlpha = 255;
    // TP Screen
    float screenDistance = 2.5f, screenSide = -0.6f, screenHeight = -0.4f, screenYaw = 160.0f, screenPitch = 0.0f, screenScale = 3.5f;
    // FP Screen
    float fpDistance = 1.5f, fpSide = 0.0f, fpHeight = 0.0f, fpYaw = 180.0f, fpPitch = 0.0f, fpScale = 1.8f;
    bool  curvedScreen = false;
    int   curvedArc    = 30;
    // TP Camera
    float camDistance = 1.7f, camSide = -1.2f, camHeight = 1.5f, camPitch = 10.0f;
    int   transitionMs = 300, transitionSkipPct = 15;
    float avatarBaseYaw = 30.0f, avatarBodyRot = 20.0f; // avatarBaseYawOffset / avatarBodyRotationOffset
    float wideScreenShift = 0.35f; // Bedrock addition: wide screens (recipe book) slide the camera so the player moves left
    float tpSensYaw = 0.1f, tpSensPitch = 0.03f;
    bool  tpParallaxOff = false, lerpXRot = false;
    // FP Camera
    float fpSensYaw = 0.4f, fpSensPitch = 0.12f;
    bool  fpParallaxOff = false;
    int   fpPitchClamp = 40;
    bool  crosshairFp = true, keepFpAngle = true, inheritOrigin = true; // useCrosshairForFirstPerson, keepFirstPersonCameraAngle, inheritScreenOriginOnSwap
    // Animations
    bool   fade = true;
    int    fadeMs = 150;
    bool   scaleAnim = true;
    int    openMs = 300;
    Easing easing = Easing::Back;
    int    startScalePct = 75;
    // Auto FOV tuning
    float baselineFov = 70.0f, screenPower = 1.2f, sideMult = -0.3f, distMult = 0.8f, tpScreenMult = 0.82f;
    // Appearance (Java 2.x: overrideFov / targetFov, blurWorldBackground / worldBlurStrength, dropShadow*)
    // Bedrock FOVs run far wider than Java's 70 (120+ vertical seen): the panel scene eases to the Java FOV so the
    // camera framing (player big at the left, panel square on at the right) matches the Java mod.
    bool  overrideFov = true;
    int   targetFov   = 70;
    bool  worldBlur = true;
    int   worldBlurStrength = 5;
    bool  blurKeepPlayer = false; // Java 2.x draws the player sharp after the blur; the reference clips blur it too
    bool  dropShadow = true;
    int   dropShadowAlpha = 120;
    bool  closeAnim = true; // enableCloseAnimation: the panel shrinks / fades out and the camera glides back
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
    bool  firstPerson = false; // first person mode (configured, or forced because the third person spot is inside a block) (screen placed along the look direction at open)
    float fpYaw0 = 0.0f, fpPitch0 = 0.0f; // first person: player yaw / clamped pitch when the screen opened (degrees)
    float playerYaw = 0.0f;
    // third person pose of this frame (world space, Minecraft yaw/pitch degrees): keeps the player sharp in the blur
    bool  havePose = false;
    float camPos[3]{}, camYaw = 0.0f, camPitch = 0.0f, feet[3]{};
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
/// Client thread, every screen-stack tick: finishes the close glide (restores the perspective).
void clientTick();
/// The open screen is wide (recipe book shown): the third person camera eases sideways (wideScreenShift).
void setWideScreen(bool wide);
View view();
/// Diagnostics text (panel_diag.txt).
void appendDiag(char* buf, unsigned long long cap);

} // namespace rf::spatialcam
