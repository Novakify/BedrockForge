// LyricsForge - [BF] material-aware impacts. Original code for Bedrock Forge; see Bursts.h.
#include "lyrics/core/Bursts.h"

#include <algorithm>
#include <cmath>

#include "lyrics/core/Glyphs.h"

namespace bf::lyrics {

namespace {
bool has(std::string_view s, std::string_view k) { return s.find(k) != std::string_view::npos; }
float h01(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return float(s & 0xFFFFFF) / float(0xFFFFFF); }
uint32_t pick(uint32_t& s, std::initializer_list<uint32_t> c) { return *(c.begin() + std::min(c.size() - 1, size_t(h01(s) * float(c.size())))); }
} // namespace

Surface surfaceOf(std::string_view n) {
    if (has(n, "water") || has(n, "kelp") || has(n, "seagrass") || has(n, "bubble")) return Surface::Water;
    if (has(n, "lava") || has(n, "magma") || has(n, "fire")) return Surface::Lava;
    if (has(n, "snow") || has(n, "powder_snow") || has(n, "ice")) return Surface::Snow;
    if (has(n, "glass") || has(n, "amethyst")) return Surface::Glass;
    if (has(n, "leaves") || has(n, "azalea") || has(n, "vine") || has(n, "moss")) return Surface::Leaves;
    if (has(n, "wool") || has(n, "carpet")) return Surface::Wool;
    if (has(n, "iron") || has(n, "gold_block") || has(n, "copper") || has(n, "netherite") || has(n, "anvil") || has(n, "chain"))
        return Surface::Metal;
    if (has(n, "sand") && !has(n, "sandstone")) return Surface::Sand;
    if (has(n, "grass") || has(n, "mycelium") || has(n, "podzol")) return Surface::Grass;
    if (has(n, "dirt") || has(n, "mud") || has(n, "farmland") || has(n, "gravel") || has(n, "clay") || has(n, "soul_soil"))
        return Surface::Dirt;
    if (has(n, "planks") || has(n, "log") || has(n, "wood") || has(n, "stem") || (has(n, "_slab") && has(n, "oak")) ||
        has(n, "bookshelf") || has(n, "barrel") || has(n, "chest") || has(n, "crafting"))
        return Surface::Wood;
    if (has(n, "stone") || has(n, "deepslate") || has(n, "cobble") || has(n, "brick") || has(n, "andesite") || has(n, "diorite") ||
        has(n, "granite") || has(n, "tuff") || has(n, "calcite") || has(n, "basalt") || has(n, "blackstone") || has(n, "netherrack") ||
        has(n, "terracotta") || has(n, "concrete") || has(n, "ore") || has(n, "obsidian") || has(n, "prismarine") || has(n, "quartz"))
        return Surface::Stone;
    return Surface::Generic;
}

void Bursts::spawn(Landing const& l, uint32_t seed) {
    uint32_t s = seed * 2654435761u + 0x9E3779B9u;
    float const k = std::clamp(l.strength, 0.2f, 2.0f), sz = std::clamp(l.size, 0.03f, 0.6f);
    int   n = 6;
    float up = 1.6f, out = 1.0f, life = 0.6f, size = 0.06f, grav = 9.0f, drag = 2.0f;
    bool  glow = false;
    auto  col = [&]() -> uint32_t {
        switch (l.surface) {
        case Surface::Stone: return pick(s, {0xFF8A8A8A, 0xFF6E6E6E, 0xFFA4A4A4});
        case Surface::Dirt: return pick(s, {0xFF7A5434, 0xFF5E3F26, 0xFF8E6A48});
        case Surface::Sand: return pick(s, {0xFFE3D3A0, 0xFFD6C38A, 0xFFF0E4BC});
        case Surface::Grass: return pick(s, {0xFF5FA83A, 0xFF4A8C2C, 0xFF7A5434});
        case Surface::Snow: return pick(s, {0xFFFFFFFF, 0xFFEAF4FF, 0xFFD8EAFF});
        case Surface::Water: return pick(s, {0xFF7FC8FF, 0xFFB8E4FF, 0xFFFFFFFF});
        case Surface::Lava: return pick(s, {0xFFFF8A1E, 0xFFFFC23A, 0xFFFF4A10});
        case Surface::Wood: return pick(s, {0xFFA07A48, 0xFF8A6438, 0xFFC09A62});
        case Surface::Metal: return pick(s, {0xFFFFE07A, 0xFFFFFFFF, 0xFFFFB84A});
        case Surface::Glass: return pick(s, {0xFFDDF6FF, 0xFFB8ECFF, 0xFFFFFFFF});
        case Surface::Leaves: return pick(s, {0xFF4C9A30, 0xFF3A7E24, 0xFF6EB846});
        case Surface::Wool: return pick(s, {0xFFF2F2F2, 0xFFE0E0E0});
        default: return pick(s, {0xFFBFBFBF, 0xFF9A9A9A});
        }
    };
    switch (l.surface) {
    case Surface::Stone: n = 7; up = 2.2f; out = 1.4f; life = 0.55f; size = 0.05f; break;              // chips that bounce off
    case Surface::Dirt: case Surface::Sand: n = 9; up = 1.1f; out = 1.3f; life = 0.8f; size = 0.07f; grav = 4.0f; drag = 3.5f; break; // puff
    case Surface::Grass: n = 8; up = 1.8f; out = 1.1f; life = 0.75f; size = 0.05f; grav = 6.0f; break;
    case Surface::Snow: n = 10; up = 1.0f; out = 1.4f; life = 1.0f; size = 0.07f; grav = 2.0f; drag = 4.0f; break; // powder hangs
    case Surface::Water: n = 10; up = 3.0f; out = 0.9f; life = 0.7f; size = 0.04f; grav = 11.0f; drag = 0.8f; break; // splash up
    case Surface::Lava: n = 9; up = 3.4f; out = 1.2f; life = 0.9f; size = 0.035f; grav = 6.0f; drag = 0.6f; glow = true; break; // sparks
    case Surface::Metal: n = 8; up = 2.6f; out = 2.2f; life = 0.45f; size = 0.025f; grav = 8.0f; drag = 0.5f; glow = true; break;
    case Surface::Glass: n = 6; up = 1.8f; out = 1.6f; life = 0.6f; size = 0.035f; glow = true; break;
    case Surface::Leaves: n = 6; up = 1.4f; out = 0.9f; life = 1.1f; size = 0.06f; grav = 2.5f; drag = 3.0f; break; // flutter
    case Surface::Wood: n = 6; up = 1.9f; out = 1.2f; life = 0.6f; size = 0.05f; break;
    case Surface::Wool: n = 4; up = 0.7f; out = 0.6f; life = 0.6f; size = 0.06f; grav = 3.0f; drag = 4.0f; break; // soft thud
    default: break;
    }
    n = std::max(2, int(float(n) * std::clamp(0.5f + 0.5f * k, 0.5f, 1.4f)));
    float const scale = sz / 0.25f; // a big letter throws bigger, further bits
    for (int i = 0; i < n; ++i) {
        Bit b;
        double const a = h01(s) * 6.2831853;
        float const  o = out * (0.4f + 0.6f * h01(s)) * k * std::sqrt(scale);
        b.pos   = l.pos + Vec3{std::cos(a) * 0.3 * sz, 0.02, std::sin(a) * 0.3 * sz};
        b.vel   = Vec3{std::cos(a) * o, up * (0.5f + 0.5f * h01(s)) * k * std::sqrt(scale), std::sin(a) * o};
        b.life  = life * (0.7f + 0.6f * h01(s));
        b.size  = size * (0.6f + 0.8f * h01(s)) * std::clamp(scale, 0.5f, 2.0f);
        b.gravity = grav;
        b.drag  = drag;
        b.argb  = col();
        b.glow  = glow;
        mBits.push_back(b);
    }
    if (mBits.size() > kMax) mBits.erase(mBits.begin(), mBits.begin() + std::ptrdiff_t(mBits.size() - kMax));
}

void Bursts::step(float dt) {
    dt = std::clamp(dt, 0.0f, 0.1f);
    for (auto& b : mBits) {
        b.age += dt;
        b.vel.y -= double(b.gravity * dt);
        b.vel = b.vel * double(std::exp(-b.drag * dt));
        b.pos += b.vel * double(dt);
    }
    std::erase_if(mBits, [](Bit const& b) { return b.age >= b.life; });
}

void Bursts::emit(Vec3 const& origin, float camYaw, float camPitch, std::vector<BfGlyphV1>& out) const {
    if (mBits.empty()) return;
    double const y = rad(camYaw), p = rad(camPitch);
    Vec3 const f{-std::sin(y) * std::cos(p), -std::sin(p), std::cos(y) * std::cos(p)};
    Vec3 const r = Vec3{-f.z, 0, f.x}.normalized();
    Vec3 const u = r.cross(f).normalized();
    for (auto const& b : mBits) {
        float const k = b.age / std::max(0.01f, b.life);
        float const a = (1.0f - k * k) * (b.glow ? 1.0f : 0.9f);
        float const s = b.size * (b.glow ? 1.0f - 0.5f * k : 1.0f);
        BfGlyphV1 q{};
        Vec3 const c = b.pos - origin;
        q.pos[0] = float(c.x); q.pos[1] = float(c.y); q.pos[2] = float(c.z);
        q.right[0] = float(r.x * s); q.right[1] = float(r.y * s); q.right[2] = float(r.z * s);
        q.up[0] = float(u.x * s); q.up[1] = float(u.y * s); q.up[2] = float(u.z * s);
        q.fill = q.fill2 = toRgba(b.argb);
        q.alpha    = a;
        q.outlineW = b.glow ? 1.0f : 0.35f; // sparks round, chips squarish
        q.glowW    = b.glow ? 2.0f : 0.8f;
        q.uv[2] = q.uv[3] = 1.0f;
        q.flags = BF_GLYPH_SOLID;
        out.push_back(q);
    }
}

} // namespace bf::lyrics
