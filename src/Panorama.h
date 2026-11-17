// Renderer Foundation - Main Menu panorama (Bedrock Forge "Main Menu": your world behind the menus).
// On request the camera turns through the six faces of a cube (front, right, back, left, up, down) with a 90 degree
// view, the world is copied before the HUD and your hand are drawn, and the faces are written as the title screen's
// panorama (textures/ui/panoramahdr_0..5.hdr) into a development resource pack Bedrock Forge registers. The menu shows
// it from the next start of the game. Original code for Renderer Foundation.
#pragma once
#include <cstdint>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;
struct D3D12_RESOURCE_DESC;

namespace rf::pano {

/// A new capture id (Atmos.txt "pano_capture <id>"): starts a capture if the id changed since the last one.
void request(int id);
/// A capture is running (the world pass must run: its targets are wanted).
bool capturing();
/// The camera for this frame (render-prep thread): true while a face is being held. yaw / pitch in degrees
/// (Minecraft: yaw 0 = +Z, + turns right; pitch + = down); fovScale multiplies the game's own field of view.
bool pose(float baseFovRad, float& yaw, float& pitch, float& fovScale);
/// In the world pass (before the HUD): copy the colour target if a face is due. tanX / tanY = the projection's
/// tan(fov/2) this frame (to crop exactly 90 degrees).
void grab(ID3D12Device* dev, ID3D12GraphicsCommandList* list, ID3D12Resource* rt, D3D12_RESOURCE_DESC const& rd, bool bgra,
          float tanX, float tanY);
/// Once per Present: advance the capture, write the files once the copies are done.
void onPresent();
/// Status for logs / the client ("" = idle).
char const* status();

} // namespace rf::pano
