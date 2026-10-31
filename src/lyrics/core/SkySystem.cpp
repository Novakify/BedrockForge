// LyricsFlow port - live sky per frame + shared store. See SkySystem.h.
#include "lyrics/core/SkySystem.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "lyrics/core/Config.h"
#include "lyrics/core/Perf.h"

namespace bf::lyrics::sky {

namespace {
uint32_t rgba(uint32_t rgb, float a) {
    uint32_t A = uint32_t(std::clamp(int(std::lround(a * 255.0f)), 0, 255));
    return (rgb >> 16 & 255) | (rgb >> 8 & 255) << 8 | (rgb & 255) << 16 | A << 24;
}
} // namespace

void toVerts(std::vector<Quad> const& quads, std::vector<BfSkyVertV1>& out) {
    out.clear();
    out.reserve(quads.size() * 4);
    for (auto const& q : quads) {
        uint32_t fl = (q.layer == Layer::World ? BF_SKY_WORLD : 0u) | (q.tex == Tex::Sprite ? BF_SKY_SPRITE : 0u);
        for (auto const& v : q.v) out.push_back({{v.x, v.y, v.z}, {v.u, v.v}, rgba(v.rgb, v.a), fl, 0});
    }
}

void System::step(bool on, Config const& cfg, AutoPerf const& perf, SystemIn const& in) {
    std::optional<Preset> p;
    if (on && in.overworld) p = find(cfg.skyPreset, cfg);
    bool picture = on && in.overworld && cfg.skyImage && !in.pictureFile.empty();

    Clock::In ci;
    ci.dtSec = in.dtSec; ci.night = in.night; ci.beat = in.beat; ci.lineActive = in.lineActive; ci.playing = in.playing;
    ci.skyOpen = in.skyOpen; ci.picture = picture;
    mFrame = mClock.tick(p ? &*p : nullptr, cfg, ci);

    FxIn fi;
    fi.nowMs = in.nowMs; fi.dtSec = in.dtSec; fi.cam = Cam::of(in.yaw, in.pitch);
    fi.camX = in.camX; fi.camY = in.camY; fi.camZ = in.camZ;
    fi.quality = perf.quality(cfg); fi.picture = picture;
    mQuads.clear();
    if (p) mFx.frame(&*p, cfg, perf, mFrame, fi, mQuads);

    BfSkyFrameV1 h{};
    h.size    = sizeof h;
    h.version = 1;
    h.active  = (p && mFrame.visible > 0.01f) || mFrame.picture > 0.01f ? 1u : 0u;
    h.frame   = ++mSeq;
    if (p) {
        float id3[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; // the dome is drawn in world axes (RF builds rays from the camera)
        auto  q      = pack(*p, aurora1(*p, cfg), aurora2(*p, cfg), mFrame.time, cfg, id3);
        for (int i = 0; i < 16; ++i) std::memcpy(h.params[i], q[size_t(i)].data(), 4);
    }
    h.info[0]       = mFrame.t;
    h.info[1]       = mFrame.brightness;
    h.info[2]       = picture ? 0.0f : mFrame.alpha;
    h.picture       = picture ? 1u : 0u;
    h.pictureAlpha  = mFrame.picture;
    h.pictureBright = mFrame.picBright;
    h.camYaw = float(in.yaw); h.camPitch = float(in.pitch);
    std::strncpy(h.spritesFile, in.spritesFile.c_str(), sizeof h.spritesFile - 1);
    std::strncpy(h.pictureFile, picture ? in.pictureFile.c_str() : "", sizeof h.pictureFile - 1);
    std::vector<BfSkyVertV1> v;
    toVerts(mQuads, v);
    SkyStore::get().publish(h, std::move(v));
}

SkyStore& SkyStore::get() {
    static SkyStore s;
    return s;
}
void SkyStore::publish(BfSkyFrameV1 const& hdr, std::vector<BfSkyVertV1>&& verts) {
    std::lock_guard l(mMutex);
    bool images = std::strcmp(hdr.spritesFile, mHdr.spritesFile) != 0 || std::strcmp(hdr.pictureFile, mHdr.pictureFile) != 0;
    uint32_t gen = mHdr.imageGeneration + (images ? 1 : 0);
    mHdr         = hdr;
    mHdr.imageGeneration = gen;
    mVerts       = std::move(verts);
}
uint32_t SkyStore::copy(BfSkyFrameV1* hdr, BfSkyVertV1* dst, uint32_t cap) {
    std::lock_guard l(mMutex);
    if (!hdr || hdr->size < sizeof(BfSkyFrameV1)) return 0;
    BfSkyFrameV1 h = mHdr;
    h.size      = sizeof h;
    h.version   = 1;
    h.vertTotal = uint32_t(mVerts.size());
    uint32_t n  = dst ? std::min<uint32_t>(cap / 4 * 4, uint32_t(mVerts.size())) : 0;
    if (n) std::memcpy(dst, mVerts.data(), size_t(n) * sizeof(BfSkyVertV1));
    h.vertCount = n;
    *hdr        = h;
    return n;
}

} // namespace bf::lyrics::sky

extern "C" BF_LYRICS_EXPORT uint32_t BF_CopySkyFrame(BfSkyFrameV1* hdr, BfSkyVertV1* dst, uint32_t capacity) {
    return bf::lyrics::sky::SkyStore::get().copy(hdr, dst, capacity);
}
