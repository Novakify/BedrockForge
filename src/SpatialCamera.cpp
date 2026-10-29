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

namespace rf::panel {
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
Clock::time_point gOpen{};

std::mutex gMutex; // gView + diag
View       gView;

bool      gHaveStart = false;
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
    if (!gActive.load()) {
        gView.yawRel     = 0.0f;
        gView.pitch      = actor.getRotation().x;
        gView.overridden = false;
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
    if (!gActive.load()) {
        // first person: the camera looks exactly where the player looks -> learn the quaternion convention
        if (glm::length(pos - eye) < 0.3f) learnConvention(q, yawP, pitchP);
        gHaveStart = false;
        return;
    }
    if (!gConv) return;
    if (!gHaveStart) {
        gHaveStart  = true;
        gStartPos   = eye; // Java: the camera position when the screen opened (first person eyes)
        gStartYaw   = yawP;
        gStartPitch = pitchP;
        float clampP = float(cfg().fpPitchClamp);
        gBaseXRot   = std::clamp(pitchP, -clampP, clampP); // CameraMixin: baseXRot
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
    float     elapsed = std::chrono::duration<float, std::milli>(Clock::now() - gOpen).count();
    glm::vec3 target;
    float     targetYaw, targetPitch;
    if (fp) {
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
        if (c.mirrorThirdPerson) side = -side;
        float h     = std::clamp(c.camHeight, -4.0f, 4.0f);
        targetYaw   = yawP + (c.tpParallaxOff ? 0.0f : mx * 90.0f * c.tpSensYaw);
        targetPitch = c.camPitch + (c.tpParallaxOff ? 0.0f : my * 180.0f * c.tpSensPitch);
        float r     = yawP * kDeg;
        target      = feet + glm::vec3(std::sin(r) * d + std::cos(r) * side, h, -std::cos(r) * d + std::sin(r) * side);
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
    ++gCompWrites;
    ++gOverrides;
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
}
} // namespace
} // namespace rf::spatialcam

LL_TYPE_INSTANCE_HOOK(RfCameraUpdateHook, ll::memory::HookPriority::Normal, mce::Camera, &mce::Camera::updateViewMatrixDependencies,
                      void) {
    origin();
    rf::spatialcam::onCamera(*this);
}

LL_TYPE_INSTANCE_HOOK(RfLevelCameraPreRenderHook, ll::memory::HookPriority::Normal, LevelRendererCamera,
                      &LevelRendererCamera::$preRenderUpdate, void, ScreenContext& screenContext,
                      LevelRenderPreRenderUpdateParameters& params) {
    rf::spatialcam::applyComponent();
    origin(screenContext, params);
}

namespace rf::spatialcam {
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
    X(tpScreenMult)

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
        bool fp = inventory ? cfg().fpInventory : cfg().fpContainers;
        gFirstPerson = fp;
        gSavedPerspective = -1;
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
        gActive = false;
        if (ci && gSavedPerspective >= 0) ci->getOptions().setPlayerViewPerspective(gSavedPerspective);
        gSavedPerspective = -1;
    }
}
bool active() { return gActive.load(); }

View view() {
    std::lock_guard l(gMutex);
    return gView;
}

void appendDiag(char* b, unsigned long long cap) {
    std::lock_guard l(gMutex);
    size_t len = std::strlen(b);
    len += std::snprintf(b + len, cap - len,
                         "camera (v0.13b): %s; active %d; calls %ld, perspective %ld, world %ld, component writes %ld\n"
                         "  player feet %.2f,%.2f,%.2f rot pitch %.1f yaw %.1f; cam %.2f,%.2f,%.2f target %.2f,%.2f,%.2f t %.2f\n"
                         "  view: valid %d overridden %d fovY %.1f aspect %.3f yawRel %.1f pitch %.1f guiScale %.2f\n",
                         gNote, gActive.load() ? 1 : 0, gCalls, gPersp, gChosen, gOverrides, gLastFeet[0], gLastFeet[1], gLastFeet[2],
                         gLastRot[0], gLastRot[1], gLastCam[0], gLastCam[1], gLastCam[2], gLastTarget[0], gLastTarget[1],
                         gLastTarget[2], gLastT, gView.valid, gView.overridden, gView.fovYDeg, gView.aspect, gView.yawRel, gView.pitch, gView.guiScale);
    len += std::snprintf(b + len, cap - len, "  quaternion convention %d (tries %ld, component misses %ld); M %.2f %.2f %.2f / %.2f %.2f %.2f / %.2f %.2f %.2f\n",
                         gConv, gConvTries, gCompMiss, gLastM[0], gLastM[3], gLastM[6], gLastM[1], gLastM[4], gLastM[7], gLastM[2], gLastM[5], gLastM[8]);
    for (int i = 0; i < gEntN && len < cap; ++i)
        len += std::snprintf(b + len, cap - len, "  entity %s comp %d active %d pos %.2f,%.2f,%.2f\n", gEnt[i].kind == 0 ? "GAME" : "preset", gEnt[i].hasComp,
                             gEnt[i].active, gEnt[i].pos[0], gEnt[i].pos[1], gEnt[i].pos[2]);
    for (int i = 0; i < gSeenN && len < cap; ++i)
        len += std::snprintf(b + len, cap - len, "  cam %p pos %.2f,%.2f,%.2f dist-to-eye %.2f p11 %.3f %s\n", gSeen[i].cam, gSeen[i].pos[0],
                             gSeen[i].pos[1], gSeen[i].pos[2], gSeen[i].dist, gSeen[i].p11, gSeen[i].chosen ? "WORLD" : "");
}
} // namespace rf::spatialcam
