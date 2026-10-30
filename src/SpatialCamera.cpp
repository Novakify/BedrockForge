// Renderer Foundation - Spatial GUI third-person camera (see SpatialCamera.h). Original code for Renderer Foundation;
// the camera math follows the Java mod Spatial GUI (tastytrash) CameraMixin with its default config.
//
// Bedrock side (v0.13b): the renderer takes the camera from the camera entity's MinecraftCamera::CameraComponent
// (position + orientation quaternion), which LevelRendererCamera::preRenderUpdate turns into ViewRenderData /
// mce::Camera. Right before preRenderUpdate we write the Java pose into that component. The quaternion convention is
// learned from the game's own first-person camera (orientation = the player's yaw/pitch) while no screen is open.
// The world camera's projection (FOV) is read in mce::Camera::updateViewMatrixDependencies. While a container screen
// is open the game is put in third person (so the player model renders) and restored afterwards.
#include "SpatialCamera.h"
#include "Cinematic.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>

#include <nlohmann/json.hpp>

#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/GuiData.h"
#include "mc/client/renderer/game/LevelRendererCamera.h"
#include "mc/deps/ecs/WeakEntityRef.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/ecs/gamerefs_entity/StackResultStorageEntity.h"
#include "mc/deps/minecraft_camera/CameraRegistry.h"
#include "mc/deps/minecraft_camera/components/ActiveCameraComponent.h"
#include "mc/deps/minecraft_camera/components/CameraComponent.h"
#include "mc/client/options/IOptionRegistry.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/math/Matrix.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/deps/renderer/MatrixStack.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorSwingSource.h"
#include "mc/world/item/HandSlot.h"
#include "mc/entity/components/ActorHeadRotationComponent.h"
#include "mc/entity/components/ActorRotationComponent.h"
#include "mc/entity/components/MobBodyRotationComponent.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/CachedComponentData.h"

namespace rf::panel {
bool grabCursorDelta(float& dx, float& dy, bool reset); // SpatialPanel.cpp: crosshair mode mouse grab
bool cursorNdc(float& x, float& y); // SpatialPanel.cpp: real cursor in [-1, 1] over the client area (y down)
}

namespace rf::spatialcam {
namespace {
using Clock = std::chrono::steady_clock;
constexpr float kDeg = 0.017453292f;

std::atomic_bool  gActive{false};
std::atomic_bool  gFirstPerson{false};
float             gBaseXRot = 0.0f;
bool              gInstalled = false;
int               gSavedPerspective = -1;
int               gSavedHideHand    = -1;
Clock::time_point gOpen{};

std::mutex gMutex; // gView + diag
View       gView;

bool      gHaveStart = false;
bool      gHaveStartFresh = false;
// CameraMixin closing branch: after a third person screen closes the camera glides back to the player's own view
// (the game stays in third person until the glide ends, then the perspective is restored on the client thread)
std::atomic_bool  gClosing{false}, gCloseDone{false};
Clock::time_point gCloseStart{};
glm::vec3         gCloseOffset{0.0f}; // camera position minus feet when the screen closed
float             gCloseYaw = 0.0f, gClosePitch = 0.0f;
int               gCloseToPersp = 0;  // the perspective the glide ends in (0 = first person)
glm::vec3         gLastCamPos{0.0f};
bool              gResumeFromLast = false; // the screen just opened: parallax smoothing starts at the pointer
glm::vec3 gStartPos{0.0f};
float     gStartYaw = 0.0f, gStartPitch = 0.0f;

struct CamSeen {
    void const* cam;
    float       pos[3];
    float       dist, p11;
    bool        chosen;
};
CamSeen gSeen[8]{};
int     gSeenN = 0;
long    gCalls = 0, gPersp = 0, gChosen = 0, gOverrides = 0;
float   gLastFeet[3]{}, gLastRot[2]{}, gLastCam[3]{}, gLastTarget[3]{};
float   gLastT = 0.0f;
struct EntSeen { int kind; bool hasComp, active; float pos[3]; };
EntSeen gEnt[24]{};
int     gEntN = 0;
char    gNote[160] = "hook not installed";

// AvatarRendererMixin: the player model turns its head (and body) toward the floating screen
struct Avatar {
    bool  saved = false, smoothInit = false;
    float headYaw = 0, headYawO = 0, bodyYaw = 0, bodyYawO = 0, pitch = 0, pitchO = 0; // originals, restored on close
    float sYaw = 0, sPitch = 0;
    int   restoreFrames = 0;
};
Avatar gAvatar;
float  gFreeYaw = 0.0f, gFreePitch = 0.0f;        // CameraMixin.freeLookYaw / freeLookPitch (crosshair mode)
bool   gGrabReset = true;
float  gLastCamYaw = 0.0f, gLastCamPitch = 0.0f;  // last applied camera angle (keepFirstPersonCameraAngle)
std::atomic_bool gPendingSnap{false};
std::atomic_bool gWide{false};
float            gWideT = 0.0f; // eased 0..1
Clock::time_point gWideLast{};
std::atomic_bool gForcedFp{false}; // CameraUtil.checkBlockCollision switched this screen to first person
long  gAvatarWrites = 0;
float gReadHead = 0, gReadBody = 0, gReadPitch = 0;

float wrapDeg(float a) {
    a = std::fmod(a, 360.0f);
    if (a >= 180.0f) a -= 360.0f;
    if (a < -180.0f) a += 360.0f;
    return a;
}
float rotLerp(float t, float a, float b) { return a + t * wrapDeg(b - a); } // Mth.rotLerp

void avatarApply(Actor& actor, float yawP, float mx, float my, bool on) {
    auto& ec   = actor.getEntityContext();
    auto  head = ec.tryGetComponent<ActorHeadRotationComponent>();
    auto  body = ec.tryGetComponent<MobBodyRotationComponent>();
    auto  rot  = ec.tryGetComponent<ActorRotationComponent>();
    if (!head || !body || !rot) return;
    gReadHead = head->mYHeadRot; gReadBody = body->mYBodyRot; gReadPitch = (*rot->mRot).x; // before our write: what the game set this frame
    if (!on) {
        if (gAvatar.saved) {
            head->mYHeadRot = gAvatar.headYaw; head->mYHeadRotO = gAvatar.headYawO;
            body->mYBodyRot = gAvatar.bodyYaw; body->mYBodyRotO = gAvatar.bodyYawO;
            gAvatar.saved   = false;
            gAvatar.restoreFrames = 0;
        }
        if (gAvatar.restoreFrames > 0) { // the game re-derives the look pitch for a few frames: keep putting it back
            --gAvatar.restoreFrames;
            (*rot->mRot).x = gAvatar.pitch; (*rot->mRotPrev).x = gAvatar.pitch;
        }
        gAvatar.smoothInit = false;
        return;
    }
    if (!gAvatar.saved) {
        gAvatar.saved   = true;
        gAvatar.headYaw = head->mYHeadRot; gAvatar.headYawO = head->mYHeadRotO;
        gAvatar.bodyYaw = body->mYBodyRot; gAvatar.bodyYawO = body->mYBodyRotO;
        gAvatar.pitch   = (*rot->mRot).x;    gAvatar.pitchO   = (*rot->mRotPrev).x;
    }
    Config const& c   = cfg();
    float         sgn = c.mirrorThirdPerson ? -1.0f : 1.0f;
    float bodyRot = yawP + c.avatarBodyRot * sgn;
    if (c.mirrorHeadMovement) mx = -mx;
    float yaw     = mx * float(c.avatarMaxYaw) + c.avatarBaseYaw * sgn;
    float pitch   = std::clamp(my * float(c.avatarMaxPitch), -60.0f, 60.0f);
    if (!gAvatar.smoothInit) { gAvatar.smoothInit = true; gAvatar.sYaw = yaw; gAvatar.sPitch = pitch; }
    else {
        gAvatar.sYaw   = rotLerp(0.15f, gAvatar.sYaw, yaw);
        gAvatar.sPitch = gAvatar.sPitch + 0.15f * (pitch - gAvatar.sPitch);
    }
    // Java render state: bodyRot absolute, yRot = head yaw relative to the body, xRot = head pitch
    body->mYBodyRot = bodyRot; body->mYBodyRotO = bodyRot;
    head->mYHeadRot = bodyRot + gAvatar.sYaw; head->mYHeadRotO = head->mYHeadRot;
    // head pitch (Java xRot) is NOT written: on Bedrock the actor pitch is the look direction and the first person
    // camera adopts it, so the player would keep looking where the avatar looked after the screen closes.
    ++gAvatarWrites;
}

// CameraUtil.checkBlockCollision: is the third person camera spot inside a block, or a block between the eyes and it?
bool tpSpotBlocked(Actor& actor) {
    // same spot the glide goes to (FOV-adjusted like applyComponent; Java uses the raw config values, which miss the
    // real camera position at wide FOVs)
    Config const& c    = cfg();
    Vec3          f    = actor.getFeetPos();
    float         r    = actor.getRotation().y * kDeg;
    float         fovY = gView.valid ? gView.fovYDeg : c.baselineFov;
    float         inv  = (c.autoScaleByFov ? fovY / c.baselineFov : 1.0f);
    inv                = 1.0f / inv - 1.0f;
    float d    = c.autoScaleByFov ? std::clamp(c.camDistance * (1.0f + inv * c.distMult), -4.0f, 4.0f) : c.camDistance;
    float side = c.autoScaleByFov ? std::clamp(c.camSide * (1.0f + inv * c.sideMult), -4.0f, 4.0f) : c.camSide;
    if (gWide.load()) side += (side < 0.0f ? -1.0f : 1.0f) * c.wideScreenShift;
    if (c.mirrorThirdPerson) side = -side;
    glm::vec3 a(f.x, f.y + 1.62f, f.z);
    glm::vec3 b(f.x + std::sin(r) * d + std::cos(r) * side, f.y + c.camHeight, f.z - std::cos(r) * d + std::sin(r) * side);
    auto& bs = actor.getDimensionBlockSource();
    for (int i = 1; i <= 24; ++i) {
        glm::vec3 p = a + (b - a) * (float(i) / 24.0f);
        BlockPos  bp(int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z)));
        Block const& blk = bs.getBlock(bp);
        if (!blk.isAir() && blk.mCachedComponentData->mIsSolid) return true;
    }
    return false;
}

// CameraUtil.adjustCameraPositionForCollision: eight rays from around the eyes (+-0.2) to around the camera spot;
// the camera comes in to the nearest block they hit
glm::vec3 collideCamera(Actor& actor, glm::vec3 const& eye, glm::vec3 const& target) {
    glm::vec3 delta = target - eye;
    float     maxD  = glm::length(delta);
    if (maxD < 0.001f) return target;
    auto& bs   = actor.getDimensionBlockSource();
    float best = maxD;
    int   steps = std::max(8, int(maxD / 0.05f));
    for (int k = 0; k < 8; ++k) {
        glm::vec3 off(((k & 1) * 2 - 1) * 0.2f, ((k >> 1 & 1) * 2 - 1) * 0.2f, ((k >> 2 & 1) * 2 - 1) * 0.2f);
        for (int i = 1; i <= steps; ++i) {
            float     t = maxD * float(i) / float(steps);
            if (t >= best) break;
            glm::vec3 p = eye + off + delta * (t / maxD);
            BlockPos  bp(int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z)));
            Block const& blk = bs.getBlock(bp);
            if (!blk.isAir() && blk.mCachedComponentData->mIsSolid) { best = t; break; }
        }
    }
    return eye + delta * (best / maxD);
}

float easeInOutSine(float t) { return float(-(std::cos(3.14159265358979 * double(t)) - 1.0) / 2.0); }

// camera-to-world rotation for a Minecraft yaw/pitch (degrees; yaw 0 = +Z, + turns right; pitch + = down)
glm::mat3 orient(float yawDeg, float pitchDeg) {
    float     y = yawDeg * kDeg, p = pitchDeg * kDeg;
    glm::vec3 f(-std::sin(y) * std::cos(p), -std::sin(p), std::cos(y) * std::cos(p));
    glm::vec3 r(-std::cos(y), 0.0f, -std::sin(y));
    glm::vec3 u = glm::cross(r, f);
    return glm::mat3(r, u, f);
}

void noteSeen(void const* cam, glm::vec3 const& pos, float dist, float p11, bool chosen) {
    for (int i = 0; i < gSeenN; ++i)
        if (gSeen[i].cam == cam) {
            gSeen[i] = {cam, {pos.x, pos.y, pos.z}, dist, p11, chosen};
            return;
        }
    if (gSeenN < 8) gSeen[gSeenN++] = {cam, {pos.x, pos.y, pos.z}, dist, p11, chosen};
}

// mce::Camera, before its derived matrices are built: while a Spatial GUI panel is up the world camera's projection is
// narrowed to the Java FOV (writing the camera component's FOV has no effect in third person)
float gFovBlendR = 0.0f; // render side copy of gFovBlend
long  gProjWrites = 0;
float gProjFrom = 0.0f, gProjTo = 0.0f;
void preCamera(mce::Camera& cam) {
    if (gFovBlendR <= 0.0f || !cfg().overrideFov) return;
    glm::mat4& P = const_cast<glm::mat4&>(static_cast<glm::mat4 const&>(*cam.projectionMatrixStack->top()._m));
    bool persp = std::fabs(P[3][3]) < 1e-4f && std::fabs(P[2][3]) > 0.5f && P[1][1] > 0.05f;
    if (!persp) return;
    auto ci = ll::service::getClientInstance();
    if (!ci) return;
    auto* lp = ci->getLocalPlayer();
    if (!lp) return;
    Vec3      feetV = static_cast<Actor&>(*lp).getFeetPos();
    glm::vec3 eye(feetV.x, feetV.y + 1.62f, feetV.z);
    glm::vec3 pos = *cam.mPosition;
    if (glm::length(pos - eye) >= 8.0f) return; // the world camera only
    float tanNow  = 1.0f / P[1][1];
    float fovNow  = 2.0f * std::atan(tanNow);
    float target  = float(std::clamp(cfg().targetFov, 30, 110)) * kDeg;
    float e       = easeInOutSine(gFovBlendR);
    float want    = fovNow + (target - fovNow) * e;
    float k       = tanNow / std::tan(want * 0.5f);
    if (std::fabs(k - 1.0f) < 1e-4f) return;
    P[0][0] *= k;
    P[1][1] *= k;
    ++gProjWrites;
    gProjFrom = fovNow / kDeg; gProjTo = want / kDeg;
}

// mce::Camera: read the world camera's projection (FOV/aspect) for the panel. Never modifies the camera.
void onCamera(mce::Camera& cam) {
    std::lock_guard l(gMutex);
    ++gCalls;
    glm::mat4 const& P = *cam.projectionMatrixStack->top()._m;
    bool persp = std::fabs(P[3][3]) < 1e-4f && std::fabs(P[2][3]) > 0.5f && P[1][1] > 0.05f;
    if (!persp) return;
    ++gPersp;
    auto ci = ll::service::getClientInstance();
    if (!ci) return;
    auto* lp = ci->getLocalPlayer();
    if (!lp) return;
    Actor&    actor = *lp;
    Vec3      feetV = actor.getFeetPos();
    glm::vec3 eye(feetV.x, feetV.y + 1.62f, feetV.z);
    glm::vec3 pos   = *cam.mPosition;
    float     dist  = glm::length(pos - eye);
    bool      world = dist < 8.0f;
    noteSeen(&cam, pos, dist, P[1][1], world);
    if (!world) return;
    ++gChosen;
    gView.valid   = true;
    gView.tanHalf = 1.0f / P[1][1];
    gView.aspect  = P[1][1] / P[0][0];
    gView.fovYDeg = 2.0f * std::atan(gView.tanHalf) / kDeg;
    gView.guiScale = ci->getGuiData()->mGuiScale;
    rf::cine::noteProjection(gView.tanHalf, gView.aspect, gView.fovYDeg);
    if (!gActive.load()) {
        gView.yawRel     = 0.0f;
        gView.pitch      = actor.getRotation().x;
        gView.overridden = false;
        gView.havePose   = false;
    }
}

// quaternion convention: game camera rotation = O(yaw, pitch) * C (local->world) or its transpose (world->local)
int       gConv = 0; // 0 unknown, 1 local->world, 2 world->local
glm::mat3 gC(1.0f);
long      gConvTries = 0, gCompWrites = 0, gCompMiss = 0;
float     gLastM[9]{};

bool signedPerm(glm::mat3 const& m) {
    for (int r = 0; r < 3; ++r) {
        int big = 0;
        for (int c = 0; c < 3; ++c) {
            float v = std::fabs(m[c][r]);
            if (v > 0.97f) ++big;
            else if (v > 0.06f) return false;
        }
        if (big != 1) return false;
    }
    return true;
}

void learnConvention(glm::quat const& q, float yawP, float pitchP) {
    ++gConvTries;
    glm::mat3 O0 = orient(yawP, pitchP);
    glm::mat3 Rq = glm::mat3_cast(q);
    glm::mat3 M1 = glm::transpose(O0) * Rq;
    for (int i = 0; i < 9; ++i) gLastM[i] = M1[i / 3][i % 3];
    if (signedPerm(M1)) { gConv = 1; gC = M1; return; }
    glm::mat3 M2 = glm::transpose(O0) * glm::transpose(Rq);
    if (signedPerm(M2)) { gConv = 2; gC = M2; return; }
}

// Cinematic Interactions scene camera (Cinematic.cpp: CameraRig), written the same way as the Spatial GUI pose.
struct FovMem { void const* comp = nullptr; float base = 0, written = 0; };
FovMem gFov[24];
int    gFovN = 0;
long   gCineWrites = 0;
// The world is drawn relative to LevelRendererCamera::mCameraPos (the mce::Camera sits at 0,0,0): in first person that
// origin follows the player's eyes, not the camera component, so the scene's position goes there too (pose of the frame)
bool   gCineHavePos = false;
glm::vec3 gCinePos{};
float  gRcBefore[3] = {}, gRcAfter[3] = {}, gRcTarget[3] = {};
long   gRcWrites = 0;
float  gCineFovRaw = 0, gCineFovScale = 1;
// Spatial GUI FOV (Java overrideFov / targetFov): 0..1 how far the panel scene has eased to the target FOV
float             gFovBlend = 0.0f;
Clock::time_point gFovLast{};
void stepFovBlend(bool on) {
    auto  nowT = Clock::now();
    float dtMs = gFovLast.time_since_epoch().count() ? std::min(100.0f, std::chrono::duration<float, std::milli>(nowT - gFovLast).count()) : 0.0f;
    gFovLast   = nowT;
    float step = dtMs / float(std::max(1, cfg().transitionMs));
    gFovBlend  = on && cfg().overrideFov ? std::min(1.0f, gFovBlend + step) : std::max(0.0f, gFovBlend - step);
    gFovBlendR = gFovBlend;
}
// writes the camera components' FOV: the Cinematic scene's scale, or the eased Spatial GUI target FOV
void applyFov(MinecraftCamera::CameraComponent** comps, int nComp, bool cineOn, float fovScale) {
    float target = float(std::clamp(cfg().targetFov, 30, 110)) * kDeg;
    float e      = easeInOutSine(gFovBlend);
    for (int i = 0; i < nComp; ++i) {
        float& f = comps[i]->mFieldOfView;
        FovMem* m = nullptr;
        for (int k = 0; k < gFovN; ++k) if (gFov[k].comp == comps[i]) m = &gFov[k];
        if (!m && gFovN < 24) { m = &gFov[gFovN++]; m->comp = comps[i]; m->base = f; m->written = f; }
        if (!m) continue;
        if (f != m->written) m->base = f; // the game set a new value since our last write
        float want = cineOn ? m->base * fovScale : m->base + (target - m->base) * e;
        if (f != want) f = want;
        m->written = f;
        if (i == 0) { gCineFovRaw = m->base; gCineFovScale = fovScale; }
    }
}
void applyCinematic(MinecraftCamera::CameraComponent** comps, int nComp, glm::vec3 const& eye, float yawP, float pitchP) {
    float           e[3] = {eye.x, eye.y, eye.z};
    rf::cine::Pose  p{};
    float           fovScale = 1.0f;
    bool            on       = rf::cine::cameraPose(e, yawP, pitchP, p, fovScale);
    applyFov(comps, nComp, on, fovScale);
    if (!on) gCineHavePos = false;
    if (!on || !gConv) return;
    glm::mat3 O = orient(p.yaw, p.pitch);
    float     r = p.roll * kDeg, c = std::cos(r), s = std::sin(r);
    glm::mat3 Rz(glm::vec3(c, s, 0.0f), glm::vec3(-s, c, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat3 R = O * Rz * gC;
    if (gConv == 2) R = glm::transpose(R);
    glm::quat qn = glm::quat_cast(R);
    glm::vec3 pos(p.pos[0], p.pos[1], p.pos[2]);
    for (int i = 0; i < nComp; ++i) {
        *comps[i]->mOrientation = qn;
        *comps[i]->mPosition    = pos;
    }
    gCinePos     = pos;
    gCineHavePos = true;
    ++gCineWrites;
}

// called right before LevelRendererCamera::preRenderUpdate (main/render-prep thread)
void applyComponent() {
    auto ci = ll::service::getClientInstance();
    if (!ci) return;
    auto* lp = ci->getLocalPlayer();
    if (!lp) return;
    Actor& actor = *lp;
    // camera entities: CameraRegistry::mGameCamera (what the renderer follows) + the preset cameras
    auto reg = ci->getCameraRegistry();
    if (!reg) { ++gCompMiss; return; }
    MinecraftCamera::CameraComponent* comps[24]{};
    int nComp = 0;
    {
        std::lock_guard dl(gMutex);
        gEntN = 0;
        auto consider = [&](OwnerPtr<EntityId>& o, int kind) {
            if (!o.has_value()) return;
            auto cc     = o->tryGetComponent<MinecraftCamera::CameraComponent>();
            bool active = o->hasComponent<MinecraftCamera::ActiveCameraComponent>();
            if (gEntN < 24) {
                auto& e  = gEnt[gEntN++];
                e.kind   = kind;
                e.hasComp = bool(cc);
                e.active = active;
                if (cc) { glm::vec3 p = *cc->mPosition; e.pos[0] = p.x; e.pos[1] = p.y; e.pos[2] = p.z; }
            }
            if (cc && (kind == 0 || active) && nComp < 24) comps[nComp++] = &*cc;
        };
        consider(*reg->mGameCamera, 0);
        for (auto& o : *reg->mCameraEntities) consider(o, 1);
    }
    if (!nComp) { ++gCompMiss; return; }
    MinecraftCamera::CameraComponent* cc = comps[0];
    std::lock_guard l(gMutex);
    Vec3      feetV = actor.getFeetPos();
    Vec2      rot   = actor.getRotation(); // x = pitch, y = yaw (degrees)
    glm::vec3 feet(feetV.x, feetV.y, feetV.z);
    glm::vec3 eye  = feet + glm::vec3(0.0f, 1.62f, 0.0f);
    glm::quat& q   = *cc->mOrientation;
    glm::vec3& pos = *cc->mPosition;
    gLastFeet[0] = feet.x; gLastFeet[1] = feet.y; gLastFeet[2] = feet.z;
    gLastRot[0]  = rot.x;  gLastRot[1]  = rot.y;
    float yawP = rot.y, pitchP = rot.x;
    stepFovBlend(gActive.load());
    if (!gActive.load() && gClosing.load() && gConv) {
        Config const& c  = cfg();
        float  dur = float(std::max(1, c.transitionMs)) * (1.0f - std::clamp(c.transitionSkipPct, 0, 99) / 100.0f);
        float  t   = std::min(1.0f, std::chrono::duration<float, std::milli>(Clock::now() - gCloseStart).count() / dur);
        float  e   = easeInOutSine(t);
        glm::vec3 from = feet + gCloseOffset;
        glm::vec3 to   = gCloseToPersp == 0 ? eye : pos; // first person: the eyes; otherwise the game's own camera
        glm::quat qFrom = glm::quat_cast(gConv == 2 ? glm::transpose(orient(gCloseYaw, gClosePitch) * gC) : orient(gCloseYaw, gClosePitch) * gC);
        glm::quat qTo   = gCloseToPersp == 0 ? glm::quat_cast(gConv == 2 ? glm::transpose(orient(yawP, pitchP) * gC) : orient(yawP, pitchP) * gC) : q;
        glm::quat qn    = glm::slerp(qFrom, qTo, e);
        glm::vec3 cp    = from + (to - from) * e;
        for (int i = 0; i < nComp; ++i) {
            *comps[i]->mOrientation = qn;
            *comps[i]->mPosition    = cp;
        }
        gLastCamPos = cp;
        applyFov(comps, nComp, false, 1.0f); // the FOV eases back with the glide
        avatarApply(actor, yawP, 0, 0, t < 1.0f); // AvatarRendererMixin: the pose blends back as the camera returns
        if (t >= 1.0f) gCloseDone = true;          // client thread restores the perspective (clientTick)
        gView.havePose = false;
        return;
    }
    if (!gActive.load()) {
        avatarApply(actor, yawP, 0, 0, false);
        if (gPendingSnap.exchange(false)) { // SpatialGUIRenderer.applyCameraSnap: keep looking where the camera looked
            if (auto rc = actor.getEntityContext().tryGetComponent<ActorRotationComponent>()) {
                (*rc->mRot).x = gLastCamPitch; (*rc->mRot).y = gLastCamYaw;
                (*rc->mRotPrev).x = gLastCamPitch; (*rc->mRotPrev).y = gLastCamYaw;
            }
            if (auto hr = actor.getEntityContext().tryGetComponent<ActorHeadRotationComponent>()) { hr->mYHeadRot = gLastCamYaw; hr->mYHeadRotO = gLastCamYaw; }
            gHaveStart = false;
            return;
        }
        // first person: the camera looks exactly where the player looks -> learn the quaternion convention
        if (glm::length(pos - eye) < 0.3f) learnConvention(q, yawP, pitchP);
        gHaveStart = false;
        applyCinematic(comps, nComp, eye, yawP, pitchP);
        return;
    }
    applyFov(comps, nComp, false, 1.0f);
    if (!gConv) return;
    if (!gHaveStart) {
        gHaveStart  = true;
        gStartPos   = gResumeFromLast ? gLastCamPos : eye; // Java: the camera position when the screen opened (first person eyes)
        gResumeFromLast = false;
        gStartYaw   = yawP;
        gStartPitch = pitchP;
        float clampP = float(cfg().fpPitchClamp);
        gBaseXRot   = std::clamp(pitchP, -clampP, clampP); // CameraMixin: baseXRot
        gFreeYaw = gFreePitch = 0.0f;
        gGrabReset = true;
        gWideT     = gWide.load() ? 1.0f : 0.0f; // opened wide: the glide goes straight to the shifted spot
        gHaveStartFresh = true;
    }
    Config const& c  = cfg();
    bool          fp = gFirstPerson.load();
    float fovY = gView.valid ? gView.fovYDeg : c.baselineFov;
    float fm   = c.autoScaleByFov ? fovY / c.baselineFov : 1.0f; // CameraUtil.calculateFovMultiplier
    float inv  = 1.0f / fm - 1.0f;
    float mx = 0.0f, my = 0.0f; // parallax: mouse position in [-1, 1]
    rf::panel::cursorNdc(mx, my);
    mx = std::clamp(mx, -1.0f, 1.0f);
    my = std::clamp(my, -1.0f, 1.0f);
    {   // CameraMixin.smoothParallaxOffsets: the camera follows the pointer with a 50 ms time constant
        static float             sx = 0.0f, sy = 0.0f;
        static Clock::time_point last{};
        auto  nowT  = Clock::now();
        float dt    = gHaveStartFresh || !last.time_since_epoch().count() ? -1.0f : std::min(0.1f, std::chrono::duration<float>(nowT - last).count());
        last        = nowT;
        if (dt < 0.0f) { sx = mx; sy = my; }
        else {
            float a = 1.0f - std::exp(-dt / 0.05f);
            sx += (mx - sx) * a;
            sy += (my - sy) * a;
        }
        gHaveStartFresh = false;
        mx = sx; my = sy;
    }
    float     elapsed = std::chrono::duration<float, std::milli>(Clock::now() - gOpen).count();
    glm::vec3 target;
    float     targetYaw, targetPitch;
    bool crosshair = fp && c.crosshairFp;
    if (crosshair) {
        // crosshair mode: grabbed mouse = free look (yaw +-90 around the open yaw, pitch within +-90 absolute)
        float dx = 0.0f, dy = 0.0f;
        if (rf::panel::grabCursorDelta(dx, dy, gGrabReset)) gGrabReset = false;
        const float sens = 0.15f; // CameraUtil.calculateMouseSensitivity at the default sensitivity, degrees per pixel
        gFreeYaw   = gFreeYaw + dx * sens;
        if (c.lockFpYaw) gFreeYaw = std::clamp(gFreeYaw, -90.0f, 90.0f);
        gFreePitch = std::clamp(gFreePitch + dy * sens, -90.0f - pitchP, 90.0f - pitchP);
        targetYaw   = yawP + gFreeYaw;
        targetPitch = pitchP + gFreePitch;
        target      = eye;
    } else if (fp) {
        // CameraMixin.calculateCameraTransform(firstPerson): camera stays at the eyes and turns with the mouse
        float blend = std::min(1.0f, elapsed / 150.0f);
        float yawOff = c.fpParallaxOff ? 0.0f : mx * 90.0f * c.fpSensYaw * blend;
        targetPitch  = c.fpParallaxOff ? gBaseXRot : gBaseXRot + my * 180.0f * c.fpSensPitch * blend;
        targetPitch  = std::clamp(targetPitch, -90.0f, 90.0f);
        targetYaw    = yawP + yawOff;
        target       = eye;
    } else {
        // CameraUtil.calculateAutoFovDistance / calculateAutoFovSideOffset
        float d    = c.autoScaleByFov ? std::clamp(c.camDistance * (1.0f + inv * c.distMult), -4.0f, 4.0f) : c.camDistance;
        float side = c.autoScaleByFov ? std::clamp(c.camSide * (1.0f + inv * c.sideMult), -4.0f, 4.0f) : c.camSide;
        {   // ease the extra sideways slide in/out (same ease as the camera glide)
            auto  nowT = Clock::now();
            float dtMs = gWideLast.time_since_epoch().count() ? std::chrono::duration<float, std::milli>(nowT - gWideLast).count() : 0.0f;
            gWideLast  = nowT;
            float step = dtMs / float(std::max(1, c.transitionMs));
            gWideT     = gWide.load() ? std::min(1.0f, gWideT + step) : std::max(0.0f, gWideT - step);
            float sgn  = side < 0.0f ? -1.0f : 1.0f;
            side += sgn * c.wideScreenShift * easeInOutSine(gWideT);
        }
        if (c.mirrorThirdPerson) side = -side;
        float h     = std::clamp(c.camHeight, -4.0f, 4.0f);
        targetYaw   = yawP + (c.tpParallaxOff ? 0.0f : mx * 90.0f * c.tpSensYaw);
        targetPitch = c.camPitch + (c.tpParallaxOff ? 0.0f : my * 180.0f * c.tpSensPitch);
        float r     = yawP * kDeg;
        target      = feet + glm::vec3(std::sin(r) * d + std::cos(r) * side, h, -std::cos(r) * d + std::sin(r) * side);
        if (actor.isGliding()) { // elytraFlyingSideOffset / elytraFlyingHeightOffset
            float es = c.mirrorThirdPerson ? -c.elytraSide : c.elytraSide;
            target += glm::vec3(std::cos(r) * es, c.elytraHeight, std::sin(r) * es);
        }
        target = collideCamera(actor, eye, target); // CameraUtil.adjustCameraPositionForCollision
    }
    float     dur      = float(std::max(1, c.transitionMs));
    float     t        = std::min(1.0f, (elapsed + c.transitionSkipPct / 100.0f * dur) / dur);
    float     e        = easeInOutSine(t);
    glm::vec3 camPos   = t >= 1.0f ? target : gStartPos + (target - gStartPos) * e;
    float     camYaw   = t >= 1.0f ? targetYaw : gStartYaw + (targetYaw - gStartYaw) * e;
    float     camPitch = t >= 1.0f || !c.lerpXRot ? targetPitch : gStartPitch + (targetPitch - gStartPitch) * e;

    glm::mat3 R = orient(camYaw, camPitch) * gC;
    if (gConv == 2) R = glm::transpose(R);
    glm::quat qn = glm::quat_cast(R);
    for (int i = 0; i < nComp; ++i) {
        *comps[i]->mOrientation = qn;
        *comps[i]->mPosition    = camPos;
    }
    gLastCamPos = camPos;
    ++gCompWrites;
    ++gOverrides;
    gLastCamYaw = camYaw; gLastCamPitch = camPitch;
    gLastCam[0] = camPos.x; gLastCam[1] = camPos.y; gLastCam[2] = camPos.z;
    gLastTarget[0] = target.x; gLastTarget[1] = target.y; gLastTarget[2] = target.z;
    gLastT = t;
    gView.yawRel      = camYaw - yawP;
    gView.pitch       = camPitch;
    gView.overridden  = true;
    gView.firstPerson = fp;
    gView.fpYaw0      = yawP;
    gView.fpPitch0    = gBaseXRot;
    gView.playerYaw   = yawP;
    gView.havePose    = !fp;
    {   // InventoryRenderer.computeLightingTint: 255 - strength * (1 - light), smoothed
        static float smooth = -1.0f;
        float        tgt    = 1.0f;
        if (c.lightTint) {
            float light = std::clamp(actor.getDimensionBlockSource().getBrightness(BlockPos(int(std::floor(eye.x)), int(std::floor(eye.y)), int(std::floor(eye.z)))), 0.0f, 1.0f);
            tgt         = std::clamp(1.0f - c.lightTintStrength / 255.0f * (1.0f - light), 0.0f, 1.0f);
        }
        smooth          = smooth < 0.0f ? tgt : smooth + (tgt - smooth) * 0.1f;
        gView.lightTint = smooth;
    }
    gView.camPos[0] = camPos.x; gView.camPos[1] = camPos.y; gView.camPos[2] = camPos.z;
    gView.camYaw = camYaw; gView.camPitch = camPitch;
    gView.feet[0] = feet.x; gView.feet[1] = feet.y; gView.feet[2] = feet.z;
    avatarApply(actor, yawP, mx, my, !fp);
}
} // namespace
} // namespace rf::spatialcam

namespace rf::spatialcam { void afterPreRender(Vec3* cameraPos, Vec3* targetPos); }

LL_TYPE_INSTANCE_HOOK(RfCameraUpdateHook, ll::memory::HookPriority::Normal, mce::Camera, &mce::Camera::updateViewMatrixDependencies,
                      void) {
    rf::spatialcam::preCamera(*this);
    origin();
    rf::spatialcam::onCamera(*this);
}

LL_TYPE_INSTANCE_HOOK(RfLevelCameraPreRenderHook, ll::memory::HookPriority::Normal, LevelRendererCamera,
                      &LevelRendererCamera::$preRenderUpdate, void, ScreenContext& screenContext,
                      LevelRenderPreRenderUpdateParameters& params) {
    rf::spatialcam::applyComponent();
    origin(screenContext, params);
    rf::spatialcam::afterPreRender(&*this->mCameraPos, &*this->mCameraTargetPos);
}

namespace rf::spatialcam {
/// after preRenderUpdate: the render origin (and the point it looks at) follow the scene camera
void afterPreRender(Vec3* cameraPos, Vec3* targetPos) {
    std::lock_guard l(gMutex);
    if (!cameraPos) return;
    gRcBefore[0] = cameraPos->x; gRcBefore[1] = cameraPos->y; gRcBefore[2] = cameraPos->z;
    if (targetPos) { gRcTarget[0] = targetPos->x; gRcTarget[1] = targetPos->y; gRcTarget[2] = targetPos->z; }
    if (!gCineHavePos || !rf::cine::active()) return;
    Vec3 d{gCinePos.x - cameraPos->x, gCinePos.y - cameraPos->y, gCinePos.z - cameraPos->z};
    cameraPos->x = gCinePos.x; cameraPos->y = gCinePos.y; cameraPos->z = gCinePos.z;
    if (targetPos) { targetPos->x += d.x; targetPos->y += d.y; targetPos->z += d.z; }
    gRcAfter[0] = cameraPos->x; gRcAfter[1] = cameraPos->y; gRcAfter[2] = cameraPos->z;
    ++gRcWrites;
}
namespace {
Config                gCfg;
std::filesystem::path gCfgFile;
} // namespace
Config& cfg() { return gCfg; }

float applyEasing(Easing e, float t) { // AnimationUtil
    t = std::clamp(t, 0.0f, 1.0f);
    switch (e) {
    case Easing::Back: { const float c1 = 1.70158f, c3 = c1 + 1.0f; float x = t - 1.0f; return 1.0f + c3 * x * x * x + c1 * x * x; }
    case Easing::Elastic: {
        if (t <= 0.0f || t >= 1.0f) return t;
        const float c4 = 2.0943951f;
        return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
    }
    case Easing::Bounce: {
        const float n1 = 7.5625f, d1 = 2.75f;
        if (t < 1.0f / d1) return n1 * t * t;
        if (t < 2.0f / d1) { t -= 1.5f / d1; return n1 * t * t + 0.75f; }
        if (t < 2.5f / d1) { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
        t -= 2.625f / d1;
        return n1 * t * t + 0.984375f;
    }
    case Easing::Expo: return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
    case Easing::Quad: return 1.0f - (1.0f - t) * (1.0f - t);
    case Easing::Quart: { float x = 1.0f - t; return 1.0f - x * x * x * x; }
    case Easing::Cubic:
    default: { float x = 1.0f - t; return 1.0f - x * x * x; }
    }
}

#define RF_SG_FIELDS(X)                                                                                                       \
    X(enabled) X(fpInventory) X(fpContainers) X(autoScaleByFov) X(mirrorThirdPerson) X(screenAlpha) X(screenDistance)       \
    X(screenSide) X(screenHeight) X(screenYaw) X(screenPitch) X(screenScale) X(fpDistance) X(fpSide) X(fpHeight) X(fpYaw)    \
    X(fpPitch) X(fpScale) X(camDistance) X(camSide) X(camHeight) X(camPitch) X(transitionMs) X(transitionSkipPct)           \
    X(tpSensYaw) X(tpSensPitch) X(tpParallaxOff) X(lerpXRot) X(fpSensYaw) X(fpSensPitch) X(fpParallaxOff) X(fpPitchClamp)   \
    X(fade) X(fadeMs) X(scaleAnim) X(openMs) X(startScalePct) X(baselineFov) X(screenPower) X(sideMult) X(distMult)         \
    X(tpScreenMult) X(autoGuiScale) X(guiScale) X(hideHotbar) X(allScreens) X(pauseScreen) X(mostContainers) X(books)     \
    X(crafting) X(furnaces) X(anvils) X(enchanting) X(beacons) X(brewing) X(villagerTrading) X(chests) X(shulkerBoxes)      \
    X(hoppers) X(dispensers) X(grindstone) X(smithing) X(cartography) X(loom) X(stonecutter) X(lectern) X(inventory)        \
    X(creativeInventory) X(renderScalePct) X(linearFiltering) X(anisotropicFiltering) X(recipeBookShrink) X(hideHandsFp)    \
    X(hideShieldFp) X(swingArmFp) X(curvedScreen) X(curvedArc) X(avatarBaseYaw) X(avatarBodyRot) X(crosshairFp)            \
    X(keepFpAngle) X(inheritOrigin) X(wideScreenShift) X(overrideFov) X(targetFov) X(worldBlur) X(worldBlurStrength) X(blurKeepPlayer)   \
    X(dropShadow) X(dropShadowAlpha) X(closeAnim) X(autoDetectCameraMode) X(fallbackFpOnBlock) X(avatarMaxYaw) X(avatarMaxPitch)   \
    X(mirrorHeadMovement) X(lockFpYaw) X(elytraSide) X(elytraHeight) X(lightTint) X(lightTintStrength)

void loadConfig(std::filesystem::path const& configDir) {
    gCfgFile = configDir / "SpatialGUI.json";
    std::ifstream in(gCfgFile);
    if (!in) { saveConfig(); return; }
    try {
        nlohmann::json j;
        in >> j;
#define X(f) if (j.contains(#f)) gCfg.f = j[#f].get<decltype(gCfg.f)>();
        RF_SG_FIELDS(X)
#undef X
        if (j.contains("easing")) gCfg.easing = Easing(std::clamp(j["easing"].get<int>(), 0, 6));
    } catch (...) {
    }
}
void saveConfig() {
    if (gCfgFile.empty()) return;
    nlohmann::json j;
#define X(f) j[#f] = gCfg.f;
    RF_SG_FIELDS(X)
#undef X
    j["easing"] = int(gCfg.easing);
    std::ofstream out(gCfgFile, std::ios::trunc);
    if (out) out << j.dump(4);
}
void resetConfig() { gCfg = Config{}; }

void install() {
    if (gInstalled) return;
    RfCameraUpdateHook::hook();
    RfLevelCameraPreRenderHook::hook();
    gInstalled = true;
    std::snprintf(gNote, sizeof gNote, "camera hooks installed (CameraComponent before preRenderUpdate)");
}
void uninstall() {
    if (!gInstalled) return;
    RfCameraUpdateHook::unhook();
    RfLevelCameraPreRenderHook::unhook();
    gInstalled = false;
}

void setActive(bool on, bool inventory) {
    auto ci = ll::service::getClientInstance();
    if (on && !gActive.load()) {
        if (!cfg().enabled) return;
        if (gClosing.exchange(false)) { // reopened while gliding back: the saved perspective is still the original one
            gCloseDone      = false;
            gResumeFromLast = true;
            if (ci && gSavedPerspective >= 0) ci->getOptions().setPlayerViewPerspective(gSavedPerspective);
            gSavedPerspective = -1;
        }
        bool fp = inventory ? cfg().fpInventory : cfg().fpContainers;
        if (cfg().autoDetectCameraMode && ci) fp = ci->getOptions().getPlayerViewPerspective() == 0; // SpatialGUIClient.shouldUseFirstPersonMode
        gForcedFp = false;
        if (!fp && ci && cfg().fallbackFpOnBlock) {
            if (auto* lp = ci->getLocalPlayer()) {
                if (tpSpotBlocked(*lp)) { fp = true; gForcedFp = true; } // Java: switchedToFirstPersonDueToBlock
            }
        }
        gFirstPerson = fp;
        gSavedPerspective = -1;
        gSavedHideHand    = -1;
        if (ci && fp && (cfg().hideHandsFp || cfg().hideShieldFp)) { // Java: hide hands / shield in first person
            auto& o        = ci->getOptions();
            gSavedHideHand = o.getHideItemInHand() ? 1 : 0;
            if (cfg().hideHandsFp) o.setHideItemInHand(true);
        }
        if (ci && !fp) {
            auto& o           = ci->getOptions();
            gSavedPerspective = o.getPlayerViewPerspective();
            if (gSavedPerspective == 0) o.setPlayerViewPerspective(1); // third person back: renders the player model
        }
        {
            std::lock_guard l(gMutex);
            gHaveStart = false;
            gOpen      = Clock::now();
        }
        gActive = true;
    } else if (!on && gActive.load()) {
        if (gFirstPerson.load() && cfg().keepFpAngle) gPendingSnap = true;
        if (!gFirstPerson.load() && gSavedPerspective >= 0 && cfg().closeAnim) {
            // third person: glide back first (applyComponent), the perspective comes back when it ends (clientTick)
            std::lock_guard l(gMutex);
            gCloseOffset  = gLastCamPos - glm::vec3(gLastFeet[0], gLastFeet[1], gLastFeet[2]);
            gCloseYaw     = gLastCamYaw;
            gClosePitch   = gLastCamPitch;
            gCloseToPersp = gSavedPerspective;
            gCloseStart   = Clock::now();
            gCloseDone    = false;
            gClosing      = true;
            gActive       = false;
            if (ci && gSavedHideHand >= 0) ci->getOptions().setHideItemInHand(gSavedHideHand == 1);
            gSavedHideHand = -1;
            return; // gSavedPerspective stays until the glide ends
        }
        gActive = false;
        if (ci && gSavedPerspective >= 0) ci->getOptions().setPlayerViewPerspective(gSavedPerspective);
        if (ci && gSavedHideHand >= 0) ci->getOptions().setHideItemInHand(gSavedHideHand == 1);
        gSavedPerspective = -1;
        gSavedHideHand    = -1;
    }
}
bool active() { return gActive.load(); }
std::atomic_bool gSwingReq{false};
void requestSwing() { if (gActive.load() && gFirstPerson.load() && cfg().swingArmFp) gSwingReq = true; }
void clientTick() {
    if (gSwingReq.exchange(false)) {
        if (auto ci = ll::service::getClientInstance())
            if (auto* lp = ci->getLocalPlayer()) static_cast<Actor&>(*lp).swing(ActorSwingSource::Interact, HandSlot::Mainhand);
    }
    if (!gCloseDone.exchange(false)) return;
    gClosing = false;
    auto ci  = ll::service::getClientInstance();
    if (ci && gSavedPerspective >= 0) ci->getOptions().setPlayerViewPerspective(gSavedPerspective);
    gSavedPerspective = -1;
}
void setWideScreen(bool wide) { gWide = wide; }

View view() {
    std::lock_guard l(gMutex);
    return gView;
}

void appendDiag(char* b, unsigned long long cap) {
    std::lock_guard l(gMutex);
    size_t len = std::strlen(b);
    len += std::snprintf(b + len, cap - len,
                         "camera (v0.15): %s; active %d; calls %ld, perspective %ld, world %ld, component writes %ld\n"
                         "  player feet %.2f,%.2f,%.2f rot pitch %.1f yaw %.1f; cam %.2f,%.2f,%.2f target %.2f,%.2f,%.2f t %.2f\n"
                         "  view: valid %d overridden %d fovY %.1f aspect %.3f yawRel %.1f pitch %.1f guiScale %.2f\n",
                         gNote, gActive.load() ? 1 : 0, gCalls, gPersp, gChosen, gOverrides, gLastFeet[0], gLastFeet[1], gLastFeet[2],
                         gLastRot[0], gLastRot[1], gLastCam[0], gLastCam[1], gLastCam[2], gLastTarget[0], gLastTarget[1],
                         gLastTarget[2], gLastT, gView.valid, gView.overridden, gView.fovYDeg, gView.aspect, gView.yawRel, gView.pitch, gView.guiScale);
    len += std::snprintf(b + len, cap - len, "  cinematic camera writes %ld, fov field %.4f x %.3f; spatial fov blend %.2f, projection writes %ld (%.1f -> %.1f)\n", gCineWrites, gCineFovRaw, gCineFovScale, gFovBlend, gProjWrites, gProjFrom, gProjTo);
    len += std::snprintf(b + len, cap - len, "  render origin (LevelRendererCamera::mCameraPos) %.2f,%.2f,%.2f -> %.2f,%.2f,%.2f (writes %ld), target %.2f,%.2f,%.2f\n",
                         gRcBefore[0], gRcBefore[1], gRcBefore[2], gRcAfter[0], gRcAfter[1], gRcAfter[2], gRcWrites, gRcTarget[0], gRcTarget[1], gRcTarget[2]);
    len += std::snprintf(b + len, cap - len, "  quaternion convention %d (tries %ld, component misses %ld); M %.2f %.2f %.2f / %.2f %.2f %.2f / %.2f %.2f %.2f\n",
                         gConv, gConvTries, gCompMiss, gLastM[0], gLastM[3], gLastM[6], gLastM[1], gLastM[4], gLastM[7], gLastM[2], gLastM[5], gLastM[8]);
    len += std::snprintf(b + len, cap - len, "  avatar writes %ld saved %d smoothed yaw %.1f pitch %.1f; orig head %.1f body %.1f pitch %.1f; last read head %.1f body %.1f pitch %.1f; first person %d (forced by block %d)\n",
                         gAvatarWrites, gAvatar.saved ? 1 : 0, gAvatar.sYaw, gAvatar.sPitch, gAvatar.headYaw, gAvatar.bodyYaw, gAvatar.pitch, gReadHead, gReadBody, gReadPitch, gFirstPerson.load() ? 1 : 0, gForcedFp.load() ? 1 : 0);
    for (int i = 0; i < gEntN && len < cap; ++i)
        len += std::snprintf(b + len, cap - len, "  entity %s comp %d active %d pos %.2f,%.2f,%.2f\n", gEnt[i].kind == 0 ? "GAME" : "preset", gEnt[i].hasComp,
                             gEnt[i].active, gEnt[i].pos[0], gEnt[i].pos[1], gEnt[i].pos[2]);
    for (int i = 0; i < gSeenN && len < cap; ++i)
        len += std::snprintf(b + len, cap - len, "  cam %p pos %.2f,%.2f,%.2f dist-to-eye %.2f p11 %.3f %s\n", gSeen[i].cam, gSeen[i].pos[0],
                             gSeen[i].pos[1], gSeen[i].pos[2], gSeen[i].dist, gSeen[i].p11, gSeen[i].chosen ? "WORLD" : "");
}
} // namespace rf::spatialcam
