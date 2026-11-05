// Bedrock Forge client - server automation modules: Auto Sprint, Auto GG, Auto Re-queue.
//
// Auto Sprint: the sprint key held for the game every frame (MoveInputState SprintDown on the player's
// MoveInputComponent, raw + processed) - the same as holding Ctrl, so the game still decides whether you can sprint
// (forward only, enough food, not sneaking) and sends normal input to the server.
//
// Auto GG / Auto Re-queue: game ends are read from chat and title packets (ClientNetworkHandler::$handle) - patterns of
// The Hive, CubeCraft, Lifeboat, Zeqa, Galaxite and Mineville. What we send goes out a moment later from the HUD frame
// (client thread): chat through TextPacketPayload::createChat, commands through a CommandRequest packet. On The Hive
// the mode to re-queue for comes from its own /connection command ("You are connected to server name SKY-DUOS12" ->
// /q sky-duos), whose answer is kept out of chat. Nothing Hive-specific is sent before The Hive was seen.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "ll/api/memory/Hook.h"

#include "mc/client/game/IClientInstance.h"
#include "mc/client/network/ClientNetworkHandler.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/entity/components/MoveInputComponent.h"
#include "mc/input/MoveInputState.h"
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
#include "mc/world/actor/player/Player.h"

#include "client/Module.h"
#include "client/ServerAuto.h"

namespace bedrock_forge::client::serverauto {
namespace {
using Clock = std::chrono::steady_clock;

std::vector<std::string> const kGgMessages = {"gg", "gg wp", "GG", "good game", "gg!"};

struct Action {
    Clock::time_point at;
    bool              command;
    std::string       text;
};
std::mutex          gMutex;
std::vector<Action> gActs;
Clock::time_point   gLastEnd{}, gAwaitUntil{};
std::atomic<bool>   gAwaitConn{false};
std::atomic<bool>   gOnHive{false};
std::string         gHiveGame;
bool                gSprintSet = false;

bool has(std::string const& s, char const* p) { return s.find(p) != std::string::npos; }

/// drops Minecraft formatting codes (UTF-8 section sign + one character)
std::string stripCodes(std::string const& s) {
    std::string r;
    r.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (uint8_t(s[i]) == 0xC2 && i + 1 < s.size() && uint8_t(s[i + 1]) == 0xA7) { i += 2; continue; }
        r += s[i];
    }
    return r;
}

void queue(float delaySec, bool command, std::string text) {
    std::lock_guard l(gMutex);
    gActs.push_back({Clock::now() + std::chrono::milliseconds(int(std::clamp(delaySec, 0.0f, 30.0f) * 1000.0f)), command, std::move(text)});
}

enum : int { EndNone, EndHive, EndOther, EndDeath };
int endFromText(std::string const& m) {
    if (stripCodes(m) == "» Game OVER!") return EndHive;                                // The Hive
    if (has(m, "§a won the game!") || has(m, "§a has won the game!")) return EndOther; // CubeCraft / Lifeboat
    return EndNone;
}
int endFromTitle(std::string const& t) {
    if (t == "§f§aYou won the game!" || t == "§f§cYou lost the game!") return EndOther; // Zeqa
    if (has(t, "Team§r§a won the game!") || has(t, "§bHiders§r§f Win") || has(t, "§eSeekers§r§f Win") || t == "Finished" ||
        t == "Out of Time!")
        return EndOther;                                                                  // Galaxite
    if (t == "§aYou Win!" || t == "§cGame Over!") return EndOther;                          // Mineville
    if (t == "§cYou died!" || t == "§7You're spectating the §as§eh§6o§cw§7!") return EndDeath; // The Hive (solo)
    return EndNone;
}

void onGameEnd(int kind) {
    if (kind == EndHive) gOnHive = true;
    if (kind == EndOther) gOnHive = false;
    if (kind == EndDeath && !gOnHive.load()) return; // /q only exists where The Hive was seen
    auto now = Clock::now();
    if (now - gLastEnd < std::chrono::seconds(8)) return; // one end seen as a title and a chat line
    gLastEnd = now;
    auto& reg = Registry::get();
    if (kind != EndDeath && reg.on("auto_gg")) {
        auto*  m   = reg.find("auto_gg");
        size_t idx = size_t(std::clamp(int(m->num("message", 0) + 0.5f), 0, int(kGgMessages.size()) - 1));
        queue(m->num("delay", 1.0f), false, kGgMessages[idx]);
    }
    if (!reg.on("auto_requeue")) return;
    auto* rq = reg.find("auto_requeue");
    if (kind == EndDeath && !rq->flag("ondeath", false)) return;
    if (kind == EndHive || kind == EndDeath) {
        float d = std::max(0.3f, rq->num("delay", 2.0f) - 0.4f);
        queue(d, true, "/connection"); // its answer queues /q <mode> (onText)
        gAwaitUntil = now + std::chrono::milliseconds(int(d * 1000.0f) + 6000);
        gAwaitConn  = true;
    }
}

/// false: keep this message out of chat (our own /connection question)
bool onText(std::string const& m) {
    if (gAwaitConn.load()) {
        std::string c = stripCodes(m);
        size_t      k = c.find("You are connected to server name ");
        if (k != std::string::npos || has(c, "You are connected")) {
            gAwaitConn = false;
            if (k != std::string::npos) {
                std::string game;
                for (char ch : c.substr(k + 33))
                    if (!std::isdigit(uint8_t(ch)) && !std::isspace(uint8_t(ch))) game += char(std::tolower(uint8_t(ch)));
                while (!game.empty() && game.back() == '-') game.pop_back();
                {
                    std::lock_guard l(gMutex);
                    gHiveGame = game;
                }
                gOnHive = true;
                if (!game.empty() && game.find("hub") == std::string::npos && game != "replay") queue(0.4f, true, "/q " + game);
            }
            return false;
        }
        if (Clock::now() > gAwaitUntil) gAwaitConn = false;
    }
    if (int e = endFromText(m)) onGameEnd(e);
    return true;
}

std::string messageOf(TextPacket const& p) {
    return std::visit([](auto const& b) { return std::string(b.mMessage.get()); }, p.mBody.get());
}

Clock::time_point gSprintPulse{};
void applySprint(LocalPlayer* lp, bool playing) {
    bool want = Registry::get().on("toggle_sprint") && lp && playing;
    if (!want && !gSprintSet) return;
    if (!lp) { gSprintSet = false; return; }
    auto mi = lp->getEntityContext().tryGetComponent<MoveInputComponent>();
    if (!mi) return;
    size_t const bit = size_t(MoveInputState::Flag::SprintDown);
    if (want) {
        // A sprint key held down the whole time only starts a sprint once: after a hit, a stop or a hunger / server
        // reset (Hive resets it a lot) the game waits for a fresh press. So while walking forward and not sprinting,
        // let go for one frame every 200 ms - a new press, like tapping Shift.
        bool down = true;
        bool const fwd = mi->mRawInputState->mFlagValues->test(size_t(MoveInputState::Flag::Up)) ||
                         mi->mInputState->mFlagValues->test(size_t(MoveInputState::Flag::Up));
        if (fwd && !lp->isSprinting()) {
            auto const now = Clock::now();
            if (now - gSprintPulse > std::chrono::milliseconds(200)) { down = false; gSprintPulse = now; }
        }
        mi->mRawInputState->mFlagValues->set(bit, down);
        mi->mInputState->mFlagValues->set(bit, down);
        gSprintSet = true;
    } else { // let go once (a sprint key really held sets it again with its next event)
        mi->mRawInputState->mFlagValues->set(bit, false);
        mi->mInputState->mFlagValues->set(bit, false);
        gSprintSet = false;
    }
}

void runActions(IClientInstance& client, LocalPlayer* lp) {
    std::vector<Action> due;
    {
        std::lock_guard l(gMutex);
        if (gActs.empty()) return;
        auto now = Clock::now();
        for (size_t i = 0; i < gActs.size();) {
            if (now - gActs[i].at > std::chrono::seconds(20)) { gActs.erase(gActs.begin() + long(i)); continue; }
            if (gActs[i].at <= now && lp) { due.push_back(std::move(gActs[i])); gActs.erase(gActs.begin() + long(i)); continue; }
            ++i;
        }
    }
    for (auto& a : due) {
        try {
            if (!a.command) {
                TextPacket pkt = TextPacketPayload::createChat(lp->getRealName(), a.text, std::nullopt, lp->getXuid(), "");
                client.getPacketSender().sendToServer(pkt);
                continue;
            }
            std::shared_ptr<Packet> sp = MinecraftPackets::createPacket(MinecraftPacketIds::CommandRequest);
            auto*                   c  = static_cast<CommandRequestPacket*>(sp.get());
            if (!c) continue;
            c->mCommand        = a.text;
            c->mOrigin->mType  = CommandOriginType::Player;
            c->mInternalSource = true;
            c->mVersion        = CurrentCmdVersion::Latest;
            client.getPacketSender().sendToServer(*c);
        } catch (...) {}
    }
}
} // namespace

std::vector<std::string> const& ggMessages() { return kGgMessages; }

void frame(IClientInstance& client, LocalPlayer* player, bool playing, bool sprintOk) {
    applySprint(player, sprintOk); // [BF] not gated on "only the HUD drew": servers can keep extra screens up
    runActions(client, player);
}

bool handleText(std::string const& message) { return onText(message); }
void handleTitle(std::string const& title) {
    if (int e = endFromTitle(title)) onGameEnd(e);
}
} // namespace bedrock_forge::client::serverauto

LL_TYPE_INSTANCE_HOOK(BfTextPacketHook, ll::memory::HookPriority::Normal, ClientNetworkHandler, &ClientNetworkHandler::$handle, void,
                      NetworkIdentifier const& source, TextPacket const& packet) {
    bool show = true;
    try { show = bedrock_forge::client::serverauto::handleText(bedrock_forge::client::serverauto::messageOf(packet)); } catch (...) {}
    if (show) origin(source, packet);
}
LL_TYPE_INSTANCE_HOOK(BfTitlePacketHook, ll::memory::HookPriority::Normal, ClientNetworkHandler, &ClientNetworkHandler::$handle, void,
                      NetworkIdentifier const& source, SetTitlePacket const& packet) {
    try { bedrock_forge::client::serverauto::handleTitle(packet.mTitleText.get()); } catch (...) {}
    origin(source, packet);
}

namespace bedrock_forge::client::serverauto {
void install() {
    BfTextPacketHook::hook();
    BfTitlePacketHook::hook();
}
void uninstall() {
    BfTextPacketHook::unhook();
    BfTitlePacketHook::unhook();
}
} // namespace bedrock_forge::client::serverauto
