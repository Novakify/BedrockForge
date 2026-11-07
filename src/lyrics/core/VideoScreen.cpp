// [LyricsForge] Video screen placement + quads (see VideoScreen.h). Original code for Bedrock Forge.
#include "lyrics/core/VideoScreen.h"

#include <algorithm>
#include <cmath>

#include "lyrics/core/Glyphs.h"

namespace bf::lyrics {
namespace {
constexpr double kSkyDist   = 90.0;  // blocks (inside the sky dome, outside the world you play in)
constexpr double kSkyWidth  = 64.0;  // at size 1: ~39 degrees wide
constexpr float  kSkyElev   = 20.0f; // degrees above the horizon
constexpr double kPinWidth  = 7.0;   // blocks at size 1
constexpr double kSideDist  = 4.5;
constexpr double kSideWidth = 2.4;
constexpr float  kSideYaw   = 34.0f; // right of the crosshair
constexpr float  kSidePitch = -7.0f; // a little above it (Minecraft pitch: negative = up)
constexpr uint32_t kRim     = 0xFFB15CFFu; // neon purple (the client UI accent)
constexpr uint32_t kBezel   = 0xFF07060Bu;

float wrapDeg(float d) {
    d = std::fmod(d + 180.0f, 360.0f);
    if (d < 0) d += 360.0f;
    return d - 180.0f;
}
} // namespace

void VideoScreen::placeHere(Config& cfg, Vec3 eye, float yaw, float pitch) {
    (void)pitch;
    cfg.screenYaw = wrapDeg(yaw);
    Vec3 const p  = eye + dir(yaw, 0.0) * 8.0 + Vec3{0, 1.2, 0};
    cfg.screenPinX = float(p.x);
    cfg.screenPinY = float(p.y);
    cfg.screenPinZ = float(p.z);
    cfg.screenPinYaw = wrapDeg(yaw);
}

void VideoScreen::build(VideoScreenIn const& in, Config const& cfg, std::vector<BfGlyphV1>& out) {
    float const dt = std::clamp(in.dt, 0.0f, 0.1f);
    mShow          = cfg.screenOn ? std::min(1.0f, mShow + dt / 0.45f) : 0.0f;
    if (!cfg.screenOn || mShow <= 0.0f) { mSideInit = false; mAlpha = 0; return; }

    float const size = std::clamp(cfg.screenSize, 0.3f, 3.0f);
    Vec3        centre, look; // look = direction the viewer looks at the screen with (eye -> screen for billboards)
    double      width = 0;
    switch (std::clamp(cfg.screenPlace, 0, 2)) {
    case 1: { // pinned: a fixed plane facing where you stood when you put it there
        centre = {cfg.screenPinX, cfg.screenPinY, cfg.screenPinZ};
        look   = dir(cfg.screenPinYaw, 0.0);
        width  = kPinWidth * size;
        break;
    }
    case 2: { // beside you: eases after your view (fast enough to stay put, slow enough not to jitter)
        float const ty = in.yaw + kSideYaw, tp = std::clamp(in.pitch, -35.0f, 35.0f) * 0.5f + kSidePitch;
        if (!mSideInit) { mSideYaw = ty; mSidePitch = tp; mSideInit = true; }
        float const k = 1.0f - std::exp(-dt / 0.18f);
        mSideYaw += wrapDeg(ty - mSideYaw) * k;
        mSidePitch += (tp - mSidePitch) * k;
        look   = dir(mSideYaw, mSidePitch);
        centre = in.eye + look * kSideDist;
        width  = kSideWidth * size;
        break;
    }
    default: { // sky: one direction, always the same distance from you
        look   = dir(cfg.screenYaw, -kSkyElev);
        centre = in.eye + look * kSkyDist;
        width  = kSkyWidth * size;
        break;
    }
    }
    if (cfg.screenPlace != 2) mSideInit = false;

    float a = std::clamp(cfg.screenOpacity, 0.1f, 1.0f) * mShow * mShow * (3.0f - 2.0f * mShow);
    if (cfg.screenCombatFade) a *= 1.0f - 0.85f * std::clamp(in.fight, 0.0f, 1.0f); // fights: see-through, not gone
    if (cfg.screenPlace == 1) { // pinned: from behind it shows nothing, very far away it fades
        Vec3 const toEye = in.eye - centre;
        double const d   = toEye.length();
        if (toEye.dot(look * -1.0) <= 0.0) a = 0;
        a *= float(std::clamp((160.0 - d) / 40.0, 0.0, 1.0));
    }
    mCentre = centre;
    mWidth  = float(width);
    mAlpha  = a;
    if (a <= 0.01f) return;

    Vec3 const n = look * -1.0, up0{0, 1, 0};
    Vec3 const r = up0.cross(n).normalized(), u = n.cross(r).normalized();
    Vec3 const c = centre - in.origin;
    double const hw = width * 0.5, hh = width * 9.0 / 32.0; // 16:9
    auto quad = [&](double kx, double ky, Vec3 push, uint32_t argb, float alpha, uint32_t flags, float corner, float soft) {
        BfGlyphV1 q{};
        Vec3 const p = c + push;
        q.pos[0] = float(p.x); q.pos[1] = float(p.y); q.pos[2] = float(p.z);
        Vec3 const rr = r * (hw * kx), uu = u * (hh * ky);
        q.right[0] = float(rr.x); q.right[1] = float(rr.y); q.right[2] = float(rr.z);
        q.up[0] = float(uu.x); q.up[1] = float(uu.y); q.up[2] = float(uu.z);
        q.uv[0] = 0; q.uv[1] = 0; q.uv[2] = 1; q.uv[3] = 1;
        q.fill = q.fill2 = toRgba(argb);
        q.alpha    = alpha;
        q.outlineW = corner;
        q.glowW    = soft;
        q.flags    = flags;
        q.atlas    = 0;
        out.push_back(q);
    };
    double const border = std::max(width * 0.012, 0.02); // bezel thickness (world)
    double const bx = (hw + border) / hw, by = (hh + border) / hh;
    // the screen's own light on its surroundings: a big blurred copy of the picture behind it
    if (in.live) quad(1.32, 1.42, {}, 0xFFFFFFFFu, 0.30f * a, BF_GLYPH_IMAGE | BF_GLYPH_LIVE | BF_GLYPH_BLUR, 0.0f, 0.16f);
    // neon rim glow, then the dark bezel, then the picture with slightly rounded corners
    quad(bx * 1.03, by * 1.055, {}, kRim, 0.55f * a, BF_GLYPH_SOLID, 0.10f, 14.0f);
    quad(bx, by, {}, kBezel, 0.95f * a, BF_GLYPH_SOLID, 0.06f, 1.0f);
    quad(1.0, 1.0, {}, 0xFFFFFFFFu, a, BF_GLYPH_IMAGE | BF_GLYPH_LIVE, 0.035f, 0.0f);
}

} // namespace bf::lyrics
