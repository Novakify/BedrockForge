#include "core/ImpactFrames.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>

#include "ll/api/event/entity/ActorHurtEvent.h"
#include "ll/api/event/render/UIRenderEvent.h"

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDamageSource.h"

#include "bf/Hud.h"

namespace bedrock_forge::impact {

namespace {
using clock = std::chrono::steady_clock;

std::atomic<long long> gLocalPlayer{0};
std::atomic<int64_t>   gImpactAtMs{0};  // when the last impact happened (steady clock ms)
std::atomic<uint32_t>  gImpactSeq{0};   // bumps per impact; render thread plays sound once per bump
std::atomic<float>     gImpactDamage{0};

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count();
}

// Tiny deterministic RNG so each impact gets its own line pattern without allocations.
struct Rng {
    uint32_t s;
    float    next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (s & 0xFFFFFF) / float(0xFFFFFF);
    }
};

void drawSpeedLines(MinecraftUIRenderContext& ctx, float w, float h, int count, uint32_t seed, float alpha) {
    Rng   rng{seed * 2654435761u + 1u};
    float cx = w * 0.5f, cy = h * 0.5f;
    for (int i = 0; i < count; ++i) {
        float thick = 0.6f + rng.next() * 1.6f;
        float reach = 0.35f + rng.next() * 0.45f; // how far toward the centre (fraction of half-size)
        float a     = alpha * (0.5f + rng.next() * 0.5f);
        switch (i % 4) {
        case 0: { // from the left edge
            float y = rng.next() * h;
            bf::hud::fillBox(ctx, 0, y, cx * reach, thick, {1, 1, 1, a});
            break;
        }
        case 1: { // from the right edge
            float y = rng.next() * h, len = cx * reach;
            bf::hud::fillBox(ctx, w - len, y, len, thick, {1, 1, 1, a});
            break;
        }
        case 2: { // from the top
            float x = rng.next() * w;
            bf::hud::fillBox(ctx, x, 0, thick, cy * reach, {1, 1, 1, a});
            break;
        }
        default: { // from the bottom
            float x = rng.next() * w, len = cy * reach;
            bf::hud::fillBox(ctx, x, h - len, thick, len, {1, 1, 1, a});
            break;
        }
        }
    }
}

void drawVignette(MinecraftUIRenderContext& ctx, float w, float h, float alpha) {
    // Four stacked bands per edge approximate a soft vignette with plain rectangles.
    for (int i = 0; i < 4; ++i) {
        float t = (i + 1) * 0.04f, a = alpha * (0.35f - i * 0.07f);
        bf::hud::fillBox(ctx, 0, 0, w, h * t, {0, 0, 0, a});
        bf::hud::fillBox(ctx, 0, h * (1 - t), w, h * t, {0, 0, 0, a});
        bf::hud::fillBox(ctx, 0, 0, w * t, h, {0, 0, 0, a});
        bf::hud::fillBox(ctx, w * (1 - t), 0, w * t, h, {0, 0, 0, a});
    }
}
} // namespace

void setLocalPlayerId(long long rawId) { gLocalPlayer = rawId; }

void install(bf::events::Subscriptions& subs, ll::mod::NativeMod& mod, Settings const& s) {
    // Fires on the (integrated) server thread in single-player. We only touch atomics here.
    subs.on<ll::event::ActorHurtEvent>([&s](ll::event::ActorHurtEvent& ev) {
        if (!s.enabled || gLocalPlayer == 0) return;
        auto const& src = ev.source();
        if (!src.isEntitySource()) return;
        if (src.getDamagingEntityUniqueID().rawID != gLocalPlayer) return;
        if (ev.damage() < s.minDamage) return;
        gImpactDamage = ev.damage();
        gImpactAtMs   = nowMs();
        ++gImpactSeq;
    });

    // Client render thread: draw the frame and play the sound.
    subs.on<ll::event::AfterUIRenderEvent>([&s, &mod](ll::event::AfterUIRenderEvent& ev) {
        static uint32_t soundedSeq = 0;
        if (!s.enabled) return;
        auto& view = ev.screenView();
        if (!bf::hud::isHudScreen(view)) return;

        int64_t const at = gImpactAtMs.load();
        if (at == 0) return;
        int64_t const t     = nowMs() - at;
        int const     total = s.flashMs + s.inkMs + s.fadeMs;
        if (t < 0 || t > total) return;

        auto&          ctx = ev.uiRenderContext();
        uint32_t const seq = gImpactSeq.load();
        if (seq != soundedSeq) {
            soundedSeq = seq;
            ctx.mClient.playUI(s.sound, s.volume, s.pitch);
            mod.getLogger().debug("[BF] impact #{} dmg {:.1f}", seq, gImpactDamage.load());
        }

        float const w = view.mSize->x, h = view.mSize->y;
        float const k = std::clamp(s.intensity, 0.0f, 1.0f);
        // bigger hits = denser lines
        int const lines = static_cast<int>(s.lines * std::clamp(gImpactDamage.load() / 6.0f, 0.6f, 1.6f));

        if (t <= s.flashMs) {
            bf::hud::fillBox(ctx, 0, 0, w, h, {1, 1, 1, 0.85f * k});
        } else if (t <= s.flashMs + s.inkMs) {
            bf::hud::fillBox(ctx, 0, 0, w, h, {0, 0, 0, 0.55f * k});
            drawSpeedLines(ctx, w, h, lines, seq, 0.95f * k);
        } else {
            float fade = 1.0f - float(t - s.flashMs - s.inkMs) / float(std::max(1, s.fadeMs));
            drawVignette(ctx, w, h, fade * k);
            drawSpeedLines(ctx, w, h, lines / 2, seq, 0.6f * fade * k);
        }
    });
}

} // namespace bedrock_forge::impact
