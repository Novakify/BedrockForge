// FiguraBridge animations - line-by-line port of Figura 0.1.6 Animation / AnimationPlayer / Keyframe / Interpolation.
#include "figura/core/Anim.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "figura/core/Model.h"

namespace bf::figura {

char const* playStateName(PlayState s) {
    switch (s) {
    case PlayState::Stopped: return "STOPPED";
    case PlayState::Paused: return "PAUSED";
    case PlayState::Playing: return "PLAYING";
    default: return "HOLDING";
    }
}
char const* loopModeName(LoopMode m) {
    switch (m) {
    case LoopMode::Loop: return "LOOP";
    case LoopMode::Once: return "ONCE";
    default: return "HOLD";
    }
}
bool loopModeFromName(std::string s, LoopMode& out) {
    for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    if (s == "LOOP") { out = LoopMode::Loop; return true; }
    if (s == "ONCE") { out = LoopMode::Once; return true; }
    if (s == "HOLD") { out = LoopMode::Hold; return true; }
    return false;
}

// ---------------------------------------------------------------- MathUtils
static double lerp(double d, double a, double b) { return d * (b - a) + a; }
static double catmull(double d, double a, double b, double c, double e) {
    return 0.5 * (2 * b + (-a + c) * d + (2 * a - 5 * b + 4 * c - e) * d * d + (-a + 3 * b - 3 * c + e) * d * d * d);
}
static double bezier(double t, double p0, double p1, double p2, double p3) {
    double d = 1 - t;
    return p0 * d * d * d + 3 * p1 * d * d * t + 3 * p2 * d * t * t + p3 * t * t * t;
}
static double bezierDeriv(double t, double p0, double p1, double p2, double p3) {
    double d = 1 - t;
    return 3 * (p1 - p0) * d * d + 6 * (p2 - p1) * d * t + 3 * (p3 - p2) * t * t;
}
static double bezierFindT(double x, double p0, double p1, double p2, double p3) {
    double t = 0.5;
    for (int i = 0; i < 100; i++) {
        double xb = bezier(t, p0, p1, p2, p3), dx = bezierDeriv(t, p0, p1, p2, p3);
        if (dx == 0) break;
        double tn = t - (xb - x) / dx;
        if (std::fabs(tn - t) < 0.0001) return tn;
        t = tn;
    }
    return t;
}

// ---------------------------------------------------------------- Keyframe
std::string Keyframe::chunkName(int idx) const {
    // "scripts rely on the animation name followed by "keyframe" being at the start"
    char t[64];
    std::snprintf(t, sizeof t, "%g", double(time));
    char const* ch = channel == TransformType::Position ? "POSITION"
                   : channel == TransformType::Rotation ? "ROTATION"
                   : channel == TransformType::GlobalRot ? "GLOBAL_ROT" : "SCALE";
    char const* ax = idx == 0 ? "X" : idx == 1 ? "Y" : idx == 2 ? "Z" : "?";
    return (animation ? animation->name : std::string()) + " keyframe (part '" + (part ? part->name : "") + "', time " + t +
           "s, " + ch + " " + ax + ")";
}

double Keyframe::evalCompile(std::array<KeyframeValue, 3>& cache, std::array<std::string, 3> const& code, int idx,
                             float delta, IAnimScripts* s) {
    KeyframeValue& kv = cache[idx];
    if (!kv.compiled) {
        // Double.parseDouble first (Java accepts things like " 1.5 ", "1e3", "NaN", "Infinity")
        std::string const& src = code[idx];
        char* end = nullptr;
        double d = std::strtod(src.c_str(), &end);
        bool isNum = end && end != src.c_str();
        if (isNum) {
            while (*end == ' ' || *end == '\t' || *end == '\n' || *end == 'f' || *end == 'F' || *end == 'd' || *end == 'D') end++;
            isNum = *end == 0;
        }
        if (isNum) {
            kv.compiled = true; kv.literal = true; kv.value = d;
        } else {
            if (!s || !s->ready()) return 0;
            bool lit = false; double litv = 0;
            int h = s->compileKeyframe(chunkName(idx), src, &lit, &litv);
            if (h == 0 && !lit) return 0; // fallback 0 (error reported by the runtime)
            kv.compiled = true; kv.literal = lit; kv.value = litv; kv.handle = h;
        }
    }
    if (kv.literal) return kv.value;
    return s ? s->evalKeyframe(kv.handle, delta, animation, chunkName(idx)) : 0.0;
}

Vec3 Keyframe::getTargetA(float delta, IAnimScripts* s) {
    if (targetA) return *targetA;
    return {evalCompile(aCache, aCode, 0, delta, s), evalCompile(aCache, aCode, 1, delta, s), evalCompile(aCache, aCode, 2, delta, s)};
}
Vec3 Keyframe::getTargetB(float delta, IAnimScripts* s) {
    if (targetB) return *targetB;
    return {evalCompile(bCache, bCode, 0, delta, s), evalCompile(bCache, bCode, 1, delta, s), evalCompile(bCache, bCode, 2, delta, s)};
}

// ---------------------------------------------------------------- Interpolation
static Vec3 result(Vec3 r, float strength, TransformType type) {
    if (type == TransformType::Scale) return {(r.x - 1) * strength + 1, (r.y - 1) * strength + 1, (r.z - 1) * strength + 1};
    return r * strength;
}

static Vec3 interpolate(Interpolation it, std::vector<Keyframe>& f, int cur, int tgt, float strength, float delta,
                        TransformType type, IAnimScripts* s) {
    switch (it) {
    case Interpolation::Linear: {
        Vec3 prev = f[cur].getTargetB(delta, s), next = f[tgt].getTargetA(delta, s);
        return result({lerp(delta, prev.x, next.x), lerp(delta, prev.y, next.y), lerp(delta, prev.z, next.z)}, strength, type);
    }
    case Interpolation::CatmullRom: {
        Keyframe* prev = &f[cur];
        Keyframe* next = &f[tgt];
        Keyframe* pp   = &f[std::max(0, cur - 1)];
        Keyframe* nn   = &f[std::min(int(f.size()) - 1, tgt + 1)];
        if (pp == prev) pp = &f.back();
        if (nn == next) nn = &f.front();
        Vec3 p0 = pp->getTargetB(delta, s), p1 = prev->getTargetB(delta, s), p2 = next->getTargetA(delta, s), p3 = nn->getTargetA(delta, s);
        return result({catmull(delta, p0.x, p1.x, p2.x, p3.x), catmull(delta, p0.y, p1.y, p2.y, p3.y), catmull(delta, p0.z, p1.z, p2.z, p3.z)},
                      strength, type);
    }
    case Interpolation::Bezier: {
        Keyframe& prev = f[cur];
        Keyframe& next = f[tgt];
        Vec3 p1t = prev.bezierRightTime, p2t = next.bezierLeftTime;
        Vec3 p0 = prev.getTargetB(delta, s), p3 = next.getTargetA(delta, s);
        Vec3 p1 = prev.bezierRight + p0, p2 = next.bezierLeft + p3;
        return result({bezier(bezierFindT(delta, 0, p1t.x, p2t.x, 1), p0.x, p1.x, p2.x, p3.x),
                       bezier(bezierFindT(delta, 0, p1t.y, p2t.y, 1), p0.y, p1.y, p2.y, p3.y),
                       bezier(bezierFindT(delta, 0, p1t.z, p2t.z, 1), p0.z, p1.z, p2.z, p3.z)},
                      strength, type);
    }
    default: return result(f[cur].getTargetB(delta, s), strength, type);
    }
}

// ---------------------------------------------------------------- Animation
Animation::Animation(std::string mdl, std::string n, LoopMode lp, bool ovr, float len, float off, float bld, float sdel,
                     float ldel)
: modelName(std::move(mdl)), name(std::move(n)), length(len), blend(bld), offset(off), startDelay(sdel), loopDelay(ldel),
  override(ovr ? 7 : 0), loop(lp) {}

void Animation::addAnimation(Part* part, AnimationChannel ch) {
    for (auto& e : animationParts)
        if (e.first == part) {
            e.second.push_back(std::move(ch));
            return;
        }
    animationParts.push_back({part, {}});
    animationParts.back().second.push_back(std::move(ch));
    // Map.Entry.comparingByKey() with FiguraModelPart.compareTo: parents before their children
    std::stable_sort(animationParts.begin(), animationParts.end(), [](auto const& a, auto const& b) {
        return b.first->isChildOf(a.first);
    });
}

void Animation::ctlInit(AnimClock const& c) { ctlPauseTime = 0; ctlLastTime = ctlTime = c.millis(); }
void Animation::ctlTick(AnimClock const& c) { ctlLastTime = ctlTime; ctlTime = c.millis(); }
void Animation::ctlReset() { ctlLastTime = ctlTime = ctlPauseTime = 0; }
void Animation::ctlPause(AnimClock const& c) { ctlLastTime = ctlTime; ctlPauseTime = c.millis(); }
void Animation::ctlResume(AnimClock const& c) {
    int64_t diff = c.millis() - ctlPauseTime;
    ctlLastTime += diff;
    ctlTime += diff;
}
float Animation::ctlDiff(AnimClock const& c) const { return c.paused && c.paused() ? 0.0f : float(ctlTime - ctlLastTime) / 1000.0f; }

void Animation::tick(AnimClock const& clock, IAnimScripts* scripts) {
    ctlTick(clock);
    time += ctlDiff(clock) * speed;
    switch (loop) {
    case LoopMode::Once:
        if ((!inverted && time >= length) || (inverted && time <= 0)) stop();
        break;
    case LoopMode::Loop:
        if (!inverted && time > length + loopDelay) time -= length + loopDelay - offset;
        else if (inverted && time < offset - loopDelay) time += length + loopDelay - offset;
        break;
    case LoopMode::Hold:
        time = inverted ? std::max(time, offset) : std::min(time, length);
        if (!inverted && time >= length) playState = PlayState::Holding;
        else if (inverted && time <= 0) playState = PlayState::Holding;
        break;
    }
    lastTime  = frameTime;
    frameTime = std::max(time, offset);
    if (inverted) playCode(frameTime, lastTime, scripts);
    else playCode(lastTime, frameTime, scripts);
}

void Animation::compileCode(IAnimScripts* s) {
    if (!s || !s->ready()) return;
    for (auto it = codeFrameQueue.begin(); it != codeFrameQueue.end();) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.2f", double(it->first));
        std::string chunk = "animations." + modelName + "." + name + " instr keyframe (time=" + buf + "s)";
        int h = s->compileCode(chunk, it->second);
        if (h) {
            codeFrames[it->first] = h;
            it = codeFrameQueue.erase(it);
        } else ++it;
    }
}

void Animation::playCode(float minTime, float maxTime, IAnimScripts* s) {
    if (!s || !s->ready()) return;
    compileCode(s);
    if (codeFrames.empty()) return;
    if (maxTime < minTime) {
        float len = length + 0.001f;
        playCode(std::min(minTime, len), len, s);
        minTime = offset;
    }
    // copy: running code may add/stop things
    std::vector<std::pair<float, int>> frames(codeFrames.begin(), codeFrames.end());
    for (auto& [t, h] : frames)
        if (t >= minTime && t < maxTime) s->runCode(h, this);
}

Animation& Animation::play(AnimClock const& c) {
    switch (playState) {
    case PlayState::Paused: ctlResume(c); break;
    case PlayState::Stopped:
        ctlInit(c);
        time      = inverted ? (length + startDelay) : (offset - startDelay);
        lastTime  = time;
        frameTime = 0.0f;
        break;
    default: return *this;
    }
    playState = PlayState::Playing;
    return *this;
}
Animation& Animation::pause(AnimClock const& c) {
    ctlPause(c);
    playState = PlayState::Paused;
    return *this;
}
Animation& Animation::stop() {
    ctlReset();
    playState = PlayState::Stopped;
    return *this;
}
Animation& Animation::setTime(float t) {
    time      = t;
    lastTime  = t;
    frameTime = std::max(t, offset);
    return *this;
}
Animation& Animation::setSpeed(std::optional<float> s) {
    speed    = s.value_or(1.0f);
    inverted = speed < 0;
    if (inverted && time >= length && playState == PlayState::Holding) playState = PlayState::Playing;
    else if (!inverted && time <= 0 && playState == PlayState::Holding) playState = PlayState::Playing;
    return *this;
}
Animation& Animation::newCode(float t, std::string data) {
    codeFrameQueue[std::max(t, 0.0f)] = std::move(data);
    return *this;
}

// ---------------------------------------------------------------- AnimationPlayer
int animationPlayerTick(Animation& anim, int limit, AnimClock const& clock, IAnimScripts* scripts) {
    if (anim.playState == PlayState::Stopped) return limit;
    if (anim.playState != PlayState::Paused) anim.tick(clock, scripts);
    for (auto& [part, channels] : anim.animationParts) {
        if (part->lastAnimationPriority > anim.priority) continue;
        bool merge                  = part->lastAnimationPriority == anim.priority;
        part->lastAnimationPriority = anim.priority;
        part->animated              = true;
        for (auto& channel : channels) {
            if (limit <= 0) return limit;
            auto& kf = channel.keyframes;
            if (kf.empty()) return limit;
            // Mth.binarySearch(0, n, i -> frameTime <= kf[i].time): first index whose time >= frameTime (n if none)
            int lo = 0, hi = int(kf.size());
            while (lo < hi) {
                int mid = (lo + hi) >> 1;
                if (anim.frameTime <= kf[mid].time) hi = mid;
                else lo = mid + 1;
            }
            int currentIndex = std::max(0, lo - 1);
            int nextIndex    = std::min(int(kf.size()) - 1, currentIndex + 1);
            Keyframe& cur    = kf[currentIndex];
            Keyframe& next   = kf[nextIndex];
            float delta;
            if (&cur == &next) delta = 0;
            else {
                float diff = anim.frameTime - cur.time;
                delta      = std::min(std::max(diff / (next.time - cur.time), 0.0f), 1.0f);
            }
            Interpolation it = next.interpolation == Interpolation::Bezier ? Interpolation::Bezier : cur.interpolation;
            Vec3 t = interpolate(it, kf, currentIndex, nextIndex, anim.blend, delta, channel.type, scripts);
            switch (channel.type) {
            case TransformType::Position: part->animPosition(t, merge); break;
            case TransformType::Rotation:
            case TransformType::GlobalRot: part->animRotation(t, merge); break;
            case TransformType::Scale: part->animScaleSet(t, merge); break;
            }
            switch (channel.type) {
            case TransformType::Rotation:
            case TransformType::GlobalRot:
                if (anim.overrideRot()) part->animationOverride |= 1;
                else if (!merge) part->animationOverride &= 6;
                break;
            case TransformType::Position:
                if (anim.overridePos()) part->animationOverride |= 2;
                else if (!merge) part->animationOverride &= 5;
                break;
            case TransformType::Scale:
                if (anim.overrideScale()) part->animationOverride |= 4;
                else if (!merge) part->animationOverride &= 3;
                break;
            }
            limit--;
        }
    }
    return limit;
}

void animationPlayerClear(Animation& anim) {
    for (auto& [part, channels] : anim.animationParts) {
        if (!part->animated) continue;
        part->animPosition({}, false);
        part->animRotation({}, false);
        part->animScaleSet({1, 1, 1}, false);
        part->lastAnimationPriority = INT32_MIN;
        part->animated              = false;
        part->animationOverride     = 0;
    }
}

} // namespace bf::figura
