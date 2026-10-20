#include "core/ImpactFrames.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <random>

#include "ll/api/event/entity/ActorHurtEvent.h"
#include "ll/api/event/render/UIRenderEvent.h"

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDamageSource.h"

#include "bf/Hud.h"

namespace bedrock_forge::impact {

namespace {
using clock = std::chrono::steady_clock;

std::atomic<long long> gLocalPlayer{0};
std::atomic<int64_t>   gImpactAtMs{0}; // when the last impact started (steady clock ms)
std::atomic<uint32_t>  gImpactSeq{0};  // bumps per impact; render thread plays the sound once per bump
std::atomic<float>     gImpactDamage{0};

// Trigger-rule state (hurt events arrive on the integrated server thread).
std::mutex   gRuleMutex;
float        gChance    = -1.0f; // -1 = not initialised from settings yet
int64_t      gLastHitMs = 0;
std::mt19937 gRand{std::random_device{}()};

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count();
}

bool passesRules(Settings const& s, float damage) {
    if (damage < s.minDamage) return false;
    std::lock_guard lock(gRuleMutex);
    int64_t const now = nowMs();
    if (gChance < 0 || now - gLastHitMs > s.chanceRecoverMs) gChance = s.chance;
    gLastHitMs = now;
    if (s.cooldownMs > 0 && now - gImpactAtMs.load() < s.cooldownMs) return false;
    if (std::uniform_real_distribution<float>(0.0f, 1.0f)(gRand) >= gChance) return false;
    gChance = std::max(0.0f, gChance - s.chanceDecay);
    return true;
}

// Tiny deterministic RNG so each impact keeps the same line pattern for its whole duration.
struct Rng {
    uint32_t s;
    float    next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (s & 0xFFFFFF) / float(0xFFFFFF);
    }
};

void drawSpeedLines(MinecraftUIRenderContext& ctx, float w, float h, int count, uint32_t seed, Rgb c, float alpha) {
    Rng   rng{seed * 2654435761u + 1u};
    float cx = w * 0.5f, cy = h * 0.5f;
    for (int i = 0; i < count; ++i) {
        float thick = 0.6f + rng.next() * 1.6f;
        float reach = 0.35f + rng.next() * 0.45f; // how far toward the centre (fraction of half-size)
        float a     = alpha * (0.5f + rng.next() * 0.5f);
        bf::hud::Rgba col{c.r, c.g, c.b, a};
        switch (i % 4) {
        case 0: bf::hud::fillBox(ctx, 0, rng.next() * h, cx * reach, thick, col); break;          // left
        case 1: { float len = cx * reach; bf::hud::fillBox(ctx, w - len, rng.next() * h, len, thick, col); break; } // right
        case 2: bf::hud::fillBox(ctx, rng.next() * w, 0, thick, cy * reach, col); break;          // top
        default: { float len = cy * reach; bf::hud::fillBox(ctx, rng.next() * w, h - len, thick, len, col); break; } // bottom
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
    // Integrated-server thread in single-player. Only atomics / the rule mutex are touched here.
    subs.on<ll::event::ActorHurtEvent>([&s](ll::event::ActorHurtEvent& ev) {
        if (!s.enabled || gLocalPlayer == 0) return;
        auto const& src = ev.source();
        if (!src.isEntitySource()) return;
        if (src.getDamagingEntityUniqueID().rawID != gLocalPlayer) return;
        if (!passesRules(s, ev.damage())) return;
        gImpactDamage = ev.damage();
        gImpactAtMs   = nowMs();
        ++gImpactSeq;
    });

    // Client render thread: draw the frames and play the sound.
    subs.on<ll::event::AfterUIRenderEvent>([&s, &mod](ll::event::AfterUIRenderEvent& ev) {
        static uint32_t soundedSeq = 0;
        if (!s.enabled) return;
        auto& view = ev.screenView();
        if (!bf::hud::isHudScreen(view)) return;

        int64_t const at = gImpactAtMs.load();
        if (at == 0) return;
        int64_t const t      = nowMs() - at;
        int const     length = std::max(1, s.lengthMs);
        if (t < 0 || t > length + s.fadeMs) return;

        auto&          ctx = ev.uiRenderContext();
        uint32_t const seq = gImpactSeq.load();
        if (seq != soundedSeq) {
            soundedSeq = seq;
            if (s.pitch > 0.0f) ctx.mClient.playUI(s.sound, s.volume, s.pitch);
            mod.getLogger().debug("[BF] impact #{} dmg {:.1f}", seq, gImpactDamage.load());
        }

        float const w = view.mSize->x, h = view.mSize->y;
        float const k = std::clamp(s.intensity, 0.0f, 1.0f);
        int const   lines =
            s.speedLines ? static_cast<int>(s.lines * std::clamp(gImpactDamage.load() / 6.0f, 0.6f, 1.6f)) : 0;

        if (t < length) {
            int const  frames = std::max(1, s.frameCount);
            int const  idx    = static_cast<int>(t * frames / length);
            bool const swap   = s.alternate && (idx % 2 == 1);
            Rgb const  bg     = swap ? s.foreground : s.background;
            Rgb const  fg     = swap ? s.background : s.foreground;
            bf::hud::fillBox(ctx, 0, 0, w, h, {bg.r, bg.g, bg.b, k});
            if (s.useFrameTextures && s.frameTextures > 0) {
                // Burst animation runs across the whole sequence; square texture covers the screen.
                int const   tex  = std::min(s.frameTextures - 1, static_cast<int>(t * s.frameTextures / length));
                float const side = std::max(w, h) * 1.15f;
                bf::hud::drawTexture(ctx, s.framePath + std::to_string(tex), (w - side) * 0.5f, (h - side) * 0.5f, side,
                                     side, {fg.r, fg.g, fg.b, k});
            }
            drawSpeedLines(ctx, w, h, lines, seq + idx * 7u, fg, 0.95f * k);
        } else if (s.fadeMs > 0) {
            float fade = 1.0f - float(t - length) / float(s.fadeMs);
            drawVignette(ctx, w, h, fade * k);
            drawSpeedLines(ctx, w, h, lines / 2, seq, {1, 1, 1}, 0.6f * fade * k);
        }
    });
}

} // namespace bedrock_forge::impact
