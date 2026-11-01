// FiguraBridge animations - port of org.figuramc.figura.animation (Figura 0.1.6): Animation, AnimationPlayer,
// Keyframe (incl. Lua-expression keyframe values), Interpolation, TimeController.
#pragma once
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "figura/core/FbMath.h"

namespace bf::figura {

struct Part;
class Animation;

enum class TransformType : uint8_t { Position, Rotation, GlobalRot, Scale };
enum class Interpolation : uint8_t { Linear, CatmullRom, Bezier, Step };
enum class PlayState : uint8_t { Stopped, Paused, Playing, Holding };
enum class LoopMode : uint8_t { Loop, Once, Hold };

char const* playStateName(PlayState s);
char const* loopModeName(LoopMode m);
bool        loopModeFromName(std::string s, LoopMode& out); // case-insensitive (toUpperCase)

/// Runs Lua for keyframe expressions and instruction keyframes. Implemented by the Lua runtime.
class IAnimScripts {
public:
    virtual ~IAnimScripts() = default;
    /// Compile `src` as Figura Keyframe.compile does: number literal -> literal; else "return <src>", else <src>.
    /// Returns a handle (>0) or 0 when it can't compile yet / failed (error already reported).
    virtual int    compileKeyframe(std::string const& chunkName, std::string const& src, bool* isLiteral, double* literal) = 0;
    /// Run a compiled keyframe chunk with (animationsApi, delta, animation); returns the number (0 on error).
    virtual double evalKeyframe(int handle, float delta, Animation* anim, std::string const& chunkName) = 0;
    /// Compile an instruction-keyframe chunk ("animations.<mdl>.<name> instr keyframe (time=...)"); 0 = not yet/failed.
    virtual int    compileCode(std::string const& chunkName, std::string const& src) = 0;
    /// Run an instruction chunk with (animationsApi, animation).
    virtual void   runCode(int handle, Animation* anim) = 0;
    virtual bool   ready() const = 0;
};

/// Real-time clock in ms + game paused flag (TimeController uses Util.getMillis and Minecraft.isPaused()).
struct AnimClock {
    std::function<int64_t()> millis;
    std::function<bool()>    paused;
};

struct KeyframeValue {
    bool        compiled = false;
    bool        literal  = true;
    double      value    = 0;
    int         handle   = 0;
};

class Keyframe {
public:
    float         time = 0;
    Interpolation interpolation = Interpolation::Linear;
    std::optional<Vec3> targetA, targetB;             // literal data
    std::array<std::string, 3> aCode, bCode;          // expression data (when target is empty)
    Vec3 bezierLeft, bezierRight, bezierLeftTime{-0.1, -0.1, -0.1}, bezierRightTime{0.1, 0.1, 0.1};
    Part*         part = nullptr;
    TransformType channel = TransformType::Position;
    Animation*    animation = nullptr;

    Vec3 getTargetA(float delta, IAnimScripts* s);
    Vec3 getTargetB(float delta, IAnimScripts* s);

private:
    std::array<KeyframeValue, 3> aCache{}, bCache{};
    double evalCompile(std::array<KeyframeValue, 3>& cache, std::array<std::string, 3> const& code, int idx, float delta,
                       IAnimScripts* s);
    std::string chunkName(int idx) const;
};

struct AnimationChannel {
    TransformType         type;
    std::vector<Keyframe> keyframes;
};

class Animation {
public:
    Animation(std::string modelName, std::string name, LoopMode loop, bool override, float length, float offset,
              float blend, float startDelay, float loopDelay);

    std::string modelName, name;
    int         index = 0;          // position in the avatar's animation list (NBT order)
    PlayState   playState = PlayState::Stopped;
    float       time = 0, lastTime = 0, frameTime = 0;
    bool        inverted = false;
    float       length, blend, offset, speed = 1.0f, startDelay, loopDelay;
    int         override;
    int         priority = 0;
    LoopMode    loop;

    std::vector<std::pair<Part*, std::vector<AnimationChannel>>> animationParts;

    void addAnimation(Part* part, AnimationChannel ch);
    void tick(AnimClock const& clock, IAnimScripts* scripts);
    void playCode(float minTime, float maxTime, IAnimScripts* scripts);

    Animation& play(AnimClock const& c);
    Animation& pause(AnimClock const& c);
    Animation& stop();
    Animation& restart(AnimClock const& c) { stop(); play(c); return *this; }
    Animation& setPlaying(bool b, AnimClock const& c) { return b ? play(c) : stop(); }
    Animation& setTime(float t);
    Animation& setSpeed(std::optional<float> s);
    Animation& newCode(float t, std::string data);
    void       setOverride(bool b) { override = b ? 7 : 0; }
    bool       overrideRot() const { return (override & 1) == 1; }
    bool       overridePos() const { return (override & 2) == 2; }
    bool       overrideScale() const { return (override & 4) == 4; }

private:
    // TimeController
    int64_t ctlLastTime = 0, ctlTime = 0, ctlPauseTime = 0;
    void    ctlInit(AnimClock const& c);
    void    ctlTick(AnimClock const& c);
    void    ctlReset();
    void    ctlPause(AnimClock const& c);
    void    ctlResume(AnimClock const& c);
    float   ctlDiff(AnimClock const& c) const;

    std::map<float, int>         codeFrames;     // time -> compiled chunk handle
    std::map<float, std::string> codeFrameQueue; // not compiled yet
    void compileCode(IAnimScripts* s);
};

/// AnimationPlayer.tick / clear. Returns the remaining channel budget.
int  animationPlayerTick(Animation& anim, int limit, AnimClock const& clock, IAnimScripts* scripts);
void animationPlayerClear(Animation& anim);

} // namespace bf::figura
