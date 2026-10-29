// Renderer Foundation - Cinematic Interactions port: game queries (no LeviLamina types in this header).
#pragma once
#include <string>
#include <vector>

namespace rf::cine::game {
/// Local player: feet position, look (degrees), alive. false = no player.
bool        playerState(double feet[3], float& yaw, float& pitch, bool& alive);
/// The block the player's hit result points at (the block a use went to).
bool        hitBlock(int& x, int& y, int& z, std::string& typeName);
std::string blockName(int x, int y, int z);
/// A block's horizontal facing from its states (0 north, 1 east, 2 south, 3 west; -1 unknown).
int         facing(int x, int y, int z);
/// A block's states as SNBT (diagnostics).
std::string blockStates(int x, int y, int z);
/// A solid block (collider) at x,y,z.
bool        solid(int x, int y, int z);
/// Hide the vanilla first-person hand (Options "hide hand"); saved = previous value, -1 = not changed by us.
void        hideHand(int& saved, bool on);
/// The local player's skin image (RGBA8, usually 64x64 or 128x128) and arm size. false = none (persona skin, no image).
bool        skin(std::vector<unsigned char>& rgba, int& w, int& h, bool& slim);
} // namespace rf::cine::game
