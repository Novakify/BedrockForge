// FiguraBridge host interface: everything the Figura runtime needs from the game. The Bedrock implementation lives in
// src/figura/win (LeviLamina); the headless simulator (tests/figura) fakes it. Units follow Java Edition/Figura
// (blocks, degrees, ticks) so scripts see the values they expect; the host converts from Bedrock.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "figura/core/FbMath.h"

namespace bf::figura {

struct ItemState {
    std::string id = "minecraft:air";   // Java-style id (the host maps Bedrock names)
    int         count = 0;
    std::string useAction = "NONE";     // NONE EAT DRINK BLOCK BOW SPEAR CROSSBOW SPYGLASS TOOT_HORN BRUSH
    bool        isTool = false, isArmor = false, isFood = false, isBlockItem = false, charged = false;
    int         maxUseDuration = 0;
    std::string name;
    std::vector<std::string> tags;
    bool empty() const { return id == "minecraft:air" || count <= 0; }
};

struct EntityRef {
    bool        valid = false;
    std::string type;                   // "minecraft:boat" ...
    std::string uuid;
    Vec3        pos;
    Vec3        rot;                    // pitch, yaw
};

/// Snapshot of the avatar's player, refreshed by the host every tick and every frame.
struct PlayerState {
    bool        loaded = false;
    std::string name, uuid;
    Vec3        pos, prevPos;           // feet, blocks (Java axes: +x east, +y up, +z south)
    Vec3        velocity;               // blocks/tick (getDeltaMovement)
    float       yaw = 0, prevYaw = 0, pitch = 0, prevPitch = 0;   // look (Java degrees)
    float       bodyYaw = 0, prevBodyYaw = 0, headYaw = 0;
    float       eyeHeight = 1.62f;
    std::string pose = "STANDING";      // Pose.name(): STANDING FALL_FLYING SLEEPING SWIMMING SPIN_ATTACK CROUCHING LONG_JUMPING DYING
    bool onGround = true, sprinting = false, crouching = false, swimming = false, gliding = false, climbing = false;
    bool inWater = false, underwater = false, inLava = false, onFire = false, invisible = false, sleeping = false;
    bool blocking = false, usingItem = false, fishing = false, alive = true, flying = false, jumping = false;
    bool leftHanded = false, isFirstPerson = false;
    int  swingTime = 0;                 // ticks since swing start (LivingEntity.swingTime), 0 when not swinging
    bool swinging = false;
    bool swingOffhand = false;
    float attackAnim = 0;               // getAttackAnim(delta) 0..1
    int  useItemRemaining = 0;          // getUseItemRemainingTicks
    bool activeHandOff = false;         // using item in the off hand
    float health = 20, maxHealth = 20, food = 20, saturation = 5, armor = 0, absorption = 0;
    int   experienceLevel = 0;
    float experienceProgress = 0;
    int   airSupply = 300, maxAir = 300;
    int   hurtTime = 0, deathTime = 0, age = 0;  // ticks
    float walkPos = 0, walkSpeed = 0, prevWalkSpeed = 0; // limb swing (walkAnimation)
    std::string gamemode = "SURVIVAL", dimension = "minecraft:overworld";
    ItemState mainHand, offHand, activeItem, helmet, chest, legs, boots;
    EntityRef vehicle;                  // getVehicle()
    bool hasVehicle() const { return vehicle.valid; }
};

/// World queries scripts can make (world API). Default: a flat empty world.
class IWorldQuery {
public:
    virtual ~IWorldQuery() = default;
    virtual int         lightLevel(int x, int y, int z) { (void)x; (void)y; (void)z; return 15; }
    virtual int         skyLight(int x, int y, int z) { (void)x; (void)y; (void)z; return 15; }
    virtual int         blockLight(int x, int y, int z) { (void)x; (void)y; (void)z; return 0; }
    virtual std::string blockId(int x, int y, int z) { (void)x; (void)z; return y < 0 ? "minecraft:stone" : "minecraft:air"; }
    virtual std::string biome(int x, int y, int z) { (void)x; (void)y; (void)z; return "minecraft:plains"; }
    virtual int64_t     time() { return 0; }
    virtual int64_t     dayTime() { return 0; }
    virtual float       rainGradient() { return 0; }
    virtual bool        thundering() { return false; }
    virtual std::string dimension() { return "minecraft:overworld"; }
};

/// Side effects and engine services.
class IHost {
public:
    virtual ~IHost() = default;
    virtual int64_t millis() = 0;                         // Util.getMillis
    virtual bool    paused() { return false; }            // Minecraft.isPaused
    virtual void    log(std::string const& msg) = 0;      // print()/chat output
    virtual void    error(std::string const& msg) = 0;    // script error (shown to the player)
    /// sounds: `custom` = an avatar sound (Figura sound name), else a Java sound id ("minecraft:entity.x.y")
    virtual void    playSound(std::string const& id, bool custom, Vec3 pos, float volume, float pitch, bool loop, int handle) {
        (void)id; (void)custom; (void)pos; (void)volume; (void)pitch; (void)loop; (void)handle;
    }
    virtual void    stopSound(int handle) { (void)handle; }   // -1 = all of this avatar
    virtual bool    keyDown(std::string const& key) { (void)key; return false; } // "key.keyboard.w" / "key.mouse.left"
    virtual std::string vanillaKeyBinding(std::string const& id) { (void)id; return "key.keyboard.unknown"; }
    virtual IWorldQuery& world() { static IWorldQuery w; return w; }
    virtual std::string  chatText() { return ""; }
    virtual void         sendChat(std::string const& s) { (void)s; }
    virtual void         setActionbar(std::string const& s) { (void)s; }
    virtual float        fps() { return 60; }
    virtual std::string  version() { return "1.20.1"; }
    virtual int          windowWidth() { return 1920; }
    virtual int          windowHeight() { return 1080; }
    virtual Vec3         cameraPos() { return {}; }
    virtual Vec3         cameraRot() { return {}; }
    virtual bool         isHudEnabled() { return true; }
    virtual bool         isFirstPerson() { return false; }
    virtual std::string  serverBrand() { return "Integrated"; }
};

} // namespace bf::figura
