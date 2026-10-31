#pragma once
// Bedrock Forge SDK - InputAPI (v0)
// Binds a key to a callback. LeviLamina's client KeyRegistry handlers never fired in game on 1.26.51 (J / K /
// LyricsFlow's keys were dead), so keys are polled instead, the same way the Right Shift menu key works: once per
// UI frame, edge-triggered, only while the game window has focus and the plain HUD screen is on top (typing in chat
// or a menu never triggers them). Not rebindable in Controls for now.

#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "bf/Events.h"
#include "bf/Hud.h"
#include "core/Win32Input.h"
#include "ll/api/event/render/UIRenderEvent.h"

namespace bf::input {

/// Windows virtual-key codes for the keys our examples use.
namespace vk {
inline constexpr int J  = 0x4A;
inline constexpr int K  = 0x4B;
inline constexpr int F8 = 0x77;
} // namespace vk

namespace detail {
struct Binding {
    std::string           name;
    int                   key  = 0;
    bool                  down = false;
    std::function<void()> fn;
};
inline std::mutex&            mutex() { static std::mutex m; return m; }
inline std::vector<Binding>&  bindings() { static std::vector<Binding> b; return b; }
inline void ensurePolling() {
    static bf::events::Subscriptions* subs = nullptr; // never freed: must outlive static destruction order at exit
    if (subs) return;
    subs = new bf::events::Subscriptions();
    subs->on<ll::event::AfterUIRenderEvent>([](ll::event::AfterUIRenderEvent& ev) {
        bool const live = bedrock_forge::win32::gameFocused() && bf::hud::isHudScreen(ev.screenView());
        std::vector<std::function<void()>> fire;
        {
            std::lock_guard l(mutex());
            for (auto& b : bindings()) {
                bool d = live && bedrock_forge::win32::keyDown(b.key);
                if (d && !b.down) fire.push_back(b.fn);
                b.down = d;
            }
        }
        for (auto& f : fire) f(); // outside the lock: callbacks may take their own locks
    });
}
} // namespace detail

/// Bind `name` (key `keyCode`, Windows VK) and call `onDown` when it is pressed in the world (HUD screen, focused).
inline void bindKey(std::string_view name, int keyCode, std::function<void()> onDown) {
    std::lock_guard l(detail::mutex());
    detail::bindings().push_back({std::string(name), keyCode, false, std::move(onDown)});
    detail::ensurePolling();
}

} // namespace bf::input
