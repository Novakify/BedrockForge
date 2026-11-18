#pragma once
// Bedrock Forge client - 3D model cosmetics (Knives first; 3D Skins and Garage cars next).
//
// Pipeline (docs/COSMETICS.md): the models are our own resource-pack attachables (real poly_mesh geometry, built by
// tools/Cosmetics), drawn natively by the game in its own item pass - no extra draw calls, nothing server-side, so
// they work on Hive. Bedrock Forge only switches them: every HUD frame it writes PUBLIC Molang variables on the
// players (v.bfknife = design, v.bfkdraw / v.bfkinsp = seconds since you drew / inspected), which the attachables
// read through c.owning_entity. No Bedrock Forge = every variable is 0 = the vanilla look.

class LocalPlayer;

namespace bedrock_forge::client {
class Registry;
namespace cosmetics {
void registerModules(Registry& r);
void frame(LocalPlayer* player);   // HUD frame
void noteLocalHit(bool crit);      // your confirmed hit (GameMode::attack hook)
} // namespace cosmetics
} // namespace bedrock_forge::client
