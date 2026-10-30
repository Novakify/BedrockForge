// Renderer Foundation - Cinematic Interactions port (see Cinematic.h). Original C++ for Renderer Foundation; the scene
// logic, poses and timings follow the Java mod Cinematic Interactions (Southware, MIT) in its client-only mode:
// Scene.beginClientOnly / frame / leave, CameraRig.override / tether / breathe / fov, Reach.planHere,
// TableStation (lean, cells, result), SceneScreen (inventory drawer), Overlay (letterbox).
#include "Cinematic.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "d3dcompiler.lib")
#include <wrl/client.h>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "CinematicGame.h"
#include "FxTextures.h"
#include "HammerModel.h"
#include "LLShim.h"
#include "SpatialCamera.h"
#include "UiCapture.h"

namespace rf::cine {
namespace {
using Microsoft::WRL::ComPtr;
constexpr double kPi  = 3.14159265358979323846;
constexpr float  kDeg = 0.017453292f;

// ================================================================ small math
struct V3 {
    double x = 0, y = 0, z = 0;
    V3 operator+(V3 const& o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator-(V3 const& o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator*(double s) const { return {x * s, y * s, z * s}; }
    double dot(V3 const& o) const { return x * o.x + y * o.y + z * o.z; }
    double len() const { return std::sqrt(x * x + y * y + z * z); }
    V3 norm() const { double l = len(); return l > 1e-12 ? V3{x / l, y / l, z / l} : V3{}; }
};
V3 lerp(V3 a, V3 b, double t) { return a + (b - a) * t; }
double clamp01(double t) { return t < 0 ? 0 : t > 1 ? 1 : t; }
double window(double t, double a, double b) { return clamp01((t - a) / (b - a)); }
double smoother(double t) { t = clamp01(t); return t * t * t * (t * (t * 6 - 15) + 10); }
double inOutCubic(double t) { t = clamp01(t); return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2; }
double inOutSine(double t) { return -(std::cos(kPi * clamp01(t)) - 1) / 2; }
double outCubic(double t) { t = clamp01(t); return 1 - std::pow(1 - t, 3); }
double outBack(double t, double o) { t = clamp01(t); double c3 = o + 1; return 1 + c3 * std::pow(t - 1, 3) + o * std::pow(t - 1, 2); }
double inCubic3(double t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return t * t * t; }
double hump(double t) { return std::sin(kPi * clamp01(t)); }
double approach(double cur, double target, double rate, double dt) { return target + (cur - target) * std::exp(-rate * dt); }
float  wrapDeg(float a) { a = std::fmod(a, 360.0f); if (a >= 180.0f) a -= 360.0f; if (a < -180.0f) a += 360.0f; return a; }
/// Vec3.directionFromRotation(pitch, yaw)
V3 dirFromRot(double pitchDeg, double yawDeg) {
    double p = pitchDeg * kDeg, y = yawDeg * kDeg;
    return {-std::sin(y) * std::cos(p), -std::sin(p), std::cos(y) * std::cos(p)};
}
void lookAngles(V3 look, float& yaw, float& pitch) {
    yaw   = float(std::atan2(-look.x, look.z) / kDeg);
    pitch = float(-std::atan2(look.y, std::hypot(look.x, look.z)) / kDeg);
}

// Directions as in Java (Direction.getNormal / toYRot / getClockWise)
enum Dir { North = 0, East, South, West };
V3    dirVec(Dir d) { switch (d) { case North: return {0, 0, -1}; case East: return {1, 0, 0}; case South: return {0, 0, 1}; default: return {-1, 0, 0}; } }
Dir   opposite(Dir d) { return Dir((int(d) + 2) % 4); }
Dir   clockWise(Dir d) { return Dir((int(d) + 1) % 4); }
float toYRot(Dir d) { switch (d) { case South: return 0; case West: return 90; case North: return 180; default: return 270; } }
int   stepX(Dir d) { return int(dirVec(d).x); }
int   stepZ(Dir d) { return int(dirVec(d).z); }

struct JPose { V3 pos; float yaw = 0, pitch = 0, roll = 0; };

// ================================================================ config
Config gCfg;
std::filesystem::path gCfgFile;

const char* kKeys[] = {"table", "furnace", "smoker", "blast_furnace", "anvil", "grindstone", "smithing", "stonecutter", "loom", "cartography", "enchanting"};
const char* kNames[] = {"Crafting table", "Furnace", "Smoker", "Blast furnace", "Anvil", "Grindstone", "Smithing table", "Stonecutter", "Loom", "Cartography table", "Enchanting table"};

double pct(int v) { return v / 100.0; }
bool livingCamera() { return gCfg.livingCamera && !gCfg.reduceMotion; }
bool cameraShake() { return gCfg.cameraShake && !gCfg.reduceMotion; }
bool letterbox() { return gCfg.letterbox && !gCfg.reduceMotion; }

// ================================================================ stations
// Which block a screen belongs to (Bedrock block names), and whether this build has its scene.
bool kindOfBlock(std::string const& name, Kind& k) {
    auto is = [&](char const* n) { return name == n; };
    if (is("minecraft:crafting_table")) { k = Kind::Table; return true; }
    if (is("minecraft:furnace") || is("minecraft:lit_furnace")) { k = Kind::Furnace; return true; }
    if (is("minecraft:smoker") || is("minecraft:lit_smoker")) { k = Kind::Smoker; return true; }
    if (is("minecraft:blast_furnace") || is("minecraft:lit_blast_furnace")) { k = Kind::BlastFurnace; return true; }
    if (is("minecraft:anvil") || is("minecraft:chipped_anvil") || is("minecraft:damaged_anvil")) { k = Kind::Anvil; return true; }
    if (is("minecraft:grindstone")) { k = Kind::Grindstone; return true; }
    if (is("minecraft:smithing_table")) { k = Kind::Smithing; return true; }
    if (is("minecraft:stonecutter_block") || is("minecraft:stonecutter")) { k = Kind::Stonecutter; return true; }
    if (is("minecraft:loom")) { k = Kind::Loom; return true; }
    if (is("minecraft:cartography_table")) { k = Kind::Cartography; return true; }
    if (is("minecraft:enchanting_table")) { k = Kind::Enchanting; return true; }
    return false;
}
bool isFurnace(Kind k) { return k == Kind::Furnace || k == Kind::Smoker || k == Kind::BlastFurnace; }
bool sceneAvailable(Kind k) { return k == Kind::Table || isFurnace(k) || k == Kind::Anvil || k == Kind::Grindstone || k == Kind::Stonecutter || k == Kind::Smithing || k == Kind::Cartography || k == Kind::Loom || k == Kind::Enchanting; }

// ---------------------------------------------------------------- FurnaceLayout (furnace/FurnaceLayout.java, px = 1/16)
// Zones (click targets) and beds (where items rest) per slot: 0 input, 1 fuel, 2 result. top = on the top face.
struct FZone { bool top; double u0, u1, v0, v1; double hl[4]; };
struct FBed  { bool top; double u0, u1, floor, depth; int maxCopies; float tilt; };
struct FGlow { bool top; double u0, u1, v0, v1; bool fire; };
struct FLayout { FZone zone[3]; FBed bed[3]; FGlow glow[2]; double cookGauge[4], burnGauge[4]; };
constexpr double px16(double p) { return p / 16.0; }
const FLayout kFurnaceL = {
    {{false, 0.11, 0.5, 0.5, 0.92, {px16(3), 0.5, px16(9), px16(13)}},
     {false, 0.11, 0.89, 0.0, 0.39, {px16(3), px16(13), px16(1), px16(5)}},
     {false, 0.5, 0.89, 0.5, 0.92, {0.5, px16(13), px16(9), px16(13)}}},
    {{false, 0.22, 0.47, px16(9), 0.035, 3, 0}, {false, 0.26, 0.74, px16(1), 0.035, 4, 0}, {false, 0.53, 0.78, px16(9), 0.035, 3, 0}},
    {{false, px16(3), px16(13), px16(1), px16(5), true}, {false, px16(3), px16(13), px16(9), px16(13), false}},
    {px16(3), px16(13), 0.405, 0.43}, {px16(3), px16(13), 0.012, 0.037}};
const FLayout kSmokerL = {
    {{false, 0.1, 0.47, 0.46, 0.86, {px16(3), px16(7.5), px16(8), px16(12)}},
     {false, 0.08, 0.92, 0.0, 0.46, {px16(2), px16(14), px16(1), px16(7)}},
     {false, 0.47, 0.84, 0.46, 0.86, {px16(7.5), px16(12), px16(8), px16(12)}}},
    {{false, 0.24, 0.43, px16(8), 0.035, 3, 0}, {false, 0.22, 0.78, px16(1), 0.035, 4, 0}, {false, 0.5, 0.7, px16(8), 0.035, 3, 0}},
    {{false, px16(2), px16(14), px16(1), px16(7), true}, {false, px16(3), px16(12), px16(8), px16(12), false}},
    {px16(3), px16(13), 0.452, 0.482}, {px16(3), px16(13), 0.012, 0.037}};
const FLayout kBlastL = {
    {{true, 0.06, 0.94, 0.12, 0.94, {0.22, 0.78, 0.3, 0.86}},
     {false, 0.12, 0.88, 0.16, 0.52, {px16(4), px16(12), px16(5), px16(8)}},
     {false, 0.12, 0.88, 0.52, 0.86, {px16(3), px16(13), px16(9), px16(10.5)}}},
    {{true, 0.32, 0.68, 0.58, 0.045, 3, 62}, {false, 0.32, 0.68, px16(5), 0.035, 3, 0}, {false, 0.3, 0.7, px16(9), 0.035, 3, 0}},
    {{false, px16(4), px16(12), px16(5), px16(8), true}, {true, 0.26, 0.74, 0.34, 0.82, false}},
    {px16(4), px16(12), 0.24, 0.265}, {px16(4), px16(12), 0.012, 0.037}};
FLayout const& furnaceLayout(Kind k) { return k == Kind::Smoker ? kSmokerL : k == Kind::BlastFurnace ? kBlastL : kFurnaceL; }
/// FurnaceStation.Staging: how the player and camera settle (LOW: crouched at a furnace or smoker, TALL: the blast furnace)
struct Staging { bool crouch; double out, up, side, lookV; float roll, bodyPitch; bool restTop; double restU, restV, restOut; };
const Staging kLow  = {true, 0.9, 0.5, 0.1, 0.44, -3.5f, 32.0f, false, 0.2, 0.09, 0.36};
const Staging kTall = {false, 0.7, 1.15, 0.08, 0.97, -2.0f, 45.0f, true, 0.15, 0.13, 0.06};

/// A station of the scene (ClientStation + the subclass numbers).
struct Station {
    Kind kind = Kind::Table;
    int  bx = 0, by = 0, bz = 0; // block
    Dir  side = South;            // side worked from (from the block towards the player)
    V3   top;                     // TableStation.top
    bool lit = false;             // a lit furnace (block name lit_*)
    V3   wheel;                   // GrindstoneStation.wheel (its top is `top`)
    std::vector<V3> shelves;      // EnchantStation.shelves: the bookshelves that empower the table (centres)
    V3 center() const {
        return kind == Kind::Table || kind == Kind::Anvil || kind == Kind::Grindstone || kind == Kind::Stonecutter || kind == Kind::Smithing || kind == Kind::Cartography || kind == Kind::Loom || kind == Kind::Enchanting ? top : V3{bx + 0.5, by + 0.5, bz + 0.5};
    }
    /// FurnaceZones.point / top: a point on the front (u left->right, v bottom->top, out > 0 in front) or the top
    /// (u left->right, v front edge -> back edge, out = up)
    V3 fpoint(bool onTop, double u, double v, double out) const {
        V3 f = sideV(), r = right();
        if (!onTop) return {bx + 0.5 + f.x * (0.5 + out) + r.x * (u - 0.5), by + v, bz + 0.5 + f.z * (0.5 + out) + r.z * (u - 0.5)};
        return {bx + 0.5 + f.x * (0.5 - v) + r.x * (u - 0.5), by + 1.0 + out, bz + 0.5 + f.z * (0.5 - v) + r.z * (u - 0.5)};
    }
    Staging const& staging() const { return kind == Kind::BlastFurnace ? kTall : kLow; }
    V3 sideV() const { return dirVec(side); }
    V3 forward() const { return dirVec(opposite(side)); }
    V3 right() const { return dirVec(clockWise(opposite(side))); }
    V3 workPoint() const { return center(); }
    bool workOnTop() const { return !isFurnace(kind); }
    double leanFov() const { return kind == Kind::Grindstone ? 64 : kind == Kind::Enchanting ? 68 : 66; }
    // TableStation.lean / FurnaceStation.lean
    JPose lean() const {
        if (isFurnace(kind)) {
            Staging const& sg = staging();
            V3    eye = center() + sideV() * sg.out + V3{0, sg.up, 0} + right() * sg.side;
            JPose p;
            p.pos = eye;
            lookAngles(fpoint(false, 0.5, sg.lookV, 0) - eye, p.yaw, p.pitch);
            p.roll = sg.roll;
            return p;
        }
        if (kind == Kind::Enchanting) { // EnchantStation.lean
            V3    eye = top + sideV() * 1.15 + V3{0, 1.0, 0};
            JPose p;
            p.pos = eye;
            lookAngles(top + V3{0, 0.5, 0} - eye, p.yaw, p.pitch);
            return p;
        }
        if (kind == Kind::Loom) { // LoomStation.lean
            V3    eye = top + sideV() * 0.8 + right() * 0.08 + V3{0, 0.72, 0};
            JPose p;
            p.pos = eye;
            lookAngles(top + sideV() * -0.02 + right() * 0.12 + V3{0, 0.05, 0} - eye, p.yaw, p.pitch);
            return p;
        }
        if (kind == Kind::Stonecutter) { // StonecutterStation.lean
            V3    eye = top + sideV() * 1.0 + V3{0, 0.95, 0};
            JPose p;
            p.pos = eye;
            lookAngles(top + sideV() * -0.05 + V3{0, 0.25, 0} - eye, p.yaw, p.pitch);
            p.roll = 1.5f;
            return p;
        }
        if (kind == Kind::Grindstone) { // GrindstoneStation.lean
            V3    eye = wheel + sideV() * 0.95 + V3{0, 1.0, 0};
            JPose p;
            p.pos = eye;
            lookAngles(wheel + V3{0, 0.32, 0} - eye, p.yaw, p.pitch);
            p.roll = -1.5f;
            return p;
        }
        if (kind == Kind::Anvil) { // AnvilStation.lean
            V3    eye = top + sideV() * 0.66 + V3{0, 0.7, 0};
            JPose p;
            p.pos = eye;
            lookAngles(top + sideV() * -0.06 + V3{0, 0.12, 0} - eye, p.yaw, p.pitch);
            return p;
        }
        // TableStation.lean; SmithingStation.lean is the same with its own numbers (LEAN_BACK 0.7, LEAN_UP 0.62, look 0.04 beyond)
        double LEAN_BACK = 0.86, LEAN_UP = 0.72, LOOK_BACK = -0.04;
        if (kind == Kind::Smithing) { LEAN_BACK = 0.7; LEAN_UP = 0.62; LOOK_BACK = -0.04; }
        if (kind == Kind::Cartography) { LEAN_BACK = 0.7; LEAN_UP = 0.64; LOOK_BACK = -0.02; }
        JPose p;
        p.pos   = top + sideV() * LEAN_BACK + V3{0, LEAN_UP, 0};
        p.pitch = float(std::atan2(LEAN_UP, LEAN_BACK - LOOK_BACK) / kDeg);
        p.yaw   = toYRot(opposite(side));
        return p;
    }
    JPose view() const { return lean(); } // swing is always 0 in client-only mode (the player is not moved)
    std::vector<V3> workSamples() const {
        V3 work = workPoint();
        if (!workOnTop()) { // ClientStation.workSamples: just out of the work face
            V3 o = sideV() * 0.04, a = right() * 0.3, b{0, 0.3, 0};
            return {work + o, work + a + o, work - a + o, work + b + o, work - b + o};
        }
        V3 lift{0, 0.04, 0}, a = right() * 0.3, b = sideV() * 0.3;
        return {work + lift, work + a + lift, work - a + lift, work + b + lift, work - b + lift};
    }
    // TableGrid / TableLayout.VANILLA
    static constexpr double CELL = 3.0 / 16.0, GRID_MIN = 3.5 / 16.0;
    V3 cellPoint(int world) const { return {bx + GRID_MIN + CELL * (world % 3 + 0.5), by + 1.0, bz + GRID_MIN + CELL * (world / 3 + 0.5)}; }
    int worldIndex(int row, int col) const {
        Dir f = opposite(side), r = clockWise(f);
        int dx = stepX(f) * (1 - row) + stepX(r) * (col - 1);
        int dz = stepZ(f) * (1 - row) + stepZ(r) * (col - 1);
        return (1 + dx) + (1 + dz) * 3;
    }
    int cellAt(double lx, double lz) const {
        double mx = GRID_MIN + 3 * CELL;
        if (lx < GRID_MIN || lz < GRID_MIN || lx >= mx || lz >= mx) return -1;
        return int((lx - GRID_MIN) / CELL) + int((lz - GRID_MIN) / CELL) * 3;
    }
    V3 resultPosition(double clock) const {
        double bob = std::sin(clock * 2.1) * 0.012;
        return top + forward() * 0.42 + V3{0, 0.17 + bob, 0};
    }
};

// ================================================================ hidden screen layout (slot detection)
constexpr int kButtons = 24;               // option buttons sampled (stonecutter recipes, ...)
constexpr int kSampledN = 11 + kButtons + 1; // 9 grid + result + the parked cursor + buttons + the loom's preview
struct Rect { float x = 0, y = 0, w = 0, h = 0; float cx() const { return x + w * 0.5f; } float cy() const { return y + h * 0.5f; } };
struct Layout {
    bool  valid = false;
    int   W = 0, H = 0;
    long  serial = 0;
    float slot = 0;                 // normal slot size (px)
    Rect  hotbar[9], inv[27];       // inventory rows (inv: 3 rows of 9, top first)
    Rect  grid[9];                  // crafting grid, recipe order (row-major from the top)
    Rect  result;                   // crafting result
    bool  hasGrid = false, hasResult = false;
    int   nRoles = 0;                // non-table stations: grid[0..nRoles) = the menu's slots in Java order
    Rect  board;                     // option buttons (stonecutter recipes, loom patterns, enchantments), when found
    Rect  buttons[kButtons];         // the option buttons themselves, reading order
    int   nButtons = 0;
    bool  hasBoard = false;
    Rect  preview;                   // the loom's banner preview (inside its black frame), right of the result
    bool  hasPreview = false;
    Rect  clue[3];                   // the enchanting window's clue tooltip text ("Sharpness I . . . ?"), when one shows
    bool  hasClue[3] = {};
    int   kind = 0;                  // Kind the layout was found for
    Rect  panel;                    // container panel (inventory rows' width)
    // a frame of the hidden screen counts when these hold: the top bevel of hotbar[0] / inv[0] is the dark line seen
    // at detection, and the panel just above it its colour (frames with the screen not drawn yet are left out)
    float probe[2][4] = {};         // per slot: line x, y, above x, y (pixels)
    float probeLine[3] = {}, probeAbove[3] = {};
    bool  hasProbe = false;
    float bg[3] = {0.545f, 0.545f, 0.545f}; // slot background (#8B8B8B)
    float hi[3] = {0.78f, 0.78f, 0.78f};    // hovered slot background
    int   found = 0;                // squares found
    std::string note = "not run";
};
Layout     gLayout;
std::mutex gLayoutMutex;

// ================================================================ scene state
enum class Phase { None, Intro, Active, Outro };
struct Scene {
    Phase   phase = Phase::None;
    Station st;
    double  time = 0, clock = 0, frameDt = 0;
    double  leanLength = 1.2;
    bool    quick = false;
    // CameraRig
    bool    haveLast = false;
    JPose   last;
    double  weight = 0, parallaxX = 0, parallaxY = 0;
    JPose   outroFrom;
    bool    haveOutroFrom = false;
    double  outroWeight = 1;
    // live view of this frame
    float   tanHalf = 0.7f, aspect = 16.0f / 9.0f, fovY = 70.0f, vanillaFovY = 70.0f;
    bool    projKnown = false;
    double  fovScaleNow = 1.0;
    // left station (quick return)
    int     leftX = 0, leftY = -9999, leftZ = 0;
    double  leftAt = -1000;
    // cursor / interaction (filled on the present thread)
    int     hoverCell = -1;     // world cell
    bool    hoverResult = false;
    int     hoverDrawer = -1;   // 0..8 hotbar, 9..35 pack
    bool    overPanel = false;
    double  expand = 0;
    bool    cursorEngaged = false;
    float   cursorStartX = 0, cursorStartY = 0;
    bool    screenOpen = false;
    // results seen (readback)
    double  resultChangedAt = -10;
    bool    cellFull[9] = {};
    bool    resultFull = false;
    bool    carrying = false;
    bool    carriedBlock = false;
    // the hidden slots' icons (readback): block items stand as cubes; bounding box of the icon inside its slot
    bool    cellBlock[9] = {};
    bool    hasCount[9] = {};      // the slot prints a stack count; its digits' box in 0..1 of the slot inside
    float   countBox[9][4] = {};
    bool    resultBlock = false;
    float   iconBox[kSampledN][4] = {};
    float   iconRgb[kSampledN][3] = {}; // mean colour of the icon (stone dust)
    bool    btnFull[kButtons] = {};
    bool    previewFull = false;    // the loom's preview shows a banner  // per sampled slot: x0 y0 x1 y1 in 0..1 of the slot inside
    // TableStation / Hands
    bool    haveAim = false;
    V3      aim;
    double  tapAt = -10, craftAt = -10;
    bool    leftWorks = false;
    // skin (read on the client thread when a scene begins)
    std::vector<unsigned char> skin;
    int     skinW = 0, skinH = 0;
    bool    skinSlim = false;
    long    skinSerial = 0;
    int     savedHideHand = -1;
    std::string refused;
    std::string states;
    bool        aimTop = false;  // FurnaceStation.aimFace == TOP
    // MenuStation move
    bool   moveOn = false, moveBeatDone = false, moveToInv = false;
    double moveAt = -10, moveLength = 1, moveBeat = 0;
    int    blowsDone = 0;
    bool   mainWasFull = false, addWasFull = false, mainWasBlock = false, addWasBlock = false;
    float  snapBox[kSampledN][4] = {};
    int    anvilTarget = 0;
    // GrindstoneStation
    int    grindTarget = 0, grindHover = -1, grindPlace = -1;
    bool   stripping = false;
    double spin = 0, run = 0;
    bool   cellGlint[9] = {};   // the slot's icon shimmers violet (enchanted)
    bool   glintWas[2] = {};
    bool   resultWasBlock = false;
    // StonecutterStation
    int    stoneTarget = 0, hoverShape = -1, selectedShape = -1, lastShapes = -1, cutShape = -1, fitIndex = 0;
    double scroll = 0, shapesAt = -10, fitAt = -10, cutLength = 1.05;
    bool   cutAll = false, btnBlock[kButtons] = {};
    float  btnRef[kButtons][3] = {}; // each button's own ground colour (keyed away like a slot's)
    // SmithingStation
    int    smithTarget = 0, smithHover = -1, smithPlace = -1, smithFx = 0;
    bool   wasFull[4] = {}, wasBlock[4] = {};
    float  molten[3] = {1, 0.5f, 0.16f};
    // CartographyStation
    int    cartTarget = 0, cartHover = -1, cartPlace = -1, cartWork = 0;
    bool   gleamed = false;
    // LoomStation
    int    loomTarget = 0, loomHover = -1, loomPattern = -1, loomPlace = -1, loomSelected = -1, loomWeave = -1, passesDone = 0;
    bool   boardShown = false, loomSnap2 = false;
    double board = 0;
    float  boardRect[4] = {}; // x y w h (client px) of the glass board as last drawn
    float  boardGui = 1;
    double grindPrevT = -1;
    // item tooltips: the hidden screen's own, found by resting its cursor on the slot a moment (a probe), kept per slot
    struct TipCache { bool ok = false, tried = false; float sig[7] = {}; double at = -10; Rect text; };
    TipCache tipCache[46];
    int    tipSlot = -1, tipSlotWas = -1, probe = -1;
    double tipSince = -10, probeAt = -10, holdUntil = -10;
    bool   probeForced = false;
    // EnchantStation
    int    enchTarget = 0, hoverOffer = -1, ritualOffer = 0, ritualLapis = 0, denyOffer = -1, lastHoverOffer = -1;
    bool   aimAir = false, burstDone = false, wasRune[18] = {};
    // the offers as they were before the hidden cursor rested on one (its button turns pink, its clue covers the cost):
    // copied (with the snapshot texture) whenever no offer is hovered; used while one is and through the rite
    bool   cleanOk = false, useClean = false, cleanFull[3] = {};
    float  cleanRgb[3][3] = {}, cleanRef[kButtons][3] = {};
    double cleanAt = -10, dirtyUntil = -10, hoverSince = -10, clueForced = -10;
    Rect   clueRect[3];
    bool   clueOk[3] = {};
    int    clueHover = -1;
    double offersAt = -10, burstAt = -10, denyAt = -10, glow = 0, lapisFocus = 0, enchTick = 0;
    long   offersPrint = -1;
    V3     ritualFrom;
    bool   resultFull3() const { return cellFull[2]; } // the menu's result slot (anvil, grindstone, cartography: slot 2)
    std::string lastRoute = "-";
    long    scenes = 0;
} gS;
std::mutex gM; // gS
double cameraKick();
double moveTime();
void   moveFrame();

LARGE_INTEGER gLastQpc{};

// ---------------------------------------------------------------- Reach.planHere (client-only: judge the real feet)
bool solidAt(int x, int y, int z) { return game::solid(x, y, z); }
/// Ray from a to b through solid blocks, ignoring the station block when the end point is inside it.
bool rayBlocked(V3 a, V3 b, Station const& st) {
    double L = (b - a).len();
    int    n = std::max(2, int(L / 0.05));
    for (int i = 1; i < n; ++i) {
        V3  p  = lerp(a, b, double(i) / n);
        int ix = int(std::floor(p.x)), iy = int(std::floor(p.y)), iz = int(std::floor(p.z));
        if (ix == st.bx && iy == st.by && iz == st.bz) continue;
        if (solidAt(ix, iy, iz)) return true;
    }
    return false;
}
bool planHere(Station const& st, V3 feet, V3 eye, std::string& why) {
    constexpr double SURFACE_HIGH = 1.42, FACE_HIGH = 1.85, LOW = -0.6, kFar = 1.5;
    V3     work = st.workPoint();
    double high = work.y - feet.y;
    double away = std::hypot(work.x - feet.x, work.z - feet.z);
    char   b[160];
    if (!(high >= LOW && high <= (st.workOnTop() ? SURFACE_HIGH : FACE_HIGH) && away <= kFar)) {
        snprintf(b, sizeof b, "the work is out of reach: %.2f over the feet, %.2f away", high, away);
        why = b;
        return false;
    }
    if (int(std::floor(feet.x)) == st.bx && int(std::floor(feet.z)) == st.bz && feet.y >= st.by + 0.9 && feet.y <= st.by + 1.6) {
        why = "standing on the block itself";
        return false;
    }
    V3 off = feet - work;
    if (off.x * st.sideV().x + off.z * st.sideV().z < 0.2) {
        why = std::string("standing beside or behind the side it is worked from");
        return false;
    }
    for (V3 const& p : st.workSamples()) {
        if (rayBlocked(eye, p, st)) { why = "work hidden from the real eye position"; return false; }
    }
    why.clear();
    return true;
}

/// ClientStation.freeSide: the side nearest the player with room to stand.
Dir freeSide(Station const& st, V3 feet, unsigned allowed = 15) {
    double dx = feet.x - (st.bx + 0.5), dz = feet.z - (st.bz + 0.5);
    for (int pass = 0; pass < 2; ++pass) {
        int    best  = -1;
        double score = -1e9;
        for (int d = 0; d < 4; ++d) {
            if (!(allowed & (1u << d))) continue;
            double s  = stepX(Dir(d)) * dx + stepZ(Dir(d)) * dz;
            int    fx = st.bx + stepX(Dir(d)), fz = st.bz + stepZ(Dir(d));
            int    fy = pass == 0 ? int(std::floor(feet.y)) : st.by;
            bool   room = !solidAt(fx, fy, fz) && !solidAt(fx, fy + 1, fz);
            if (room && s > score) { score = s; best = d; }
        }
        if (best >= 0) return Dir(best);
    }
    return std::fabs(dx) > std::fabs(dz) ? (dx > 0 ? East : West) : (dz > 0 ? South : North);
}

// ---------------------------------------------------------------- lifecycle
void setHideHand(bool on) { game::hideHand(gS.savedHideHand, on); }

void finish() { // Scene.finish
    gS.phase    = Phase::None;
    gS.haveLast = false;
    gS.weight   = 0;
    gS.parallaxX = gS.parallaxY = 0;
    gS.screenOpen = false;
    gS.hoverCell = -1;
    gS.hoverResult = false;
    gS.hoverDrawer = -1;
    setHideHand(false);
}

void leave() { // Scene.leave
    if (gS.phase != Phase::Intro && gS.phase != Phase::Active) return;
    gS.leftX = gS.st.bx; gS.leftY = gS.st.by; gS.leftZ = gS.st.bz;
    gS.leftAt = gS.clock;
    gS.outroFrom     = gS.haveLast ? gS.last : gS.st.view();
    gS.haveOutroFrom = true;
    gS.outroWeight   = gS.weight;
    gS.phase = Phase::Outro;
    gS.time  = 0;
    gS.screenOpen = false;
    gS.hoverCell = -1;
    gS.hoverResult = false;
    gS.hoverDrawer = -1;
}

/// Scene.beginClientOnly: the vanilla screen is open; true = the scene plays.
bool begin(Kind kind, int bx, int by, int bz) {
    double f[3];
    float  py, pp;
    bool   alive;
    if (!game::playerState(f, py, pp, alive)) return false;
    V3 feet{f[0], f[1], f[2]}, eye{f[0], f[1] + 1.62, f[2]};
    Station st;
    st.kind = kind;
    st.bx = bx; st.by = by; st.bz = bz;
    st.top = {bx + 0.5, by + 1.0, bz + 0.5};
    // StationKind.sides: which sides a block is worked from
    int      facing  = game::facing(bx, by, bz);
    unsigned allowed = 15;
    if (facing >= 0) {
        if (kind == Kind::Anvil) allowed = (1u << int(clockWise(Dir(facing)))) | (1u << int(opposite(clockWise(Dir(facing)))));
        else if (kind == Kind::Grindstone || kind == Kind::Stonecutter) allowed = (1u << facing) | (1u << int(opposite(Dir(facing))));
        else if (kind == Kind::Loom) allowed = 1u << facing;
    }
    st.side = freeSide(st, feet, allowed);
    if (kind == Kind::Stonecutter) st.top = {bx + 0.5, by + 9 / 16.0, bz + 0.5}; // StonecutterStation.topOf: the table top
    if (kind == Kind::Enchanting) {
        st.top = {bx + 0.5, by + 0.75, bz + 0.5}; // EnchantFx.top
        // EnchantFx.shelves: EnchantingTableBlock.BOOKSHELF_OFFSETS (the ring two out, at the table's height and one up),
        // each with the block half-way to it free
        for (int dy = 0; dy <= 1; ++dy)
            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz) {
                    if (std::abs(dx) != 2 && std::abs(dz) != 2) continue;
                    if (game::blockName(bx + dx, by + dy, bz + dz) != "minecraft:bookshelf") continue;
                    if (solidAt(bx + dx / 2, by + dy, bz + dz / 2)) continue;
                    st.shelves.push_back({bx + dx + 0.5, by + dy + 0.5, bz + dz + 0.5});
                }
    }
    if (kind == Kind::Grindstone) {
        // GrindstoneStation.wheelCenter: between its posts on the floor, from a wall (pushed out from it) or under a ceiling
        std::string sn = game::blockStates(bx, by, bz);
        V3          c{bx + 0.5, by + 0.5, bz + 0.5};
        if (sn.find("hanging") != std::string::npos) st.wheel = c + V3{0, -2 / 16.0, 0};
        else if (sn.find("\"side\"") != std::string::npos && facing >= 0) st.wheel = c + dirVec(Dir(facing)) * (2 / 16.0);
        else st.wheel = c + V3{0, 2 / 16.0, 0};
        st.top = st.wheel + V3{0, 6 / 16.0, 0};
    }
    gS.states = game::blockStates(bx, by, bz);
    if (isFurnace(kind)) {
        // a furnace is always worked from its front (FurnaceStation(pos, FACING))
        int f = game::facing(bx, by, bz);
        if (f >= 0) st.side = Dir(f);
        gS.states = game::blockStates(bx, by, bz);
        st.lit = game::blockName(bx, by, bz).rfind("minecraft:lit_", 0) == 0;
    }
    std::string why;
    if (!planHere(st, feet, eye, why)) {
        gS.refused = why;
        ll_shim::info("[RF] cinematic: " + std::string(kNames[int(kind)]) + ": no scene (" + why + "), Spatial GUI instead");
        return false;
    }
    gS.st      = st;
    gS.refused.clear();
    gS.quick      = gCfg.quickMode == 2; // client-only: only "quick on every entry"
    gS.leanLength = gS.quick ? 0.4 : 1.2;
    gS.phase   = Phase::Intro;
    gS.time    = 0;
    gS.haveLast = false;
    gS.weight  = 0;
    gS.parallaxX = gS.parallaxY = 0;
    gS.haveOutroFrom = false;
    gS.screenOpen = true;
    gS.cursorEngaged = false;
    gS.expand = 0;
    gS.resultChangedAt = -10;
    std::fill(std::begin(gS.cellFull), std::end(gS.cellFull), false);
    gS.resultFull = false;
    ++gS.scenes;
    if (gCfg.hideHand) setHideHand(true);
    gS.haveAim = false;
    gS.tapAt = gS.craftAt = -10;
    gS.leftWorks = false;
    {
        std::vector<unsigned char> px;
        int  w = 0, h = 0;
        bool slim = false;
        if (game::skin(px, w, h, slim)) {
            if (px != gS.skin || w != gS.skinW) { gS.skin = std::move(px); gS.skinW = w; gS.skinH = h; ++gS.skinSerial; }
            gS.skinSlim = slim;
        }
    }
    return true;
}

// ================================================================ CameraRig
JPose blend(JPose const& a, JPose const& b, double k) {
    float f = float(k);
    return {lerp(a.pos, b.pos, k), a.yaw + wrapDeg(b.yaw - a.yaw) * f, a.pitch + (b.pitch - a.pitch) * f, a.roll + (b.roll - a.roll) * f};
}
// Reach.tethered / cameraPose
JPose tethered(JPose const& pose, V3 eye, V3 aimedAt) {
    constexpr double CAMERA_AHEAD = 1.05, CAMERA_DOWN = 0.95, CAMERA_UP = 0.16, LEAN_OVER = 1.2, LEAN_OVER_GAP = 0.45, LEAN_OVER_RISE = 0.35;
    V3     off = pose.pos - eye;
    double ox = off.x, oz = off.z;
    double dy = std::clamp(off.y, -CAMERA_DOWN, CAMERA_UP);
    double tooLow = -CAMERA_DOWN - off.y;
    if (tooLow > 0) {
        double tx = aimedAt.x - pose.pos.x, tz = aimedAt.z - pose.pos.z;
        double toward = std::hypot(tx, tz);
        double move = std::min(tooLow * LEAN_OVER, toward - LEAN_OVER_GAP);
        if (move > 0) { ox += tx / toward * move; oz += tz / toward * move; }
        dy += std::min(tooLow, LEAN_OVER_RISE);
    }
    double ahead = std::hypot(ox, oz);
    double scale = ahead > CAMERA_AHEAD ? CAMERA_AHEAD / ahead : 1;
    if (scale == 1 && dy == off.y) return pose;
    V3 at = eye + V3{ox * scale, dy, oz * scale};
    JPose r;
    r.pos = at;
    lookAngles(aimedAt - at, r.yaw, r.pitch);
    r.roll = pose.roll;
    return r;
}
JPose cameraPoseReach(Station const& st, JPose const& pose, V3 eye) {
    double distance = std::max(0.3, (pose.pos - st.center()).len());
    V3     aim      = pose.pos + dirFromRot(pose.pitch, pose.yaw) * distance; // Reach.lookPoint
    JPose  held     = tethered(pose, eye, aim);
    double strength = pct(gCfg.leanStrength);
    if (strength >= 0.999) return held;
    JPose r;
    r.pos = lerp(eye, held.pos, strength);
    lookAngles(aim - r.pos, r.yaw, r.pitch);
    r.roll = held.roll * float(strength);
    return r;
}
// CameraRig.tether (client-only: never more than 0.35 from the eyes, never through a wall)
JPose tether(JPose const& pose, V3 eye) {
    JPose held = cameraPoseReach(gS.st, pose, eye);
    V3    offset = held.pos - eye;
    // client-only mode keeps the camera within 0.35 of the eyes; single player with the full lean leans in as the
    // Java mod does with its server side (Reach.tethered already holds it within what the head can do)
    V3    end    = gCfg.fullLean ? held.pos : eye + (offset.len() > 0.35 ? offset.norm() * 0.35 : offset);
    {
        double L = (end - eye).len();
        int    n = std::max(2, int(L / 0.02));
        for (int i = 1; i <= n; ++i) {
            V3 p = lerp(eye, end, double(i) / n);
            if (solidAt(int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z)))) {
                V3 travel = lerp(eye, end, double(i - 1) / n) - eye;
                end       = eye + (travel.len() > 0.08 ? travel.norm() * (travel.len() - 0.08) : V3{});
                break;
            }
        }
    }
    held.pos = end;
    return held;
}
bool  gCursorValid = false;
float gCursorNdcX = 0, gCursorNdcY = 0; // Picking.mouseNdc (y up), updated by the input thread
JPose breathe(JPose pose, double parallax) {
    if (!livingCamera()) { gS.parallaxX = gS.parallaxY = 0; return pose; }
    double t = gS.clock, dt = gS.frameDt;
    double nx = gS.phase == Phase::Active ? gCursorNdcX : 0, ny = gS.phase == Phase::Active ? gCursorNdcY : 0;
    gS.parallaxX = approach(gS.parallaxX, std::clamp(nx, -1.0, 1.0), 4, dt);
    gS.parallaxY = approach(gS.parallaxY, std::clamp(ny, -1.0, 1.0), 4, dt);
    pose.pos.y += std::sin(t * 1.3) * 0.0035;
    pose.yaw   += float(std::sin(t * 0.61) * 0.16 + gS.parallaxX * 1.1 * parallax);
    pose.pitch += float(std::sin(t * 0.83 + 1.1) * 0.13 - gS.parallaxY * 0.8 * parallax);
    return pose;
}

constexpr double OUTRO_LENGTH = 1.05;

void frame() { // Scene.frame (no pause handling: Bedrock single player keeps the world running behind screens)
    LARGE_INTEGER now, f;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&f);
    double dt = gLastQpc.QuadPart ? std::min(0.1, double(now.QuadPart - gLastQpc.QuadPart) / double(f.QuadPart)) : 0;
    gLastQpc  = now;
    gS.frameDt = dt;
    gS.clock += dt;
    if (gS.phase == Phase::None) return;
    moveFrame();
    double pace = gS.phase == Phase::Intro ? pct(gCfg.approachSpeed) : gS.phase == Phase::Outro ? pct(gCfg.leaveSpeed) : 1;
    gS.time += dt * pace;
    if (gS.phase == Phase::Intro && gS.time >= gS.leanLength) {
        gS.phase = Phase::Active; // Scene.enterActive
        gS.time  = 0;
        gS.cursorEngaged = false;
    } else if (gS.phase == Phase::Outro && gS.time >= OUTRO_LENGTH) {
        finish();
    }
}

JPose overridePose(V3 livePos, float liveYaw, float livePitch) { // CameraRig.override
    JPose live{livePos, liveYaw, livePitch, 0};
    JPose pose;
    switch (gS.phase) {
    case Phase::Intro: {
        double u  = clamp01(gS.time / gS.leanLength); // introProgress with walkShare 0 (the player never walks)
        double ep = inOutCubic(window(u, 0, 1.0));
        double er = inOutCubic(window(u, 0, 0.82));
        JPose  lean  = tether(breathe(gS.st.view(), 0), livePos);
        JPose  start = live;
        V3     pos   = lerp(start.pos, lean.pos, ep) + V3{0, 0.045 * hump(u), 0};
        float  turn  = wrapDeg(lean.yaw - start.yaw);
        pose.pos   = pos;
        pose.yaw   = start.yaw + turn * float(er);
        pose.pitch = start.pitch + (lean.pitch - start.pitch) * float(er);
        float swing = float(hump(u) * std::clamp(turn * 0.04, -1.6, 1.6));
        pose.roll  = swing + (start.roll + (lean.roll - start.roll) * float(er));
        gS.weight  = ep;
        break;
    }
    case Phase::Active: {
        JPose  base = tether(breathe(gS.st.view(), smoother(gS.time / 0.6)), livePos);
        double kick = cameraKick();
        if (kick > 0.001 && cameraShake()) { // CameraRig.kick: a tremor that dies down with the moment
            double t        = gS.clock;
            float  shakeYaw = float((std::sin(t * 47) + std::sin(t * 31 + 1)) * 0.18 * kick);
            float  shakePit = float((std::sin(t * 53 + 2) + std::sin(t * 29)) * 0.14 * kick);
            base.yaw += shakeYaw;
            base.pitch += shakePit;
            base.roll += shakeYaw * 0.4f;
        }
        pose      = base; // FreeLook (Left Alt) not ported
        gS.weight = 1;
        break;
    }
    default: {
        JPose  from = gS.haveOutroFrom ? gS.outroFrom : gS.st.view();
        double u    = gS.time / OUTRO_LENGTH;
        double ep   = inOutCubic(window(u, 0.14, 1.0));
        double er   = inOutCubic(window(u, 0.2, 1.0));
        pose.pos   = lerp(from.pos, live.pos, ep) + V3{0, 0.03 * hump(ep), 0};
        pose.yaw   = from.yaw + wrapDeg(live.yaw - from.yaw) * float(er);
        pose.pitch = from.pitch + (live.pitch - from.pitch) * float(er);
        pose.roll  = from.roll + (0 - from.roll) * float(er);
        gS.weight  = (1 - ep) * gS.outroWeight;
        break;
    }
    }
    gS.last     = pose;
    gS.haveLast = true;
    return pose;
}

// ================================================================ picking (present / input thread, under gM)
struct Basis { V3 r, u, f; };
/// tan(fovY/2) of what is on screen: the game's projection does not show our field-of-view write (it reports the
/// player's own FOV), so the narrowing is applied here unless the projection already carries it.
double effTanHalf() {
    double s = gS.fovScaleNow;
    if (std::fabs(s - 1.0) < 1e-4 || std::fabs(gS.fovY - gS.vanillaFovY) > 0.5) return gS.tanHalf;
    return std::tan(std::atan(double(gS.tanHalf)) * s);
}
Basis basisOf(JPose const& p) {
    float y = p.yaw * kDeg, pt = p.pitch * kDeg, rl = p.roll * kDeg;
    V3 f{-std::sin(y) * std::cos(pt), -std::sin(pt), std::cos(y) * std::cos(pt)};
    V3 r{-std::cos(y), 0, -std::sin(y)};
    V3 u{r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x};
    V3 r2 = r * std::cos(rl) + u * std::sin(rl);
    V3 u2 = u * std::cos(rl) - r * std::sin(rl);
    return {r2, u2, f};
}
/// world -> clip (x right, y up, w = view depth)
bool toClip(JPose const& cam, Basis const& b, V3 p, float out[4]) {
    V3 d = p - cam.pos;
    double x = d.dot(b.r), y = d.dot(b.u), z = d.dot(b.f);
    double th = effTanHalf();
    out[0] = float(x / (th * gS.aspect));
    out[1] = float(y / th);
    out[2] = float(z * 0.5);
    out[3] = float(z);
    return z > 0.02;
}
/// world -> client pixel
bool toPixel(JPose const& cam, Basis const& b, V3 p, int W, int H, float& px, float& py) {
    float c[4];
    if (!toClip(cam, b, p, c)) return false;
    px = (c[0] / c[3] + 1) * 0.5f * W;
    py = (1 - c[1] / c[3]) * 0.5f * H;
    return true;
}
/// cursor (ndc, y up) -> world ray
void cursorRay(JPose const& cam, Basis const& b, float nx, float ny, V3& o, V3& d) {
    o = cam.pos;
    double th = effTanHalf();
    d = (b.f + b.r * (nx * th * gS.aspect) + b.u * (ny * th)).norm();
}
bool onPlaneY(V3 o, V3 d, double y, V3& hit) {
    if (std::fabs(d.y) < 1e-9) return false;
    double t = (y - o.y) / d.y;
    if (t < 0) return false;
    hit = o + d * t;
    return true;
}

// ---------------------------------------------------------------- drawer layout (SceneScreen.layout, in client pixels)
struct DrawerGeom { float x, y, w, h, scale, slotPx, padPx, gapPx; };
DrawerGeom drawerGeom(int W, int H) {
    constexpr int SLOT = 18, PAD = 5, ROW_GAP = 4, INNER = 9 * SLOT;
    float gui = float(spatialcam::javaAutoGuiScale(W, H)); // Java GUI scale for this window
    float gh  = H / gui;
    float scale = std::clamp(gh * 0.068f / SLOT, 0.6f, 1.25f);
    double appear;
    if (gS.phase == Phase::Outro) appear = 1 - inOutSine(gS.time / 0.22);
    else if (gS.phase == Phase::Active) appear = outCubic(gS.time / 0.45);
    else appear = 0;
    DrawerGeom g;
    g.scale  = scale;
    g.slotPx = SLOT * scale * gui;
    g.padPx  = PAD * scale * gui;
    g.gapPx  = ROW_GAP * scale * gui;
    g.w      = (INNER + PAD * 2) * scale * gui;
    float full = float(PAD * 2 + SLOT + gS.expand * (ROW_GAP + 3 * SLOT));
    g.h      = full * scale * gui;
    g.x      = (W - g.w) * 0.5f;
    g.y      = float((6 - (1 - appear) * (g.h / gui + 10)) * gui);
    return g;
}
int drawerSlotAt(DrawerGeom const& g, float mx, float my) {
    float lx = mx - g.x - g.padPx, ly = my - g.y - g.padPx;
    if (lx < 0 || lx >= 9 * g.slotPx) return -1;
    int col = int(lx / g.slotPx);
    if (ly >= 0 && ly < g.slotPx) return col; // hotbar
    float below = ly - g.slotPx - g.gapPx;
    if (gS.expand > 0.9 && below >= 0 && below < 3 * g.slotPx) return 9 + int(below / g.slotPx) * 9 + col;
    return -1;
}
bool overDrawer(DrawerGeom const& g, float mx, float my) {
    float reach = (5 * 2 + 18 + (gS.expand > 0.5 ? 4 + 3 * 18 : 0)) * g.scale * (g.slotPx / (18 * g.scale));
    return mx >= g.x - 4 && mx <= g.x + g.w + 4 && my >= 0 && my <= g.y + std::max(g.h, reach) + 4;
}

// ---------------------------------------------------------------- cursor -> what it is over (Scene.updateCursor)
int   gW = 0, gH = 0;
std::atomic<unsigned> gHeld{0};          // buttons held as the hidden screen sees them
enum class PressState { Idle, Hover, Down, ForceDown, Swallow };
struct Press {
    PressState state = PressState::Idle;
    unsigned   bit = 0;
    int        target = -1;
    double     since = 0, releasedAt = -1e9;
};
Press    gPress;
std::string gPressLog[12];
int         gPressLogN = 0;
unsigned gPrevButtons = 0;
double nowMs() {
    LARGE_INTEGER n, f;
    QueryPerformanceCounter(&n);
    QueryPerformanceFrequency(&f);
    return double(n.QuadPart) * 1000.0 / double(f.QuadPart);
}
float gMapX = 0, gMapY = 0; bool gMapValid = false; // last mapped position (diag)

// ---------------------------------------------------------------- clicks we make ourselves (a move's beat, a take)
// The hidden screen's cursor goes to the slot, the button goes down there for a moment, and up again.
struct Inject { int target; unsigned bit; double start; };
std::deque<Inject> gInject;
bool injectActive(double now, int& target, unsigned& bit, bool& down) {
    while (!gInject.empty()) {
        Inject& i = gInject.front();
        if (i.start < 0) i.start = now;
        double t = now - i.start;
        target = i.target;
        bit    = i.bit;
        if (t < 45) { down = false; return true; }
        if (t < 125) { down = true; return true; }
        if (t < 185) { down = false; return true; }
        gInject.pop_front();
    }
    return false;
}
void inject(int target, unsigned bit = 1) { gInject.push_back({target, bit, -1}); }
std::atomic_bool gSnapRequest{false};

// ---------------------------------------------------------------- MenuStation's big move (forging, ...)
constexpr double FORGE_LENGTH = 2.05, SWING = 0.09;
constexpr double BLOWS[3] = {0.7, 1.02, 1.3};
constexpr double PUT_AWAY = BLOWS[2] + 0.32;
double moveTime() { double t = gS.clock - gS.moveAt; return gS.moveOn && t >= 0 && t <= gS.moveLength ? t : -1; }
// EnchantStation / EnchantFx numbers
constexpr double OFFER_HIGH = 0.92, OFFER_LOW = 0.74, OFFER_FAR = -0.12, OFFER_NEAR = 0.02, OFFER_SPREAD = 0.4;
constexpr double RITUAL_LENGTH = 1.5, RITUAL_PEAK = 0.5, E_ITEM_SIZE = 0.34, LAPIS_RADIUS = 0.115;
constexpr double LAPIS_RIGHT = 0.44, LAPIS_NEAR = 0.18, LAPIS_LIFT = 0.27;
constexpr double OUTER_LIFT = 0.003, INNER_LIFT = 0.004, HINT_LIFT = 0.006, SOCKET_LIFT = 0.006; // 0.18.3
// the book's outline over the table (blocks): half its width, how far it reaches towards / away from the player, its height there
constexpr double BOOK_LIFT = 0.1, BOOK_HALF_W = 0.36, BOOK_NEAR = 0.2, BOOK_FAR = 0.3, BOOK_NEAR_H = 0.12, BOOK_FAR_H = 0.42;
bool   moving() { return moveTime() >= 0; }
double sinceBlow(double t) {
    double since = 1e9;
    for (double b : BLOWS) if (t >= b) since = t - b;
    return since;
}
double grindKick(double t);
double smithKick(double t);
void   smithAction(int t, unsigned bit, bool shift);
void   cartAction(int t, unsigned bit, bool shift);
void   loomAction(int t, unsigned bit, bool shift);
double stoneKick(double t);
/// ClientStation.cameraKick: 0..1 how hard the camera is shaken right now
double cameraKick() {
    double t = moveTime();
    if (t < 0) return 0;
    if (gS.st.kind == Kind::Anvil) return 0.5 * std::exp(-sinceBlow(t) * 13);
    if (gS.st.kind == Kind::Grindstone) return grindKick(t);
    if (gS.st.kind == Kind::Stonecutter) return stoneKick(t);
    if (gS.st.kind == Kind::Smithing) return smithKick(t);
    if (gS.st.kind == Kind::Enchanting) { // EnchantStation.cameraKick
        double u = t / RITUAL_LENGTH, peak = RITUAL_PEAK / RITUAL_LENGTH;
        return u < peak ? 0.6 * smoother(u / peak) : std::exp(-(u - peak) * RITUAL_LENGTH * 3.2);
    }
    return 0;
}
double stationFlash(float rgb[3]) {
    if (gS.st.kind == Kind::Enchanting) { // EnchantStation.flash: the white-violet flash of the burst
        double d = gS.clock - gS.burstAt;
        rgb[0] = 0xE8 / 255.0f; rgb[1] = 0xDC / 255.0f; rgb[2] = 1;
        return d >= 0 && d < 0.5 ? std::pow(1 - d / 0.5, 2) * 0.26 : 0;
    }
    double t = moveTime();
    if (t < 0) return 0;
    if (gS.st.kind == Kind::Anvil) { rgb[0] = 1; rgb[1] = 0xE6 / 255.0f; rgb[2] = 0xB8 / 255.0f; return 0.11 * std::exp(-sinceBlow(t) * 16); }
    if (gS.st.kind == Kind::Smithing) { rgb[0] = 1; rgb[1] = 0xE8 / 255.0f; rgb[2] = 0xC8 / 255.0f; return t < 1.35 ? 0 : 0.2 * std::exp(-(t - 1.35) * 7); }
    return 0;
}

// ---------------------------------------------------------------- AnvilStation (vanilla geometry)
V3 anvilMain() { return gS.st.top + gS.st.right() * -0.02; }                                              // slot 0
V3 anvilAdd() { return gS.st.top + gS.st.right() * 0.07 + gS.st.sideV() * -0.03 + V3{0, 0.012, 0}; } // slot 1
V3 anvilPoint() { return anvilMain() + gS.st.right() * 0.03; }
enum AnvilTarget { ANone, ASurface, AStack, AName };
/// GrindstoneStation.under: the cursor within a disc round a point, the disc `radius` tall there (at least 9 GUI px)
bool underCursor(Basis const& b, float mx, float my, V3 center, double radius) {
    float ax, ay, ex, ey;
    if (!toPixel(gS.last, b, center, gW, gH, ax, ay) || !toPixel(gS.last, b, center + V3{0, radius, 0}, gW, gH, ex, ey)) return false;
    float gui = float(spatialcam::javaAutoGuiScale(gW, gH));
    float r   = std::max(9.0f * gui, std::hypot(ax - ex, ay - ey));
    return std::hypot(mx - ax, my - ay) < r;
}
/// AnvilStation.updateCursor; returns the target id (hidden slot, or an action >= 100)
int anvilCursor(Basis const& b, float mx, float my, V3 o, V3 d) {
    gS.anvilTarget = ANone;
    if (moving()) return -1;
    bool carrying = gS.carrying, pile = gS.cellFull[0] || gS.cellFull[1];
    if (!carrying && pile && underCursor(b, mx, my, gS.cellFull[0] ? anvilMain() : anvilAdd(), 0.16)) {
        gS.anvilTarget = AStack;
        gS.haveAim     = true;
        gS.aim         = gS.cellFull[1] ? anvilAdd() : anvilMain();
        return 100;
    }
    V3 hit;
    if (!carrying || !onPlaneY(o, d, gS.st.top.y, hit)) return -1;
    V3 rel = hit - gS.st.top;
    if (std::fabs(rel.dot(gS.st.right())) < 0.52 && std::fabs(rel.dot(gS.st.sideV())) < 0.36) {
        gS.anvilTarget = ASurface;
        gS.haveAim     = true;
        int slot       = gS.cellFull[0] ? 1 : 0; // the item first; then what goes into it, on top
        gS.aim         = slot == 0 ? anvilMain() : anvilAdd();
        return slot;
    }
    return -1;
}
// ---------------------------------------------------------------- GrindstoneStation (vanilla geometry)
constexpr double G_HALF = 6 / 16.0, G_WIDTH = 4 / 16.0, G_SLOT_SIDE = 0.1, G_ITEM_SIZE = 0.3, G_TOOL_TURN = -45, G_SLANT = 25;
constexpr double GRIND_LENGTH = 2.4, G_CONTACT = 0.6, G_STROKE = 0.433, G_CLEAN_AT = 1.5, G_LET_GO = 1.9;
enum GrindTarget { GNone, GPlace, GItem };
V3 grindRest(int slot) { return gS.st.top + gS.st.right() * (slot == 0 ? G_SLOT_SIDE : -G_SLOT_SIDE); } // restPoint: first right, second left
V3 grindPoint2() { return gS.st.top + gS.st.sideV() * 0.04 + V3{0, 0.03, 0}; }                       // where the blade is ground
double grindStroke(double t) {
    double s    = window(t, G_CONTACT, G_LET_GO);
    double fade = smoother(s * 8) * smoother((1 - s) * 8);
    return 0.075 * std::sin(3.14159265358979 * (t - G_CONTACT) / G_STROKE) * fade;
}
/// unit vector from the handle to the tip of item k (0 right hand, 1 left) while it is ground
V3 grindBlade(int k) {
    double a = G_SLANT * kDeg;
    return gS.st.sideV() * -std::cos(a) + gS.st.right() * (k == 0 ? -std::sin(a) : std::sin(a));
}
V3 grindHeldPos(int k, double t) {
    double sideways = (k == 0 ? 0.08 : -0.08) + grindStroke(t) * (k == 0 ? 1 : -1);
    V3     grinding = grindPoint2() + gS.st.right() * sideways;
    V3     lying    = grindRest(k) + V3{0, 0.008, 0};
    double lift     = inOutCubic(window(t, 0.28, G_CONTACT));
    V3     at       = lerp(lying, grinding, lift) + V3{0, 0.09 * hump(lift), 0};
    return at - V3{0, 0.008 * std::fabs(std::sin(3.14159265358979 * (t - G_CONTACT) / G_STROKE)) * window(t, G_CONTACT, G_CONTACT + 0.1), 0};
}
/// yaw from "pointing away" (ItemPlacer.awayYaw): from lying straight on the wheel to the slant it is ground at
double grindHeldYaw(int k, double t) {
    double lift = inOutCubic(window(t, 0.28, G_CONTACT));
    double lying = k == 0 ? -8 : 8, slant = k == 0 ? G_SLANT : -G_SLANT;
    return G_TOOL_TURN + lying + (slant - lying) * lift;
}
float grindHeldTilt(double t) { return float(-9 * inOutCubic(window(t, 0.28, G_CONTACT))); }
V3    grindBite(int k, double t) { V3 at = grindHeldPos(k, t) + grindBlade(k) * 0.07; return {at.x, grindPoint2().y - 0.026, at.z}; }
V3    grindHandle(int k, V3 item) { return item - grindBlade(k) * 0.13 + V3{0, 0.062 * 0.6, 0}; }
V3    grindLifted() { return gS.st.top + gS.st.sideV() * 0.3 + gS.st.right() * 0.06 + V3{0, 0.25, 0}; }
/// GrindstoneStation.overWheel: the cursor on the wheel's top or on the running face towards the player
bool overWheel(V3 o, V3 d) {
    V3 hit;
    if (onPlaneY(o, d, gS.st.top.y, hit)) {
        V3 rel = hit - gS.st.top;
        if (std::fabs(rel.dot(gS.st.right())) < G_WIDTH + 0.08 && std::fabs(rel.dot(gS.st.sideV())) < G_HALF + 0.06) return true;
    }
    V3     front = gS.st.wheel + gS.st.sideV() * G_HALF, n = gS.st.sideV();
    double dn    = d.dot(n);
    if (std::fabs(dn) < 1e-9) return false;
    double t = (front - o).dot(n) / dn;
    if (t <= 0) return false;
    V3 h = o + d * t;
    return std::fabs((h - front).dot(gS.st.right())) < G_WIDTH + 0.08 && std::fabs(h.y - gS.st.wheel.y) < G_HALF;
}
/// GrindstoneStation.updateCursor; slot ids 0/1 to lay a carried item down, 101/102 the item lying there
int grindCursor(Basis const& b, float mx, float my, V3 o, V3 d) {
    gS.grindTarget = GNone;
    gS.grindHover = gS.grindPlace = -1;
    if (moving()) return -1;
    if (!gS.carrying) {
        for (int i = 0; i < 2; ++i)
            if (gS.cellFull[i] && underCursor(b, mx, my, grindRest(i), 0.13)) {
                gS.grindTarget = GItem;
                gS.grindHover  = i;
                gS.haveAim     = true;
                gS.aim         = grindRest(i);
                return 101 + i;
            }
        return -1;
    }
    if (overWheel(o, d)) {
        gS.grindTarget = GPlace;
        gS.grindPlace  = !gS.cellFull[0] || gS.cellFull[1] ? 0 : 1;
        gS.haveAim     = true;
        gS.aim         = grindRest(gS.grindPlace);
        return gS.grindPlace;
    }
    return -1;
}
double grindKick(double t) { return 0.13 * window(t, G_CONTACT - 0.05, G_CONTACT + 0.15) * (1 - window(t, G_LET_GO - 0.15, G_LET_GO + 0.1)); }

// ---------------------------------------------------------------- StonecutterStation (vanilla geometry)
constexpr double S_BLADE = 7 / 16.0, S_DIAL_HIGH = 0.5, S_SHAPE_SIZE = 0.15, S_PIECE = 0.2;
constexpr int    S_DIAL_SLOTS = 9;
constexpr double CUT_LENGTH = 1.05, CUT_ALL_LENGTH = 1.5, S_BITE = 0.26;
enum StoneTarget { SNone, SPlace, SStack, SShape };
V3 stoneStack() { return gS.st.top + gS.st.right() * -0.25 + gS.st.sideV() * 0.24; } // the stone, beside the blade
/// the shapes on offer: the hidden screen's recipe buttons that hold something
int  shapeCount() { int n = 0; for (int k = 0; k < kButtons; ++k) n += gS.btnFull[k]; return gS.cellFull[0] ? n : 0; }
int  shapeButton(int k) { for (int b = 0; b < kButtons; ++b) if (gS.btnFull[b] && k-- == 0) return b; return -1; }
int  dialVisible() { return std::min(S_DIAL_SLOTS, shapeCount()); }
double dialStep() { int n = dialVisible(); return n <= 1 ? 0 : std::min(22.0, 144.0 / (n - 1)); }
/// a shape's angle on the dial: 90 straight over the blade, the first ones to the left
double dialAngle(int k) { return 90 + ((dialVisible() - 1) / 2.0 - (k - gS.scroll)) * dialStep(); }
double onDial(int k) { double a = dialAngle(k); return smoother(window(a, 4, 18)) * smoother(window(176 - a, 0, 14)); }
// DIAL_FITS: {width, lift, towards the player} - the dial closes in, rises and comes forward when blocks stand beside
const double kDialFits[4][3] = {{0.72, 0, 0}, {0.5, 0.08, 0.04}, {0.42, 0.14, 0.26}, {0.34, 0.2, 0.42}};
V3 dialPoint(double degrees, double up, double const fit[3]) {
    double a = degrees * kDeg;
    return gS.st.top + gS.st.sideV() * (-0.06 + fit[2]) + gS.st.right() * (std::cos(a) * fit[0]) + V3{0, up + fit[1] + std::sin(a) * S_DIAL_HIGH, 0};
}
bool dialFree(double const fit[3]) { // no shape of this dial inside a block (SHAPE_ROOM 0.07 round each)
    for (double a = 4; a <= 176; a += 8) {
        V3  at = dialPoint(a, 0.04, fit);
        int bx = int(std::floor(at.x)), by = int(std::floor(at.y)), bz = int(std::floor(at.z));
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dz = -1; dz <= 1; ++dz) {
                    int x = bx + dx, y = by + dy, z = bz + dz;
                    if (x == gS.st.bx && y == gS.st.by && z == gS.st.bz) continue;
                    if (at.x < x - 0.07 || at.x > x + 1.07 || at.y < y - 0.07 || at.y > y + 1.07 || at.z < z - 0.07 || at.z > z + 1.07) continue;
                    if (solidAt(x, y, z)) return false;
                }
    }
    return true;
}
double const* dialFit() {
    if (gS.clock - gS.fitAt > 0.5) { // FIT_EVERY
        gS.fitAt    = gS.clock;
        gS.fitIndex = 3;
        for (int i = 0; i < 4; ++i)
            if (dialFree(kDialFits[i])) { gS.fitIndex = i; break; }
    }
    return kDialFits[gS.fitIndex];
}
V3 shapePoint(int k) { return dialPoint(dialAngle(k), 0.04 + 0.008 * std::sin(gS.clock * 1.7 + k * 1.3), dialFit()); }
/// StonecutterStation.updateCursor; 200+k a shape, 103 the stone (take), 0 lay the carried stone down
int stoneCursor(Basis const& b, float mx, float my, V3 o, V3 d) {
    gS.stoneTarget = SNone;
    gS.hoverShape  = -1;
    if (moving()) return -1;
    int n = shapeCount();
    for (int k = 0; k < n; ++k)
        if (onDial(k) > 0.5 && underCursor(b, mx, my, shapePoint(k), 0.075)) {
            gS.stoneTarget = SShape;
            gS.hoverShape  = k;
            gS.haveAim     = true;
            gS.aim         = shapePoint(k) + V3{0, -0.1, 0} + gS.st.sideV() * 0.12;
            return 200 + k;
        }
    if (!gS.carrying && gS.cellFull[0] && underCursor(b, mx, my, stoneStack() + V3{0, 0.08, 0}, 0.12)) {
        gS.stoneTarget = SStack;
        gS.haveAim     = true;
        gS.aim         = stoneStack() + V3{0, 0.12, 0};
        return 103;
    }
    V3 hit;
    if (gS.carrying && onPlaneY(o, d, gS.st.top.y, hit)) {
        V3 rel = hit - gS.st.top;
        if (std::fabs(rel.dot(gS.st.right())) < 0.52 && std::fabs(rel.dot(gS.st.sideV())) < 0.52) {
            gS.stoneTarget = SPlace;
            gS.haveAim     = true;
            gS.aim         = stoneStack();
            return 0;
        }
    }
    return -1;
}
double stoneOutAt(bool all) { return all ? 1.15 : 0.72; }
double stoneAlong(double t) { return -0.46 + 0.76 * inOutSine(window(t, S_BITE - 0.04, stoneOutAt(gS.cutAll))); }
bool   stoneCutting(double t) { return t >= S_BITE && std::fabs(stoneAlong(t)) < S_BLADE && t < stoneOutAt(gS.cutAll); }
/// the piece until it is out past the blade: taken off the stack, set at the end of the blade, pushed through
V3 piecePath(double t) {
    V3 top = gS.st.top, rgt = gS.st.right();
    if (t < S_BITE) {
        V3     onStack = stoneStack() + V3{0, S_PIECE / 2 + 0.01, 0};
        V3     start   = top + rgt * -0.46 + V3{0, S_PIECE / 2, 0};
        double k       = inOutCubic(window(t, 0.06, S_BITE - 0.02));
        return lerp(onStack, start, k) + V3{0, 0.07 * hump(k), 0};
    }
    return top + rgt * stoneAlong(t) + V3{0, S_PIECE / 2, 0};
}
V3     stoneCatch() { return gS.st.top + gS.st.right() * 0.34 + gS.st.sideV() * 0.08 + V3{0, S_PIECE * 0.6 + 0.062, 0}; }
double stoneKick(double t) { return stoneCutting(t) ? 0.12 : 0; }

void stoneAction(int t, unsigned bit, bool shift);
// ---------------------------------------------------------------- SmithingStation (vanilla geometry)
// Template, gear and material across the plate, the gear in front in the middle (r right, d towards the player)
constexpr double SM_LAYOUT[3][2] = {{-0.26, -0.12}, {0.0, 0.06}, {0.27, -0.12}};
constexpr double SM_SIZES[3] = {0.22, 0.34, 0.2};
constexpr double SMITH_LENGTH = 2.6, TEMPLATE_DOWN = 0.38, MATERIAL_UP = 0.5, MATERIAL_DOWN = 0.76, SM_PRESS = 0.95, SM_PEAK = 1.35,
                 SM_COOLED = 2.15, ON_GEAR = 0.025, ON_TEMPLATE = 0.041;
enum SmithTarget { SmNone, SmPlace, SmItem };
V3 smithPoint(int slot) { return gS.st.top + gS.st.right() * SM_LAYOUT[slot][0] + gS.st.sideV() * SM_LAYOUT[slot][1]; }
double smithFuse(double t) { return smoother(window(t, SM_PRESS + 0.05, SM_PEAK - 0.03)); }
V3 templatePos(double t) {
    V3 from = smithPoint(0) + V3{0, 0.004, 0}, over = smithPoint(1) + V3{0, 0.14, 0}, on = smithPoint(1) + V3{0, ON_GEAR, 0};
    V3 at = lerp(from, from + V3{0, 0.1, 0}, inOutCubic(window(t, 0.06, 0.24)));
    at    = lerp(at, over, inOutCubic(window(t, 0.14, 0.3)));
    at    = lerp(at, on, inCubic3(window(t, 0.28, TEMPLATE_DOWN)));
    return at - V3{0, (ON_GEAR - 0.006) * smithFuse(t) + 0.004 * hump(window(t, SM_PRESS, SM_PRESS + 0.12)), 0};
}
V3 materialPos(double t) {
    V3 from = smithPoint(2) + V3{0, 0.004, 0};
    V3 over = smithPoint(1) + V3{0, 0.17, 0} + gS.st.right() * 0.012, on = smithPoint(1) + V3{0, ON_TEMPLATE, 0} + gS.st.right() * 0.012;
    V3 at = lerp(from, from + V3{0, 0.1, 0}, inOutCubic(window(t, MATERIAL_UP - 0.04, MATERIAL_UP + 0.08)));
    at    = lerp(at, over, inOutCubic(window(t, MATERIAL_UP + 0.02, MATERIAL_DOWN - 0.06)));
    at    = lerp(at, on, inCubic3(window(t, MATERIAL_DOWN - 0.09, MATERIAL_DOWN)));
    return at - V3{0, (ON_TEMPLATE - 0.008) * smithFuse(t) + 0.006 * hump(window(t, SM_PRESS, SM_PRESS + 0.12)), 0};
}
/// the turn of an item lying flat: from how it lay on the plate to square on the gear
double smithTurn(double t, double laid, double stack, double from, double to) { return laid + (stack - laid) * inOutCubic(window(t, from, to)); }
double smithHeat(double t) { return smoother(window(t, MATERIAL_DOWN, SM_PEAK)) * (1 - smoother(window(t, SM_PEAK, SM_COOLED))); }
double smithKick(double t) {
    return (t >= SM_PRESS ? 0.35 * std::exp(-(t - SM_PRESS) * 14) : 0) + (t >= SM_PEAK ? 0.3 * std::exp(-(t - SM_PEAK) * 9) : 0);
}
/// SmithingStation.updateCursor; 101+i an item lying there, 0..2 the spot the carried item goes to
int smithCursor(Basis const& b, float mx, float my, V3 o, V3 d) {
    gS.smithTarget = SmNone;
    gS.smithHover = gS.smithPlace = -1;
    if (moving()) return -1;
    if (!gS.carrying) {
        for (int i = 0; i < 3; ++i)
            if (gS.cellFull[i] && underCursor(b, mx, my, smithPoint(i), SM_SIZES[i] * 0.42)) {
                gS.smithTarget = SmItem;
                gS.smithHover  = i;
                gS.haveAim     = true;
                gS.aim         = smithPoint(i);
                return 101 + i;
            }
        return -1;
    }
    V3 hit;
    if (!onPlaneY(o, d, gS.st.top.y, hit)) return -1;
    V3 rel = hit - gS.st.top;
    if (std::fabs(rel.x) > 0.52 || std::fabs(rel.z) > 0.52) return -1;
    int nearest = 0;
    for (int i = 1; i < 3; ++i)
        if ((hit - smithPoint(i)).len() < (hit - smithPoint(nearest)).len()) nearest = i;
    gS.smithTarget = SmPlace;
    gS.smithPlace  = nearest;
    gS.haveAim     = true;
    gS.aim         = smithPoint(nearest);
    return nearest;
}
void smithAction(int t, unsigned bit, bool shift) {
    if (t < 101 || t > 103) return;
    int i = t - 101;
    if (bit == 1 && gS.cellFull[3]) {
        // SmithingStation.smith: the server smiths at white heat (PEAK)
        gS.moveOn = true; gS.moveAt = gS.clock; gS.moveLength = SMITH_LENGTH; gS.moveBeat = SM_PEAK; gS.moveBeatDone = false;
        gS.moveToInv = shift;
        for (int k = 0; k < 4; ++k) { gS.wasFull[k] = gS.cellFull[k]; gS.wasBlock[k] = gS.cellBlock[k]; }
        // SmithingStation.moltenColor: a trim runs in its material's colour, the netherite upgrade like molten metal
        float* c  = gS.iconRgb[2];
        float  mx = std::max(c[0], std::max(c[1], c[2]));
        if (mx > 0.4f) { gS.molten[0] = c[0] / mx; gS.molten[1] = c[1] / mx; gS.molten[2] = c[2] / mx; }
        else { gS.molten[0] = 1; gS.molten[1] = 0.5f; gS.molten[2] = 0.16f; }
        gS.smithFx = 0;
        std::memcpy(gS.snapBox, gS.iconBox, sizeof gS.snapBox);
        gSnapRequest = true;
    } else inject(i, 1); // take it
    gS.tapAt = gS.clock;
}

// ---------------------------------------------------------------- CartographyStation (vanilla geometry)
constexpr double CT_LAYOUT[2][2] = {{-0.04, 0.04}, {0.34, -0.1}}; // the map in the middle, what goes into it on the right
constexpr double MAP_SIZE = 0.5, ADDITION_SIZE = 0.24, WORK_LENGTH = 2.1, CT_BEAT = 1.55;
enum CartWork { CZoom, CLock, CCopy };
enum CartTarget { CtNone, CtPlace, CtItem };
V3 cartPoint(int slot) { return gS.st.top + gS.st.right() * CT_LAYOUT[slot][0] + gS.st.sideV() * CT_LAYOUT[slot][1]; }
double cartAdditionYaw(double t) { (void)t; return 0; } // turned from its own rotation to the map's (both 0 here)
V3 cartAdditionPos(double t) {
    V3 from = cartPoint(1), to;
    switch (gS.cartWork) {
    case CZoom: to = cartPoint(0) + V3{0, -0.002, 0}; break;
    case CLock: to = cartPoint(0) + V3{0, 0.012, 0}; break;
    default: to = cartPoint(0) + gS.st.right() * -0.02 + gS.st.sideV() * 0.02; break;
    }
    double k = inOutCubic(window(t, 0.1, 0.55));
    return lerp(from, to, k) + V3{0, (gS.cartWork == CLock ? 0.16 : 0.07) * hump(k), 0};
}
int cartCursor(Basis const& b, float mx, float my, V3 o, V3 d) {
    gS.cartTarget = CtNone;
    gS.cartHover = gS.cartPlace = -1;
    if (moving()) return -1;
    if (!gS.carrying) {
        for (int i = 1; i >= 0; --i)
            if (gS.cellFull[i] && underCursor(b, mx, my, cartPoint(i), i == 0 ? 0.2 : 0.1)) {
                gS.cartTarget = CtItem;
                gS.cartHover  = i;
                gS.haveAim    = true;
                gS.aim        = cartPoint(i);
                return 101 + i;
            }
        return -1;
    }
    V3 hit;
    if (!onPlaneY(o, d, gS.st.top.y, hit) || std::fabs(hit.x - gS.st.top.x) > 0.52 || std::fabs(hit.z - gS.st.top.z) > 0.52) return -1;
    int nearest = (hit - cartPoint(0)).len() < (hit - cartPoint(1)).len() ? 0 : 1;
    gS.cartTarget = CtPlace;
    gS.cartPlace  = nearest;
    gS.haveAim    = true;
    gS.aim        = cartPoint(nearest);
    return nearest;
}
/// what the addition is, from its icon: glass (light, blue-tinged) locks, an empty map (warm parchment) copies,
/// paper (white) zooms out
int cartWorkOf() {
    float* c = gS.iconRgb[1];
    if (c[2] > c[0] + 0.03f) return CLock;
    if (c[0] - c[2] > 0.12f) return CCopy;
    return CZoom;
}
void cartAction(int t, unsigned bit, bool shift) {
    if (t < 101 || t > 102) return;
    int i = t - 101;
    if (bit == 1 && i == 0 && gS.cellFull[2]) {
        gS.cartWork = cartWorkOf();
        gS.moveOn = true; gS.moveAt = gS.clock; gS.moveLength = WORK_LENGTH; gS.moveBeat = CT_BEAT; gS.moveBeatDone = false;
        gS.moveToInv = shift;
        gS.gleamed   = false;
        for (int k = 0; k < 4; ++k) { gS.wasFull[k] = gS.cellFull[k]; gS.wasBlock[k] = gS.cellBlock[k]; }
        std::memcpy(gS.snapBox, gS.iconBox, sizeof gS.snapBox);
        gSnapRequest = true;
    } else inject(i, 1);
    gS.tapAt = gS.clock;
}

// ---------------------------------------------------------------- LoomStation (vanilla geometry)
constexpr double CLOTH_WIDTH = 0.42, CLOTH_LENGTH = 0.84, WEAVE_LENGTH = 2.4, WEAVE_FROM = 0.2, WEAVE_TO = 1.85, L_BEAT = 2.0;
constexpr double LOOM_CORNERS[3][2] = {{0, 0}, {0.37, 0.34}, {-0.37, 0.34}}; // banner, dye (right front), pattern (left front)
constexpr int    L_PASSES = 6, L_COLS = 5, L_ROWS = 4, L_CELL_W = 22, L_CELL_H = 38, L_PAD = 6, L_HEADER = 13;
enum LoomTarget { LNone, LPlace, LItem, LPattern };
V3  loomPoint(int slot) { return gS.st.top + gS.st.right() * LOOM_CORNERS[slot][0] + gS.st.sideV() * LOOM_CORNERS[slot][1]; }
/// the patterns the loom offers: its pattern buttons that hold something, once banner and dye are in
int loomPatterns() { int n = 0; for (int k = 0; k < kButtons; ++k) n += gS.btnFull[k]; return gS.cellFull[0] && gS.cellFull[1] ? n : 0; }
double loomWoven(double t) { return window(t, WEAVE_FROM, WEAVE_TO); }
double shuttleAcross(double t) { // -1 at the left edge of the cloth .. 1 at the right, crossing once per pass
    double passes = loomWoven(t) * L_PASSES;
    int    pass   = std::min(L_PASSES - 1, int(passes));
    double f      = inOutSine(passes - pass);
    return pass % 2 == 0 ? -1 + 2 * f : 1 - 2 * f;
}
V3 weaveLine(double t) { return loomPoint(0) + gS.st.sideV() * (CLOTH_LENGTH / 2 - CLOTH_LENGTH * loomWoven(t)) + V3{0, 0.01, 0}; }
int loomCursor(Basis const& b, float mx, float my, V3 o, V3 d) {
    gS.loomTarget = LNone;
    gS.loomHover = gS.loomPattern = gS.loomPlace = -1;
    if (moving()) return -1;
    float* r = gS.boardRect;
    if (gS.boardShown && mx >= r[0] && mx < r[0] + r[2] && my >= r[1] && my < r[1] + r[3]) {
        float g   = gS.boardGui;
        int   col = int((mx - r[0] - L_PAD * g) / (L_CELL_W * g)), row = int((my - r[1] - (L_PAD + L_HEADER) * g) / (L_CELL_H * g));
        int   k   = row * L_COLS + col;
        gS.haveAim = true; // the hand lifts towards the board
        gS.aim     = gS.st.top + gS.st.right() * 0.42 + gS.st.sideV() * 0.05 + V3{0, 0.12, 0};
        if (col >= 0 && col < L_COLS && row >= 0 && row < L_ROWS && my >= r[1] + (L_PAD + L_HEADER) * g && k < loomPatterns()) {
            gS.loomTarget  = LPattern;
            gS.loomPattern = k;
            return 200 + k;
        }
        return -1;
    }
    if (!gS.carrying) {
        for (int i = 0; i < 3; ++i)
            if (gS.cellFull[i] && underCursor(b, mx, my, loomPoint(i), i == 0 ? 0.24 : 0.09)) {
                gS.loomTarget = LItem;
                gS.loomHover  = i;
                gS.haveAim    = true;
                gS.aim        = loomPoint(i);
                return 101 + i;
            }
        return -1;
    }
    V3 hit;
    if (!onPlaneY(o, d, gS.st.top.y, hit) || std::fabs(hit.x - gS.st.top.x) > 0.52 || std::fabs(hit.z - gS.st.top.z) > 0.52) return -1;
    int nearest = 0;
    for (int i = 1; i < 3; ++i)
        if ((hit - loomPoint(i)).len() < (hit - loomPoint(nearest)).len()) nearest = i;
    gS.loomTarget = LPlace;
    gS.loomPlace  = nearest;
    gS.haveAim    = true;
    gS.aim        = loomPoint(nearest);
    return nearest;
}
void loomAction(int t, unsigned bit, bool shift) {
    if (t >= 101 && t <= 103) { inject(t - 101, 1); gS.tapAt = gS.clock; return; } // take it (slotClick(hoverSlot, 0, shift))
    if (t < 200 || bit != 1) return;
    int k = t - 200, button = -1;
    for (int b = 0, n = 0; b < kButtons; ++b) if (gS.btnFull[b] && n++ == k) { button = b; break; }
    if (button < 0) return;
    // LoomStation.weave: pick the pattern as the window does, then the banner is taken when it is woven
    inject(300 + button, 1);
    gS.loomSelected = k;
    gS.loomWeave    = button;
    gS.moveOn = true; gS.moveAt = gS.clock; gS.moveLength = WEAVE_LENGTH; gS.moveBeat = L_BEAT; gS.moveBeatDone = false;
    gS.moveToInv  = shift;
    gS.passesDone = 0;
    gS.loomSnap2  = false;
    for (int i = 0; i < 4; ++i) { gS.wasFull[i] = gS.cellFull[i]; gS.wasBlock[i] = gS.cellBlock[i]; }
    std::memcpy(gS.snapBox, gS.iconBox, sizeof gS.snapBox);
    gSnapRequest = true;
    gS.tapAt = gS.clock;
}


// ---------------------------------------------------------------- EnchantStation (vanilla geometry)
// Hidden screen: grid[0] the item, grid[1] the lapis; buttons[0..2] the three offers, buttons[3..5] the right end of
// each (the level cost), buttons[6..8] its left end, buttons[9..11] the middle (the runes)
enum EnchTarget { ENone, ESurface, EItem, ELapis, EOffer };
/// how far into the rite, 0..1 of RITUAL_LENGTH, or -1
double ritualTime() { double t = moveTime(); return gS.st.kind == Kind::Enchanting && t >= 0 ? std::min(1.0, t / RITUAL_LENGTH) : -1; }
V3 enchItemCenter(double clock) { return gS.st.top + V3{0, 0.62 + std::sin(clock * 1.6) * 0.025, 0}; } // EnchantFx.itemCenter
V3 enchItemPos() {
    double t    = ritualTime();
    double lift = t >= 0 ? 0.1 * hump(window(t * RITUAL_LENGTH, 0.2, 1.3)) : 0;
    return enchItemCenter(gS.clock) + V3{0, lift, 0};
}
V3 lapisCorner() { return gS.st.top + gS.st.sideV() * LAPIS_NEAR + gS.st.right() * LAPIS_RIGHT + V3{0, LAPIS_LIFT, 0}; }
/// the offers hang in a crown over the item: the middle one highest, the outer ones beside it and towards the player
V3 sigilPos(int k) {
    int    off = k - 1;
    V3     at  = gS.st.top + V3{0, off == 0 ? OFFER_HIGH : OFFER_LOW, 0} + gS.st.sideV() * (off == 0 ? OFFER_FAR : OFFER_NEAR) + gS.st.right() * (off * OFFER_SPREAD);
    at = at + V3{0, 0.015 * std::sin(gS.clock * 1.3 + k * 2.1), 0};
    if (k == gS.denyOffer) {
        double d = gS.clock - gS.denyAt;
        if (d >= 0 && d < 0.35) at = at + gS.st.right() * (0.02 * std::sin(d * 60) * (1 - d / 0.35));
    }
    return at;
}
bool enchItem() { return gS.cellFull[0]; }
bool offered(int k) { return k >= 0 && k < 3 && (gS.useClean ? gS.cleanFull[k] : gS.btnFull[3 + k]) && enchItem(); } // an offer prints its level cost
/// Bedrock prints an offer's level cost green while the player can pay for it (red / dark when not)
bool affordable(int k) {
    if (!offered(k)) return false;
    float const* c = gS.useClean ? gS.cleanRgb[k] : gS.iconRgb[11 + 3 + k];
    return c[1] > c[0] + 0.12f && c[1] > c[2] + 0.12f;
}
/// the carried stack is lapis (its icon blue)
bool carryingLapis() { float const* c = gS.iconRgb[10]; return gS.carrying && c[2] > c[0] + 0.15f && c[2] > c[1] + 0.05f; }
bool underMin(Basis const& b, float mx, float my, V3 center, double radius, float minGui) { // EnchantStation.under (at least 8 GUI px)
    float ax, ay, ex, ey;
    if (!toPixel(gS.last, b, center, gW, gH, ax, ay) || !toPixel(gS.last, b, center + V3{0, radius, 0}, gW, gH, ex, ey)) return false;
    float gui = float(spatialcam::javaAutoGuiScale(gW, gH));
    return std::hypot(mx - ax, my - ay) < std::max(minGui * gui, std::hypot(ax - ex, ay - ey));
}
int enchCursor(Basis const& b, float mx, float my, V3 o, V3 d) {
    gS.enchTarget = ENone;
    gS.hoverOffer = -1;
    gS.aimAir     = false;
    bool rite = ritualTime() >= 0;
    if (!rite)
        for (int k = 0; k < 3; ++k)
            if (offered(k) && underMin(b, mx, my, sigilPos(k), 0.15, 8)) {
                gS.enchTarget = EOffer; gS.hoverOffer = k; gS.haveAim = true; gS.aim = sigilPos(k); gS.aimAir = true;
                return 200 + k;
            }
    if (enchItem() && underMin(b, mx, my, enchItemPos(), 0.15, 8)) {
        gS.enchTarget = EItem; gS.haveAim = true; gS.aim = enchItemPos(); gS.aimAir = true;
        return rite ? -1 : 0;
    }
    // the socket and lapis count point the hand to the same place
    V3 hit;
    if (underMin(b, mx, my, lapisCorner() + V3{0, 0.06, 0}, 0.09, 8) ||
        (onPlaneY(o, d, lapisCorner().y, hit) && (hit - lapisCorner()).len() <= LAPIS_RADIUS)) {
        gS.enchTarget = ELapis; gS.haveAim = true; gS.aim = lapisCorner(); gS.aimAir = true;
        return rite ? -1 : 1;
    }
    if (!onPlaneY(o, d, gS.st.top.y, hit)) return -1;
    V3 rel = hit - gS.st.top;
    if (std::fabs(rel.x) > 0.52 || std::fabs(rel.z) > 0.52) return -1;
    gS.enchTarget = ESurface;
    gS.haveAim    = true;
    gS.aim        = {gS.st.top.x + std::clamp(rel.x, -0.43, 0.43), gS.st.top.y, gS.st.top.z + std::clamp(rel.z, -0.43, 0.43)};
    return rite ? -1 : 0;
}
void enchAction(int t, unsigned bit, bool shift) {
    (void)shift;
    if (t < 200 || t > 202 || ritualTime() >= 0) return;
    int k = t - 200;
    if (!offered(k)) return;
    (void)bit;
    if (!affordable(k)) { gS.denyAt = gS.clock; gS.denyOffer = k; return; }
    // EnchantStation.startRitual: the window's own button; the item is drawn as it was until the light breaks
    gS.ritualFrom  = sigilPos(k);
    gS.ritualOffer = k;
    gS.ritualLapis = k + 1;
    gS.burstDone   = false;
    inject(300 + k, 1);
    gS.moveOn = true; gS.moveAt = gS.clock; gS.moveLength = RITUAL_LENGTH; gS.moveBeat = 1e9; gS.moveBeatDone = false;
    gS.moveToInv = false;
    for (int i = 0; i < 4; ++i) { gS.wasFull[i] = gS.cellFull[i]; gS.wasBlock[i] = gS.cellBlock[i]; }
    // the snapshot (texture, boxes, runes) is the clean one from before the offer was hovered
}

/// A press on an action target (MenuStation.click -> act): starts a move or clicks for the player.
std::string gActionNote = "-";
void stationAction(int t, unsigned bit, bool shift) {
    {
        char b[96];
        snprintf(b, sizeof b, "action %d button %u shift %d at %.2f", t, bit, shift, gS.clock);
        gActionNote = b;
    }
    if (gS.st.kind == Kind::Anvil && t == 100) {
        if (bit == 1 && gS.resultFull3()) {
            // AnvilStation.forge: three blows; the first one takes the result (the server checks the cost)
            gS.moveOn = true; gS.moveAt = gS.clock; gS.moveLength = FORGE_LENGTH; gS.moveBeat = BLOWS[0]; gS.moveBeatDone = false;
            gS.moveToInv = shift; gS.blowsDone = 0;
            gS.mainWasFull = gS.cellFull[0]; gS.addWasFull = gS.cellFull[1];
            gS.mainWasBlock = gS.cellBlock[0]; gS.addWasBlock = gS.cellBlock[1];
            std::memcpy(gS.snapBox, gS.iconBox, sizeof gS.snapBox);
            gSnapRequest = true;
        } else {
            inject(gS.cellFull[1] ? 1 : 0, 1); // take (the window's own click with the left button)
        }
        gS.tapAt = gS.clock;
    }
    if (gS.st.kind == Kind::Stonecutter) { stoneAction(t, bit, shift); return; }
    if (gS.st.kind == Kind::Smithing) { smithAction(t, bit, shift); return; }
    if (gS.st.kind == Kind::Cartography) { cartAction(t, bit, shift); return; }
    if (gS.st.kind == Kind::Loom) { loomAction(t, bit, shift); return; }
    if (gS.st.kind == Kind::Enchanting) { enchAction(t, bit, shift); return; }
    if (gS.st.kind == Kind::Grindstone && (t == 101 || t == 102)) {
        if (bit == 1 && gS.resultFull3()) {
            // GrindstoneStation.grind: the result is taken when the blade first touches the stone
            gS.moveOn = true; gS.moveAt = gS.clock; gS.moveLength = GRIND_LENGTH; gS.moveBeat = G_CONTACT; gS.moveBeatDone = false;
            gS.moveToInv = shift;
            gS.mainWasFull = gS.cellFull[0]; gS.addWasFull = gS.cellFull[1];
            gS.mainWasBlock = gS.cellBlock[0]; gS.addWasBlock = gS.cellBlock[1];
            gS.glintWas[0] = gS.cellGlint[0]; gS.glintWas[1] = gS.cellGlint[1];
            gS.resultWasBlock = gS.cellBlock[2];
            gS.grindPrevT = -1;
            gS.stripping = (gS.cellFull[0] && gS.cellGlint[0]) || (gS.cellFull[1] && gS.cellGlint[1]);
            std::memcpy(gS.snapBox, gS.iconBox, sizeof gS.snapBox);
            gSnapRequest = true;
        } else {
            inject(t - 101, 1); // take it (slotClick(hoverSlot, 0))
        }
        gS.tapAt = gS.clock;
    }
}
void stoneAction(int t, unsigned bit, bool shift) {
    if (t == 103) { inject(0, bit); gS.tapAt = gS.clock; return; } // the stone: take it (slotClick(0, button, shift))
    if (t < 200) return;
    int k = t - 200, button = shapeButton(k);
    if (bit != 1 || button < 0) { gActionNote += " - no cut (button " + std::to_string(button) + ")"; return; } // only the left button cuts (deny)
    gActionNote += " - cut shape " + std::to_string(k) + " button " + std::to_string(button);
    // StonecutterStation.cut: pick the recipe as the window does, then the move takes the piece when the blade bites
    if (gS.selectedShape != k) inject(300 + button, 1);
    gS.selectedShape = k;
    gS.cutShape  = k;
    gS.cutAll    = shift;
    gS.cutLength = shift ? CUT_ALL_LENGTH : CUT_LENGTH;
    gS.moveOn = true; gS.moveAt = gS.clock; gS.moveLength = gS.cutLength; gS.moveBeat = S_BITE; gS.moveBeatDone = false;
    gS.moveToInv = shift;
    gS.mainWasFull = gS.cellFull[0]; gS.mainWasBlock = gS.cellBlock[0];
    gS.resultWasBlock = gS.btnBlock[button];
    std::memcpy(gS.snapBox, gS.iconBox, sizeof gS.snapBox);
    gSnapRequest = true;
    gS.tapAt = gS.clock;
}
/// Every frame: the move's beat lands on time (MenuStation.frame).
void moveFrame() {
    if (!gS.moveOn) return;
    double t = gS.clock - gS.moveAt;
    if (!gS.moveBeatDone && t >= gS.moveBeat) {
        gS.moveBeatDone = true;
        if (gS.phase == Phase::Active && gS.st.kind == Kind::Stonecutter) {
            inject(1, 1); // the piece (the result can still be on its way from the server: no check)
        } else if (gS.phase == Phase::Active && gS.st.kind == Kind::Smithing) {
            if (gS.cellFull[3]) inject(3, 1);
            else gS.moveOn = false;
        } else if (gS.phase == Phase::Active && gS.st.kind == Kind::Loom) {
            if (gS.cellFull[3]) inject(3, 1);
            else gS.moveOn = false;
        } else if (gS.phase == Phase::Active && gS.st.kind == Kind::Cartography) {
            if (gS.cellFull[2]) inject(2, 1);
            else gS.moveOn = false;
        } else if (gS.phase == Phase::Active && (gS.st.kind == Kind::Anvil || gS.st.kind == Kind::Grindstone)) {
            if (gS.resultFull3()) inject(2, 1);
            else gS.moveOn = false; // nothing to take any more: the move stops (cancelMove)
        }
    }
    // the loom keeps the woven banner (result slot) just before it is taken
    if (gS.st.kind == Kind::Loom && !gS.loomSnap2 && t >= L_BEAT - 0.15) {
        gS.loomSnap2 = true;
        std::memcpy(gS.snapBox, gS.iconBox, sizeof gS.snapBox);
        gSnapRequest = true;
    }
    if (t > gS.moveLength) gS.moveOn = false;
}

/// Target ids: 0..8 grid (recipe order), 9 result, 10..18 hotbar, 19..45 pack, -1 none
int currentTarget0(float mx, float my);
/// the hidden slot whose item is under the cursor (its tooltip), else -1
int tipSlotOf(int t) {
    if (t >= 10 && t < 46) return t; // the drawer
    if (t >= 0 && t < 9) return gS.cellFull[t] ? t : -1;
    if (t == 9) return gS.resultFull ? 9 : -1;
    if (t >= 101 && t <= 104) return gS.cellFull[t - 101] ? t - 101 : -1;
    if (gS.st.kind == Kind::Anvil && t == 100) return gS.cellFull[1] ? 1 : gS.cellFull[0] ? 0 : -1;
    if (gS.st.kind == Kind::Stonecutter && t == 103) return gS.cellFull[0] ? 0 : -1;
    return -1;
}
int currentTarget(float mx, float my) {
    int t = currentTarget0(mx, my);
    int s = gS.phase == Phase::Active && !moving() ? tipSlotOf(t) : -1;
    // the enchanting table's item and lapis are slots of their own targets
    if (gS.st.kind == Kind::Enchanting && (gS.enchTarget == EItem) && enchItem() && !moving()) s = 0;
    if (s != gS.tipSlot) { gS.tipSlot = s; gS.tipSince = gS.clock; }
    return t;
}
int currentTarget0(float mx, float my) {
    if (gS.phase != Phase::Active || !gS.haveLast || gW <= 0) { gS.haveAim = false; return -1; }
    if (!gS.cursorEngaged) {
        if (std::fabs(mx - gS.cursorStartX) + std::fabs(my - gS.cursorStartY) < 4) return -1;
        gS.cursorEngaged = true;
    }
    DrawerGeom dg = drawerGeom(gW, gH);
    gS.overPanel  = overDrawer(dg, mx, my);
    gS.hoverCell = -1;
    gS.hoverResult = false;
    gS.hoverDrawer = -1;
    gS.haveAim = false;
    if (gS.overPanel) {
        int s = drawerSlotAt(dg, mx, my);
        gS.hoverDrawer = s;
        return s < 0 ? -1 : s < 9 ? 10 + s : 19 + (s - 9);
    }
    Basis b  = basisOf(gS.last);
    float nx = 2 * mx / gW - 1, ny = 1 - 2 * my / gH;
    if (gS.st.kind == Kind::Anvil) {
        V3 o, d;
        cursorRay(gS.last, b, nx, ny, o, d);
        return anvilCursor(b, mx, my, o, d);
    }
    if (gS.st.kind == Kind::Grindstone) {
        V3 o, d;
        cursorRay(gS.last, b, nx, ny, o, d);
        return grindCursor(b, mx, my, o, d);
    }
    if (gS.st.kind == Kind::Stonecutter) {
        V3 o, d;
        cursorRay(gS.last, b, nx, ny, o, d);
        return stoneCursor(b, mx, my, o, d);
    }
    if (gS.st.kind == Kind::Smithing) {
        V3 o, d;
        cursorRay(gS.last, b, nx, ny, o, d);
        return smithCursor(b, mx, my, o, d);
    }
    if (gS.st.kind == Kind::Cartography) {
        V3 o, d;
        cursorRay(gS.last, b, nx, ny, o, d);
        return cartCursor(b, mx, my, o, d);
    }
    if (gS.st.kind == Kind::Loom) {
        V3 o, d;
        cursorRay(gS.last, b, nx, ny, o, d);
        return loomCursor(b, mx, my, o, d);
    }
    if (gS.st.kind == Kind::Enchanting) {
        V3 o, d;
        cursorRay(gS.last, b, nx, ny, o, d);
        return enchCursor(b, mx, my, o, d);
    }
    if (isFurnace(gS.st.kind)) {
        // FurnaceStation.updateCursor (vanilla geometry): the front face, and the top where a zone lies there
        FLayout const& fl = furnaceLayout(gS.st.kind);
        V3 o, d, hit;
        cursorRay(gS.last, b, nx, ny, o, d);
        double best = 1e18;
        int    zone = -1;
        auto pick = [&](bool onTop, double u, double v) {
            gS.aimTop = onTop;
            zone      = -1;
            for (int z = 0; z < 3; ++z)
                if (fl.zone[z].top == onTop && u >= fl.zone[z].u0 && u <= fl.zone[z].u1 && v >= fl.zone[z].v0 && v <= fl.zone[z].v1) { zone = z; break; }
            gS.haveAim = true;
            gS.aim     = gS.st.fpoint(onTop, std::clamp(u, 0.08, 0.92), std::clamp(v, 0.05, 0.95), 0);
        };
        V3     n = gS.st.sideV(), face = gS.st.center() + n * 0.5; // the front face
        double dn   = d.dot(n);
        if (std::fabs(dn) > 1e-9) {
            double t = (face - o).dot(n) / dn;
            if (t > 0) {
                V3     h = o + d * t;
                double u = (h - face).dot(gS.st.right()) + 0.5, v = h.y - gS.st.by;
                if (u > -0.08 && u < 1.08 && v > -0.08 && v < 1.08) { best = t; pick(false, u, v); }
            }
        }
        bool usesTop = fl.zone[0].top || fl.zone[1].top || fl.zone[2].top;
        if (usesTop && onPlaneY(o, d, gS.st.by + 1.0, hit) && (hit - o).len() < best) {
            V3     tc  = gS.st.fpoint(true, 0.5, 0.5, 0);
            V3     rel = hit - tc;
            double u = rel.dot(gS.st.right()) + 0.5, v = 0.5 - rel.dot(gS.st.sideV());
            if (u > -0.05 && u < 1.05 && v > -0.05 && v < 1.05) pick(true, u, v);
        }
        gS.hoverCell = zone;
        return zone;
    }
    // the floating result (TableStation.updateCursor: a disc of resultRadiusGui around it)
    if (gS.resultFull) {
        V3    at = gS.st.resultPosition(gS.clock);
        float ax, ay, bx2, by2;
        if (toPixel(gS.last, b, at, gW, gH, ax, ay) && toPixel(gS.last, b, at + V3{0, 0.1, 0}, gW, gH, bx2, by2)) {
            float gui = float(spatialcam::javaAutoGuiScale(gW, gH));
            float r   = std::max(10.0f * gui, std::hypot(ax - bx2, ay - by2) * 1.25f);
            if ((mx - ax) * (mx - ax) + (my - ay) * (my - ay) < r * r) { gS.hoverResult = true; return 9; }
        }
    }
    V3 o, d, hit;
    cursorRay(gS.last, b, nx, ny, o, d);
    if (!onPlaneY(o, d, gS.st.by + 1.0, hit)) return -1;
    double lx = hit.x - gS.st.bx, lz = hit.z - gS.st.bz;
    if (lx > -0.02 && lx < 1.02 && lz > -0.02 && lz < 1.02) {
        int world = gS.st.cellAt(lx, lz);
        gS.hoverCell = world;
        // TableStation.updateCursor: the working hand aims at the point under the cursor, kept on the top
        gS.haveAim = true;
        gS.aim = {std::clamp(hit.x, gS.st.bx + 0.07, gS.st.bx + 0.93), gS.st.by + 1.0, std::clamp(hit.z, gS.st.bz + 0.07, gS.st.bz + 0.93)};
        if (world < 0) return -1;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                if (gS.st.worldIndex(r, c) == world) return r * 3 + c;
    }
    return -1;
}

bool targetRect(Layout const& L, int t, Rect& r) {
    if (t >= 0 && t < 9 && L.hasGrid && (L.kind == int(Kind::Table) || t < L.nRoles)) { r = L.grid[t]; return true; }
    if (t == 9 && L.hasResult) { r = L.result; return true; }
    if (t >= 10 && t < 19) { r = L.hotbar[t - 10]; return true; }
    if (t >= 19 && t < 46) { r = L.inv[t - 19]; return true; }
    if (t >= 300 && t < 300 + L.nButtons) { r = L.buttons[t - 300]; return true; }
    return false;
}
/// Off every target the hidden screen's cursor waits in an empty corner (outside the container panel, so nothing is
/// hovered; clicks there are never let through). Vanilla draws the carried stack there, which we pick up as its icon.
void neutralPoint(Layout const& L, float& x, float& y) {
    float s = L.slot > 0 ? L.slot : 64;
    x = float(L.W) - 2.5f * s;
    y = 1.5f * s;
}

std::atomic<int> gTargetNow{-1};
std::atomic<int> gGoodTarget{-1};  // the last target the cursor was really over, and when (a press a moment later still goes there)
std::atomic<double> gGoodAt{-1e9};
long gLiveCalls = 0;
int  gLiveX = 0, gLiveY = 0;
} // namespace

// ================================================================ public: config
char const* kindName(Kind k) { return kNames[int(k)]; }
char const* kindKey(Kind k) { return kKeys[int(k)]; }
Config&     cfg() { return gCfg; }

void loadConfig(std::filesystem::path const& configDir) {
    gCfgFile = configDir / "CinematicInteractions.json";
    std::ifstream in(gCfgFile);
    if (!in) { saveConfig(); return; }
    try {
        nlohmann::json j;
        in >> j;
#define RD(f) if (j.contains(#f)) gCfg.f = j[#f].get<decltype(gCfg.f)>();
        RD(enabled) RD(approachSpeed) RD(leaveSpeed) RD(leanStrength) RD(sceneZoom) RD(quickMode) RD(quickWindow) RD(letterbox)
        RD(livingCamera) RD(cameraShake) RD(screenFlash) RD(reduceMotion) RD(controlHints) RD(hideHand) RD(writeFov) RD(hoverTargets)
        RD(dumpUi) RD(keyTolerance) RD(fullLean) RD(armStyle) RD(drawArms) RD(cubeItems) RD(devScenes)
#undef RD
        if (j.contains("stations"))
            for (int k = 0; k < int(Kind::Count); ++k)
                if (j["stations"].contains(kKeys[k])) {
                    std::string m = j["stations"][kKeys[k]].get<std::string>();
                    gCfg.mode[k] = m == "spatial" ? 1 : m == "vanilla" ? 2 : 0;
                }
    } catch (...) {
    }
}
void saveConfig() {
    if (gCfgFile.empty()) return;
    nlohmann::json j;
#define WR(f) j[#f] = gCfg.f;
    WR(enabled) WR(approachSpeed) WR(leaveSpeed) WR(leanStrength) WR(sceneZoom) WR(quickMode) WR(quickWindow) WR(letterbox)
    WR(livingCamera) WR(cameraShake) WR(screenFlash) WR(reduceMotion) WR(controlHints) WR(hideHand) WR(writeFov) WR(hoverTargets)
    WR(dumpUi) WR(keyTolerance) WR(fullLean) WR(armStyle) WR(drawArms) WR(cubeItems) WR(devScenes)
#undef WR
    for (int k = 0; k < int(Kind::Count); ++k) j["stations"][kKeys[k]] = gCfg.mode[k] == 1 ? "spatial" : gCfg.mode[k] == 2 ? "vanilla" : "cinematic";
    std::ofstream out(gCfgFile, std::ios::trunc);
    if (out) out << j.dump(4);
}

// ================================================================ public: client thread
Route onScreen(std::string const& containerTop) {
    std::lock_guard l(gM);
    if (containerTop.empty()) {
        leave();
        return Route::None;
    }
    if (gS.phase == Phase::Intro || gS.phase == Phase::Active) return Route::Cinematic; // same scene, screen still up
    // which block was used: the hit result the use went to
    std::string lower = containerTop;
    for (auto& ch : lower) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    if (lower.find("inventory_screen") != std::string::npos) { gS.lastRoute = "inventory: Spatial"; return Route::Spatial; }
    int         bx, by, bz;
    std::string type;
    if (!game::hitBlock(bx, by, bz, type)) { gS.lastRoute = "no block hit: Spatial"; return Route::Spatial; }
    Kind kind;
    if (!kindOfBlock(type, kind)) { gS.lastRoute = type + ": Spatial"; return Route::Spatial; }
    Mode m = Mode(gCfg.mode[int(kind)]);
    if (m == Mode::Vanilla) { gS.lastRoute = std::string(kNames[int(kind)]) + ": Vanilla"; return Route::Vanilla; }
    if (m == Mode::Spatial || !gCfg.enabled) { gS.lastRoute = std::string(kNames[int(kind)]) + ": Spatial"; return Route::Spatial; }
    if (!sceneAvailable(kind) && !gCfg.devScenes) { gS.lastRoute = std::string(kNames[int(kind)]) + ": scene not ported yet, Spatial"; return Route::Spatial; }
    if (gS.phase == Phase::Outro) finish(); // used again while still stepping away
    if (!begin(kind, bx, by, bz)) { gS.lastRoute = std::string(kNames[int(kind)]) + ": " + gS.refused + ", Spatial"; return Route::Spatial; }
    gS.lastRoute = std::string(kNames[int(kind)]) + ": Cinematic";
    return Route::Cinematic;
}

void tick() {
    std::lock_guard l(gM);
    if (gS.phase == Phase::None || gS.phase == Phase::Outro) return;
    double f[3];
    float  py, pp;
    bool   alive;
    if (!game::playerState(f, py, pp, alive)) { finish(); return; }
    if (!alive) { finish(); return; }
    V3     c  = gS.st.center();
    double d2 = (f[0] - c.x) * (f[0] - c.x) + (f[1] - c.y) * (f[1] - c.y) + (f[2] - c.z) * (f[2] - c.z);
    Kind        k;
    std::string name = game::blockName(gS.st.bx, gS.st.by, gS.st.bz);
    gS.st.lit        = name.rfind("minecraft:lit_", 0) == 0;
    if (d2 > 49 || !kindOfBlock(name, k) || k != gS.st.kind) leave();
}

bool active() { std::lock_guard l(gM); return gS.phase != Phase::None; }
bool wantsRedirect() { std::lock_guard l(gM); return gS.phase == Phase::Intro || gS.phase == Phase::Active; }

// ================================================================ public: camera
void noteProjection(float tanHalf, float aspect, float fovYDeg) {
    std::lock_guard l(gM);
    gS.tanHalf = tanHalf;
    gS.aspect  = aspect;
    gS.fovY    = fovYDeg;
    gS.projKnown = true;
    if (gS.phase == Phase::None) gS.vanillaFovY = fovYDeg;
}

bool cameraPose(float const eye[3], float yaw, float pitch, Pose& out, float& fovScale) {
    std::lock_guard l(gM);
    frame();
    fovScale = 1.0f;
    if (gS.phase == Phase::None) { gS.fovScaleNow = 1.0; return false; }
    JPose p = overridePose(V3{eye[0], eye[1], eye[2]}, yaw, pitch);
    out.pos[0] = float(p.pos.x); out.pos[1] = float(p.pos.y); out.pos[2] = float(p.pos.z);
    out.yaw = p.yaw; out.pitch = p.pitch; out.roll = p.roll;
    // CameraRig.fov: lerp(weight * sceneZoom, vanilla, leanFov)
    if (gCfg.writeFov) {
        double vanilla = gS.vanillaFovY;
        double punch   = cameraShake() ? 7 * cameraKick() : 0;
        double fov     = vanilla + (gS.st.leanFov() - vanilla) * (gS.weight * pct(gCfg.sceneZoom)) - punch;
        fovScale       = float(fov / std::max(1.0, vanilla));
    }
    gS.fovScaleNow = fovScale;
    return true;
}

// ================================================================ public: input
bool mapClient(int w, int h, int& x, int& y, int liveIn) {
    bool live = liveIn > 0;
    std::lock_guard l(gM);
    if (gS.phase != Phase::Intro && gS.phase != Phase::Active) return false;
    gW = w;
    gH = h;
    Layout L;
    {
        std::lock_guard ll2(gLayoutMutex);
        L = gLayout;
    }
    // only the game's own mouse reading moves the cursor: the other ways the game asks (cursor position, window
    // messages, already remapped points) are answered with the hidden screen's point for the target of that reading
    int t = gTargetNow.load();
    static double lastLive = -1e9;
    double        nowL     = nowMs();
    if (live) lastLive = nowL;
    else if (liveIn == 0 && nowL - lastLive > 300) live = true; // no GameInput readings: the other ways are all there is
    if (live && x == 0 && y == 0) live = false; // a reading without a real position
    if (live) {
        ++gLiveCalls;
        gLiveX = x; gLiveY = y;
        gCursorNdcX = 2.0f * x / std::max(1, w) - 1.0f;
        gCursorNdcY = 1.0f - 2.0f * y / std::max(1, h);
        gCursorValid = true;
        if (!gS.cursorEngaged && gS.phase == Phase::Active && gS.time < 0.05) { gS.cursorStartX = float(x); gS.cursorStartY = float(y); }
        t = currentTarget(float(x), float(y));
        gTargetNow = t;
        if (t >= 0) { gGoodTarget = t; gGoodAt = nowMs(); }
    }
    double now = nowMs();
    // a press goes to its target, a drag over other targets follows them (sharing a stack), the release lands where
    // the press is; otherwise the hidden screen's cursor waits in an empty corner
    int  use = gPress.state != PressState::Idle ? (gPress.state == PressState::Down && t >= 0 ? t : gPress.target) : t;
    bool grace = gPress.state == PressState::Idle && now - gPress.releasedAt < 150 && gPress.target >= 0;
    if (grace) use = gPress.target;
    bool mapToTarget = use >= 0 && (gCfg.hoverTargets || gPress.state != PressState::Idle || grace);
    {
        int      it;
        unsigned ib;
        bool     idown;
        if (injectActive(now, it, ib, idown)) { use = it; mapToTarget = true; }
    }
    if (gPress.state == PressState::Down && t >= 0) gPress.target = t;
    Rect r;
    float fx, fy;
    // an enchantment offer under the cursor: the hidden screen's cursor rests on its button (nothing is clicked there:
    // presses on offers are the scene's), so the window shows the offer's clue as its tooltip
    bool clueHover = !mapToTarget && gS.st.kind == Kind::Enchanting && t >= 200 && t <= 202 && gPress.state == PressState::Idle;
    // an item tooltip probe: the cursor rests on that slot a moment (presses go where they always go)
    if (!mapToTarget && !clueHover && gS.probe >= 0 && gPress.state == PressState::Idle) { use = gS.probe; mapToTarget = true; }
    if (L.valid && L.kind == int(gS.st.kind) && mapToTarget && targetRect(L, use, r)) { fx = r.cx(); fy = r.cy(); }
    else if (L.valid && L.kind == int(gS.st.kind) && clueHover && L.nButtons >= 3) { fx = L.buttons[t - 200].cx(); fy = L.buttons[t - 200].cy(); }
    else if (L.valid) neutralPoint(L, fx, fy);
    else { fx = float(w) - 100.0f; fy = 60.0f; }
    x = int(fx);
    y = int(fy);
    gMapX = fx; gMapY = fy; gMapValid = true;
    return true;
}

bool lastMapped(int& x, int& y) {
    std::lock_guard l(gM);
    x = int(gMapX); y = int(gMapY);
    return gMapValid;
}

unsigned filterButtons(unsigned buttons) {
    std::lock_guard l(gM);
    if (gS.phase != Phase::Intro && gS.phase != Phase::Active) { gPress = Press{}; return buttons; }
    double   now = nowMs();
    unsigned out = 0;
    unsigned bit = gPress.bit;
    {   // our own click under way: the real buttons wait
        int      it;
        unsigned ib;
        bool     idown;
        if (injectActive(now, it, ib, idown)) {
            gPrevButtons = buttons & 7u;
            gHeld        = idown ? ib : 0;
            return (buttons & ~7u) | (idown ? ib : 0);
        }
    }
    switch (gPress.state) {
    case PressState::Swallow: // a press that started a move or a click of ours: nothing of it reaches the screen
        if (!(buttons & bit)) { gPress.state = PressState::Idle; gPress.releasedAt = now; gPress.target = -1; }
        break;
    case PressState::Idle: {
        int t = gTargetNow.load();
        if (t < 0 && gGoodTarget.load() >= 0 && now - gGoodAt.load() < 200) t = gGoodTarget.load();
        for (unsigned b = 1; b <= 4; b <<= 1) {
            if (!(buttons & b) || (gPrevButtons & b)) continue; // a new press
            if (gS.phase == Phase::Active && t >= 100) {
                gPress = Press{PressState::Swallow, b, -1, now, -1e9};
                stationAction(t, b, (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
            } else if (gS.phase == Phase::Active && t >= 0) {
                // first the hidden screen's cursor moves onto the slot (hover), then the button goes down there
                gPress = Press{PressState::Hover, b, t, now, -1e9};
                gS.tapAt = gS.clock; // Hands.tap
                if (t == 9) gS.craftAt = gS.clock; // TableStation.craft: the hands press the ingredients together
            }
            break;
        }
        break;
    }
    case PressState::Hover:
        if (now - gPress.since >= 45) {
            gPress.state = (buttons & bit) ? PressState::Down : PressState::ForceDown;
            gPress.since = now;
            out |= bit;
        }
        break;
    case PressState::Down:
        if (buttons & bit) out |= bit;
        else { gPress.state = PressState::Idle; gPress.releasedAt = now; }
        break;
    case PressState::ForceDown: // the real button was already up: hold it long enough to be seen, then let go
        if (now - gPress.since < 70) out |= bit;
        else { gPress.state = PressState::Idle; gPress.releasedAt = now; }
        break;
    }
    {   // press log (diag): every change of the real or the passed buttons
        static unsigned lastIn = 0, lastOut = 0;
        if ((buttons & 7u) != lastIn || out != lastOut) {
            char e[200];
            snprintf(e, sizeof e, "%.0f ms: real %x -> passed %x, state %d target %d (now %d) at %.0f,%.0f; live %ld at %d,%d", now, buttons & 7u, out,
                     int(gPress.state), gPress.target, gTargetNow.load(), gMapX, gMapY, gLiveCalls, gLiveX, gLiveY);
            gPressLog[gPressLogN++ % 12] = e;
            lastIn = buttons & 7u;
            lastOut = out;
        }
    }
    gPrevButtons = buttons & 7u;
    gHeld = gPress.state == PressState::Down || gPress.state == PressState::ForceDown ? bit : 0;
    return (buttons & ~7u) | out;
}

// ================================================================ rendering + slot detection
namespace {
struct Gfx {
    ComPtr<ID3D12Device>         dev;
    ComPtr<ID3D12RootSignature>  root;
    ComPtr<ID3D12PipelineState>  pso;
    DXGI_FORMAT                  fmt = DXGI_FORMAT_UNKNOWN;
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    UINT                         srvInc = 0;
    ComPtr<ID3D12Resource>       keep;  // last seen hidden screen (kept for the closing shot)
    UINT                         kw = 0, kh = 0;
    DXGI_FORMAT                  kfmt = DXGI_FORMAT_UNKNOWN;
    bool                         keepValid = false;
    bool                         broken = false;
    std::string                  status = "idle";
    // full readback (slot detection)
    ComPtr<ID3D12Resource>       rb;
    UINT64                       rbSize = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    long                         rbFrame = -1; // frame the copy was recorded in (-1 none pending)
    ULONGLONG                    lastDetect = 0;
    // small readback ring (station slot contents)
    static constexpr int         kRing = 4;
    ComPtr<ID3D12Resource>       sr[kRing];
    long                         srFrame[kRing] = {-1, -1, -1, -1};
    UINT                         srPitch = 0, srCell = 0; // px per sampled cell side
    UINT64                       srStride = 0;            // bytes per sampled slot (512-aligned placement)
    long                         frameNo = 0;
    // the player's skin (arms)
    ComPtr<ID3D12Resource>       skinTex, skinUp;
    ComPtr<ID3D12Resource>       propTex, propUp; // the hammer atlas (HammerModel.h)
    ComPtr<ID3D12Resource>       snap;            // the hidden screen when a move began (what lay on the block)
    ComPtr<ID3D12Resource>       tip;             // the hidden screen's item tooltips, copied where they were (t5)
    ComPtr<ID3D12Resource>       cand;            // the game thread's copy of the last whole UI frame, before it is checked
    ComPtr<ID3D12DescriptorHeap> rtvHeap;         // keep as a render target (the checked copy is drawn into it)
    ComPtr<ID3D12PipelineState>  psoCopy;         // no blending: cand -> keep
    DXGI_FORMAT                  copyFmt = DXGI_FORMAT_UNKNOWN;
    bool                         snapValid = false;
    long                         skinSerial = -1;
    bool                         skinValid = false;
    int                          skinW = 0, skinH = 0;
} gG;

std::atomic_bool gDetectBusy{false};
std::filesystem::path gDumpDir;

const char* kShader = R"(
cbuffer C : register(b0) {
    float4 corner[4];
    float4 uv01;   // uv of corners 0 and 1
    float4 uv23;   // uv of corners 2 and 3
    float4 color;
    float4 mode;   // x: 0 solid, 1 hidden screen, 2 skin; y: key on; z: key tolerance; w: opacity
    float4 key0;   // slot background
    float4 key1;   // hovered slot background
    float4 hull[4]; // a convex polygon (pixels, in order) the quad is hidden behind: what stands in front of it in the world
    float4 occl;    // x: the polygon's corner count (0: none)
};
Texture2D    tUi   : register(t0);
Texture2D    tSkin : register(t1);
Texture2D    tProp : register(t2);
Texture2D    tSnap : register(t3);
Texture2D    tCand : register(t4);
Texture2D    tTip  : register(t5);
SamplerState sP    : register(s0);
SamplerState sL    : register(s1);
struct V2P { float4 pos : SV_Position; float2 uv : TEXCOORD0; float2 local : TEXCOORD1; };
V2P VS(uint id : SV_VertexID) {
    static const uint idx[6] = {0, 1, 2, 2, 1, 3};
    uint k = idx[id];
    V2P o;
    o.pos = corner[k];
    o.uv  = k == 0 ? uv01.xy : k == 1 ? uv01.zw : k == 2 ? uv23.xy : uv23.zw;
    o.local = float2(k == 1 || k == 3 ? 1 : 0, k >= 2 ? 1 : 0);
    return o;
}
bool behind(float2 p) {
    int n = int(occl.x + 0.5);
    if (n < 3) return false;
    float sgn = 0;
    for (int k = 0; k < 8; ++k) {
        if (k >= n) break;
        float4 A = hull[k / 2], B = hull[((k + 1) % n) / 2];
        float2 a = (k % 2) == 0 ? A.xy : A.zw, b = (((k + 1) % n) % 2) == 0 ? B.xy : B.zw;
        float  c = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
        if (sgn == 0) sgn = sign(c);
        else if (c * sgn < 0) return false;
    }
    return true;
}
bool near3(float4 p, float3 c) { float3 q = p.rgb / max(p.a, 1e-3); return p.a > 0.9 && all(abs(q - c) < 0.07); }
float4 PS(V2P i) : SV_Target {
    if (mode.x > 5.5 && mode.x < 6.5) { // 6: the kept screen, copied only from a frame that has the screen drawn (probes)
        if (mode.y > 0.5) {
            bool ok = false;
            for (int k = 0; k < 2; ++k) {
                float4 pr = k == 0 ? key0 : key1;
                bool bevel = near3(tCand.Load(int3(pr.xy, 0)), color.rgb) || near3(tCand.Load(int3(pr.xy + float2(0, 1), 0)), color.rgb) ||
                            near3(tCand.Load(int3(pr.xy - float2(0, 1), 0)), color.rgb);
                ok = ok || (bevel && near3(tCand.Load(int3(pr.zw, 0)), uv01.xyz));
            }
            if (!ok) discard;
        }
        return tCand.Load(int3(i.pos.xy, 0));
    }
    if (behind(i.pos.xy)) discard;
    if (mode.x < 0.5) return (mode.y > 0.5 ? float4(color.rgb * color.a, 0) : float4(color.rgb * color.a, color.a)) * mode.w; // y: additive light
    if (mode.x > 6.5 && mode.x < 7.5) { // 7: a light texture of the atlas (RenderType.eyes: added light), smooth
        float2 dx = ddx(i.uv) * 0.25, dy = ddy(i.uv) * 0.25; // four smooth taps over the pixel (the circle is minified)
        float4 k = (tProp.SampleLevel(sL, i.uv + dx + dy, 0) + tProp.SampleLevel(sL, i.uv + dx - dy, 0) +
                    tProp.SampleLevel(sL, i.uv - dx + dy, 0) + tProp.SampleLevel(sL, i.uv - dx - dy, 0)) * 0.25;
        return float4(k.rgb * color.rgb * color.a, 0) * mode.w;
    }
    if (mode.x > 7.5 && mode.x < 8.5) { // 8: the rune circle (entityTranslucent): white mask, colour top -> key0 at the bottom edge
        float4 k = tProp.SampleLevel(sP, i.uv, 0);
        float  a = k.a * color.a * mode.w;
        return float4(k.rgb * lerp(color.rgb, key0.rgb, i.local.y) * a, a);
    }
    if (mode.x > 9.5) { // 10: an item tooltip of the hidden screen (copied): its writing only (the box, border and shadows dark)
        float4 u = tTip.SampleLevel(sP, i.uv, 0);
        float3 c = u.rgb; // as stored: over the window's see-through ground the box keeps its colours, not its alpha
        if (all(abs(c - float3(0.106, 0.047, 0.106)) < 0.04) || all(abs(c - float3(0.173, 0.031, 0.388)) < 0.05)) discard; // its ground, its border
        float a = color.a * mode.w;
        return float4(c * a, a);
    }
    if (mode.x > 8.5) { // 9: glyphs of the hidden screen (cost digits, runes) taken in another colour: the ground keyed away
        float4 u = mode.y > 0.5 ? tSnap.SampleLevel(sP, i.uv, 0) : tUi.SampleLevel(sP, i.uv, 0);
        float3 c = u.rgb / max(u.a, 1e-3);
        if (u.a < 0.5 || all(abs(c - key0.rgb) < mode.z)) discard;
        // key1.w: only a slot's count digits (white, #3F3F3F shadow); key1.x / y: the most / least saturation kept
        if (key1.w > 0.5 && !(all(c > 0.96) || all(abs(c - 0.247) < 0.03))) discard;
        float  sat = max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b));
        if ((key1.x > 0 && sat > key1.x) || sat < key1.y || max(c.r, max(c.g, c.b)) < key1.z) discard;
        float  v = key0.w > 0.5 ? 1 : max(c.r, max(c.g, c.b)); // key0.w: one flat colour
        float  a = color.a * mode.w;
        return float4(color.rgb * v * a, a);
    }
    if (mode.x > 4.5) { // 5: a soft round glow (sparks, the anvil's surface disc), added light
        float2 c = i.uv * 2 - 1;
        float  f = saturate(1 - dot(c, c));
        return float4(color.rgb * color.a * f * f, 0) * mode.w;
    }
    if (mode.x > 1.5 && mode.x < 3.5) { // 2 skin, 3 prop (hammer): alpha tested, shaded
        float4 k = mode.x < 2.5 ? tSkin.SampleLevel(sP, i.uv, 0) : tProp.SampleLevel(sP, i.uv, 0);
        if (k.a < 0.1) discard;
        return float4(k.rgb * color.rgb, 1.0) * mode.w;
    }
    float4 u = mode.x > 3.5 ? tSnap.SampleLevel(sP, i.uv, 0) : tUi.SampleLevel(sP, i.uv, 0); // 4: the snapshot
    if (mode.y > 0.5 && u.a > 0.0) {
        float3 c = u.rgb / max(u.a, 1e-3);
        if (all(abs(c - key0.rgb) < mode.z) || all(abs(c - key1.rgb) < mode.z)) u = 0;
        if (all(abs(c - float3(106, 173, 67) / 255.0) < 0.03)) u = 0; // Bedrock's green hovered (focused) slot
        // the stack count the slot prints in its lower right corner (white digits, #3F3F3F shadow) is not the item
        float lx = key1.w > 0.5 ? 1 - i.local.x : i.local.x;
        if (key0.w > 0.5 && lx > 0.42 && i.local.y > 0.5 && (all(c > 0.96) || all(abs(c - 0.247) < 0.03))) u = 0;
        // occl.y: the stack count's own box (uv, hull[0]) on a block's cube faces: its digits and their shadow
        if (occl.y > 0.5 && all(i.uv >= hull[0].xy) && all(i.uv <= hull[0].zw) && (all(c > 0.96) || all(abs(c - 0.247) < 0.03))) u = 0;
    }
    return float4(u.rgb * color.rgb, u.a) * color.a * mode.w;
}
)";

bool fail(std::string const& what, HRESULT hr) {
    char b[16];
    snprintf(b, sizeof b, "%08X", uint32_t(hr));
    gG.status = "off: " + what + " failed (hr=0x" + b + ")";
    gG.broken = true;
    ll_shim::error("[RF] cinematic " + gG.status);
    return false;
}

D3D12_GRAPHICS_PIPELINE_STATE_DESC gPsoDesc{};
ComPtr<ID3DBlob> gVs, gPs;
bool createPipeline(ID3D12Device* dev, DXGI_FORMAT fmt) {
    if (gG.pso && gG.fmt == fmt) return true;
    gG.dev = dev;
    if (!gG.root) {
        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6, 0, 0, 0};
        D3D12_ROOT_PARAMETER   p[2]{};
        p[0].ParameterType    = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        p[0].Constants        = {0, 0, 60};
        p[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        p[1].ParameterType    = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        p[1].DescriptorTable  = {1, &range};
        p[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC ss[2]{};
        D3D12_STATIC_SAMPLER_DESC& s = ss[0];
        s.Filter   = D3D12_FILTER_MIN_MAG_MIP_POINT;
        s.AddressU = s.AddressV = s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        s.MaxLOD   = D3D12_FLOAT32_MAX;
        s.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        ss[1] = s; // s1: smooth (the soft light textures)
        ss[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        ss[1].ShaderRegister = 1;
        D3D12_ROOT_SIGNATURE_DESC rs{2, p, 2, ss, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ComPtr<ID3DBlob> blob, err;
        HRESULT hr = D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
        if (FAILED(hr)) return fail("D3D12SerializeRootSignature", hr);
        if (FAILED(hr = dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&gG.root))))
            return fail("CreateRootSignature", hr);
        D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 6, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
        if (FAILED(hr = dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&gG.srvHeap)))) return fail("CreateDescriptorHeap", hr);
        gG.srvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    ComPtr<ID3DBlob> vs, ps, err;
    HRESULT hr = D3DCompile(kShader, strlen(kShader), "cinematic.hlsl", nullptr, nullptr, "VS", "vs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs, &err);
    if (FAILED(hr)) {
        if (err) ll_shim::error(std::string("[RF] cinematic VS: ") + static_cast<char const*>(err->GetBufferPointer()));
        return fail("D3DCompile(VS)", hr);
    }
    hr = D3DCompile(kShader, strlen(kShader), "cinematic.hlsl", nullptr, nullptr, "PS", "ps_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, &err);
    if (FAILED(hr)) {
        if (err) ll_shim::error(std::string("[RF] cinematic PS: ") + static_cast<char const*>(err->GetBufferPointer()));
        return fail("D3DCompile(PS)", hr);
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = gG.root.Get();
    d.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    d.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    auto& rt = d.BlendState.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D12_BLEND_ONE;      rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;      rt.BlendOp = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D12_BLEND_ONE; rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA; rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.SampleMask = UINT_MAX;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable = FALSE;
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 1;
    d.RTVFormats[0] = fmt;
    d.SampleDesc.Count = 1;
    gG.pso.Reset();
    if (FAILED(hr = dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&gG.pso)))) return fail("CreateGraphicsPipelineState", hr);
    gG.fmt = fmt;
    gPsoDesc = d; // kept (with the shaders) for the copy pipeline
    gVs = vs; gPs = ps;
    gG.psoCopy.Reset();
    return true;
}
/// the pipeline that draws the checked copy into keep (format of the hidden screen, no blending)
bool ensureCopyPso(ID3D12Device* dev, DXGI_FORMAT fmt) {
    if (gG.psoCopy && gG.copyFmt == fmt) return true;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = gPsoDesc;
    d.BlendState.RenderTarget[0].BlendEnable = FALSE;
    d.RTVFormats[0] = fmt;
    gG.psoCopy.Reset();
    HRESULT hr = dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&gG.psoCopy));
    if (FAILED(hr)) return fail("CreateGraphicsPipelineState(copy)", hr);
    gG.copyFmt = fmt;
    return true;
}

void barrier(ID3D12GraphicsCommandList* l, ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
    D3D12_RESOURCE_BARRIER x{};
    x.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    x.Transition.pResource = r;
    x.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    x.Transition.StateBefore = a;
    x.Transition.StateAfter = b;
    l->ResourceBarrier(1, &x);
}

std::mutex       gKeepM;          // gG.keep / snap (re)creation vs the game thread's copy
std::atomic_bool gKeepFresh{false}; // the game thread copied a whole frame's UI into keep
std::atomic<long> gKeepCopies{0};
long gKeepChecked = 0;
bool ensureKeep(ID3D12Device* dev, D3D12_RESOURCE_DESC const& ud) {
    std::lock_guard kl(gKeepM);
    if (gG.keep && gG.kw == UINT(ud.Width) && gG.kh == ud.Height && gG.kfmt == ud.Format) return true;
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC   d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = ud.Width; d.Height = ud.Height; d.DepthOrArraySize = 1; d.MipLevels = 1; d.Format = ud.Format; d.SampleDesc.Count = 1;
    ComPtr<ID3D12Resource> k;
    D3D12_RESOURCE_DESC    kd = d;
    kd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    HRESULT hr = dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &kd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&k));
    if (FAILED(hr)) return fail("CreateCommittedResource(keep)", hr);
    static std::vector<ComPtr<ID3D12Resource>> graveyard; // may still be in flight
    if (gG.keep) graveyard.push_back(gG.keep);
    gG.keep = k;
    gG.kw = UINT(ud.Width); gG.kh = ud.Height; gG.kfmt = ud.Format;
    gG.keepValid = false;
    D3D12_SHADER_RESOURCE_VIEW_DESC s{};
    s.Format = ud.Format;
    s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    s.Texture2D.MipLevels = 1;
    dev->CreateShaderResourceView(gG.keep.Get(), &s, gG.srvHeap->GetCPUDescriptorHandleForHeapStart());
    {
        if (!gG.rtvHeap) {
            D3D12_DESCRIPTOR_HEAP_DESC rd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
            if (FAILED(dev->CreateDescriptorHeap(&rd, IID_PPV_ARGS(&gG.rtvHeap)))) return fail("CreateDescriptorHeap(rtv)", E_FAIL);
        }
        dev->CreateRenderTargetView(gG.keep.Get(), nullptr, gG.rtvHeap->GetCPUDescriptorHandleForHeapStart());
        ComPtr<ID3D12Resource> cd;
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&cd))))
            return fail("CreateCommittedResource(candidate)", E_FAIL);
        if (gG.cand) graveyard.push_back(gG.cand);
        gG.cand = cd;
        auto h4 = gG.srvHeap->GetCPUDescriptorHandleForHeapStart();
        h4.ptr += SIZE_T(4) * gG.srvInc;
        dev->CreateShaderResourceView(gG.cand.Get(), &s, h4);
    }
    {
        ComPtr<ID3D12Resource> sn;
        HRESULT hs = dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&sn));
        if (FAILED(hs)) return fail("CreateCommittedResource(snapshot)", hs);
        if (gG.snap) graveyard.push_back(gG.snap);
        gG.snap = sn;
        gG.snapValid = false;
        auto h3 = gG.srvHeap->GetCPUDescriptorHandleForHeapStart();
        h3.ptr += SIZE_T(3) * gG.srvInc;
        dev->CreateShaderResourceView(gG.snap.Get(), &s, h3);
        ComPtr<ID3D12Resource> tp;
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&tp))))
            return fail("CreateCommittedResource(tooltips)", E_FAIL);
        if (gG.tip) graveyard.push_back(gG.tip);
        gG.tip = tp;
        auto h5 = gG.srvHeap->GetCPUDescriptorHandleForHeapStart();
        h5.ptr += SIZE_T(5) * gG.srvInc;
        dev->CreateShaderResourceView(gG.tip.Get(), &s, h5);
    }
    // full readback buffer for slot detection
    UINT64 total = 0;
    dev->GetCopyableFootprints(&d, 0, 1, 0, &gG.fp, nullptr, nullptr, &total);
    D3D12_HEAP_PROPERTIES rh{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC   bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = total; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (gG.rb) graveyard.push_back(gG.rb);
    gG.rb.Reset();
    if (FAILED(hr = dev->CreateCommittedResource(&rh, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&gG.rb))))
        return fail("CreateCommittedResource(readback)", hr);
    gG.rbSize  = total;
    gG.rbFrame = -1;
    return true;
}


/// Steve-like stand-in when the skin has no image (persona skins): skin-coloured arms with short teal sleeves.
std::vector<unsigned char> fallbackSkin() {
    std::vector<unsigned char> px(64 * 64 * 4, 0);
    auto fill = [&](int u, int v, bool sleeve) {
        for (int y = v; y < v + 16; ++y)
            for (int x = u; x < u + 16; ++x) {
                bool    teal = sleeve ? false : (y >= v + 4 && y < v + 8);
                uint8_t r = teal ? 0x00 : 0xB4, g = teal ? 0xAF : 0x84, b = teal ? 0xAF : 0x6D;
                if (sleeve) continue;
                unsigned char* q = &px[(size_t(y) * 64 + x) * 4];
                q[0] = r; q[1] = g; q[2] = b; q[3] = 255;
            }
    };
    fill(40, 16, false);
    fill(32, 48, false);
    return px;
}
/// RGBA8 pixels -> a texture with an SRV in our heap at `slot` (upload heap + copy on this frame's list).
bool uploadTexture(ID3D12Device* dev, ID3D12GraphicsCommandList* list, unsigned char const* px, int w, int h, ComPtr<ID3D12Resource>& tex,
                   ComPtr<ID3D12Resource>& up, UINT slot) {
    static std::vector<ComPtr<ID3D12Resource>> graveyard;
    if (tex) graveyard.push_back(tex);
    if (up) graveyard.push_back(up);
    tex.Reset();
    up.Reset();
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC   d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = UINT(w); d.Height = UINT(h); d.DepthOrArraySize = 1; d.MipLevels = 1; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex))))
        return fail("CreateCommittedResource(texture)", E_FAIL);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT64 total = 0;
    dev->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &total);
    D3D12_HEAP_PROPERTIES uh{D3D12_HEAP_TYPE_UPLOAD};
    D3D12_RESOURCE_DESC   bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = total; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(dev->CreateCommittedResource(&uh, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&up))))
        return fail("CreateCommittedResource(texture upload)", E_FAIL);
    void* m = nullptr;
    if (FAILED(up->Map(0, nullptr, &m)) || !m) return fail("Map(texture upload)", E_FAIL);
    for (int y = 0; y < h; ++y) std::memcpy(static_cast<uint8_t*>(m) + fp.Offset + size_t(y) * fp.Footprint.RowPitch, px + size_t(y) * w * 4, size_t(w) * 4);
    up->Unmap(0, nullptr);
    D3D12_TEXTURE_COPY_LOCATION dst{tex.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
    dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION srcl{up.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
    srcl.PlacedFootprint = fp;
    list->CopyTextureRegion(&dst, 0, 0, 0, &srcl, nullptr);
    barrier(list, tex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    auto hc = gG.srvHeap->GetCPUDescriptorHandleForHeapStart();
    hc.ptr += SIZE_T(slot) * gG.srvInc;
    dev->CreateShaderResourceView(tex.Get(), &sv, hc);
    return true;
}
// ---- the prop atlas (t2, 192 x 128): the hammer (HammerModel.h) on the left, block textures of the game itself
// beside it, Cinematic Interactions' own light textures (FxTextures.h: the enchanting beam, rune circle and glow) on the
// right; the block textures are read from the game's own resource pack (data/resource_packs/vanilla/__brarchive/textures/blocks.brarchive:
// a table of 256-byte entries {name length, name, offset, size} after a 16-byte header, then the files)
constexpr int PROP_W = 512, PROP_H = 256;
constexpr float FX_BEAM[4] = {96, 0, 32, 128}, FX_GLOW[4] = {128, 0, 64, 64}, FX_MAGIC[4] = {256, 0, 256, 256}; // x y w h
struct PropSlot { char const* file; int x, y, w, h; };
const PropSlot kPropSlots[] = {
    {"grindstone_round.tga", 64, 0, 16, 16},     // GrindstoneStation.drawRim
    {"stonecutter2_saw.tga", 80, 0, 16, 48},     // the stonecutter's blade (animated strip)
};
std::string gPropNote = "-";
// the enchanting table's glyphs (vanilla EnchantmentTableParticle's sga_a..z): the game's own particles.png
// (textures/particle.brarchive), its two bottom rows of 8 px glyphs, copied to (0, 64) of the atlas
constexpr int FX_GLYPHS_X = 0, FX_GLYPHS_Y = 64;
bool decodePng(std::vector<uint8_t> const& f, std::vector<uint8_t>& rgba, int& w, int& h) {
    HRESULT ci = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool    ok = false;
    {
        ComPtr<IWICImagingFactory>    fac;
        ComPtr<IWICStream>            st;
        ComPtr<IWICBitmapDecoder>     dec;
        ComPtr<IWICBitmapFrameDecode> fr;
        ComPtr<IWICFormatConverter>   cv;
        UINT uw = 0, uh = 0;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac))) &&
            SUCCEEDED(fac->CreateStream(&st)) && SUCCEEDED(st->InitializeFromMemory(const_cast<BYTE*>(f.data()), DWORD(f.size()))) &&
            SUCCEEDED(fac->CreateDecoderFromStream(st.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &dec)) && SUCCEEDED(dec->GetFrame(0, &fr)) &&
            SUCCEEDED(fac->CreateFormatConverter(&cv)) &&
            SUCCEEDED(cv->Initialize(fr.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) &&
            SUCCEEDED(cv->GetSize(&uw, &uh)) && uw > 0 && uh > 0) {
            rgba.resize(size_t(uw) * uh * 4);
            ok = SUCCEEDED(cv->CopyPixels(nullptr, uw * 4, UINT(rgba.size()), rgba.data()));
            w = int(uw); h = int(uh);
        }
    }
    if (SUCCEEDED(ci)) CoUninitialize();
    return ok;
}
bool readBrarchive(std::filesystem::path const& file, char const* name, std::vector<uint8_t>& out) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (d.size() < 16) return false;
    uint32_t n = 0;
    std::memcpy(&n, d.data() + 8, 4);
    size_t base = 16 + size_t(n) * 256;
    if (base > d.size()) return false;
    for (uint32_t i = 0; i < n; ++i) {
        uint8_t const* e = d.data() + 16 + size_t(i) * 256;
        std::string    nm(reinterpret_cast<char const*>(e + 1), std::min<size_t>(e[0], 247));
        if (nm != name) continue;
        uint32_t off = 0, sz = 0;
        std::memcpy(&off, e + 248, 4);
        std::memcpy(&sz, e + 252, 4);
        if (base + off + sz > d.size()) return false;
        out.assign(d.begin() + long(base + off), d.begin() + long(base + off + sz));
        return true;
    }
    return false;
}
/// uncompressed 24/32-bit TGA -> RGBA8, top row first
bool decodeTga(std::vector<uint8_t> const& f, std::vector<uint8_t>& rgba, int& w, int& h) {
    if (f.size() < 18 || f[2] != 2) return false;
    w = f[12] | (f[13] << 8);
    h = f[14] | (f[15] << 8);
    int bpp = f[16] / 8;
    if (bpp < 3 || w <= 0 || h <= 0) return false;
    size_t at = 18 + f[0];
    if (at + size_t(w) * h * bpp > f.size()) return false;
    bool top = (f[17] & 0x20) != 0;
    rgba.assign(size_t(w) * h * 4, 255);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t const* p = f.data() + at + (size_t(y) * w + x) * bpp;
            int            row = top ? y : h - 1 - y;
            uint8_t*       q = &rgba[(size_t(row) * w + x) * 4];
            q[0] = p[2]; q[1] = p[1]; q[2] = p[0]; q[3] = bpp == 4 ? p[3] : 255;
        }
    return true;
}
bool gPropHas[8] = {};
bool uploadProp(ID3D12Device* dev, ID3D12GraphicsCommandList* list) {
    if (gG.propTex) return true;
    std::vector<uint8_t> atlas(size_t(PROP_W) * PROP_H * 4, 0);
    for (int y = 0; y < 64; ++y) std::memcpy(&atlas[size_t(y) * PROP_W * 4], hammer::kAtlas + size_t(y) * 64 * 4, 64 * 4);
    auto blit = [&](uint8_t const* px, int w, int h, float const at[4]) {
        for (int y = 0; y < h; ++y) std::memcpy(&atlas[(size_t(int(at[1]) + y) * PROP_W + int(at[0])) * 4], px + size_t(y) * w * 4, size_t(w) * 4);
    };
    blit(fxtex::kBeam, fxtex::kBeamW, fxtex::kBeamH, FX_BEAM);
    blit(fxtex::kMagic, fxtex::kMagicW, fxtex::kMagicH, FX_MAGIC);
    blit(fxtex::kGlow, fxtex::kGlowW, fxtex::kGlowH, FX_GLOW);
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    auto arch = std::filesystem::path(exe).parent_path() / "data" / "resource_packs" / "vanilla" / "__brarchive" / "textures" / "blocks.brarchive";
    gPropNote.clear();
    {
        std::vector<uint8_t> file, px;
        int                  w = 0, h = 0;
        if (readBrarchive(arch.parent_path() / "particle.brarchive", "particles.png", file) && decodePng(file, px, w, h) && w >= 128 && h >= 128) {
            for (int y = 0; y < 16; ++y)
                std::memcpy(&atlas[(size_t(FX_GLYPHS_Y + y) * PROP_W + FX_GLYPHS_X) * 4], &px[(size_t(h - 16 + y) * w) * 4], 128 * 4);
            for (int y = 0; y < 8; ++y) // generic_0..7 (the portal particles' sprites): its top row
                std::memcpy(&atlas[(size_t(FX_GLYPHS_Y + 16 + y) * PROP_W + FX_GLYPHS_X) * 4], &px[(size_t(y) * w) * 4], 64 * 4);
            gPropNote += "particles.png ok; ";
            gPropHas[7] = true;
        } else gPropNote += "particles.png missing; ";
    }
    for (size_t i = 0; i < std::size(kPropSlots); ++i) {
        auto const&          ps = kPropSlots[i];
        std::vector<uint8_t> file, px;
        int                  w = 0, h = 0;
        if (!readBrarchive(arch, ps.file, file) || !decodeTga(file, px, w, h)) { gPropNote += std::string(ps.file) + " missing; "; continue; }
        for (int y = 0; y < std::min(h, ps.h); ++y)
            for (int x = 0; x < std::min(w, ps.w); ++x)
                std::memcpy(&atlas[(size_t(ps.y + y) * PROP_W + ps.x + x) * 4], &px[(size_t(y) * w + x) * 4], 4);
        gPropHas[i] = true;
        gPropNote += std::string(ps.file) + " ok; ";
    }
    return uploadTexture(dev, list, atlas.data(), PROP_W, PROP_H, gG.propTex, gG.propUp, 2);
}
bool uploadSkin(ID3D12Device* dev, ID3D12GraphicsCommandList* list) {
    std::vector<unsigned char> px;
    int  w, h;
    long serial;
    {
        std::lock_guard l(gM);
        serial = gS.skinSerial;
        if (serial == gG.skinSerial && gG.skinTex) return true;
        px = gS.skin;
        w  = gS.skinW;
        h  = gS.skinH;
    }
    if (px.empty() || w < 64) { px = fallbackSkin(); w = h = 64; }
    if (!uploadTexture(dev, list, px.data(), w, h, gG.skinTex, gG.skinUp, 1)) return false;
    gG.skinSerial = serial;
    gG.skinValid  = true;
    gG.skinW = w;
    gG.skinH = h;
    return true;
}

// ---------------------------------------------------------------- slot detection (worker thread)
struct Img {
    int W = 0, H = 0;
    bool bgra = false;
    std::vector<uint8_t> px; // tightly packed RGBA
    void get(int x, int y, int& r, int& g, int& b, int& a) const {
        uint8_t const* p = &px[(size_t(y) * W + x) * 4];
        r = bgra ? p[2] : p[0]; g = p[1]; b = bgra ? p[0] : p[2]; a = p[3];
    }
};
bool isDark(Img const& im, int x, int y) {
    if (x < 0 || y < 0 || x >= im.W || y >= im.H) return false;
    int r, g, b, a;
    im.get(x, y, r, g, b, a);
    int mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
    return a >= 200 && mx <= 85 && mn >= 25 && mx - mn <= 12;
}
bool isLight(Img const& im, int x, int y) {
    if (x < 0 || y < 0 || x >= im.W || y >= im.H) return false;
    int r, g, b, a;
    im.get(x, y, r, g, b, a);
    return a >= 200 && std::min(r, std::min(g, b)) >= 215;
}
bool isBgPx(Img const& im, int x, int y) {
    if (x < 0 || y < 0 || x >= im.W || y >= im.H) return false;
    int r, g, b, a;
    im.get(x, y, r, g, b, a);
    return a >= 200 && std::abs(r - 139) <= 8 && std::abs(g - 139) <= 8 && std::abs(b - 139) <= 8;
}

void writeBmp(std::filesystem::path const& p, Img const& im) {
    std::ofstream o(p, std::ios::binary);
    if (!o) return;
    int      W = im.W, H = im.H;
    uint32_t rowBytes = uint32_t(W) * 3, pad = (4 - rowBytes % 4) % 4, size = 54 + (rowBytes + pad) * H;
    uint8_t  h[54]{'B', 'M'};
    auto put32 = [&](int off, uint32_t v) { std::memcpy(h + off, &v, 4); };
    put32(2, size); put32(10, 54); put32(14, 40); put32(18, uint32_t(W)); put32(22, uint32_t(H));
    h[26] = 1; h[28] = 24;
    o.write(reinterpret_cast<char*>(h), 54);
    std::vector<uint8_t> row(rowBytes + pad);
    for (int y = H - 1; y >= 0; --y) {
        for (int x = 0; x < W; ++x) {
            int r, g, b, a;
            im.get(x, y, r, g, b, a);
            // un-premultiplied over magenta, so transparent areas are visible
            row[x * 3 + 0] = uint8_t(std::min(255, b + (255 - a)));
            row[x * 3 + 1] = uint8_t(std::min(255, g));
            row[x * 3 + 2] = uint8_t(std::min(255, r + (255 - a)));
        }
        o.write(reinterpret_cast<char*>(row.data()), row.size());
    }
}

// the item tooltip probe (record -> detect): which slot, where the hidden cursor rests (px); what was found
std::atomic<int>   gProbeSlot{-1};
std::atomic<float> gProbeX{0}, gProbeY{0}, gProbeUnit{4};
std::atomic<int>   gProbeLooks{0}, gProbeHits{0};
std::mutex         gTipM;
int                gTipFoundSlot = -1;
Rect               gTipFoundRect;
/// Bedrock's hover text near a point the cursor rests on: a dark violet box (half transparent) with a violet border,
/// beside and above the cursor; `text` = its writing (bright pixels inside the border)
bool findTooltip(Img const& im, int cx, int cy, float unit, Rect& text) {
    // the box: a one-pixel (gui) violet border (44,8,99) inside a ring of its dark ground (27,12,27); its writing within
    auto border = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= im.W || y >= im.H) return false;
        int r, g, b, a;
        im.get(x, y, r, g, b, a);
        return std::abs(r - 44) < 9 && g < 18 && std::abs(b - 99) < 12;
    };
    auto bright = [&](int x, int y) { int r, g, b, a; im.get(x, y, r, g, b, a); return std::max(r, std::max(g, b)) > 80 && !border(x, y); };
    int u = std::max(1, int(unit + 0.5f));
    // its corners are cut (pixel-art rounding): the top border is a run of its own, the box one unit wider each side
    for (int y = cy - int(24 * unit); y <= cy + int(8 * unit); ++y)
        for (int x = cx; x < cx + int(40 * unit); ++x) {
            if (!border(x, y) || border(x - 1, y) || border(x, y - 1)) continue;
            int x1 = x;
            while (border(x1 + 1, y)) ++x1;
            if (x1 - x < 16 * unit) { x = x1; continue; }
            int xm = (x + x1) / 2, yy = y;
            while (border(xm, yy + 1)) ++yy; // its thickness
            int top = yy + 1, yb = top;
            while (yb < im.H && yb - top < 200 * unit && !border(xm, yb)) ++yb;
            auto under = [&](int bx) { for (int d = -u; d <= u; ++d) if (border(bx, yb + d)) return true; return false; };
            int q = (x1 - x) / 4;
            if (yb >= im.H || yb - top < 6 * unit || !under(x + q) || !under(x1 - q)) continue; // the bottom border under the width
            int tx0 = 1 << 30, ty0 = 1 << 30, tx1 = -1, ty1 = -1;
            for (int ty = top; ty < yb; ++ty)
                for (int tx = x; tx <= x1; ++tx)
                    if (bright(tx, ty)) { tx0 = std::min(tx0, tx); ty0 = std::min(ty0, ty); tx1 = std::max(tx1, tx); ty1 = std::max(ty1, ty); }
            if (tx1 <= tx0 || ty1 <= ty0) return false;
            tx1 = std::min(tx1 + u, x1); ty1 = std::min(ty1 + u, yb - 1); // the writing's shadow
            text = {float(tx0), float(ty0), float(tx1 + 1 - tx0), float(ty1 + 1 - ty0)};
            return true;
        }
    return false;
}
void detect(Img im, bool dump, int kindI) {
    Kind kind = Kind(kindI);
    struct Sq { int x, y, s; };
    std::vector<Sq> sq;
    int W = im.W, H = im.H;
    for (int y = 1; y < H - 8; ++y) {
        int x = 0;
        while (x < W) {
            if (!isDark(im, x, y)) { ++x; continue; }
            int x0 = x;
            while (x < W && isDark(im, x, y)) ++x;
            int L = x - x0;
            if (L < 12 || L > 400) continue;
            if (isDark(im, x0, y - 1) || isDark(im, x0 - 1, y)) continue; // not the top-left corner of a frame
            int V = 0;
            while (y + V < H && isDark(im, x0, y + V)) ++V;
            if (std::abs(V - L) > std::max(3, L / 8)) continue;
            int S = std::max(L, V);
            // confirm: light bevel along the bottom or right, or the slot background inside
            int lightBottom = 0, lightRight = 0, n = 0;
            for (int k = S / 8; k < S; k += std::max(1, S / 16)) {
                ++n;
                for (int dy = -2; dy <= 3; ++dy) if (isLight(im, x0 + k, y + S - 1 + dy)) { ++lightBottom; break; }
                for (int dx = -2; dx <= 3; ++dx) if (isLight(im, x0 + S - 1 + dx, y + k)) { ++lightRight; break; }
            }
            bool bgIn = isBgPx(im, x0 + S / 8 + 1, y + S / 8 + 1) || isBgPx(im, x0 + S - S / 8 - 2, y + S / 8 + 1) ||
                        isBgPx(im, x0 + S / 8 + 1, y + S - S / 8 - 2);
            if (!(lightBottom * 2 >= n || lightRight * 2 >= n || bgIn)) continue;
            bool dup = false;
            for (auto& q : sq) if (std::abs(q.x - x0) <= 3 && std::abs(q.y - y) <= 3) dup = true;
            if (!dup) sq.push_back({x0, y, S});
        }
    }
    { // the tooltip probe first: its box may cover slots, so the layout of this frame may well fail
        int ps = gProbeSlot.load();
        Rect tr;
        if (ps >= 0) {
            ++gProbeLooks;
            if (findTooltip(im, int(gProbeX.load()), int(gProbeY.load()), gProbeUnit.load(), tr)) {
                ++gProbeHits;
                std::lock_guard tl(gTipM);
                gTipFoundSlot = ps;
                gTipFoundRect = tr;
            }
        }
    }
    Layout L;
    L.W = W; L.H = H;
    L.found = int(sq.size());
    char note[512];
    // normal slot size = most common
    std::vector<int> sizes;
    for (auto& q : sq) sizes.push_back(q.s);
    std::sort(sizes.begin(), sizes.end());
    int best = 0, bestN = 0;
    for (size_t i = 0; i < sizes.size();) {
        size_t j = i;
        while (j < sizes.size() && sizes[j] - sizes[i] <= std::max(2, sizes[i] / 20)) ++j;
        if (int(j - i) > bestN) { bestN = int(j - i); best = sizes[(i + j) / 2]; }
        i = j;
    }
    auto normal = [&](Sq const& q) { return std::abs(q.s - best) <= std::max(2, best / 12); };
    // rows of 9 evenly spaced normal slots
    struct Row { int y; std::vector<Sq> s; };
    std::vector<Row> rows;
    for (auto& q : sq) {
        if (!normal(q)) continue;
        Row* r = nullptr;
        for (auto& rr : rows) if (std::abs(rr.y - q.y) <= 3) r = &rr;
        if (!r) { rows.push_back({q.y, {}}); r = &rows.back(); }
        r->s.push_back(q);
    }
    std::vector<Row> nine;
    for (auto& r : rows) {
        std::sort(r.s.begin(), r.s.end(), [](Sq const& a, Sq const& b) { return a.x < b.x; });
        // one slot of the row may be drawn over (Bedrock's green focus square when the keyboard was used last): a gap of
        // two pitches in an otherwise even row is that slot
        if (r.s.size() == 8)
            for (size_t k = 1; k < r.s.size(); ++k)
                if (std::abs(r.s[k].x - r.s[k - 1].x - 2 * best) <= std::max(4, best / 6)) {
                    Sq m{r.s[k - 1].x + (r.s[k].x - r.s[k - 1].x) / 2, r.s[k - 1].y, r.s[k - 1].s};
                    r.s.insert(r.s.begin() + long(k), m);
                    break;
                }
        // longest run of evenly spaced slots
        for (size_t i = 0; i + 9 <= r.s.size(); ++i) {
            bool ok = true;
            for (size_t k = i + 1; k < i + 9 && ok; ++k) {
                int gap = r.s[k].x - r.s[k - 1].x;
                ok = std::abs(gap - best) <= std::max(4, best / 6);
            }
            if (ok) { nine.push_back({r.y, std::vector<Sq>(r.s.begin() + i, r.s.begin() + i + 9)}); break; }
        }
    }
    std::sort(nine.begin(), nine.end(), [](Row const& a, Row const& b) { return a.y < b.y; });
    if (nine.size() < 4 || best <= 0) {
        snprintf(note, sizeof note, "squares %zu, slot %d px, rows of 9: %zu (need 4) - no layout", sq.size(), best, nine.size());
        L.note = note;
    } else {
        auto& hb = nine[nine.size() - 1];
        // the dark top/left frame line is one GUI pixel (best/17) short of the whole 18 px slot
        float unit = float(best) / 17.0f;
        float s    = float(best) + unit;
        for (int i = 0; i < 9; ++i) L.hotbar[i] = {float(hb.s[i].x), float(hb.s[i].y), s, s};
        for (int r = 0; r < 3; ++r) {
            auto& row = nine[nine.size() - 4 + r];
            for (int i = 0; i < 9; ++i) L.inv[r * 9 + i] = {float(row.s[i].x), float(row.s[i].y), s, s};
        }
        L.slot  = s;
        float px0 = L.inv[0].x, px1 = L.inv[8].x + s, invTop = L.inv[0].y;
        L.panel = {px0 - 0.5f * s, 0, px1 - px0 + s, invTop};
        {
            Rect const* ps[2] = {&L.hotbar[0], &L.inv[0]};
            for (int k = 0; k < 2; ++k) {
                L.probe[k][0] = ps[k]->x + ps[k]->w * 0.5f; L.probe[k][1] = ps[k]->y + 1;
                L.probe[k][2] = ps[k]->x + ps[k]->w * 0.5f; L.probe[k][3] = std::max(0.0f, ps[k]->y - 3);
            }
            int rr, gg, bb, aa;
            im.get(int(L.probe[0][0]), int(L.probe[0][1]), rr, gg, bb, aa);
            L.probeLine[0] = rr / 255.0f; L.probeLine[1] = gg / 255.0f; L.probeLine[2] = bb / 255.0f;
            im.get(int(L.probe[0][2]), int(L.probe[0][3]), rr, gg, bb, aa);
            L.probeAbove[0] = rr / 255.0f; L.probeAbove[1] = gg / 255.0f; L.probeAbove[2] = bb / 255.0f;
            L.hasProbe = true;
        }
        // station slots: above the inventory, within the panel's width
        std::vector<Sq> st;
        for (auto& q : sq) {
            if (q.y + q.s > invTop - 2) continue;
            if (q.x < px0 - s || q.x + q.s > px1 + s) continue;
            st.push_back(q);
        }
        L.kind = kindI;
        if (kind != Kind::Table) {
            // the menu's own slots: the right-most square is the result; grids of option buttons (stonecutter, loom)
            // are left out (a square with neighbours at slot spacing on both sides, or in a row of 4 or more)
            auto nearSq = [&](Sq const& a, int dx, int dy) {
                for (auto& q : st)
                    if (std::abs(q.x - (a.x + dx)) <= best / 6 && std::abs(q.y - (a.y + dy)) <= best / 6) return true;
                return false;
            };
            std::vector<Sq> own, buttons;
            for (auto& q : st) {
                int  pitch  = q.s + q.s / 17;
                bool inGrid = false && ((nearSq(q, -pitch, 0) && nearSq(q, pitch, 0)) || (nearSq(q, pitch, 0) && nearSq(q, 2 * pitch, 0) && nearSq(q, 3 * pitch, 0)) ||
                              (nearSq(q, -pitch, 0) && nearSq(q, -2 * pitch, 0) && nearSq(q, -3 * pitch, 0)));
                if (!inGrid && (q.s < best * 0.85f || q.x + q.s > px1 + s * 0.25f)) continue; // fragments, the window's close button
                (inGrid ? buttons : own).push_back(q);
            }
            {
                std::vector<Sq> bs = buttons;
                std::sort(bs.begin(), bs.end(), [&](Sq const& a, Sq const& b) { return std::abs(a.y - b.y) > best / 3 ? a.y < b.y : a.x < b.x; });
                L.nButtons = 0;
                for (auto& q : bs)
                    if (L.nButtons < kButtons) L.buttons[L.nButtons++] = {float(q.x), float(q.y), float(q.s) + unit, float(q.s) + unit};
            }
            if ((kind == Kind::Stonecutter || kind == Kind::Loom) && L.nButtons == 0) {
                // Bedrock lists the recipes / patterns in a dark side panel left of the window (rows of buttons at slot
                // pitch on a #5D5D5D ground): find the panel, the first button's corner, then the grid while cells hold one
                auto px3 = [&](int x, int y, int& r, int& g, int& b) { int a; im.get(std::clamp(x, 0, im.W - 1), std::clamp(y, 0, im.H - 1), r, g, b, a); };
                auto ground = [&](int x, int y) { int r, g, b; px3(x, y, r, g, b); return std::abs(r - 93) <= 4 && std::abs(g - 93) <= 4 && std::abs(b - 93) <= 4; };
                int  gx0 = 1 << 30, gy0 = 1 << 30, gx1 = -1, gy1 = -1, xr = int(px0 - 1.5f * s);
                for (int y = 0; y < im.H; y += 4)
                    for (int x = 0; x < xr; x += 4)
                        if (ground(x, y)) { gx0 = std::min(gx0, x); gy0 = std::min(gy0, y); gx1 = std::max(gx1, x); gy1 = std::max(gy1, y); }
                if (gx1 - gx0 > 2 * s && gy1 - gy0 > 2 * s) {
                    int y = gy0 + int(s / 2), bx = -1, by = -1;
                    for (int x = gx0; x < gx1 && bx < 0; ++x) if (!ground(x, y) && !ground(x + 1, y)) bx = x;
                    if (bx >= 0)
                        for (int yy = gy0; yy < gy1 && by < 0; ++yy) if (!ground(bx + int(s / 2), yy) && !ground(bx + int(s / 2), yy + 1)) by = yy;
                    if (bx >= 0 && by >= 0) {
                        int pitch = int(s + 0.5f), cols = std::max(1, (gx1 - bx + pitch / 2) / pitch);
                        for (int r = 0; L.nButtons < kButtons; ++r) {
                            int got = 0;
                            for (int c = 0; c < cols && L.nButtons < kButtons; ++c) {
                                int cx = bx + c * pitch + pitch / 2, cy = by + r * pitch + pitch / 2;
                                if (cy >= gy1 || ground(cx, cy)) continue;
                                L.buttons[L.nButtons++] = {float(bx + c * pitch), float(by + r * pitch), float(pitch), float(pitch)};
                                ++got;
                            }
                            if (!got) break;
                        }
                    }
                }
                if (L.nButtons) { L.board = {L.buttons[0].x, L.buttons[0].y, L.buttons[L.nButtons - 1].x + L.buttons[0].w - L.buttons[0].x, L.buttons[L.nButtons - 1].y + L.buttons[0].h - L.buttons[0].y}; L.hasBoard = true; }
            }
            if (!buttons.empty()) {
                int bx0 = 1 << 30, by0 = 1 << 30, bx1 = -1, by1 = -1;
                for (auto& q : buttons) { bx0 = std::min(bx0, q.x); by0 = std::min(by0, q.y); bx1 = std::max(bx1, q.x + q.s); by1 = std::max(by1, q.y + q.s); }
                L.board = {float(bx0), float(by0), float(bx1 - bx0), float(by1 - by0)};
                L.hasBoard = true;
            }
            int expect = isFurnace(kind) ? 3 : kind == Kind::Smithing || kind == Kind::Loom ? 4 : kind == Kind::Stonecutter || kind == Kind::Enchanting ? 2 : 3;
            bool hasRes = kind != Kind::Enchanting;
            std::vector<Sq> rest = own;
            Sq res{};
            if (hasRes && !rest.empty()) {
                size_t ri = 0;
                for (size_t i = 1; i < rest.size(); ++i)
                    if (rest[i].x > rest[ri].x + 2 || (std::abs(rest[i].x - rest[ri].x) <= 2 && rest[i].s > rest[ri].s)) ri = i;
                res = rest[ri];
                rest.erase(rest.begin() + long(ri));
            }
            auto byY = [](Sq const& a, Sq const& b) { return a.y != b.y ? a.y < b.y : a.x < b.x; };
            auto byX = [](Sq const& a, Sq const& b) { return a.x != b.x ? a.x < b.x : a.y < b.y; };
            // furnace: input above fuel; grindstone: input above the additional slot; cartography: map above paper
            if (isFurnace(kind) || kind == Kind::Grindstone || kind == Kind::Cartography) std::sort(rest.begin(), rest.end(), byY);
            else if (kind == Kind::Loom) {
                // banner and dye in the first row (left, right), the pattern below
                std::sort(rest.begin(), rest.end(), byY);
                if (rest.size() >= 2 && std::abs(rest[0].y - rest[1].y) <= best / 4 && rest[1].x < rest[0].x) std::swap(rest[0], rest[1]);
            } else std::sort(rest.begin(), rest.end(), byX); // anvil, smithing (template, base, addition), stonecutter, enchanting
            int n = 0;
            for (auto& q : rest) {
                if (n >= (hasRes ? expect - 1 : expect)) break;
                L.grid[n++] = {float(q.x), float(q.y), float(q.s) + unit, float(q.s) + unit};
            }
            if (hasRes && res.s > 0) L.grid[n++] = {float(res.x), float(res.y), float(res.s) + unit, float(res.s) + unit};
            if (kind == Kind::Loom && res.s > 0) {
                // the banner preview: a black-framed tall box right of the result
                auto black = [&](int x, int y) {
                    int r, g, b, a;
                    im.get(std::clamp(x, 0, im.W - 1), std::clamp(y, 0, im.H - 1), r, g, b, a);
                    return r < 16 && g < 16 && b < 16 && a > 200;
                };
                int cy = res.y + res.s / 2, x0 = -1, x1 = -1;
                for (int x = res.x + res.s + 2; x < std::min(im.W, int(px1 + 2 * s)); ++x)
                    if (black(x, cy)) { if (x0 < 0) x0 = x; else if (x - x0 > s / 2) { x1 = x; break; } }
                if (x0 >= 0 && x1 > x0) {
                    int mx = (x0 + x1) / 2, y0 = cy, y1 = cy;
                    while (y0 > 0 && !black(mx, y0)) --y0;
                    while (y1 < im.H - 1 && !black(mx, y1)) ++y1;
                    float in = s / 18.0f; // one GUI pixel inside the frame
                    L.preview    = {float(x0) + in, float(y0) + in, float(x1 - x0) - 2 * in, float(y1 - y0) - 2 * in};
                    L.hasPreview = y1 - y0 > s && L.preview.w > 4;
                }
            }
            L.nRoles  = n;
            L.hasGrid = n == expect;
            if (kind == Kind::Enchanting && n == 2) {
                // the three enchantment buttons fill a panel right of the lapis slot: its frame (#373737, one GUI px)
                // is found on a line through it above the slots, its height down the frame's left column, its width
                // along the frame's top line; the buttons are its thirds (a light bevel on top, a dark line below)
                auto px3   = [&](int x, int y, int& r, int& g, int& b) { int a; im.get(std::clamp(x, 0, im.W - 1), std::clamp(y, 0, im.H - 1), r, g, b, a); };
                auto frame = [&](int x, int y) { int r, g, b; px3(x, y, r, g, b); return std::abs(r - 55) <= 3 && std::abs(g - 55) <= 3 && std::abs(b - 55) <= 3; };
                Rect const& lap = L.grid[1].x > L.grid[0].x ? L.grid[1] : L.grid[0];
                int  y = int(lap.y - 1.5f * s), x = int(lap.x + lap.w + unit), xEnd = int(px1 + s);
                while (x < xEnd && !frame(x, y)) ++x;
                int fx = x;
                while (x < xEnd && frame(x, y)) ++x;
                int x0 = x, th = x0 - fx;
                if (fx < xEnd && th > 0 && th < s / 4) {
                    int yt = y, yb = y;
                    while (yt > 0 && frame(fx, yt - 1)) --yt;
                    while (yb < im.H - 1 && frame(fx, yb + 1)) ++yb;
                    int x1 = fx;
                    while (x1 < im.W - 1 && frame(x1 + 1, yt)) ++x1;
                    float top = float(yt + th), bottom = float(yb + 1), right = float(x1 + 1), h = (bottom - top) / 3;
                    if (h > s * 0.8f && right - x0 > 4 * s) {
                        float in = L.slot * 16.0f / 18.0f;
                        for (int k = 0; k < 3; ++k) {
                            float ty = top + k * h, mid = ty + h / 2;
                            L.buttons[k]     = {float(x0), ty, right - x0, h};
                            L.buttons[3 + k] = {right - unit - in, mid - in / 2, in, in}; // the level cost (right, green while it can be paid)
                            float rx = float(x0) + 16 * unit, step = (right - 2 * unit - rx - in) / 5; // the runes, along the middle
                            for (int j = 0; j < 6; ++j) L.buttons[6 + k * 6 + j] = {rx + j * step, mid - in / 2, in, in};
                        }
                        L.nButtons = 24;
                        L.board    = {float(x0), top, right - x0, bottom - top};
                        L.hasBoard = true;
                        // the clue the window shows beside the button the cursor rests on: a dark violet box (half
                        // transparent: lighter over the transparent ground) with a violet border, white writing
                        auto tip = [&](int x2, int y2) {
                            int r, g, b;
                            px3(x2, y2, r, g, b);
                            return (g <= 18 && std::abs(r - b) < 14 && r >= 18 && r <= 205) || (b > r + 30 && g < 16);
                        };
                        auto white = [&](int x2, int y2) { int r, g, b; px3(x2, y2, r, g, b); return r > 235 && g > 235 && b > 235; };
                        for (int k = 0; k < 3; ++k) {
                            int cx = int(L.buttons[k].cx()), cy = int(L.buttons[k].cy()), yr = cy - int(9 * unit), bx0 = -1;
                            for (int x2 = cx + int(2 * unit); x2 < cx + int(24 * unit) && bx0 < 0; ++x2) if (tip(x2, yr) && tip(x2 + 1, yr)) bx0 = x2;
                            if (bx0 < 0) continue;
                            int bx1 = bx0, miss = 0;
                            for (int x2 = bx0; x2 < im.W && miss < int(2 * unit); ++x2) { if (tip(x2, yr) || white(x2, yr)) { bx1 = x2; miss = 0; } else ++miss; }
                            int xs = bx0 + int(3 * unit), by0 = yr, by1 = yr;
                            while (by0 > 0 && tip(xs, by0 - 1)) --by0;
                            while (by1 < im.H - 1 && (tip(xs, by1 + 1) || white(xs, by1 + 1))) ++by1;
                            int tx0 = 1 << 30, ty0 = 1 << 30, tx1 = -1, ty1 = -1;
                            for (int y2 = by0; y2 <= by1; ++y2)
                                for (int x2 = bx0; x2 <= bx1; ++x2)
                                    if (white(x2, y2)) { tx0 = std::min(tx0, x2); ty0 = std::min(ty0, y2); tx1 = std::max(tx1, x2); ty1 = std::max(ty1, y2); }
                            if (tx1 > tx0 && ty1 > ty0 && bx1 - bx0 > 8 * unit) {
                                L.clue[k]    = {float(tx0), float(ty0), float(tx1 + 1 - tx0), float(ty1 + 1 - ty0)};
                                L.hasClue[k] = true;
                            }
                        }
                    }
                }
            }
        }
        // crafting grid: 3 x 3 normal slots
        std::vector<Sq> nm;
        if (kind == Kind::Table) for (auto& q : st) if (normal(q)) nm.push_back(q);
        for (auto& a : nm) {
            int got = 0;
            Sq  g[9];
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    for (auto& q : nm)
                        if (std::abs(q.x - (a.x + c * best)) <= std::max(4, best / 6) && std::abs(q.y - (a.y + r * best)) <= std::max(4, best / 6)) { g[r * 3 + c] = q; ++got; break; }
            if (got == 9) {
                for (int i = 0; i < 9; ++i) L.grid[i] = {float(g[i].x), float(g[i].y), s, s};
                L.hasGrid = true;
                break;
            }
        }
        // result: the biggest square right of the grid (else the biggest one)
        int bi = -1;
        for (size_t i = 0; kind == Kind::Table && i < st.size(); ++i) {
            if (L.hasGrid && st[i].x < L.grid[2].x + s) continue;
            if (bi < 0 || st[i].s > st[bi].s) bi = int(i);
        }
        if (bi >= 0) { L.result = {float(st[bi].x), float(st[bi].y), float(st[bi].s) + unit, float(st[bi].s) + unit}; L.hasResult = true; }
        // slot background colour (an empty pack slot's inside), else the default #8B8B8B
        for (auto& r : L.inv) {
            int x = int(r.x + r.w * 0.2f), y = int(r.y + r.h * 0.2f);
            if (isBgPx(im, x, y)) {
                int rr, gg, bb, aa;
                im.get(x, y, rr, gg, bb, aa);
                L.bg[0] = rr / 255.0f; L.bg[1] = gg / 255.0f; L.bg[2] = bb / 255.0f;
                break;
            }
        }
        L.valid = true;
        snprintf(note, sizeof note, "%s: squares %zu, slot %d px, rows of 9: %zu, station squares %zu, slots %s (%d), result %s (%.0f px), board %s",
                 kNames[kindI], sq.size(), best, nine.size(), st.size(), L.hasGrid ? "yes" : "no", kind == Kind::Table ? (L.hasGrid ? 9 : 0) : L.nRoles,
                 L.hasResult ? "yes" : "no", L.hasResult ? L.result.w : 0.0f, L.hasBoard ? "yes" : "no");
        L.note = note;
    }
    {
        std::lock_guard l(gLayoutMutex);
        L.serial = gLayout.serial + 1;
        bool keepOld = !L.valid && gLayout.valid && gLayout.W == L.W && gLayout.kind == kindI;
        if (keepOld) { gLayout.note = L.note + " (kept the previous layout)"; }
        else gLayout = L;
    }
    if (dump) {
        static int n = 0;
        char name[64];
        snprintf(name, sizeof name, "cinematic_ui_%d.bmp", n % 4);
        // write next to the other captures
        std::string text = L.note + "\n";
        for (auto& q : sq) { char b[64]; snprintf(b, sizeof b, "square %d,%d size %d\n", q.x, q.y, q.s); text += b; }
        rf::uicapture::writeText((std::string("cinematic_slots_") + std::to_string(n % 4) + ".txt").c_str(), text.c_str());
        if (!gDumpDir.empty()) writeBmp(gDumpDir / name, im);
        ++n;
    }
    gDetectBusy = false;
}

void requestReadback(ID3D12GraphicsCommandList* list, ID3D12Resource* ui) {
    if (!gG.rb || gG.rbFrame >= 0 || gDetectBusy.load()) return;
    D3D12_TEXTURE_COPY_LOCATION dst{gG.rb.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
    dst.PlacedFootprint = gG.fp;
    D3D12_TEXTURE_COPY_LOCATION src{ui, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
    src.SubresourceIndex = 0;
    barrier(list, ui, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    barrier(list, ui, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    gG.rbFrame = gG.frameNo;
}
} // namespace
void beforeUiClear(ID3D12GraphicsCommandList* list, ID3D12Resource* ui) {
    if (!list || !ui) return;
    {
        std::lock_guard l(gM);
        if (gS.phase != Phase::Intro && gS.phase != Phase::Active) return; // the closing shot keeps the last screen
    }
    std::lock_guard kl(gKeepM);
    if (!gG.cand || gG.broken) return;
    D3D12_RESOURCE_DESC d = ui->GetDesc();
    if (gG.kw != UINT(d.Width) || gG.kh != d.Height || gG.kfmt != d.Format) return;
    barrier(list, gG.cand.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    barrier(list, ui, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyResource(gG.cand.Get(), ui);
    barrier(list, ui, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    barrier(list, gG.cand.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    gKeepFresh = true;
    ++gKeepCopies;
}
namespace {
void pollReadback() {
    if (gG.rbFrame < 0 || gG.frameNo - gG.rbFrame < 4) return; // 3 frames in flight: the copy has run by now
    gG.rbFrame = -1;
    void*       p = nullptr;
    D3D12_RANGE rr{0, SIZE_T(gG.rbSize)};
    if (FAILED(gG.rb->Map(0, &rr, &p)) || !p) return;
    Img im;
    im.W = int(gG.kw);
    im.H = int(gG.kh);
    im.bgra = gG.kfmt == DXGI_FORMAT_B8G8R8A8_UNORM || gG.kfmt == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    im.px.resize(size_t(im.W) * im.H * 4);
    for (int y = 0; y < im.H; ++y)
        std::memcpy(&im.px[size_t(y) * im.W * 4], static_cast<uint8_t*>(p) + gG.fp.Offset + size_t(y) * gG.fp.Footprint.RowPitch, size_t(im.W) * 4);
    D3D12_RANGE none{0, 0};
    gG.rb->Unmap(0, &none);
    gDetectBusy = true;
    bool dump = gCfg.dumpUi;
    int kindNow;
    {
        std::lock_guard l(gM);
        kindNow = int(gS.st.kind);
    }
    std::thread(detect, std::move(im), dump, kindNow).detach();
}

// ---------------------------------------------------------------- per-frame slot contents (small ring readback)
// The station's slots (grid + result) are copied into a small buffer every frame; read 4 frames later: a slot is
// empty when its inside is all slot background (or the hovered slot's background).
constexpr int kSampled = kSampledN; // 9 grid + result + the parked cursor (carried stack) + option buttons
bool ensureRing(ID3D12Device* dev, UINT cell) {
    if (gG.sr[0] && gG.srCell == cell) return true;
    gG.srCell   = cell;
    gG.srPitch  = (cell * 4 + 255) & ~255u;
    gG.srStride = (UINT64(gG.srPitch) * cell + 511) & ~UINT64(511);
    D3D12_HEAP_PROPERTIES rh{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC   bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = gG.srStride * kSampled; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    for (int i = 0; i < Gfx::kRing; ++i) {
        gG.sr[i].Reset();
        gG.srFrame[i] = -1;
        if (FAILED(dev->CreateCommittedResource(&rh, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&gG.sr[i]))))
            return fail("CreateCommittedResource(slot ring)", E_FAIL);
    }
    return true;
}
/// Rect of sampled slot s in the hidden screen (the inside of the slot, where the icon is drawn).
bool sampledRect(Layout const& L, int s, Rect& r) {
    if (s < 9) { if (!L.hasGrid || (L.kind != int(Kind::Table) && s >= L.nRoles)) return false; r = L.grid[s]; }
    else if (s == 9) { if (!L.hasResult) return false; r = L.result; }
    else if (s == 10) { float x, y; neutralPoint(L, x, y); r = {x - L.slot * 0.5f, y - L.slot * 0.5f, L.slot, L.slot}; return true; }
    else if (s == 11 + kButtons) {
        if (!L.hasPreview) return false;
        float in = L.slot * 16.0f / 18.0f;
        r = {L.preview.cx() - in * 0.5f, L.preview.cy() - in * 0.5f, in, in};
        return true;
    }
    else { if (s - 11 >= L.nButtons) return false; r = L.buttons[s - 11]; }
    // the 16 px icon sits in the middle of the slot (a big result slot too)
    float in = L.slot * 16.0f / 18.0f;
    r = {r.cx() - in * 0.5f, r.cy() - in * 0.5f, in, in};
    return true;
}
/// Is this icon an isometric block (a full cube seen from above: a hexagon filling its box, empty corners)?
bool isoBlock(std::vector<uint8_t> const& m, int n, float const box[4]) {
    float bw = box[2] - box[0], bh = box[3] - box[1];
    if (bw < 0.5f || bh < 0.5f) return false;
    float ratio = bh / bw; // 1.113 for a cube at the inventory angle (45 / 30 degrees)
    if (ratio < 0.98f || ratio > 1.25f) return false;
    auto at = [&](float u, float v) {
        int x = std::clamp(int((box[0] + u * bw) * n), 0, n - 1), y = std::clamp(int((box[1] + v * bh) * n), 0, n - 1);
        return m[size_t(y) * n + x] != 0;
    };
    float in[6][2]  = {{0.5f, 0.06f}, {0.06f, 0.32f}, {0.94f, 0.32f}, {0.06f, 0.68f}, {0.94f, 0.68f}, {0.5f, 0.94f}};
    float out[4][2] = {{0.04f, 0.04f}, {0.96f, 0.04f}, {0.04f, 0.96f}, {0.96f, 0.96f}};
    for (auto& p : in) if (!at(p[0], p[1])) return false;
    int corners = 0;
    for (auto& p : out) corners += at(p[0], p[1]);
    return corners <= 1; // a count digit may sit in the lower right corner
}
void sampleSlots(ID3D12Device* dev, ID3D12GraphicsCommandList* list, ID3D12Resource* ui, Layout const& L) {
    if (!L.valid || !L.hasGrid) return;
    UINT cell = UINT(std::max(8.0f, L.slot * 16.0f / 18.0f));
    if (!ensureRing(dev, cell)) return;
    int i = int(gG.frameNo % Gfx::kRing);
    if (gG.srFrame[i] >= 0) {
        void*       p = nullptr;
        D3D12_RANGE rr{0, SIZE_T(gG.srStride * kSampled)};
        if (SUCCEEDED(gG.sr[i]->Map(0, &rr, &p)) && p) {
            bool  full[kSampled] = {}, block[kSampled] = {}, glint[kSampled] = {}, cnt[9] = {};
            float cbox[9][4] = {};
            float box[kSampled][4] = {}, rgb[kSampled][3] = {}, refs[kSampled][3] = {};
            float tol  = 10.0f / 255.0f;
            bool  bgra = gG.kfmt == DXGI_FORMAT_B8G8R8A8_UNORM || gG.kfmt == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            int   n    = int(cell);
            std::vector<uint8_t> mask(size_t(n) * n);
            for (int s = 0; s < kSampled; ++s) {
                int other = 0, total = 0, x0 = n, y0 = n, x1 = -1, y1 = -1, violet = 0, cx0 = n, cy0 = n, cx1 = -1, cy1 = -1;
                double sr = 0, sgc = 0, sb = 0;
                float  ref[3] = {0, 0, 0}; // a button's own ground: its inside corner
                if (s == 11 + kButtons) { ref[0] = ref[1] = ref[2] = 73 / 255.0f; }
                else if (s >= 11) {
                    uint8_t const* q = static_cast<uint8_t*>(p) + size_t(s) * gG.srStride + size_t(1) * gG.srPitch + size_t(1) * 4;
                    float a = std::max(q[3] / 255.0f, 1e-3f);
                    ref[0] = (bgra ? q[2] : q[0]) / 255.0f / a; ref[1] = q[1] / 255.0f / a; ref[2] = (bgra ? q[0] : q[2]) / 255.0f / a;
                    std::memcpy(refs[s], ref, sizeof ref);
                }
                for (int y = 0; y < n; ++y)
                    for (int x = 0; x < n; ++x) {
                        uint8_t const* q = static_cast<uint8_t*>(p) + size_t(s) * gG.srStride + size_t(y) * gG.srPitch + size_t(x) * 4;
                        float a = q[3] / 255.0f;
                        bool  item;
                        if (s == 10) { // the parked cursor sits on transparent ground
                            item = a > 0.5f;
                            if (item) { float ia = 1.0f / a; sr += (bgra ? q[2] : q[0]) / 255.0f * ia; sgc += q[1] / 255.0f * ia; sb += (bgra ? q[0] : q[2]) / 255.0f * ia; }
                        }
                        else {
                            float r = (bgra ? q[2] : q[0]) / 255.0f, g = q[1] / 255.0f, b = (bgra ? q[0] : q[2]) / 255.0f;
                            if (a > 0.01f) { r /= a; g /= a; b /= a; }
                            bool bg   = std::fabs(r - L.bg[0]) < tol && std::fabs(g - L.bg[1]) < tol && std::fabs(b - L.bg[2]) < tol;
                            bool hi   = std::fabs(r - L.hi[0]) < tol && std::fabs(g - L.hi[1]) < tol && std::fabs(b - L.hi[2]) < tol;
                            // Bedrock draws a ghost outline (#4A494C) in an empty slot that says what goes there: not an item
                            bool ghost = std::max(r, std::max(g, b)) - std::min(r, std::min(g, b)) < 0.03f && r > 0.26f && r < 0.4f; // #4A494C, #5A5759 (lapis)
                            item = a > 0.5f && !bg && !hi && !ghost;
                            if (s >= 11) item = a > 0.5f && (std::fabs(r - ref[0]) > 0.12f || std::fabs(g - ref[1]) > 0.12f || std::fabs(b - ref[2]) > 0.12f);
                            if (s >= 11 + 3 && s < 11 + 24 && L.kind == int(Kind::Enchanting)) {
                                // the enchanting buttons: the cost is printed in colour (green / red), the runes in grey-brown
                                float sat = std::max(r, std::max(g, b)) - std::min(r, std::min(g, b));
                                item      = item && (s < 11 + 6 ? sat > 0.2f : sat < 0.2f);
                            }
                            if (item) { sr += r; sgc += g; sb += b; }
                            if (item && b > g + 0.12f && r > g + 0.04f) ++violet; // the enchantment glint's violet
                            // the stack count (white digits in the lower right)
                            if (s < 9 && a > 0.5f && r > 0.96f && g > 0.96f && b > 0.96f && x > n * 0.42f && y > n / 2) {
                                cx0 = std::min(cx0, x); cy0 = std::min(cy0, y); cx1 = std::max(cx1, x); cy1 = std::max(cy1, y);
                            }
                        }
                        mask[size_t(y) * n + x] = item;
                        ++total;
                        if (item) { ++other; x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
                    }
                full[s] = other * 50 > total;
                if (s < 9) {
                    cnt[s] = cx1 >= cx0 && cy1 - cy0 >= n / 5;
                    if (cnt[s]) { cbox[s][0] = float(cx0) / n; cbox[s][1] = float(cy0) / n; cbox[s][2] = float(cx1 + 1) / n; cbox[s][3] = float(cy1 + 1) / n; }
                }
                if (other > 0) { rgb[s][0] = float(sr / other); rgb[s][1] = float(sgc / other); rgb[s][2] = float(sb / other); }
                if (full[s]) {
                    box[s][0] = float(x0) / n; box[s][1] = float(y0) / n; box[s][2] = float(x1 + 1) / n; box[s][3] = float(y1 + 1) / n;
                    block[s] = isoBlock(mask, n, box[s]);
                    glint[s] = violet * 12 > other;
                }
            }
            D3D12_RANGE none{0, 0};
            gG.sr[i]->Unmap(0, &none);
            std::lock_guard l(gM);
            if (gS.probe >= 0 || gS.clock < gS.holdUntil) { gG.srFrame[i] = -1; goto sampled; } // a tooltip covers slots now
            {
            bool before = gS.resultFull;
            static double glintAt[9] = {-10, -10, -10, -10, -10, -10, -10, -10, -10};
            for (int s = 0; s < 9; ++s) {
                gS.cellFull[s] = full[s]; gS.cellBlock[s] = block[s];
                gS.hasCount[s] = cnt[s];
                std::memcpy(gS.countBox[s], cbox[s], sizeof cbox[s]);
                if (glint[s]) glintAt[s] = gS.clock; // the glint sweeps: an enchanted icon is violet in some frames only
                gS.cellGlint[s] = full[s] && gS.clock - glintAt[s] < 1.5;
            }
            gS.resultFull  = L.hasResult && full[9];
            gS.resultBlock = block[9];
            gS.carrying     = full[10];
            gS.carriedBlock = block[10];
            for (int s = 0; s < kSampled; ++s) { std::memcpy(gS.iconBox[s], box[s], sizeof box[s]); std::memcpy(gS.iconRgb[s], rgb[s], sizeof rgb[s]); }
            gS.previewFull = L.hasPreview && full[11 + kButtons];
            for (int k = 0; k < kButtons; ++k) {
                gS.btnFull[k] = k < L.nButtons && full[11 + k]; gS.btnBlock[k] = block[11 + k];
                std::memcpy(gS.btnRef[k], refs[11 + k], sizeof refs[11 + k]);
            }
            if (gS.resultFull != before) gS.resultChangedAt = gS.clock;
            }
        }
        gG.srFrame[i] = -1;
    }
sampled:
    barrier(list, ui, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    for (int s = 0; s < kSampled; ++s) {
        Rect r;
        if (!sampledRect(L, s, r)) continue;
        D3D12_BOX box{UINT(std::max(0.0f, r.x)), UINT(std::max(0.0f, r.y)), 0, 0, 0, 1};
        box.right  = std::min(UINT(L.W), box.left + cell);
        box.bottom = std::min(UINT(L.H), box.top + cell);
        if (box.right <= box.left || box.bottom <= box.top) continue;
        D3D12_TEXTURE_COPY_LOCATION dst{gG.sr[i].Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
        dst.PlacedFootprint.Offset    = UINT64(s) * gG.srStride;
        dst.PlacedFootprint.Footprint = {gG.kfmt, box.right - box.left, box.bottom - box.top, 1, gG.srPitch};
        D3D12_TEXTURE_COPY_LOCATION src{ui, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
    }
    barrier(list, ui, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    gG.srFrame[i] = gG.frameNo;
}

// ---------------------------------------------------------------- quads
struct Quad {
    float c[16];
    float uv[8] = {0, 0, 1, 0, 0, 1, 1, 1}; // per corner (TL, TR, BL, BR)
    float color[4] = {1, 1, 1, 1};
    float mode[4] = {0, 0, 0, 1};
    float key0[4] = {0, 0, 0, 0};
    float key1[4] = {0, 0, 0, 0};
    float hull[8][2] = {};
    float occl[4] = {0, 0, 0, 0};
};
static_assert(sizeof(Quad) == 60 * 4, "root constants");
std::vector<Quad> gQuads;

void pixelQuad(float x0, float y0, float x1, float y1, int W, int H, Quad& q) {
    auto cx = [&](float x) { return x / W * 2 - 1; };
    auto cy = [&](float y) { return 1 - y / H * 2; };
    float pts[4][2] = {{x0, y0}, {x1, y0}, {x0, y1}, {x1, y1}};
    for (int k = 0; k < 4; ++k) { q.c[k * 4] = cx(pts[k][0]); q.c[k * 4 + 1] = cy(pts[k][1]); q.c[k * 4 + 2] = 0.5f; q.c[k * 4 + 3] = 1; }
}
void solid(float x0, float y0, float x1, float y1, int W, int H, float r, float g, float b, float a) {
    Quad q;
    pixelQuad(x0, y0, x1, y1, W, H, q);
    q.color[0] = r; q.color[1] = g; q.color[2] = b; q.color[3] = a;
    gQuads.push_back(q);
}
void setRect(float uv[8], float u0, float v0, float u1, float v1) {
    uv[0] = u0; uv[1] = v0; uv[2] = u1; uv[3] = v0; uv[4] = u0; uv[5] = v1; uv[6] = u1; uv[7] = v1;
}
void uvOf(Rect r, Layout const& L, float inset, float uv[8]) {
    float i = r.w * inset;
    setRect(uv, (r.x + i) / L.W, (r.y + i) / L.H, (r.x + r.w - i) / L.W, (r.y + r.h - i) / L.H);
}
float const* gKeyOv = nullptr; // a button's own ground colour instead of the slot's (while drawing its icon)
void keyed(Quad& q, Layout const& L, float opacity, bool snap = false) {
    q.mode[0] = snap ? 4.0f : 1.0f; q.mode[1] = 1; q.mode[2] = gCfg.keyTolerance; q.mode[3] = opacity;
    for (int i = 0; i < 3; ++i) { q.key0[i] = gKeyOv ? gKeyOv[i] : L.bg[i]; q.key1[i] = gKeyOv ? gKeyOv[i] : L.hi[i]; }
    if (gKeyOv) q.mode[2] = std::max(q.mode[2], 0.06f);
}
/// world-space quad: centre, two half-axes; false when behind the camera
bool worldQuad(JPose const& cam, Basis const& b, V3 c, V3 ax, V3 ay, Quad& q) {
    V3 p[4] = {c - ax + ay, c + ax + ay, c - ax - ay, c + ax - ay}; // TL TR BL BR (uv 0,0 top-left)
    for (int k = 0; k < 4; ++k)
        if (!toClip(cam, b, p[k], q.c + k * 4)) return false;
    return true;
}

/// world-space quad from 4 corners (TL TR BL BR)
bool worldQuad4(JPose const& cam, Basis const& b, V3 const p[4], Quad& q) {
    for (int k = 0; k < 4; ++k)
        if (!toClip(cam, b, p[k], q.c + k * 4)) return false;
    return true;
}
/// Entity-style shading of a face by its world normal (top bright, bottom dark), as the vanilla entity lights do.
float faceShade(V3 n) {
    V3 l0 = V3{0.2, 1.0, -0.7}.norm(), l1 = V3{-0.2, 1.0, 0.7}.norm();
    double d = std::max(0.0, n.dot(l0)) * 0.6 + std::max(0.0, n.dot(l1)) * 0.6 + 0.4;
    return float(std::min(1.0, d));
}

// ---------------------------------------------------------------- items as the hidden slots draw them
// A block's inventory icon is the cube seen at the GUI angle (45 deg round, 30 deg down): every point of the cube's
// three visible faces can be found in the icon, so the icon unwraps back onto a real cube (ItemPlacer's block cubes).
// The icon's own lighting (bright top, shaded sides) comes along. Hidden faces repeat the visible ones.
struct IconSrc { Rect inside; float box[4]; bool snap = false; bool count = false; float countBox[4] = {}; }; // slot inside (screen px) + icon bounding box in 0..1 of it (+ the stack count's)
void iconUv(IconSrc const& s, Layout const& L, double x, double y, double z, float& u, float& v) {
    float bw = s.box[2] - s.box[0], bh = s.box[3] - s.box[1];
    float k  = bw / 1.41421f;
    if (bh / 1.5739f < k) k = bh / 1.5739f;
    float cx = (s.box[0] + s.box[2]) * 0.5f, cy = (s.box[1] + s.box[3]) * 0.5f;
    float ix = cx + float((x - z) * 0.70711) * k;
    float iy = cy - float(y * 0.86603 - (x + z) * 0.35355) * k;
    u = (s.inside.x + ix * s.inside.w) / L.W;
    v = (s.inside.y + iy * s.inside.h) / L.H;
}
/// A cube of edge `size` standing on `base`, its sides along ax (right) / az (towards the player); `opacity` fades it.
void iconCube(JPose const& cam, Basis const& b, Layout const& L, IconSrc const& src, V3 base, V3 ax, V3 az, double size, float opacity) {
    V3 ay{0, 1, 0};
    V3 c = base + V3{0, size / 2, 0};
    // faces: normal (local), and for each corner its local coordinates (x right, y up, z towards the player)
    struct F { int nx, ny, nz; double pts[4][3]; };
    const double h = 0.5;
    F faces[6] = {
        {0, 1, 0, {{-h, h, -h}, {h, h, -h}, {-h, h, h}, {h, h, h}}},     // top
        {0, 0, 1, {{-h, h, h}, {h, h, h}, {-h, -h, h}, {h, -h, h}}},     // towards the player
        {1, 0, 0, {{h, h, h}, {h, h, -h}, {h, -h, h}, {h, -h, -h}}},     // right
        {-1, 0, 0, {{-h, h, -h}, {-h, h, h}, {-h, -h, -h}, {-h, -h, h}}}, // left
        {0, 0, -1, {{h, h, -h}, {-h, h, -h}, {h, -h, -h}, {-h, -h, -h}}}, // away
        {0, -1, 0, {{-h, -h, h}, {h, -h, h}, {-h, -h, -h}, {h, -h, -h}}}, // bottom
    };
    // back faces first (painter's order is enough for a small convex box: draw the faces turned away first)
    V3 toCam = (cam.pos - c).norm();
    for (int pass = 0; pass < 2; ++pass)
        for (auto& f : faces) {
            V3   n = ax * f.nx + ay * f.ny + az * f.nz;
            bool front = n.dot(toCam) > 0;
            if (front != (pass == 1)) continue;
            Quad q;
            V3   wp[4];
            for (int k = 0; k < 4; ++k) {
                double x = f.pts[k][0], y = f.pts[k][1], z = f.pts[k][2];
                wp[k] = c + ax * (x * size) + ay * (y * size) + az * (z * size);
                // the icon face this face borrows: top -> top, x faces -> the icon's right (+X) side, z faces -> its left (+Z)
                double ux = x, uy = y, uz = z;
                if (f.ny != 0) uy = 0.5;
                else if (f.nx != 0) { ux = 0.5; uz = f.nx > 0 ? z : -z; }
                else { uz = 0.5; ux = f.nz > 0 ? x : -x; }
                iconUv(src, L, ux, uy, uz, q.uv[k * 2], q.uv[k * 2 + 1]);
            }
            if (!worldQuad4(cam, b, wp, q)) continue;
            keyed(q, L, opacity, src.snap);
            if (src.count) { // the digits and the shadow one pixel past them
                float px = 1.0f / 16.0f;
                q.occl[1]    = 1;
                q.hull[0][0] = (src.inside.x + src.inside.w * src.countBox[0]) / L.W;
                q.hull[0][1] = (src.inside.y + src.inside.h * src.countBox[1]) / L.H;
                q.hull[1][0] = (src.inside.x + src.inside.w * (src.countBox[2] + px)) / L.W;
                q.hull[1][1] = (src.inside.y + src.inside.h * (src.countBox[3] + px)) / L.H;
            }
            gQuads.push_back(q);
        }
}
/// An item lying on a surface (flat) or standing as a cube (block). `facing` = the item's up on the surface.
V3 turnY(V3 v, double deg) { double t = deg * kDeg; return {v.x * std::cos(t) - v.z * std::sin(t), v.y, v.x * std::sin(t) + v.z * std::cos(t)}; }
void placeItem(JPose const& cam, Basis const& b, Layout const& L, IconSrc const& src, bool block, V3 base, V3 right, V3 out,
               double flatSize, double blockSize, float tiltDeg, float opacity, double turnDeg = 0) {
    // turnDeg as ItemPlacer.lying's yaw (Axis.YP: counter-clockwise seen from above), from the yaw that points the
    // icon's top away from the player
    if (turnDeg != 0) { right = turnY(right, -turnDeg); out = turnY(out, -turnDeg); }
    if (block && gCfg.cubeItems) {
        iconCube(cam, b, L, src, base, right, out, blockSize, opacity);
        return;
    }
    // lying like a dropped sheet, tipped `tilt` degrees towards its owner
    double t  = tiltDeg * kDeg;
    V3     up = (out * -1.0) * std::cos(t) + V3{0, 1, 0} * std::sin(t); // the icon's top edge points away from the player
    V3     c  = base + V3{0, 0.004 + std::sin(t) * flatSize * 0.5, 0};
    Quad   q;
    if (!worldQuad(cam, b, c, right * (flatSize / 2), up * (flatSize / 2), q)) return;
    // ItemDisplayContext.FIXED turns the model half round: lying flat, the sprite is seen from its back (mirrored)
    setRect(q.uv, (src.inside.x + src.inside.w) / L.W, src.inside.y / L.H, src.inside.x / L.W, (src.inside.y + src.inside.h) / L.H);
    keyed(q, L, opacity, src.snap);
    q.key0[3] = 1; // hide the stack count
    q.key1[3] = 1; // the quad's left is the icon's right
    gQuads.push_back(q);
}

/// ItemPlacer.drawOriented(lying(awayYaw + yaw, tilt)) round a centre: a flat item `size` across, a block a cube of `size`.
void orientedItem(JPose const& cam, Basis const& b, Layout const& L, IconSrc const& src, bool block, V3 center, V3 right, V3 out, double size,
                  float tiltDeg, float opacity, double yawDeg) {
    if (block && gCfg.cubeItems) {
        placeItem(cam, b, L, src, true, center - V3{0, size / 2, 0}, right, out, size, size, 0, opacity, yawDeg);
        return;
    }
    double t = tiltDeg * kDeg;
    placeItem(cam, b, L, src, false, center - V3{0, 0.004 + std::sin(t) * size * 0.5, 0}, right, out, size, size, tiltDeg, opacity, yawDeg);
}

/// ItemPlacer.drawOriented(Axis.YP(yaw)): a flat item stands upright and turns like a card; a block is a cube turned
void cardItem(JPose const& cam, Basis const& b, Layout const& L, IconSrc const& src, bool block, V3 center, double size, float opacity, double yawDeg) {
    V3 h = turnY(V3{1, 0, 0}, -yawDeg);
    if (block && gCfg.cubeItems) {
        iconCube(cam, b, L, src, center - V3{0, size / 2, 0}, h, turnY(V3{0, 0, 1}, -yawDeg), size, opacity);
        return;
    }
    Quad q;
    if (!worldQuad(cam, b, center, h * (size / 2), V3{0, size / 2, 0}, q)) return;
    setRect(q.uv, src.inside.x / L.W, src.inside.y / L.H, (src.inside.x + src.inside.w) / L.W, (src.inside.y + src.inside.h) / L.H);
    keyed(q, L, opacity, src.snap);
    q.key0[3] = 1; // hide the stack count
    gQuads.push_back(q);
}

// ---------------------------------------------------------------- Hands + ArmMesh (the player's own arms)
struct Spring3 { // Anim.Spring3: critically damped, stepped exactly
    V3 x, v;
    void snap(V3 p) { x = p; v = {}; }
    void update(V3 t, double w, double dt) {
        double e = std::exp(-w * dt);
        auto one = [&](double& xx, double& vv, double tt) {
            double d = xx - tt, tx = (vv + w * d) * dt;
            vv = (vv - w * tx) * e;
            xx = tt + (d + tx) * e;
        };
        one(x.x, v.x, t.x); one(x.y, v.y, t.y); one(x.z, v.z, t.z);
    }
};
Spring3 gRight, gLeft;
V3      gOutroR, gOutroL;
Phase   gHandsPhase = Phase::None;
bool    gRightLanded = false, gLeftLanded = false;
constexpr double HAND_Y = 0.062, ITEM_REACH = 0.075, ARM_SCALE = 0.55;

V3 yawForward(float yaw) { double r = yaw * kDeg; return {-std::sin(r), 0, std::cos(r)}; }
V3 shoulderOf(bool right, V3 camera, float camYaw) {
    V3 f = yawForward(camYaw), rv{-f.z, 0, f.x};
    return camera + rv * (right ? 0.38 : -0.38) + V3{0, -0.46, 0} + f * 0.08;
}
V3 hiddenOf(bool right, V3 camera, float camYaw) { return shoulderOf(right, camera, camYaw) + yawForward(camYaw) * 0.1 + V3{0, -0.05, 0}; }
V3 restOf(bool right) {
    if (isFurnace(gS.st.kind)) { // FurnaceStation.rest: low in front of the furnace, or on the blast furnace's top
        Staging const& sg = gS.st.staging();
        double         u  = right ? 1 - sg.restU : sg.restU;
        V3             at = gS.st.fpoint(sg.restTop, u, sg.restV, sg.restOut);
        return sg.restTop ? at + V3{0, HAND_Y - sg.restOut, 0} : at;
    }
    if (gS.st.kind == Kind::Enchanting) // EnchantStation.rest
        return gS.st.top + gS.st.sideV() * 0.36 + gS.st.right() * (right ? 0.24 : -0.24) + V3{0, HAND_Y, 0};
    if (gS.st.kind == Kind::Loom) // LoomStation.rest
        return gS.st.top + gS.st.sideV() * 0.4 + gS.st.right() * (right ? 0.25 : -0.25) + V3{0, HAND_Y, 0};
    if (gS.st.kind == Kind::Cartography) // CartographyStation.rest
        return gS.st.top + gS.st.sideV() * 0.4 + gS.st.right() * (right ? 0.26 : -0.26) + V3{0, HAND_Y, 0};
    if (gS.st.kind == Kind::Smithing) // SmithingStation.rest
        return gS.st.top + gS.st.sideV() * 0.4 + gS.st.right() * (right ? 0.25 : -0.25) + V3{0, HAND_Y, 0};
    if (gS.st.kind == Kind::Stonecutter) // StonecutterStation.rest
        return gS.st.top + gS.st.sideV() * 0.36 + gS.st.right() * (right ? 0.26 : -0.38) + V3{0, HAND_Y, 0};
    if (gS.st.kind == Kind::Grindstone) // GrindstoneStation.rest: on the frame, on top of the axle posts either side of the wheel
        return gS.st.wheel + gS.st.right() * (right ? 0.31 : -0.31) + gS.st.sideV() * 0.1 + V3{0, 3 / 16.0 + HAND_Y, 0};
    if (gS.st.kind == Kind::Anvil) // AnvilStation.rest: on the near edge of the face, either side of the work
        return gS.st.top + gS.st.sideV() * 0.24 + gS.st.right() * (right ? 0.3 : -0.3) + V3{0, HAND_Y, 0};
    // TableStation.rest
    return gS.st.top + gS.st.sideV() * 0.4 + gS.st.right() * (right ? 0.24 : -0.24) + V3{0, HAND_Y, 0};
}
V3 handTarget(V3 aim, V3 shoulder, bool carrying, double tap) {
    if (isFurnace(gS.st.kind)) { // FurnaceStation.handTarget
        if (gS.aimTop) {
            V3     reach = V3{aim.x - shoulder.x, 0, aim.z - shoulder.z}.norm();
            double hover = carrying ? 0.075 : 0.045;
            return aim + V3{0, HAND_Y + hover - 0.07 * tap, 0} - reach * 0.07;
        }
        V3     reach = (aim - shoulder).norm();
        double hover = carrying ? 0.11 : 0.075;
        // a tap pushes the fist past the face, into the opening
        return aim + gS.st.sideV() * (hover - 0.18 * tap) - reach * 0.07 + V3{0, 0.035, 0};
    }
    if (gS.st.kind == Kind::Enchanting && gS.aimAir) { // EnchantStation.handTarget: a hand reaching out to something floating stops just short of it
        V3 dir = (aim - shoulder).norm();
        return aim - dir * (0.14 - 0.05 * tap) + V3{0, -0.03, 0};
    }
    // TableStation.handTarget (the anvil's is the same at the point aimed at)
    V3     reach = V3{aim.x - shoulder.x, 0, aim.z - shoulder.z}.norm();
    double hover = carrying ? 0.075 : 0.045;
    double y     = gS.st.kind == Kind::Table ? gS.st.by + 1.0 : aim.y;
    return V3{aim.x, y + HAND_Y + hover - 0.06 * tap, aim.z} - reach * (gS.st.kind == Kind::Grindstone || gS.st.kind == Kind::Stonecutter ? 0.07 : ITEM_REACH);
}
V3 heldItemPoint(V3 hand, V3 shoulder) {
    if (isFurnace(gS.st.kind) && !gS.aimTop) { // FurnaceStation.heldItemPoint on the front
        V3 reach = (hand - shoulder).norm();
        return hand + reach * 0.07 + V3{0, -0.045, 0};
    }
    V3 reach = V3{hand.x - shoulder.x, 0, hand.z - shoulder.z}.norm();
    bool r07 = isFurnace(gS.st.kind) || gS.st.kind == Kind::Grindstone || gS.st.kind == Kind::Stonecutter;
    return hand + reach * (r07 ? 0.07 : ITEM_REACH) + V3{0, -HAND_Y + 0.012, 0};
}
double craftProgress() { double t = (gS.clock - gS.craftAt) / 0.5; return t >= 0 && t <= 1 ? t : -1; }

// ---- AnvilStation's forging: the hammer's swing (grip + angle) on the three-blow timeline
struct Swing { V3 grip; double angle; };
V3 swingDirection() { return (gS.st.sideV() * -0.3 + gS.st.right() * -0.12).norm(); }
V3 handleDirection(double angle) { double a = angle * kDeg; return swingDirection() * std::cos(a) + V3{0, std::sin(a), 0}; }
V3 headAxis(double angle) { double a = angle * kDeg; return V3{0, std::cos(a), 0} - swingDirection() * std::sin(a); }
constexpr double HAMMER_SCALE = 0.30, STRIKE_ALONG = (1.125 - 0.18) * HAMMER_SCALE, STRIKE_ACROSS = 0.375 * HAMMER_SCALE;
Swing impactSwing() { double a = -12; return {anvilPoint() - handleDirection(a) * STRIKE_ALONG + headAxis(a) * STRIKE_ACROSS, a}; }
Swing raisedSwing() { Swing h = impactSwing(); return {h.grip + V3{0, 0.16, 0} + gS.st.sideV() * 0.05, 95}; }
Swing reboundSwing() { Swing h = impactSwing(); return {h.grip + V3{0, 0.05, 0} + gS.st.sideV() * 0.02, 28}; }
Swing hiddenSwing(JPose const& cam) { return {hiddenOf(true, cam.pos, cam.yaw), 80}; }
Swing mixSwing(Swing a, Swing b, double k) { return {lerp(a.grip, b.grip, k), a.angle + (b.angle - a.angle) * k}; }
double inCubic(double t) { t = clamp01(t); return t * t * t; }
bool hammerAt(double t, JPose const& cam, Swing& out) {
    if (t < 0.3 || t > PUT_AWAY) return false;
    if (t < 0.55) { out = mixSwing(hiddenSwing(cam), raisedSwing(), outCubic(window(t, 0.3, 0.55))); return true; }
    for (int k = 0; k < 3; ++k) {
        double blow = BLOWS[k], windFrom = k == 0 ? 0.55 : BLOWS[k - 1] + 0.12;
        if (t < blow - SWING) { out = mixSwing(k == 0 ? raisedSwing() : reboundSwing(), raisedSwing(), inOutSine(window(t, windFrom, blow - SWING))); return true; }
        if (t < blow) { out = mixSwing(raisedSwing(), impactSwing(), inCubic(window(t, blow - SWING, blow))); return true; }
        if (t < blow + 0.12 || k == 2) {
            if (k == 2 && t >= blow + 0.12) { out = mixSwing(reboundSwing(), hiddenSwing(cam), inCubic(window(t, blow + 0.12, PUT_AWAY))); return true; }
            out = mixSwing(impactSwing(), reboundSwing(), outCubic(window(t, blow, blow + 0.12)));
            return true;
        }
    }
    return false;
}
void anvilHands(JPose const& cam, V3& right, V3& left) {
    double t = moveTime();
    Swing  sw;
    bool   swing = hammerAt(t, cam, sw);
    if (t < 0.3) right = lerp(restOf(true), hiddenSwing(cam).grip, inCubic(window(t, 0, 0.3)));
    else if (swing) right = sw.grip;
    else { // empty-handed again: up from below to the new item, then settle with it
        V3 lifted = anvilMain() + V3{0, 0.13 + HAND_Y, 0} + gS.st.sideV() * ITEM_REACH;
        V3 up     = lerp(hiddenSwing(cam).grip, lifted, outCubic(window(t, PUT_AWAY, PUT_AWAY + 0.16)));
        right     = lerp(up, restOf(true) + V3{0, gS.moveToInv ? 0 : 0.05, 0}, inOutCubic(window(t, PUT_AWAY + 0.16, FORGE_LENGTH)));
    }
    // the other hand steadies the work at its left end
    double hold   = inOutCubic(window(t, 0.05, 0.4)) * (1 - inOutCubic(window(t, BLOWS[2] + 0.1, BLOWS[2] + 0.4)));
    V3     steady = anvilMain() + gS.st.right() * -0.2 + gS.st.sideV() * 0.05 + V3{0, HAND_Y + 0.015, 0};
    left          = lerp(restOf(false), steady, hold);
}

/// GrindstoneStation.handsOverride
void grindHands(JPose const& cam, V3& right, V3& left) {
    double t = moveTime();
    if (t < G_LET_GO) right = lerp(restOf(true), grindHandle(0, grindHeldPos(0, t)), inOutCubic(window(t, 0, 0.28)));
    else {
        V3 up  = lerp(grindHandle(0, grindHeldPos(0, G_LET_GO)), grindLifted() + gS.st.sideV() * 0.1 + V3{0, HAND_Y * 0.6, 0},
                      inOutCubic(window(t, G_LET_GO, G_LET_GO + 0.3)));
        V3 end = restOf(true) + V3{0, gS.moveToInv ? 0 : 0.05, 0};
        right  = lerp(up, end, inOutCubic(window(t, G_LET_GO + 0.3, GRIND_LENGTH)));
    }
    (void)cam;
    if (gS.addWasFull) {
        // the second item is ground with the other hand, mirrored
        double hold = inOutCubic(window(t, 0.05, 0.33)) * (1 - inOutCubic(window(t, G_CLEAN_AT, G_CLEAN_AT + 0.3)));
        left        = lerp(restOf(false), grindHandle(1, grindHeldPos(1, std::min(t, G_CLEAN_AT))), hold);
    } else {
        // the other hand braces the frame, bearing down with every stroke
        double bear = std::fabs(std::sin(3.14159265358979 * (t - G_CONTACT) / G_STROKE)) * window(t, G_CONTACT, G_CONTACT + 0.1) *
                      (1 - window(t, G_LET_GO - 0.1, G_LET_GO));
        left = restOf(false) + V3{0, -0.008 * bear, 0} + gS.st.sideV() * (-0.02 * window(t, 0.1, 0.4));
    }
}

/// StonecutterStation.handsOverride: the left hand takes a block off the stone and pushes it through the blade from
/// the left; the right waits past the blade for the new piece, then keeps it
void stoneHands(V3& right, V3& left) {
    double t = moveTime(), out = stoneOutAt(gS.cutAll), length = gS.cutLength;
    V3     top = gS.st.top, rgt = gS.st.right(), sv = gS.st.sideV();
    V3     onStack = stoneStack() + V3{0, S_PIECE + HAND_Y, 0} + sv * 0.05;
    if (t < 0.08) left = lerp(restOf(false), onStack, inOutCubic(t / 0.08));
    else {
        V3 start = top + rgt * -0.63 + V3{0, S_PIECE * 0.55 + HAND_Y, 0} + sv * 0.05;
        V3 path  = t < S_BITE ? lerp(onStack, start, inOutCubic(window(t, 0.06, S_BITE - 0.02)))
                              : top + rgt * (stoneAlong(t) - 0.17) + V3{0, S_PIECE * 0.55 + HAND_Y, 0} + sv * 0.05;
        left = lerp(path, restOf(false), inOutCubic(window(t, out - 0.05, length)));
    }
    V3 waiting = lerp(restOf(true), stoneCatch(), inOutCubic(window(t, S_BITE, out - 0.05)));
    V3 end     = restOf(true) + V3{0, gS.cutAll ? 0 : 0.05, 0};
    right      = lerp(waiting, end, inOutCubic(window(t, out, length)));
}

/// SmithingStation.handsOverride
void smithHands(V3& r, V3& l) {
    double t  = moveTime();
    V3     rg = gS.st.right(), sv = gS.st.sideV();
    r = restOf(true);
    l = restOf(false);
    // both hands carry the template over and lay it on the gear
    double carry    = inOutCubic(window(t, 0, 0.12)) * (1 - inOutCubic(window(t, TEMPLATE_DOWN, TEMPLATE_DOWN + 0.1)));
    V3     template_ = templatePos(std::max(t, 0.06));
    r = lerp(r, template_ + rg * 0.09 + sv * 0.04 + V3{0, HAND_Y, 0}, carry);
    l = lerp(l, template_ + rg * -0.09 + sv * 0.04 + V3{0, HAND_Y, 0}, carry);
    // the right hand fetches the material and lays it on top, while the left keeps the template in place
    double fetch = inOutCubic(window(t, TEMPLATE_DOWN + 0.02, MATERIAL_UP - 0.03)) * (1 - inOutCubic(window(t, MATERIAL_DOWN, MATERIAL_DOWN + 0.08)));
    r = lerp(r, materialPos(t) + sv * ITEM_REACH + V3{0, HAND_Y, 0}, fetch);
    double steady = inOutCubic(window(t, TEMPLATE_DOWN, TEMPLATE_DOWN + 0.12)) * (1 - inOutCubic(window(t, MATERIAL_DOWN, SM_PRESS - 0.05)));
    l = lerp(l, smithPoint(1) + rg * -0.14 + sv * 0.05 + V3{0, HAND_Y + 0.012, 0}, steady);
    // both hands press the stack together, then back away from the heat
    double press = inOutCubic(window(t, MATERIAL_DOWN + 0.02, SM_PRESS - 0.03)) * (1 - inOutCubic(window(t, SM_PRESS + 0.16, SM_PRESS + 0.34)));
    V3     stack = smithPoint(1) + sv * 0.03 + V3{0, HAND_Y + 0.05 - 0.03 * hump(window(t, SM_PRESS - 0.07, SM_PRESS + 0.15)), 0};
    r = lerp(r, stack + rg * 0.07, press);
    l = lerp(l, stack + rg * -0.07, press);
    double away = hump(window(t, SM_PRESS + 0.25, SM_COOLED - 0.1));
    r = r + sv * (0.06 * away) + V3{0, 0.04 * away, 0};
    l = l + sv * (0.06 * away) + V3{0, 0.04 * away, 0};
    if (t > SM_COOLED - 0.1) { // the right hand takes the new piece
        V3     grab = smithPoint(1) + V3{0, HAND_Y + 0.02, 0} + sv * ITEM_REACH;
        double take = inOutCubic(window(t, SM_COOLED - 0.1, SM_COOLED + 0.12));
        V3     end  = restOf(true) + V3{0, gS.moveToInv ? 0 : 0.05, 0};
        r = lerp(lerp(r, grab, take), end, inOutCubic(window(t, SM_COOLED + 0.12, SMITH_LENGTH)));
    }
}

/// CartographyStation.handsOverride: the right hand brings the addition over, the left holds the map down by its corner
void cartHands(V3& right, V3& left) {
    double t = moveTime();
    V3     rg = gS.st.right(), sv = gS.st.sideV();
    double carry = inOutCubic(window(t, 0, 0.12)) * (1 - inOutCubic(window(t, 0.6, 0.85)));
    right = lerp(restOf(true), cartAdditionPos(t) + sv * ITEM_REACH + V3{0, HAND_Y + 0.02, 0}, carry);
    V3     corner = cartPoint(0) + rg * (-MAP_SIZE * 0.4) + sv * (MAP_SIZE * 0.35) + V3{0, HAND_Y + 0.01, 0};
    double hold   = inOutCubic(window(t, 0.05, 0.3)) * (1 - inOutCubic(window(t, CT_BEAT - 0.1, CT_BEAT + 0.1)));
    left = lerp(restOf(false), corner, hold);
    if (t > 0.85 && t < CT_BEAT) { // smoothing the sheet flat across, or tracing the land with a fingertip
        double s     = window(t, 0.85, CT_BEAT - 0.05);
        V3     sweep = cartPoint(0) + rg * (-0.15 + 0.3 * s) + sv * (0.08 * std::sin(s * kPi * 3)) + V3{0, HAND_Y + 0.02, 0};
        right = lerp(restOf(true), sweep, hump(s) * 0.9 + 0.1);
    }
    if (t >= CT_BEAT - 0.1) {
        V3 grab = cartPoint(0) + sv * 0.18 + V3{0, HAND_Y + 0.03, 0};
        right = lerp(lerp(right, grab, inOutCubic(window(t, CT_BEAT - 0.1, CT_BEAT + 0.05))), restOf(true) + V3{0, gS.moveToInv ? 0 : 0.05, 0},
                     inOutCubic(window(t, CT_BEAT + 0.05, WORK_LENGTH)));
    }
}

/// LoomStation.handsOverride: each hand catches the shuttle as it arrives and beats the row tight
void loomHands(V3& right, V3& left) {
    double t  = moveTime();
    V3     ac = gS.st.right(), nearV = gS.st.sideV();
    double in = inOutCubic(window(t, 0, WEAVE_FROM)) * (1 - inOutCubic(window(t, WEAVE_TO - 0.05, L_BEAT)));
    V3     line = weaveLine(std::min(t, WEAVE_TO)) + nearV * 0.06 + V3{0, HAND_Y + 0.03, 0};
    double across = shuttleAcross(t);
    double beat   = hump(window(std::fmod(loomWoven(t) * L_PASSES, 1.0), 0.8, 1.0)) * 0.02;
    V3     r = line + ac * (CLOTH_WIDTH / 2 + 0.06 - 0.05 * std::max(0.0, across)) - nearV * beat;
    V3     l = line + ac * (-CLOTH_WIDTH / 2 - 0.06 + 0.05 * std::max(0.0, -across)) - nearV * beat;
    right = lerp(restOf(true), r, in);
    left  = lerp(restOf(false), l, in);
    if (t > L_BEAT - 0.1) { // the right hand takes the banner off the loom
        V3 grab = loomPoint(0) + nearV * 0.2 + V3{0, HAND_Y + 0.04, 0};
        right = lerp(lerp(right, grab, inOutCubic(window(t, L_BEAT - 0.1, L_BEAT + 0.05))), restOf(true) + V3{0, gS.moveToInv ? 0 : 0.05, 0},
                     inOutCubic(window(t, L_BEAT + 0.05, WEAVE_LENGTH)));
    }
}

void updateHands(JPose const& cam, double dt) {
    V3    cp = cam.pos;
    float cy = cam.yaw;
    if (gS.phase != gHandsPhase) {
        if (gS.phase == Phase::Intro) { gRightLanded = gLeftLanded = false; gS.leftWorks = false; }
        if (gS.phase == Phase::Active) { gRight.snap(restOf(true)); gLeft.snap(restOf(false)); }
        if (gS.phase == Phase::Outro) { gOutroR = gRight.x; gOutroL = gLeft.x; }
        gHandsPhase = gS.phase;
    }
    auto arrive = [&](bool right, double k) { // Hands.arrive
        V3     from = hiddenOf(right, cp, cy), to = restOf(right);
        double e = outCubic(k);
        double hover = 0.11 * hump(std::min(1.0, k * 1.15)) * (1 - window(k, 0.75, 1.0));
        return lerp(from, to, e) + V3{0, hover, 0};
    };
    auto depart = [&](V3 from, bool right, double u) { // Hands.depart
        double lift = outCubic(window(u, 0.0, 0.3)) * 0.07;
        double away = std::pow(clamp01(window(u, 0.18, 0.72)), 3);
        return lerp(from + V3{0, lift, 0}, hiddenOf(right, cp, cy), away);
    };
    switch (gS.phase) {
    case Phase::Intro: {
        double u = clamp01(gS.time / gS.leanLength);
        gRight.snap(arrive(true, window(u, 0.42, 0.93)));
        gLeft.snap(arrive(false, window(u, 0.47, 0.98)));
        break;
    }
    case Phase::Active: {
        if ((gS.st.kind == Kind::Anvil || gS.st.kind == Kind::Grindstone || gS.st.kind == Kind::Stonecutter || gS.st.kind == Kind::Smithing ||
             gS.st.kind == Kind::Cartography || gS.st.kind == Kind::Loom || gS.st.kind == Kind::Enchanting) && moving()) { // handsOverride, springs at 55
            V3 r, l;
            if (gS.st.kind == Kind::Enchanting) { // EnchantStation.handsOverride: during the rite both hands rise around the item
                double raise = hump(window(ritualTime(), 0, 0.85));
                V3     c     = enchItemPos() + gS.st.sideV() * 0.07 + V3{0, -0.1 + 0.05 * raise, 0};
                double sp    = 0.17 - 0.04 * raise; // * max(0.8, geometry().scale(0)), the scale 1 here
                r = c + gS.st.right() * sp;
                l = c + gS.st.right() * -sp;
            } else if (gS.st.kind == Kind::Anvil) anvilHands(cam, r, l);
            else if (gS.st.kind == Kind::Stonecutter) stoneHands(r, l);
            else if (gS.st.kind == Kind::Smithing) smithHands(r, l);
            else if (gS.st.kind == Kind::Cartography) cartHands(r, l);
            else if (gS.st.kind == Kind::Loom) loomHands(r, l);
            else grindHands(cam, r, l);
            gRight.update(r, 55, dt);
            gLeft.update(l, 55, dt);
            break;
        }
        double craft = craftProgress();
        if (craft >= 0) { // TableStation.handsOverride: the hands press the ingredients together
            V3     middle = gS.st.top + gS.st.sideV() * 0.04;
            double y      = HAND_Y + 0.05 - 0.07 * hump(window(craft, 0.08, 0.62));
            gRight.update(middle + gS.st.right() * 0.085 + V3{0, y, 0}, 20, dt);
            gLeft.update(middle + gS.st.right() * -0.085 + V3{0, y, 0}, 20, dt);
            break;
        }
        if (gS.haveAim) {
            double lateral = (gS.aim - gS.st.center()).dot(gS.st.right());
            if (!gS.leftWorks && lateral < -0.13) gS.leftWorks = true;
            else if (gS.leftWorks && lateral > -0.07) gS.leftWorks = false;
        }
        bool   carrying = gS.carrying;
        double tap      = hump(window(gS.clock - gS.tapAt, 0, 0.22));
        V3     shoulder = shoulderOf(!gS.leftWorks, cp, cy);
        V3     working  = gS.haveAim ? handTarget(gS.aim, shoulder, carrying, tap)
                                     : restOf(!gS.leftWorks) + V3{0, (carrying ? 0.05 : 0.0) - 0.02 * tap, 0};
        double idle = gS.clock;
        V3 restR = restOf(true) + V3{0, 0.003 * std::sin(idle * 1.7), 0};
        V3 restL = restOf(false) + V3{0, 0.003 * std::sin(idle * 1.5 + 2), 0};
        gRight.update(gS.leftWorks ? restR : working, gS.leftWorks ? 12 : 17, dt);
        gLeft.update(gS.leftWorks ? working : restL, gS.leftWorks ? 17 : 12, dt);
        break;
    }
    case Phase::Outro: {
        double u = gS.time / OUTRO_LENGTH;
        gRight.snap(depart(gOutroR, true, u));
        gLeft.snap(depart(gOutroL, false, u - 0.05));
        break;
    }
    default: break;
    }
}

/// One box of an arm (ModelPart.Cube): model pixels, +Y towards the hand; UV in skin pixels (64 wide layout).
struct ArmBox { float x0, y0, z0, w, h, d; int u, v; float grow; unsigned faces; };
enum : unsigned { FUp = 1, FDown = 2, FWest = 4, FNorth = 8, FEast = 16, FSouth = 32, FSides = 4 | 8 | 16 | 32 };
void armBox(JPose const& cam, Basis const& b, ArmBox const& a, V3 pivot, V3 X, V3 Y, V3 Z, float centerX, float stretch, bool stretchY,
            float uScale, float vScale, float opacity) {
    float x0 = a.x0 - a.grow, x1 = a.x0 + a.w + a.grow, y0 = a.y0 - a.grow, y1 = a.y0 + a.h + a.grow, z0 = a.z0 - a.grow, z1 = a.z0 + a.d + a.grow;
    const float joint = 6.0f; // (UPPER - 2) px: the upper piece stretches away from the elbow-less joint
    auto wp = [&](float x, float y, float z) {
        if (stretchY) y = joint + (y - joint) * stretch;
        double s = ARM_SCALE / 16.0;
        return pivot + X * ((x - centerX) * s) + Y * (y * s) + Z * (z * s);
    };
    float u = float(a.u), v = float(a.v), w = a.w, h = a.h, d = a.d;
    struct Face { unsigned bit; float uv[4]; float p[4][3]; V3 n; };
    Face fs[6] = {
        {FUp, {u + d, v, u + d + w, v + d}, {{x0, y0, z0}, {x1, y0, z0}, {x0, y0, z1}, {x1, y0, z1}}, Y * -1},
        {FDown, {u + d + w, v, u + d + 2 * w, v + d}, {{x0, y1, z0}, {x1, y1, z0}, {x0, y1, z1}, {x1, y1, z1}}, Y},
        {FWest, {u, v + d, u + d, v + d + h}, {{x0, y0, z1}, {x0, y0, z0}, {x0, y1, z1}, {x0, y1, z0}}, X * -1},
        {FNorth, {u + d, v + d, u + d + w, v + d + h}, {{x0, y0, z0}, {x1, y0, z0}, {x0, y1, z0}, {x1, y1, z0}}, Z * -1},
        {FEast, {u + d + w, v + d, u + 2 * d + w, v + d + h}, {{x1, y0, z0}, {x1, y0, z1}, {x1, y1, z0}, {x1, y1, z1}}, X},
        {FSouth, {u + 2 * d + w, v + d, u + 2 * d + 2 * w, v + d + h}, {{x1, y0, z1}, {x0, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}, Z},
    };
    for (auto& f : fs) {
        if (!(a.faces & f.bit)) continue;
        Quad q;
        V3   p[4];
        for (int k = 0; k < 4; ++k) p[k] = wp(f.p[k][0], f.p[k][1], f.p[k][2]);
        if (!worldQuad4(cam, b, p, q)) continue;
        setRect(q.uv, f.uv[0] * uScale, f.uv[1] * vScale, f.uv[2] * uScale, f.uv[3] * vScale);
        float sh = faceShade(f.n.norm());
        q.color[0] = q.color[1] = q.color[2] = sh;
        q.mode[0] = 2; q.mode[3] = opacity;
        gQuads.push_back(q);
    }
}
/// Hands.drawArm (armStyle 0 stretching, 2 floating forearm): the arm from the shoulder to the hand.
void drawArm(JPose const& cam, Basis const& b, V3 hand, V3 shoulder, bool right, bool slim, int skinW, int skinH) {
    V3 d = hand - shoulder;
    if (d.len() < 1e-4) return;
    double length = d.len() + 0.18;
    d = d.norm();
    V3 up{0, 1, 0};
    V3 upPerp = up - d * up.dot(d);
    if (upPerp.len() < 1e-3) upPerp = gS.st.forward();
    upPerp = upPerp.norm();
    V3 X = right ? upPerp * -1.0 : upPerp; // the forearm turned palm-down: the inner face of each arm looks at the work
    V3 Z{X.y * d.z - X.z * d.y, X.z * d.x - X.x * d.z, X.x * d.y - X.y * d.x};
    float stretch = float(std::max(0.6, (length * 16 / ARM_SCALE - 4.0) / 8.0));
    float centerX = slim ? (right ? -0.5f : 0.5f) : (right ? -1.0f : 1.0f);
    V3    pivot   = hand - d * (10.0 / 16.0 * ARM_SCALE);
    float width = slim ? 3.0f : 4.0f;
    float x0    = right ? (slim ? -2.0f : -3.0f) : -1.0f;
    bool  legacy = skinH * 2 <= skinW; // 64x32 skins have no left arm of their own: the right arm is reused
    int   au = right || legacy ? 40 : 32, av = right || legacy ? 16 : 48;
    int   su = right ? 40 : 48, sv = right ? 32 : 48;
    float uS = 1.0f / 64.0f, vS = 1.0f / (64.0f * float(skinH) / float(std::max(1, skinW)));
    bool  floating = gCfg.armStyle == 2;
    auto pieces = [&](int u, int v, float grow) {
        if (floating) {
            // ArmMesh.renderFloating: the hand, a short upper piece and its cap
            armBox(cam, b, {x0, 6, -2, width, 4, 4, u, v + 8, grow, FSides}, pivot, X, d, Z, centerX, 1, false, uS, vS, 1);
            armBox(cam, b, {x0, -2, -2, width, 8, 4, u, v, grow, FSides | FDown}, pivot, X, d, Z, centerX, 0.4f, true, uS, vS, 1);
            return;
        }
        armBox(cam, b, {x0, 6, -2, width, 4, 4, u, v + 8, grow, FSides}, pivot, X, d, Z, centerX, 1, false, uS, vS, 1);          // hand
        armBox(cam, b, {x0, -2, -2, width, 12, 4, u, v, grow, FUp}, pivot, X, d, Z, centerX, 1, false, uS, vS, 1);               // cap
        armBox(cam, b, {x0, -2, -2, width, 8, 4, u, v, grow, FSides | FDown}, pivot, X, d, Z, centerX, stretch, true, uS, vS, 1); // upper
    };
    pieces(au, av, 0.0f);
    if (!legacy) pieces(su, sv, 0.25f); // the sleeve layer (alpha tested)
}

/// HammerModel.render: the model gripped at `grip`, its handle along `along`, its striking face away from `head`.
void drawHammer(JPose const& cam, Basis const& b, V3 grip, V3 along, V3 head) {
    V3 X = head, Y = along, Z{X.y * Y.z - X.z * Y.y, X.z * Y.x - X.x * Y.z, X.x * Y.y - X.y * Y.x};
    auto wp = [&](double x, double y, double z) {
        // pose.scale(0.3); translate(-GRIP): model pixels / 16 - grip (0.5, 0.18, 0.5)
        double mx = (x / 16 - 0.5) * HAMMER_SCALE, my = (y / 16 - 0.18) * HAMMER_SCALE, mz = (z / 16 - 0.5) * HAMMER_SCALE;
        return grip + X * mx + Y * my + Z * mz;
    };
    for (auto const& e : hammer::kElements) {
        double x1 = e.from[0], y1 = e.from[1], z1 = e.from[2], x2 = e.to[0], y2 = e.to[1], z2 = e.to[2];
        // corners TL TR BL BR as Minecraft's FaceBakery lays out each face, seen from outside
        double P[6][4][3] = {
            {{x1, y1, z2}, {x2, y1, z2}, {x1, y1, z1}, {x2, y1, z1}}, // down
            {{x1, y2, z1}, {x2, y2, z1}, {x1, y2, z2}, {x2, y2, z2}}, // up
            {{x2, y2, z1}, {x1, y2, z1}, {x2, y1, z1}, {x1, y1, z1}}, // north
            {{x1, y2, z2}, {x2, y2, z2}, {x1, y1, z2}, {x2, y1, z2}}, // south
            {{x1, y2, z1}, {x1, y2, z2}, {x1, y1, z1}, {x1, y1, z2}}, // west
            {{x2, y2, z2}, {x2, y2, z1}, {x2, y1, z2}, {x2, y1, z1}}, // east
        };
        V3 N[6] = {Y * -1.0, Y, Z * -1.0, Z, X * -1.0, X};
        for (int f = 0; f < 6; ++f) {
            Quad q;
            V3   p4[4];
            for (int k = 0; k < 4; ++k) p4[k] = wp(P[f][k][0], P[f][k][1], P[f][k][2]);
            if (!worldQuad4(cam, b, p4, q)) continue;
            auto const& fc = e.face[f];
            float u0 = fc.u0 / PROP_W, v0 = fc.v0 / PROP_H, u1 = fc.u1 / PROP_W, v1 = fc.v1 / PROP_H;
            if (fc.rot == 180) { std::swap(u0, u1); std::swap(v0, v1); }
            setRect(q.uv, u0, v0, u1, v1);
            float sh = faceShade(N[f].norm());
            q.color[0] = q.color[1] = q.color[2] = sh;
            q.mode[0] = 3;
            gQuads.push_back(q);
        }
    }
}

// ---- Fx: sparks (Fx.spark / hotSparks / blowSparks / fountain / flakes / motes), points and streaks of light
struct Spark { V3 p, v; double age, life; float size, r, g, bl; double gravity, streak; int phase; };
std::vector<Spark> gSparks;
uint32_t gRng = 0x9E3779B9u;
double rnd() { gRng ^= gRng << 13; gRng ^= gRng >> 17; gRng ^= gRng << 5; return (gRng & 0xFFFFFF) / double(0x1000000); }
double gauss() { double u = std::max(1e-6, rnd()), v = rnd(); return std::sqrt(-2 * std::log(u)) * std::cos(6.2831853 * v); }
double spread(double amount) { return (rnd() - 0.5) * 2 * amount; }
void spark(V3 at, V3 v, double life, float size, float r, float g, float b, double gravity, double streak) {
    if (gSparks.size() >= 900) return; // MAX_SPARKS
    gSparks.push_back({at, v, 0, life, size, r, g, b, gravity, streak, int(gSparks.size())});
}
void hotSparks(V3 at, V3 direction, double spreadAmount, int count, double speed) {
    for (int i = 0; i < count; ++i) {
        V3     dir  = (direction + V3{spread(spreadAmount), spread(spreadAmount), spread(spreadAmount)}).norm();
        double v    = speed * (0.55 + rnd() * 0.7);
        float  warm = float(rnd());
        spark(at + V3{spread(0.012), spread(0.012), spread(0.012)}, dir * v, 0.2 + rnd() * 0.35, float(0.0075 + rnd() * 0.006), 1.0f,
              0.78f + 0.2f * warm, 0.45f + 0.4f * warm, 4.5, 0.045);
    }
}
void blowSparks(V3 at, int count, double speed) {
    for (int i = 0; i < count; ++i) {
        double a = rnd() * 6.283185307179586;
        hotSparks(at, V3{std::cos(a), 0.35 + rnd() * 0.9, std::sin(a)}, 0.15, 1, speed);
    }
}
void fountain(V3 at, V3 lean, V3 across, int count, double speed) {
    for (int i = 0; i < count; ++i) hotSparks(at, V3{0, 1, 0} + lean + across * spread(0.75), 0.18, 1, speed);
}
void flakes(V3 at, double radius, int count) { // the enchantment leaving an item: violet motes falling away
    for (int i = 0; i < count; ++i) {
        V3    from = at + V3{spread(radius), spread(radius * 0.4), spread(radius)};
        V3    v{spread(0.25), 0.1 + rnd() * 0.2, spread(0.25)};
        float pick = float(rnd());
        spark(from, v, 0.9 + rnd() * 0.7, float(0.014 + rnd() * 0.014), 0.75f + 0.25f * pick, 0.4f + 0.3f * pick, 1.0f, 0.5, 0);
    }
}
void motes(V3 at, double radius, int count, float r, float g, float b, double rise) {
    for (int i = 0; i < count; ++i)
        spark(at + V3{spread(radius), spread(radius * 0.3), spread(radius)}, V3{spread(0.04), rise * (0.6 + rnd() * 0.6), spread(0.04)},
              0.6 + rnd() * 0.6, float(0.008 + rnd() * 0.01), r, g, b, 0, 0);
}
void steam(V3 at, double radius, int count) { // Fx.steam: faint grey wisps of light rising and spreading
    for (int i = 0; i < count; ++i) {
        float k = float(0.16 + rnd() * 0.1);
        spark(at + V3{spread(radius), 0, spread(radius)}, V3{spread(0.06), 0.18 + rnd() * 0.16, spread(0.06)}, 0.9 + rnd() * 0.6,
              float(0.028 + rnd() * 0.025), k, k, k * 1.05f, -0.05, 0);
    }
}
/// EnchantFx.billboard: a soft glow facing the camera (added light)
void glowBillboard(JPose const& cam, Basis const& b, V3 at, double radius, float r, float g, float bl) {
    Quad q;
    if (!worldQuad(cam, b, at, b.r * radius, b.u * radius, q)) return;
    setRect(q.uv, 0, 0, 1, 1);
    q.color[0] = r; q.color[1] = g; q.color[2] = bl; q.color[3] = 1;
    q.mode[0] = 5;
    gQuads.push_back(q);
}
void drawSparks(JPose const& cam, Basis const& b, double clock, double dt) { // Fx.renderSparks
    (void)clock;
    for (size_t i = gSparks.size(); i-- > 0;) {
        Spark& s = gSparks[i];
        s.age += dt;
        if (s.age >= s.life) { gSparks[i] = gSparks.back(); gSparks.pop_back(); continue; }
        double drag = std::exp((s.streak > 0 ? -0.9 : -2.2) * dt);
        s.v.x *= drag;
        s.v.y = s.v.y * drag - s.gravity * dt;
        s.v.z *= drag;
        s.p = s.p + s.v * dt;
        if (s.streak > 0) {
            // a hot spark is brightest when struck and cools as it flies (Fx.streak)
            float k    = float(std::pow(1 - s.age / s.life, 1.4));
            V3    head = s.p, tail = s.p - s.v * s.streak;
            V3    along = head - tail;
            if (along.len() < 1e-4) along = {0, 1e-4, 0};
            V3 toCam = head - cam.pos;
            V3 cr{along.y * toCam.z - along.z * toCam.y, along.z * toCam.x - along.x * toCam.z, along.x * toCam.y - along.y * toCam.x};
            if (cr.len() < 1e-9) continue;
            V3   across = cr.norm() * s.size, ext = along.norm() * (s.size * 0.8);
            V3   h = head + ext, t = tail - ext;
            V3   p4[4] = {t + across, h + across, t - across, h - across};
            Quad q;
            if (!worldQuad4(cam, b, p4, q)) continue;
            setRect(q.uv, 0, 0, 1, 1);
            q.color[0] = s.r * k; q.color[1] = s.g * k * k; q.color[2] = s.bl * k * k * k; q.color[3] = 1;
            q.mode[0] = 5;
            gQuads.push_back(q);
        } else {
            float fade    = float(std::sin(3.14159265 * s.age / s.life));
            float twinkle = 0.75f + 0.25f * float(std::sin(s.age * 23 + s.phase));
            float k       = fade * twinkle;
            glowBillboard(cam, b, s.p, s.size * (0.6 + 0.4 * fade), s.r * k, s.g * k, s.bl * k);
        }
    }
}
void glowDisc(JPose const& cam, Basis const& b, V3 at, double radius, float r, float g, float bl) {
    Quad q;
    if (!worldQuad(cam, b, at, V3{radius, 0, 0}, V3{0, 0, radius}, q)) return;
    setRect(q.uv, 0, 0, 1, 1);
    q.color[0] = r; q.color[1] = g; q.color[2] = bl; q.color[3] = 1;
    q.mode[0] = 5;
    gQuads.push_back(q);
}

// ---- EnchantFx: the rune circle, light textures of the atlas, the shaft of light, the table's glyphs
void atlasRect(float uv[8], float const r[4]) { setRect(uv, r[0] / PROP_W, r[1] / PROP_H, (r[0] + r[2]) / PROP_W, (r[1] + r[3]) / PROP_H); }
/// a square turned `angle` round its centre in the plane of ax / ay (unit axes): Java's corners i = 0..3 (uv 00 10 11 01)
bool turnedSquare(JPose const& cam, Basis const& b, V3 c, V3 ax, V3 ay, double radius, double angle, Quad& q) {
    V3 p[4];
    for (int i = 0; i < 4; ++i) {
        double a = angle + kPi / 4 + i * kPi / 2, d = radius * std::sqrt(2.0);
        p[i] = c + ax * (std::cos(a) * d) + ay * (std::sin(a) * d);
    }
    V3 tl[4] = {p[0], p[1], p[3], p[2]}; // TL TR BL BR
    return worldQuad4(cam, b, tl, q);
}
// What stands in front of a quad in the world (we draw over the finished frame, without its depth): a convex
// polygon on screen the quad is cut away inside. The enchanting table's open book over the circles.
int   gOcclN = 0;
float gOccl[8][2];
void setOccluder(JPose const& cam, Basis const& b, V3 const* pts, int n) {
    std::vector<std::pair<float, float>> p;
    for (int i = 0; i < n; ++i) {
        float x, y;
        if (!toPixel(cam, b, pts[i], gW, gH, x, y)) { gOcclN = 0; return; }
        p.push_back({x, y});
    }
    std::sort(p.begin(), p.end());
    std::vector<std::pair<float, float>> h(2 * p.size());
    auto cross = [](auto o, auto a, auto c) { return (a.first - o.first) * (c.second - o.second) - (a.second - o.second) * (c.first - o.first); };
    size_t k = 0;
    for (size_t i = 0; i < p.size(); ++i) { while (k >= 2 && cross(h[k - 2], h[k - 1], p[i]) <= 0) --k; h[k++] = p[i]; }
    for (size_t i = p.size() - 1, t = k + 1; i-- > 0;) { while (k >= t && cross(h[k - 2], h[k - 1], p[i]) <= 0) --k; h[k++] = p[i]; }
    k = k > 0 ? k - 1 : 0;
    if (k > 8) k = 8; // a box seen from anywhere has at most 6 corners on its outline
    gOcclN = int(k);
    for (size_t i = 0; i < k; ++i) { gOccl[i][0] = h[i].first; gOccl[i][1] = h[i].second; }
}
void applyOccluder(Quad& q) {
    q.occl[0] = float(gOcclN);
    std::memcpy(q.hull, gOccl, sizeof q.hull);
}
float clampC(float v) { return std::clamp(v, 0.0f, 1.0f); } // EnchantFx.clamp: a vertex colour byte
/// EnchantFx.disc / billboard: a light texture of the atlas (Fx.glow: added light) as a turned square
void fxLight(JPose const& cam, Basis const& b, float const tex[4], V3 c, V3 ax, V3 ay, double radius, double angle, float r, float g, float bl, bool occluded) {
    Quad q;
    if (!turnedSquare(cam, b, c, ax, ay, radius, angle, q)) return;
    atlasRect(q.uv, tex);
    q.mode[0] = 7;
    q.color[0] = clampC(r); q.color[1] = clampC(g); q.color[2] = clampC(bl); q.color[3] = 1;
    if (occluded) applyOccluder(q);
    gQuads.push_back(q);
}
/// EnchantFx.circle: the magic circle lying on a surface
void fxCircle(JPose const& cam, Basis const& b, V3 center, double radius, double angle, float r, float g, float bl) {
    fxLight(cam, b, FX_MAGIC, center, V3{1, 0, 0}, V3{0, 0, 1}, radius, angle, r, g, bl, true);
}
/// EnchantFx.billboard(CIRCLE): facing the camera (camera left / up as Java's)
void fxCircleBillboard(JPose const& cam, Basis const& b, V3 center, double radius, double angle, float r, float g, float bl) {
    fxLight(cam, b, FX_MAGIC, center, b.r * -1.0, b.u, radius, angle, r, g, bl, false);
}
/// EnchantFx.billboard(GLOW): the soft glow texture facing the camera, added light
void fxGlow(JPose const& cam, Basis const& b, V3 center, double radius, double angle, float r, float g, float bl) {
    fxLight(cam, b, FX_GLOW, center, b.r * -1.0, b.u, radius, angle, r, g, bl, false);
}
/// EnchantFx.lapisGhost: the empty socket's crystal, a pentagon of light lines facing the camera
void lapisGhost(JPose const& cam, Basis const& b, V3 at, float brightness, float scale) {
    static const double gem[5][2] = {{0, 0.052}, {0.035, 0.018}, {0.023, -0.032}, {-0.023, -0.032}, {-0.035, 0.018}};
    auto line = [&](double x0, double y0, double x1, double y1, float k) {
        V3 l = b.r * -1.0, u = b.u;
        V3 a = at + (l * x0 + u * y0) * scale, c = at + (l * x1 + u * y1) * scale;
        V3 d = c - a;
        if (d.len() < 1e-6) return;
        V3 n = (l * -(y1 - y0) + u * (x1 - x0)).norm() * (0.0025 * scale);
        V3 p4[4] = {a + n, c + n, a - n, c - n};
        Quad q;
        if (!worldQuad4(cam, b, p4, q)) return;
        // the ribbons fade to nothing at their edges (alpha 190 -> 0, added light): about half their light on average
        float s = k * 190 / 255.0f * 0.5f;
        q.mode[1] = 1;
        q.color[0] = clampC(0.25f * s); q.color[1] = clampC(0.62f * s); q.color[2] = clampC(s); q.color[3] = 1;
        gQuads.push_back(q);
    };
    for (int i = 0; i < 5; ++i) line(gem[i][0], gem[i][1], gem[(i + 1) % 5][0], gem[(i + 1) % 5][1], brightness);
    line(-0.035, 0.018, 0.035, 0.018, brightness * 0.65f);
    line(0, 0.052, -0.023, -0.032, brightness * 0.65f);
    line(0, 0.052, 0.023, -0.032, brightness * 0.65f);
}
/// EnchantFx.beam: two crossed planes rising from `base`, bright at the foot
void fxBeam(JPose const& cam, Basis const& b, V3 base, double height, double width, double spin, float r, float g, float bl) {
    for (int plane = 0; plane < 2; ++plane) {
        double a  = spin + plane * kPi / 2;
        V3     dh = V3{std::cos(a), 0, std::sin(a)} * (width / 2);
        V3     p4[4] = {base - dh + V3{0, height, 0}, base + dh + V3{0, height, 0}, base - dh, base + dh};
        Quad   q;
        if (!worldQuad4(cam, b, p4, q)) continue;
        atlasRect(q.uv, FX_BEAM);
        q.mode[0] = 7;
        q.color[0] = r; q.color[1] = g; q.color[2] = bl; q.color[3] = 1;
        gQuads.push_back(q);
    }
}
// vanilla's EnchantmentTableParticle (EnchantFx.stream): spawned at `to` with an offset, it starts at to + offset and
// converges on to, falling 1.2 at the very end; a glyph of light (its colour 0.9f, 0.9f, f)
struct Glyph { V3 to, d; double age, life; float size, f; int sprite; bool portal = false; };
std::vector<Glyph> gGlyphs;
void glyphStream(V3 from, V3 to, int count) {
    for (int i = 0; i < count && gGlyphs.size() < 600; ++i) {
        V3 d = from - to + V3{(rnd() - 0.5) * 0.5, (rnd() - 0.5) * 0.4, (rnd() - 0.5) * 0.5};
        gGlyphs.push_back({to, d, 0, (30 + std::floor(rnd() * 10)) / 20.0, float(0.1 * (rnd() * 0.5 + 0.2)), float(rnd() * 0.6 + 0.4), int(rnd() * 26) % 26});
    }
}
void drawGlyphs(JPose const& cam, Basis const& b, double dt) {
    for (size_t i = gGlyphs.size(); i-- > 0;) {
        Glyph& g = gGlyphs[i];
        g.age += dt;
        if (g.age >= g.life) { gGlyphs[i] = gGlyphs.back(); gGlyphs.pop_back(); continue; }
        double a = g.age / g.life, f = 1 - a, f1 = a * a * a * a;
        if (g.portal) { // per tick: position += speed * age / lifetime (the speed is per tick); quad shrinks
            double ticks = g.age * 20, life = g.life * 20;
            V3     p = g.to + g.d * (ticks * (ticks + 1) / 2 / life);
            double sz = g.size * (1 - ticks / (life * 1.5));
            float  k  = 1;
            Quad   q;
            if (!worldQuad(cam, b, p, b.r * sz, b.u * sz, q)) continue;
            if (gPropHas[7]) {
                float u0 = float(FX_GLYPHS_X + (16 + g.sprite) * 8 % 128), v0 = float(FX_GLYPHS_Y + 16); // generic_0..7, copied below the glyphs
                setRect(q.uv, u0 / PROP_W, v0 / PROP_H, (u0 + 8) / PROP_W, (v0 + 8) / PROP_H);
                q.mode[0] = 3;
            } else { setRect(q.uv, 0, 0, 1, 1); q.mode[0] = 5; }
            q.color[0] = 0.9f * g.f * k; q.color[1] = 0.3f * g.f * k; q.color[2] = g.f * k; q.color[3] = 1;
            gQuads.push_back(q);
            continue;
        }
        V3     p = g.to + g.d * f - V3{0, f1 * 1.2, 0};
        // getLightColor: lit up towards the end (a^4 of full bright over the world's light; the night here)
        float k = 1; // getLightColor: the world's light brightened towards the end; at a table in daylight that is full
        Quad  q;
        // SingleQuadParticle: a camera-facing square of +-quadSize, the glyph sprite, opaque (PARTICLE_SHEET_OPAQUE)
        if (!worldQuad(cam, b, p, b.r * g.size, b.u * g.size, q)) continue;
        if (gPropHas[7]) {
            float u0 = float(FX_GLYPHS_X + (g.sprite % 16) * 8), v0 = float(FX_GLYPHS_Y + (g.sprite / 16) * 8);
            setRect(q.uv, u0 / PROP_W, v0 / PROP_H, (u0 + 8) / PROP_W, (v0 + 8) / PROP_H);
            q.mode[0] = 3;
            q.color[0] = 0.9f * g.f * k; q.color[1] = 0.9f * g.f * k; q.color[2] = g.f * k; q.color[3] = 1;
        } else {
            setRect(q.uv, 0, 0, 1, 1);
            q.mode[0] = 5;
            q.color[0] = 0.9f * g.f * k; q.color[1] = 0.9f * g.f * k; q.color[2] = g.f * k; q.color[3] = 1;
        }
        gQuads.push_back(q);
    }
}
// EnchantFx.spark & co: soft glowing motes in the violet and blue of the enchanting table
const float E_PALETTE[3][3] = {{0.75f, 0.45f, 1.0f}, {0.45f, 0.8f, 1.0f}, {1.0f, 0.92f, 1.0f}};
void eSpark(V3 at, V3 v, double life, float size) {
    float const* c = E_PALETTE[int(rnd() * 3) % 3];
    spark(at, v, life, size, c[0], c[1], c[2], -0.04, 0);
}
void eBurst(V3 at, double strength) { // the moment of enchantment: motes flung out, glyphs pulled in
    for (int i = 0; i < int(70 * strength); ++i) {
        V3 dir = V3{gauss(), gauss() * 0.7 + 0.25, gauss()}.norm();
        eSpark(at, dir * (0.35 + rnd() * 1.0), 0.6 + rnd() * 0.8, float(0.018 + rnd() * 0.03));
    }
    for (int i = 0; i < int(8 * strength) && gGlyphs.size() < 600; ++i) { // REVERSE_PORTAL: a little purple haze
        // ReversePortalParticle: PortalParticle (quadSize 0.1 (0.5..0.7), colour f (0.9, 0.3, 1), a generic sprite)
        // x 1.5, 60..61 ticks, moving by its speed x age / lifetime each tick, shrinking to 1/3
        Glyph g{at + V3{(rnd() - 0.5) * 0.3, (rnd() - 0.5) * 0.3, (rnd() - 0.5) * 0.3}, V3{(rnd() - 0.5) * 0.05, rnd() * 0.04, (rnd() - 0.5) * 0.05},
                0, (60 + std::floor(rnd() * 2)) / 20.0, float(0.1 * (rnd() * 0.2 + 0.5) * 1.5), float(rnd() * 0.6 + 0.4), int(rnd() * 8) % 8, true};
        gGlyphs.push_back(g);
    }
    for (int i = 0; i < int(30 * strength); ++i) glyphStream(at + V3{gauss(), gauss(), gauss()}.norm() * (1.1 + rnd() * 0.6), at, 1);
}
void eSparkle(V3 at, double radius) {
    double a = rnd() * kPi * 2, r = radius * (0.6 + rnd() * 0.4);
    eSpark(at + V3{std::cos(a) * r, (rnd() - 0.5) * 0.2, std::sin(a) * r}, V3{0, 0.05 + rnd() * 0.05, 0}, 1.0 + rnd() * 0.6, float(0.012 + rnd() * 0.012));
}
void eGather(V3 at, double radius) {
    V3     dir  = V3{gauss(), gauss(), gauss()}.norm();
    double life = 0.45 + rnd() * 0.2;
    eSpark(at + dir * radius, dir * (-radius / life * 1.6), life, float(0.014 + rnd() * 0.012));
}
V3 glyphSource() { // EnchantStation.source: a bookshelf, or a point in the air round the table when there are none
    auto const& sh = gS.st.shelves;
    if (!sh.empty()) return sh[size_t(rnd() * sh.size()) % sh.size()] + V3{0, 0.3, 0};
    double a = rnd() * kPi * 2;
    return gS.st.top + V3{std::cos(a) * 1.6, 0.6 + rnd() * 0.8, std::sin(a) * 1.6};
}
} // namespace

bool record(ID3D12Device* device, ID3D12GraphicsCommandList* list, ID3D12Resource* ui, bool uiFresh, unsigned W, unsigned H,
            D3D12_CPU_DESCRIPTOR_HANDLE const& bbRtv) {
    if (gG.broken) return false;
    ++gG.frameNo;
    Phase phase;
    {
        std::lock_guard l(gM);
        phase = gS.phase;
        gW = int(W);
        gH = int(H);
    }
    if (phase == Phase::None) { gG.keepValid = false; return false; }
    if (gDumpDir.empty()) {
        wchar_t path[MAX_PATH]{};
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&gDumpDir), &self);
        if (GetModuleFileNameW(self, path, MAX_PATH)) gDumpDir = std::filesystem::path(path).parent_path() / "logs" / "uicapture";
    }
    D3D12_RESOURCE_DESC ud = ui ? ui->GetDesc() : D3D12_RESOURCE_DESC{};
    if (!createPipeline(device, ud.Format ? ud.Format : DXGI_FORMAT_R8G8B8A8_UNORM)) return false;
    if (ui && !ensureKeep(device, ud)) return false;
    if (!uploadSkin(device, list)) return false;
    if (!uploadProp(device, list)) return false;
    // a move began: keep what lay on the block (its slots empty on the move's beat)
    if (gSnapRequest.exchange(false) && gG.keep && gG.snap && gG.keepValid) {
        barrier(list, gG.snap.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        barrier(list, gG.keep.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyResource(gG.snap.Get(), gG.keep.Get());
        barrier(list, gG.keep.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        barrier(list, gG.snap.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        gG.snapValid = true;
    }
    // the hidden screen: the game thread keeps each whole frame of it (beforeUiClear); until it has, this frame's
    bool fresh = gKeepFresh.exchange(false);
    if (ui && uiFresh && !gG.keepValid) {
        barrier(list, gG.keep.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        barrier(list, ui, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyResource(gG.keep.Get(), ui);
        barrier(list, ui, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        barrier(list, gG.keep.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        gG.keepValid = true;
        fresh        = true;
    }
    if (fresh && gG.keep && gG.cand && gG.rtvHeap && !(ui && uiFresh && gG.keepValid == false) && ensureCopyPso(device, gG.kfmt)) {
        // cand -> keep, only when the probes say the screen is in it (else keep stays as it was)
        Layout P;
        {
            std::lock_guard l(gLayoutMutex);
            P = gLayout;
        }
        Quad q;
        pixelQuad(0, 0, float(gG.kw), float(gG.kh), int(gG.kw), int(gG.kh), q);
        q.mode[0] = 6;
        q.mode[1] = P.valid && P.hasProbe && P.W == int(gG.kw) ? 1.0f : 0.0f;
        for (int i = 0; i < 4; ++i) { q.key0[i] = P.probe[0][i]; q.key1[i] = P.probe[1][i]; }
        for (int i = 0; i < 3; ++i) { q.color[i] = P.probeLine[i]; q.uv[i] = P.probeAbove[i]; }
        barrier(list, gG.keep.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        ID3D12DescriptorHeap* heaps[] = {gG.srvHeap.Get()};
        list->SetDescriptorHeaps(1, heaps);
        list->SetGraphicsRootSignature(gG.root.Get());
        list->SetPipelineState(gG.psoCopy.Get());
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        D3D12_VIEWPORT vp{0, 0, float(gG.kw), float(gG.kh), 0, 1};
        D3D12_RECT     sc{0, 0, LONG(gG.kw), LONG(gG.kh)};
        auto           rtv = gG.rtvHeap->GetCPUDescriptorHandleForHeapStart();
        list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        list->RSSetViewports(1, &vp);
        list->RSSetScissorRects(1, &sc);
        list->SetGraphicsRootDescriptorTable(1, gG.srvHeap->GetGPUDescriptorHandleForHeapStart());
        list->SetGraphicsRoot32BitConstants(0, 60, &q, 0);
        list->DrawInstanced(6, 1, 0, 0);
        barrier(list, gG.keep.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        list->OMSetRenderTargets(1, &bbRtv, FALSE, nullptr); // the caller's target again
        ++gKeepChecked;
    }
    if (fresh && gG.keep) {
        ui = gG.keep.Get(); // read the slots from the kept image (PIXEL_SHADER_RESOURCE, as `ui` was)
        pollReadback();
        ULONGLONG now = GetTickCount64();
        bool      haveLayout;
        {
            std::lock_guard l(gLayoutMutex);
            haveLayout = gLayout.valid && gLayout.W == int(W) && gLayout.kind == int(gS.st.kind);
        }
        // find the slots ~0.15 s after the screen opened, then again every second (layout can change: recipe book)
        if (now - gG.lastDetect > (haveLayout ? 1000u : 150u)) {
            gG.lastDetect = now;
            requestReadback(list, ui);
        }
        Layout L;
        {
            std::lock_guard l(gLayoutMutex);
            L = gLayout;
        }
        sampleSlots(device, list, ui, L);
    }
    Layout L;
    {
        std::lock_guard l(gLayoutMutex);
        L = gLayout;
    }
    // ---- build the quads (SceneScreen + TableStation + Overlay)
    gQuads.clear();
    {
        std::lock_guard l(gM);
        if (gS.phase == Phase::None) return false;
        bool haveCam = gS.haveLast && gS.projKnown;
        JPose cam = gS.last;
        Basis b   = basisOf(cam);
        double clock = gS.clock;
        bool layoutOk = L.valid && L.W == int(W) && gG.keepValid && L.kind == int(gS.st.kind);
        // ---- item tooltips (a probe): the hidden cursor rests on the hovered item's slot, the slot detection finds the
        // window's hover text, it is copied aside (t5) and shown in the scene's tooltip from then on
        if (layoutOk && gS.phase == Phase::Active) {
            auto sigOf = [&](int sl, float out[7]) {
                for (int k = 0; k < 7; ++k) out[k] = 0;
                if (sl < 9) { std::memcpy(out, gS.iconRgb[sl], 3 * sizeof(float)); std::memcpy(out + 3, gS.iconBox[sl], 4 * sizeof(float)); }
            };
            auto fresh = [&](int sl) {
                auto const& c = gS.tipCache[sl];
                if (!c.ok && !c.tried) return false;
                if (sl >= 9) return clock - c.at < 4; // the drawer: no samples of its own, looked at again now and then
                float sg[7];
                sigOf(sl, sg);
                for (int k = 0; k < 7; ++k) if (std::fabs(sg[k] - c.sig[k]) > 0.02f) return false;
                return true;
            };
            if (gS.probe < 0 && gS.tipSlot >= 0 && !moving() && clock - gS.tipSince > 0.12 && !fresh(gS.tipSlot) && gPress.state == PressState::Idle) {
                Rect r;
                if (targetRect(L, gS.tipSlot, r)) {
                    gS.probe = gS.tipSlot; gS.probeAt = clock; gS.probeForced = false;
                    sigOf(gS.probe, gS.tipCache[gS.probe].sig);
                    gProbeX = r.cx(); gProbeY = r.cy(); gProbeUnit = std::max(1.0f, L.slot / 18.0f); gProbeSlot = gS.probe;
                    std::lock_guard tl(gTipM);
                    gTipFoundSlot = -1;
                }
            }
            if (gS.probe >= 0) {
                if (!gS.probeForced && clock - gS.probeAt > 0.1) { gS.probeForced = true; gG.lastDetect = 0; }
                Rect found;
                bool got = false;
                {
                    std::lock_guard tl(gTipM);
                    if (gTipFoundSlot == gS.probe) { found = gTipFoundRect; got = true; gTipFoundSlot = -1; }
                }
                auto& c = gS.tipCache[gS.probe];
                if (got && gG.tip && gG.keep) { // copy the writing while the window still shows it
                    D3D12_BOX bx{UINT(std::max(0.0f, found.x)), UINT(std::max(0.0f, found.y)), 0, UINT(std::min(float(gG.kw), found.x + found.w)),
                                 UINT(std::min(float(gG.kh), found.y + found.h)), 1};
                    if (bx.right > bx.left && bx.bottom > bx.top) {
                        barrier(list, gG.tip.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
                        barrier(list, gG.keep.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
                        D3D12_TEXTURE_COPY_LOCATION dst{gG.tip.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
                        D3D12_TEXTURE_COPY_LOCATION srcl{gG.keep.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
                        list->CopyTextureRegion(&dst, bx.left, bx.top, 0, &srcl, &bx);
                        barrier(list, gG.keep.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                        barrier(list, gG.tip.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                        for (auto& o : gS.tipCache) // one copy texture: what this one covers is gone for the others
                            if (o.ok && &o != &c && o.text.x < found.x + found.w && found.x < o.text.x + o.text.w && o.text.y < found.y + found.h && found.y < o.text.y + o.text.h) { o.ok = false; o.tried = false; }
                        c.ok = true; c.tried = true; c.at = clock; c.text = found;
                    }
                    gS.probe = -1; gProbeSlot = -1; gS.holdUntil = clock + 0.3;
                } else if (clock - gS.probeAt > 1.2 || gS.tipSlot != gS.probe) { // nothing shown (an empty slot), or the cursor left
                    if (gS.tipSlot == gS.probe) { c.ok = false; c.tried = true; c.at = clock; }
                    gS.probe = -1; gProbeSlot = -1; gS.holdUntil = clock + 0.3;
                }
            }
        }
        float tableAlpha = gS.phase == Phase::Active ? float(smoother(gS.time / 0.4)) : gS.phase == Phase::Outro ? float(1 - smoother(gS.time / 0.3)) : float(smoother(gS.time / gS.leanLength));
        auto src = [&](Rect r, int sampled) {
            float   in = L.slot * 16.0f / 18.0f;
            IconSrc o{{r.cx() - in * 0.5f, r.cy() - in * 0.5f, in, in}, {0, 0, 1, 1}};
            if (sampled >= 0) std::memcpy(o.box, gS.iconBox[sampled], sizeof o.box);
            if (sampled >= 0 && sampled < 9 && gS.hasCount[sampled]) { o.count = true; std::memcpy(o.countBox, gS.countBox[sampled], sizeof o.countBox); }
            return o;
        };
        // the hands (Hands.update), every phase
        static double lastHands = -1;
        double handsDt = lastHands < 0 ? 0 : std::clamp(clock - lastHands, 0.0, 0.1);
        lastHands = clock;
        if (haveCam) updateHands(cam, handsDt);
        if (haveCam && layoutOk && L.hasGrid && isFurnace(gS.st.kind)) {
            // FurnaceStation.renderWorld + FurnaceRenderer: the fire's light in the openings, the hovered zone's glow,
            // and what lies in each opening (beds: flat on the floor of the opening, charged from the top half-sunk)
            FLayout const& fl  = furnaceLayout(gS.st.kind);
            V3             rgt = gS.st.right(), out = gS.st.sideV();
            auto faceQuad = [&](bool onTop, double u0, double u1, double v0, double v1, double o, float r, float g, float bl, float a, bool add) {
                V3   p4[4] = {gS.st.fpoint(onTop, u0, v1, o), gS.st.fpoint(onTop, u1, v1, o), gS.st.fpoint(onTop, u0, v0, o), gS.st.fpoint(onTop, u1, v0, o)};
                Quad q;
                if (!worldQuad4(cam, b, p4, q)) return;
                q.color[0] = r; q.color[1] = g; q.color[2] = bl; q.color[3] = a;
                q.mode[1] = add ? 1.0f : 0.0f;
                gQuads.push_back(q);
            };
            if (gS.st.lit) {
                double t       = clock;
                double flicker = 0.82 + 0.1 * std::sin(t * 11.3) + 0.08 * std::sin(t * 23.1 + 1.3);
                for (auto const& g : fl.glow) {
                    float k = float(g.fire ? 0.55 * flicker : 0.22 * (0.9 + 0.1 * std::sin(t * 9.0)));
                    faceQuad(g.top, g.u0, g.u1, g.v0, g.v1, 0.003, 1.0f, g.fire ? 0.55f : 0.66f, g.fire ? 0.18f : 0.3f, k * tableAlpha, true);
                }
            }
            if (gS.hoverCell >= 0 && gS.phase == Phase::Active) {
                FZone const& z = fl.zone[gS.hoverCell];
                faceQuad(z.top, z.hl[0], z.hl[1], z.hl[2], z.hl[3], 0.004, 1.0f, 236 / 255.0f, 200 / 255.0f, 44 / 255.0f, false);
            }
            for (int slot = 0; slot < 3; ++slot) {
                if (!gS.cellFull[slot]) continue;
                FBed const& bd = fl.bed[slot];
                double      u  = (bd.u0 + bd.u1) / 2;
                V3 base = bd.top ? gS.st.fpoint(true, u, bd.floor, -bd.depth) : gS.st.fpoint(false, u, bd.floor + 0.002, -bd.depth);
                placeItem(cam, b, L, src(L.grid[slot], slot), gS.cellBlock[slot], base, rgt, out, 0.205 * 0.95, 0.15 * 0.95, bd.tilt, tableAlpha);
            }
        }
        if (haveCam && layoutOk && L.hasGrid && gS.st.kind == Kind::Anvil) {
            // AnvilStation.renderScene / renderWorld
            V3     rgt = gS.st.right(), out = gS.st.sideV();
            double t   = moveTime();
            constexpr double MAIN_SIZE = 0.4, ADDITION_SIZE = 0.3, MAIN_TURN = 45;
            auto pile = [&](bool mainFull, bool mainBlock, bool addFull, bool addBlock, IconSrc ms, IconSrc as, double additionScale) {
                if (mainFull) {
                    double size = mainBlock ? 0.2 : MAIN_SIZE;
                    placeItem(cam, b, L, ms, mainBlock, anvilMain(), rgt, out, size, size, 0, tableAlpha, MAIN_TURN);
                }
                if (addFull && additionScale > 0.01) {
                    double size = (addBlock ? 0.16 : ADDITION_SIZE) * (0.35 + 0.65 * additionScale);
                    V3     home = mainFull ? anvilAdd() : anvilMain();
                    V3     at   = lerp(anvilMain(), home, additionScale) + V3{0, 0.002 - 0.01 * (1 - additionScale), 0};
                    placeItem(cam, b, L, as, addBlock, at, rgt, out, size, size, 0, tableAlpha, MAIN_TURN + 70);
                }
            };
            if (t < 0) {
                if (gS.phase == Phase::Active && gS.anvilTarget == ASurface && gS.haveAim) {
                    float p = float(0.16 + 0.06 * std::sin(clock * 5)); // the spot it would go to glows (EnchantFx.disc)
                    glowDisc(cam, b, gS.aim + V3{0, 0.004, 0}, 0.14, p, p * 0.75f, p * 0.45f);
                }
                pile(gS.cellFull[0], gS.cellBlock[0], gS.cellFull[1], gS.cellBlock[1], src(L.grid[0], 0), src(L.grid[1], 1), 1);
            } else {
                // what lay there when the move began (the snapshot): the top piece sinks in with every blow
                IconSrc m0 = src(L.grid[0], -1), a0 = src(L.grid[1], -1);
                std::memcpy(m0.box, gS.snapBox[0], sizeof m0.box);
                std::memcpy(a0.box, gS.snapBox[1], sizeof a0.box);
                m0.snap = a0.snap = gG.snapValid;
                if (t < BLOWS[2] + 0.04) {
                    double sunk = 0;
                    for (double bl : BLOWS) sunk += smoother(window(t, bl, bl + 0.07)) / 3;
                    pile(gS.mainWasFull, gS.mainWasBlock, gS.addWasFull, gS.addWasBlock, m0, a0, 1 - sunk);
                    double heat = sunk + 0.25 * std::exp(-sinceBlow(t) * 10);
                    if (gS.addWasFull && heat > 0.02) glowBillboard(cam, b, anvilPoint() + V3{0, 0.03, 0}, 0.16 + 0.05 * heat, float(0.22 * heat), float(0.6 * 0.22 * heat), float(0.25 * 0.22 * heat));
                } else {
                    // the new item rises off the anvil, turned over, then goes to the hand (it is in the cursor now)
                    V3 lifted = anvilMain() + V3{0, 0.13, 0};
                    V3 at     = lerp(anvilMain(), lifted, inOutCubic(window(t, BLOWS[2] + 0.18, PUT_AWAY + 0.08)));
                    V3 held   = heldItemPoint(gRight.x, shoulderOf(true, cam.pos, cam.yaw));
                    at        = lerp(at, held, inOutCubic(window(t, PUT_AWAY + 0.12, FORGE_LENGTH)));
                    double size = MAIN_SIZE + (0.205 - MAIN_SIZE) * window(t, PUT_AWAY + 0.1, FORGE_LENGTH);
                    double turn = 200 * inOutCubic(window(t, BLOWS[2] + 0.1, PUT_AWAY + 0.1));
                    float  tilt = float(35 * hump(window(t, BLOWS[2] + 0.1, FORGE_LENGTH)));
                    float  nx2, ny2;
                    neutralPoint(L, nx2, ny2);
                    float   sz = L.slot * 16.0f / 18.0f;
                    IconSrc cs{{nx2 - sz * 0.5f, ny2 - sz * 0.5f, sz, sz}, {0, 0, 1, 1}};
                    std::memcpy(cs.box, gS.iconBox[10], sizeof cs.box);
                    if (gS.carrying) placeItem(cam, b, L, cs, gS.carriedBlock, at, rgt, out, size, std::min(size, 0.2), tilt, 1, MAIN_TURN + turn);
                    double glow = 1 - window(t, BLOWS[2], PUT_AWAY + 0.2);
                    if (glow > 0.01) glowBillboard(cam, b, at, 0.2, float(0.4 * glow), float(0.32 * glow), float(0.2 * glow));
                }
                // blows: sparks off the anvil, and after the last the new item's glow
                while (gS.blowsDone < 3 && t >= BLOWS[gS.blowsDone]) {
                    // AnvilStation.blow
                    V3 sp = anvilPoint() + V3{0, 0.03, 0};
                    blowSparks(sp, 34, 2.4);
                    hotSparks(sp, out * -1.0 + V3{0, 0.7, 0}, 0.5, 12, 2.6);
                    if (gS.blowsDone == 2) motes(anvilMain() + V3{0, 0.05, 0}, 0.1, 10, 1.0f, 0.75f, 0.4f, 0.2);
                    ++gS.blowsDone;
                }
                Swing sw;
                if (hammerAt(t, cam, sw)) drawHammer(cam, b, gRight.x, handleDirection(sw.angle), headAxis(sw.angle));
            }
        }
        if (haveCam && layoutOk && L.hasGrid && gS.st.kind == Kind::Grindstone) {
            // GrindstoneStation.frame / renderScene / renderWorld
            V3     rgt = gS.st.right(), out = gS.st.sideV();
            double t   = moveTime();
            bool   driven = t >= 0 && t < G_LET_GO + 0.1;
            gS.spin = driven ? approach(gS.spin, 1, 5, handsDt) : approach(gS.spin, 0, 1.3, handsDt);
            gS.run -= gS.spin * 70 * handsDt; // the top runs away from the player
            if (t >= 0 && t >= G_CONTACT && t < G_LET_GO && handsDt > 0) { // grindFx
                int count = int(std::lround(handsDt * 160 + rnd()));
                V3  lean  = out * -0.55;
                fountain(grindBite(0, t), lean, rgt, count, 2.2);
                if (gS.addWasFull && t < G_CLEAN_AT) fountain(grindBite(1, t), lean, rgt, count, 2.2);
                if (gS.stripping && t < G_CLEAN_AT + 0.1)
                    flakes(grindHeldPos(0, t) + grindBlade(0) * 0.04 + V3{0, 0.02, 0}, 0.06, int(std::lround(handsDt * 34 + rnd() * 0.6)));
                if (gS.stripping && gS.grindPrevT < G_CLEAN_AT && t >= G_CLEAN_AT) {
                    V3 at = grindHeldPos(0, t);
                    flakes(at, 0.1, 30);
                    motes(at, 0.06, 12, 0.85f, 0.55f, 1.0f, 0.2);
                }
            }
            gS.grindPrevT = t;
            // drawRim: the running faces of the wheel with their texture rolling round (the game's grindstone_round)
            if (gS.spin > 0.01 && gPropHas[0]) {
                double h = G_HALF + 0.0025;
                V3     up{0, 1, 0}, across = rgt * G_WIDTH, w = gS.st.wheel;
                V3     faces[4][3] = {{w + up * h - out * G_HALF, out, up},
                                      {w + out * h + up * G_HALF, up * -1.0, out},
                                      {w - up * h + out * G_HALF, out * -1.0, up * -1.0},
                                      {w - out * h - up * G_HALF, up, out * -1.0}};
                double offset = std::fmod(std::fmod(gS.run, 12.0) + 12.0, 12.0);
                auto rimQuad = [&](V3 start, V3 run, V3 normal, double from, double to, double v0, double v1) {
                    if (to - from < 1e-4) return;
                    V3   a = start + run * from, c = start + run * to;
                    V3   p4[4] = {a - across, a + across, c - across, c + across};
                    Quad q;
                    if (!worldQuad4(cam, b, p4, q)) return;
                    setRect(q.uv, 64.0f / PROP_W, float(v0 / PROP_H), 72.0f / PROP_W, float(v1 / PROP_H));
                    float sh = faceShade(normal);
                    q.color[0] = q.color[1] = q.color[2] = sh;
                    q.mode[0] = 3; q.mode[3] = tableAlpha;
                    gQuads.push_back(q);
                };
                for (auto& f : faces) {
                    V3 mid = f[0] + f[1] * G_HALF;
                    if (f[2].dot(cam.pos - mid) <= 0) continue; // turned away (no depth test here)
                    double split = (12 - offset) / 12 * (2 * G_HALF);
                    rimQuad(f[0], f[1], f[2], 0, split, offset, 12);
                    rimQuad(f[0], f[1], f[2], split, 2 * G_HALF, 0, offset);
                }
            }
            auto drawLying = [&](IconSrc const& is, bool block, V3 at, int slot, double scale) {
                double size   = (block ? 0.16 : G_ITEM_SIZE) * scale;
                V3     center = at + V3{0, block ? size / 2 : 0.004 + slot * 0.003, 0};
                orientedItem(cam, b, L, is, block, center, rgt, out, size, 0, tableAlpha, G_TOOL_TURN + (slot == 0 ? -8 : 8));
            };
            if (t < 0) {
                if (gS.phase == Phase::Active && gS.grindTarget == GPlace && gS.haveAim) {
                    float p = float(0.18 + 0.06 * std::sin(clock * 5));
                    glowDisc(cam, b, gS.aim + V3{0, 0.004, 0}, 0.12, p, p * 0.8f, p * 0.55f);
                }
                for (int i = 0; i < 2; ++i)
                    if (gS.cellFull[i]) drawLying(src(L.grid[i], i), gS.cellBlock[i], grindRest(i), i, i == gS.grindHover ? 1.08 : 1.0);
            } else {
                // what lay on the wheel when the move began, and the result (the snapshot)
                IconSrc first = src(L.grid[0], -1), second = src(L.grid[1], -1), after = src(L.grid[2], -1);
                std::memcpy(first.box, gS.snapBox[0], sizeof first.box);
                std::memcpy(second.box, gS.snapBox[1], sizeof second.box);
                std::memcpy(after.box, gS.snapBox[2], sizeof after.box);
                first.snap = second.snap = after.snap = gG.snapValid;
                if (t < G_LET_GO) { // drawGrinding
                    bool    clean = !gS.stripping || t >= G_CLEAN_AT;
                    bool    showAfter = clean || (gS.addWasFull && t >= G_CLEAN_AT);
                    V3      at = grindHeldPos(0, t);
                    orientedItem(cam, b, L, showAfter ? after : first, showAfter ? gS.resultWasBlock : gS.mainWasBlock, at, rgt, out, G_ITEM_SIZE,
                                 grindHeldTilt(t), 1, grindHeldYaw(0, t));
                    if (gS.stripping && t >= G_CONTACT - 0.1 && t < G_CLEAN_AT + 0.05) {
                        float g = float(0.35 * (1 - window(t, G_CONTACT, G_CLEAN_AT))); // the enchantment's glow draining out
                        glowBillboard(cam, b, at + V3{0, 0.02, 0}, 0.17, 0.65f * g, 0.35f * g, g);
                    }
                    if (gS.addWasFull && t < G_CLEAN_AT)
                        orientedItem(cam, b, L, second, gS.addWasBlock, grindHeldPos(1, t), rgt, out, G_ITEM_SIZE, grindHeldTilt(t), 1, grindHeldYaw(1, t));
                    else if (gS.addWasFull && t < G_CLEAN_AT + 0.25) {
                        double k = inCubic(window(t, G_CLEAN_AT, G_CLEAN_AT + 0.25));
                        orientedItem(cam, b, L, second, gS.addWasBlock, lerp(grindHeldPos(1, G_CLEAN_AT), grindHeldPos(0, t), k), rgt, out,
                                     G_ITEM_SIZE * (1 - k), grindHeldTilt(t), 1, grindHeldYaw(1, t));
                    }
                    if (t >= G_CONTACT) { // the bright line where steel meets stone
                        float bb = float(0.55 + 0.25 * std::sin(clock * 40));
                        glowBillboard(cam, b, grindBite(0, t) + V3{0, 0.008, 0}, 0.05, bb, bb * 0.6f, bb * 0.25f);
                    }
                } else { // lifted up to the eye: the clean steel catches the light, then it is held (or slips into the bag)
                    double k    = window(t, G_LET_GO, GRIND_LENGTH);
                    double size = gS.moveToInv ? G_ITEM_SIZE * (1 - 0.8 * inCubic(window(k, 0.4, 1))) : G_ITEM_SIZE + (0.205 - G_ITEM_SIZE) * window(k, 0.6, 1);
                    double upK  = inOutCubic(window(t, G_LET_GO, G_LET_GO + 0.3));
                    V3     at   = lerp(grindHeldPos(0, G_LET_GO), grindLifted(), upK);
                    if (gS.moveToInv) {
                        V3 drawer = cam.pos + V3{0, 0.55, 0} + yawForward(cam.yaw) * 0.6;
                        at        = lerp(at, drawer, inCubic(window(t, G_LET_GO + 0.22, GRIND_LENGTH)));
                    } else {
                        V3 held = heldItemPoint(gRight.x, shoulderOf(true, cam.pos, cam.yaw));
                        at      = lerp(at, held, inOutCubic(window(t, G_LET_GO + 0.3, GRIND_LENGTH)));
                    }
                    float  tilt0 = grindHeldTilt(G_LET_GO);
                    float  tilt  = float(tilt0 + (60 - tilt0) * hump(std::min(1.0, k * 1.25)) + 18 * window(k, 0.8, 1));
                    double from  = grindHeldYaw(0, G_LET_GO);
                    double turn  = from + (0 - from) * inOutCubic(window(k, 0.5, 1)) + 25 * std::sin(k * 3.14159265358979);
                    orientedItem(cam, b, L, after, gS.resultWasBlock, at, rgt, out, size, tilt, 1, turn);
                }
            }
        }
        if (haveCam && layoutOk && L.hasGrid && gS.st.kind == Kind::Stonecutter) {
            // StonecutterStation.frame / renderScene / renderWorld
            V3     rgt = gS.st.right(), out = gS.st.sideV();
            double t   = moveTime();
            gS.scroll  = approach(gS.scroll, 0, 10, handsDt);
            int nShapes = shapeCount();
            if (nShapes != gS.lastShapes) { gS.lastShapes = nShapes; gS.shapesAt = clock; if (nShapes == 0) gS.selectedShape = -1; }
            auto buttonSrc = [&](int button) {
                Rect    r  = L.buttons[button];
                float   in = std::min(r.w, L.slot) * 16.0f / 18.0f;
                IconSrc o{{r.cx() - in * 0.5f, r.cy() - in * 0.5f, in, in}, {0, 0, 1, 1}};
                std::memcpy(o.box, gS.iconBox[11 + button], sizeof o.box);
                return o;
            };
            if (t >= 0 && stoneCutting(t) && handsDt > 0) { // cutFx: dust and a few sparks where the blade bites
                V3 bite = gS.st.top + rgt * (stoneAlong(t) + S_PIECE * 0.3) + V3{0, S_PIECE * 0.85, 0};
                if (rnd() < handsDt * 45) fountain(bite, out * -0.3, out, 1, 1.6);
                float* c  = gS.iconRgb[0];
                float  cr = c[0] + c[1] + c[2] > 0.05f ? c[0] : 0x8A / 255.0f, cg = c[0] + c[1] + c[2] > 0.05f ? c[1] : 0x8A / 255.0f,
                      cb = c[0] + c[1] + c[2] > 0.05f ? c[2] : 0x8A / 255.0f;
                int count = int(std::lround(handsDt * (gS.cutAll ? 110 : 75) + rnd()));
                for (int i = 0; i < count; ++i) { // dust: chips of the stone in its own colour
                    V3    dir = (rgt * 0.5 + V3{gauss() * 0.5, 0.8 + rnd() * 0.7, gauss() * 0.5}).norm();
                    float k   = float(0.55 + rnd() * 0.45);
                    spark(bite + V3{gauss() * 0.015, 0, gauss() * 0.015}, dir * (0.6 + rnd() * 1.1), 0.3 + rnd() * 0.4, float(0.005 + rnd() * 0.007),
                          cr * k, cg * k, cb * k, 2.6, 0.012);
                }
            }
            // the stone on the table (a cube, or the item lying flat)
            bool showStack = gS.cellFull[0] && !(t >= 0 && gS.cutAll);
            if (showStack) {
                if (gS.cellBlock[0] && gCfg.cubeItems)
                    orientedItem(cam, b, L, src(L.grid[0], 0), true, stoneStack() + V3{0, S_PIECE / 2, 0}, rgt, out, S_PIECE, 0, tableAlpha, 180 + 6);
                else orientedItem(cam, b, L, src(L.grid[0], 0), false, stoneStack() + V3{0, 0.004, 0}, rgt, out, 0.205 * 1.1, 0, tableAlpha, 0);
            }
            if (gS.phase == Phase::Active && gS.stoneTarget == SPlace && gS.haveAim) {
                float p = float(0.16 + 0.06 * std::sin(clock * 5));
                glowDisc(cam, b, gS.aim + V3{0, 0.004, 0}, 0.13, p, p * 0.9f, p * 0.75f);
            }
            if (t >= 0) { // drawPiece
                double outT = stoneOutAt(gS.cutAll);
                double size = S_PIECE;
                if (t > outT) size *= gS.cutAll ? 1 - 0.85 * inCubic(window(t, outT, gS.cutLength)) : 1 - 0.3 * window(t, outT, gS.cutLength);
                V3 at = piecePath(std::min(t, outT));
                if (t >= outT) {
                    if (gS.cutAll) at = lerp(at, cam.pos + V3{0, 0.5, 0} + yawForward(cam.yaw) * 0.6, inCubic(window(t, outT, gS.cutLength)));
                    else {
                        V3     held = heldItemPoint(gRight.x, shoulderOf(true, cam.pos, cam.yaw));
                        double k    = inOutCubic(window(t, outT, gS.cutLength));
                        at          = lerp(at, held, k) + V3{0, 0.06 * hump(k), 0};
                    }
                }
                bool cut    = t >= S_BITE && stoneAlong(t) > 0;
                int  button = shapeButton(gS.cutShape);
                bool block  = cut ? gS.resultWasBlock : gS.mainWasBlock;
                IconSrc is  = cut && button >= 0 ? buttonSrc(button) : src(L.grid[0], 0);
                if (cut && button >= 0) gKeyOv = gS.btnRef[button];
                int copies = cut ? 1 : gS.cutAll ? 2 : 1;
                for (int i = 0; i < copies; ++i) {
                    V3 off = rgt * ((i - (copies - 1) / 2.0) * size * 0.75) + V3{0, i * 0.01, 0};
                    V3 c   = at + off + V3{0, block ? 0 : -size / 2 + 0.006, 0};
                    if (block) cardItem(cam, b, L, is, true, c, size, 1, 180);
                    else orientedItem(cam, b, L, is, false, c, rgt, out, size * 1.3, 0, 1, 0);
                }
                gKeyOv = nullptr;
                if (stoneCutting(t)) { // the bright bite of the blade in the stone
                    float bb = float(0.45 + 0.2 * std::sin(clock * 50));
                    glowBillboard(cam, b, gS.st.top + rgt * (stoneAlong(t) + S_PIECE * 0.3) + V3{0, S_PIECE * 0.8, 0}, 0.04, bb, bb * 0.8f, bb * 0.55f);
                }
            }
            // the dial: the shapes hang in an arc round the blade
            double appear = outBack(window(clock - gS.shapesAt, 0, 0.4), 1.4);
            double dim    = moving() ? 0.45 : 1;
            for (int k = 0; k < nShapes && gS.phase != Phase::Outro; ++k) {
                double on = onDial(k) * appear * tableAlpha;
                int    bt = shapeButton(k);
                if (on < 0.02 || bt < 0) continue;
                V3    at      = shapePoint(k);
                bool  hovered = k == gS.hoverShape, selected = k == gS.selectedShape;
                float g       = float((hovered ? 0.34 : 0.15) * on * dim);
                glowBillboard(cam, b, at, (hovered ? 0.095 : 0.075) * on, g, g * 0.95f, g * 0.85f);
                if (selected) {
                    float sl = float(0.4 * on * dim);
                    glowBillboard(cam, b, at, 0.085 * on, sl * 0.5f, sl * 0.42f, sl * 0.2f);
                }
                double size = S_SHAPE_SIZE * on * (hovered ? 1.35 : 1.0);
                double spin = clock * (hovered ? 90 : 25) + k * 40;
                gKeyOv      = gS.btnRef[bt];
                cardItem(cam, b, L, buttonSrc(bt), gS.btnBlock[bt], at, gS.btnBlock[bt] ? size * 0.85 : size * 1.2, 1, spin);
                gKeyOv = nullptr;
            }
        }
        if (haveCam && layoutOk && L.hasGrid && gS.st.kind == Kind::Smithing) {
            // SmithingStation.frame / renderScene / renderWorld
            V3     rgt = gS.st.right(), out = gS.st.sideV();
            double t   = moveTime();
            auto laid = [&](IconSrc const& is, bool block, int slot, double scale, float opacity) { // drawLaid
                double size = (block ? 0.13 : SM_SIZES[slot]) * scale;
                double yaw  = slot == 0 ? -12 : slot == 2 ? 18 : 0;
                orientedItem(cam, b, L, is, block, smithPoint(slot) + V3{0, block ? size / 2 : 0.004, 0}, rgt, out, size, 0, opacity, yaw);
            };
            if (t < 0) {
                for (int i = 0; i < 3; ++i)
                    if (gS.cellFull[i]) laid(src(L.grid[i], i), gS.cellBlock[i], i, i == gS.smithHover ? 1.06 : 1.0, tableAlpha);
                if (gS.phase == Phase::Active && gS.smithTarget == SmPlace) {
                    // where things go: the spot the carried item would take glows, the other empty ones show faintly
                    for (int i = 0; i < 3; ++i) {
                        bool dest = i == gS.smithPlace;
                        if (!dest && gS.cellFull[i]) continue;
                        float p = dest ? float(0.2 + 0.07 * std::sin(clock * 5)) : 0.05f;
                        glowDisc(cam, b, smithPoint(i) + V3{0, 0.003, 0}, SM_SIZES[i] * 0.55, p, p * 0.8f, p * 0.6f);
                    }
                }
            } else {
                // frame: the sparks, motes and steam of the moment
                V3 gear = smithPoint(1);
                float* m = gS.molten;
                if (gS.smithFx < 1 && t >= SM_PRESS) { gS.smithFx = 1; motes(gear + V3{0, 0.05, 0}, 0.08, 10, 0.6f, 0.85f, 1.0f, 0.1); }
                if (t >= SM_PRESS && t < SM_PEAK && handsDt > 0) {
                    int drops = int(std::lround(handsDt * 40 + rnd())); // molten metal trickles down the stack's sides
                    for (int i = 0; i < drops; ++i) {
                        double a = rnd() * 6.283185307179586;
                        float  k = float(0.9 + 0.1 * rnd());
                        spark(gear + V3{std::cos(a) * 0.1, 0.04, std::sin(a) * 0.1}, V3{std::cos(a) * 0.25, 0.1, std::sin(a) * 0.25}, 0.18 + rnd() * 0.08, 0.009f,
                              m[0] * k, m[1] * k, m[2] * k, 2.4, 0.04);
                    }
                    if (rnd() < handsDt * 24) motes(gear + V3{0, 0.05, 0}, 0.08, 1, 1.0f, 0.55f, 0.2f, 0.25); // embers
                } else if (t >= MATERIAL_DOWN && t < SM_PRESS && rnd() < handsDt * 14)
                    motes(gear + V3{0, 0.05, 0}, 0.07, 1, m[0], m[1] * 0.7f, m[2] * 0.4f, 0.15);
                if (gS.smithFx < 2 && t >= SM_PEAK) {
                    gS.smithFx = 2;
                    motes(gear + V3{0, 0.04, 0}, 0.1, 18, m[0], m[1], m[2], 0.25);
                    blowSparks(gear + V3{0, 0.03, 0}, 10, 1.4);
                }
                if (t >= SM_PEAK + 0.05 && t < SM_COOLED - 0.2 && handsDt > 0) // quenching: steam off the hot metal
                    steam(gear + V3{0, 0.03, 0}, 0.07, int(std::lround(handsDt * 70 * (1 - window(t, SM_PEAK, SM_COOLED - 0.2)) + rnd())));
                // what stays behind of the template and material stacks once one of each is used
                if (t >= SM_PEAK)
                    for (int i : {0, 2})
                        if (gS.cellFull[i]) laid(src(L.grid[i], i), gS.cellBlock[i], i, 1.0, 1);
                IconSrc sn[4];
                for (int i = 0; i < 4; ++i) {
                    sn[i] = src(L.grid[i], -1);
                    std::memcpy(sn[i].box, gS.snapBox[i], sizeof sn[i].box);
                    sn[i].snap = gG.snapValid;
                }
                // the gear, then the new piece, white-hot at first
                V3 at;
                if (t < SM_PEAK) at = smithPoint(1) + V3{0, 0.004, 0};
                else if (gS.moveToInv) at = lerp(smithPoint(1), cam.pos + V3{0, 0.55, 0} + yawForward(cam.yaw) * 0.6, inCubic(window(t, SM_COOLED + 0.05, SMITH_LENGTH))) + V3{0, 0.004, 0};
                else at = lerp(smithPoint(1), heldItemPoint(gRight.x, shoulderOf(true, cam.pos, cam.yaw)), inOutCubic(window(t, SM_COOLED + 0.12, SMITH_LENGTH))) + V3{0, 0.004, 0};
                double size = t < SM_COOLED || gS.moveToInv ? SM_SIZES[1] : SM_SIZES[1] + (0.205 - SM_SIZES[1]) * window(t, SM_COOLED + 0.1, SMITH_LENGTH);
                if (gS.moveToInv) size *= 1 - 0.85 * inCubic(window(t, SM_COOLED + 0.05, SMITH_LENGTH));
                float tilt = float(25 * window(t, SM_COOLED, SMITH_LENGTH));
                bool  gearIsNew = t >= SM_PEAK;
                orientedItem(cam, b, L, gearIsNew ? sn[3] : sn[1], gearIsNew ? gS.wasBlock[3] : gS.wasBlock[1], at, rgt, out, size, tilt, 1, 0);
                // the template on it and the material on top, sinking into it as they are pressed together
                double left = 1 - smithFuse(t);
                if (left > 0.02 && t < SM_PEAK) {
                    if (gS.wasFull[0])
                        orientedItem(cam, b, L, sn[0], gS.wasBlock[0], templatePos(t), rgt, out, SM_SIZES[0] * (0.55 + 0.45 * left), 0, 1,
                                     smithTurn(t, -12, 0, 0.14, TEMPLATE_DOWN));
                    if (gS.wasFull[2])
                        orientedItem(cam, b, L, sn[2], gS.wasBlock[2], materialPos(t), rgt, out, SM_SIZES[2] * (0.5 + 0.5 * left), 0, 1,
                                     smithTurn(t, 18, 8, MATERIAL_UP, MATERIAL_DOWN));
                    float glow = float(smoother(window(t, MATERIAL_DOWN - 0.1, SM_PRESS)) * left);
                    if (glow > 0.01) { // the material on top glows in its molten colour as it is pressed in
                        float k = glow * float(0.85 + 0.15 * std::sin(clock * 13));
                        glowBillboard(cam, b, materialPos(t) + V3{0, 0.02, 0}, 0.12, m[0] * k, m[1] * 0.75f * k, m[2] * 0.55f * k);
                    }
                }
                double heat = smithHeat(t);
                if (heat > 0.01) { // red, then orange, then white heat round the stack, cooling back after the quench
                    float h = float(heat), white = float(smoother(window(t, SM_PRESS, SM_PEAK))), fl = float(0.92 + 0.08 * std::sin(clock * 17));
                    float r = h * fl, g = (0.3f + 0.6f * white) * h * fl, bl = (0.08f + 0.6f * white * white) * h * fl;
                    glowBillboard(cam, b, smithPoint(1) + V3{0, 0.05, 0}, 0.17 + 0.08 * h, r, g, bl);
                    glowDisc(cam, b, smithPoint(1) + V3{0, 0.003, 0}, 0.2, r * 0.7f, g * 0.7f, bl * 0.7f);
                }
                if (t >= SM_PRESS - 0.02 && t < SM_PRESS + 0.4) { // the press lights up the plate under the stack
                    float k = float(std::exp(-(t - SM_PRESS) * 7) * 0.6);
                    glowDisc(cam, b, smithPoint(1) + V3{0, 0.004, 0}, 0.2, 0.6f * k, 0.85f * k, k);
                }
            }
        }
        if (haveCam && layoutOk && L.hasGrid && gS.st.kind == Kind::Cartography) {
            // CartographyStation.frame / renderScene / renderWorld (the map is drawn as its item: the map's own
            // picture is not reachable here)
            V3     rgt = gS.st.right(), out = gS.st.sideV();
            double t   = moveTime();
            auto drawMap = [&](IconSrc const& is, V3 at, double yaw, double size, float opacity) {
                orientedItem(cam, b, L, is, false, at + V3{0, 0.004, 0}, rgt, out, size, 0, opacity, yaw);
            };
            auto laidAddition = [&](IconSrc const& is, bool block, double scale, float opacity) { // SmithingStation.drawLaid(slot 2)
                double size = (block ? 0.13 : 0.2) * scale * ADDITION_SIZE / 0.2;
                orientedItem(cam, b, L, is, block, cartPoint(1) + V3{0, block ? size / 2 : 0.004, 0}, rgt, out, size, 0, opacity, 18);
            };
            if (t < 0) {
                if (gS.cellFull[0]) drawMap(src(L.grid[0], 0), cartPoint(0), 0, MAP_SIZE * (gS.cartHover == 0 ? 1.03 : 1.0), tableAlpha);
                if (gS.cellFull[1]) laidAddition(src(L.grid[1], 1), gS.cellBlock[1], gS.cartHover == 1 ? 1.08 : 1.0, tableAlpha);
                if (gS.phase == Phase::Active && gS.cartTarget == CtPlace && gS.haveAim) {
                    float p = float(0.18 + 0.06 * std::sin(clock * 5));
                    glowDisc(cam, b, gS.aim + V3{0, 0.004, 0}, gS.cartPlace == 0 ? 0.26 : 0.12, p, p * 0.8f, p * 0.6f);
                }
            } else {
                IconSrc mapBefore = src(L.grid[0], -1), addition = src(L.grid[1], -1);
                std::memcpy(mapBefore.box, gS.snapBox[0], sizeof mapBefore.box);
                std::memcpy(addition.box, gS.snapBox[1], sizeof addition.box);
                mapBefore.snap = addition.snap = gG.snapValid;
                V3 map = cartPoint(0) + V3{0, 0.02, 0};
                if (gS.cartWork == CCopy && t > 0.6 && t < CT_BEAT && rnd() < handsDt * 30) motes(map, MAP_SIZE * 0.3, 1, 0.35f, 0.3f, 0.25f, 0.08);
                if (!gS.gleamed && t > 0.9) {
                    gS.gleamed = true;
                    if (gS.cartWork == CLock) motes(map, MAP_SIZE * 0.4, 12, 0.8f, 0.9f, 1.0f, 0.15);
                }
                // what is left of the addition stack once one is used (live)
                if (t >= CT_BEAT && gS.cellFull[1]) laidAddition(src(L.grid[1], 1), gS.cellBlock[1], 1.0, 1);
                if (t < CT_BEAT) { // drawWorking
                    V3     mp = cartPoint(0), add = cartAdditionPos(t);
                    double settle = inOutCubic(window(t, 0.55, 1.25));
                    if (gS.cartWork == CZoom) { // the paper grows into the larger sheet, the old map shrinks to its middle
                        orientedItem(cam, b, L, addition, false, add + V3{0, 0.002, 0}, rgt, out, ADDITION_SIZE + (MAP_SIZE * 1.04 - ADDITION_SIZE) * settle, 0, 1, 0);
                        drawMap(mapBefore, mp + V3{0, 0.006, 0}, 0, MAP_SIZE * (1 - 0.5 * settle), 1);
                    } else if (gS.cartWork == CLock) { // the pane is turned over and laid on it, a gleam runs across
                        drawMap(mapBefore, mp, 0, MAP_SIZE, 1);
                        double flip = 180 * inOutCubic(window(t, 0.15, 0.5));
                        orientedItem(cam, b, L, addition, false, add + V3{0, 0.006, 0}, rgt, out, ADDITION_SIZE + (MAP_SIZE * 1.02 - ADDITION_SIZE) * settle,
                                     float(flip), 1, 0);
                        if (t > 0.9) {
                            double g  = window(t, 0.9, 1.4);
                            float  bb = float(0.4 * hump(g));
                            V3 at = mp + rgt * (-MAP_SIZE / 2 + MAP_SIZE * g) + out * (MAP_SIZE * (0.5 - g) * 0.6) + V3{0, 0.02, 0};
                            glowBillboard(cam, b, at, 0.08, bb, bb, bb);
                        }
                    } else { // the empty map turns into the copy as the land is traced onto it
                        drawMap(mapBefore, mp + rgt * (-0.06 * settle), -6 * settle, MAP_SIZE, 1);
                        V3     copy   = add + rgt * (0.08 * settle) + out * (-0.02 * settle) + V3{0, 0.004, 0};
                        double traced = smoother(window(t, 0.85, CT_BEAT - 0.05));
                        if (traced < 0.5) orientedItem(cam, b, L, addition, false, copy, rgt, out, ADDITION_SIZE + (MAP_SIZE - ADDITION_SIZE) * settle, 0, 1, 6);
                        else drawMap(mapBefore, copy, 6, MAP_SIZE, 1);
                        if (t > 0.85) {
                            float bb = float(0.3 * hump(traced));
                            glowBillboard(cam, b, lerp(mp, copy, traced) + V3{0, 0.03, 0}, 0.07, bb, bb * 0.9f, bb * 0.7f);
                        }
                    }
                } else { // to the hand (or the bag)
                    double k    = inOutCubic(window(t, CT_BEAT, WORK_LENGTH));
                    V3     dest = gS.moveToInv ? cam.pos + V3{0, 0.5, 0} + yawForward(cam.yaw) * 0.6 : heldItemPoint(gRight.x, shoulderOf(true, cam.pos, cam.yaw));
                    V3     at   = lerp(cartPoint(0) + V3{0, 0.1 * hump(k), 0}, dest, inCubic(k));
                    drawMap(mapBefore, at, 0, MAP_SIZE * (1 - 0.7 * k), 1);
                }
            }
        }
        if (haveCam && layoutOk && L.hasGrid && gS.st.kind == Kind::Loom) {
            // LoomStation.frame / renderScene / renderBoard (the cloth is the banner's own icon, stretched over the warp)
            V3     rgt = gS.st.right(), out = gS.st.sideV();
            double t   = moveTime();
            int    nPat = loomPatterns();
            auto cloth = [&](IconSrc const& is, V3 center, V3 upAxis, V3 across, double w, double l, double v0, double v1, float opacity) {
                // the part of the flag from v0 to v1 of its length (0 = its top, at the farV end)
                V3   tl = center + upAxis * (l / 2) - across * (w / 2);
                V3   p4[4] = {tl - upAxis * (l * v0), tl + across * w - upAxis * (l * v0), tl - upAxis * (l * v1), tl + across * w - upAxis * (l * v1)};
                Quad q;
                if (!worldQuad4(cam, b, p4, q)) return;
                float u0 = is.inside.x / L.W, u1 = (is.inside.x + is.inside.w) / L.W;
                float vy0 = (is.inside.y + is.inside.h * float(v0)) / L.H, vy1 = (is.inside.y + is.inside.h * float(v1)) / L.H;
                setRect(q.uv, u0, vy0, u1, vy1);
                keyed(q, L, opacity, is.snap);
                gQuads.push_back(q);
            };
            auto laid = [&](IconSrc const& is, bool block, int slot, double scale, float opacity) { // SmithingStation.drawLaid(slot 2)
                double size = (block ? 0.13 : 0.2) * scale;
                orientedItem(cam, b, L, is, block, loomPoint(slot) + V3{0, block ? size / 2 : 0.004, 0}, rgt, out, size, 0, opacity, 18);
            };
            V3 farV = out * -1.0, up{0, 1, 0};
            if (t < 0) {
                if (gS.cellFull[0]) {
                    // Bedrock's own preview of the banner (with the pattern under the cursor), as the Java cloth shows it
                    if (L.hasPreview && gS.previewFull) {
                        IconSrc pv{L.preview, {0, 0, 1, 1}};
                        float   ground[3] = {73 / 255.0f, 73 / 255.0f, 73 / 255.0f};
                        gKeyOv = ground;
                        cloth(pv, loomPoint(0) + V3{0, 0.003, 0}, farV, rgt, CLOTH_WIDTH, CLOTH_LENGTH, 0, 1, tableAlpha);
                        gKeyOv = nullptr;
                    } else cloth(src(L.grid[0], 0), loomPoint(0) + V3{0, 0.003, 0}, farV, rgt, CLOTH_WIDTH, CLOTH_LENGTH, 0, 1, tableAlpha);
                }
                for (int i = 1; i < 3; ++i)
                    if (gS.cellFull[i]) laid(src(L.grid[i], i), gS.cellBlock[i], i, i == gS.loomHover ? 1.08 : 0.95, tableAlpha);
                if (gS.phase == Phase::Active && gS.loomTarget == LPlace && gS.haveAim) {
                    float p = float(0.18 + 0.06 * std::sin(clock * 5));
                    glowDisc(cam, b, gS.aim + V3{0, 0.004, 0}, gS.loomPlace == 0 ? 0.24 : 0.1, p, p * 0.8f, p * 0.6f);
                }
            } else {
                IconSrc sn[4];
                for (int i = 0; i < 4; ++i) {
                    sn[i] = src(L.grid[i], -1);
                    std::memcpy(sn[i].box, gS.snapBox[i], sizeof sn[i].box);
                    sn[i].snap = gG.snapValid;
                }
                if (gS.cellFull[2]) laid(src(L.grid[2], 2), gS.cellBlock[2], 2, 0.95, 1);
                if (t >= WEAVE_TO && gS.cellFull[1]) laid(src(L.grid[1], 1), gS.cellBlock[1], 1, 0.95, 1);
                int passes = int(std::min(double(L_PASSES), loomWoven(t) * L_PASSES + 0.5));
                float* dc  = gS.iconRgb[1];
                if (passes > gS.passesDone) {
                    gS.passesDone = passes;
                    motes(weaveLine(t) + rgt * (shuttleAcross(t) * CLOTH_WIDTH / 2), 0.05, 4, dc[0], dc[1], dc[2], 0.12);
                }
                if (t < L_BEAT) {
                    double w = loomWoven(t);
                    // the old banner, and the new one growing behind the shuttle from the farV end (live result slot)
                    if (gS.loomSnap2) cloth(sn[3], loomPoint(0) + V3{0, 0.003, 0}, farV, rgt, CLOTH_WIDTH, CLOTH_LENGTH, 0, 1, 1);
                    else {
                        if (w < 0.999) cloth(sn[0], loomPoint(0) + V3{0, 0.003, 0}, farV, rgt, CLOTH_WIDTH, CLOTH_LENGTH, w, 1, 1);
                        if (w > 0.001 && gS.cellFull[3]) cloth(src(L.grid[3], 3), loomPoint(0) + V3{0, 0.004, 0}, farV, rgt, CLOTH_WIDTH, CLOTH_LENGTH, 0, w, 1);
                        else if (w > 0.001) cloth(sn[0], loomPoint(0) + V3{0, 0.003, 0}, farV, rgt, CLOTH_WIDTH, CLOTH_LENGTH, 0, w, 1);
                    }
                    if (t >= WEAVE_FROM && t < WEAVE_TO) { // the shuttle, carrying the dye's thread across
                        V3 line = weaveLine(t);
                        V3 at   = line + rgt * (shuttleAcross(t) * (CLOTH_WIDTH / 2 + 0.04)) + V3{0, 0.03, 0};
                        orientedItem(cam, b, L, sn[1], false, at, rgt, out, 0.13, 15, 1, 90);
                        float g = 0.35f;
                        glowBillboard(cam, b, line + rgt * (shuttleAcross(t) * CLOTH_WIDTH / 2), 0.05, dc[0] * g + 0.1f, dc[1] * g + 0.1f, dc[2] * g + 0.1f);
                    }
                } else { // off the loom: the new banner rises, stands up and goes to the hand (or the bag)
                    double k     = inOutCubic(window(t, L_BEAT, WEAVE_LENGTH));
                    V3     lift  = loomPoint(0) + V3{0, 0.003 + 0.25 * k, 0} + out * (0.15 * k);
                    V3     dest  = gS.moveToInv ? cam.pos + V3{0, 0.5, 0} + yawForward(cam.yaw) * 0.6 : heldItemPoint(gRight.x, shoulderOf(true, cam.pos, cam.yaw));
                    V3     center = lerp(lift, dest, inCubic(window(t, L_BEAT + 0.15, WEAVE_LENGTH)));
                    double stand  = smoother(k * 1.4);
                    V3     vAxis  = lerp(farV, up, stand).norm();
                    double size   = 1 - 0.75 * inCubic(window(t, L_BEAT + 0.1, WEAVE_LENGTH));
                    cloth(sn[3], center, vAxis, rgt, CLOTH_WIDTH * size, CLOTH_LENGTH * size, 0, 1, 1);
                }
            }
            // the glass board of patterns, beside the loom (GUI pixels, as the Java mod draws it)
            gS.boardShown = nPat > 0 && t < 0 && gS.phase == Phase::Active;
            double target = gS.boardShown ? 1 : 0;
            gS.board      = approach(gS.board, target, target > gS.board ? 9 : 14, handsDt);
            if (gS.board > 0.02 && nPat > 0) {
                float gui = float(spatialcam::javaAutoGuiScale(int(W), int(H)));
                gS.boardGui = gui;
                int   rows = std::min(L_ROWS, (nPat + L_COLS - 1) / L_COLS);
                float bw = float(L_PAD * 2 + L_COLS * L_CELL_W) * gui, bh = float(L_PAD * 2 + L_HEADER + rows * L_CELL_H) * gui;
                float ax, ay;
                if (!toPixel(cam, b, gS.st.top + rgt * 0.58 + V3{0, 0.28, 0}, int(W), int(H), ax, ay)) { ax = W * 0.7f; ay = H * 0.45f; }
                float appear = float(smoother(gS.board)) * tableAlpha;
                float bx = std::clamp(ax + 6 * gui + float(1 - gS.board) * 18 * gui, 8 * gui, float(W) - bw - 8 * gui);
                float by = std::clamp(ay - bh / 2, float(H) * 0.14f, float(H) * 0.86f - bh);
                gS.boardRect[0] = bx; gS.boardRect[1] = by; gS.boardRect[2] = bw; gS.boardRect[3] = bh;
                solid(bx, by, bx + bw, by + bh, int(W), int(H), 12 / 255.0f, 12 / 255.0f, 16 / 255.0f, 0.5f * appear);
                solid(bx, by + bh - gui, bx + bw, by + bh, int(W), int(H), 1, 1, 1, 0.22f * appear);
                solid(bx, by, bx + gui, by + bh - gui, int(W), int(H), 1, 1, 1, 0.08f * appear);
                solid(bx + bw - gui, by, bx + bw, by + bh - gui, int(W), int(H), 1, 1, 1, 0.08f * appear);
                if (appear >= 0.3f)
                    for (int k = 0, bt = 0; bt < kButtons && k < rows * L_COLS; ++bt) {
                        if (!gS.btnFull[bt]) continue;
                        int   row = k / L_COLS, col = k % L_COLS;
                        float x = bx + (L_PAD + col * L_CELL_W) * gui, y = by + (L_PAD + L_HEADER + row * L_CELL_H) * gui;
                        bool  hov = k == gS.loomPattern;
                        solid(x + gui, y + gui, x + (L_CELL_W - 1) * gui, y + (L_CELL_H - 1) * gui, int(W), int(H), 1, 1, 1, (hov ? 0.3f : 0.1f) * appear);
                        if (k == gS.loomSelected) {
                            solid(x, y, x + L_CELL_W * gui, y + gui, int(W), int(H), 1, 226 / 255.0f, 150 / 255.0f, 0.8f * appear);
                            solid(x, y + (L_CELL_H - 1) * gui, x + L_CELL_W * gui, y + L_CELL_H * gui, int(W), int(H), 1, 226 / 255.0f, 150 / 255.0f, 0.8f * appear);
                        }
                        // the pattern as the hidden screen's button shows it
                        Rect  r  = L.buttons[bt];
                        float in = std::min(r.w, L.slot) * 16.0f / 18.0f;
                        float sz = (hov ? 16.0f : 15.0f) * gui;
                        float cx = x + L_CELL_W * gui / 2, cy = y + (3 - (hov ? 1 : 0)) * gui + sz / 2 + 4 * gui;
                        Quad  q;
                        pixelQuad(cx - sz / 2, cy - sz / 2, cx + sz / 2, cy + sz / 2, int(W), int(H), q);
                        setRect(q.uv, (r.cx() - in / 2) / L.W, (r.cy() - in / 2) / L.H, (r.cx() + in / 2) / L.W, (r.cy() + in / 2) / L.H);
                        gKeyOv = gS.btnRef[bt];
                        keyed(q, L, appear, false);
                        gKeyOv = nullptr;
                        gQuads.push_back(q);
                        ++k;
                    }
            } else if (!gS.boardShown) {
                gS.boardRect[2] = gS.boardRect[3] = 0;
            }
        }
        if (haveCam && layoutOk && L.hasGrid && gS.st.kind == Kind::Enchanting) {
            // EnchantStation.tick / renderScene / renderWorld: the circle, the hovering item, the lapis, the shaft of
            // light, the offers as rune sigils with their cost, the rite
            V3     rgt = gS.st.right(), out = gS.st.sideV();
            double t = ritualTime(), rt = t >= 0 ? t * RITUAL_LENGTH : -1;
            bool   active = gS.phase == Phase::Active;
            float  guiPx  = L.slot / 18.0f; // one GUI pixel of the hidden screen
            {
                if (gS.hoverOffer >= 0) gS.dirtyUntil = clock + 0.3; // and a moment after (the screen catches up)
                bool dirty = gS.hoverOffer >= 0 || t >= 0 || clock < gS.dirtyUntil;
                if (!dirty && active && clock - gS.cleanAt > 0.2 && gG.keepValid) {
                    gSnapRequest = true;
                    std::memcpy(gS.snapBox, gS.iconBox, sizeof gS.snapBox);
                    for (int j = 0; j < 18; ++j) gS.wasRune[j] = gS.btnFull[6 + j];
                    for (int k = 0; k < 3; ++k) { gS.cleanFull[k] = gS.btnFull[3 + k]; std::memcpy(gS.cleanRgb[k], gS.iconRgb[14 + k], sizeof gS.cleanRgb[k]); }
                    std::memcpy(gS.cleanRef, gS.btnRef, sizeof gS.cleanRef);
                    gS.cleanAt = clock;
                    gS.cleanOk = true;
                }
                gS.useClean = dirty && gS.cleanOk;
                // the clue tooltips the window showed (found by the slot detection on its next look)
                for (int k = 0; k < 3; ++k)
                    if (L.hasClue[k]) { gS.clueRect[k] = L.clue[k]; gS.clueOk[k] = true; }
                if (gS.hoverOffer != gS.clueHover) { gS.clueHover = gS.hoverOffer; gS.hoverSince = clock; }
                if (gS.hoverOffer >= 0 && !gS.clueOk[gS.hoverOffer] && clock - gS.hoverSince > 0.12 && clock - gS.clueForced > 0.25) {
                    gS.clueForced = clock;
                    gG.lastDetect = 0; // look at the whole screen again now
                }
            }
            // the offers change (an item laid, taken, enchanted): they pop up again
            long print = 1;
            for (int k = 0; k < 3; ++k) {
                float const* bx = gS.useClean ? gS.snapBox[14 + k] : gS.iconBox[14 + k];
                print = print * 37 + (offered(k) ? 1 + int(16 * (bx[2] - bx[0])) : 0);
            }
            if (print != gS.offersPrint) { gS.offersPrint = print; gS.offersAt = clock; for (bool& c : gS.clueOk) c = false; }
            if (gS.hoverOffer != gS.lastHoverOffer) gS.lastHoverOffer = gS.hoverOffer;
            // per tick (20 a second)
            static int tickNo = 0;
            gS.enchTick += handsDt;
            while (gS.enchTick >= 0.05) {
                gS.enchTick -= 0.05;
                ++tickNo;
                if (!active) continue;
                bool itemNow = t >= 0 && rt < RITUAL_PEAK ? gS.wasFull[0] : enchItem();
                if (itemNow && tickNo % 3 == 0) eSparkle(enchItemPos(), 0.2);
                if (gS.hoverOffer >= 0) {
                    V3 sg = sigilPos(gS.hoverOffer);
                    glyphStream(glyphSource(), sg, 1);
                    glyphStream(sg, enchItemPos(), 1);
                }
                if (t >= 0 && rt < RITUAL_PEAK)
                    for (int i = 0; i < 3; ++i) { glyphStream(glyphSource(), enchItemPos(), 1); eGather(enchItemPos(), 0.55); }
            }
            if (t >= 0 && !gS.burstDone && rt >= RITUAL_PEAK) {
                gS.burstDone = true;
                gS.burstAt   = clock;
                eBurst(enchItemPos(), 1.4);
            }
            // the open book (EnchantTableRenderer: 0.1 over the top, turned to the player, leaning back) hides the
            // circles under it
            {
                V3 c = gS.st.top + V3{0, BOOK_LIFT, 0};
                V3 pts[8];
                for (int i = 0; i < 8; ++i) {
                    double ax = (i & 1) ? BOOK_HALF_W : -BOOK_HALF_W, az = (i & 2) ? BOOK_NEAR : -BOOK_FAR;
                    double ay = (i & 4) ? ((i & 2) ? BOOK_NEAR_H : BOOK_FAR_H) : -BOOK_LIFT + 0.002;
                    pts[i] = c + rgt * ax + out * az + V3{0, ay, 0};
                }
                setOccluder(cam, b, pts, 8);
            }
            // ---- renderScene
            double introP = clamp01(gS.time / gS.leanLength);
            double target = gS.phase == Phase::Intro ? 0.5 * smoother(window(introP, 0.45, 1.0))
                          : active ? 0.45 + (enchItem() ? 0.2 : 0) + (gS.hoverOffer >= 0 ? 0.3 : 0) + (t >= 0 ? 1.4 * hump(window(t, 0, 0.75)) : 0) : 0;
            gS.glow = approach(gS.glow, target, target > gS.glow ? 6 : 2.5, handsDt);
            double spin = clock * 0.25 + (t >= 0 ? 4 * smoother(t) : 0);
            float  g    = float(gS.glow);
            fxCircle(cam, b, gS.st.top + V3{0, OUTER_LIFT, 0}, 0.47, spin, 0.62f * g, 0.4f * g, 1.0f * g);
            fxCircle(cam, b, gS.st.top + V3{0, INNER_LIFT, 0}, 0.25, -spin * 1.7, 0.4f * g, 0.7f * g, 1.0f * g);
            bool   outro = gS.phase == Phase::Outro;
            double fade  = outro ? 1 - smoother(gS.time / 0.45) : 1;
            // the item over the table: during the rite, the one being enchanted until the light breaks
            auto floating = [&](IconSrc const& is, bool block, V3 at, double spinDeg, double size, float opacity) { // EnchantStation.drawFloating
                if (size <= 0.002) return;
                cardItem(cam, b, L, is, block, at, block ? size * 1.4 * 0.5 : size, opacity, spinDeg);
            };
            bool    before = t >= 0 && rt < RITUAL_PEAK;
            IconSrc itemSrc = src(L.grid[0], 0), lapisSrc = src(L.grid[1], 1);
            bool    itemFull = enchItem(), itemBlock = gS.cellBlock[0];
            if (before) {
                std::memcpy(itemSrc.box, gS.snapBox[0], sizeof itemSrc.box);
                itemSrc.snap = gG.snapValid;
                itemFull = gS.wasFull[0]; itemBlock = gS.wasBlock[0];
            }
            V3 at = enchItemPos();
            if (itemFull && fade > 0.01) {
                fxGlow(cam, b, at, 0.24, clock * 0.4, 0.3f * g, 0.18f * g, 0.5f * g);
                double extraSpin = t >= 0 ? 900 * outCubic(t) : 0;
                double pulse = t >= 0 && rt > RITUAL_PEAK ? 1 + 0.18 * hump(window(t, RITUAL_PEAK / RITUAL_LENGTH, 0.8)) : 1;
                floating(itemSrc, itemBlock, at, clock * 40 + extraSpin, E_ITEM_SIZE * fade * pulse, 1);
            }
            // the lapis socket (the server takes the lapis for the rite at once; what flies into the item comes below)
            double presence = gS.phase == Phase::Intro ? smoother(window(introP, 0.45, 1)) : fade;
            gS.lapisFocus = approach(gS.lapisFocus, t >= 0 ? 0 : gS.enchTarget == ELapis && active ? 1 : carryingLapis() ? 0.65 : 0, 12, handsDt);
            V3 corner = lapisCorner();
            if (presence > 0.01) {
                bool wrong = active && gS.enchTarget == ELapis && gS.carrying && !carryingLapis();
                float sk = float((0.50 + 0.42 * gS.lapisFocus) * presence);
                int keepN = gOcclN;
                gOcclN    = 0; // the socket floats beside the book, in front of it
                fxCircle(cam, b, corner + V3{0, SOCKET_LIFT, 0}, 0.10, clock * 0.3, wrong ? sk : sk * 0.2f, wrong ? sk * 0.2f : sk * 0.55f, wrong ? sk * 0.2f : sk);
                gOcclN = keepN;
                if (!gS.cellFull[1] && t < 0) lapisGhost(cam, b, corner + V3{0, 0.052 + std::sin(clock * 1.5) * 0.004, 0}, sk, 1);
            }
            // the cost digits / runes / a count of the hidden screen, in another colour, facing the camera
            auto glyphs = [&](Rect r, float const* key, bool snap, bool countOnly, V3 c, double perPx, unsigned rgb, float alpha, float satMax = 0, float satMin = 0, bool flat = false, float minValue = 0) {
                if (r.w <= 0 || alpha < 0.03f) return;
                double hw = r.w / guiPx * perPx / 2, hh = r.h / guiPx * perPx / 2;
                Quad   q;
                if (!worldQuad(cam, b, c, b.r * hw, b.u * hh, q)) return;
                setRect(q.uv, r.x / L.W, r.y / L.H, (r.x + r.w) / L.W, (r.y + r.h) / L.H);
                q.mode[0] = 9; q.mode[1] = snap ? 1.0f : 0.0f; q.mode[2] = 0.08f; q.mode[3] = 1;
                q.color[0] = ((rgb >> 16) & 255) / 255.0f; q.color[1] = ((rgb >> 8) & 255) / 255.0f; q.color[2] = (rgb & 255) / 255.0f; q.color[3] = alpha;
                for (int i = 0; i < 3; ++i) q.key0[i] = key ? key[i] : L.bg[i];
                q.key1[3] = countOnly ? 1.0f : 0.0f;
                q.key1[0] = satMax; q.key1[1] = satMin; q.key1[2] = minValue; q.key0[3] = flat ? 1.0f : 0.0f;
                gQuads.push_back(q);
            };
            bool hasCount = gS.cellFull[1] && gS.hasCount[1]; // the slot prints a count (2 or more)
            if (gS.cellFull[1] && fade > 0.01) { // EnchantStation.drawLapis: a few lying copies by the count
                int    copies = hasCount ? 2 : 1;
                double yaws[3] = {35, -20, 60};
                for (int i = 0; i < copies; ++i) {
                    V3 p = corner + V3{(i - (copies - 1) / 2.0) * 0.05, 0.001 + i * 0.008, (i % 2) * 0.03};
                    orientedItem(cam, b, L, lapisSrc, gS.cellBlock[1], p + V3{0, 0.004, 0}, rgt, out, 0.2 * 0.8 * fade, 0, 1, yaws[i]);
                }
                // drawLapisCount: the slot's own count digits, in the socket's blue, over it
                if (hasCount) { // "x" + the count, centred, with a shadow (font scale 0.0045 per pixel)
                    Rect   sl = lapisSrc.inside;
                    float* cb = gS.countBox[1];
                    Rect   cr{sl.x + sl.w * cb[0], sl.y + sl.h * cb[1], sl.w * (cb[2] - cb[0]) + guiPx, sl.h * (cb[3] - cb[1]) + guiPx}; // + the shadow
                    double px = 0.0045, digitsW = cr.w / guiPx * px, crossW = 6 * px, total = crossW + digitsW;
                    V3     at = corner + V3{0, 0.115, 0};
                    float  al = float(230 / 255.0 * presence);
                    glyphs(cr, nullptr, false, true, at + b.r * (-total / 2 + crossW + digitsW / 2), px, 0x9CC8FF, al);
                    // the "x": two 5 px strokes, the font's shadow (a quarter of the colour) one pixel down-right
                    V3 xc = at + b.r * (-total / 2 + 2.5 * px);
                    for (int sh = 1; sh >= 0; --sh) {
                        V3   o = (b.r - b.u) * (sh * px);
                        float k = sh ? 0.25f : 1.0f;
                        for (int d = 0; d < 2; ++d) {
                            V3   ax = (d ? b.r + b.u : b.r - b.u) * (2.5 * px), ay = (d ? b.u - b.r : b.r + b.u) * (0.5 * px);
                            Quad q;
                            if (!worldQuad(cam, b, xc + o + V3{0, -0.5 * px, 0}, ax, ay, q)) continue;
                            q.color[0] = 0x9C / 255.0f * k; q.color[1] = 0xC8 / 255.0f * k; q.color[2] = k; q.color[3] = al;
                            gQuads.push_back(q);
                        }
                    }
                }
            }
            if (t >= 0) { // the shaft of light from the burst on
                double peak = RITUAL_PEAK / RITUAL_LENGTH, beam = t < peak ? 0 : 1 - smoother((t - peak) / (1 - peak));
                if (beam > 0.01) {
                    float bb = float(beam * 1.3);
                    fxBeam(cam, b, V3{at.x, gS.st.top.y, at.z}, 1.9, 0.5 + 0.3 * beam, clock * 0.9, 0.7f * bb, 0.5f * bb, 1.0f * bb);
                }
            }
            // ---- renderWorld: the sigils, the things flying into the item (only while working)
            auto drawSigil = [&](int k, V3 c, float appear, bool hovered, bool aff, float brightness, bool snap) {
                if (appear <= 0.01f) return;
                float size = float(0.13 * appear * (hovered ? 1.2 : 1.0));
                float br   = brightness * (hovered ? 1.35f : 0.85f) * std::min(1.0f, appear);
                float r = aff ? 0.55f : 0.5f, gg = aff ? 0.45f : 0.12f, bl = aff ? 1.0f : 0.16f;
                fxGlow(cam, b, c, size * 1.8, 0, r * br * 0.35f, gg * br * 0.35f, bl * br * 0.35f);
                double turn = clock * (hovered ? 1.8 : 0.5) * (k % 2 == 0 ? 1 : -1);
                fxCircleBillboard(cam, b, c, size, turn, r * br, gg * br, bl * br);
                // the cost (Bedrock's own digits), the runes under it at 0.55, the lapis gems under those
                double s   = 0.01 * appear * (hovered ? 1.2 : 1.0);
                float  in  = L.slot * 16.0f / 18.0f;
                Rect   cc  = L.buttons[3 + k];
                Rect   cell{cc.cx() - in / 2, cc.cy() - in / 2, in, in};
                float const* bx = snap ? gS.snapBox[14 + k] : gS.iconBox[14 + k];
                Rect   num{cell.x + cell.w * bx[0], cell.y + cell.h * bx[1], cell.w * (bx[2] - bx[0]), cell.h * (bx[3] - bx[1])};
                float  a = std::min(1.0f, appear);
                auto ref = [&](int b2) { return snap ? gS.cleanRef[b2] : gS.btnRef[b2]; };
                glyphs(num, ref(3 + k), snap, false, c, s, aff ? 0xF4EEFF : 0xFF6B6B, a, 0, 0.2f, true, 0.5f); // no shadow (0.18.3)
                // the runes: what the six cells along the button hold (their extent), one flat colour, under the cost
                float rx0 = 1e9f, rx1 = -1, ry0 = 1e9f, ry1 = -1;
                for (int j = 0; j < 6; ++j) {
                    int c6 = 6 + k * 6 + j;
                    bool full = snap ? gS.wasRune[k * 6 + j] : gS.btnFull[c6];
                    if (!full) continue;
                    float const* rb = snap ? gS.snapBox[11 + c6] : gS.iconBox[11 + c6];
                    Rect  rc = L.buttons[c6];
                    Rect  ic{rc.cx() - in / 2, rc.cy() - in / 2, in, in};
                    rx0 = std::min(rx0, ic.x + ic.w * rb[0]); rx1 = std::max(rx1, ic.x + ic.w * rb[2]);
                    ry0 = std::min(ry0, ic.y + ic.h * rb[1]); ry1 = std::max(ry1, ic.y + ic.h * rb[3]);
                }
                if (rx1 > rx0) {
                    Rect   rune{rx0, ry0, rx1 - rx0, ry1 - ry0};
                    double hh = rune.h / guiPx * s * 0.55 / 2;
                    glyphs(rune, ref(6 + k * 6), snap, false, c - b.u * (26 * 0.55 * s + hh), s * 0.55, aff ? 0xB9A6FF : 0x7A4A55, a, 0.2f, 0, true);
                }
                for (int i = 0; i <= k; ++i) { // "◆" x (k + 1)
                    V3   gc = c - b.u * ((37 + 3.5) * 0.55 * s) + b.r * ((i - k / 2.0) * 7 * 0.55 * s);
                    double h = 3.0 * 0.55 * s;
                    V3   d1 = (b.r + b.u) * (h / std::sqrt(2.0)), d2 = (b.u - b.r) * (h / std::sqrt(2.0));
                    Quad q;
                    if (!worldQuad(cam, b, gc, d1, d2, q)) continue;
                    q.color[0] = 0x5B / 255.0f; q.color[1] = 0x86 / 255.0f; q.color[2] = 1; q.color[3] = a;
                    gQuads.push_back(q);
                }
            };
            if (active) {
                double appear = outBack(window(clock - gS.offersAt, 0, 0.45), 1.6);
                for (int k = 0; k < 3; ++k) {
                    if (!offered(k) || (t >= 0 && k == gS.ritualOffer)) continue;
                    drawSigil(k, sigilPos(k), float(appear), k == gS.hoverOffer, affordable(k), t >= 0 ? 0.4f : 1.0f, gS.useClean && gG.snapValid);
                }
                if (t >= 0) {
                    double flight = inOutCubic(window(rt, 0, RITUAL_PEAK - 0.05));
                    if (flight < 1) {
                        V3 p = lerp(gS.ritualFrom, enchItemPos(), flight) + V3{0, 0.12 * hump(flight), 0};
                        drawSigil(gS.ritualOffer, p, float(1 - 0.8 * flight), true, true, 1.6f, gG.snapValid);
                    }
                    IconSrc lap = src(L.grid[1], -1);
                    std::memcpy(lap.box, gS.snapBox[1], sizeof lap.box);
                    lap.snap = gG.snapValid;
                    for (int i = 0; i < gS.ritualLapis && gS.wasFull[1]; ++i) {
                        double f = inOutSine(window(rt, 0.04 + i * 0.07, RITUAL_PEAK - 0.02));
                        if (f > 0 && f < 1) {
                            V3 p = lerp(corner + V3{0, 0.02, 0}, enchItemPos(), f) + V3{0, 0.18 * hump(f), 0};
                            floating(lap, gS.wasBlock[1], p, clock * 300 + i * 90, 0.16 * (1 - 0.6 * f), 1);
                        }
                    }
                }
                if (gS.enchTarget == ESurface && gS.carrying && gS.haveAim) {
                    float pp = float(0.25 + 0.1 * std::sin(clock * 5));
                    fxCircle(cam, b, gS.st.top + V3{0, HINT_LIFT, 0}, 0.16, -clock, pp * 0.8f, pp * 0.7f, pp);
                }
            }
            drawGlyphs(cam, b, handsDt);
            gOcclN = 0;
        }
        if (haveCam) drawSparks(cam, b, clock, handsDt);
        if (haveCam && layoutOk && L.hasGrid && gS.st.kind == Kind::Table) {
            V3 fwd = gS.st.forward(), rgt = gS.st.right(), out = gS.st.sideV();
            double craft = craftProgress();
            // hover glow under the items (TableStation.drawHighlight: one even warm glow, rim 150/255)
            if (gS.hoverCell >= 0 && gS.phase == Phase::Active && craft < 0) {
                double half = Station::CELL / 2 - 0.006;
                V3     p    = gS.st.cellPoint(gS.hoverCell) + V3{0, 0.002, 0};
                Quad   q;
                if (worldQuad(cam, b, p, V3{half, 0, 0}, V3{0, 0, half}, q)) {
                    q.color[0] = 1.0f; q.color[1] = 244 / 255.0f; q.color[2] = 214 / 255.0f; q.color[3] = 47 / 255.0f;
                    gQuads.push_back(q);
                }
                double w = 0.004;
                V3 rims[4][2] = {{p + V3{0, 0, -half + w / 2}, V3{half, 0, 0}}, {p + V3{0, 0, half - w / 2}, V3{half, 0, 0}},
                                 {p + V3{-half + w / 2, 0, 0}, V3{0, 0, half}}, {p + V3{half - w / 2, 0, 0}, V3{0, 0, half}}};
                for (auto& rm : rims) {
                    V3 ax = rm[1].x != 0 ? V3{half, 0, 0} : V3{w / 2, 0, 0};
                    V3 ay = rm[1].z != 0 ? V3{0, 0, half} : V3{0, 0, w / 2};
                    Quad e;
                    if (worldQuad(cam, b, rm[0], ax, ay, e)) {
                        e.color[0] = 1.0f; e.color[1] = 235 / 255.0f; e.color[2] = 185 / 255.0f; e.color[3] = 150 / 255.0f;
                        gQuads.push_back(e);
                    }
                }
            }
            // what lies on the table (TableRenderer + ItemPlacer: flat items lie, blocks stand as 0.15 cubes); while
            // crafting they gather towards the middle and sink as the result appears
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) {
                    int recipe = r * 3 + c, world = gS.st.worldIndex(r, c);
                    if (!gS.cellFull[recipe]) continue;
                    V3    p = gS.st.cellPoint(world);
                    float scale = 1.0f;
                    if (craft >= 0) {
                        double k = inOutCubic(window(craft, 0.0, 0.55));
                        p        = lerp(p, gS.st.top, k * 0.6);
                        scale    = float(1 - 0.85 * window(craft, 0.35, 0.8));
                    }
                    if (scale <= 0.02f) continue;
                    placeItem(cam, b, L, src(L.grid[recipe], recipe), gS.cellBlock[recipe], p, rgt, out, 0.205 * scale, 0.15 * scale, 0, tableAlpha);
                }
            // the result floats over the far edge, turning slowly (TableStation.drawResult)
            if (gS.resultFull && L.hasResult) {
                double appear = outBack(window(clock - gS.resultChangedAt, 0, 0.35), 1.8);
                double hover  = gS.hoverResult ? 1.14 : 1.0;
                double pop    = craft >= 0 ? 1 - 0.6 * hump(window(craft, 0.15, 0.55)) : 1;
                double size   = 0.2 * appear * hover * pop;
                double spin   = std::fmod(clock * (gS.hoverResult ? 70 : 32), 360.0) * kDeg;
                V3     at     = gS.st.resultPosition(clock);
                V3     ax{std::cos(spin), 0, std::sin(spin)}, az{-std::sin(spin), 0, std::cos(spin)};
                IconSrc rs = src(L.result, 9);
                if (size > 0.002) {
                    if (gS.resultBlock && gCfg.cubeItems) {
                        // ItemDisplayContext.FIXED of a block model: a cube of half the scale, x1.6 for blocks
                        double edge = size * 1.6 * 0.5;
                        iconCube(cam, b, L, rs, at - V3{0, edge / 2, 0}, ax, az, edge, tableAlpha);
                    } else {
                        Quad q;
                        double half = size * 0.5;
                        if (worldQuad(cam, b, at, ax * half, V3{0, half, 0}, q)) {
                            setRect(q.uv, rs.inside.x / L.W, rs.inside.y / L.H, (rs.inside.x + rs.inside.w) / L.W, (rs.inside.y + rs.inside.h) / L.H);
                            keyed(q, L, tableAlpha);
                            gQuads.push_back(q);
                        }
                    }
                }
            }
        }
        // the player's own arms (Hands.render) and what the working hand carries
        if (haveCam && gCfg.drawArms && gG.skinValid && gS.phase != Phase::None) {
            float  skinW = float(gG.skinW), skinH = float(gG.skinH);
            V3     sR = shoulderOf(true, cam.pos, cam.yaw), sL = shoulderOf(false, cam.pos, cam.yaw);
            drawArm(cam, b, gRight.x, sR, true, gS.skinSlim, int(skinW), int(skinH));
            drawArm(cam, b, gLeft.x, sL, false, gS.skinSlim, int(skinW), int(skinH));
        }
        if (haveCam && layoutOk && gS.phase == Phase::Active && gS.carrying && gS.haveAim && craftProgress() < 0) {
            // Hands.itemInHand: the carried stack hangs off the working fist (TableStation.heldItemPoint, tilt 18)
            bool  lw = gS.leftWorks;
            V3    hand = lw ? gLeft.x : gRight.x;
            V3    at   = heldItemPoint(hand, shoulderOf(!lw, cam.pos, cam.yaw));
            float nx, ny;
            neutralPoint(L, nx, ny);
            float   sz = L.slot * 16.0f / 18.0f;
            IconSrc cs{{nx - sz * 0.5f, ny - sz * 0.5f, sz, sz}, {0, 0, 1, 1}};
            std::memcpy(cs.box, gS.iconBox[10], sizeof cs.box);
            placeItem(cam, b, L, cs, gS.carriedBlock, at, gS.st.right(), gS.st.sideV(), 0.205, 0.15, 18, 1);
        }
        // ---- the inventory drawer (SceneScreen.drawPanel)
        if (layoutOk && (gS.phase == Phase::Active || gS.phase == Phase::Outro)) {
            bool  over = gS.phase == Phase::Active && gS.overPanel;
            gS.expand  = approach(gS.expand, over ? 1 : 0, 13, handsDt); // this frame's time (the camera is asked several times a frame)
            DrawerGeom g = drawerGeom(int(W), int(H));
            float alpha = gS.phase == Phase::Active ? float(smoother(gS.time / 0.4)) : float(1 - inOutSine(gS.time / 0.22));
            float px = g.slotPx / 18.0f; // one GUI pixel of the drawer
            solid(g.x, g.y, g.x + g.w, g.y + g.h, int(W), int(H), 12 / 255.0f, 12 / 255.0f, 16 / 255.0f, 0.46f * alpha);
            solid(g.x, g.y + g.h - px, g.x + g.w, g.y + g.h, int(W), int(H), 1, 1, 1, 0.20f * alpha);
            solid(g.x, g.y, g.x + px, g.y + g.h - px, int(W), int(H), 1, 1, 1, 0.08f * alpha);
            solid(g.x + g.w - px, g.y, g.x + g.w, g.y + g.h - px, int(W), int(H), 1, 1, 1, 0.08f * alpha);
            auto slot = [&](Rect const& src, float x, float y, bool hovered, float a) {
                if (y + g.slotPx < 0 || y > g.y + g.h) return;
                solid(x + px, y + px, x + g.slotPx - px, y + g.slotPx - px, int(W), int(H), 1, 1, 1, 0.13f * a);
                if (hovered) solid(x + px, y + px, x + g.slotPx - px, y + g.slotPx - px, int(W), int(H), 1, 1, 1, 0.35f * a);
                if (a > 0.35f) {
                    Quad q;
                    pixelQuad(x + px, y + px, x + g.slotPx - px, y + g.slotPx - px, int(W), int(H), q);
                    uvOf(src, L, 1.0f / 18.0f, q.uv);
                    keyed(q, L, a);
                    gQuads.push_back(q);
                }
            };
            for (int c = 0; c < 9; ++c) slot(L.hotbar[c], g.x + g.padPx + c * g.slotPx, g.y + g.padPx, gS.hoverDrawer == c, alpha);
            if (gS.expand > 0.02) {
                float rows = float(smoother(gS.expand));
                float dy   = g.y + g.padPx + g.slotPx + g.gapPx * 0.5f;
                solid(g.x + g.padPx + 2 * px, dy, g.x + g.padPx + 9 * g.slotPx - 2 * px, dy + px, int(W), int(H), 1, 1, 1, 0.12f * alpha * rows);
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 9; ++c)
                        slot(L.inv[r * 9 + c], g.x + g.padPx + c * g.slotPx, g.y + g.padPx + g.slotPx + g.gapPx + r * g.slotPx,
                             gS.hoverDrawer == 9 + r * 9 + c, alpha * rows);
            } else {
                float gx = g.x + g.padPx + 4.5f * g.slotPx;
                solid(gx - 9 * px, g.y + g.padPx + g.slotPx + 2 * px, gx + 9 * px, g.y + g.padPx + g.slotPx + 3 * px, int(W), int(H), 1, 1, 1, 0.35f * alpha);
            }
            // off the work the carried stack follows the real cursor (SceneScreen: carried, drawn over everything)
            if (gS.phase == Phase::Active && gCursorValid && gS.carrying && !gS.haveAim && !gS.hoverResult) {
                float nx, ny;
                neutralPoint(L, nx, ny);
                float s2 = L.slot * 16.0f / 18.0f;
                float mx = (gCursorNdcX + 1) * 0.5f * W, my = (1 - gCursorNdcY) * 0.5f * H;
                float half = s2 * 0.5f;
                Quad  q;
                pixelQuad(mx - half, my - half, mx + half, my + half, int(W), int(H), q);
                setRect(q.uv, (nx - half) / L.W, (ny - half) / L.H, (nx + half) / L.W, (ny + half) / L.H);
                q.mode[0] = 1; q.mode[3] = 1;
                gQuads.push_back(q);
            }
        }
        // ---- the station's flash over the whole frame (Overlay.render)
        {
            float  fc[3] = {1, 1, 1};
            double fl    = stationFlash(fc);
            if (fl > 0.003 && gCfg.screenFlash && !gCfg.reduceMotion) solid(0, 0, float(W), float(H), int(W), int(H), fc[0], fc[1], fc[2], float(std::min(1.0, fl)));
        }
        // ---- letterbox (Overlay.bars)
        if (letterbox()) {
            double k = 0;
            if (gS.phase == Phase::Intro && !gS.quick) {
                double u = clamp01(gS.time / gS.leanLength);
                k = inOutSine(window(u, 0, 0.3)) * (1 - inOutSine(window(u, 0.72, 1.0)));
            } else if (gS.phase == Phase::Outro) {
                double u = gS.time / OUTRO_LENGTH;
                k = inOutSine(window(u, 0, 0.25)) * (1 - inOutSine(window(u, 0.7, 1.0)));
            }
            if (k > 0.001) {
                float h = float(H * 0.085 * k);
                solid(0, 0, float(W), h, int(W), int(H), 0, 0, 0, 1);
                solid(0, H - h, float(W), float(H), int(W), int(H), 0, 0, 0, 1);
            }
        }
    }
    if (gQuads.empty()) return true;
    ID3D12DescriptorHeap* heaps[] = {gG.srvHeap.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootSignature(gG.root.Get());
    list->SetPipelineState(gG.pso.Get());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D12_VIEWPORT vp{0, 0, float(W), float(H), 0, 1};
    D3D12_RECT     sc{0, 0, LONG(W), LONG(H)};
    list->OMSetRenderTargets(1, &bbRtv, FALSE, nullptr);
    list->RSSetViewports(1, &vp);
    list->RSSetScissorRects(1, &sc);
    list->SetGraphicsRootDescriptorTable(1, gG.srvHeap->GetGPUDescriptorHandleForHeapStart());
    for (auto& q : gQuads) {
        list->SetGraphicsRoot32BitConstants(0, 60, &q, 0);
        list->DrawInstanced(6, 1, 0, 0);
    }
    gG.status = "drawing " + std::to_string(gQuads.size()) + " quads";
    return true;
}

// ================================================================ ImGui: hint line, F6
namespace {
// lang/en_us.json of the Java mod
char const* kZoneName[3][3] = {{"Upper opening - to smelt", "Fire box - fuel", "Done"},
                               {"Window - to cook", "Behind the grate - fuel", "Ready"},
                               {"Charging from the top - ore", "Front grate - fuel", "Sill - finished ingots"}};
char const* kZoneEmpty[3][3] = {{"Put in what you want smelted", "Put coal, wood or other fuel here", "Smelted items come out here"},
                                {"Put in raw food", "Put coal, wood or other fuel behind the grate", "Cooked food comes out here"},
                                {"Load ore and metal gear here", "Put coal or other fuel behind the grate", "Ingots come out on this sill"}};
std::string stationHint(Kind k, bool overResult) {
    char const* dot = "  \xC2\xB7  ";
    switch (k) {
    case Kind::Table:
        return overResult ? std::string("Click to craft") + dot + "Shift-click to craft all"
                          : std::string("LMB take / put down (drag to share)") + dot + "RMB one at a time (drag to spread)" + dot + "Shift quick" + dot + "Esc step back";
    case Kind::Furnace: return std::string("Fuel goes in below, things to smelt above") + dot + "LMB take / put" + dot + "RMB one at a time" + dot + "Shift to inventory" + dot + "Esc step back";
    case Kind::Smoker: return std::string("Fuel behind the grate, food into the window") + dot + "LMB take / put" + dot + "RMB one at a time" + dot + "Shift to inventory" + dot + "Esc step back";
    case Kind::BlastFurnace: return std::string("Ore goes in from the top, fuel behind the grate") + dot + "LMB take / put" + dot + "RMB one at a time" + dot + "Shift to inventory" + dot + "Esc step back";
    case Kind::Anvil: return std::string("Item on the anvil, what goes into it on top") + dot + "LMB on them - forge" + dot + "RMB - take" + dot + "Esc - step away";
    case Kind::Enchanting: return std::string("Lay the item on the table") + dot + "lapis into the blue rune right of the book" + dot + "touch a rune to enchant" + dot + "Esc step back";
    case Kind::Loom: return std::string("Banner on the warp, dye right, pattern left") + dot + "pick a pattern on the board - weave" + dot + "Esc - step away";
    case Kind::Cartography: return std::string("Map in the middle, paper, glass or an empty map on the right") + dot + "click the map - work it" + dot + "Esc - step away";
    case Kind::Smithing: return std::string("Template left, gear in the middle, material right") + dot + "LMB on the gear - smith" + dot + "RMB - take" + dot + "Esc - step away";
    case Kind::Stonecutter: return std::string("Lay stone by the saw") + dot + "pick a shape round the blade - cut one" + dot + "Shift - cut the stack" + dot + "Esc - step away";
    case Kind::Grindstone: return std::string("Lay the item on the wheel") + dot + "LMB on it - grind (enchantments come off)" + dot + "RMB - take" + dot + "Esc - step away";
    default: return "Esc step back";
    }
}
} // namespace

ID3D12GraphicsCommandList* gImList = nullptr;
ID3D12DescriptorHeap*      gImHeap = nullptr;
unsigned                   gImW = 0, gImH = 0;
std::vector<Quad>          gOverlayQuads; // drawn inside ImGui's order (overlayQuadCb)
void setImGuiTarget(ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* heap, unsigned W, unsigned H) {
    gImList = list; gImHeap = heap; gImW = W; gImH = H;
}
static void overlayQuadCb(ImDrawList const*, ImDrawCmd const* cmd) {
    size_t i = size_t(reinterpret_cast<uintptr_t>(cmd->UserCallbackData));
    if (!gImList || i >= gOverlayQuads.size() || !gG.root || !gG.pso || !gG.srvHeap) return;
    ID3D12DescriptorHeap* heaps[] = {gG.srvHeap.Get()};
    gImList->SetDescriptorHeaps(1, heaps);
    gImList->SetGraphicsRootSignature(gG.root.Get());
    gImList->SetPipelineState(gG.pso.Get());
    gImList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D12_VIEWPORT vp{0, 0, float(gImW), float(gImH), 0, 1};
    D3D12_RECT     sc{0, 0, LONG(gImW), LONG(gImH)};
    gImList->RSSetViewports(1, &vp);
    gImList->RSSetScissorRects(1, &sc);
    gImList->SetGraphicsRootDescriptorTable(1, gG.srvHeap->GetGPUDescriptorHandleForHeapStart());
    gImList->SetGraphicsRoot32BitConstants(0, 60, &gOverlayQuads[i], 0);
    gImList->DrawInstanced(6, 1, 0, 0);
    if (gImHeap) { ID3D12DescriptorHeap* back[] = {gImHeap}; gImList->SetDescriptorHeaps(1, back); }
}

void drawOverlay() {
    std::string hint, tipTitle, tipLine;
    std::vector<std::pair<std::string, ImU32>> tipLines; // a tooltip of coloured lines (as the Java one) instead of title + line
    bool        clueLine = false; // tipLines[0] is the hidden screen's clue text (`clue`, its pixels)
    Rect        clue;
    float       clueGui = 1, layW = 1, layH = 1;
    bool        itemTip = false; // the hovered item's own hover text (the hidden screen's, copied aside by the probe)
    Rect        itemText;
    float       tipW = 1, tipH = 1;
    static bool creativeNow = false;
    static double creativeAt = -10;
    bool        boardLabel = false;
    float       boardAt[4] = {}, boardA = 0, boardG = 1;
    float       a = 0, mx = 0, my = 0;
    {
        std::lock_guard l(gM);
        if (gS.phase != Phase::Active || !gCfg.controlHints) return;
        double fade = 1 - 0.55 * smoother((gS.time - 7) / 2);
        a    = float(smoother(gS.time / 0.4) * fade);
        hint = stationHint(gS.st.kind, gS.hoverResult);
        tipTitle.clear();
        tipLine.clear();
        if (isFurnace(gS.st.kind) && gS.hoverCell >= 0 && !gS.carrying) {
            int z   = gS.hoverCell, k = gS.st.kind == Kind::Smoker ? 1 : gS.st.kind == Kind::BlastFurnace ? 2 : 0;
            tipTitle = kZoneName[k][z];
            tipLine  = gS.cellFull[z] ? (z == 2 ? "Click to take \xC2\xB7 Shift-click to inventory" : "") : kZoneEmpty[k][z];
        }
        if (gS.st.kind == Kind::Grindstone && gS.grindTarget == GItem && !gS.carrying && !moving()) {
            // GrindstoneStation.tooltip (the item's own lines are the hidden screen's: not shown here)
            if (gS.resultFull3()) {
                bool strips = (gS.cellFull[0] && gS.cellGlint[0]) || (gS.cellFull[1] && gS.cellGlint[1]);
                bool merges = gS.cellFull[0] && gS.cellFull[1];
                tipTitle = strips ? "Enchantments come off (curses stay), a little experience returns" : merges ? "The two are ground into one" : "Grind";
                tipLine  = "LMB - grind  \xC2\xB7  RMB - take  \xC2\xB7  Shift - straight into the inventory";
            } else {
                tipTitle = "Click - take";
            }
        }
        if (gS.st.kind == Kind::Loom && !moving()) { // LoomStation.tooltip (without the item's own lines)
            if (gS.loomTarget == LPattern) tipTitle = "LMB - weave  \xC2\xB7  Shift - straight into the inventory";
            else if (gS.loomTarget == LItem) {
                tipTitle = "Click - take";
                if (gS.loomHover == 0 && !gS.cellFull[1]) tipLine = "Lay a dye and the patterns appear";
            }
        }
        if (gS.st.kind == Kind::Cartography && !moving()) { // CartographyStation.tooltip (without the item's own lines)
            if (gS.cartTarget == CtPlace && gS.cartPlace >= 0) tipTitle = gS.cartPlace == 0 ? "A filled map goes here" : "Paper, glass or an empty map goes here";
            else if (gS.cartTarget == CtItem) {
                if (gS.cellFull[2] && gS.cartHover == 0) {
                    int w    = cartWorkOf();
                    tipTitle = w == CZoom ? "Paper: the map will cover more land" : w == CLock ? "Glass: the map is locked and won't change" : "Empty map: makes a copy";
                    tipLine  = "LMB - work it  \xC2\xB7  Shift - straight into the inventory";
                } else {
                    tipTitle = "Click - take";
                    if (gS.cartHover == 0 && !gS.cellFull[1]) tipLine = "Lay paper, glass or an empty map on the right";
                    else if (gS.cellFull[1] && !gS.cellFull[2] && gS.cellFull[0]) tipLine = "This map can't take that";
                }
            }
        }
        if (gS.st.kind == Kind::Smithing && !moving()) { // SmithingStation.tooltip (without the item's own lines)
            char const* names[3] = {"Smithing template", "Gear: armour, a weapon or a tool", "Material: an ingot or a mineral"};
            char const* missing[3] = {"A template is missing", "The gear is missing", "A material is missing"};
            if (gS.smithTarget == SmPlace && gS.smithPlace >= 0) tipTitle = names[gS.smithPlace];
            else if (gS.smithTarget == SmItem) {
                if (gS.cellFull[3]) { tipTitle = "LMB - smith  \xC2\xB7  RMB - take  \xC2\xB7  Shift - straight into the inventory"; }
                else {
                    tipTitle = "Click - take";
                    for (int i = 0; i < 3; ++i) if (!gS.cellFull[i]) { tipLine = missing[i]; break; }
                }
            }
        }
        if (gS.st.kind == Kind::Enchanting && gS.clock - creativeAt > 1) { creativeAt = gS.clock; creativeNow = game::creative(); }
        {
            std::lock_guard ll3(gLayoutMutex);
            clueGui = gLayout.slot / 18.0f; layW = float(gLayout.W); layH = float(gLayout.H);
        }
        if (gS.tipSlot >= 0 && gS.tipCache[gS.tipSlot].ok && gG.kw > 0) {
            itemTip  = true;
            itemText = gS.tipCache[gS.tipSlot].text;
            tipW = float(gG.kw); tipH = float(gG.kh);
        }
        if (gS.st.kind == Kind::Enchanting && !moving() && !gS.overPanel) { // EnchantStation.tooltip (the clue and the item's own lines are the hidden screen's)
            if (gS.enchTarget == EOffer && gS.hoverOffer >= 0) { // offerTooltip: the window's own clue, then what it costs
                int  h    = gS.hoverOffer, need = h + 1;
                bool aff  = affordable(h);
                ImU32 gray = IM_COL32(170, 170, 170, 255), red = IM_COL32(255, 85, 85, 255);
                if (gS.clueOk[h]) { clueLine = true; clue = gS.clueRect[h]; }
                tipLines.push_back({"", IM_COL32(255, 255, 255, 255)}); // the clue (drawn from the hidden screen)
                if (!creativeNow) {
                    tipLines.push_back({"", gray});
                    if (!aff) tipLines.push_back({"Not enough experience or lapis", red});
                    tipLines.push_back({need == 1 ? "1 Lapis Lazuli" : std::to_string(need) + " Lapis Lazuli", aff || gS.cellFull[1] ? gray : red});
                    tipLines.push_back({need == 1 ? "1 Enchantment Level" : std::to_string(need) + " Enchantment Levels", gray});
                }
                if (aff && !creativeNow) tipLines.push_back({"Touch the rune to enchant", IM_COL32(255, 85, 255, 255)});
            } else if (gS.enchTarget == EItem && !gS.carrying) tipTitle = "Click to take it back";
            else if (gS.enchTarget == ESurface && !gS.carrying && !enchItem()) tipTitle = "Lay here what you want to enchant";
            else if (gS.enchTarget == ELapis) {
                if (gS.carrying && !carryingLapis()) tipTitle = "Only lapis lazuli fits here";
                else if (!gS.carrying && !gS.cellFull[1]) tipTitle = "Put lapis lazuli into this blue rune";
                else if (!gS.carrying) tipTitle = "Lapis Lazuli";
            }
        }
        if (gS.st.kind == Kind::Stonecutter && !moving()) { // StonecutterStation.tooltip (without the item's own lines)
            if (gS.stoneTarget == SShape) tipTitle = "LMB - cut one  \xC2\xB7  Shift - cut the whole stack";
            else if (gS.stoneTarget == SStack) {
                tipTitle = "Click - take";
                if (shapeCount() == 0) tipLine = "This can't be cut";
            }
        }
        if (gS.st.kind == Kind::Loom && gS.boardRect[2] > 0 && gS.board > 0.3) {
            boardLabel = true;
            std::memcpy(boardAt, gS.boardRect, sizeof boardAt);
            boardA  = float(smoother(gS.board));
            boardG  = gS.boardGui;
        }
        mx = (gCursorNdcX + 1) * 0.5f;
        my = (1 - gCursorNdcY) * 0.5f;
    }
    if (a < 0.03f) return;
    auto*  dl = ImGui::GetForegroundDrawList();
    ImVec2 d  = ImGui::GetIO().DisplaySize;
    float  fs = std::max(13.0f, d.y / 60.0f);
    ImFont* font = ImGui::GetFont();
    ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, hint.c_str());
    float  x = (d.x - ts.x) * 0.5f, y = d.y - fs * 2.2f;
    dl->AddRectFilled(ImVec2(x - fs * 0.5f, y - fs * 0.3f), ImVec2(x + ts.x + fs * 0.5f, y + ts.y + fs * 0.3f), IM_COL32(0, 0, 0, int(a * 200 * 0.45f)));
    dl->AddText(font, fs, ImVec2(x, y), IM_COL32(0xF2, 0xEA, 0xD8, int(a * 200)), hint.c_str());
    if (boardLabel) // the loom's board: its title in the corner
        dl->AddText(font, 9.0f * boardG, ImVec2(boardAt[0] + L_PAD * boardG, boardAt[1] + (L_PAD - 1) * boardG),
                    IM_COL32(242, 234, 216, int(0.75f * boardA * 255)), "Patterns");
    // the station's tooltip, vanilla style (dark violet box, gold title)
    gOverlayQuads.clear();
    if (itemTip && tipLines.empty()) { // the item's text first, then the station's lines (title gold, the rest grey)
        if (!tipTitle.empty()) tipLines.push_back({tipTitle, IM_COL32(255, 170, 0, 255)});
        if (!tipLine.empty()) tipLines.push_back({tipLine, IM_COL32(170, 170, 170, 255)});
        tipTitle.clear();
    }
    if (!tipLines.empty() || itemTip) {
        float ts2 = std::max(14.0f, d.y / 54.0f), pad = ts2 * 0.35f, w = 0, h = pad * 2, lh = ts2 * 1.15f;
        // the clue: Bedrock's writing at the tooltip's text size (its 7 px capitals as tall as ours)
        float cs = clueGui > 0 ? ts2 * 0.72f / (7 * clueGui) : 1, cw = clueLine ? clue.w * cs : 0, ch = clueLine ? clue.h * cs : 0;
        float iw = itemTip ? itemText.w * cs : 0, ih = itemTip ? itemText.h * cs : 0, io = itemTip ? ih + (tipLines.empty() ? 0 : pad * 0.6f) : 0;
        for (auto& l : tipLines) w = std::max(w, font->CalcTextSizeA(ts2, FLT_MAX, 0, l.first.c_str()).x);
        w = std::max(w, std::max(cw, iw));
        w += pad * 2;
        h += lh * tipLines.size() + io;
        float tx = std::min(mx * d.x + ts2, d.x - w - 4), ty = std::min(my * d.y - ts2 * 0.6f, d.y - h - 4);
        dl->AddRectFilled(ImVec2(tx, ty), ImVec2(tx + w, ty + h), IM_COL32(16, 0, 16, 240), 0);
        dl->AddRect(ImVec2(tx + 1, ty + 1), ImVec2(tx + w - 1, ty + h - 1), IM_COL32(80, 0, 255, 90), 0, 0, 2.0f);
        for (size_t i = 0; i < tipLines.size(); ++i) dl->AddText(font, ts2, ImVec2(tx + pad, ty + pad + io + lh * i), tipLines[i].second, tipLines[i].first.c_str());
        if (itemTip && gImList && gImW > 0) {
            float sx = float(gImW) / std::max(1.0f, d.x), sy = float(gImH) / std::max(1.0f, d.y);
            float x0 = (tx + pad) * sx, y0 = (ty + pad) * sy;
            Quad  q;
            pixelQuad(x0, y0, x0 + iw * sx, y0 + ih * sy, int(gImW), int(gImH), q);
            setRect(q.uv, itemText.x / tipW, itemText.y / tipH, (itemText.x + itemText.w) / tipW, (itemText.y + itemText.h) / tipH);
            q.mode[0] = 10; q.mode[1] = 0; q.mode[2] = 0; q.mode[3] = 1;
            q.color[0] = q.color[1] = q.color[2] = 1; q.color[3] = 1;
            gOverlayQuads.push_back(q);
            dl->AddCallback(overlayQuadCb, reinterpret_cast<void*>(uintptr_t(gOverlayQuads.size() - 1)));
            dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
        }
        if (clueLine && gImList && gImW > 0) {
            // the window's clue, its white writing only, drawn in ImGui's order (over the tooltip box)
            float sx = float(gImW) / std::max(1.0f, d.x), sy = float(gImH) / std::max(1.0f, d.y);
            float x0 = (tx + pad) * sx, y0 = (ty + pad + io + (lh - ch) * 0.5f) * sy;
            Quad  q;
            pixelQuad(x0, y0, x0 + cw * sx, y0 + ch * sy, int(gImW), int(gImH), q);
            setRect(q.uv, clue.x / layW, clue.y / layH, (clue.x + clue.w) / layW, (clue.y + clue.h) / layH);
            q.mode[0] = 9; q.mode[1] = 0; q.mode[2] = 0.08f; q.mode[3] = 1;
            q.color[0] = q.color[1] = q.color[2] = 1; q.color[3] = 1;
            q.key0[0] = 27 / 255.0f; q.key0[1] = 12 / 255.0f; q.key0[2] = 27 / 255.0f; q.key0[3] = 1; // flat white
            q.key1[2] = 0.9f;                                                                         // the writing only
            gOverlayQuads.push_back(q);
            dl->AddCallback(overlayQuadCb, reinterpret_cast<void*>(uintptr_t(gOverlayQuads.size() - 1)));
            dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
        }
    } else if (!tipTitle.empty()) {
        float  ts2 = std::max(14.0f, d.y / 54.0f), pad = ts2 * 0.35f;
        ImVec2 t1  = font->CalcTextSizeA(ts2, FLT_MAX, 0, tipTitle.c_str());
        ImVec2 t2  = tipLine.empty() ? ImVec2(0, 0) : font->CalcTextSizeA(ts2, FLT_MAX, 0, tipLine.c_str());
        float  w = std::max(t1.x, t2.x) + pad * 2, h = t1.y + (tipLine.empty() ? 0 : t2.y + pad * 0.4f) + pad * 2;
        float  tx = std::min(mx * d.x + ts2, d.x - w - 4), ty = std::min(my * d.y + ts2 * 0.6f, d.y - h - 4);
        dl->AddRectFilled(ImVec2(tx, ty), ImVec2(tx + w, ty + h), IM_COL32(16, 0, 16, 240), 0);
        dl->AddRectFilledMultiColor(ImVec2(tx + 1, ty + 1), ImVec2(tx + w - 1, ty + 3), IM_COL32(80, 0, 255, 120), IM_COL32(80, 0, 255, 120), IM_COL32(40, 0, 127, 120), IM_COL32(40, 0, 127, 120));
        dl->AddRect(ImVec2(tx + 1, ty + 1), ImVec2(tx + w - 1, ty + h - 1), IM_COL32(80, 0, 255, 90), 0, 0, 2.0f);
        dl->AddText(font, ts2, ImVec2(tx + pad, ty + pad), IM_COL32(255, 170, 0, 255), tipTitle.c_str());
        if (!tipLine.empty()) dl->AddText(font, ts2, ImVec2(tx + pad, ty + pad + t1.y + pad * 0.4f), IM_COL32(170, 170, 170, 255), tipLine.c_str());
    }
}

void drawUi() {
    if (!ImGui::TreeNode("Cinematic Interactions (stations)")) return;
    ImGui::TextWrapped("Port of Cinematic Interactions by Southware (MIT). Per station: the Cinematic scene, the Spatial GUI "
                       "floating screen, or the plain vanilla screen. A scene that cannot play where you stand falls back to Spatial GUI.");
    ImGui::Checkbox("Cinematic scenes (master)", &gCfg.enabled);
    ImGui::SameLine();
    ImGui::Checkbox("Spatial GUI (master)", &spatialcam::cfg().enabled);
    const char* modes[] = {"Cinematic", "Spatial", "Vanilla"};
    for (int k = 0; k < int(Kind::Count); ++k) {
        ImGui::PushID(k);
        ImGui::SetNextItemWidth(140);
        ImGui::Combo(kNames[k], &gCfg.mode[k], modes, 3);
        if (gCfg.mode[k] == 0 && !sceneAvailable(Kind(k))) { ImGui::SameLine(); ImGui::TextDisabled("(scene not ported yet: Spatial)"); }
        ImGui::PopID();
    }
    if (ImGui::TreeNode("Camera (Java settings)")) {
        ImGui::SliderInt("Approach speed (%)", &gCfg.approachSpeed, 50, 200);
        ImGui::SliderInt("Leave speed (%)", &gCfg.leaveSpeed, 50, 200);
        ImGui::SliderInt("Lean strength (%)", &gCfg.leanStrength, 0, 100);
        ImGui::SliderInt("Scene zoom (%)", &gCfg.sceneZoom, 0, 100);
        const char* qm[] = {"Cinematic entry", "Quick on a recent return", "Quick on every entry"};
        ImGui::Combo("Quick mode", &gCfg.quickMode, qm, 3);
        ImGui::SliderInt("Quick window (s)", &gCfg.quickWindow, 5, 30);
        ImGui::Checkbox("Letterbox", &gCfg.letterbox);
        ImGui::SameLine();
        ImGui::Checkbox("Living camera", &gCfg.livingCamera);
        ImGui::Checkbox("Camera shake", &gCfg.cameraShake);
        ImGui::SameLine();
        ImGui::Checkbox("Screen flash", &gCfg.screenFlash);
        ImGui::Checkbox("Reduce motion", &gCfg.reduceMotion);
        ImGui::SameLine();
        ImGui::Checkbox("Control hints", &gCfg.controlHints);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Bedrock")) {
        ImGui::Checkbox("Full lean (single player: the camera leans in like the Java mod with its server side)", &gCfg.fullLean);
        ImGui::Checkbox("Draw the arms (your skin)", &gCfg.drawArms);
        const char* arms[] = {"Original stretching arms", "Natural reach (as stretching for now)", "Floating forearms"};
        ImGui::Combo("Arm style", &gCfg.armStyle, arms, 3);
        ImGui::Checkbox("Block items stand as cubes", &gCfg.cubeItems);
        ImGui::Checkbox("Lean in at stations without a scene yet (testing)", &gCfg.devScenes);
        ImGui::Checkbox("Hide the vanilla hand in scenes", &gCfg.hideHand);
        ImGui::Checkbox("Narrow the view (field of view)", &gCfg.writeFov);
        ImGui::Checkbox("Hidden screen cursor follows the hovered slot", &gCfg.hoverTargets);
        ImGui::SliderFloat("Slot background key tolerance", &gCfg.keyTolerance, 0.0f, 0.15f, "%.3f");
        ImGui::Checkbox("Dump the hidden screen + found slots (logs/uicapture)", &gCfg.dumpUi);
        ImGui::TreePop();
    }
    {
        std::lock_guard l(gLayoutMutex);
        ImGui::TextDisabled("slots: %s", gLayout.note.c_str());
    }
    {
        std::lock_guard l(gM);
        ImGui::TextDisabled("last screen: %s; scenes %ld; renderer: %s", gS.lastRoute.c_str(), gS.scenes, gG.status.c_str());
    }
    if (ImGui::Button("Save##cine")) saveConfig();
    ImGui::TreePop();
}

void appendDiag(char* b, unsigned long long cap) {
    size_t len = std::strlen(b);
    std::lock_guard l(gM);
    std::lock_guard l2(gLayoutMutex);
    len += std::snprintf(b + len, cap - len,
                         "cinematic: phase %d time %.2f weight %.2f; station %s at %d,%d,%d side %d; last route: %s; refused: %s\n"
                         "  layout: %s (valid %d, %dx%d, slot %.0f, grid %d result %d, bg %.3f %.3f %.3f)\n"
                         "  cursor ndc %.2f,%.2f -> mapped %.0f,%.0f target %d hoverCell %d result %d drawer %d held %u; cells full %d%d%d%d%d%d%d%d%d result %d\n"
                         "  camera: pose %.2f,%.2f,%.2f yaw %.1f pitch %.1f roll %.1f, tanHalf %.3f aspect %.3f fov %.1f vanilla %.1f; renderer %s\n",
                         int(gS.phase), gS.time, gS.weight, kNames[int(gS.st.kind)], gS.st.bx, gS.st.by, gS.st.bz, int(gS.st.side), gS.lastRoute.c_str(),
                         gS.refused.c_str(), gLayout.note.c_str(), gLayout.valid, gLayout.W, gLayout.H, gLayout.slot, gLayout.hasGrid, gLayout.hasResult,
                         gLayout.bg[0], gLayout.bg[1], gLayout.bg[2], gCursorNdcX, gCursorNdcY, gMapX, gMapY, gTargetNow.load(), gS.hoverCell,
                         gS.hoverResult, gS.hoverDrawer, gHeld.load(), gS.cellFull[0], gS.cellFull[1], gS.cellFull[2], gS.cellFull[3], gS.cellFull[4],
                         gS.cellFull[5], gS.cellFull[6], gS.cellFull[7], gS.cellFull[8], gS.resultFull, gS.last.pos.x, gS.last.pos.y, gS.last.pos.z,
                         gS.last.yaw, gS.last.pitch, gS.last.roll, gS.tanHalf, gS.aspect, gS.fovY, gS.vanillaFovY, gG.status.c_str());
    len += std::snprintf(b + len, cap - len, "  block states: %s; lit %d; aimTop %d\n", gS.states.c_str(), gS.st.lit, gS.aimTop);
    if (len < cap)
        len += std::snprintf(b + len, cap - len, "  side %d; wheel %.3f %.3f %.3f; grind target %d hover %d place %d; glint %d%d%d; spin %.2f; props: %s; keep copies %ld checked %ld\n"
                             "  move on %d at %.2f (clock %.2f) length %.2f beat %.2f done %d; %s; buttons %d; drawer expand %.2f over %d live %ld at %d,%d\n", int(gS.st.side),
                             gS.st.wheel.x, gS.st.wheel.y, gS.st.wheel.z, gS.grindTarget, gS.grindHover, gS.grindPlace, gS.cellGlint[0], gS.cellGlint[1],
                             gS.cellGlint[2], gS.spin, gPropNote.c_str(), gKeepCopies.load(), gKeepChecked, gS.moveOn, gS.moveAt, gS.clock,
                             gS.moveLength, gS.moveBeat, gS.moveBeatDone, gActionNote.c_str(), gLayout.nButtons, gS.expand, gS.overPanel, gLiveCalls, gLiveX, gLiveY);
    for (int i = 0; i < 12 && len < cap; ++i) {
        auto const& e = gPressLog[(gPressLogN + i) % 12];
        if (!e.empty()) len += std::snprintf(b + len, cap - len, "  press: %s\n", e.c_str());
    }
    if (len < cap) {
        int ok = 0, tried = 0;
        for (auto const& c : gS.tipCache) { ok += c.ok; tried += c.tried; }
        auto const& c = gS.tipCache[std::max(0, gS.tipSlot)];
        len += std::snprintf(b + len, cap - len, "  item tooltip: slot %d (%.2f s) probe %d at %.0f,%.0f unit %.1f; looks %d hits %d; cached ok %d tried %d; this one ok %d text %.0f,%.0f %.0fx%.0f\n",
                             gS.tipSlot, gS.clock - gS.tipSince, gS.probe, gProbeX.load(), gProbeY.load(), gProbeUnit.load(), gProbeLooks.load(), gProbeHits.load(),
                             ok, tried, c.ok, c.text.x, c.text.y, c.text.w, c.text.h);
    }
    if (gLayout.valid)
        len += std::snprintf(b + len, cap - len, "  hotbar[0] %.0f,%.0f  inv[0] %.0f,%.0f  grid[0] %.0f,%.0f grid[8] %.0f,%.0f  result %.0f,%.0f %.0f\n",
                             gLayout.hotbar[0].x, gLayout.hotbar[0].y, gLayout.inv[0].x, gLayout.inv[0].y, gLayout.grid[0].x, gLayout.grid[0].y,
                             gLayout.grid[8].x, gLayout.grid[8].y, gLayout.result.x, gLayout.result.y, gLayout.result.w);
}

} // namespace rf::cine
