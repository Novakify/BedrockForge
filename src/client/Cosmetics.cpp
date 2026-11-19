// Bedrock Forge client - 3D model cosmetics: Knives (see Cosmetics.h, docs/COSMETICS.md). Original code for Bedrock Forge.
#include "client/Cosmetics.h"

#include <algorithm>
#include <cctype>
#include <utility>
#include <vector>
#include <atomic>
#include <cmath>
#include <chrono>
#include <memory>
#include <string>

#include "client/Menu.h"
#include "client/Module.h"
#include "core/Win32Input.h"

#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/util/MolangScriptArg.h"
#include "mc/util/MolangVariable.h"
#include "mc/util/MolangVariableMap.h"
#include "mc/util/MolangVariableSetting_AccessSpecifier.h"
#include "mc/util/MolangVariableSettings.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"

namespace bedrock_forge::client {
namespace w32 = bedrock_forge::win32;
namespace cosmetics {
namespace {

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// A variable another entity may read (c.owning_entity->v.x). Normally only variables a client entity file declares
// "public" are visible there; we set the same access flag on our own variables instead, so no player.entity.json
// is needed (Hive / Figura avatars keep theirs). Written under both spellings the game may key a "v.x" by.
void setPublic(Actor& a, char const* shortName, float value) {
    auto& map = a.mMolangVariables.get();
    std::string const names[2] = {std::string("variable.") + shortName, std::string(shortName)};
    for (auto const& n : names) {
        HashedString const h(n);
        auto const idx = MolangVariable::_findOrAddVariableIndex(h.getHash(), h.c_str(), false);
        MolangVariable* var = map._getOrAddMolangVariable(idx);
        if (!var) continue;
        MolangScriptArg const arg(value);
        var->mValue = arg;
        var->mSettings.get().mAccessSpecifier = MolangVariableSetting_AccessSpecifier::Public;
        std::unique_ptr<MolangScriptArg>& pub = var->mPublicValue;   // TypedStorage -> T&
        if (pub) *pub = arg;
        else pub = std::make_unique<MolangScriptArg>(arg);
    }
    map.mHasPublicVariables = true;
}

// Performance: each write hashes two names and touches the actor's variable map, so a value is only written when it
// changed, or once a second as a refresh (a world reload or dimension change gives the actor a fresh map).
struct Cached { float v = -12345.f; int64_t at = 0; };
void setCached(Actor& a, char const* n, float v, Cached& c, int64_t now) {
    if (v == c.v && now - c.at < 1000) return;
    setPublic(a, n, v);
    c.v = v; c.at = now;
}

bool isSword(ItemStack const& st) {
    try {
        if (st.isNull()) return false;
        return st.getTypeName().find("_sword") != std::string::npos;
    } catch (...) { return false; }
}

// fx colours (art-directed, not neon): steel, Novak violet, ember, gold, crimson
constexpr float kFx[5][3] = {{0.80f, 0.95f, 0.97f}, {0.74f, 0.56f, 1.00f}, {1.00f, 0.56f, 0.26f}, {1.00f, 0.82f, 0.42f}, {0.95f, 0.24f, 0.30f}};
constexpr int   kFinishes = 4;   // Factory, Blackout, Gold, Fade (tools/Cosmetics/finishes.py)
constexpr int   kMatchedSlash[3] = {1, 0, 2};   // karambit: crescent, balisong: trail, dagger: thrust streak

uint32_t rng() {   // xorshift, seeded from the clock: only picks cosmetic finishes / colours
    static uint32_t x = uint32_t(nowMs()) | 1u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x;
}
int rerollNot(int n, int last) {   // a random index in [0, n) that differs from the last one
    if (n <= 1) return 0;
    int const r = int(rng() % uint32_t(n - 1));
    return r >= last ? r + 1 : r;
}

std::string lower(std::string s) {
    for (auto& ch : s) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

// Which knife (1..3, 0 = keep the vanilla sword) the held sword becomes, by the "Applies to" setting
int knifeFor(Module& m, Player const& p, ItemStack const& st) {
    int const main = std::clamp(int(m.num("model", 0)), 0, 2) + 1;
    switch (int(m.num("applies", 0))) {
    case 1: {   // by sword type
        static constexpr std::pair<char const*, char const*> kTypes[] = {{"wooden_sword", "sw_wood"}, {"stone_sword", "sw_stone"},
            {"copper_sword", "sw_copper"}, {"iron_sword", "sw_iron"}, {"golden_sword", "sw_gold"}, {"diamond_sword", "sw_diamond"},
            {"netherite_sword", "sw_netherite"}};
        std::string const t = st.getTypeName();
        for (auto const& [type, sid] : kTypes) {
            if (t.find(type) == std::string::npos) continue;
            int const c = int(m.num(sid, 0));   // Main knife, Karambit, Balisong, Dagger, Vanilla
            return c == 0 ? main : c == 4 ? 0 : c;
        }
        return main;   // a sword type we don't list (modded / future): the main knife
    }
    case 2:     // one hotbar slot
        return p.getSelectedItemSlot() + 1 == int(m.num("slot", 1)) ? main : 0;
    case 3: {   // renamed swords (anvil name; only your client reads it)
        std::string const n = lower(st.getCustomName());
        if (n.find("karambit") != std::string::npos) return 1;
        if (n.find("balisong") != std::string::npos || n.find("butterfly") != std::string::npos) return 2;
        if (n.find("dagger") != std::string::npos) return 3;
        if (n.find("knife") != std::string::npos) return main;
        return 0;
    }
    default:
        return main;
    }
}

struct State {
    int     finish = 0, fxColor = 0;
    Cached  c[20];
    bool    heldSword = false;
    void const* heldSlot = nullptr;
    int64_t drawAt = 0, inspectAt = 0, othersAt = 0, attackAt = 0, hitAt = 0, stillSince = 0;
    int     inspectN = 0, attackN = 0, hitN = 0;
    bool    keyWas = false, lmbWas = false;
    float   lastX = 0, lastZ = 0, lastYaw = 0;
} gS;

std::atomic<int64_t> gHitMs{0};     // your confirmed hits (GameMode::attack hook in Modules.cpp)
std::atomic<int>     gHitCount{0};
std::atomic<int>     gTestSwing{0};

} // namespace

void testSwing() { ++gTestSwing; }
void noteLocalHit(bool /*crit*/) {
    gHitMs = nowMs();
    ++gHitCount;
}

// Settings mirror Purxiy's Knives module (docs/PURXIY_MODULES.md) where we build the same thing; our own designs.
void registerModules(Registry& r) {
    static std::vector<std::string> const kPer = {"Main knife", "Karambit", "Balisong", "Dagger", "Vanilla"};
    r.add("knives", "Knives", Category::Visual,
          "Your sword becomes a real 3D knife with its own tricks, swings and sounds (only you see it; nothing changes in the fight)", false)
        .choice("model", "Knife", {"Karambit", "Balisong", "Dagger"}, 0)
        .choice("applies", "Applies to", {"All swords", "By sword type", "One hotbar slot", "Renamed swords"}, 0)
        .choice("sw_wood", "Wooden sword", kPer, 0)
        .choice("sw_stone", "Stone sword", kPer, 0)
        .choice("sw_copper", "Copper sword", kPer, 0)
        .choice("sw_iron", "Iron sword", kPer, 0)
        .choice("sw_gold", "Golden sword", kPer, 0)
        .choice("sw_diamond", "Diamond sword", kPer, 0)
        .choice("sw_netherite", "Netherite sword", kPer, 0)
        .slider("slot", "Hotbar slot", 1, 1, 9, 1)
        .choice("finish", "Finish", {"Random", "Factory", "Blackout", "Gold", "Fade"}, 0)
        .choice("fx_color", "Effect colour", {"Random", "Steel", "Violet", "Ember", "Gold", "Crimson"}, 0)
        .choice("slash", "Slash style", {"Matched to the knife", "Trail", "Crescent", "Thrust streak"}, 0)
        .slider("size", "Size (all knives)", 1.f, 0.5f, 1.8f, 0.05f)
        .slider("size_k1", "Karambit size (remembered for this knife)", 1.f, 0.5f, 1.8f, 0.05f)
        .slider("size_k2", "Balisong size (remembered for this knife)", 1.f, 0.5f, 1.8f, 0.05f)
        .slider("size_k3", "Dagger size (remembered for this knife)", 1.f, 0.5f, 1.8f, 0.05f)
        .slider("pos_x", "Left / right (first person)", 0.f, -3.f, 3.f, 0.25f)
        .slider("pos_y", "Up / down (first person)", 0.f, -3.f, 3.f, 0.25f)
        .toggleSetting("third_person", "Knife in third person too", true)
        .toggleSetting("glint", "Enchantment glint on the knife", true)
        .slider("inspect_vk", "Inspect key (Windows key code: 73 = I, 0 = none; press again to chain the next trick)", 73, 0, 254, 1)
        .toggleSetting("auto_inspect", "Idle tricks (plays with the blade while you stand still)", true)
        .slider("interval", "Trick interval (s of idle time)", 12.f, 5.f, 30.f, 1.f)
        .slider("speed", "Animation speed", 1.f, 0.65f, 1.5f, 0.05f)
        .toggleSetting("draw", "Draw animation (a flourish when you equip a sword)", true)
        .toggleSetting("sounds", "Trick and attack sounds (clean steel, our own)", true)
        .toggleSetting("quiet", "Quiet sounds (about half as loud)", false)
        .toggleSetting("slash_arcs", "Slash effects (the blade leaves light as it swings)", true)
        .toggleSetting("hit_effects", "Hit effects (sparks and a cut on every confirmed hit)", true);
}

void frame(LocalPlayer* lp) {
    if (!lp) { gS = {}; return; }
    auto* m = Registry::get().find("knives");
    bool const on = m && m->enabled && !m->planned;
    int64_t const now = nowMs();
    Actor& self = *lp;
    auto secs = [&](int64_t at) { return at ? std::min(99.f, float(now - at) / 1000.f) : 99.f; };
    try {
        ItemStack const& held = lp->getSelectedItem();
        bool const sword = isSword(held);
        // draw on equipping a sword: from a non-sword, or from one sword slot to another (the slot's stack moved)
        void const* slot = &held;
        bool const equipped = sword && (!gS.heldSword || slot != gS.heldSlot);
        gS.heldSword = sword;
        gS.heldSlot = slot;
        float design = 0.f, draw = 99.f, insp = 99.f, atk = 99.f, hit = 99.f, spd = 1.f, opt = 0.f;
        float fin = 0.f, slash = 0.f, scl = 1.f, ox = 0.f, oy = 0.f;
        if (equipped) gS.drawAt = now;
        if (on) {
            design = sword ? float(knifeFor(*m, *lp, held)) : 0.f;
            spd = m->num("speed", 1.f);
            int const fset = int(m->num("finish", 0));   // 0 = random: a new finish every time you draw
            if (equipped && fset == 0) gS.finish = rerollNot(kFinishes, gS.finish);
            fin = float(fset == 0 ? gS.finish : fset - 1);
            int const sset = int(m->num("slash", 0));
            slash = float(sset == 0 ? (design > 0 ? kMatchedSlash[int(design) - 1] : 0) : sset - 1);
            scl = m->num("size", 1.f);   // x the held knife's own size (each knife keeps its own)
            if (design >= 1 && design <= 3) scl *= m->num(design == 1 ? "size_k1" : design == 2 ? "size_k2" : "size_k3", 1.f);
            scl = std::clamp(scl, 0.4f, 2.0f);
            ox = m->num("pos_x", 0.f);
            oy = m->num("pos_y", 0.f);
            bool const input = w32::gameFocused() && !menu::isOpen() && w32::cursorHidden();
            if (m->flag("draw", true)) draw = secs(gS.drawAt);
            // inspect: the key (each press chains the next trick), or idle tricks after `interval` s standing still
            int const key = int(m->num("inspect_vk", 73));
            bool const down = key > 0 && input && w32::keyDown(key);
            if (down && !gS.keyWas && sword) { gS.inspectAt = now; ++gS.inspectN; }
            gS.keyWas = down;
            Vec3 const pos = self.getPosition();
            float const yaw = self.getRotation().z;
            bool const still = std::abs(pos.x - gS.lastX) < 0.01f && std::abs(pos.z - gS.lastZ) < 0.01f && std::abs(yaw - gS.lastYaw) < 0.5f;
            gS.lastX = pos.x; gS.lastZ = pos.z; gS.lastYaw = yaw;
            if (!still || !sword) gS.stillSince = now;
            int64_t const idleMs = int64_t(m->num("interval", 12.f) * 1000.f);
            if (m->flag("auto_inspect", true) && sword && now - gS.stillSince > idleMs && now - gS.inspectAt > idleMs) {
                gS.inspectAt = now; ++gS.inspectN;
            }
            insp = secs(gS.inspectAt);
            // attack: every left click while holding the knife (hits and misses), confirmed hits from the attack hook
            int const cset = int(m->num("fx_color", 0));   // 0 = random: a new colour every swing
            bool const lmb = input && w32::keyDown(0x01);
            if (((lmb && !gS.lmbWas) || gTestSwing.exchange(0) > 0) && sword) {
                gS.attackAt = now; ++gS.attackN;
                if (cset == 0) gS.fxColor = rerollNot(5, gS.fxColor);
            }
            if (cset > 0) gS.fxColor = cset - 1;
            gS.lmbWas = lmb;
            atk = secs(gS.attackAt);
            if (int const n = gHitCount.load(); n != gS.hitN) { gS.hitN = n; gS.hitAt = gHitMs.load(); }
            hit = secs(gS.hitAt);
            opt = float((m->flag("slash_arcs", true) ? 1 : 0) | (m->flag("hit_effects", true) ? 2 : 0) | (m->flag("sounds", true) ? 4 : 0)
                        | (m->flag("quiet", false) ? 8 : 0) | (m->flag("glint", true) ? 0 : 16) | (m->flag("third_person", true) ? 0 : 32));
        }
        // always written (rest values while off): the pack reads them every frame, and a variable that was never set
        // made the sword vanish in game. Unchanged values are skipped (setCached).
        float const* col = kFx[std::clamp(gS.fxColor, 0, 4)];
        std::pair<char const*, float> const vars[] = {
            {"bfknife", design}, {"bfkdraw", draw}, {"bfkinsp", insp}, {"bfkinsn", float(gS.inspectN & 1)},
            {"bfkatk", atk}, {"bfkatn", float(gS.attackN & 1)}, {"bfkhit", hit}, {"bfkhitn", float(gS.hitN & 1)},
            {"bfkspd", spd}, {"bfkopt", opt}, {"bfkfin", fin}, {"bfkslash", slash}, {"bfkscl", scl}, {"bfkox", ox},
            {"bfkoy", oy}, {"bfkcr", col[0]}, {"bfkcg", col[1]}, {"bfkcb", col[2]}};
        static_assert(std::size(vars) <= 20, "State::c holds 20");
        for (size_t i = 0; i < std::size(vars); ++i) setCached(self, vars[i].first, vars[i].second, gS.c[i], now);
    } catch (...) {}
    // everyone else: the same variables at rest, so their swords stay vanilla and the pack never reads a missing one
    if (now - gS.othersAt > (on ? 500 : 2000)) {   // local and visual only: no packet, nobody else sees it
        gS.othersAt = now;
        try {
            lp->getLevel().forEachPlayer([&](Player& p) {
                if (static_cast<Actor*>(&p) == &self) return true;
                try {
                    setPublic(p, "bfknife", 0.f);
                    for (char const* n : {"bfkdraw", "bfkinsp", "bfkatk", "bfkhit"}) setPublic(p, n, 99.f);
                    for (char const* n : {"bfkinsn", "bfkatn", "bfkhitn", "bfkopt", "bfkfin", "bfkslash", "bfkox", "bfkoy"}) setPublic(p, n, 0.f);
                    for (char const* n : {"bfkspd", "bfkscl", "bfkcr", "bfkcg", "bfkcb"}) setPublic(p, n, 1.f);
                } catch (...) {}
                return true;
            });
        } catch (...) {}
    }
}

} // namespace cosmetics
} // namespace bedrock_forge::client
