// Bedrock Forge client - Chat Compact: an identical message arriving again within 60 s replaces the previous line,
// which gets a counter: "hello (x3)". The older copy is expired (still in chat history), not deleted.
#include <algorithm>
#include <chrono>
#include <mutex>
#include <string>

#include "ll/api/memory/Hook.h"

#include "mc/client/gui/GuiData.h"
#include "mc/client/gui/GuiMessage.h"

#include "client/Module.h"
#include "client/Diag.h"

namespace bedrock_forge::client {

// [BF] Nick Hider (Purxiy "hide your name in screenshots and streams"): your name in chat, swapped for the alias
std::mutex  gNickMutex;
std::string gNickReal;   // your name (set every HUD frame, Modules.cpp)
std::string nickAlias() {
    auto* m = Registry::get().find("nick_hider");
    if (!m || !m->enabled) return {};
    static char const* const kAlias[] = {"You", "Player", "Hidden", "Novak"};
    return kAlias[std::clamp(int(m->num("alias", 0)), 0, 3)];
}
std::string hideNick(std::string s) {
    std::string real;
    { std::lock_guard l(gNickMutex); real = gNickReal; }
    std::string const alias = nickAlias();
    if (alias.empty() || real.size() < 3) return s;
    for (size_t i = 0; (i = s.find(real, i)) != std::string::npos; i += alias.size()) s.replace(i, real.size(), alias);
    return s;
}

namespace {
std::string gLastKey, gLastShown;
int         gCount = 0;
std::chrono::steady_clock::time_point gLastAt{};
} // namespace

LL_TYPE_INSTANCE_HOOK(BfChatHook, ll::memory::HookPriority::Normal, GuiData, &GuiData::_addMessage, void,
                      GuiData::MessageConfig config, ProfanityFilterContext context) {
    ++diag::chatHook;
    if (Registry::get().on("nick_hider")) { *config.username = hideNick(*config.username); *config.message = hideNick(*config.message); }
    if (!Registry::get().on("chat_compact")) return origin(std::move(config), context);
    std::string key = *config.username + "\x1f" + *config.message;
    auto        now = std::chrono::steady_clock::now();
    if (key == gLastKey && now - gLastAt < std::chrono::seconds(60)) {
        ++gCount;
        for (auto it = mGuiMessages->rbegin(); it != mGuiMessages->rend(); ++it)
            if (*it->mMessage == gLastShown) { it->mDuration = 0.0f; it->mForceVisible = false; break; }
        *config.message = *config.message + " §7(x" + std::to_string(gCount) + ")";
    } else {
        gCount = 1;
    }
    gLastKey   = key;
    gLastAt    = now;
    gLastShown = *config.message;
    origin(std::move(config), context);
}

void installChatHooks() { BfChatHook::hook(); }
void uninstallChatHooks() { BfChatHook::unhook(); }

} // namespace bedrock_forge::client
