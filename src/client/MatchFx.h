#pragma once
// Bedrock Forge -> Renderer Foundation: the MATCH, for game-aware visuals (World FX atmospheres).
// A shader only sees pixels; BF sees the game. This bridge hands RF what happened in the match so the look can react:
//   * phase      living match sky: 0 golden hour (match start) -> 1 dusk (first bed broken) -> 2 night (final fight)
//   * teams      every bed in range with its team (dye) colour and whether it still stands: team-lit islands
//   * events     blocks broken / placed near the player, with the block's colour: real block shards, placement pop
// Same pattern as BF_GetPostFx: BF owns the data, RF pulls it once per frame. RF calls BF_MatchFxConsumer(1) every
// frame it uses it; only then does BF also move the game's own time of day with the phase (so vanilla lighting,
// torches and block light follow the sky).
#include <cstdint>
#include <string>

extern "C" {
struct BfMatchTeamV1 {
    float    pos[3];   // bed centre (world)
    float    col[3];   // team colour, linear 0..1
    uint32_t alive;    // 1 = bed stands
    int64_t  changedMs; // steady-clock ms of the last change (bed found / broken): RF fades from it
};
struct BfMatchEventV1 {
    int32_t  x, y, z;
    uint32_t kind;     // 1 broken, 2 placed
    int64_t  atMs;     // steady-clock ms
    float    col[4];   // the block's map colour (linear-ish 0..1), a = 1
    uint32_t seed;     // stable per event (shard layout)
    uint32_t floorDy;  // the shards land this many blocks below the block (0 = on the block under it); 255 = nothing: they fall away
};
struct BfMatchFxV1 {
    uint32_t size, version; // sizeof, 6
    uint32_t active;        // 1 while a world is on screen
    uint32_t frame;
    float    phase;         // smoothed 0..2
    float    phaseTarget;   // 0, 1 or 2
    float    timeOfDay;     // what BF tells the game (when a consumer is attached)
    uint32_t bedsBroken;    // this match
    int64_t  nowMs;         // BF's steady clock when filled (RF ages events against it)
    uint32_t nTeams, nEvents; // nEvents = total written; ring index = n % 256
    BfMatchTeamV1  teams[16];
    BfMatchEventV1 events[256]; // v2: 256 (a bridge is dozens of placements)
    // v3: per team, what the players are doing around it (same index as teams[])
    struct TeamEx {
        float    threat;  // 0..1 enemies near this bed (smoothed)
        uint32_t elim;    // 1 = the team is out (the server said so)
        uint32_t mine;    // 1 = your team
        int64_t  elimMs;  // when it went out
        // v4: the server's own bed model (an entity on the bed): where it stands and faces - RF draws the cosmetic bed there
        float    vis[3];
        float    yaw;     // degrees
        uint32_t hasVis;  // 1 = found (and hidden on this screen while RF draws the cosmetic bed)
    } teamEx[16];
    // v5: projectiles in flight (arrows, pearls, fireballs, snowballs, eggs): the last 12 positions each, newest first
    struct Proj {
        uint32_t kind;        // 0 empty, 1 arrow, 2 ender pearl, 3 fireball, 4 snowball / egg, 5 other
        uint32_t n;           // points used
        int64_t  lastMs;      // last seen (steady ms); RF fades a trail out after it stops being seen
        float    col[3];      // the shooter's team colour if known, else white
        float    pts[12][4];  // xyz world, w = seconds ago
    } proj[16];
    // v6: the players' skins (classic 64x64 layout, RGBA, top row first) for RF's death scenes: a kill event's floorDy
    // is the victim's slot + 1 (255 / 0 = none). gen changes whenever a slot's pixels change.
    struct Skin {
        uint32_t gen, ok;
        uint8_t  px[64 * 64 * 4];
    } skins[16];
    uint32_t mySkin; // your own skin's slot + 1 (0 = none)
    // v7: you - your health (RF: low health drains the edges of the frame in time with a heartbeat BF plays) and your
    // kills since you last died (RF: a run of kills warms the frame)
    float    hp, hpMax;  // hpMax 0 = not known
    uint32_t streak;     // kills this life
    uint32_t pad7;
    int64_t  beatMs;     // the last heartbeat (BF's clock), 0 = none
    int64_t  killMs;     // your last kill
};
__declspec(dllexport) BfMatchFxV1 const* BF_GetMatchFx();
__declspec(dllexport) void               BF_MatchFxConsumer(int on);
/// RF's F6 "preview sky phase": the game's time of day follows this phase instead of the match (< 0 = the match)
__declspec(dllexport) void               BF_MatchFxPreview(float phase);
}

class IClientInstance;
class LocalPlayer;

namespace bedrock_forge::client::matchfx {
void install();   // block-change listener
void uninstall();
/// every HUD frame (client thread)
void frame(IClientInstance& client, LocalPlayer* player);
/// chat / title text (fallback for servers that remove beds without a block update we see)
void onText(std::string const& message);
/// the server played an actor event (2 = hurt) on runtime id `rid` (network thread)
void onActorEvent(uint64_t rid, int event);
/// the time of day the game should render while a consumer is attached; false = leave the game's own
bool timeOverride(float& t);
} // namespace bedrock_forge::client::matchfx
