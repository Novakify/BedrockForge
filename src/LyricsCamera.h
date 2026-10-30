// Renderer Foundation - LyricsFlow port, spike S1: the exact world camera for world-space lyrics.
// STAGED, UNVERIFIED (needs the PC): hooks mce::Camera::updateViewMatrixDependencies (the same function SpatialCamera
// reads the projection from) and keeps the world camera's position, basis and projection matrix, so text placed by
// Bedrock Forge stays glued to the terrain (third person, view bobbing, FOV effects included).
// Plain data only here: files that include d3d12.h must not include LeviLamina headers (see LLShim.h).
#pragma once
#include <cstdint>

namespace rf::lyrics {

struct CameraSnap {
    bool    valid = false;
    double  pos[3]{};                       // camera position, world
    float   right[3]{}, up[3]{}, forward[3]{}; // camera basis, world
    float   proj[16]{};                     // projection matrix, column-major (glm), as the game uses it
    float   fovYDeg = 70.0f, aspect = 16.0f / 9.0f;
    int64_t atMs = 0;                       // steady clock ms of the capture
    uint64_t calls = 0, chosen = 0;         // diagnostics
};

void       installCamera();
/// The hook stays installed but does nothing (no game reads at all) until this is on. LyricsPass sets it each Present:
/// on only while BF actually publishes an active lyrics frame and "Draw lyrics" is checked.
void       setCameraWanted(bool on);
void       uninstallCamera();
CameraSnap camera();

} // namespace rf::lyrics
