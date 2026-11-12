// Renderer Foundation - LyricsFlow port, spike S1 camera capture (see LyricsCamera.h). Original code.
// STAGED, UNVERIFIED IN GAME. Same world-camera test as SpatialCamera::onCamera (perspective projection, camera within
// 8 blocks of the local player's eyes) so the UI / item cameras are ignored.
#include "LyricsCamera.h"
#include "LLShim.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <atomic>
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
CameraSnap gSnap, gWorldSnap;   // the camera at the eye; the world's own (origin) camera as a stand-in
bool       gHooked = false;

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::atomic<bool> gWanted{false};

void onCamera(mce::Camera& cam) {
    if (!gWanted.load(std::memory_order_relaxed)) return; // lyrics not drawing: touch nothing
    glm::mat4 const& P = *cam.projectionMatrixStack->top()._m;
    bool persp = std::fabs(P[3][3]) < 1e-4f && std::fabs(P[2][3]) > 0.5f && P[1][1] > 0.05f;
    if (!persp) return;
    // game reads first, outside the lock (lesson from cinematic 22bee91: no game calls under our locks)
    auto ci = ll::service::getClientInstance();
    if (!ci) return;
    auto* lp = ci->getLocalPlayer();
    if (!lp) return;
    Actor&    actor = *lp;
    Vec3      feet  = actor.getFeetPos();
    glm::vec3 eye(feet.x, feet.y + 1.62f, feet.z);
    glm::vec3 pos = *cam.mPosition;
    if (glm::length(pos - eye) >= 8.0f) { // not the camera at the eye
        // The world itself is drawn camera-relative: its camera sits at the origin and carries the real view. The
        // camera at the eye (the one used below) stops updating whenever the HUD is not drawn (F1, chat open, some
        // screens) - then the sky and the World FX pass lost their camera and vanished. Keep the origin camera as the
        // stand-in: its axes and projection, with the player's eye as the position.
        if (glm::length(pos) < 0.5f) {
            CameraSnap w;
            w.valid = true;
            w.pos[0] = eye.x; w.pos[1] = eye.y; w.pos[2] = eye.z;
            glm::vec3 r2 = *cam.mRight, u2 = *cam.mUp, f2 = *cam.mForward;
            std::memcpy(w.right, &r2[0], sizeof w.right);
            std::memcpy(w.up, &u2[0], sizeof w.up);
            std::memcpy(w.forward, &f2[0], sizeof w.forward);
            std::memcpy(w.proj, &P[0][0], sizeof w.proj);
            w.fovYDeg = 2.0f * std::atan(1.0f / P[1][1]) * 57.2957795f;
            w.aspect  = P[1][1] / P[0][0];
            w.atMs    = nowMs();
            std::lock_guard l(gMutex);
            gWorldSnap = w;
        }
        return;
    }
    CameraSnap s;
    s.valid = true;
    s.pos[0] = pos.x; s.pos[1] = pos.y; s.pos[2] = pos.z;
    glm::vec3 r = *cam.mRight, u = *cam.mUp, f = *cam.mForward;
    std::memcpy(s.right, &r[0], sizeof s.right);
    std::memcpy(s.up, &u[0], sizeof s.up);
    std::memcpy(s.forward, &f[0], sizeof s.forward);
    std::memcpy(s.proj, &P[0][0], sizeof s.proj);
    s.fovYDeg = 2.0f * std::atan(1.0f / P[1][1]) * 57.2957795f;
    s.aspect  = P[1][1] / P[0][0];
    s.atMs    = nowMs();
    std::lock_guard l(gMutex); // only plain data under the lock
    s.calls  = gSnap.calls + 1;
    s.chosen = gSnap.chosen + 1;
    gSnap    = s;
}
} // namespace

CameraSnap camera() {
    std::lock_guard l(gMutex);
    int64_t const now = nowMs();
    if (gSnap.valid && now - gSnap.atMs <= 120) return gSnap;
    if (gWorldSnap.valid && now - gWorldSnap.atMs <= 250) {   // the eye camera is stale: the HUD is hidden
        CameraSnap w = gWorldSnap;
        w.calls = gSnap.calls; w.chosen = gSnap.chosen;
        return w;
    }
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
void setCameraWanted(bool on) { gWanted.store(on, std::memory_order_relaxed); }
void uninstallCamera() {
    if (!gHooked) return;
    RfLyricsCameraHook::unhook();
    gHooked = false;
}
} // namespace rf::lyrics
