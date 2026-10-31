// LyricsFlow port - WorldLyrics (scheduling, placement, animation). Logic ported from LyricsFlow v2.0 (374m, MIT).
#include "lyrics/core/Scene.h"

#include "lyrics/core/Text.h"

#include <algorithm>
#include <cmath>

namespace bf::lyrics {

using anim::rnd;

namespace {
constexpr float  kEnterMs = 300.0f;
constexpr float  kExitMs  = 650.0f;
constexpr double kSkyDist = 48.0;
constexpr char const* kNotes[] = {"\xE2\x99\xAA \xE2\x99\xAB", "\xE2\x99\xAB \xE2\x99\xAA \xE2\x99\xAC", "\xE2\x99\xAA \xE2\x99\xAB \xE2\x99\xAA",
                                  "\xE2\x99\xAC \xE2\x99\xAA", "\xE2\x99\xAB \xE2\x99\xAA \xE2\x99\xAB \xE2\x99\xAA"};
constexpr int64_t kNoteEvery = 1700;

bool skyWords(Config const& c) { return c.lyricsStyle == LyricsStyle::Sky && c.skyWords; }
bool chunked(Config const& c) { return c.lyricsStyle == LyricsStyle::Floating || skyWords(c); }
bool inWorld(Config const& c) { return c.lyricsStyle != LyricsStyle::Screen; }

std::vector<std::pair<int, int>> chunks(Line const& line, Config const& c) {
    int n = int(line.words.size());
    if (!chunked(c)) return {{0, n}};
    std::vector<std::pair<int, int>> out; // Java: one word per chunk (len is always 1 in v2.0)
    for (int i = 0; i < n; ++i) out.push_back({i, i + 1});
    return out;
}

bool quiet(Lyrics const* lyrics, Line const* cur, int64_t t) {
    if (cur && cur->index >= 0 && t <= cur->end + 1200) return false;
    if (lyrics)
        for (auto const& l : lyrics->lines)
            if (l.start > t) return l.start - t > 2600;
    return true;
}

std::string const& fontOf(Placed const& p, Config const& c) {
    static std::string const mc = "minecraft";
    return p.line.index < 0 ? mc : c.font;
}

bool inClouds(float pitch, double dist, double cloud, CameraState const& cam) {
    if (std::isnan(cloud)) return false;
    double y = cam.pos.y + dir(0.0, pitch).y * dist;
    return std::fabs(y - cloud) < 6.0;
}

double skyDist(Config const& c, CameraState const& cam) {
    double far = double(cam.depthFar) * 0.85;
    return std::max(20.0, std::min(far, double(c.skyDistance)));
}

struct AvoidBox { float yaw, pitch, hw, hh; };
std::vector<AvoidBox> avoidBoxes(CameraState const& cam, IWorld* w) {
    std::vector<AvoidBox> out;
    if (!w) return out;
    for (auto const& e : w->entities()) {
        if (e.isLocalPlayer || (!e.isPlayer && !e.isTarget)) continue;
        Vec3   d    = e.center() - cam.pos;
        double dist = d.length();
        if (dist > 40.0 || dist < 0.3) continue;
        out.push_back({float(yawOf(d)), float(-deg(std::asin(d.y / dist))), float(deg(std::atan((e.width() * 0.5 + 0.9) / dist))),
                       float(deg(std::atan((e.height() * 0.5 + 1.0) / dist)))});
    }
    return out;
}

bool rtl(std::string_view s) {
    for (uint32_t c : text::codepoints(s))
        if (text::isRtl(c)) return true;
    return false;
}
} // namespace

// ---------------------------------------------------------------- word animation
std::optional<std::array<float, 4>> wordAnim(Placed const& p, int j, Word const& w, int64_t t, Config const& c, float speed) {
    float ein = anim::progress(double(t), double(w.start - 40), double(float(w.start - 40) + kEnterMs / speed));
    if (ein <= 0.0f) return std::nullopt;
    float dx = 0, dy = 0, sc = 1, alpha;
    switch (c.enterAnim) {
    case EnterAnim::Fade: alpha = anim::easeInOut(ein); break;
    case EnterAnim::Rise: {
        float e = anim::easeOutCubic(ein);
        alpha   = e;
        dy      = (1 - e) * 0.9f;
        break;
    }
    default: {
        float e = anim::easeOutBack(ein);
        alpha   = anim::easeOutCubic(ein);
        dy      = (1 - anim::easeOutCubic(ein)) * 0.45f;
        sc      = 0.82f + 0.18f * e;
    }
    }
    if (p.exitAt >= 0) {
        int64_t start = p.exitAt + 55 * int64_t(j);
        float   eo    = anim::progress(double(t), double(start), double(float(start) + kExitMs / speed));
        if (eo >= 1.0f) return std::nullopt;
        float e = anim::easeInCubic(eo);
        alpha *= 1 - anim::easeInOut(eo);
        switch (c.exitAnim) {
        case ExitAnim::FloatUp:
            dy -= e * 1.6f;
            sc *= 1 + e * 0.08f;
            break;
        case ExitAnim::Drift:
            dx += e * 1.8f * (rnd(p.seed, 90) > 0.5f ? 1.0f : -1.0f);
            dy -= e * 0.35f;
            break;
        default: break;
        }
    }
    if (p.w0 + j == int(p.line.words.size()) - 1 && t >= w.start) { // held last word "pulses" on the beat
        int64_t held = std::max(w.end, p.line.end) - w.start;
        if (held >= 650 && t < w.start + held + 300) {
            float ph   = float((t - w.start) % 860) / 860.0f;
            float beat = float(std::exp(-std::pow(double((ph - 0.08f) / 0.05f), 2.0)) + 0.55 * std::exp(-std::pow(double((ph - 0.27f) / 0.05f), 2.0)));
            float fin  = anim::clamp01(float(t - w.start) / 200.0f);
            sc *= 1 + 0.085f * beat * fin;
        }
    }
    return std::array<float, 4>{alpha, dx, dy, sc};
}

// ---------------------------------------------------------------- scene
void Scene::clear() {
    mVisible.clear();
    mShown.clear();
}

Placed* Scene::find(Line const& l, int w0) {
    for (auto& p : mVisible)
        if (p.sameLine(l) && p.w0 == w0) return &p;
    return nullptr;
}
Placed* Scene::findIndex(int index) {
    for (auto& p : mVisible)
        if (p.line.index == index) return &p;
    return nullptr;
}
Line const& Scene::noteLine(int64_t t) {
    int64_t slot = t / kNoteEvery;
    if (slot != mNoteSlot || mNoteSong != mSongSeq || !mHaveNote) {
        mNoteSlot = slot;
        mNoteSong = mSongSeq;
        int64_t st = slot * kNoteEvery;
        mNoteLine  = Line::notes(-1 - int(slot % 100000), kNotes[slot % 5], st, st + 2900);
        mHaveNote  = true;
    }
    return mNoteLine;
}

void Scene::tick(SceneInput const& in, Config const& c, CameraState const& cam, IWorld* world, TextWidthFn const& width) {
    Lyrics const* lyrics = in.lyrics;
    int64_t       t      = in.t;
    if (in.songSeq != mSongSeq) {
        mSongSeq = in.songSeq;
        mShown.clear();
        mVisible.clear();
        mBaseYaw = cam.yaw;
    }
    mPauseFade     = anim::approach(mPauseFade, in.playing ? 1.0f : 0.0f, in.playing ? 4.0f : 1.1f, in.dt);
    bool noLyrics  = !lyrics || lyrics->lines.empty();
    bool notesOk   = c.musicNotes && (!noLyrics || in.lyricsState == LyricsState::NotFound);
    if (t < 0 || (noLyrics && !notesOk)) {
        mVisible.clear();
        return;
    }
    if (mLastT >= 0 && t < mLastT - 1500) { // seeked backwards
        mVisible.clear();
        mShown.clear();
    }
    mLastT          = t;
    Line const* cur = noLyrics ? nullptr : lyrics->lastStartedBefore(t);
    bool skyPairs   = c.lyricsStyle == LyricsStyle::Sky && !c.skyWords;
    if (notesOk && in.playing && quiet(lyrics, cur, t)) cur = &noteLine(t);

    if (cur && float(t) < float(cur->end) + kExitMs && t - cur->start < 6000 && !cur->words.empty()) {
        Line const line = *cur; // copy: noteLine() storage may be replaced next tick
        int64_t    seed = int64_t(line.index) * 7919 + mSongSeq * 104729;
        for (auto [c0, c1] : chunks(line, c)) {
            int64_t key = int64_t(line.index) * 1000 + c0;
            if (find(line, c0) || mShown.count(key)) continue;
            int64_t st = line.words[size_t(c0)].start;
            if (chunked(c) && t < st - 80) continue;
            if (chunked(c) && t > line.words[size_t(c1 - 1)].end + 1200) continue;
            mShown.insert(key);
            Placed p;
            p.line = line;
            p.w0   = c0;
            p.w1   = c1;
            p.seed = seed + int64_t(c0) * 613;
            place(p, c, cam, world, width);
            if (c0 == 0)
                for (auto& o : mVisible)
                    if ((line.index >= 0 || o.line.index >= 0) && (!skyPairs || line.index % 2 != 1 || o.line.index != line.index - 1) &&
                        o.exitAt < 0 && !o.sameLine(line))
                        o.exitAt = chunked(c) ? std::max(o.lastEnd(), std::min(t, line.start) + 250) : std::max(o.line.start, std::min(t, line.start));
            mVisible.push_back(std::move(p));
        }
    }

    int limit = chunked(c) ? std::max(2, c.maxLines * 3) : std::max(skyPairs ? 2 : 1, c.maxLines);
    int alive = 0;
    for (int i = int(mVisible.size()) - 1; i >= 0; --i) {
        Placed& p = mVisible[size_t(i)];
        if (p.exitAt >= 0) continue;
        ++alive;
        int64_t linger = chunked(c) ? (c.lyricsStyle == LyricsStyle::Floating ? 900 : 1500) : 600;
        if (alive > limit) { p.exitAt = t; continue; }
        int64_t lineEnd = p.line.index >= 0 ? p.line.end : 0;
        if (skyPairs && !noLyrics && p.line.index >= 0 && p.line.index % 2 == 0 && size_t(p.line.index + 1) < lyrics->lines.size())
            lineEnd = std::max(lineEnd, lyrics->lines[size_t(p.line.index + 1)].end);
        int64_t until = std::max(p.lastEnd() + linger, lineEnd);
        if (t > until) p.exitAt = until;
    }

    float speed = std::max(0.3f, c.animSpeed);
    std::erase_if(mVisible, [&](Placed const& p) {
        bool gone = p.exitAt >= 0 && float(t) > float(p.exitAt) + kExitMs / speed + float(60 * int64_t(p.n()));
        return gone || (p.lookedAway && p.viewVis < 0.02f);
    });

    if (mPauseFade >= 0.01f && inWorld(c))
        for (auto& p : mVisible) update(p, c, cam, world, in.dt);
}

void Scene::update(Placed& p, Config const& c, CameraState const& cam, IWorld* w, float dt) {
    if (c.lyricsStyle == LyricsStyle::Sky) {
        if (++p.occlTick % 10 == 1 && w) {
            BlockPos at    = BlockPos::of(cam.pos);
            bool     under = !c.showUnderground && !w->canSeeSky({at.x, at.y + 1, at.z}) && w->skyLight(at) < 4;
            p.occl         = under ? 0.0f : 1.0f;
        }
        p.viewVis = 1.0f;
    } else {
        Vec3   a      = anchor(p, c, cam);
        Vec3   d      = a - cam.pos;
        double dl     = d.length();
        float  yaw    = float(yawOf(d));
        float  pitch  = dl < 1e-4 ? 0.0f : float(-deg(std::asin(d.y / dl)));
        float  relYaw = std::fabs(wrap(yaw - cam.yaw));
        float  relP   = std::fabs(pitch - cam.pitch);
        bool   inView = relYaw < 52.0f && relP < 42.0f && dl < double(24.0f * std::max(1.0f, c.lyricsDistance) + 4.0f);
        if (c.lyricsStyle == LyricsStyle::Wall) inView = dl < 40.0;
        if (!inView && p.viewVis > 0.5f) p.lookedAway = true;
        p.viewVis = anim::approach(p.viewVis, inView && !p.lookedAway ? 1.0f : 0.0f, p.lookedAway ? 9.0f : 12.0f, dt);
        if (++p.occlTick % 6 == 1 && w) {
            RayHit h = w->clip(cam.pos, a, RayShape::Visual, false);
            p.occl   = (h.hit && h.pos.dist(cam.pos) < dl - 0.4) ? 0.0f : 1.0f;
        }
        if (c.lyricsStyle == LyricsStyle::Wall) p.occl = 1.0f;
    }
    p.occlVis = anim::approach(p.occlVis, p.occl, 6.0f, dt);
}

Vec3 Scene::anchor(Placed const& p, Config const& c, CameraState const& cam) const {
    if (c.lyricsStyle == LyricsStyle::Sky && !c.viewLock) return p.a;
    if (c.lyricsStyle == LyricsStyle::Sky) return cam.pos + dir(cam.yaw + p.yaw, cam.pitch + p.pitch) * kSkyDist;
    if (c.viewLock) return cam.pos + dir(cam.yaw + p.yaw, cam.pitch + p.pitch) * p.dist;
    return p.a;
}

Quat Scene::facing(Placed const& p, Config const& c, CameraState const& cam) const {
    if (p.surf) return *p.surf;
    float yaw = c.viewLock ? cam.yaw + p.yaw : p.yaw;
    float pit = c.viewLock ? cam.pitch + p.pitch : p.pitch;
    if (c.lyricsStyle == LyricsStyle::Sky) yaw += p.turn;
    pit = std::clamp(pit, -35.0f, 35.0f);
    return Quat::rotationYXZ(kPi - rad(yaw), -rad(pit), 0.0);
}

float Scene::em(Placed const& p, Config const& c) const {
    if (c.lyricsStyle == LyricsStyle::Sky) return float(150.0 * std::tan(rad(3.1))) * c.lyricsScale;
    if (c.lyricsStyle == LyricsStyle::Floating) return float(0.0765 * std::max(2.5, double(p.sizeDist))) * c.lyricsScale * p.fit;
    return float(0.085 * std::max(2.5, p.dist)) * c.lyricsScale * p.fit;
}

double Scene::clearance(float yaw, float pitch, double dist, CameraState const& cam, IWorld* w) const {
    if (!w) return dist;
    Vec3   to = cam.pos + dir(yaw, pitch) * dist;
    RayHit h  = w->clip(cam.pos, to, RayShape::Visual, false);
    return h.hit ? h.pos.dist(cam.pos) : dist;
}
int Scene::blocked(float yaw, float pitch, double dist, float hw, CameraState const& cam, IWorld* w) const {
    int n = 0;
    if (clearance(yaw, pitch, dist, cam, w) < dist - 0.3) ++n;
    if (clearance(yaw - hw, pitch, dist, cam, w) < dist - 0.3) ++n;
    if (clearance(yaw + hw, pitch, dist, cam, w) < dist - 0.3) ++n;
    return n;
}
float Scene::crowd(Vec3 at) const {
    float pen = 0;
    for (auto const& o : mVisible) {
        double d = o.a.dist(at);
        if (d < 3.5) pen += float(3.5 - d) * 160.0f;
    }
    return pen;
}

bool Scene::overPlayer(Placed const& p, Config const& c, CameraState const& cam, IWorld* w) const {
    if (!w) return false;
    Vec3   e   = anchor(p, c, cam) - cam.pos;
    double len = e.length();
    if (len < 0.001) return false;
    float ly = float(yawOf(e)), lp = float(-deg(std::asin(e.y / len)));
    float k  = float(p.dist / len);
    for (auto const& pl : w->entities()) {
        if (!pl.isPlayer || (pl.isLocalPlayer && !cam.thirdPerson)) continue;
        Vec3   d    = pl.center() - cam.pos;
        double dist = d.length();
        if (dist > 48.0 || dist < 0.3 || dist < len) continue;
        float yaw = float(yawOf(d)), pitch = float(-deg(std::asin(d.y / dist)));
        float hw = float(deg(std::atan(pl.width() * 0.5 / dist))), hh = float(deg(std::atan(pl.height() * 0.5 / dist)));
        if (std::fabs(wrap(ly - yaw)) < hw + p.angW * k && std::fabs(lp - pitch) < hh + p.angH * k) return true;
    }
    return false;
}

// ---------------------------------------------------------------- placement
void Scene::wallPlace(Placed& p, int64_t s, CameraState const& cam, IWorld* w) {
    Vec3                eye = cam.pos;
    std::optional<Vec3> best, bestN;
    std::optional<Quat> bestQ;
    float               bestScore = -1e9f;
    if (w) {
        for (int k = 0; k < 10; ++k) {
            float  yaw   = cam.yaw + (k == 0 ? 0.0f : (rnd(s, 200 + k * 2) * 2 - 1) * 35.0f);
            float  pitch = std::clamp(cam.pitch * 0.5f + (k == 0 ? 0.0f : (rnd(s, 201 + k * 2) - 0.5f) * 24.0f), -30.0f, 25.0f);
            RayHit hit   = w->clip(eye, eye + dir(yaw, pitch) * 16.0, RayShape::Collider, true);
            if (!hit.hit) continue;
            double dist = hit.pos.dist(eye);
            if (!horizontal(hit.face) || dist < 2.5) continue;
            Vec3  n     = normalOf(hit.face);
            Vec3  tg{n.z, 0, -n.x};
            float score = 1000.0f - float(std::fabs(dist - 6.0)) * 20.0f - std::fabs(wrap(yaw - cam.yaw)) * 2.0f - crowd(hit.pos);
            for (int side = -2; side <= 2; ++side) {
                if (side == 0) continue;
                Vec3 q   = hit.pos + tg * (double(side) * 1.1);
                bool ok  = !w->isAir(BlockPos::of(q - n * 0.5)) && w->empty(BlockPos::of(q + n * 0.5));
                if (!ok) score -= std::abs(side) == 1 ? 260.0f : 120.0f;
            }
            if (score > bestScore) {
                bestScore   = score;
                bestN       = n;
                best        = hit.pos + n * 0.04;
                double yawIn = deg(std::atan2(n.x, -n.z));
                bestQ       = Quat::rotationYXZ(kPi - rad(yawIn), 0, 0);
            }
        }
        if (!best) { // no wall: lay the text on the ground in front of the player
            float groundBest = -1e9f;
            for (int kx = 0; kx < 8; ++kx) {
                float  yaw  = cam.yaw + (kx == 0 ? 0.0f : (rnd(s, 230 + kx * 2) * 2 - 1) * 25.0f);
                double dist = 4.5 + double(rnd(s, 231 + kx * 2)) * 2.5;
                Vec3   d    = dir(yaw, 0);
                Vec3   top  = eye + Vec3{d.x * dist, 1.0, d.z * dist};
                RayHit hit  = w->clip(top, top + Vec3{0, -8, 0}, RayShape::Collider, true);
                if (!hit.hit || hit.face != Face::Up) continue;
                Vec3   pos = hit.pos + Vec3{0, 0.04, 0};
                RayHit see = w->clip(eye, pos, RayShape::Visual, false);
                if (see.hit && see.pos.dist(pos) > 0.3) continue;
                float score = (kx == 0 ? 50.0f : 0.0f) - crowd(pos);
                if (score <= groundBest) continue;
                groundBest = score;
                best       = pos;
                bestN      = Vec3{0, 1, 0};
                double yawTo = deg(std::atan2(-(pos.x - eye.x), pos.z - eye.z));
                bestQ      = Quat::rotationYXZ(kPi - rad(yawTo), -rad(90.0), 0);
            }
        }
    }
    if (!best) best = eye + dir(cam.yaw, std::clamp(cam.pitch, -20.0f, 20.0f)) * 6.0;
    p.a       = *best;
    Vec3 rel  = *best - eye;
    p.dist    = std::max(2.5, rel.length());
    p.yaw     = float(yawOf(rel));
    p.pitch   = p.dist < 1e-4 ? 0.0f : float(-deg(std::asin(rel.y / rel.length())));
    p.surf    = bestQ;
    p.surfN   = bestQ ? bestN : std::nullopt;
    p.fit     = 1.0f;
}

void Scene::fitToSurface(Placed& p, Config const& c, IWorld* w) {
    if (!p.surf || !p.surfN || !w) return;
    float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
    for (int j = 0; j < p.n(); ++j) {
        x0 = std::min(x0, p.wx0[size_t(j)] - p.widths[size_t(j)] / 2);
        x1 = std::max(x1, p.wx0[size_t(j)] + p.widths[size_t(j)] / 2);
        y0 = std::min(y0, -p.wy0[size_t(j)] - p.wsize[size_t(j)] * 0.45f);
        y1 = std::max(y1, -p.wy0[size_t(j)] + p.wsize[size_t(j)] * 0.45f);
    }
    if (x0 > x1) return;
    Vec3 right = p.surf->rotate({1, 0, 0}), up = p.surf->rotate({0, 1, 0}), n = *p.surfN, base = p.a;
    auto fits  = [&](Vec3 center, double fx0, double fx1, double fy0, double fy1) {
        int nx = std::max(2, int(std::ceil((fx1 - fx0) / 0.45)));
        int ny = std::max(1, int(std::ceil((fy1 - fy0) / 0.45)));
        for (int i = 0; i <= nx; ++i) {
            double x = fx0 + (fx1 - fx0) * i / nx;
            for (int k = 0; k <= ny; ++k) {
                double y = fy0 + (fy1 - fy0) * k / ny;
                Vec3   q = center + right * x + up * y;
                if (!w->solid(BlockPos::of(q - n * 0.3))) return false;
                if (!w->empty(BlockPos::of(q + n * 0.3))) return false;
            }
        }
        return true;
    };
    static constexpr float  scales[] = {1.0f, 0.8f, 0.65f, 0.5f};
    static constexpr double slide[]  = {0.0, 0.5, -0.5, 1.0, -1.0, 1.5, -1.5, 2.0, -2.0, 3.0, -3.0};
    static constexpr double lift[]   = {0.0, 1.0, -1.0};
    for (float sc : scales) {
        p.fit    = sc;
        float e  = em(p, c);
        for (double dv : lift)
            for (double dh : slide) {
                Vec3 center = base + right * dh + up * dv;
                if (fits(center, x0 * e, x1 * e, y0 * e, y1 * e)) { p.a = center; return; }
            }
    }
    p.fit = 0.5f;
}

bool Scene::wordsInBlocks(Placed const& p, Quat const& q, float e, IWorld* w) {
    std::vector<Aabb> boxes;
    for (int j = 0; j < p.n(); ++j) {
        float hw = p.widths[size_t(j)] * e * 0.5f, hh = 0.42f * p.wsize[size_t(j)] * e;
        float cx = p.wx0[size_t(j)] * e, cy = -p.wy0[size_t(j)] * e;
        int   nx = std::max(3, int(std::ceil(double(hw * 2 / 0.2f))) + 1);
        int   ny = std::max(2, int(std::ceil(double(hh * 2 / 0.2f))) + 1);
        for (int ix = 0; ix < nx; ++ix)
            for (int iy = 0; iy < ny; ++iy) {
                Vec3     v  = q.rotate({double(cx - hw + 2 * hw * float(ix) / float(nx - 1)), double(cy - hh + 2 * hh * float(iy) / float(ny - 1)), 0});
                Vec3     wp = p.a + v;
                BlockPos bp = BlockPos::of(wp);
                if (w->isAir(bp)) continue;
                boxes.clear();
                w->collisionBoxes(bp, boxes);
                double lx = wp.x - bp.x, ly = wp.y - bp.y, lz = wp.z - bp.z;
                for (auto const& b : boxes)
                    if (lx >= b.x0 - 0.05 && lx <= b.x1 + 0.05 && ly >= b.y0 - 0.05 && ly <= b.y1 + 0.05 && lz >= b.z0 - 0.05 && lz <= b.z1 + 0.05) return true;
            }
    }
    return false;
}

void Scene::keepOutOfBlocks(Placed& p, Config const& c, CameraState const& cam, IWorld* w) {
    if (!w) return;
    float e = em(p, c);
    Quat  q = facing(p, c, cam) * Quat::rotationZ(rad(-p.tilt));
    Vec3  o = p.a;
    if (!wordsInBlocks(p, q, e, w)) return;
    // pull it toward the camera first ...
    Vec3   ex  = o - cam.pos;
    double len = ex.length();
    for (double d = len - 0.4; d >= 1.8; d -= 0.4) {
        p.a = cam.pos + ex * (d / len);
        if (!wordsInBlocks(p, q, e, w)) { p.dist = d; return; }
    }
    // ... then lift it
    for (double up = 0.5; up <= 3.01; up += 0.5) {
        p.a = o + Vec3{0, up, 0};
        if (!wordsInBlocks(p, q, e, w)) return;
    }
    p.a = o;
}

void Scene::place(Placed& p, Config const& c, CameraState const& cam, IWorld* w, TextWidthFn const& width) {
    int64_t const s       = p.seed;
    bool const    random  = c.randomPosition;
    bool const    skyStyle = c.lyricsStyle == LyricsStyle::Sky;
    bool const    shaderMode = false; // Java: FLOATING with Iris shaders on; RF draws real 3D always
    int           tries   = random ? (c.lyricsStyle == LyricsStyle::Floating ? 40 : 12) : (!skyStyle && !shaderMode ? 1 : 8);
    double        cloud   = (c.lyricsStyle == LyricsStyle::Screen || !w) ? std::nan("") : w->cloudY();
    float         bestScore = -1e9f, bYaw = 0, bPitch = 0, bSx = 0.5f, bSy = 0.3f, bSize = 6.0f, bPY = 0, bPP = 0;
    double        bDist   = 6.0;
    auto          avoid   = c.lyricsStyle == LyricsStyle::Floating ? avoidBoxes(cam, w) : std::vector<AvoidBox>{};

    float const   spread  = c.spreadOut ? 1.6f : 1.0f; // [BF] "Spread lyrics out"
    for (int k = 0; k < tries; ++k) {
        float  r1 = random ? rnd(s, 100 + k * 3) * 2 - 1 : 0.0f;
        float  r2 = random ? rnd(s, 101 + k * 3) : 0.5f;
        float  r3 = random ? rnd(s, 102 + k * 3) : 0.5f;
        float  yaw = 0, pitch = 0, sx = 0, sy = 0, pY = 0, pP = 0, tSize = 6.0f, score = 1000.0f;
        double dist = 8.0;
        switch (c.lyricsStyle) {
        case LyricsStyle::Sky:
            if (c.skyWords) {
                dist  = skyDist(c, cam);
                yaw   = cam.yaw + r1 * (36.0f * spread);
                pitch = std::clamp(cam.pitch * 0.5f - 16.0f + (r2 - 0.5f) * (30.0f * spread), -60.0f, -4.0f);
                score -= float(blocked(yaw, pitch, dist, 6.0f, cam, w) * 450);
                if (inClouds(pitch, dist, cloud, cam)) score -= 500.0f;
            } else { // two-line "pairs" in the sky: odd lines sit beside their even mate
                dist        = skyDist(c, cam);
                float emDeg = float(deg(std::atan(double(em(p, c)) / dist)));
                float h = emDeg * 2.0f * spread, v = emDeg * 1.9f * (1.0f + (spread - 1.0f) * 0.4f); // wider, barely taller (stays on screen)
                int   side  = p.line.index >= 0 ? (p.line.index % 2 == 0 ? -1 : 1) : 0;
                Placed* mate = side > 0 ? findIndex(p.line.index - 1) : nullptr;
                if (mate) {
                    if (k > 0) continue;
                    pY = mate->pairYaw;
                    pP = mate->pairPitch;
                    score += 5000.0f;
                } else {
                    float ry = k == 0 ? 0.0f : r1, rp = k == 0 ? 0.5f : r2;
                    if (k == 0) score += 120.0f;
                    pY = cam.yaw + ry * (14.0f * spread * spread);
                    pP = std::min(cam.pitch - (8.0f + (rp - 0.5f) * 6.0f) - (k >= tries / 2 ? 4.0f : 0.0f), -3.0f - v);
                }
                yaw   = pY + float(side) * h;
                pitch = pP + float(side) * v;
                score -= float(blocked(yaw, pitch, dist, 8.0f, cam, w) * 450);
                if (inClouds(pitch, dist, cloud, cam)) score -= 500.0f;
            }
            break;
        case LyricsStyle::Floating: {
            if (!c.followView && !c.viewLock) yaw = cam.yaw + (r1 < 0 ? -1.0f : 1.0f) * (8.0f * spread + std::fabs(r1) * 26.0f * spread);
            else yaw = (c.viewLock ? 0.0f : cam.yaw) + r1 * 13.0f * spread;
            float rp = (r2 - 0.5f) * 26.0f * spread;
            pitch    = std::clamp((c.viewLock ? 0.0f : cam.pitch * 0.6f) + rp - 9.0f, -40.0f, 28.0f);
            dist     = double((5.0f + r3 * 2.5f) * std::clamp(c.lyricsDistance, 0.4f, 2.0f));
            float absYaw = c.viewLock ? cam.yaw + yaw : yaw, absPitch = c.viewLock ? cam.pitch + pitch : pitch;
            float cy = std::fabs(wrap(absYaw - cam.yaw)), cp = std::fabs(absPitch - cam.pitch);
            if (cy < 4.0f && cp < 3.0f) score -= 250.0f;
            if (c.combatSafe && cy < c.keepOutDeg && cp < c.keepOutDeg * 0.75f) score -= 6000.0f; // [BF] crosshair keep-out
            for (auto const& b : avoid) {
                float dy = std::fabs(wrap(absYaw - b.yaw)) - b.hw - 12.0f, dp = std::fabs(absPitch - b.pitch) - b.hh - 6.0f;
                if (dy < 0 && dp < 0) score -= 4000.0f;
            }
            if (!avoid.empty() && cy < 14.0f && cp < 10.0f) score -= 1500.0f;
            float hw   = float(deg(std::atan(2.8 / dist)));
            int   ends = blocked(absYaw, absPitch, dist, hw, cam, w);
            score -= float(ends * 800);
            float hh   = float(deg(std::atan(0.7 / dist)));
            int   more = blocked(absYaw, absPitch - hh, dist, hw, cam, w) + blocked(absYaw, absPitch + hh, dist, hw, cam, w);
            score -= float((ends + more) * 700);
            if (inClouds(absPitch, dist, cloud, cam)) score -= 500.0f;
            tSize       = float(dist / double(std::clamp(c.lyricsDistance, 0.4f, 2.0f)));
            double free = clearance(absYaw, absPitch, dist + 4.0, cam, w);
            if (free < dist + 4.0) {
                score -= 800.0f;
                if (free < 2.2) score -= 700.0f;
                dist = std::max(1.8, free - 0.8);
                score -= float(8.0 - free) * 10.0f;
            }
            break;
        }
        case LyricsStyle::Screen:
            sx = c.screenLyricsX + (random ? r1 * 0.26f : 0.0f);
            sy = c.screenLyricsY + (random ? (r2 - 0.5f) * 0.24f : 0.0f);
            if (std::fabs(sx - 0.5f) < 0.18f && std::fabs(sy - 0.5f) < 0.12f) sy = 0.32f;
            break;
        default: break;
        }

        float nearest = 1000.0f;
        for (auto const& o : mVisible) {
            if (o.exitAt >= 0 && c.lyricsStyle == LyricsStyle::Screen) continue;
            float d;
            if (c.lyricsStyle == LyricsStyle::Screen) {
                float dx = (o.sx - sx) * 1.6f, dy = o.sy - sy;
                d        = std::sqrt(dx * dx + dy * dy) * 100.0f;
            } else {
                float oy = o.yaw, op = o.pitch;
                if (!c.viewLock && c.lyricsStyle != LyricsStyle::Wall) {
                    Vec3 e = o.a - cam.pos;
                    if (e.length() > 0.001) { oy = float(yawOf(e)); op = float(-deg(std::asin(e.y / e.length()))); }
                }
                float dy = std::fabs(wrap(oy - yaw)), dp = std::fabs(op - pitch) * 2.2f;
                d        = std::sqrt(dy * dy + dp * dp);
            }
            nearest = std::min(nearest, d);
        }
        if (chunked(c) && !c.viewLock) { // words of the same line must not overlap each other
            float emB   = skyWords(c) ? em(p, c) : float(0.0765 * std::max(2.5, double(tSize))) * c.lyricsScale;
            int   chars = 0;
            for (int j = p.w0; j < p.w1; ++j) chars += int(text::length(p.line.words[size_t(j)].text)) + 1;
            float cw = float(deg(std::atan(double(float(chars) * 0.55f * emB / 2.0f) / dist)));
            float ch = float(deg(std::atan(double(0.8f * emB) / dist)));
            for (auto const& ox : mVisible) {
                Vec3   e   = ox.a - cam.pos;
                double len = e.length();
                if (len < 0.001) continue;
                float ok = float(ox.dist / len), oy = float(yawOf(e)), op = float(-deg(std::asin(e.y / len)));
                if (std::fabs(wrap(oy - yaw)) < cw + ox.angW * ok + 3.0f * spread && std::fabs(op - pitch) < ch + ox.angH * ok + 2.5f * spread) score -= 12000.0f;
                else if (std::fabs(wrap(oy - yaw)) < cw + ox.angW * ok + 6.0f * spread && std::fabs(op - pitch) < ch + ox.angH * ok + 4.0f * spread) score -= 600.0f;
            }
        }
        if (nearest < 22.0f * spread) score -= (22.0f * spread - nearest) * 40.0f;
        score += std::min(nearest, 60.0f * spread) * 4.0f;
        if (score > bestScore) {
            bestScore = score;
            bYaw = yaw; bPitch = pitch; bDist = dist; bSx = sx; bSy = sy; bSize = tSize; bPY = pY; bPP = pP;
        }
    }
    p.yaw = bYaw; p.pitch = bPitch; p.sx = bSx; p.sy = bSy; p.dist = bDist; p.sizeDist = bSize; p.pairYaw = bPY; p.pairPitch = bPP;
    p.surf.reset();
    if (c.lyricsStyle == LyricsStyle::Wall) wallPlace(p, s, cam, w);
    if ((c.lyricsStyle == LyricsStyle::Floating || c.lyricsStyle == LyricsStyle::Sky) && !c.viewLock) p.a = cam.pos + dir(bYaw, bPitch) * bDist;
    if (c.lyricsStyle == LyricsStyle::Floating && !c.viewLock) p.a.y += double(c.floatHeight);

    bool const sky = c.lyricsStyle == LyricsStyle::Sky;
    p.turn = sky && c.arcLayout ? (rnd(s, 5) > 0.5f ? 1.0f : -1.0f) * (c.skyWords ? 8.0f + rnd(s, 6) * 16.0f : 3.0f + rnd(s, 6) * 4.0f) : 0.0f;
    p.tilt = c.arcLayout ? (sky ? (c.skyWords ? -22.0f + rnd(s, 4) * 44.0f : -1.5f + rnd(s, 4) * 3.0f) : -10.0f + rnd(s, 4) * 14.0f) : 0.0f;
    if (c.lyricsStyle == LyricsStyle::Wall) p.tilt = c.arcLayout ? -3.0f + rnd(s, 4) * 6.0f : 0.0f;

    // ---- word sizes, widths and rows
    int const          n    = p.n();
    std::string const& font = fontOf(p, c);
    float const        space = width ? width(" ", font) : 0.3f;
    p.wx0.assign(size_t(n), 0); p.wy0.assign(size_t(n), 0); p.wsize.assign(size_t(n), 1); p.wrot.assign(size_t(n), 0); p.widths.assign(size_t(n), 0);
    p.dropped.assign(size_t(n), false);
    for (int j = 0; j < n; ++j) {
        int   i    = p.w0 + j;
        float size = 1.0f;
        if (c.varySizes) {
            float hx = rnd(s, 10 + i);
            size     = hx > 0.82f ? 1.4f : (hx > 0.55f ? 1.12f : (hx < 0.2f ? 0.82f : 0.96f));
            if (j == 0 && text::length(p.line.words[size_t(i)].text) <= 2 && !sky) size = 1.35f;
        }
        p.wsize[size_t(j)]  = size;
        std::string shown   = show(p.line.words[size_t(i)].text, c.allCaps);
        p.widths[size_t(j)] = ((width ? width(shown, font) : 0.5f * float(text::length(shown))) + 0.08f) * size;
        p.wrot[size_t(j)]   = c.arcLayout && !sky ? (rnd(s, 40 + i) - 0.5f) * 8.0f : 0.0f;
    }
    float const spaceEm = std::max(0.26f, space);
    float       maxRow  = sky ? 18.0f : (chunked(c) ? 99.0f : 9.5f);
    std::vector<std::pair<int, int>> rows;
    for (int pass = 0; pass < 20; ++pass) {
        rows.clear();
        int   start = 0;
        float rowW  = 0;
        for (int j = 0; j < n; ++j) {
            float add = p.widths[size_t(j)] + (j > start ? spaceEm : 0.0f);
            if (j > start && rowW + add > maxRow) { rows.push_back({start, j}); start = j; rowW = p.widths[size_t(j)]; }
            else rowW += add;
        }
        rows.push_back({start, n});
        if (!sky || rows.size() <= 3) break;
        maxRow *= 1.25f;
    }
    float widest = 0;
    for (auto [a, b] : rows) {
        float wr = 0;
        for (int j = a; j < b; ++j) wr += p.widths[size_t(j)] + (j > a ? spaceEm : 0.0f);
        widest = std::max(widest, wr);
    }
    float const rowGap = sky ? 1.4f : 1.5f;
    float const totalH = float(rows.size() - 1) * rowGap;
    for (size_t r = 0; r < rows.size(); ++r) {
        auto [a, b] = rows[r];
        float wr    = 0;
        for (int j = a; j < b; ++j) wr += p.widths[size_t(j)] + (j > a ? spaceEm : 0.0f);
        float shift = sky ? 0.0f : (rows.size() > 1 ? (r == 0 ? -1.0f : 1.0f) * 0.6f * (random ? 0.5f + rnd(s, 60 + int(r)) * 0.5f : 0.6f) : 0.0f);
        float x     = sky ? -widest / 2 : -wr / 2 + shift;
        for (int j = a; j < b; ++j) {
            float cxw          = x + p.widths[size_t(j)] / 2;
            p.wx0[size_t(j)]   = cxw;
            float arc          = c.arcLayout ? (sky ? 0.008f : 0.03f) * cxw * cxw : 0.0f;
            p.wy0[size_t(j)]   = -totalH / 2 + float(r) * rowGap + (sky ? -arc : arc) - (p.wsize[size_t(j)] - 1.0f) * 0.35f;
            x += p.widths[size_t(j)] + spaceEm;
        }
    }
    p.rowsN   = int(rows.size());
    float emA = em(p, c);
    p.angW    = float(deg(std::atan(double(widest * 0.5f * emA) / std::max(0.5, p.dist))));
    p.angH    = float(deg(std::atan(double(float(rows.size()) * 0.7f * emA) / std::max(0.5, p.dist))));
    if (sky && !c.viewLock && !c.skyWords && p.line.index >= 0 && p.line.index % 2 == 1)
        if (Placed* mate = findIndex(p.line.index - 1); mate && mate->rowsN + p.rowsN > 2) {
            float emDeg = float(deg(std::atan(double(em(p, c)) / p.dist)));
            p.pitch += float(mate->rowsN - 1 + p.rowsN - 1) * rowGap * 0.5f * emDeg;
            p.a = cam.pos + dir(p.yaw, p.pitch) * p.dist;
        }
    if (rtl(p.line.text))
        for (auto& x : p.wx0) x = -x;
    if (c.lyricsStyle == LyricsStyle::Wall) fitToSurface(p, c, w);
    if (c.lyricsStyle == LyricsStyle::Floating && !c.viewLock) keepOutOfBlocks(p, c, cam, w);
}

// ---------------------------------------------------------------- draw
std::vector<DrawWord> Scene::draw(SceneInput const& in, Config const& c, CameraState const& cam, IWorld* w) {
    std::vector<DrawWord> out;
    if (!c.enabled || !c.showLyrics || mVisible.empty() || mPauseFade < 0.01f) return out;
    int64_t const t        = in.t;
    float const   speed    = std::max(0.3f, c.animSpeed);
    bool const    sky      = c.lyricsStyle == LyricsStyle::Sky;
    bool const    wall     = c.lyricsStyle == LyricsStyle::Wall;
    float const   glowBase = c.glow ? 0.65f : 0.0f;
    float const   beat     = c.beatGlow ? in.beat : 0.0f;
    float const   combatK  = c.combatSafe ? 1.0f - (1.0f - c.combatAlpha) * anim::clamp01(in.combat) : 1.0f;

    if (!inWorld(c)) { // SCREEN mode (WorldLyrics.drawScreen), in screen-height units
        float const emPx = 15.0f * c.lyricsScale / 240.0f; // Java: 15 GUI px at a ~240 px tall GUI
        float const gA   = mPauseFade * in.focusA * c.textOpacity * combatK;
        if (gA < 0.01f) return out;
        for (auto& p : mVisible)
            for (int j = 0; j < p.n(); ++j) {
                Word const& wd = p.line.words[size_t(p.w0 + j)];
                auto        s  = wordAnim(p, j, wd, t, c, speed);
                if (!s) continue;
                auto [alpha, dx, dy, sc] = *s;
                double   bob  = std::sin(in.nowSeconds * 1.6 + j * 0.9 + double(p.seed)) * 0.04;
                DrawWord d;
                d.text   = show(wd.text, c.allCaps);
                d.font   = fontOf(p, c);
                d.screen = true;
                d.origin = {double(p.sx) * cam.screenAspect, double(p.sy), 0};
                d.orient = Quat::rotationZ(rad(p.tilt));
                d.x      = (p.wx0[size_t(j)] + dx) * emPx;
                d.y      = float((double(p.wy0[size_t(j)] + dy) + bob) * emPx);
                d.rotZ   = float(rad(p.wrot[size_t(j)]));
                d.scale  = emPx * p.wsize[size_t(j)] * sc;
                d.alpha  = alpha * gA;
                d.sung   = t >= wd.start && t < wd.end + 80;
                d.glow   = glowBase + (d.sung ? 0.35f : 0.0f) + beat * 0.5f;
                d.key    = int64_t(p.line.index) * 1000 + p.w0 + j;
                out.push_back(std::move(d));
            }
        return out;
    }

    for (auto& p : mVisible) {
        if (c.lyricsStyle == LyricsStyle::Floating) p.coverFade = anim::approach(p.coverFade, overPlayer(p, c, cam, w) ? 0.35f : 1.0f, 6.0f, in.dt);
        float gA = p.coverFade * mPauseFade * in.focusA * c.textOpacity * p.occlVis * (sky ? 1.0f : anim::easeInOut(p.viewVis)) * combatK;
        Vec3  a  = anchor(p, c, cam);
        if (c.combatSafe) { // [BF] fade text that drifts into the crosshair area
            Vec3  d   = a - cam.pos;
            float dyw = std::fabs(wrap(float(yawOf(d)) - cam.yaw)), dp = std::fabs(float(pitchOf(d)) - cam.pitch);
            float ang = std::sqrt(dyw * dyw + dp * dp);
            gA *= 0.15f + 0.85f * anim::clamp01((ang - c.keepOutDeg * 0.5f) / std::max(1.0f, c.keepOutDeg * 0.5f));
        }
        if (gA < 0.02f) continue;
        float e = em(p, c);
        Quat  q = facing(p, c, cam) * Quat::rotationZ(rad(-p.tilt));
        for (int j = 0; j < p.n(); ++j) {
            Word const& wd = p.line.words[size_t(p.w0 + j)];
            auto        s  = wordAnim(p, j, wd, t, c, speed);
            bool const  falling = c.fallingLetters && p.exitAt >= 0 && t >= p.exitAt + 55 * int64_t(j) + c.fallDelayMs;
            if (!s && !(falling && !p.dropped[size_t(j)])) continue;
            std::array<float, 4> st = s ? *s : std::array<float, 4>{1.0f, 0, 0, 1};
            if (falling) { // [BF] the word stops animating and drops as letters
                if (p.dropped[size_t(j)]) continue;
                p.dropped[size_t(j)] = true;
            }
            auto [alpha, dx, dy, sc] = st;
            double   bob  = wall ? 0.0 : std::sin(in.nowSeconds * 1.6 + j * 0.9 + double(p.seed)) * (sky ? 0.02 : 0.04);
            DrawWord d;
            d.text      = show(wd.text, c.allCaps);
            d.font      = fontOf(p, c);
            d.wall      = wall;
            d.origin    = a;
            d.orient    = q;
            d.x         = (p.wx0[size_t(j)] + dx) * e;
            d.y         = -float((double(p.wy0[size_t(j)] + dy) + bob) * e);
            d.rotZ      = float(rad(-p.wrot[size_t(j)]));
            d.scale     = e * p.wsize[size_t(j)] * sc;
            d.alpha     = alpha * gA;
            d.sung      = t >= wd.start && t < wd.end + 80;
            d.glow      = glowBase + (d.sung ? 0.35f : 0.0f) + beat * 0.5f;
            d.layerStep = wall ? 0.006f : 0.06f; // font units (wall: world units, see Glyphs.cpp)
            d.dropNow   = falling;
            d.key       = int64_t(p.line.index) * 1000 + p.w0 + j;
            out.push_back(std::move(d));
        }
    }
    return out;
}

} // namespace bf::lyrics
