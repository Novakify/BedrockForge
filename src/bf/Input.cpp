// Bedrock Forge SDK - InputAPI (v0): polled key bindings (see Input.h). In a .cpp because LeviLamina's
// UIRenderEvent.h has no include guard (a second include redefines the event classes).
#include "bf/Input.h"

#include <mutex>
#include <string>
#include <vector>

#include "bf/Events.h"
#include "bf/Hud.h"
#include "core/Win32Input.h"
#include "ll/api/event/render/UIRenderEvent.h"

namespace bf::input {
namespace {
struct Binding {
    std::string           name;
    int                   key  = 0;
    bool                  down = false;
    std::function<void()> fn;
};
std::mutex& gMutex() { static std::mutex m; return m; }
std::vector<Binding>& gBindings() { static std::vector<Binding> b; return b; }
void ensurePolling() {
    static bf::events::Subscriptions* subs = nullptr; // never freed: must outlive static destruction at exit
    if (subs) return;
    subs = new bf::events::Subscriptions();
    subs->on<ll::event::AfterUIRenderEvent>([](ll::event::AfterUIRenderEvent& ev) {
        bool const live = bedrock_forge::win32::gameFocused() && bf::hud::isHudScreen(ev.screenView());
        std::vector<std::function<void()>> fire;
        {
            std::lock_guard l(gMutex());
            for (auto& b : gBindings()) {
                bool d = live && bedrock_forge::win32::keyDown(b.key);
                if (d && !b.down) fire.push_back(b.fn);
                b.down = d;
            }
        }
        for (auto& f : fire) f(); // outside the lock: callbacks take their own locks
    });
}
} // namespace

void bindKey(std::string_view name, int keyCode, std::function<void()> onDown) {
    std::lock_guard l(gMutex());
    gBindings().push_back({std::string(name), keyCode, false, std::move(onDown)});
    ensurePolling();
}

} // namespace bf::input
