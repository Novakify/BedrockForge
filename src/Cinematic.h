// Renderer Foundation - Cinematic Interactions port (client-only mode), built into the Spatial GUI mod.
// Port of the Java mod Cinematic Interactions by Southware (github.com/dcvke/CinematicInteractions, MIT): its
// "client-only mode" (Scene.beginClientOnly + CameraRig + Reach + the per-station classes), where the server's own
// menu stays open and every item move is an ordinary menu click.
//
// Bedrock side (shared with Spatial GUI, see SpatialPanel.h / SpatialCamera.h):
//   * screen detection: SceneStack::update reports the top screen; a station screen (crafting table, furnace, ...)
//     whose per-station mode is "Cinematic" starts a scene instead of the floating Spatial GUI screen
//   * the vanilla screen stays open but hidden (the UI redirect draws it into our own target, never on screen)
//   * camera: the scene pose is written into the camera component right before LevelRendererCamera::preRenderUpdate
//   * the hidden screen's slots are found in the redirected UI image (slot detection); a click on a table cell, the
//     floating result or the inventory drawer is a mouse event remapped onto that slot of the hidden screen (GameInput
//     v2 absolute position), so vanilla does the actual pick up / put down / shift / drag
//   * items on the block and in the drawer are the hidden screen's own slot pixels, drawn in the world
// Original C++ for Renderer Foundation; the Java numbers (poses, timings, sizes) are kept as in the original.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;
struct D3D12_CPU_DESCRIPTOR_HANDLE;

namespace rf::cine {

enum class Kind : int { Table = 0, Furnace, Smoker, BlastFurnace, Anvil, Grindstone, Smithing, Stonecutter, Loom, Cartography, Enchanting, Count };
/// Per-station presentation (F6): the Cinematic scene, the Spatial GUI floating screen, or the plain vanilla screen.
enum class Mode : int { Cinematic = 0, Spatial = 1, Vanilla = 2 };
/// What the open container screen gets.
enum class Route : int { None = -1, Cinematic = 0, Spatial = 1, Vanilla = 2 };

char const* kindName(Kind k);  // "crafting table", ...
char const* kindKey(Kind k);   // "table", "furnace", ... (config keys, = Java Settings.key)

struct Config {
    bool enabled = true;                         // master toggle: Cinematic scenes
    int  mode[int(Kind::Count)] = {};            // Mode per station (default all Cinematic)
    // Java Settings (cinematic-client.toml), same names, ranges and defaults
    int  approachSpeed = 100, leaveSpeed = 100, leanStrength = 100, sceneZoom = 100;
    int  quickMode = 1, quickWindow = 12;
    bool letterbox = true, livingCamera = true, cameraShake = true, screenFlash = true, reduceMotion = false, controlHints = true;
    // Bedrock additions
    bool hideHand      = true;  // Java draws its own arms instead of the vanilla hand; we hide the vanilla hand
    bool writeFov      = true;  // narrow the view to the station's lean FOV (camera component field of view)
    bool hoverTargets  = false; // keep the hidden screen's cursor on the hovered slot (off: only while a button is held)
    bool fullLean      = true;  // single player: the camera leans in like the Java mod with its server side (no 0.35 tether)
    int  armStyle      = 0;     // Java armStyle: 0 stretching arms, 1 natural reach (bent), 2 floating forearms
    bool drawArms      = true;  // the player's own skin arms reach into the scene (Hands / ArmMesh)
    bool cubeItems     = true;  // block items stand as little cubes (unwrapped from their inventory icon)
    bool dumpUi        = false; // write the hidden screen + found slots to logs/uicapture/cinematic_*.bmp/.txt
    float keyTolerance = 0.035f;
};
Config& cfg();
void    loadConfig(std::filesystem::path const& configDir);
void    saveConfig();

// ---- client thread (SceneStack::update via SpatialPanel::noteScreen)
/// The set of top screens changed. Starts / ends scenes; returns how the open container screen is shown.
/// `containerTop` = the container screen on top of a stack ("" = none).
Route onScreen(std::string const& containerTop);
/// Every SceneStack update while a scene runs.
void tick();
/// A scene is running (any phase, including the closing shot after the screen went away).
bool active();
/// The hidden screen must be redirected (scene with the screen still open).
bool wantsRedirect();

// ---- render-prep thread (SpatialCamera applyComponent)
struct Pose {
    float pos[3];
    float yaw, pitch, roll; // degrees, Minecraft convention
};
/// eye = live eye position, yaw/pitch = live look. Returns true with the scene camera pose; fovScale multiplies the
/// game's field of view (1 = unchanged).
bool cameraPose(float const eye[3], float yaw, float pitch, Pose& out, float& fovScale);
/// Projection of the world camera this frame (tan(fovY/2), aspect).
void noteProjection(float tanHalf, float aspect, float fovYDeg);

// ---- present thread (SpatialPanel::record)
/// Draws the scene over the back buffer (bbRtv bound by the caller). `ui` = the redirected UI image (PIXEL_SHADER_RESOURCE),
/// `uiFresh` = it holds this frame's screen. Returns false when nothing was drawn.
bool record(ID3D12Device* device, ID3D12GraphicsCommandList* list, ID3D12Resource* ui, bool uiFresh, unsigned w, unsigned h,
            D3D12_CPU_DESCRIPTOR_HANDLE const& bbRtv);
/// ImGui frame: hint line etc.
void drawOverlay();
/// F6 section.
void drawUi();

// ---- input (GameInput mouse readings, WM_* coordinates)
/// Client pixel of the real cursor -> client pixel the hidden screen should see. true = remapped.
bool mapClient(int w, int h, int& x, int& y, bool live);
/// Mouse buttons the hidden screen should see (GameInput buttons bitmask).
unsigned filterButtons(unsigned buttons);

/// Diagnostics (panel_diag.txt).
void appendDiag(char* b, unsigned long long cap);

} // namespace rf::cine
