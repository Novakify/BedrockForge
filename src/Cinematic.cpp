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
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

#include <d3d12.h>
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")
#include <wrl/client.h>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "CinematicGame.h"
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
bool sceneAvailable(Kind k) { return k == Kind::Table || isFurnace(k); }

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
    V3 center() const { return kind == Kind::Table ? top : V3{bx + 0.5, by + 0.5, bz + 0.5}; }
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
    double leanFov() const { return 66; }
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
        constexpr double LEAN_BACK = 0.86, LEAN_UP = 0.72, LOOK_BACK = -0.04;
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
    bool  hasBoard = false;
    int   kind = 0;                  // Kind the layout was found for
    Rect  panel;                    // container panel (inventory rows' width)
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
    bool    resultBlock = false;
    float   iconBox[11][4] = {};  // per sampled slot: x0 y0 x1 y1 in 0..1 of the slot inside
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
    std::string lastRoute = "-";
    long    scenes = 0;
} gS;
std::mutex gM; // gS

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
Dir freeSide(Station const& st, V3 feet) {
    double dx = feet.x - (st.bx + 0.5), dz = feet.z - (st.bz + 0.5);
    for (int pass = 0; pass < 2; ++pass) {
        int    best  = -1;
        double score = -1e9;
        for (int d = 0; d < 4; ++d) {
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
    st.side = freeSide(st, feet);
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
        JPose base = tether(breathe(gS.st.view(), smoother(gS.time / 0.6)), livePos);
        pose       = base; // kick: no dramatic moments at the table; FreeLook not ported (Left Alt)
        gS.weight  = 1;
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
enum class PressState { Idle, Hover, Down, ForceDown };
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

/// Target ids: 0..8 grid (recipe order), 9 result, 10..18 hotbar, 19..45 pack, -1 none
int currentTarget(float mx, float my) {
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
        V3     face = gS.st.center(), n = gS.st.sideV();
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
        RD(dumpUi) RD(keyTolerance) RD(fullLean) RD(armStyle) RD(drawArms) RD(cubeItems)
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
    WR(dumpUi) WR(keyTolerance) WR(fullLean) WR(armStyle) WR(drawArms) WR(cubeItems)
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
    if (!sceneAvailable(kind)) { gS.lastRoute = std::string(kNames[int(kind)]) + ": scene not ported yet, Spatial"; return Route::Spatial; }
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
        double fov     = vanilla + (gS.st.leanFov() - vanilla) * (gS.weight * pct(gCfg.sceneZoom));
        fovScale       = float(fov / std::max(1.0, vanilla));
    }
    gS.fovScaleNow = fovScale;
    return true;
}

// ================================================================ public: input
bool mapClient(int w, int h, int& x, int& y) {
    std::lock_guard l(gM);
    if (gS.phase != Phase::Intro && gS.phase != Phase::Active) return false;
    gW = w;
    gH = h;
    gCursorNdcX = 2.0f * x / std::max(1, w) - 1.0f;
    gCursorNdcY = 1.0f - 2.0f * y / std::max(1, h);
    gCursorValid = true;
    if (!gS.cursorEngaged && gS.phase == Phase::Active && gS.time < 0.05) { gS.cursorStartX = float(x); gS.cursorStartY = float(y); }
    Layout L;
    {
        std::lock_guard ll2(gLayoutMutex);
        L = gLayout;
    }
    int t = currentTarget(float(x), float(y));
    gTargetNow = t;
    double now = nowMs();
    // a press goes to its target, a drag over other targets follows them (sharing a stack), the release lands where
    // the press is; otherwise the hidden screen's cursor waits in an empty corner
    int  use = gPress.state != PressState::Idle ? (gPress.state == PressState::Down && t >= 0 ? t : gPress.target) : t;
    bool grace = gPress.state == PressState::Idle && now - gPress.releasedAt < 150 && gPress.target >= 0;
    if (grace) use = gPress.target;
    bool mapToTarget = use >= 0 && (gCfg.hoverTargets || gPress.state != PressState::Idle || grace);
    if (gPress.state == PressState::Down && t >= 0) gPress.target = t;
    Rect r;
    float fx, fy;
    if (L.valid && L.kind == int(gS.st.kind) && mapToTarget && targetRect(L, use, r)) { fx = r.cx(); fy = r.cy(); }
    else if (L.valid) neutralPoint(L, fx, fy);
    else { fx = float(w) - 100.0f; fy = 60.0f; }
    x = int(fx);
    y = int(fy);
    gMapX = fx; gMapY = fy; gMapValid = true;
    return true;
}

unsigned filterButtons(unsigned buttons) {
    std::lock_guard l(gM);
    if (gS.phase != Phase::Intro && gS.phase != Phase::Active) { gPress = Press{}; return buttons; }
    double   now = nowMs();
    unsigned out = 0;
    unsigned bit = gPress.bit;
    switch (gPress.state) {
    case PressState::Idle: {
        int t = gTargetNow.load();
        for (unsigned b = 1; b <= 4; b <<= 1) {
            if (!(buttons & b) || (gPrevButtons & b)) continue; // a new press
            if (gS.phase == Phase::Active && t >= 0) {
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
            char e[120];
            snprintf(e, sizeof e, "%.0f ms: real %x -> passed %x, state %d target %d at %.0f,%.0f", now, buttons & 7u, out, int(gPress.state),
                     gPress.target, gMapX, gMapY);
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
};
Texture2D    tUi   : register(t0);
Texture2D    tSkin : register(t1);
SamplerState sP    : register(s0);
struct V2P { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
V2P VS(uint id : SV_VertexID) {
    static const uint idx[6] = {0, 1, 2, 2, 1, 3};
    uint k = idx[id];
    V2P o;
    o.pos = corner[k];
    o.uv  = k == 0 ? uv01.xy : k == 1 ? uv01.zw : k == 2 ? uv23.xy : uv23.zw;
    return o;
}
float4 PS(V2P i) : SV_Target {
    if (mode.x < 0.5) return (mode.y > 0.5 ? float4(color.rgb * color.a, 0) : float4(color.rgb * color.a, color.a)) * mode.w; // y: additive light
    if (mode.x > 1.5) {
        float4 k = tSkin.SampleLevel(sP, i.uv, 0);
        if (k.a < 0.1) discard;
        return float4(k.rgb * color.rgb, 1.0) * mode.w;
    }
    float4 u = tUi.SampleLevel(sP, i.uv, 0);
    if (mode.y > 0.5 && u.a > 0.0) {
        float3 c = u.rgb / max(u.a, 1e-3);
        if (all(abs(c - key0.rgb) < mode.z) || all(abs(c - key1.rgb) < mode.z)) u = 0;
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

bool createPipeline(ID3D12Device* dev, DXGI_FORMAT fmt) {
    if (gG.pso && gG.fmt == fmt) return true;
    gG.dev = dev;
    if (!gG.root) {
        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0};
        D3D12_ROOT_PARAMETER   p[2]{};
        p[0].ParameterType    = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        p[0].Constants        = {0, 0, 40};
        p[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        p[1].ParameterType    = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        p[1].DescriptorTable  = {1, &range};
        p[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC s{};
        s.Filter   = D3D12_FILTER_MIN_MAG_MIP_POINT;
        s.AddressU = s.AddressV = s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        s.MaxLOD   = D3D12_FLOAT32_MAX;
        s.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC rs{2, p, 1, &s, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ComPtr<ID3DBlob> blob, err;
        HRESULT hr = D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
        if (FAILED(hr)) return fail("D3D12SerializeRootSignature", hr);
        if (FAILED(hr = dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&gG.root))))
            return fail("CreateRootSignature", hr);
        D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
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

bool ensureKeep(ID3D12Device* dev, D3D12_RESOURCE_DESC const& ud) {
    if (gG.keep && gG.kw == UINT(ud.Width) && gG.kh == ud.Height && gG.kfmt == ud.Format) return true;
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC   d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = ud.Width; d.Height = ud.Height; d.DepthOrArraySize = 1; d.MipLevels = 1; d.Format = ud.Format; d.SampleDesc.Count = 1;
    ComPtr<ID3D12Resource> k;
    HRESULT hr = dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&k));
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
    static std::vector<ComPtr<ID3D12Resource>> graveyard;
    if (gG.skinTex) graveyard.push_back(gG.skinTex);
    if (gG.skinUp) graveyard.push_back(gG.skinUp);
    gG.skinTex.Reset();
    gG.skinUp.Reset();
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC   d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = UINT(w); d.Height = UINT(h); d.DepthOrArraySize = 1; d.MipLevels = 1; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&gG.skinTex))))
        return fail("CreateCommittedResource(skin)", E_FAIL);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT64 total = 0;
    dev->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &total);
    D3D12_HEAP_PROPERTIES up{D3D12_HEAP_TYPE_UPLOAD};
    D3D12_RESOURCE_DESC   bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = total; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(dev->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gG.skinUp))))
        return fail("CreateCommittedResource(skin upload)", E_FAIL);
    void* m = nullptr;
    if (FAILED(gG.skinUp->Map(0, nullptr, &m)) || !m) return fail("Map(skin upload)", E_FAIL);
    for (int y = 0; y < h; ++y) std::memcpy(static_cast<uint8_t*>(m) + fp.Offset + size_t(y) * fp.Footprint.RowPitch, &px[size_t(y) * w * 4], size_t(w) * 4);
    gG.skinUp->Unmap(0, nullptr);
    D3D12_TEXTURE_COPY_LOCATION dst{gG.skinTex.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
    dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION srcl{gG.skinUp.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
    srcl.PlacedFootprint = fp;
    list->CopyTextureRegion(&dst, 0, 0, 0, &srcl, nullptr);
    barrier(list, gG.skinTex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    auto h1 = gG.srvHeap->GetCPUDescriptorHandleForHeapStart();
    h1.ptr += gG.srvInc;
    dev->CreateShaderResourceView(gG.skinTex.Get(), &sv, h1);
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
                bool inGrid = (nearSq(q, -pitch, 0) && nearSq(q, pitch, 0)) || (nearSq(q, pitch, 0) && nearSq(q, 2 * pitch, 0) && nearSq(q, 3 * pitch, 0)) ||
                              (nearSq(q, -pitch, 0) && nearSq(q, -2 * pitch, 0) && nearSq(q, -3 * pitch, 0));
                (inGrid ? buttons : own).push_back(q);
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
            L.nRoles  = n;
            L.hasGrid = n == expect;
            if (kind == Kind::Enchanting && n == 2) {
                // the three enchantment buttons fill the panel right of the lapis slot
                float x0 = std::max(L.grid[0].x, L.grid[1].x) + L.grid[1].w + unit * 4, y0 = L.grid[0].y - 3.0f * s;
                L.board  = {x0, std::max(0.0f, y0 - s * 0.4f), px1 - x0, 3.6f * s};
                L.hasBoard = true;
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
constexpr int kSampled = 11; // 9 grid + result + the parked cursor (carried stack)
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
    else { float x, y; neutralPoint(L, x, y); r = {x - L.slot * 0.5f, y - L.slot * 0.5f, L.slot, L.slot}; return true; }
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
            bool  full[kSampled] = {}, block[kSampled] = {};
            float box[kSampled][4] = {};
            float tol  = 10.0f / 255.0f;
            bool  bgra = gG.kfmt == DXGI_FORMAT_B8G8R8A8_UNORM || gG.kfmt == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            int   n    = int(cell);
            std::vector<uint8_t> mask(size_t(n) * n);
            for (int s = 0; s < kSampled; ++s) {
                int other = 0, total = 0, x0 = n, y0 = n, x1 = -1, y1 = -1;
                for (int y = 0; y < n; ++y)
                    for (int x = 0; x < n; ++x) {
                        uint8_t const* q = static_cast<uint8_t*>(p) + size_t(s) * gG.srStride + size_t(y) * gG.srPitch + size_t(x) * 4;
                        float a = q[3] / 255.0f;
                        bool  item;
                        if (s == 10) item = a > 0.5f; // the parked cursor sits on transparent ground
                        else {
                            float r = (bgra ? q[2] : q[0]) / 255.0f, g = q[1] / 255.0f, b = (bgra ? q[0] : q[2]) / 255.0f;
                            if (a > 0.01f) { r /= a; g /= a; b /= a; }
                            bool bg   = std::fabs(r - L.bg[0]) < tol && std::fabs(g - L.bg[1]) < tol && std::fabs(b - L.bg[2]) < tol;
                            bool hi   = std::fabs(r - L.hi[0]) < tol && std::fabs(g - L.hi[1]) < tol && std::fabs(b - L.hi[2]) < tol;
                            item = a > 0.5f && !bg && !hi;
                        }
                        mask[size_t(y) * n + x] = item;
                        ++total;
                        if (item) { ++other; x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
                    }
                full[s] = other * 50 > total;
                if (full[s]) {
                    box[s][0] = float(x0) / n; box[s][1] = float(y0) / n; box[s][2] = float(x1 + 1) / n; box[s][3] = float(y1 + 1) / n;
                    block[s] = isoBlock(mask, n, box[s]);
                }
            }
            D3D12_RANGE none{0, 0};
            gG.sr[i]->Unmap(0, &none);
            std::lock_guard l(gM);
            bool before = gS.resultFull;
            for (int s = 0; s < 9; ++s) { gS.cellFull[s] = full[s]; gS.cellBlock[s] = block[s]; }
            gS.resultFull  = L.hasResult && full[9];
            gS.resultBlock = block[9];
            gS.carrying     = full[10];
            gS.carriedBlock = block[10];
            for (int s = 0; s < kSampled; ++s) std::memcpy(gS.iconBox[s], box[s], sizeof box[s]);
            if (gS.resultFull != before) gS.resultChangedAt = gS.clock;
        }
        gG.srFrame[i] = -1;
    }
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
};
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
void keyed(Quad& q, Layout const& L, float opacity) {
    q.mode[0] = 1; q.mode[1] = 1; q.mode[2] = gCfg.keyTolerance; q.mode[3] = opacity;
    for (int i = 0; i < 3; ++i) { q.key0[i] = L.bg[i]; q.key1[i] = L.hi[i]; }
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
struct IconSrc { Rect inside; float box[4]; }; // slot inside (screen px) + icon bounding box in 0..1 of it
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
            keyed(q, L, opacity);
            gQuads.push_back(q);
        }
}
/// An item lying on a surface (flat) or standing as a cube (block). `facing` = the item's up on the surface.
void placeItem(JPose const& cam, Basis const& b, Layout const& L, IconSrc const& src, bool block, V3 base, V3 right, V3 out,
               double flatSize, double blockSize, float tiltDeg, float opacity) {
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
    setRect(q.uv, src.inside.x / L.W, src.inside.y / L.H, (src.inside.x + src.inside.w) / L.W, (src.inside.y + src.inside.h) / L.H);
    keyed(q, L, opacity);
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
    // TableStation.handTarget
    V3     reach = V3{aim.x - shoulder.x, 0, aim.z - shoulder.z}.norm();
    double hover = carrying ? 0.075 : 0.045;
    return V3{aim.x, gS.st.by + 1.0 + HAND_Y + hover - 0.06 * tap, aim.z} - reach * ITEM_REACH;
}
V3 heldItemPoint(V3 hand, V3 shoulder) {
    if (isFurnace(gS.st.kind) && !gS.aimTop) { // FurnaceStation.heldItemPoint on the front
        V3 reach = (hand - shoulder).norm();
        return hand + reach * 0.07 + V3{0, -0.045, 0};
    }
    V3 reach = V3{hand.x - shoulder.x, 0, hand.z - shoulder.z}.norm();
    return hand + reach * (isFurnace(gS.st.kind) ? 0.07 : ITEM_REACH) + V3{0, -HAND_Y + 0.012, 0};
}
double craftProgress() { double t = (gS.clock - gS.craftAt) / 0.5; return t >= 0 && t <= 1 ? t : -1; }

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
    // keep this frame's hidden screen
    if (ui && uiFresh) {
        barrier(list, gG.keep.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        barrier(list, ui, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyResource(gG.keep.Get(), ui);
        barrier(list, ui, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        barrier(list, gG.keep.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        gG.keepValid = true;
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
        float tableAlpha = gS.phase == Phase::Active ? float(smoother(gS.time / 0.4)) : gS.phase == Phase::Outro ? float(1 - smoother(gS.time / 0.3)) : float(smoother(gS.time / gS.leanLength));
        auto src = [&](Rect r, int sampled) {
            float   in = L.slot * 16.0f / 18.0f;
            IconSrc o{{r.cx() - in * 0.5f, r.cy() - in * 0.5f, in, in}, {0, 0, 1, 1}};
            if (sampled >= 0) std::memcpy(o.box, gS.iconBox[sampled], sizeof o.box);
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
            gS.expand  = approach(gS.expand, over ? 1 : 0, 13, gS.frameDt);
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
        list->SetGraphicsRoot32BitConstants(0, 40, &q, 0);
        list->DrawInstanced(6, 1, 0, 0);
    }
    gG.status = "drawing " + std::to_string(gQuads.size()) + " quads";
    return true;
}

// ================================================================ ImGui: hint line, F6
namespace {
// lang/en_us.json of the Java mod
char const* kZoneName[3][3] = {{"Upper opening \xE2\x80\x94 to smelt", "Fire box \xE2\x80\x94 fuel", "Done"},
                               {"Window \xE2\x80\x94 to cook", "Behind the grate \xE2\x80\x94 fuel", "Ready"},
                               {"Charging from the top \xE2\x80\x94 ore", "Front grate \xE2\x80\x94 fuel", "Sill \xE2\x80\x94 finished ingots"}};
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
    default: return "Esc step back";
    }
}
} // namespace

void drawOverlay() {
    std::string hint, tipTitle, tipLine;
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
    // the station's tooltip, vanilla style (dark violet box, gold title)
    if (!tipTitle.empty()) {
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
    for (int i = 0; i < 12 && len < cap; ++i) {
        auto const& e = gPressLog[(gPressLogN + i) % 12];
        if (!e.empty()) len += std::snprintf(b + len, cap - len, "  press: %s\n", e.c_str());
    }
    if (gLayout.valid)
        len += std::snprintf(b + len, cap - len, "  hotbar[0] %.0f,%.0f  inv[0] %.0f,%.0f  grid[0] %.0f,%.0f grid[8] %.0f,%.0f  result %.0f,%.0f %.0f\n",
                             gLayout.hotbar[0].x, gLayout.hotbar[0].y, gLayout.inv[0].x, gLayout.inv[0].y, gLayout.grid[0].x, gLayout.grid[0].y,
                             gLayout.grid[8].x, gLayout.grid[8].y, gLayout.result.x, gLayout.result.y, gLayout.result.w);
}

} // namespace rf::cine
