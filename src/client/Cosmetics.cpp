// Bedrock Forge client - 3D model cosmetics: Knives (see Cosmetics.h, docs/COSMETICS.md). Original code for Bedrock Forge.
#include "client/Cosmetics.h"

#include <algorithm>
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

bool isSword(ItemStack const& st) {
    try {
        if (st.isNull()) return false;
        return st.getTypeName().find("_sword") != std::string::npos;
    } catch (...) { return false; }
}

struct State {
    bool    heldSword = false;
    void const* heldSlot = nullptr;
    int64_t drawAt = 0, inspectAt = 0, othersAt = 0, attackAt = 0, hitAt = 0, stillSince = 0;
    int     inspectN = 0, attackN = 0, hitN = 0;
    bool    keyWas = false, lmbWas = false;
    float   lastX = 0, lastZ = 0, lastYaw = 0;
} gS;

std::atomic<int64_t> gHitMs{0};     // your confirmed hits (GameMode::attack hook in Modules.cpp)
std::atomic<int>     gHitCount{0};

} // namespace

void noteLocalHit(bool /*crit*/) {
    gHitMs = nowMs();
    ++gHitCount;
}

// Settings mirror Purxiy's Knives module (docs/PURXIY_MODULES.md) where we build the same thing; our own designs.
void registerModules(Registry& r) {
    r.add("knives", "Knives", Category::Visual,
          "Your sword becomes a real 3D knife with its own tricks, swings and sounds (only you see it; nothing changes in the fight)", false)
        .choice("model", "Knife", {"Karambit", "Balisong", "Dagger"}, 0)
        .slider("inspect_key", "Inspect key (0 = none; press again to chain the next trick)", 0, 0, 254, 1)
        .toggleSetting("auto_inspect", "Idle tricks (plays with the blade while you stand still)", true)
        .slider("interval", "Trick interval (s of idle time)", 12.f, 5.f, 30.f, 1.f)
        .slider("speed", "Animation speed", 1.f, 0.65f, 1.5f, 0.05f)
        .toggleSetting("draw", "Draw animation (a flourish when you equip a sword)", true)
        .toggleSetting("sounds", "Trick and attack sounds (clean steel, our own)", true)
        .toggleSetting("slash_arcs", "Slash arcs (the blade leaves an arc of light as it swings)", true)
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
        if (sword && (!gS.heldSword || slot != gS.heldSlot)) gS.drawAt = now;
        gS.heldSword = sword;
        gS.heldSlot = slot;
        float design = 0.f, draw = 99.f, insp = 99.f, atk = 99.f, hit = 99.f, spd = 1.f, opt = 0.f;
        if (on) {
            design = float(std::clamp(int(m->num("model", 0)), 0, 2) + 1);
            spd = m->num("speed", 1.f);
            bool const input = w32::gameFocused() && !menu::isOpen() && w32::cursorHidden();
            if (m->flag("draw", true)) draw = secs(gS.drawAt);
            // inspect: the key (each press chains the next trick), or idle tricks after `interval` s standing still
            int const key = int(m->num("inspect_key", 0));
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
            bool const lmb = input && w32::keyDown(0x01);
            if (lmb && !gS.lmbWas && sword) { gS.attackAt = now; ++gS.attackN; }
            gS.lmbWas = lmb;
            atk = secs(gS.attackAt);
            if (int const n = gHitCount.load(); n != gS.hitN) { gS.hitN = n; gS.hitAt = gHitMs.load(); }
            hit = secs(gS.hitAt);
            opt = float((m->flag("slash_arcs", true) ? 1 : 0) | (m->flag("hit_effects", true) ? 2 : 0) | (m->flag("sounds", true) ? 4 : 0));
        }
        // always written (rest values while off, once a second): the pack reads them every frame, and a variable
        // that was never set made the sword vanish in game
        static int64_t restAt = 0;
        if (on || now - restAt > 1000) {
            restAt = now;
            setPublic(self, "bfknife", design);
            setPublic(self, "bfkdraw", draw);
            setPublic(self, "bfkinsp", insp);
            setPublic(self, "bfkinsn", float(gS.inspectN & 1));
            setPublic(self, "bfkatk", atk);
            setPublic(self, "bfkatn", float(gS.attackN & 1));
            setPublic(self, "bfkhit", hit);
            setPublic(self, "bfkhitn", float(gS.hitN & 1));
            setPublic(self, "bfkspd", spd);
            setPublic(self, "bfkopt", opt);
        }
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
                    for (char const* n : {"bfkinsn", "bfkatn", "bfkhitn", "bfkopt"}) setPublic(p, n, 0.f);
                    setPublic(p, "bfkspd", 1.f);
                } catch (...) {}
                return true;
            });
        } catch (...) {}
    }
}

} // namespace cosmetics
} // namespace bedrock_forge::client
