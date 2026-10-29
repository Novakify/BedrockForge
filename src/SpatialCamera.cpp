// Renderer Foundation - Spatial GUI third-person camera (see SpatialCamera.h). Original code for Renderer Foundation;
// the camera math follows the Java mod Spatial GUI (tastytrash) CameraMixin with its default config.
//
// Bedrock side: every mce::Camera rebuilds its derived data in updateViewMatrixDependencies(). For the world camera
// (perspective, within a few blocks of the player's eyes) we replace the view matrix with the Java pose and rebuild
// again, so frustum/culling match. While a container screen is open the game is put in third person (so the player
// model renders) and restored afterwards.
#include "SpatialCamera.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/client/game/ClientInstance.h"
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

// true = the view matrix was replaced (caller rebuilds the dependencies again)
bool onCamera(mce::Camera& cam) {
    std::lock_guard l(gMutex);
    ++gCalls;
    glm::mat4 const& P = *cam.projectionMatrixStack->top()._m;
    bool persp = std::fabs(P[3][3]) < 1e-4f && std::fabs(P[2][3]) > 0.5f && P[1][1] > 0.05f;
    if (!persp) return false;
    ++gPersp;
    auto ci = ll::service::getClientInstance();
    if (!ci) return false;
    auto* lp = ci->getLocalPlayer();
    if (!lp) return false;
    Actor&    actor = *lp;
    Vec3      feetV = actor.getFeetPos();
    Vec2      rot   = actor.getRotation(); // x = pitch, y = yaw (degrees)
    glm::vec3 feet(feetV.x, feetV.y, feetV.z);
    glm::vec3 eye  = feet + glm::vec3(0.0f, 1.62f, 0.0f);
    glm::vec3 pos  = *cam.mPosition;
    float     dist = glm::length(pos - eye);
    bool      world = dist < 8.0f;
    noteSeen(&cam, pos, dist, P[1][1], world);
    if (!world) return false;
    ++gChosen;
    gLastFeet[0] = feet.x; gLastFeet[1] = feet.y; gLastFeet[2] = feet.z;
    gLastRot[0]  = rot.x;  gLastRot[1]  = rot.y;

    View v;
    v.valid   = true;
    v.tanHalf = 1.0f / P[1][1];
    v.aspect  = P[1][1] / P[0][0];
    v.fovYDeg = 2.0f * std::atan(v.tanHalf) / kDeg;
    v.yawRel  = 0.0f;
    v.pitch   = rot.x;
    if (!gActive.load()) {
        gHaveStart = false;
        gView      = v;
        return false;
    }

    float yawP = rot.y, pitchP = rot.x;
    if (!gHaveStart) {
        gHaveStart  = true;
        gStartPos   = eye; // Java: the camera position when the screen opened (first person eyes)
        gStartYaw   = yawP;
        gStartPitch = pitchP;
    }
    // CameraUtil.calculateAutoFovDistance / calculateAutoFovSideOffset
    float fm   = v.fovYDeg / kBaselineFov;
    float inv  = 1.0f / fm - 1.0f;
    float d    = std::clamp(kCamDistance * (1.0f + inv * kDistMult), -4.0f, 4.0f);
    float side = std::clamp(kCamSide * (1.0f + inv * kSideMult), -4.0f, 4.0f);
    float h    = std::clamp(kCamHeight, -4.0f, 4.0f);
    // parallax (third person): mouse position in [-1, 1]
    float mx = 0.0f, my = 0.0f;
    rf::panel::cursorNdc(mx, my);
    mx = std::clamp(mx, -1.0f, 1.0f);
    my = std::clamp(my, -1.0f, 1.0f);
    float targetYaw   = yawP + mx * 90.0f * kParallaxYaw;
    float targetPitch = kCamPitch + my * 180.0f * kParallaxPitch;
    float r           = yawP * kDeg;
    glm::vec3 target  = feet + glm::vec3(std::sin(r) * d + std::cos(r) * side, h, -std::cos(r) * d + std::sin(r) * side);
    // applyCameraUpdate: eased glide from the start pose
    float elapsed = std::chrono::duration<float, std::milli>(Clock::now() - gOpen).count();
    float t       = std::min(1.0f, (elapsed + kTransitionSkip * kTransitionMs) / kTransitionMs);
    float e       = easeInOutSine(t);
    glm::vec3 camPos = t >= 1.0f ? target : gStartPos + (target - gStartPos) * e;
    float camYaw     = t >= 1.0f ? targetYaw : gStartYaw + (targetYaw - gStartYaw) * e;
    float camPitch   = t >= 1.0f ? targetPitch : gStartPitch + (targetPitch - gStartPitch) * e;

    // new view matrix in the game's own convention: R' = R * O0 * O'^T (R = current rotation part, O0 its orientation)
    glm::mat4& V  = *cam.viewMatrixStack->getTop()._m;
    glm::mat3  R  = glm::mat3(V);
    glm::mat3  O0 = orient(yawP, pitchP); // first person and third-person-back cameras look where the player looks
    glm::mat3  R2 = R * O0 * glm::transpose(orient(camYaw, camPitch));
    glm::vec3  t2 = -(R2 * camPos);
    glm::mat4  V2(R2);
    V2[3] = glm::vec4(t2, 1.0f);
    V     = V2;
    ++gOverrides;
    gLastCam[0] = camPos.x; gLastCam[1] = camPos.y; gLastCam[2] = camPos.z;
    gLastTarget[0] = target.x; gLastTarget[1] = target.y; gLastTarget[2] = target.z;
    gLastT = t;

    v.yawRel     = camYaw - yawP;
    v.pitch      = camPitch;
    v.overridden = true;
    gView        = v;
    return true;
}
} // namespace
} // namespace rf::spatialcam

LL_TYPE_INSTANCE_HOOK(RfCameraUpdateHook, ll::memory::HookPriority::Normal, mce::Camera, &mce::Camera::updateViewMatrixDependencies,
                      void) {
    origin();
    if (rf::spatialcam::onCamera(*this)) origin();
}

namespace rf::spatialcam {
void install() {
    if (gInstalled) return;
    RfCameraUpdateHook::hook();
    gInstalled = true;
    std::snprintf(gNote, sizeof gNote, "mce::Camera::updateViewMatrixDependencies hooked");
}
void uninstall() {
    if (!gInstalled) return;
    RfCameraUpdateHook::unhook();
    gInstalled = false;
}

void setActive(bool on) {
    auto ci = ll::service::getClientInstance();
    if (on && !gActive.load()) {
        if (ci) {
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
                         "camera (v0.13): %s; active %d; calls %ld, perspective %ld, world %ld, overrides %ld\n"
                         "  player feet %.2f,%.2f,%.2f rot pitch %.1f yaw %.1f; cam %.2f,%.2f,%.2f target %.2f,%.2f,%.2f t %.2f\n"
                         "  view: valid %d overridden %d fovY %.1f aspect %.3f yawRel %.1f pitch %.1f\n",
                         gNote, gActive.load() ? 1 : 0, gCalls, gPersp, gChosen, gOverrides, gLastFeet[0], gLastFeet[1], gLastFeet[2],
                         gLastRot[0], gLastRot[1], gLastCam[0], gLastCam[1], gLastCam[2], gLastTarget[0], gLastTarget[1],
                         gLastTarget[2], gLastT, gView.valid, gView.overridden, gView.fovYDeg, gView.aspect, gView.yawRel, gView.pitch);
    for (int i = 0; i < gSeenN && len < cap; ++i)
        len += std::snprintf(b + len, cap - len, "  cam %p pos %.2f,%.2f,%.2f dist-to-eye %.2f p11 %.3f %s\n", gSeen[i].cam, gSeen[i].pos[0],
                             gSeen[i].pos[1], gSeen[i].pos[2], gSeen[i].dist, gSeen[i].p11, gSeen[i].chosen ? "WORLD" : "");
}
} // namespace rf::spatialcam
