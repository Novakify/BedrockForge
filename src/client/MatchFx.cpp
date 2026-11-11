// Bedrock Forge - MatchFx: the match state World FX reacts to (see MatchFx.h). Original code for Bedrock Forge.
#include "client/MatchFx.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/client/ClientJoinLevelEvent.h"

#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
#include "mc/deps/core/math/Color.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BedBlockActor.h"
#include "mc/world/level/block/actor/BlockActor.h"

#include "bf/Events.h"
extern "C" __declspec(dllimport) short __stdcall GetAsyncKeyState(int vKey);
#include "ll/api/mod/NativeMod.h"

namespace {
using namespace std::chrono;
// diagnostics straight to <mod>/logs/matchfx.txt, flushed per line (the game's log is buffered and loses its tail)
template <class... A>
void note(std::format_string<A...> f, A&&... a) {
    try {
        std::string line = std::format(f, std::forward<A>(a)...);
        static std::filesystem::path file = [] {
            auto dir = ll::mod::NativeMod::current()->getModDir() / "logs";
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            return dir / "matchfx.txt";
        }();
        std::time_t t = std::time(nullptr);
        std::tm tm{};
        localtime_s(&tm, &t);
        std::ofstream(file, std::ios::app | std::ios::binary) << std::format("{:02}:{:02}:{:02} ", tm.tm_hour, tm.tm_min, tm.tm_sec) << line << "\n";
    } catch (...) {}
}
int64_t nowMs() { return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count(); }

BfMatchFxV1          gFx{};
std::mutex           gMutex; // block events arrive on the server thread too (single-player)
std::atomic<int64_t> gConsumerMs{0};
std::atomic<int64_t> gHudMs{0};
std::atomic<float>   gPreview{-1.0f};
std::atomic<bool>    gNewLevel{false}; // joined a world (Hive moves you hub -> lobby -> map this way)
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

struct Bed { int x, y, z; int dye = 14; bool alive = true; int64_t changed = 0; bool dyeKnown = false; bool elim = false; int64_t elimMs = 0; float threat = 0; };
std::vector<Bed> gBeds;
uint32_t         gBroken = 0;
int64_t          gResetMs = 0;
bool             gIsMap = false;   // a Bed Wars map: a bed by where you arrived + beds spread over islands, or a bed broke
bool             gSpawnBed = false; // a bed next to where you arrived
float            gTarget = 0, gPhase = 0;
double           gAnchor[3]{};
bool             gAnchored = false;
// incremental scan for beds around the player (a few thousand blocks per frame)
int gScanI = 0;
bool gScanPaused = false;
double gScanC[3]{}; // centre of the current scan pass (the player's position when it started)
constexpr int kScanR = 160, kScanDown = 40, kScanUp = 24, kPerFrame = 12000;

// a bed: vanilla's, or a server's own bed block (any "...bed..." block that is not bedrock)
bool isBedName(std::string const& n) {
    if (n == "minecraft:bed") return true;
    if (n.find("bed") == std::string::npos || n.find("bedrock") != std::string::npos) return false;
    if (n.find("gen") != std::string::npos) return false;  // The Hive: hive:bed_gen_hitbox is the generator by the bed
    return n.find("minecraft:") != 0; // vanilla's flower beds etc. are not team beds; The Hive's is hive:bed_hitbox
}
bool isBed(Block const& b) { return isBedName(b.getTypeName()); }
// a server bed that is not vanilla's has no dye: take the team colour from the coloured blocks around it (The Hive lays
// the team's carpet / wool under and around the bed)
int dyeOfName(std::string const& n) {
    static char const* kNames[16] = {"white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray",
                                     "light_gray", "cyan", "purple", "blue", "brown", "green", "red", "black"};
    auto c = n.find(':');
    std::string base = c == std::string::npos ? n : n.substr(c + 1);
    for (char const* suffix : {"_carpet", "_wool", "_concrete", "_stained_glass", "_terracotta"}) {
        auto k = base.rfind(suffix);
        if (k == std::string::npos || k + std::strlen(suffix) != base.size()) continue;
        std::string col = base.substr(0, k);
        if (col == "silver") col = "light_gray";
        for (int i = 0; i < 16; ++i)
            if (col == kNames[i]) return i;
    }
    return -1;
}
int dyeAround(BlockSource& bs, int x, int y, int z) {
    int votes[16]{};
    for (int dy = -2; dy <= 1; ++dy)
        for (int dz = -3; dz <= 3; ++dz)
            for (int dx = -3; dx <= 3; ++dx) {
                int d = dyeOfName(bs.getBlock(BlockPos{x + dx, y + dy, z + dz}).getTypeName());
                if (d >= 0) ++votes[d];
            }
    int best = -1, n = 0;
    for (int i = 0; i < 16; ++i)
        if (votes[i] > n) n = votes[i], best = i;
    return best;
}
std::vector<std::string> gSeenNames; // diagnostics: every bed-like block name seen (logged once each)
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
    if (gBroken >= 1 && gIsMap) t = 1;
    if (gBroken >= 1 && gBeds.size() >= 2 && alive <= 1) t = 2;
    gTarget = std::max(gTarget, t); // never back to day inside one match
}

void onBroken(int x, int y, int z) {
    if (Bed* b = bedNear(x, y, z); b && b->alive) {
        b->alive = false, b->changed = nowMs();
        ++gBroken;
        gIsMap = true;
        note("bed at {} {} {} broken ({} so far)", b->x, b->y, b->z, gBroken);
        retarget();
    }
}

void pushEvent(BlockSource& bs, int x, int y, int z, uint32_t kind, Block const& b) {
    auto& e = gFx.events[gFx.nEvents % 256];
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
    double const dx = p.x + 0.5 - gScanC[0], dy = p.y + 0.5 - gScanC[1], dz = p.z + 0.5 - gScanC[2];
    double const d2 = dx * dx + dy * dy + dz * dz;
    bool const near = d2 < 72.0 * 72.0;     // shards: only what you can see break
    bool const seen = d2 < 220.0 * 220.0;   // placements: the whole map (bridge trails show rushes coming)
    // single-player fires twice (server + client BlockSource): one event per block per 150 ms
    int64_t const now = nowMs();
    for (uint32_t k = 0; k < 64 && k < gFx.nEvents; ++k) {
        auto const& e = gFx.events[(gFx.nEvents - 1 - k) % 256];
        if (now - e.atMs > 150) break;
        if (e.x == p.x && e.y == p.y && e.z == p.z) return;
    }
    if (isBed(prev) && !isBed(next)) onBroken(p.x, p.y, p.z);
    if (isBed(next) && !bedNear(p.x, p.y, p.z) && gBeds.size() < 16) gBeds.push_back({p.x, p.y, p.z, 14, true, now});
    std::string const& pn = prev.getTypeName();
    std::string const& nn = next.getTypeName();
    if (!seen) return;
    if (near && !boring(pn) && nn == "minecraft:air") pushEvent(ev.blockSource(), p.x, p.y, p.z, 1, prev);
    else if (pn == "minecraft:air" && !boring(nn)) pushEvent(ev.blockSource(), p.x, p.y, p.z, 2, next);
}

// ---- kills. A remote server only tells the client two things: a chat line ("FINAL KILL! a eliminated b", "b was
// killed by a", "You were killed by a") and the victim being moved (respawn / spectator teleport) or removed. Neither
// alone is enough (names come in any order; pearls teleport too), together they are: the player named in a kill line
// who jumped or vanished within ~2 s is the victim, and where they stood just before is where they died.
struct Track { std::string name; float pos[3]; float prev[3]; int dye = -1; int64_t seen = 0; bool local = false; };
struct Gone { std::string name; float pos[3]; int dye; int64_t at; bool local; };
struct KillLine { std::string text; bool fin; bool you; int64_t at; std::vector<Gone> cands; }; // cands: players it names, where they stood
std::vector<Track>    gTracks;
std::vector<Gone>     gGone;  // players that jumped / vanished in the last ~2.5 s
std::vector<KillLine> gKills; // kill lines in the last ~2.5 s, not matched yet

std::string lower(std::string const& m) {
    std::string s;
    for (size_t i = 0; i < m.size(); ++i) {
        if (uint8_t(m[i]) == 0xC2 && i + 1 < m.size() && uint8_t(m[i + 1]) == 0xA7) { i += 2; continue; }
        if (uint8_t(m[i]) == 0xA7) { ++i; continue; }
        s += char(std::tolower(uint8_t(m[i])));
    }
    return s;
}
// the team colour a name tag is drawn in (The Hive colours names by team): its first colour code -> dye
int dyeOfTag(std::string const& t) {
    for (size_t i = 0; i + 1 < t.size(); ++i) {
        char c = 0;
        if (uint8_t(t[i]) == 0xC2 && i + 2 < t.size() && uint8_t(t[i + 1]) == 0xA7) c = char(std::tolower(uint8_t(t[i + 2])));
        else continue;
        switch (c) {
        case '0': return 15; case '1': return 11; case '2': return 13; case '3': return 9; case '4': return 14;
        case '5': return 10; case '6': return 1;  case '7': return 8;  case '8': return 7; case '9': return 11;
        case 'a': return 5;  case 'b': return 3;  case 'c': return 14; case 'd': return 6; case 'e': return 4;
        case 'f': return 0;  default: break;
        }
    }
    return -1;
}
void pushKill(Gone const& g0, bool fin) {
    static std::string lastName;
    static int64_t     lastAt = 0;
    int64_t const      t0 = nowMs();
    if (g0.name == lastName && t0 - lastAt < 3000) return; // "you died!" + "final kill! a eliminated you": one death
    lastName = g0.name, lastAt = t0;
    Gone g = g0;
    if (g.dye < 0) { // no colour on the name tag: the team whose bed is nearest (for you: the one by where you arrived)
        double best = 1e18;
        double const cx = g.local ? gAnchor[0] : g.pos[0], cz = g.local ? gAnchor[2] : g.pos[2];
        for (auto const& b : gBeds)
            if (b.dyeKnown) {
                double d = (b.x - cx) * (b.x - cx) + (b.z - cz) * (b.z - cz);
                if (d < best) best = d, g.dye = b.dye;
            }
    }
    auto& e = gFx.events[gFx.nEvents % 256];
    e.x = int32_t(std::floor(g.pos[0])), e.y = int32_t(std::floor(g.pos[1])), e.z = int32_t(std::floor(g.pos[2]));
    e.kind = fin ? 4u : 3u, e.atMs = nowMs();
    int const d = g.dye >= 0 ? g.dye : 0;
    e.col[0] = kDye[d][0], e.col[1] = kDye[d][1], e.col[2] = kDye[d][2], e.col[3] = g.local ? 1.0f : 0.0f;
    e.seed = uint32_t(e.atMs) * 2654435761u;
    e.floorDy = 255;
    ++gFx.nEvents;
    note("{} at {:.0f} {:.0f} {:.0f}: {} (team colour {})", fin ? "FINAL KILL" : "kill", g.pos[0], g.pos[1], g.pos[2], g.name, g.dye);
}
void matchKills(int64_t now) {
    for (auto k = gKills.begin(); k != gKills.end();) {
        bool done = false;
        // the named player who jumped away / vanished around the line is the victim, where they stood before
        for (auto g = gGone.begin(); g != gGone.end() && !done; ++g) {
            if (std::llabs(g->at - k->at) > 2500) continue;
            bool const named = (g->local && k->you) || (g->name.size() >= 3 && k->text.find(g->name) != std::string::npos);
            if (!named) continue;
            pushKill(*g, k->fin);
            gGone.erase(g);
            done = true;
        }
        // one player named (the killer is out of range, or "you were killed"): that is the victim, now
        if (!done && k->cands.size() == 1) { pushKill(k->cands[0], k->fin); done = true; }
        // two named and nobody moved yet: after 1 s go by the wording - "b was killed by a" names the victim first,
        // "final kill! a eliminated b" / "a killed b" names them last
        if (!done && k->cands.size() > 1 && now - k->at > 1000) {
            bool const passive = k->text.find(" by ") != std::string::npos;
            pushKill(passive ? k->cands.front() : k->cands.back(), k->fin);
            done = true;
        }
        if (done || now - k->at > 2500) k = gKills.erase(k);
        else ++k;
    }
    std::erase_if(gGone, [&](Gone const& g) { return now - g.at > 2500; });
}
void trackPlayers(LocalPlayer& lp, int64_t now) {
    auto const& me = lp.getPosition();
    for (auto& t : gTracks) t.prev[0] = t.pos[0], t.prev[1] = t.pos[1], t.prev[2] = t.pos[2];
    std::vector<char> hit(gTracks.size(), 0);
    lp.getLevel().forEachPlayer([&](Player& pl) {
        auto const q = static_cast<Actor&>(pl).getFeetPos(); // (getPosition is the eyes)
        bool const local = static_cast<Actor*>(&pl) == static_cast<Actor*>(&lp);
        if (!local && std::hypot(q.x - me.x, q.z - me.z) > 180.0) return true; // the whole map (islands are far apart)
        // the name chat uses: the real (gamer) name; a remote player may not carry it on the client, then the name tag
        // (first line, without a "[rank]" in front)
        std::string name;
        try { name = lower(pl.getRealName()); } catch (...) {}
        if (name.empty()) {
            try { name = lower(static_cast<Actor&>(pl).getNameTag()); } catch (...) {}
            if (auto nl = name.find('\n'); nl != std::string::npos) name.resize(nl);
            if (auto rb = name.rfind(']'); rb != std::string::npos) name = name.substr(rb + 1);
            while (!name.empty() && uint8_t(name.front()) <= ' ') name.erase(name.begin());
            while (!name.empty() && uint8_t(name.back()) <= ' ') name.pop_back();
        }
        if (name.empty() && !local) return true; // nameless: nothing in chat can point at it
        Track* t = nullptr;
        for (size_t i = 0; i < gTracks.size(); ++i)
            if (gTracks[i].name == name && gTracks[i].local == local) { t = &gTracks[i]; hit[i] = 1; break; }
        if (!t) {
            if (gTracks.size() >= 64) return true;
            gTracks.push_back({name, {q.x, q.y, q.z}, {q.x, q.y, q.z}, -1, now, local});
            note("player '{}'{}", name, local ? " (you)" : "");
            hit.push_back(1);
            t = &gTracks.back();
        }
        if (t->dye < 0) try { t->dye = dyeOfTag(static_cast<Actor&>(pl).getNameTag()); } catch (...) {}
        float const j = std::sqrt((q.x - t->pos[0]) * (q.x - t->pos[0]) + (q.y - t->pos[1]) * (q.y - t->pos[1]) + (q.z - t->pos[2]) * (q.z - t->pos[2]));
        if (j > 10.0f && now - t->seen < 400) gGone.push_back({t->name, {t->pos[0], t->pos[1], t->pos[2]}, t->dye, now, local}); // moved away at once
        t->pos[0] = q.x, t->pos[1] = q.y, t->pos[2] = q.z, t->seen = now;
        return true;
    });
    for (size_t i = 0; i < gTracks.size();) { // gone from the world (a spectator is hidden / removed)
        if (!hit[i]) {
            if (now - gTracks[i].seen < 400) gGone.push_back({gTracks[i].name, {gTracks[i].pos[0], gTracks[i].pos[1], gTracks[i].pos[2]}, gTracks[i].dye, now, gTracks[i].local});
            gTracks.erase(gTracks.begin() + long(i)), hit.erase(hit.begin() + long(i));
        } else ++i;
    }
    matchKills(now);
}

void resetMatch(double const* pos) {
    note("new match at {:.0f} {:.0f} {:.0f} (had {} beds, {} broken)", pos[0], pos[1], pos[2], gBeds.size(), gBroken);
    gBeds.clear();
    gBroken = 0;
    gIsMap  = false;
    gSpawnBed = false;
    gResetMs = nowMs();
    gTarget = gPhase = 0;
    gScanI  = 0;
    gScanPaused = false;
    gFx.nEvents = 0;
    gTracks.clear(); gGone.clear(); gKills.clear();
    gAnchor[0] = pos[0], gAnchor[1] = pos[1], gAnchor[2] = pos[2];
    gAnchored = true;
}

void scan(BlockSource& bs, double const* pos) { // walk a box around the player a slice at a time, note every bed (and its dye)
    if (gScanPaused) return;
    int const sx = 2 * kScanR + 1, sy = kScanDown + kScanUp + 1;
    long const total = long(sx) * sx * sy;
    if (gScanI == 0) std::memcpy(gScanC, pos, sizeof gScanC);
    int const ax = int(std::floor(gScanC[0])), ay = int(std::floor(gScanC[1])), az = int(std::floor(gScanC[2]));
    for (int n = 0; n < kPerFrame; ++n) {
        long i = gScanI;
        gScanI = int((gScanI + 1) % total);
        int const y = ay - kScanDown + int(i % sy);
        long r = i / sy;
        int const x = ax - kScanR + int(r % sx), z = az - kScanR + int(r / sx);
        if (y < -64 || y > 319) continue;
        BlockPos const bp{x, y, z};
        Block const&   b = bs.getBlock(bp);
        std::string const& tn = b.getTypeName();
        if (tn.find("bed") != std::string::npos && tn != "minecraft:bedrock" &&
            std::find(gSeenNames.begin(), gSeenNames.end(), tn) == gSeenNames.end() && gSeenNames.size() < 64) {
            gSeenNames.push_back(tn);
            note("block name seen: {} at {} {} {}", tn, x, y, z);
        }
        if (!isBed(b)) continue;
        Bed* bed = bedNear(x, y, z);
        if (!bed) {
            if (gBeds.size() >= 16) continue;
            gBeds.push_back({x, y, z, 14, true, nowMs()});
            bed = &gBeds.back();
            note("bed at {} {} {}", x, y, z);
            bool const byAnchor = std::hypot(x + 0.5 - gAnchor[0], z + 0.5 - gAnchor[2]) < 24.0;
            bool const byYou    = nowMs() - gResetMs < 15000 && std::hypot(x + 0.5 - gScanC[0], z + 0.5 - gScanC[2]) < 24.0;
            if (!gSpawnBed && (byAnchor || byYou)) {
                gSpawnBed = true; // a map has beds on islands far apart; a lobby's decoration beds sit together
                note("a bed by the spawn");
            }
        }
        if (auto* ba = tn == "minecraft:bed" ? bs.getBlockEntity(bp) : nullptr) bed->dye = std::clamp(int(static_cast<BedBlockActor*>(ba)->mDyeColor), 0, 15), bed->dyeKnown = true;
        else if (tn != "minecraft:bed" && !bed->dyeKnown) {
            int d = dyeAround(bs, x, y, z);
            if (d >= 0) { bed->dye = d; bed->dyeKnown = true; note("bed at {} {} {}: team colour {} (from the blocks around it)", x, y, z, d); }
        }
        // a broken bed seen standing again: only a long time later (a new map reusing the spot); The Hive leaves the hitbox
        // a moment after the 'bed destroyed' line, which must not bring it back
        if (!bed->alive && nowMs() - bed->changed > 90000) { bed->alive = true, bed->changed = nowMs(); }
    }
}
} // namespace

extern "C" __declspec(dllexport) BfMatchFxV1 const* BF_GetMatchFx() {
    std::lock_guard l(gMutex);
    gFx.size = sizeof gFx, gFx.version = 3;
    gFx.active = nowMs() - gHudMs.load() < 250 ? 1u : 0u;
    gFx.nowMs  = nowMs();
    return &gFx;
}
extern "C" __declspec(dllexport) void BF_MatchFxConsumer(int on) { gConsumerMs = on ? nowMs() : 0; }
extern "C" __declspec(dllexport) void BF_MatchFxPreview(float phase) { gPreview = phase; }

namespace bedrock_forge::client::matchfx {

void install() {
    gSubs.on<ll::event::BlockChangedEvent>([](ll::event::BlockChangedEvent& ev) { try { onChange(ev); } catch (...) {} });
    gSubs.on<ll::event::ClientJoinLevelEvent>([](ll::event::ClientJoinLevelEvent&) { gNewLevel = true; });
}
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
    // or a teleport (> 30 blocks in one frame) to somewhere with none of this match's beds nearby (a respawn on the
    // same map keeps the match; a server moving you to the next map / the lobby starts a new one)
    static double lastPos[3]{};
    double const  jump = std::sqrt((pos[0] - lastPos[0]) * (pos[0] - lastPos[0]) + (pos[1] - lastPos[1]) * (pos[1] - lastPos[1]) +
                                   (pos[2] - lastPos[2]) * (pos[2] - lastPos[2]));
    bool newPlace = false;
    if (last && now - last < 20000 && jump > 30.0) { // (a loading screen in between is still a teleport)
        newPlace = true;
        if (gIsMap)
            for (auto const& b : gBeds)
                if (std::hypot(b.x - pos[0], b.z - pos[2]) < 160.0) { newPlace = false; break; }
    }
    lastPos[0] = pos[0], lastPos[1] = pos[1], lastPos[2] = pos[2];
    // the server re-sent the world (a respawn / spectating on the same map, or a new world - The Hive's hub, lobby and
    // maps share coordinates, so distance can't tell): 3 s later, are this match's standing beds still there?
    static int64_t verifyAt = 0;
    static std::vector<BlockPos> verifyBeds; // the standing beds as they were before the re-send
    if (gNewLevel.exchange(false)) {
        if (!gIsMap) newPlace = true;
        else {
            verifyAt = now + 3000;
            verifyBeds.clear();
            for (auto const& b : gBeds)
                if (b.alive) verifyBeds.push_back(BlockPos{b.x, b.y, b.z});
            gScanPaused = true; // don't mix another world's beds (the hub's) into this match while we look
        }
    }
    if (verifyAt && now >= verifyAt) {
        verifyAt = 0;
        gScanPaused = false;
        int known = 0, still = 0;
        BlockSource& bs = lp->getDimensionBlockSource();
        for (auto const& b : verifyBeds) { ++known; still += isBed(bs.getBlock(b)) ? 1 : 0; }
        if (known == 0 || still == 0) newPlace = true;
        note("world re-sent: {} of {} standing beds still there -> {}", still, known, newPlace ? "new match" : "same match");
    }
    // a long time away from the HUD (a death screen, a menu) is only a new match when you came back somewhere else
    // every 3 s: the standing beds near you must still be beds. All of them gone without a break = you are somewhere
    // else (Hive moves you between hub, lobby and maps on the same coordinates, not always with a world change);
    // one gone while others stand = it broke without a block update we saw
    static int64_t lastCheck = 0;
    if (gIsMap && !newPlace && now - lastCheck > 3000) {
        lastCheck = now;
        BlockSource& bs = lp->getDimensionBlockSource();
        int checked = 0, missing = 0;
        std::vector<Bed*> gone;
        for (auto& b : gBeds) {
            if (!b.alive || std::hypot(b.x + 0.5 - pos[0], b.z + 0.5 - pos[2]) > 48.0) continue;
            ++checked;
            if (!isBed(bs.getBlock(BlockPos{b.x, b.y, b.z}))) { ++missing; gone.push_back(&b); }
        }
        if (checked >= 1 && missing == checked && (checked >= 2 || !gSpawnBed || gBroken == 0)) {
            newPlace = true;
            note("none of the {} standing beds near you are there any more -> new place", checked);
        } else
            for (Bed* b : gone) { b->alive = false, b->changed = now, ++gBroken; note("bed at {} {} {} gone (no block update seen)", b->x, b->y, b->z); }
    }
    if (last && now - last > 20000 && !newPlace) {
        if (!gIsMap) newPlace = true;
        else {
            verifyAt = now + 3000;
            verifyBeds.clear();
            for (auto const& b : gBeds)
                if (b.alive) verifyBeds.push_back(BlockPos{b.x, b.y, b.z});
            gScanPaused = true;
        }
    }
    if (!gAnchored || newPlace || std::hypot(pos[0] - gAnchor[0], pos[2] - gAnchor[2]) > 1500.0) resetMatch(pos);
    scan(lp->getDimensionBlockSource(), pos);
    if (gIsMap) {
        trackPlayers(*lp, now);
        // threat: players of another team close to a bed (a rush on it) - the beacon and, for yours, the screen react
        for (auto& b : gBeds) {
            float n = 0;
            if (b.alive && b.dyeKnown)
                for (auto const& t : gTracks) {
                    if (t.dye < 0 || t.dye == b.dye || now - t.seen > 500) continue;
                    if (std::hypot(t.pos[0] - (b.x + 0.5), t.pos[2] - (b.z + 0.5)) < 14.0 && std::abs(t.pos[1] - b.y) < 9.0) n += 1;
                }
            float const want = std::min(1.0f, n * 0.6f);
            b.threat += (want - b.threat) * std::min(1.0f, dt * (want > b.threat ? 6.0f : 1.5f));
        }
    }
    {   // F8: a test kill 5 blocks in front of you (normal / final alternate) - to see the animation without a fight
        static bool down = false;
        static bool fin  = false;
        bool const k = (GetAsyncKeyState(0x77) & 0x8000) != 0; // VK_F8
        if (k && !down) {
            float const yaw = lp->getRotation().y * 3.14159265f / 180.0f;
            Gone g{"test", {float(pos[0]) - std::sin(yaw) * 5.0f, float(pos[1]) - 1.62f, float(pos[2]) + std::cos(yaw) * 5.0f}, fin ? 14 : 3, now, false};
            pushKill(g, fin);
            fin = !fin;
        }
        down = k;
    }
    retarget();
    // the light leaves slowly: ~20 s golden -> dusk, ~25 s dusk -> night
    float const rate = gTarget > gPhase ? (gPhase < 1.0f ? 1.0f / 20.0f : 1.0f / 25.0f) : 1.0f / 3.0f;
    if (gPhase < gTarget) gPhase = std::min(gTarget, gPhase + rate * dt);
    else gPhase = std::max(gTarget, gPhase - rate * dt);
    gFx.phase = gPhase, gFx.phaseTarget = gTarget, gFx.bedsBroken = gBroken;
    if (!gIsMap && gSpawnBed) {
        for (size_t i = 0; i < gBeds.size() && !gIsMap; ++i)
            for (size_t j = i + 1; j < gBeds.size(); ++j)
                // two teams: beds far apart AND of two known, different colours (the Hive hub's decoration beds have no team
                // blocks around them, a lobby's beds share one colour)
                if (gBeds[i].dyeKnown && gBeds[j].dyeKnown && gBeds[i].dye != gBeds[j].dye
                    && std::hypot(double(gBeds[i].x - gBeds[j].x), double(gBeds[i].z - gBeds[j].z)) >= 20.0) {
                    gIsMap = true;
                    note("beds of two teams on islands apart - this is a Bed Wars map");
                    break;
                }
    }
    uint32_t nt = 0;
    if (gIsMap)
        for (auto const& b : gBeds) {
            if (!b.dyeKnown || nt >= 16) continue; // a bed with no team colour is decoration, not a team
            auto& x = gFx.teamEx[nt];
            x.threat = b.threat, x.elim = b.elim ? 1u : 0u, x.elimMs = b.elimMs;
            x.mine = std::hypot(b.x + 0.5 - gAnchor[0], b.z + 0.5 - gAnchor[2]) < 30.0 ? 1u : 0u; // the bed by where you arrived
            auto& t = gFx.teams[nt++];
            t.pos[0] = b.x + 0.5f, t.pos[1] = b.y + 0.6f, t.pos[2] = b.z + 0.5f;
            std::memcpy(t.col, kDye[b.dye], sizeof t.col);
            t.alive = b.alive ? 1u : 0u, t.changedMs = b.changed;
        }
    gFx.nTeams = nt;
    ++gFx.frame;
    static int64_t lastNote = 0;
    if (now - lastNote > 10000) {
        lastNote = now;
        int alive = 0;
        for (auto& b : gBeds) alive += b.alive ? 1 : 0;
        note("status: at {:.0f} {:.0f} {:.0f}, map {}, beds {} ({} alive), broken {}, phase {:.2f} -> {:.0f}, events {}", pos[0], pos[1], pos[2],
             gIsMap, gBeds.size(), alive, gBroken, gPhase, gTarget, gFx.nEvents);
    }
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
    {   // a kill line (not a bed / team line)
        if (gIsMap) note("text: {}", s.substr(0, 120)); // (finding the server's kill lines)
        bool const killy = s.find("death.") != std::string::npos || s.find("kill") != std::string::npos || s.find("slain") != std::string::npos || s.find("died") != std::string::npos ||
                           s.find("fell") != std::string::npos || s.find("void") != std::string::npos ||
                           (s.find("eliminated") != std::string::npos && s.find("team") == std::string::npos);
        if (killy && s.find("bed") == std::string::npos && gIsMap) {
            bool const you = s.find("you were") != std::string::npos || s.find("you died") != std::string::npos || s.find("you fell") != std::string::npos;
            note("kill line: {}", s.substr(0, 160));
            KillLine kl{s, s.find("final") != std::string::npos, you, nowMs(), {}};
            std::vector<std::pair<size_t, Gone>> named;
            for (auto const& t : gTracks) {
                size_t at = t.name.size() >= 3 ? s.find(t.name) : std::string::npos;
                if (t.local && you) at = 0;
                if (at != std::string::npos) named.push_back({at, Gone{t.name, {t.pos[0], t.pos[1], t.pos[2]}, t.dye, kl.at, t.local}});
            }
            std::sort(named.begin(), named.end(), [](auto const& x, auto const& y) { return x.first < y.first; });
            for (auto& n : named) kl.cands.push_back(n.second);
            // the wording names the victim: "b was killed by a" first, "final kill! a eliminated b" / "a killed b" last.
            // Decide now (the HUD frames that run the matcher stop under the death / game-over screens)
            if (!kl.cands.empty()) {
                bool const passive = s.find(" by ") != std::string::npos;
                Gone v = passive ? kl.cands.front() : kl.cands.back();
                for (auto const& g : gGone) // they were already moved (respawn / spectator): where they stood before
                    if (g.name == v.name && g.local == v.local && kl.at - g.at < 2500) { v.pos[0] = g.pos[0], v.pos[1] = g.pos[1], v.pos[2] = g.pos[2]; }
                pushKill(v, kl.fin);
            } else
                gKills.push_back(std::move(kl));
            if (gKills.size() > 16) gKills.erase(gKills.begin());
            matchKills(nowMs());
        }
    }
    bool const bedGone = s.find("bed") != std::string::npos && (s.find("destroy") != std::string::npos || s.find("broke") != std::string::npos);
    bool const teamOut = s.find("team") != std::string::npos && s.find("eliminated") != std::string::npos && s.find("final kill") == std::string::npos;
    if (teamOut) { // the team is out: its island goes dark (its bed may have broken long before)
        struct W2 { char const* w; int d[2]; };
        static W2 const kW[] = {{"red", {14, -1}}, {"blue", {11, 3}}, {"green", {13, 5}}, {"lime", {5, 13}}, {"yellow", {4, 1}},
                                {"aqua", {9, 3}}, {"cyan", {9, 3}}, {"white", {0, 8}}, {"pink", {6, 2}}, {"gray", {7, 8}},
                                {"grey", {7, 8}}, {"orange", {1, 4}}, {"magenta", {2, 10}}, {"purple", {10, 2}}, {"black", {15, 7}},
                                {"brown", {12, 1}}};
        W2 const* fw = nullptr; size_t at = std::string::npos;
        for (auto const& w : kW) { size_t k = s.find(w.w); if (k != std::string::npos && (at == std::string::npos || k < at)) at = k, fw = &w; }
        if (fw)
            for (int k = 0; k < 2; ++k) {
                bool any = false;
                for (auto& b : gBeds)
                    if (b.dyeKnown && b.dye == fw->d[k] && !b.elim) { b.elim = true, b.elimMs = nowMs(), any = true; note("team out: bed at {} {} {}", b.x, b.y, b.z); }
                if (any) break;
            }
    }
    if (bedGone || teamOut) {
        // which team? the server names its colour (The Hive: "BLUE BED DESTROYED", "Aqua Team has been eliminated!")
        struct W { char const* w; int d[2]; };
        static W const kWords[] = {{"red", {14, -1}}, {"blue", {11, 3}}, {"green", {13, 5}}, {"lime", {5, 13}}, {"yellow", {4, 1}},
                                   {"aqua", {9, 3}}, {"cyan", {9, 3}}, {"white", {0, 8}}, {"pink", {6, 2}}, {"gray", {7, 8}},
                                   {"grey", {7, 8}}, {"orange", {1, 4}}, {"magenta", {2, 10}}, {"purple", {10, 2}}, {"black", {15, 7}},
                                   {"brown", {12, 1}}};
        Bed* hit = nullptr;
        if (s.find("your bed") != std::string::npos) { // ours: the bed by where we arrived
            double best = 1e9;
            for (auto& b : gBeds)
                if (b.alive) {
                    double d = std::hypot(b.x + 0.5 - gAnchor[0], b.z + 0.5 - gAnchor[2]);
                    if (d < best) best = d, hit = &b;
                }
        }
        // the colour word that comes first (a player name after "by" may hold one too)
        W const* first = nullptr;
        size_t   at    = std::string::npos;
        for (auto const& w : kWords) {
            size_t k = s.find(w.w);
            if (k != std::string::npos && (at == std::string::npos || k < at)) at = k, first = &w;
        }
        // its bed: the first dye that any bed of this match has (alive or not); if that bed already went down (the block
        // update came first), this message is about it - nothing more to break
        if (!hit && first)
            for (int k = 0; k < 2 && !hit; ++k) {
                bool any = false;
                for (auto& b : gBeds)
                    if (b.dye == first->d[k]) {
                        any = true;
                        if (b.alive) { hit = &b; break; }
                    }
                if (any) break;
            }
        if (hit) {
            hit->alive = false, hit->changed = nowMs();
            ++gBroken;
            note("bed at {} {} {} broken (the server said: {})", hit->x, hit->y, hit->z, s.substr(0, 80));
        } else if (bedGone && gBroken == 0 && !first) gBroken = 1; // a bed we couldn't see went down
        gIsMap = true;
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
