// FiguraBridge host interface: everything the Figura runtime needs from the game. The Bedrock implementation lives in
// src/figura/win (LeviLamina); the headless simulator (tests/figura) fakes it. Units follow Java Edition/Figura
// (blocks, degrees, ticks) so scripts see the values they expect; the host converts from Bedrock.
#pragma once
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "figura/core/FbMath.h"

namespace bf::figura {

/// An item stack. Fields left at their "unknown" value (-1 / empty) are filled by the Lua API from its Java 1.20.1
/// item table (max stack size, durability, display name, food, use action ...), so a host only needs the id + count.
struct ItemState {
    std::string id = "minecraft:air";   // Java-style id (the host maps Bedrock names)
    int         count = 0;
    std::string useAction = "NONE";     // NONE EAT DRINK BLOCK BOW SPEAR CROSSBOW SPYGLASS TOOT_HORN BRUSH
    bool        isTool = false, isArmor = false, isFood = false, isBlockItem = false, charged = false;
    int         maxUseDuration = 0;
    std::string name;                   // hover name (custom name); empty = the item's display name
    std::vector<std::string> tags;
    int         damage = 0;             // getDamageValue
    int         maxDamage = -1;         // -1 = from the item table
    int         maxCount = -1;          // -1 = from the item table
    std::string rarity;                 // empty = from the item table (COMMON UNCOMMON RARE EPIC)
    bool        glint = false;          // enchanted / foil
    int         popTime = 0;
    std::map<std::string, std::string> nbt; // extra NBT tag entries as text ("Damage" "0", "display" json ...)
    bool empty() const { return id == "minecraft:air" || count <= 0; }
};

/// Another entity (vehicle, passenger, raycast hit, world.getEntities ...): a snapshot.
struct EntityRef {
    bool        valid = false;
    std::string type;                   // "minecraft:boat" ...
    std::string uuid;
    std::string name;                   // display name (custom name or type name); empty = derived from type
    Vec3        pos, prevPos;           // feet; prevPos = last tick (for getVelocity / lerp), defaults to pos
    Vec3        rot;                    // pitch, yaw
    Vec3        velocity;               // blocks/tick
    float       bodyYaw = 0, eyeHeight = -1, width = -1, height = -1; // -1 = Figura-plausible default by type
    std::string pose = "STANDING";
    bool        living = false, isPlayer = false;
    bool        onGround = true, alive = true, invisible = false, onFire = false, crouching = false, sprinting = false;
    bool        inWater = false, underwater = false, inLava = false, glowing = false, silent = false;
    float       health = 0, maxHealth = 0, armor = 0;
    ItemState   mainHand, offHand, helmet, chest, legs, boots;
    bool        hasAvatar = false;
};

struct StatusEffect {
    std::string name;                   // description id: "effect.minecraft.speed"
    int         amplifier = 0, duration = 0;
    bool        visible = true;
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
    bool inRain = false, glowing = false, silent = false, riptide = false;
    int  swingTime = 0;                 // ticks since swing start (LivingEntity.swingTime), 0 when not swinging
    bool swinging = false;
    bool swingOffhand = false;
    int  swingDuration = 6;             // LivingEntity.getCurrentSwingDuration (6, haste/fatigue change it)
    float attackAnim = 0;               // getAttackAnim(delta) 0..1
    float attackCharge = 1;             // getAttackStrengthScale(0) 0..1 (host.getAttackCharge)
    int  useItemRemaining = 0;          // getUseItemRemainingTicks
    bool activeHandOff = false;         // using item in the off hand
    float health = 20, maxHealth = 20, food = 20, saturation = 5, exhaustion = 0, armor = 0, absorption = 0;
    int   experienceLevel = 0;
    float experienceProgress = 0;
    int   airSupply = 300, maxAir = 300;
    int   hurtTime = 0, deathTime = 0, age = 0;  // ticks
    int   frozenTicks = 0, arrowCount = 0, stingerCount = 0;
    int   permissionLevel = 4;          // 0..4 (op level); singleplayer with cheats = 4
    float walkPos = 0, walkSpeed = 0, prevWalkSpeed = 0; // limb swing (walkAnimation)
    std::string gamemode = "SURVIVAL", dimension = "minecraft:overworld";
    std::string modelType = "DEFAULT";  // DEFAULT (wide arms) or SLIM
    bool        hasCape = false, hasSkin = true;
    float       reachDistance = 4.5f;   // gameMode.getPickRange (5 in creative)
    ItemState mainHand, offHand, activeItem, helmet, chest, legs, boots;
    int       selectedSlot = 0;         // hotbar 0..8
    std::vector<ItemState> inventory;   // optional: Inventory slots 0..35 (0..8 hotbar); missing = empty
    std::vector<StatusEffect> effects;
    EntityRef vehicle;                  // getVehicle()
    std::vector<EntityRef> passengers;
    bool hasVehicle() const { return vehicle.valid; }
};

/// Result of a block raycast (Figura LuaUtils.parseBlockHitResult).
struct BlockHit {
    bool        hit = false;
    int         x = 0, y = 0, z = 0;    // block position
    Vec3        pos;                    // exact hit position
    std::string face = "up";            // Direction.getName(): down up north south west east
};
/// Result of an entity raycast.
struct EntityHit {
    bool      hit = false;
    EntityRef entity;
    Vec3      pos;
};

/// World queries scripts can make (world API). Default: a flat world (stone below y=0, air above), no entities.
class IWorldQuery {
public:
    virtual ~IWorldQuery() = default;
    virtual int         lightLevel(int x, int y, int z) { (void)x; (void)y; (void)z; return 15; }
    virtual int         skyLight(int x, int y, int z) { (void)x; (void)y; (void)z; return 15; }
    virtual int         blockLight(int x, int y, int z) { (void)x; (void)y; (void)z; return 0; }
    virtual std::string blockId(int x, int y, int z) { (void)x; (void)z; return y < 0 ? "minecraft:stone" : "minecraft:air"; }
    /// Block state properties ("facing" -> "north"); empty = the block's default state.
    virtual std::map<std::string, std::string> blockProperties(int x, int y, int z) { (void)x; (void)y; (void)z; return {}; }
    virtual std::string biome(int x, int y, int z) { (void)x; (void)y; (void)z; return "minecraft:plains"; }
    virtual int64_t     time() { return 0; }      // game time (ticks)
    virtual int64_t     dayTime() { return 0; }   // Level.getDayTime (ticks, not wrapped)
    virtual int         moonPhase() { return int((dayTime() / 24000) % 8); }
    virtual float       rainGradient() { return 0; }
    virtual bool        thundering() { return false; }
    virtual std::string dimension() { return "minecraft:overworld"; }
    virtual bool        chunkLoaded(int x, int y, int z) { (void)x; (void)y; (void)z; return true; }
    virtual int         redstonePower(int x, int y, int z, bool strong) { (void)x; (void)y; (void)z; (void)strong; return 0; }
    virtual Vec3        spawnPoint() { return {0, 64, 0}; }
    virtual int         minBuildHeight() { return -64; }
    virtual int         maxBuildHeight() { return 320; }
    /// Heightmap: first free y above the highest blocking block in the column (MOTION_BLOCKING ...).
    virtual int height(int x, int z, std::string const& heightmap) {
        (void)heightmap;
        for (int y = maxBuildHeight() - 1; y >= minBuildHeight(); y--) {
            std::string b = blockId(x, y, z);
            if (b != "minecraft:air" && b != "minecraft:cave_air" && b != "minecraft:void_air") return y + 1;
        }
        return minBuildHeight();
    }
    virtual bool canSeeSky(int x, int y, int z) { return height(x, z, "MOTION_BLOCKING") <= y; }
    /// Entities whose bounding box intersects [min, max] (the avatar's own player excluded by the caller as needed).
    virtual std::vector<EntityRef> entities(Vec3 min, Vec3 max) { (void)min; (void)max; return {}; }
    virtual EntityRef entityByUUID(std::string const& uuid) { (void)uuid; return {}; }
    /// Other players by name (the local player is added by the API).
    virtual std::vector<EntityRef> players() { return {}; }
    /// Block raycast (Level.clip). blockMode: COLLIDER OUTLINE VISUAL FALLDAMAGE_RESETTING; fluidMode: NONE SOURCE_ONLY ANY
    /// WATER. Default: voxel walk over blockId() treating every non-air non-fluid block as a full cube.
    virtual BlockHit raycastBlock(Vec3 from, Vec3 to, std::string const& blockMode, std::string const& fluidMode) {
        (void)blockMode;
        BlockHit r;
        Vec3   d   = to - from;
        double len = d.length();
        if (len <= 0) return r;
        int    x = int(std::floor(from.x)), y = int(std::floor(from.y)), z = int(std::floor(from.z));
        int    sx = d.x > 0 ? 1 : -1, sy = d.y > 0 ? 1 : -1, sz = d.z > 0 ? 1 : -1;
        auto   tmax = [](double p, double dd, int c, int s) { return dd == 0 ? 1e30 : ((s > 0 ? c + 1 - p : p - c) / std::abs(dd)); };
        double tx = tmax(from.x, d.x, x, sx), ty = tmax(from.y, d.y, y, sy), tz = tmax(from.z, d.z, z, sz);
        double dx = d.x == 0 ? 1e30 : 1 / std::abs(d.x), dy = d.y == 0 ? 1e30 : 1 / std::abs(d.y), dz = d.z == 0 ? 1e30 : 1 / std::abs(d.z);
        double t = 0;
        int    axis = -1;
        for (int i = 0; i < 4096 && t <= 1.0; i++) {
            std::string b = blockId(x, y, z);
            bool fluid = b == "minecraft:water" || b == "minecraft:lava";
            bool solid = b != "minecraft:air" && b != "minecraft:cave_air" && b != "minecraft:void_air" && !fluid;
            if (fluid && fluidMode != "NONE" && !(fluidMode == "WATER" && b != "minecraft:water")) solid = true;
            if (solid) {
                r.hit = true;
                r.x = x; r.y = y; r.z = z;
                r.pos = from + d * t;
                static char const* faces[3][2] = {{"west", "east"}, {"down", "up"}, {"north", "south"}};
                if (axis == 0) r.face = faces[0][sx < 0 ? 1 : 0];
                else if (axis == 1) r.face = faces[1][sy < 0 ? 1 : 0];
                else if (axis == 2) r.face = faces[2][sz < 0 ? 1 : 0];
                return r;
            }
            if (tx < ty && tx < tz) { t = tx; tx += dx; x += sx; axis = 0; }
            else if (ty < tz) { t = ty; ty += dy; y += sy; axis = 1; }
            else { t = tz; tz += dz; z += sz; axis = 2; }
        }
        // a miss: Figura still returns the "block" at the end point (BlockHitResult.miss, face = Direction.getNearest(from - to))
        r.x = int(std::floor(to.x)); r.y = int(std::floor(to.y)); r.z = int(std::floor(to.z));
        r.pos = to;
        Vec3 b = from - to;
        double ax = std::abs(b.x), ay = std::abs(b.y), az = std::abs(b.z);
        if (ax >= ay && ax >= az) r.face = b.x > 0 ? "east" : "west";
        else if (ay >= az) r.face = b.y > 0 ? "up" : "down";
        else r.face = b.z > 0 ? "south" : "north";
        return r;
    }
    /// Entity raycast (ProjectileUtil.getEntityHitResult). Default: no entities.
    virtual EntityHit raycastEntity(Vec3 from, Vec3 to) { (void)from; (void)to; return {}; }
};

/// A particle spawned by an avatar (particles:newParticle / Particle:spawn).
struct ParticleSpec {
    std::string id;                     // Java particle id + optional args: "minecraft:dust 1 0 0 1"
    Vec3        pos, velocity;
    Vec4        color{1, 1, 1, 1};
    float       scale = 1, gravity = 0, power = 0;
    int         lifetime = -1;          // -1 = particle default
    bool        physics = true, colorSet = false;
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
    /// replace the pixels of a resource-pack texture (path without extension, e.g. "textures/figura/x/t8");
    /// rgba = w*h*4 bytes, top row first. Returns false if the host can't.
    virtual bool    uploadTexture(std::string const& path, int w, int h, uint8_t const* rgba) {
        (void)path; (void)w; (void)h; (void)rgba;
        return false;
    }
    virtual void    pauseSound(int handle) { stopSound(handle); }
    /// a playing sound's parameters changed (setPos/setVolume/setPitch/setLoop on a playing Sound)
    virtual void    updateSound(int handle, Vec3 pos, float volume, float pitch, bool loop) {
        (void)handle; (void)pos; (void)volume; (void)pitch; (void)loop;
    }
    /// is the sound still playing? (non-looping sounds end on their own; `fallback` = what the API believes)
    virtual bool    soundPlaying(int handle, bool fallback) { (void)handle; return fallback; }
    virtual bool    keyDown(std::string const& key) { (void)key; return false; } // "key.keyboard.w" / "key.mouse.left"
    virtual std::string vanillaKeyBinding(std::string const& id) { (void)id; return "key.keyboard.unknown"; }
    virtual IWorldQuery& world() { static IWorldQuery w; return w; }
    virtual std::string  chatText() { return ""; }        // text being typed in the chat box ("" = chat closed)
    virtual bool         chatOpen() { return false; }
    virtual void         setChatText(std::string const& s) { (void)s; }
    virtual void         sendChat(std::string const& s) { (void)s; }
    virtual void         sendCommand(std::string const& s) { sendChat("/" + s); }
    virtual void         appendChatHistory(std::string const& s) { (void)s; }
    virtual void         setActionbar(std::string const& s) { (void)s; }  // plain text (JSON already flattened)
    virtual void         setTitle(std::string const& s) { (void)s; }
    virtual void         setSubtitle(std::string const& s) { (void)s; }
    virtual void         setTitleTimes(int fadeIn, int stay, int fadeOut) { (void)fadeIn; (void)stay; (void)fadeOut; }
    virtual void         clearTitle() {}
    virtual std::string  actionbarText() { return ""; }   // currently shown ("" = none)
    virtual std::string  titleText() { return ""; }
    virtual std::string  subtitleText() { return ""; }
    virtual void         swingArm(bool offhand) { (void)offhand; }
    virtual std::string  clipboard() { return ""; }
    virtual void         setClipboard(std::string const& s) { (void)s; }
    virtual std::string  screen() { return ""; }          // open screen class name ("" = none)
    virtual bool         containerOpen() { return false; }
    virtual int          spawnParticle(ParticleSpec const& p) { (void)p; return 0; } // returns a handle (0 = none)
    virtual void         removeParticle(int handle) { (void)handle; }
    virtual void         removeParticles() {}
    virtual bool         particleExists(std::string const& id) { (void)id; return true; }
    virtual float        fps() { return 60; }
    virtual std::string  version() { return "1.20.1"; }
    virtual int          windowWidth() { return 1920; }
    virtual int          windowHeight() { return 1080; }
    virtual double       guiScale() { return 2; }
    virtual Vec3         mousePos() { return {}; }
    virtual bool         windowFocused() { return true; }
    virtual double       fov() { return 70; }             // options FOV setting
    virtual Vec3         cameraPos() { return {}; }
    virtual Vec3         cameraRot() { return {}; }       // Figura getCameraRot: pitch, yaw, roll (degrees)
    virtual bool         isHudEnabled() { return true; }
    virtual bool         isDebugOverlay() { return false; }
    virtual bool         isFirstPerson() { return false; }
    virtual bool         isCameraBackwards() { return false; } // third-person front view
    virtual std::string  serverBrand() { return "Integrated"; }
    virtual bool         singleplayer() { return true; }
    virtual std::string  serverName() { return "World"; }
    virtual std::string  serverIp() { return ""; }
    virtual std::string  serverMotd() { return ""; }
    virtual std::string  language() { return "en_us"; }
    virtual std::vector<std::string> resourcePacks() { return {"vanilla"}; }
    virtual std::vector<std::string> tabList() { return {}; } // player names (the local player is added by the API)
    virtual int          entityCount() { return 0; }
    virtual bool         avatarUploaded() { return false; }
};

} // namespace bf::figura
