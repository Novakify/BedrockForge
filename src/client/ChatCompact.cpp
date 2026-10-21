// Bedrock Forge client - Chat Compact: an identical message arriving again within 60 s replaces the previous line,
// which gets a counter: "hello (x3)". The older copy is expired (still in chat history), not deleted.
#include <chrono>
#include <string>

#include "ll/api/memory/Hook.h"

#include "mc/client/gui/GuiData.h"
#include "mc/client/gui/GuiMessage.h"

#include "client/Module.h"
#include "client/Diag.h"

namespace bedrock_forge::client {

namespace {
std::string gLastKey, gLastShown;
int         gCount = 0;
std::chrono::steady_clock::time_point gLastAt{};
} // namespace

LL_TYPE_INSTANCE_HOOK(BfChatHook, ll::memory::HookPriority::Normal, GuiData, &GuiData::_addMessage, void,
                      GuiData::MessageConfig config, ProfanityFilterContext context) {
    ++diag::chatHook;
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
