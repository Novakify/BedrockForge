// Bedrock Forge client HUD + client menu. Original code for Renderer Foundation. See Hud.h.
//
// Threads: the game's state (player, armor, held item, player list) is copied on the client thread inside the
// mce::Camera hook (throttled: 20 Hz, the player list 2 Hz) into one small snapshot under a mutex; the ImGui frame
// (Present) only reads that copy. Clicks / keys are polled with GetAsyncKeyState in the ImGui frame. Nothing here
// runs per draw call, and with every module off the hook returns at once.
#include "Hud.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/network/ClientNetworkHandler.h"
#include "mc/client/options/IOptionRegistry.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/entity/components/MoveInputComponent.h"
#include "mc/input/MoveInputState.h"
#include "mc/options/option_types/FloatOption.h"
#include "mc/options/option_types/OptionID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerListEntry.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/NetworkIdentifier.h"
#include "mc/network/PacketSender.h"
#include "mc/network/packet/CommandRequestPacket.h"
#include "mc/network/packet/CommandRequestPacketPayload.h"
#include "mc/network/packet/SetTitlePacket.h"
#include "mc/network/packet/SetTitlePacketPayload.h"
#include "mc/network/packet/TextPacket.h"
#include "mc/network/packet/TextPacketPayload.h"
#include "mc/server/commands/CommandOriginData.h"
#include "mc/server/commands/CommandOriginType.h"
#include "mc/server/commands/CurrentCmdVersion.h"

#include <windows.h>

#include <imgui.h>

#include "ScreenWatch.h"
#include "SpatialConfigScreen.h"

namespace rf::postfx {
void drawUi(); // PostFx.h includes d3d12.h, which must not meet LeviLamina's headers in one file
}

namespace rf::hud {
namespace {
using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------- snapshot (client thread -> ImGui frame)
struct Piece {
    bool        has = false;
    std::string name;
    int         dmg = 0, maxDmg = 0; // maxDmg 0: not damageable
    int         count = 0;
};
struct Snap {
    bool                     inWorld = false;
    float                    x = 0, y = 0, z = 0, yaw = 0, pitch = 0;
    int                      health = 0, maxHealth = 0, armor = 0;
    Piece                    armorPiece[4], held;
    std::vector<std::string> players;
};
std::mutex        gSnapMutex;
Snap              gSnap;
Clock::time_point gLastFast{}, gLastList{};
bool              gHooked = false;

// ---------------------------------------------------------------- options (BedrockForgeClient.cfg)
struct Module {
    char const* id;
    char const* label;
    char const* desc;
    bool        on;
    float       x, y;   // where the window's pivot sits, as a fraction of the screen
    float       px, py; // pivot inside the window (0,0 top-left .. 1,1 bottom-right)
    float       dx, dy; // default x, y
};
enum : int { MFps, MCps, MKeys, MCoords, MDir, MArmor, MTab, MCount };
Module gMods[MCount] = {
    {"fps", "FPS", "Frames per second", true, 0.006f, 0.012f, 0, 0, 0.006f, 0.012f},
    {"cps", "CPS", "Clicks per second (left | right)", true, 0.006f, 0.052f, 0, 0, 0.006f, 0.052f},
    {"keys", "Keystrokes", "W A S D, mouse buttons and space", true, 0.006f, 0.62f, 0, 0, 0.006f, 0.62f},
    {"coords", "Coordinates", "Your X / Y / Z", false, 0.006f, 0.092f, 0, 0, 0.006f, 0.092f},
    {"dir", "Direction", "Facing, yaw and pitch", false, 0.006f, 0.132f, 0, 0, 0.006f, 0.132f},
    {"armor", "Armor HUD", "Health, armor and each piece's durability", true, 0.995f, 0.985f, 1, 1, 0.995f, 0.985f},
    {"tab", "Player List", "Everyone on the server (hold Tab)", true, 0.5f, 0.06f, 0.5f, 0, 0.5f, 0.06f},
};
struct Cross {
    bool  on      = false;
    int   style   = 0; // 0 cross, 1 dot, 2 circle, 3 cross + dot, 4 T
    float size    = 6.0f, gap = 2.0f, thick = 2.0f;
    ImU32 color   = IM_COL32(255, 255, 255, 255);
    bool  outline = true;
} gCross;
bool              gHudOn      = true;
int               gMenuKey    = VK_RSHIFT;
float             gScale      = 1.0f;
ImU32             gAccent     = IM_COL32(110, 140, 255, 255);
int               gBgAlpha    = 110;
bool              gShadow     = true;
bool              gTabHold    = true;
int               gTabKey     = VK_TAB;
bool              gCoordsDec  = true;
bool              gArmorHeld  = true;
struct ZoomOpt {
    bool  on     = true;
    int   key    = 'C';
    float factor = 4.0f;
    bool  smooth = true;
} gZoom;
struct TimeOpt {
    bool on    = false;
    int  ticks = 6000; // 0 sunrise, 6000 noon, 12000 sunset, 18000 midnight
} gTime;
struct BrightOpt {
    bool  on    = false;
    float gamma = 12.0f;
    float orig  = -1.0f; // the player's own brightness (0..1) from before fullbright, kept across restarts
} gBright;
bool              gAutoSprint = false;
struct AutoOpt {
    bool        gg        = false;
    std::string ggMsg     = "gg";
    float       ggDelay   = 1.0f;
    bool        rq        = false;
    bool        rqOnDeath = false;
    float       rqDelay   = 2.0f;
    std::string rqCustom; // other servers: a play-again command of your own ("" = The Hive only, found by itself)
} gAuto;
std::atomic<bool> gZoomHeld{false};
std::atomic<bool> gMenu{false};
std::atomic<bool> gBtnHover{false};
bool              gEdit = false;
std::filesystem::path gCfgFile;
ImFont*           gFont = nullptr;

struct Entry {
    std::string key;
    char        t; // b bool, i int, f float, u colour, s text
    void*       p;
};
std::vector<Entry> entries() {
    std::vector<Entry> e = {
        {"hud", 'b', &gHudOn},          {"menuKey", 'i', &gMenuKey},        {"scale", 'f', &gScale},
        {"accent", 'u', &gAccent},      {"bgAlpha", 'i', &gBgAlpha},        {"shadow", 'b', &gShadow},
        {"tabHold", 'b', &gTabHold},    {"tabKey", 'i', &gTabKey},          {"coordsDecimals", 'b', &gCoordsDec},
        {"armorHeld", 'b', &gArmorHeld}, {"cross.on", 'b', &gCross.on},     {"cross.style", 'i', &gCross.style},
        {"cross.size", 'f', &gCross.size}, {"cross.gap", 'f', &gCross.gap}, {"cross.thick", 'f', &gCross.thick},
        {"cross.color", 'u', &gCross.color}, {"cross.outline", 'b', &gCross.outline},
        {"zoom.on", 'b', &gZoom.on},    {"zoom.key", 'i', &gZoom.key},      {"zoom.factor", 'f', &gZoom.factor},
        {"zoom.smooth", 'b', &gZoom.smooth}, {"time.on", 'b', &gTime.on},   {"time.ticks", 'i', &gTime.ticks},
        {"bright.on", 'b', &gBright.on}, {"bright.gamma", 'f', &gBright.gamma}, {"bright.orig", 'f', &gBright.orig},
        {"autoSprint", 'b', &gAutoSprint}, {"auto.gg", 'b', &gAuto.gg}, {"auto.ggMsg", 's', &gAuto.ggMsg},
        {"auto.ggDelay", 'f', &gAuto.ggDelay}, {"auto.rq", 'b', &gAuto.rq}, {"auto.rqDeath", 'b', &gAuto.rqOnDeath},
        {"auto.rqDelay", 'f', &gAuto.rqDelay}, {"auto.rqCustom", 's', &gAuto.rqCustom},
    };
    for (auto& m : gMods) {
        std::string k = std::string("m.") + m.id;
        e.push_back({k + ".on", 'b', &m.on});
        e.push_back({k + ".x", 'f', &m.x});
        e.push_back({k + ".y", 'f', &m.y});
    }
    return e;
}

// ---------------------------------------------------------------- helpers
std::string sfmt(char const* f, ...) {
    char    b[256];
    va_list a;
    va_start(a, f);
    std::vsnprintf(b, sizeof b, f, a);
    va_end(a);
    return b;
}
/// drops Minecraft formatting codes (the section sign + one character)
std::string stripCodes(std::string const& s) {
    std::string r;
    r.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (uint8_t(s[i]) == 0xC2 && i + 1 < s.size() && uint8_t(s[i + 1]) == 0xA7) { i += 2; continue; } // UTF-8 section sign
        r += s[i];
    }
    return r;
}
ImU32 mcColour(char c) {
    switch (std::tolower(uint8_t(c))) {
    case '0': return IM_COL32(0, 0, 0, 255);
    case '1': return IM_COL32(0, 0, 170, 255);
    case '2': return IM_COL32(0, 170, 0, 255);
    case '3': return IM_COL32(0, 170, 170, 255);
    case '4': return IM_COL32(170, 0, 0, 255);
    case '5': return IM_COL32(170, 0, 170, 255);
    case '6': return IM_COL32(255, 170, 0, 255);
    case '7': return IM_COL32(170, 170, 170, 255);
    case '8': return IM_COL32(85, 85, 85, 255);
    case '9': return IM_COL32(85, 85, 255, 255);
    case 'a': return IM_COL32(85, 255, 85, 255);
    case 'b': return IM_COL32(85, 255, 255, 255);
    case 'c': return IM_COL32(255, 85, 85, 255);
    case 'd': return IM_COL32(255, 85, 255, 255);
    case 'e': return IM_COL32(255, 255, 85, 255);
    case 'g': return IM_COL32(221, 214, 5, 255);
    default: return IM_COL32(255, 255, 255, 255);
    }
}
bool gameFocused() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}
bool down(int vk) { return vk > 0 && vk < 256 && (GetAsyncKeyState(vk) & 0x8000) != 0; }

float uiScale() {
    float H = ImGui::GetIO().DisplaySize.y;
    return std::max(0.5f, H / 1080.0f);
}
/// font scale for a wanted pixel height (the HUD font is loaded at 32 px, ImGui's default at 13 px)
float fontScaleFor(float px) { return px / (gFont ? 32.0f : 13.0f); }

void text(ImU32 col, std::string const& s) {
    ImVec2 p  = ImGui::GetCursorScreenPos();
    ImVec2 sz = ImGui::CalcTextSize(s.c_str());
    auto*  dl = ImGui::GetWindowDrawList();
    float  o  = std::max(1.0f, std::round(ImGui::GetFontSize() / 14.0f));
    if (gShadow) dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p.x + o, p.y + o), IM_COL32(0, 0, 0, 160), s.c_str());
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), p, col, s.c_str());
    ImGui::Dummy(sz);
}
/// text with Minecraft colour codes (player names on Hive / CubeCraft carry them)
void mcText(std::string const& s, ImU32 base) {
    ImVec2 p   = ImGui::GetCursorScreenPos();
    float  x   = p.x, fs = ImGui::GetFontSize();
    auto*  dl  = ImGui::GetWindowDrawList();
    float  o   = std::max(1.0f, std::round(fs / 14.0f));
    ImU32  col = base;
    std::string seg;
    auto flush = [&] {
        if (seg.empty()) return;
        if (gShadow) dl->AddText(ImGui::GetFont(), fs, ImVec2(x + o, p.y + o), IM_COL32(0, 0, 0, 160), seg.c_str());
        dl->AddText(ImGui::GetFont(), fs, ImVec2(x, p.y), col, seg.c_str());
        x += ImGui::CalcTextSize(seg.c_str()).x;
        seg.clear();
    };
    for (size_t i = 0; i < s.size(); ++i) {
        bool sect = uint8_t(s[i]) == 0xC2 && i + 2 < s.size() && uint8_t(s[i + 1]) == 0xA7; // UTF-8 section sign
        if (sect) {
            flush();
            char c = s[i + 2];
            if (c == 'r' || c == 'R') col = base;
            else if (std::isxdigit(uint8_t(c)) || c == 'g' || c == 'G') col = mcColour(c);
            i += 2;
            continue;
        }
        seg += s[i];
    }
    flush();
    ImGui::Dummy(ImVec2(std::max(1.0f, x - p.x), fs));
}
ImU32 durColour(float f) {
    if (f > 0.6f) return IM_COL32(85, 255, 85, 255);
    if (f > 0.3f) return IM_COL32(255, 255, 85, 255);
    return IM_COL32(255, 85, 85, 255);
}
std::string keyName(int vk) {
    switch (vk) {
    case 0: return "None";
    case VK_RSHIFT: return "Right Shift";
    case VK_LSHIFT: return "Left Shift";
    case VK_RCONTROL: return "Right Ctrl";
    case VK_LCONTROL: return "Left Ctrl";
    case VK_TAB: return "Tab";
    case VK_INSERT: return "Insert";
    case VK_DELETE: return "Delete";
    case VK_HOME: return "Home";
    case VK_END: return "End";
    case VK_PRIOR: return "Page Up";
    case VK_NEXT: return "Page Down";
    case VK_CAPITAL: return "Caps Lock";
    default: break;
    }
    if (vk >= VK_F1 && vk <= VK_F24) return sfmt("F%d", vk - VK_F1 + 1);
    char b[64] = {};
    UINT sc    = MapVirtualKeyA(UINT(vk), MAPVK_VK_TO_VSC);
    if (sc && GetKeyNameTextA(LONG(sc << 16), b, sizeof b) > 0) return b;
    return sfmt("Key %d", vk);
}

// ---------------------------------------------------------------- client thread
void fill(Piece& p, ItemStack const& st) {
    p = {};
    if (st.isNull()) return;
    p.has   = true;
    p.count = int(st.mCount);
    p.name  = stripCodes(st.getHoverName());
    if (st.isDamageableItem())
        if (Item const* it = st.mItem.get()) {
            p.maxDmg = it->getMaxDamage();
            p.dmg    = st.getDamageValue();
        }
}
bool anyGameModule() {
    return gMods[MCoords].on || gMods[MDir].on || gMods[MArmor].on || gMods[MTab].on;
}
// ---------------------------------------------------------------- Auto GG / auto re-queue
// Game ends are read from chat and title packets (patterns of The Hive, CubeCraft, Lifeboat, Zeqa, Galaxite, Mineville).
// What we send goes out later on the client thread (a short, natural delay): a chat message, a command. On The Hive the
// game to re-queue for comes from its own /connection command ("You are connected to server name SKY-DUOS12"), whose
// answer is kept out of chat. Nothing Hive-specific is ever sent before Hive was seen (its Game OVER or that answer).
struct Action {
    Clock::time_point at;
    bool              command;
    std::string       text;
};
std::mutex          gActMutex;
std::vector<Action> gActs;
Clock::time_point   gLastEnd{}, gAwaitUntil{};
std::atomic<bool>   gAwaitConn{false};
std::atomic<bool>   gOnHive{false};
std::atomic<long>   gGgSent{0}, gRqSent{0};
std::mutex          gGameMutex;
std::string         gHiveGame; // last /connection answer, digits removed: "SKY-DUOS"

void queueAction(float delaySec, bool command, std::string text) {
    std::lock_guard l(gActMutex);
    gActs.push_back({Clock::now() + std::chrono::milliseconds(int(std::clamp(delaySec, 0.0f, 30.0f) * 1000.0f)), command, std::move(text)});
}
enum : int { EndNone, EndHive, EndOther, EndDeath };
bool has(std::string const& s, char const* p) { return s.find(p) != std::string::npos; }
int endFromText(std::string const& m) {
    std::string c = stripCodes(m);
    if (c == "» Game OVER!") return EndHive; // The Hive
    if (has(m, "§a won the game!") || has(m, "§a has won the game!")) return EndOther; // CubeCraft / Lifeboat
    return EndNone;
}
int endFromTitle(std::string const& t) {
    if (t == "§f§aYou won the game!" || t == "§f§cYou lost the game!") return EndOther; // Zeqa
    if (has(t, "Team§r§a won the game!") || has(t, "§bHiders§r§f Win") || has(t, "§eSeekers§r§f Win") || t == "Finished" ||
        t == "Out of Time!")
        return EndOther; // Galaxite
    if (t == "§aYou Win!" || t == "§cGame Over!") return EndOther; // Mineville
    if (t == "§cYou died!" || t == "§7You're spectating the §as§eh§6o§cw§7!") return EndDeath; // The Hive (solo)
    return EndNone;
}
void onGameEnd(int kind) {
    if (kind == EndHive) gOnHive = true;
    if (kind == EndOther) gOnHive = false;
    if (kind == EndDeath && !gOnHive.load()) return; // only re-queue after a death where /q is known to exist
    auto now = Clock::now();
    if (now - gLastEnd < std::chrono::seconds(8)) return; // the same end seen as a title and a chat line
    gLastEnd = now;
    if (kind != EndDeath && gAuto.gg && !gAuto.ggMsg.empty()) {
        queueAction(gAuto.ggDelay, false, gAuto.ggMsg);
        ++gGgSent;
    }
    if (!gAuto.rq || (kind == EndDeath && !gAuto.rqOnDeath)) return;
    if (kind == EndHive || kind == EndDeath) {
        float d = std::max(0.3f, gAuto.rqDelay - 0.4f);
        queueAction(d, true, "/connection"); // its answer queues /q <game> (onText)
        gAwaitUntil = now + std::chrono::milliseconds(int(d * 1000.0f) + 6000);
        gAwaitConn  = true;
    } else if (!gAuto.rqCustom.empty())
        queueAction(gAuto.rqDelay, true, gAuto.rqCustom);
}
/// false: keep this message out of chat (our own /connection question)
bool onText(std::string const& m) {
    if (gAwaitConn.load()) {
        std::string c = stripCodes(m);
        size_t      k = c.find("You are connected to server name ");
        if (k != std::string::npos || has(c, "You are connected")) {
            gAwaitConn = false;
            if (k != std::string::npos) {
                std::string srv = c.substr(k + 33), game;
                for (char ch : srv)
                    if (!std::isdigit(uint8_t(ch)) && !std::isspace(uint8_t(ch))) game += char(std::tolower(uint8_t(ch)));
                while (!game.empty() && game.back() == '-') game.pop_back();
                {
                    std::lock_guard l(gGameMutex);
                    gHiveGame = game;
                }
                gOnHive = true;
                if (!game.empty() && game.find("hub") == std::string::npos && game != "replay") {
                    queueAction(0.4f, true, "/q " + game);
                    ++gRqSent;
                }
            }
            return false;
        }
        if (Clock::now() > gAwaitUntil) gAwaitConn = false;
    }
    if (int e = endFromText(m)) onGameEnd(e);
    return true;
}
void onTitle(std::string const& t) {
    if (int e = endFromTitle(t)) onGameEnd(e);
}
/// client thread: send what is due (kept while the player is between servers, dropped after 20 s)
void runActions(ClientInstance* ci, LocalPlayer* lp) {
    std::vector<Action> due;
    {
        std::lock_guard l(gActMutex);
        if (gActs.empty()) return;
        auto now = Clock::now();
        for (size_t i = 0; i < gActs.size();) {
            if (now - gActs[i].at > std::chrono::seconds(20)) { gActs.erase(gActs.begin() + i); continue; }
            if (gActs[i].at <= now && ci && lp) { due.push_back(std::move(gActs[i])); gActs.erase(gActs.begin() + i); continue; }
            ++i;
        }
    }
    for (auto& a : due) {
        if (!a.command) {
            TextPacket pkt = TextPacketPayload::createChat(lp->getRealName(), a.text, std::nullopt, lp->getXuid(), "");
            ci->getPacketSender().sendToServer(pkt);
            continue;
        }
        std::shared_ptr<Packet> sp = MinecraftPackets::createPacket(MinecraftPacketIds::CommandRequest);
        auto*                   c  = static_cast<CommandRequestPacket*>(sp.get());
        if (!c) continue;
        c->mCommand         = a.text;
        c->mOrigin->mType   = CommandOriginType::Player;
        c->mInternalSource  = true;
        c->mVersion         = CurrentCmdVersion::Latest;
        ci->getPacketSender().sendToServer(*c);
    }
}
std::string messageOf(TextPacket const& p) {
    return std::visit([](auto const& b) { return std::string(b.mMessage.get()); }, p.mBody.get());
}

/// Auto sprint: the sprint key held for the game (same as holding Ctrl - the game still decides whether you can sprint:
/// forward only, enough food, not sneaking), re-asserted every frame because a key-up event clears it.
bool gSprintSet = false;
void applySprint(LocalPlayer* lp) {
    bool want = gAutoSprint && lp && rf::screens::playing();
    if (!want && !gSprintSet) return;
    if (!lp) { gSprintSet = false; return; }
    auto mi = lp->getEntityContext().tryGetComponent<MoveInputComponent>();
    if (!mi) return;
    size_t const bit = size_t(MoveInputState::Flag::SprintDown);
    if (want) {
        mi->mRawInputState->mFlagValues->set(bit, true);
        mi->mInputState->mFlagValues->set(bit, true);
        gSprintSet = true;
    } else { // let go once (a sprint key the player really holds sets it again on its own)
        if (!down(VK_CONTROL)) {
            mi->mRawInputState->mFlagValues->set(bit, false);
            mi->mInputState->mFlagValues->set(bit, false);
        }
        gSprintSet = false;
    }
}
/// Fullbright: the game's brightness option above its slider's 0..1 range. The player's own value is kept (also in the
/// cfg, so a crash while it was on still gets it back on the next start).
bool gBrightApplied = false;
void applyBright(ClientInstance& c) {
    Option* o = c.getOptions().get(OptionID::Gamma).get();
    if (!o) return;
    float& v = static_cast<FloatOption*>(o)->mValue; // TypedStorage of a scalar is the scalar itself
    if (gBright.on) {
        if (!gBrightApplied) {
            if (v <= 1.0f) gBright.orig = v;
            gBrightApplied = true;
        }
        if (v != gBright.gamma) v = gBright.gamma;
    } else if (gBrightApplied || (v > 1.0f && gBright.orig >= 0.0f)) {
        v              = gBright.orig >= 0.0f && gBright.orig <= 1.0f ? gBright.orig : 0.5f;
        gBrightApplied = false;
    }
}
void snapshot() {
    auto  ci = ll::service::getClientInstance();
    auto* lp = ci ? ci->getLocalPlayer() : nullptr;
    applySprint(lp); // every frame
    runActions(ci ? &*ci : nullptr, lp);
    auto now = Clock::now();
    if (now - gLastFast < std::chrono::milliseconds(50)) return; // 20 Hz is plenty for a HUD
    gLastFast = now;
    if (ci) applyBright(*ci);
    if (!gHudOn) return;
    Snap s;
    bool list = false;
    if (lp) {
        Actor& a  = *lp;
        Vec3   f  = a.getFeetPos();
        s.inWorld = true;
        s.x = f.x, s.y = f.y, s.z = f.z;
        Vec2 const& r = a.getRotation();
        s.pitch       = r.x;
        s.yaw         = r.y;
        if (anyGameModule()) {
            s.health    = a.getHealth();
            s.maxHealth = a.getMaxHealth();
            s.armor     = lp->getArmorValue();
            for (int i = 0; i < 4; ++i) fill(s.armorPiece[i], a.getArmor(SharedTypes::Legacy::ArmorSlot(i)));
            fill(s.held, lp->getSelectedItem());
        }
        if (gMods[MTab].on && now - gLastList >= std::chrono::milliseconds(500)) {
            gLastList = now;
            list      = true;
            for (auto const& kv : a.getLevel().getPlayerList()) s.players.push_back(kv.second.mName.get());
            std::sort(s.players.begin(), s.players.end(), [](std::string const& l, std::string const& r) {
                return _stricmp(stripCodes(l).c_str(), stripCodes(r).c_str()) < 0;
            });
        }
    }
    std::lock_guard l(gSnapMutex);
    if (!list && s.inWorld) s.players = std::move(gSnap.players); // keep the last list between refreshes
    gSnap = std::move(s);
}

// ---------------------------------------------------------------- input (ImGui frame)
std::deque<double> gClicksL, gClicksR;
bool               gLWas = false, gRWas = false;
void pollClicks(double t, bool live) {
    bool l = live && down(VK_LBUTTON), r = live && down(VK_RBUTTON);
    if (l && !gLWas) gClicksL.push_back(t);
    if (r && !gRWas) gClicksR.push_back(t);
    gLWas = l, gRWas = r;
    while (!gClicksL.empty() && t - gClicksL.front() > 1.0) gClicksL.pop_front();
    while (!gClicksR.empty() && t - gClicksR.front() > 1.0) gClicksR.pop_front();
}

// ---------------------------------------------------------------- module bodies
float hudPx() { return 18.0f * uiScale() * gScale; }

void bodyFps() { text(IM_COL32(255, 255, 255, 255), sfmt("%d FPS", int(ImGui::GetIO().Framerate + 0.5f))); }
void bodyCps() { text(IM_COL32(255, 255, 255, 255), sfmt("CPS %d | %d", int(gClicksL.size()), int(gClicksR.size()))); }
void bodyCoords(Snap const& s) {
    if (gCoordsDec) text(IM_COL32(255, 255, 255, 255), sfmt("X %.1f  Y %.1f  Z %.1f", s.x, s.y, s.z));
    else text(IM_COL32(255, 255, 255, 255), sfmt("X %d  Y %d  Z %d", int(std::floor(s.x)), int(std::floor(s.y)), int(std::floor(s.z))));
}
void bodyDir(Snap const& s) {
    static char const* kFace[4] = {"South (+Z)", "West (-X)", "North (-Z)", "East (+X)"}; // Bedrock yaw: 0 = +Z
    float y = std::fmod(s.yaw, 360.0f);
    if (y < 0) y += 360.0f;
    int i = int((y + 45.0f) / 90.0f) % 4;
    text(IM_COL32(255, 255, 255, 255), sfmt("Facing %s", kFace[i]));
    text(IM_COL32(200, 200, 200, 255), sfmt("Yaw %.1f  Pitch %.1f", s.yaw, s.pitch));
}
void bodyKeys() {
    float k   = std::round(30.0f * uiScale() * gScale), g = std::max(2.0f, std::round(3.0f * uiScale() * gScale));
    bool  live = gameFocused();
    ImVec2 o  = ImGui::GetCursorScreenPos();
    auto*  dl = ImGui::GetWindowDrawList();
    float  r  = std::round(4.0f * uiScale());
    ImU32  bg = IM_COL32(0, 0, 0, gBgAlpha), on = (gAccent & 0x00FFFFFF) | (IM_COL32(0, 0, 0, 210) & 0xFF000000);
    auto box = [&](float x, float y, float w, float h, bool pressed, std::string const& label) {
        ImVec2 a(o.x + x, o.y + y), b(o.x + x + w, o.y + y + h);
        dl->AddRectFilled(a, b, pressed ? on : bg, r);
        ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        ImVec2 tp(std::round(a.x + (w - ts.x) * 0.5f), std::round(a.y + (h - ts.y) * 0.5f));
        float  so = std::max(1.0f, std::round(ImGui::GetFontSize() / 14.0f));
        if (gShadow) dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(tp.x + so, tp.y + so), IM_COL32(0, 0, 0, 160), label.c_str());
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), tp, IM_COL32(255, 255, 255, 255), label.c_str());
    };
    float W3 = 3 * k + 2 * g;
    box(k + g, 0, k, k, live && down('W'), "W");
    box(0, k + g, k, k, live && down('A'), "A");
    box(k + g, k + g, k, k, live && down('S'), "S");
    box(2 * (k + g), k + g, k, k, live && down('D'), "D");
    float half = (W3 - g) * 0.5f;
    box(0, 2 * (k + g), half, k, live && down(VK_LBUTTON), sfmt("LMB %d", int(gClicksL.size())));
    box(half + g, 2 * (k + g), half, k, live && down(VK_RBUTTON), sfmt("RMB %d", int(gClicksR.size())));
    box(0, 3 * (k + g), W3, k * 0.6f, live && down(VK_SPACE), "____");
    ImGui::Dummy(ImVec2(W3, 3 * (k + g) + k * 0.6f));
}
void bodyArmor(Snap const& s) {
    text(IM_COL32(255, 255, 255, 255), sfmt("HP %d/%d   Armor %d", s.health, s.maxHealth, s.armor));
    float  barW = std::round(46.0f * uiScale() * gScale), barH = std::max(2.0f, std::round(4.0f * uiScale() * gScale));
    auto   row  = [&](Piece const& p) {
        if (!p.has) return;
        ImVec2 c  = ImGui::GetCursorScreenPos();
        float  fs = ImGui::GetFontSize();
        auto*  dl = ImGui::GetWindowDrawList();
        if (p.maxDmg > 0) {
            float f = std::clamp(1.0f - float(p.dmg) / float(p.maxDmg), 0.0f, 1.0f);
            float y = std::round(c.y + (fs - barH) * 0.5f);
            dl->AddRectFilled(ImVec2(c.x, y), ImVec2(c.x + barW, y + barH), IM_COL32(0, 0, 0, 200));
            dl->AddRectFilled(ImVec2(c.x, y), ImVec2(c.x + std::round(barW * f), y + barH), durColour(f));
            ImGui::Dummy(ImVec2(barW, fs));
            ImGui::SameLine();
            text(durColour(f), sfmt("%d", p.maxDmg - p.dmg));
            ImGui::SameLine();
        } else if (p.count > 1) {
            text(IM_COL32(255, 255, 255, 255), sfmt("x%d", p.count));
            ImGui::SameLine();
        }
        text(IM_COL32(230, 230, 230, 255), p.name);
    };
    for (auto const& p : s.armorPiece) row(p);
    if (gArmorHeld) row(s.held);
}
void bodyTab(Snap const& s) {
    text(gAccent, sfmt("Players (%d)", int(s.players.size())));
    ImGui::Separator();
    int n = int(s.players.size()), cols = n > 36 ? 4 : n > 24 ? 3 : n > 12 ? 2 : 1;
    if (cols > 1 && ImGui::BeginTable("##bftab", cols, ImGuiTableFlags_SizingFixedFit)) {
        for (auto const& p : s.players) {
            ImGui::TableNextColumn();
            mcText(p, IM_COL32(255, 255, 255, 255));
        }
        ImGui::EndTable();
    } else
        for (auto const& p : s.players) mcText(p, IM_COL32(255, 255, 255, 255));
}

void drawCrosshair() {
    ImGuiIO& io = ImGui::GetIO();
    auto*    dl = ImGui::GetBackgroundDrawList();
    float    s  = uiScale();
    ImVec2   c(std::floor(io.DisplaySize.x * 0.5f), std::floor(io.DisplaySize.y * 0.5f));
    float    len = std::round(gCross.size * s), gap = std::round(gCross.gap * s), t = std::max(1.0f, std::round(gCross.thick * s));
    float    h   = t * 0.5f;
    auto rect = [&](float x0, float y0, float x1, float y1, ImU32 col, float grow) {
        dl->AddRectFilled(ImVec2(x0 - grow, y0 - grow), ImVec2(x1 + grow, y1 + grow), col);
    };
    auto shapes = [&](ImU32 col, float grow) {
        bool cross = gCross.style == 0 || gCross.style == 3 || gCross.style == 4;
        if (cross) {
            rect(c.x - gap - len, c.y - h, c.x - gap, c.y + h, col, grow);           // left
            rect(c.x + gap, c.y - h, c.x + gap + len, c.y + h, col, grow);           // right
            rect(c.x - h, c.y + gap, c.x + h, c.y + gap + len, col, grow);           // bottom
            if (gCross.style != 4) rect(c.x - h, c.y - gap - len, c.x + h, c.y - gap, col, grow); // top (not on T)
        }
        if (gCross.style == 1 || gCross.style == 3) rect(c.x - h, c.y - h, c.x + h, c.y + h, col, grow);
        if (gCross.style == 2) dl->AddCircle(c, len + gap, col, 40, t + 2 * grow);
    };
    if (gCross.outline) shapes(IM_COL32(0, 0, 0, 200), 1.0f);
    shapes(gCross.color, 0.0f);
}

/// one HUD module = one small ImGui window (NoInputs unless the layout is being edited)
template <class F> void moduleWindow(Module& m, bool background, F&& body) {
    ImGuiIO& io = ImGui::GetIO();
    float    W = io.DisplaySize.x, H = io.DisplaySize.y;
    bool     edit = gEdit && gMenu.load();
    ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (!edit) f |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove;
    if (!background && !edit) f |= ImGuiWindowFlags_NoBackground;
    ImGui::SetNextWindowPos(ImVec2(m.x * W, m.y * H), edit ? ImGuiCond_Appearing : ImGuiCond_Always, ImVec2(m.px, m.py));
    float s = uiScale() * gScale;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, std::round(5.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(std::round(7.0f * s), std::round(4.0f * s)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, edit ? std::max(1.0f, s) : 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(std::round(5.0f * s), std::round(2.0f * s)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, gBgAlpha));
    ImGui::PushStyleColor(ImGuiCol_Border, gAccent);
    ImGui::PushStyleColor(ImGuiCol_Separator, IM_COL32(255, 255, 255, 60));
    std::string name = std::string("##bfhud_") + m.id;
    if (ImGui::Begin(name.c_str(), nullptr, f)) {
        if (gFont) ImGui::PushFont(gFont);
        ImGui::SetWindowFontScale(fontScaleFor(hudPx()));
        body();
        if (edit) {
            ImVec2 p = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize();
            m.x      = std::clamp((p.x + sz.x * m.px) / W, 0.0f, 1.0f);
            m.y      = std::clamp((p.y + sz.y * m.py) / H, 0.0f, 1.0f);
        }
        if (gFont) ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(5);
}

// ---------------------------------------------------------------- zoom + time changer (game hooks below)
Clock::time_point gZoomLast{};
float             gZoomK = 1.0f;
/// LevelRendererPlayer::getFov: the world FOV narrowed while the zoom key is held (eased in and out)
float zoomFov(float fov, bool variable) {
    auto  now = Clock::now();
    float dt  = gZoomLast.time_since_epoch().count() ? std::chrono::duration<float>(now - gZoomLast).count() : 0.0f;
    gZoomLast = now;
    dt        = std::clamp(dt, 0.0f, 0.1f);
    float target = gZoom.on && gZoomHeld.load(std::memory_order_relaxed) ? std::max(1.0f, gZoom.factor) : 1.0f;
    gZoomK       = gZoom.smooth ? gZoomK + (target - gZoomK) * std::min(1.0f, dt * 14.0f) : target;
    if (std::fabs(gZoomK - target) < 0.002f) gZoomK = target;
    if (gZoomK <= 1.0005f) return fov;
    if (!variable && std::fabs(fov - 70.0f) < 0.01f) return fov; // the held item's fixed FOV stays
    constexpr float kRad = 3.14159265f / 180.0f;
    return 2.0f * std::atan(std::tan(fov * kRad * 0.5f) / gZoomK) / kRad;
}
/// Dimension::getTimeOfDay: the sky, sun and light follow our time instead of the server's (client only on servers)
int timeOfDay(int time) { return gTime.on ? std::clamp(gTime.ticks, 0, 23999) : time; }

// ---------------------------------------------------------------- menu bits
int  gCapture = 0; // waiting for a key: 1 menu, 2 player list, 3 zoom
int* captureTarget() { return gCapture == 1 ? &gMenuKey : gCapture == 2 ? &gTabKey : &gZoom.key; }
void captureKey() {
    if (!gCapture) return;
    if (down(VK_ESCAPE)) { gCapture = 0; return; }
    static int const kOrder[] = {VK_RSHIFT, VK_LSHIFT, VK_RCONTROL, VK_LCONTROL};
    for (int vk : kOrder)
        if (down(vk)) {
            *captureTarget() = vk;
            gCapture         = 0;
            return;
        }
    for (int vk = 0x08; vk < 0xFF; ++vk) {
        if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) continue;
        if (vk >= VK_LSHIFT && vk <= VK_RMENU) continue;
        if (down(vk)) {
            *captureTarget() = vk;
            gCapture         = 0;
            return;
        }
    }
}
/// single-line text field bound to a std::string (ImGui keeps its own buffer while the field is being edited)
void inputString(char const* label, std::string& str, float w) {
    char buf[128];
    std::snprintf(buf, sizeof buf, "%s", str.c_str());
    ImGui::SetNextItemWidth(w);
    if (ImGui::InputText(label, buf, sizeof buf)) {
        str = buf;
        str.erase(std::remove(str.begin(), str.end(), '\n'), str.end());
    }
}
/// a button that shows a key and waits for a new one when clicked
void keyButton(char const* id, int which, int key) {
    std::string lbl = (gCapture == which ? std::string("press a key... (Esc cancels)") : keyName(key)) + "##" + id;
    if (ImGui::SmallButton(lbl.c_str())) gCapture = which;
}
void closeMenu() {
    gMenu    = false;
    gEdit    = false;
    gCapture = 0;
    saveConfig();
}
} // namespace

// ---------------------------------------------------------------- public
void loadConfig(std::filesystem::path const& configDir) {
    std::error_code ec;
    std::filesystem::create_directories(configDir, ec);
    gCfgFile = configDir / "BedrockForgeClient.cfg";
    std::ifstream in(gCfgFile);
    if (!in) return;
    auto        es = entries();
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t sp = line.find(' ');
        if (sp == std::string::npos) continue;
        std::string key = line.substr(0, sp), val = line.substr(sp + 1);
        for (auto& e : es) {
            if (e.key != key) continue;
            switch (e.t) {
            case 'b': *static_cast<bool*>(e.p) = val == "1" || val == "true"; break;
            case 'i': *static_cast<int*>(e.p) = std::atoi(val.c_str()); break;
            case 'f': *static_cast<float*>(e.p) = float(std::atof(val.c_str())); break;
            case 'u': *static_cast<ImU32*>(e.p) = ImU32(std::strtoul(val.c_str(), nullptr, 16)); break;
            case 's': *static_cast<std::string*>(e.p) = val; break;
            }
        }
    }
    gCross.style = std::clamp(gCross.style, 0, 4);
    gScale       = std::clamp(gScale, 0.5f, 2.5f);
    gBgAlpha     = std::clamp(gBgAlpha, 0, 255);
}
void saveConfig() {
    if (gCfgFile.empty()) return;
    std::ofstream out(gCfgFile, std::ios::trunc);
    if (!out) return;
    for (auto& e : entries()) {
        out << e.key << ' ';
        switch (e.t) {
        case 'b': out << (*static_cast<bool*>(e.p) ? 1 : 0); break;
        case 'i': out << *static_cast<int*>(e.p); break;
        case 'f': out << *static_cast<float*>(e.p); break;
        case 'u': out << sfmt("%08X", unsigned(*static_cast<ImU32*>(e.p))); break;
        case 's': out << *static_cast<std::string*>(e.p); break;
        }
        out << '\n';
    }
}

void loadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.Fonts->Fonts.empty()) io.Fonts->AddFontDefault(); // ImGui's own font stays font 0 for every other window
    char win[MAX_PATH] = {};
    if (!GetWindowsDirectoryA(win, MAX_PATH)) return;
    for (char const* f : {"\\Fonts\\seguisb.ttf", "\\Fonts\\segoeui.ttf", "\\Fonts\\arial.ttf"}) {
        std::string path = std::string(win) + f;
        if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        cfg.OversampleV = 2;
        gFont = io.Fonts->AddFontFromFileTTF(path.c_str(), 32.0f, &cfg);
        if (gFont) break;
    }
}

bool menuOpen() { return gMenu.load(); }
bool wantsInput() { return gMenu.load(); }
bool blocksGameMouse() {
    if (!gMenu.load() && !gBtnHover.load()) return false;
    return ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
}
void toggleMenu() {
    if (gMenu.load()) closeMenu();
    else gMenu = true;
}
bool isMenuKey(unsigned long long wp, long long lp) {
    int k = gMenuKey;
    if (k <= 0 || gCapture) return false;
    UINT sc  = UINT((lp >> 16) & 0xFF);
    bool ext = ((lp >> 24) & 1) != 0;
    if (k == VK_RSHIFT || k == VK_LSHIFT) return wp == VK_SHIFT && ((sc == 0x36) == (k == VK_RSHIFT));
    if (k == VK_RCONTROL || k == VK_LCONTROL) return wp == VK_CONTROL && (ext == (k == VK_RCONTROL));
    return int(wp) == k;
}

void drawOverlay() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x < 10 || io.DisplaySize.y < 10) return;
    Snap s;
    {
        std::lock_guard l(gSnapMutex);
        s = gSnap;
    }
    bool pause   = rf::sgconfig::pauseOnTop();
    bool inGame  = s.inWorld && rf::screens::hudOnTop() && !pause && !rf::sgconfig::isOpen();
    bool editing = gEdit && gMenu.load() && s.inWorld;
    double t     = ImGui::GetTime();
    pollClicks(t, inGame && gameFocused());
    gZoomHeld = gZoom.on && inGame && rf::screens::playing() && !gMenu.load() && gameFocused() && down(gZoom.key);
    if (!gHudOn || !(inGame || editing)) return;

    if (gMods[MFps].on) moduleWindow(gMods[MFps], true, [] { bodyFps(); });
    if (gMods[MCps].on) moduleWindow(gMods[MCps], true, [] { bodyCps(); });
    if (gMods[MCoords].on) moduleWindow(gMods[MCoords], true, [&] { bodyCoords(s); });
    if (gMods[MDir].on) moduleWindow(gMods[MDir], true, [&] { bodyDir(s); });
    if (gMods[MKeys].on) moduleWindow(gMods[MKeys], false, [] { bodyKeys(); });
    if (gMods[MArmor].on) moduleWindow(gMods[MArmor], true, [&] { bodyArmor(s); });
    if (gMods[MTab].on && (editing || !gTabHold || (gameFocused() && down(gTabKey))) && !s.players.empty())
        moduleWindow(gMods[MTab], true, [&] { bodyTab(s); });
    if (gCross.on && inGame) drawCrosshair();
}

void drawMenu() {
    ImGuiIO& io = ImGui::GetIO();
    float    W = io.DisplaySize.x, H = io.DisplaySize.y;
    if (W < 10 || H < 10) return;
    float s = uiScale();
    // pause screen: the way in with the mouse (Right Shift works anywhere)
    bool pause = rf::sgconfig::pauseOnTop();
    gBtnHover  = false;
    if (!gMenu.load()) {
        if (!pause || rf::sgconfig::isOpen()) return;
        float ms = std::max(1.0f, std::floor(H / 360.0f));
        ImGui::SetNextWindowPos(ImVec2(8 * ms, H - 58 * ms));
        ImGui::Begin("##bfclientbtn", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize);
        if (gFont) ImGui::PushFont(gFont);
        ImGui::SetWindowFontScale(fontScaleFor(17.0f * s));
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(20, 22, 30, 220));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, gAccent);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f * s);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12 * s, 6 * s));
        if (ImGui::Button("Bedrock Forge Client")) gMenu = true;
        gBtnHover = ImGui::IsItemHovered();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        if (gFont) ImGui::PopFont();
        ImGui::End();
        return;
    }
    captureKey();

    ImGui::SetNextWindowPos(ImVec2(W * 0.5f, H * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::round(620 * s), std::round(500 * s)), ImGuiCond_Appearing);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * s, 12 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8 * s, 6 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6 * s, 4 * s));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(14, 15, 20, 238));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, IM_COL32(14, 15, 20, 255));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, IM_COL32(24, 26, 36, 255));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, gAccent);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, gAccent);
    ImGui::PushStyleColor(ImGuiCol_Tab, IM_COL32(30, 32, 44, 255));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, gAccent);
    ImGui::PushStyleColor(ImGuiCol_TabSelected, (gAccent & 0x00FFFFFF) | 0xC0000000);
    bool open = true;
    if (ImGui::Begin("Bedrock Forge Client", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        if (gFont) ImGui::PushFont(gFont);
        ImGui::SetWindowFontScale(fontScaleFor(17.0f * s));
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(gAccent), "Bedrock Forge Client");
        ImGui::SameLine();
        ImGui::TextDisabled("%s to close", keyName(gMenuKey).c_str());
        if (!pause) ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "Press Esc to use your mouse here.");
        ImGui::Separator();
        if (ImGui::BeginTabBar("##bftabs")) {
            if (ImGui::BeginTabItem("HUD")) {
                ImGui::Checkbox("HUD on", &gHudOn);
                ImGui::SameLine();
                ImGui::Checkbox("Edit layout (drag the modules)", &gEdit);
                ImGui::SameLine();
                if (ImGui::SmallButton("Reset positions"))
                    for (auto& m : gMods) m.x = m.dx, m.y = m.dy;
                ImGui::Separator();
                for (auto& m : gMods) {
                    ImGui::PushID(m.id);
                    ImGui::Checkbox(m.label, &m.on);
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", m.desc);
                    if (m.on) {
                        ImGui::Indent();
                        if (&m == &gMods[MCoords]) ImGui::Checkbox("Decimals", &gCoordsDec);
                        if (&m == &gMods[MArmor]) ImGui::Checkbox("Show held item", &gArmorHeld);
                        if (&m == &gMods[MTab]) {
                            ImGui::Checkbox("Only while holding the key", &gTabHold);
                            if (gTabHold) {
                                ImGui::SameLine();
                                keyButton("tabkey", 2, gTabKey);
                            }
                        }
                        ImGui::Unindent();
                    }
                    ImGui::PopID();
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Crosshair")) {
                ImGui::Checkbox("Custom crosshair", &gCross.on);
                ImGui::TextDisabled("Drawn over the game's own crosshair.");
                char const* styles[] = {"Cross", "Dot", "Circle", "Cross + dot", "T"};
                ImGui::SetNextItemWidth(200 * s);
                ImGui::Combo("Style", &gCross.style, styles, 5);
                ImGui::SetNextItemWidth(200 * s);
                ImGui::SliderFloat("Length", &gCross.size, 1.0f, 30.0f, "%.0f");
                ImGui::SetNextItemWidth(200 * s);
                ImGui::SliderFloat("Gap", &gCross.gap, 0.0f, 20.0f, "%.0f");
                ImGui::SetNextItemWidth(200 * s);
                ImGui::SliderFloat("Thickness", &gCross.thick, 1.0f, 8.0f, "%.0f");
                ImVec4 c = ImGui::ColorConvertU32ToFloat4(gCross.color);
                if (ImGui::ColorEdit4("Colour", &c.x, ImGuiColorEditFlags_NoInputs)) gCross.color = ImGui::ColorConvertFloat4ToU32(c);
                ImGui::Checkbox("Black outline", &gCross.outline);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Visual")) {
                ImGui::Checkbox("Zoom", &gZoom.on);
                ImGui::SameLine();
                ImGui::TextDisabled("hold");
                ImGui::SameLine();
                keyButton("zoomkey", 3, gZoom.key);
                if (gZoom.on) {
                    ImGui::Indent();
                    ImGui::SetNextItemWidth(200 * s);
                    ImGui::SliderFloat("Zoom amount", &gZoom.factor, 1.5f, 10.0f, "%.1fx");
                    ImGui::Checkbox("Smooth", &gZoom.smooth);
                    ImGui::Unindent();
                }
                ImGui::Separator();
                ImGui::Checkbox("Time changer", &gTime.on);
                ImGui::SameLine();
                ImGui::TextDisabled("your sky only - servers still run their own time");
                if (gTime.on) {
                    ImGui::Indent();
                    ImGui::SetNextItemWidth(260 * s);
                    ImGui::SliderInt("Time", &gTime.ticks, 0, 23999);
                    if (ImGui::SmallButton("Sunrise")) gTime.ticks = 0;
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Noon")) gTime.ticks = 6000;
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Sunset")) gTime.ticks = 12000;
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Midnight")) gTime.ticks = 18000;
                    ImGui::TextDisabled("In single player this also changes the world's own day/night.");
                    ImGui::Unindent();
                }
                ImGui::Separator();
                ImGui::Checkbox("Fullbright", &gBright.on);
                if (gBright.on) {
                    ImGui::Indent();
                    ImGui::SetNextItemWidth(200 * s);
                    ImGui::SliderFloat("Brightness", &gBright.gamma, 1.5f, 25.0f, "%.1f");
                    ImGui::TextDisabled("Turning it off gives your own brightness back. Vibrant Visuals may ignore it.");
                    ImGui::Unindent();
                }
                ImGui::Separator();
                ImGui::TextDisabled("Post effects (bloom, motion blur, chromatic aberration, sharpening):");
                rf::postfx::drawUi();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Utility")) {
                ImGui::Checkbox("Auto sprint", &gAutoSprint);
                ImGui::SameLine();
                ImGui::TextDisabled("like holding the sprint key - forward only, the game's normal rules");
                ImGui::Separator();
                ImGui::Checkbox("Auto GG", &gAuto.gg);
                ImGui::SameLine();
                ImGui::TextDisabled("says it in chat when a game ends");
                if (gAuto.gg) {
                    ImGui::Indent();
                    inputString("Message", gAuto.ggMsg, 160 * s);
                    ImGui::SetNextItemWidth(160 * s);
                    ImGui::SliderFloat("Delay##gg", &gAuto.ggDelay, 0.2f, 5.0f, "%.1f s");
                    ImGui::Unindent();
                }
                ImGui::Checkbox("Auto re-queue", &gAuto.rq);
                ImGui::SameLine();
                ImGui::TextDisabled("joins the next game of the same mode");
                if (gAuto.rq) {
                    ImGui::Indent();
                    ImGui::SetNextItemWidth(160 * s);
                    ImGui::SliderFloat("Delay##rq", &gAuto.rqDelay, 0.5f, 10.0f, "%.1f s");
                    ImGui::Checkbox("Also when you die (The Hive solo modes)", &gAuto.rqOnDeath);
                    ImGui::TextDisabled("The Hive: found by itself (/q with the mode you were in).");
                    inputString("Other servers' command", gAuto.rqCustom, 200 * s);
                    ImGui::TextDisabled("For other servers: their play-again command if they have one. Empty = The Hive only.");
                    ImGui::Unindent();
                }
                {
                    std::string game;
                    {
                        std::lock_guard l(gGameMutex);
                        game = gHiveGame;
                    }
                    ImGui::TextDisabled("Seen: %s%s   GG sent %ld   re-queues %ld", gOnHive.load() ? "The Hive" : "no Hive game yet",
                                        game.empty() ? "" : (" (" + game + ")").c_str(), gGgSent.load(), gRqSent.load());
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Settings")) {
                ImGui::SetNextItemWidth(220 * s);
                ImGui::SliderFloat("HUD size", &gScale, 0.5f, 2.5f, "%.2fx");
                ImGui::SetNextItemWidth(220 * s);
                ImGui::SliderInt("Background opacity", &gBgAlpha, 0, 255);
                ImGui::Checkbox("Text shadow", &gShadow);
                ImVec4 a = ImGui::ColorConvertU32ToFloat4(gAccent);
                if (ImGui::ColorEdit3("Accent colour", &a.x, ImGuiColorEditFlags_NoInputs)) gAccent = ImGui::ColorConvertFloat4ToU32(a) | 0xFF000000;
                ImGui::Text("Menu key:");
                ImGui::SameLine();
                keyButton("menukey", 1, gMenuKey);
                ImGui::Separator();
                ImGui::TextDisabled("Saved to BedrockForgeClient.cfg in Renderer Foundation's config folder.");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Coming next")) {
                ImGui::TextWrapped("On the way in the next builds:");
                for (char const* f : {"Potion effects HUD", "Ping",
                                      "Lower mouse sensitivity while zoomed", "Movable vanilla HUD (hotbar, chat, scoreboard)",
                                      "Replay mode"})
                    ImGui::BulletText("%s", f);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        if (gFont) ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleColor(8);
    ImGui::PopStyleVar(5);
    if (!open) closeMenu();
}
} // namespace rf::hud

// The world camera updates on the client thread every frame: copy what the HUD shows from there.
LL_TYPE_INSTANCE_HOOK(RfHudSnapshotHook, ll::memory::HookPriority::Low, mce::Camera, &mce::Camera::updateViewMatrixDependencies, void) {
    origin();
    rf::hud::snapshot();
}

// Zoom: the world FOV while the zoom key is held.
LL_TYPE_INSTANCE_HOOK(RfZoomHook, ll::memory::HookPriority::Normal, LevelRendererPlayer, &LevelRendererPlayer::getFov, float, float a,
                      bool variable) {
    return rf::hud::zoomFov(origin(a, variable), variable);
}
// Auto GG / re-queue: chat and title packets as they arrive (a /connection answer we asked for is kept out of chat).
LL_TYPE_INSTANCE_HOOK(RfTextPacketHook, ll::memory::HookPriority::Normal, ClientNetworkHandler, &ClientNetworkHandler::$handle, void,
                      NetworkIdentifier const& source, TextPacket const& packet) {
    if (!rf::hud::onText(rf::hud::messageOf(packet))) return;
    origin(source, packet);
}
LL_TYPE_INSTANCE_HOOK(RfTitlePacketHook, ll::memory::HookPriority::Normal, ClientNetworkHandler, &ClientNetworkHandler::$handle, void,
                      NetworkIdentifier const& source, SetTitlePacket const& packet) {
    rf::hud::onTitle(packet.mTitleText.get());
    origin(source, packet);
}
// Time changer: the sky / sun / light curve evaluated at our time.
LL_TYPE_INSTANCE_HOOK(RfTimeHook, ll::memory::HookPriority::Normal, Dimension, &Dimension::$getTimeOfDay, float, int time, float a) {
    return origin(rf::hud::timeOfDay(time), a);
}

namespace rf::hud {
void install() {
    if (gHooked) return;
    RfHudSnapshotHook::hook();
    RfZoomHook::hook();
    RfTimeHook::hook();
    RfTextPacketHook::hook();
    RfTitlePacketHook::hook();
    gHooked = true;
}
void uninstall() {
    if (!gHooked) return;
    RfHudSnapshotHook::unhook();
    RfZoomHook::unhook();
    RfTimeHook::unhook();
    RfTextPacketHook::unhook();
    RfTitlePacketHook::unhook();
    gHooked = false;
    saveConfig();
}
} // namespace rf::hud
