// Renderer Foundation - Cinematic Interactions port: game queries (no LeviLamina types in this header).
#pragma once
#include <string>

namespace rf::cine::game {
/// Local player: feet position, look (degrees), alive. false = no player.
bool        playerState(double feet[3], float& yaw, float& pitch, bool& alive);
/// The block the player's hit result points at (the block a use went to).
bool        hitBlock(int& x, int& y, int& z, std::string& typeName);
std::string blockName(int x, int y, int z);
/// A solid block (collider) at x,y,z.
bool        solid(int x, int y, int z);
/// Hide the vanilla first-person hand (Options "hide hand"); saved = previous value, -1 = not changed by us.
void        hideHand(int& saved, bool on);
} // namespace rf::cine::game
