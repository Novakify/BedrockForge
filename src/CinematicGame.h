// Renderer Foundation - Cinematic Interactions port: game queries (no LeviLamina types in this header).
#pragma once
#include <string>
#include <vector>

namespace rf::cine::game {
/// Local player: feet position, look (degrees), alive. false = no player.
bool        playerState(double feet[3], float& yaw, float& pitch, bool& alive);
/// the player is in creative mode (the enchanting offers then tell only their clue)
bool        creative();
/// The block the player's hit result points at (the block a use went to).
bool        hitBlock(int& x, int& y, int& z, std::string& typeName);
std::string blockName(int x, int y, int z);
/// A block's horizontal facing from its states (0 north, 1 east, 2 south, 3 west; -1 unknown).
int         facing(int x, int y, int z);
/// A block's states as SNBT (diagnostics).
std::string blockStates(int x, int y, int z);
/// Testing aid: use (right-click) the block the player looks at, as the game's own "use" does. false = nothing hit.
bool        useHitBlock();
/// A solid block (collider) at x,y,z.
bool        solid(int x, int y, int z);
/// Hide the vanilla first-person hand (Options "hide hand"); saved = previous value, -1 = not changed by us.
void        hideHand(int& saved, bool on);
/// The local player's skin image (RGBA8, usually 64x64 or 128x128) and arm size. false = none (persona skin, no image).
bool        skin(std::vector<unsigned char>& rgba, int& w, int& h, bool& slim);
/// The stack the cursor carries in a container screen (Bedrock's UI cursor slot): its type name ("minecraft:potion"),
/// empty when nothing is carried; count = how many.
std::string carriedItem(int& count);
/// The item in the selected hotbar slot (type name, empty = none).
std::string handItem(int& count);
/// The selected hotbar slot (0..8).
int         selectedSlot();
/// Use a block as the game's own use does (with the item in hand); not taken over by a scene. face 1 = its top.
bool        useBlock(int x, int y, int z, int face);
/// Open the player's own inventory screen (the hidden window of a scene at a block with no window of its own).
bool        openInventory();
/// The record a jukebox holds (its type name), empty = none.
std::string jukeboxRecord(int x, int y, int z);
/// A campfire's four spots: what lies on each (type name, empty = nothing) and how long it has cooked (ticks).
bool        campfireFood(int x, int y, int z, std::string names[4], int times[4]);
/// Hooks: a use of a block with no window of its own may start a scene instead (rf::cine::screenlessUse).
void        installHooks();
} // namespace rf::cine::game
