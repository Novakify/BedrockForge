// Bedrock Forge - MatchFx: the match state World FX reacts to (see MatchFx.h). Original code for Bedrock Forge.
#include "client/MatchFx.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/BlockChangedEvent.h"

#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/math/Color.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BedBlockActor.h"
#include "mc/world/level/block/actor/BlockActor.h"

#include "bf/Events.h"

namespace {
using namespace std::chrono;
int64_t nowMs() { return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count(); }

BfMatchFxV1          gFx{};
std::mutex           gMutex; // block events arrive on the server thread too (single-player)
std::atomic<int64_t> gConsumerMs{0};
std::atomic<int64_t> gHudMs{0};
std::atomic<float>   gPreview{-1.0f};
bf::events::Subscriptions gSubs;

// Bedrock dye order (bed colour data): white, orange, magenta, light blue, yellow, lime, pink, gray, light gray,
// cyan, purple, blue, brown, green, red, black. Team light colours, a touch more saturated than the wool so they read
// as light, not paint.
float const kDye[16][3] = {
    {0.92f, 0.94f, 1.00f}, {1.00f, 0.48f, 0.10f}, {0.86f, 0.24f, 0.86f}, {0.30f, 0.62f, 1.00f},
    {1.00f, 0.84f, 0.16f}, {0.42f, 0.92f, 0.16f}, {1.00f, 0.44f, 0.66f}, {0.50f, 0.52f, 0.58f},
    {0.74f, 0.76f, 0.80f}, {0.10f, 0.80f, 0.86f}, {0.58f, 0.22f, 1.00f}, {0.20f, 0.34f, 1.00f},
    {0.62f, 0.38f, 0.20f}, {0.22f, 0.70f, 0.18f}, {1.00f, 0.16f, 0.12f}, {0.34f, 0.30f, 0.44f},
};

struct Bed { int x, y, z; int dye = 14; bool alive = true; int64_t changed = 0; };
std::vector<Bed> gBeds;
uint32_t         gBroken = 0;
float            gTarget = 0, gPhase = 0;
double           gAnchor[3]{};
bool             gAnchored = false;
// incremental scan for beds around the player (a few thousand blocks per frame)
int gScanI = 0;
constexpr int kScanR = 112, kScanDown = 40, kScanUp = 24, kPerFrame = 12000;

bool isBed(Block const& b) { return b.getTypeName() == "minecraft:bed"; }
bool boring(std::string const& n) {
    return n == "minecraft:air" || n.find("water") != std::string::npos || n.find("lava") != std::string::npos ||
           n == "minecraft:fire" || n == "minecraft:soul_fire" || n.find("structure_void") != std::string::npos;
}

Bed* bedNear(int x, int y, int z) { // a bed is two blocks: both halves are one bed
    for (auto& b : gBeds)
        if (std::abs(b.x - x) <= 1 && std::abs(b.y - y) <= 1 && std::abs(b.z - z) <= 1) return &b;
    return nullptr;
}

void retarget() { // living match sky: start golden, first bed down -> dusk, one (or no) bed left -> night
    int alive = 0;
    for (auto& b : gBeds) alive += b.alive ? 1 : 0;
    float t = 0;
    if (gBroken >= 1) t = 1;
    if (gBroken >= 1 && gBeds.size() >= 2 && alive <= 1) t = 2;
    gTarget = std::max(gTarget, t); // never back to day inside one match
}

void onBroken(int x, int y, int z) {
    if (Bed* b = bedNear(x, y, z); b && b->alive) {
        b->alive = false, b->changed = nowMs();
        ++gBroken;
        retarget();
    }
}

void pushEvent(BlockSource& bs, int x, int y, int z, uint32_t kind, Block const& b) {
    auto& e = gFx.events[gFx.nEvents % 64];
    e.x = x, e.y = y, e.z = z, e.kind = kind, e.atMs = nowMs();
    mce::Color c = b.getBlockType().mMapColor;
    e.col[0] = c.r, e.col[1] = c.g, e.col[2] = c.b, e.col[3] = 1;
    e.seed = uint32_t(x * 73856093) ^ uint32_t(y * 19349663) ^ uint32_t(z * 83492791) ^ uint32_t(e.atMs);
    e.floorDy = 255;
    for (int d = 1; d <= 6; ++d) { // what the pieces will land on
        Block const& below = bs.getBlock(BlockPos{x, y - d, z});
        if (!below.isAir() && !boring(below.getTypeName())) { e.floorDy = uint32_t(d - 1); break; }
    }
    ++gFx.nEvents;
}

void onChange(ll::event::BlockChangedEvent& ev) {
    if (ev.layer() != 0) return;
    auto const& p    = ev.pos();
    auto const& prev = ev.previousBlock();
    auto const& next = ev.newBlock();
    std::lock_guard l(gMutex);
    if (!gAnchored) return;
    double const dx = p.x + 0.5 - gAnchor[0], dy = p.y + 0.5 - gAnchor[1], dz = p.z + 0.5 - gAnchor[2];
    bool const near = dx * dx + dy * dy + dz * dz < 64.0 * 64.0;
    // single-player fires twice (server + client BlockSource): one event per block per 150 ms
    int64_t const now = nowMs();
    for (uint32_t k = 0; k < 64 && k < gFx.nEvents; ++k) {
        auto const& e = gFx.events[(gFx.nEvents - 1 - k) % 64];
        if (now - e.atMs > 150) break;
        if (e.x == p.x && e.y == p.y && e.z == p.z) return;
    }
    if (isBed(prev) && !isBed(next)) onBroken(p.x, p.y, p.z);
    if (isBed(next) && !bedNear(p.x, p.y, p.z) && gBeds.size() < 16) gBeds.push_back({p.x, p.y, p.z, 14, true, now});
    std::string const& pn = prev.getTypeName();
    std::string const& nn = next.getTypeName();
    if (!near) return;
    if (!boring(pn) && nn == "minecraft:air") pushEvent(ev.blockSource(), p.x, p.y, p.z, 1, prev);
    else if (pn == "minecraft:air" && !boring(nn)) pushEvent(ev.blockSource(), p.x, p.y, p.z, 2, next);
}

void resetMatch(double const* pos) {
    gBeds.clear();
    gBroken = 0;
    gTarget = gPhase = 0;
    gScanI  = 0;
    gFx.nEvents = 0;
    gAnchor[0] = pos[0], gAnchor[1] = pos[1], gAnchor[2] = pos[2];
    gAnchored = true;
}

void scan(BlockSource& bs) { // walk a box around the anchor a slice at a time, note every bed (and its dye)
    int const sx = 2 * kScanR + 1, sy = kScanDown + kScanUp + 1;
    long const total = long(sx) * sx * sy;
    int const ax = int(std::floor(gAnchor[0])), ay = int(std::floor(gAnchor[1])), az = int(std::floor(gAnchor[2]));
    for (int n = 0; n < kPerFrame; ++n) {
        long i = gScanI;
        gScanI = int((gScanI + 1) % total);
        int const y = ay - kScanDown + int(i % sy);
        long r = i / sy;
        int const x = ax - kScanR + int(r % sx), z = az - kScanR + int(r / sx);
        if (y < -64 || y > 319) continue;
        BlockPos const bp{x, y, z};
        Block const&   b = bs.getBlock(bp);
        if (!isBed(b)) continue;
        Bed* bed = bedNear(x, y, z);
        if (!bed) {
            if (gBeds.size() >= 16) continue;
            gBeds.push_back({x, y, z, 14, true, nowMs()});
            bed = &gBeds.back();
        }
        if (auto* ba = bs.getBlockEntity(bp)) bed->dye = std::clamp(int(static_cast<BedBlockActor*>(ba)->mDyeColor), 0, 15);
        if (!bed->alive) { bed->alive = true, bed->changed = nowMs(); } // a new map reused the spot
    }
}
} // namespace

extern "C" __declspec(dllexport) BfMatchFxV1 const* BF_GetMatchFx() {
    std::lock_guard l(gMutex);
    gFx.size = sizeof gFx, gFx.version = 1;
    gFx.active = nowMs() - gHudMs.load() < 250 ? 1u : 0u;
    gFx.nowMs  = nowMs();
    return &gFx;
}
extern "C" __declspec(dllexport) void BF_MatchFxConsumer(int on) { gConsumerMs = on ? nowMs() : 0; }
extern "C" __declspec(dllexport) void BF_MatchFxPreview(float phase) { gPreview = phase; }

namespace bedrock_forge::client::matchfx {

void install() { gSubs.on<ll::event::BlockChangedEvent>([](ll::event::BlockChangedEvent& ev) { try { onChange(ev); } catch (...) {} }); }
void uninstall() { gSubs.clear(); }

void frame(IClientInstance& client, LocalPlayer* lp) {
    (void)client;
    if (!lp) return;
    int64_t const now  = nowMs();
    int64_t const last = gHudMs.exchange(now);
    float const   dt   = last ? std::clamp(float(now - last) / 1000.0f, 0.0f, 0.1f) : 0.0f;
    auto const&   p    = lp->getPosition();
    double const  pos[3]{p.x, p.y, p.z};
    std::lock_guard l(gMutex);
    // a new match / world: a big sideways jump (Hive sends you to a new map) or a long time out of the world
    if (!gAnchored || std::hypot(pos[0] - gAnchor[0], pos[2] - gAnchor[2]) > 1500.0 || now - last > 20000) resetMatch(pos);
    scan(lp->getDimensionBlockSource());
    retarget();
    // the light leaves slowly: ~20 s golden -> dusk, ~25 s dusk -> night
    float const rate = gTarget > gPhase ? (gPhase < 1.0f ? 1.0f / 20.0f : 1.0f / 25.0f) : 1.0f / 3.0f;
    if (gPhase < gTarget) gPhase = std::min(gTarget, gPhase + rate * dt);
    else gPhase = std::max(gTarget, gPhase - rate * dt);
    gFx.phase = gPhase, gFx.phaseTarget = gTarget, gFx.bedsBroken = gBroken;
    gFx.nTeams = uint32_t(std::min<size_t>(gBeds.size(), 16));
    for (uint32_t i = 0; i < gFx.nTeams; ++i) {
        auto const& b = gBeds[i];
        auto&       t = gFx.teams[i];
        t.pos[0] = b.x + 0.5f, t.pos[1] = b.y + 0.6f, t.pos[2] = b.z + 0.5f;
        std::memcpy(t.col, kDye[b.dye], sizeof t.col);
        t.alive = b.alive ? 1u : 0u, t.changedMs = b.changed;
    }
    ++gFx.frame;
    // the game's own time of day follows: golden hour, sunset, moonlit night (vanilla: 0 noon, 0.25 sunset, 0.5 midnight)
    float const k = gPreview.load() >= 0.0f ? std::min(gPreview.load(), 2.0f) : gPhase;
    gFx.timeOfDay = k < 1.0f ? 0.205f + 0.065f * k : 0.27f + 0.08f * (k - 1.0f);
}

void onText(std::string const& m) {
    std::string s;
    s.reserve(m.size());
    for (size_t i = 0; i < m.size(); ++i) {
        if (uint8_t(m[i]) == 0xC2 && i + 1 < m.size() && uint8_t(m[i + 1]) == 0xA7) { i += 2; continue; } // §x
        s += char(std::tolower(uint8_t(m[i])));
    }
    std::lock_guard l(gMutex);
    if (s.find("bed") != std::string::npos && (s.find("destroy") != std::string::npos || s.find("broke") != std::string::npos)) {
        if (gBroken == 0) gBroken = 1; // a bed we couldn't see went down
        retarget();
    }
    if (s.find("all beds") != std::string::npos || s.find("bed destruction") != std::string::npos ||
        s.find("sudden death") != std::string::npos)
        gTarget = 2;
}

bool timeOverride(float& t) {
    if (nowMs() - gConsumerMs.load() > 500) return false;
    t = gFx.timeOfDay;
    return true;
}
} // namespace bedrock_forge::client::matchfx
