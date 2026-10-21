#include "core/ClientModules.h"

#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <format>
#include <string>
#include <vector>

#include "ll/api/event/render/UIRenderEvent.h"

#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/Actor.h"

#include "bf/Hud.h"
#include "bf/Input.h"
#include "core/Win32Input.h"

namespace bedrock_forge::modules {

namespace {
using Clock = std::chrono::steady_clock;

struct Fps {
    Clock::time_point start = Clock::now();
    int               frames = 0, value = 0;
    void              frame() {
        ++frames;
        if (auto now = Clock::now(); now - start >= std::chrono::seconds(1)) { value = frames; frames = 0; start = now; }
    }
};

// Clicks are sampled once per rendered frame (edge detection). At 60+ fps that catches normal clicking;
// a press shorter than one frame can be missed - a low-level mouse hook would fix that later.
struct Cps {
    bool                          wasDown = false;
    std::deque<Clock::time_point> presses;
    int update(bool down) {
        auto now = Clock::now();
        if (down && !wasDown) presses.push_back(now);
        wasDown = down;
        while (!presses.empty() && now - presses.front() > std::chrono::seconds(1)) presses.pop_front();
        return static_cast<int>(presses.size());
    }
};

Fps gFps;
Cps gLeft, gRight;

char const* compass(float yaw) {
    // Bedrock yaw: 0 = south (+Z), 90 = west, 180 = north, -90 = east
    static constexpr std::array<char const*, 8> names{"S", "SW", "W", "NW", "N", "NE", "E", "SE"};
    float y = std::fmod(yaw + 360.0f + 22.5f, 360.0f);
    return names[static_cast<size_t>(y / 45.0f) % 8];
}

void key(MinecraftUIRenderContext& ctx, float x, float y, float w, float h, char const* label, bool down) {
    bf::hud::fillBox(ctx, x, y, w, h, down ? bf::hud::Rgba{1.f, 1.f, 1.f, 0.75f} : bf::hud::Rgba{0.f, 0.f, 0.f, 0.45f});
    float const tx = x + w / 2.0f - 2.5f * static_cast<float>(std::char_traits<char>::length(label));
    bf::hud::drawLabel(ctx, tx, y + h / 2.0f - 4.5f, label, down ? bf::hud::Rgba{0.f, 0.f, 0.f, 1.f} : bf::hud::Rgba{1.f, 1.f, 1.f, 1.f});
}
} // namespace

void install(bf::events::Subscriptions& subs, ll::mod::NativeMod& mod, Settings& s) {
    bf::input::bindKey("bedrock_forge.toggle_modules", 0x48 /* H */, [&s, &mod] {
        s.enabled = !s.enabled;
        mod.getLogger().info("[BF] HUD modules {}", s.enabled ? "on" : "off");
    });

    subs.on<ll::event::AfterUIRenderEvent>([&s](ll::event::AfterUIRenderEvent& ev) {
        auto& view = ev.screenView();
        if (!bf::hud::isHudScreen(view)) return;
        gFps.frame();
        bool const focused = win32::gameFocused();
        int const  lcps    = gLeft.update(focused && win32::keyDown(win32::vk::LButton));
        int const  rcps    = gRight.update(focused && win32::keyDown(win32::vk::RButton));
        if (!s.enabled) return;

        auto& ctx = ev.uiRenderContext();
        std::vector<std::string> lines;
        if (s.fps) lines.push_back(std::format("FPS  {}", gFps.value));
        if (s.cps) lines.push_back(std::format("CPS  {} | {}", lcps, rcps));
        if (s.coords || s.direction) {
            if (auto* player = ctx.mClient.getLocalPlayer()) {
                auto const& p = player->getPosition();
                if (s.coords) lines.push_back(std::format("XYZ  {:.0f} {:.0f} {:.0f}", std::floor(p.x), std::floor(p.y - 1.62f), std::floor(p.z)));
                if (s.direction) {
                    float const yaw = player->getRotation().y;
                    lines.push_back(std::format("Dir  {} ({:.0f})", compass(yaw), yaw));
                }
            }
        }
        if (s.clock) {
            int h = 0, m = 0;
            win32::localTime(h, m);
            lines.push_back(std::format("Time {:02}:{:02}", h, m));
        }
        if (!lines.empty()) {
            float const x = static_cast<float>(s.x), y = static_cast<float>(s.y);
            bf::hud::fillBox(ctx, x - 2, y - 2, 92, 11.0f * static_cast<float>(lines.size()) + 3, {0.f, 0.f, 0.f, s.opacity});
            for (size_t i = 0; i < lines.size(); ++i)
                bf::hud::drawLabel(ctx, x, y + 11.0f * static_cast<float>(i), lines[i], {1.f, 1.f, 1.f, 1.f});
        }

        if (s.keystrokes) {
            float const W = view.mSize->x, H = view.mSize->y, k = 18.0f, g = 2.0f;
            float const bx = s.keysX >= 0 ? static_cast<float>(s.keysX) : W + static_cast<float>(s.keysX) - (3 * k + 2 * g);
            float const by = s.keysY >= 0 ? static_cast<float>(s.keysY) : H + static_cast<float>(s.keysY);
            auto down = [&](int vk) { return focused && win32::keyDown(vk); };
            key(ctx, bx + k + g, by, k, k, "W", down(win32::vk::W));
            key(ctx, bx, by + k + g, k, k, "A", down(win32::vk::A));
            key(ctx, bx + k + g, by + k + g, k, k, "S", down(win32::vk::S));
            key(ctx, bx + 2 * (k + g), by + k + g, k, k, "D", down(win32::vk::D));
            float const half = (3 * k + 2 * g - g) / 2.0f;
            key(ctx, bx, by + 2 * (k + g), half, k, "LMB", down(win32::vk::LButton));
            key(ctx, bx + half + g, by + 2 * (k + g), half, k, "RMB", down(win32::vk::RButton));
            key(ctx, bx, by + 3 * (k + g), 3 * k + 2 * g, k * 0.6f, "___", down(win32::vk::Space));
        }
    });
}

} // namespace bedrock_forge::modules
