// Bedrock Forge client - 3D model cosmetics: Knives (see Cosmetics.h, docs/COSMETICS.md). Original code for Bedrock Forge.
#include "client/Cosmetics.h"

#include <algorithm>
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
    int64_t drawAt = 0, inspectAt = 0, othersAt = 0;
    bool    keyWas = false;
} gS;

} // namespace

void registerModules(Registry& r) {
    r.add("knives", "Knives", Category::Visual,
          "Your sword becomes a 3D knife of our own design, with a flip when you draw it (only you see it; nothing changes in the fight)", false)
        .choice("design", "Knife", {"Novak Karambit"}, 0)
        .toggleSetting("draw_flip", "Flip it when you draw it", true)
        .slider("inspect_key", "Inspect key (0 = none)", 0, 0, 254, 1);
}

void frame(LocalPlayer* lp) {
    if (!lp) { gS = {}; return; }
    auto* m = Registry::get().find("knives");
    bool const on = m && m->enabled && !m->planned;
    int64_t const now = nowMs();
    Actor& self = *lp;
    float design = 0.f, draw = 99.f, insp = 99.f;
    try {
        bool const sword = isSword(lp->getSelectedItem());
        if (sword && !gS.heldSword) gS.drawAt = now;
        gS.heldSword = sword;
        if (on) {
            design = float(std::clamp(int(m->num("design", 0)), 0, 0) + 1);
            if (m->flag("draw_flip", true) && gS.drawAt) draw = std::min(99.f, float(now - gS.drawAt) / 1000.f);
            int const key = int(m->num("inspect_key", 0));
            bool const down = key > 0 && w32::gameFocused() && !menu::isOpen() && w32::keyDown(key);
            if (down && !gS.keyWas && sword) gS.inspectAt = now;
            gS.keyWas = down;
            if (gS.inspectAt) insp = std::min(99.f, float(now - gS.inspectAt) / 1000.f);
        }
        setPublic(self, "bfknife", design);
        setPublic(self, "bfkdraw", draw);
        setPublic(self, "bfkinsp", insp);
    } catch (...) {}
    // everyone else: the same variables at 0, so their swords stay vanilla and the pack never reads a missing one
    if (now - gS.othersAt > 500) {
        gS.othersAt = now;
        try {
            lp->getLevel().forEachPlayer([&](Player& p) {
                if (static_cast<Actor*>(&p) == &self) return true;
                try { setPublic(p, "bfknife", 0.f); setPublic(p, "bfkdraw", 99.f); setPublic(p, "bfkinsp", 99.f); } catch (...) {}
                return true;
            });
        } catch (...) {}
    }
}

} // namespace cosmetics
} // namespace bedrock_forge::client
