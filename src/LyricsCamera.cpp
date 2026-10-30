// Renderer Foundation - LyricsFlow port, spike S1 camera capture (see LyricsCamera.h). Original code.
// STAGED, UNVERIFIED IN GAME. Same world-camera test as SpatialCamera::onCamera (perspective projection, camera within
// 8 blocks of the local player's eyes) so the UI / item cameras are ignored.
#include "LyricsCamera.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>

#include <glm/glm.hpp>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/math/Matrix.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/deps/renderer/MatrixStack.h"
#include "mc/world/actor/Actor.h"

namespace rf::lyrics {
namespace {
std::mutex gMutex;
CameraSnap gSnap;
bool       gHooked = false;

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void onCamera(mce::Camera& cam) {
    glm::mat4 const& P = *cam.projectionMatrixStack->top()._m;
    std::lock_guard  l(gMutex);
    ++gSnap.calls;
    bool persp = std::fabs(P[3][3]) < 1e-4f && std::fabs(P[2][3]) > 0.5f && P[1][1] > 0.05f;
    if (!persp) return;
    auto ci = ll::service::getClientInstance();
    if (!ci) return;
    auto* lp = ci->getLocalPlayer();
    if (!lp) return;
    Actor&    actor = *lp;
    Vec3      feet  = actor.getFeetPos();
    glm::vec3 eye(feet.x, feet.y + 1.62f, feet.z);
    glm::vec3 pos = *cam.mPosition;
    if (glm::length(pos - eye) >= 8.0f) return; // not the world camera
    ++gSnap.chosen;
    gSnap.valid = true;
    gSnap.pos[0] = pos.x; gSnap.pos[1] = pos.y; gSnap.pos[2] = pos.z;
    glm::vec3 r = *cam.mRight, u = *cam.mUp, f = *cam.mForward;
    std::memcpy(gSnap.right, &r[0], sizeof gSnap.right);
    std::memcpy(gSnap.up, &u[0], sizeof gSnap.up);
    std::memcpy(gSnap.forward, &f[0], sizeof gSnap.forward);
    std::memcpy(gSnap.proj, &P[0][0], sizeof gSnap.proj);
    gSnap.fovYDeg = 2.0f * std::atan(1.0f / P[1][1]) * 57.2957795f;
    gSnap.aspect  = P[1][1] / P[0][0];
    gSnap.atMs    = nowMs();
}
} // namespace

CameraSnap camera() {
    std::lock_guard l(gMutex);
    return gSnap;
}
} // namespace rf::lyrics

LL_TYPE_INSTANCE_HOOK(RfLyricsCameraHook, ll::memory::HookPriority::Low, mce::Camera, &mce::Camera::updateViewMatrixDependencies, void) {
    origin();
    rf::lyrics::onCamera(*this);
}

namespace rf::lyrics {
void installCamera() {
    if (gHooked) return;
    RfLyricsCameraHook::hook();
    gHooked = true;
}
void uninstallCamera() {
    if (!gHooked) return;
    RfLyricsCameraHook::unhook();
    gHooked = false;
}
} // namespace rf::lyrics
