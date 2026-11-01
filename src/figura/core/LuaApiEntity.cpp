// FiguraBridge Lua API: player/user (EntityAPI + LivingEntityAPI + PlayerAPI), ItemStack, world, client, host,
// renderer, nameplate, vanilla_model, sounds, avatar, particles, config - the game-facing APIs (Figura 0.1.6 names).
// Values come from Avatar::player / IHost; Bedrock specifics are converted by the host before they get here.
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <new>

#include "figura/core/Avatar.h"
#include "figura/core/LuaApi.h"
#include "figura/core/LuaBind.h"
#include "lyrics/core/Json.h"

namespace bf::figura {

using namespace lua;

namespace {
uint32_t tPlayer = 0, tItem = 0, tEntity = 0, tWorld = 0, tClient = 0, tHost = 0, tRenderer = 0, tVanillaPart = 0,
         tNameplate = 0, tSounds = 0, tSound = 0, tAvatarAPI = 0, tBlock = 0, tParticles = 0;

Avatar*            av(lua_State* L) { return avatarOf(L); }
PlayerState const& P(lua_State* L) { return avatarOf(L)->player; }
int                ret1(lua_State* L) { lua_settop(L, 1); return 1; }
int                pushB(lua_State* L, bool b) { lua_pushboolean(L, b); return 1; }
int                pushN(lua_State* L, double d) { lua_pushnumber(L, d); return 1; }
int                pushS(lua_State* L, std::string const& s) { lua_pushstring(L, s.c_str()); return 1; }
float              lerpf(float a, float b, float t) { return a + (b - a) * t; }

// ---------------------------------------------------------------- ItemStack (value userdata)
struct ItemUD {
    ItemState it;
};
void pushItem(lua_State* L, ItemState const& it) {
    void* m = lua_newuserdata(L, sizeof(ItemUD));
    new (m) ItemUD{it};
    luaL_setmetatable(L, (std::string("fb:") + typeName(tItem)).c_str());
}
ItemState& item(lua_State* L, char const* fn) {
    void* p = luaL_testudata(L, 1, (std::string("fb:") + typeName(tItem)).c_str());
    if (!p) luaL_error(L, "bad argument 1 to '%s' (ItemStack expected)", fn);
    return static_cast<ItemUD*>(p)->it;
}
int itGC(lua_State* L) { static_cast<ItemUD*>(lua_touserdata(L, 1))->~ItemUD(); return 0; }
int itGetID(lua_State* L) { return pushS(L, item(L, "getID").id); }
int itGetCount(lua_State* L) { lua_pushinteger(L, item(L, "getCount").count); return 1; }
int itGetName(lua_State* L) { auto& i = item(L, "getName"); return pushS(L, i.name.empty() ? i.id : i.name); }
int itGetUseAction(lua_State* L) { return pushS(L, item(L, "getUseAction").useAction); }
int itIsTool(lua_State* L) { return pushB(L, item(L, "isTool").isTool); }
int itIsArmor(lua_State* L) { return pushB(L, item(L, "isArmor").isArmor); }
int itIsFood(lua_State* L) { return pushB(L, item(L, "isFood").isFood); }
int itIsBlockItem(lua_State* L) { return pushB(L, item(L, "isBlockItem").isBlockItem); }
int itGetUseDuration(lua_State* L) { lua_pushinteger(L, item(L, "getUseDuration").maxUseDuration); return 1; }
int itGetTag(lua_State* L) {
    auto& i = item(L, "getTag");
    lua_newtable(L);
    if (i.charged) { lua_pushboolean(L, 1); lua_setfield(L, -2, "Charged"); }
    return 1;
}
int itGetTags(lua_State* L) { return pushStrings(L, item(L, "getTags").tags); }
int itIsEnchanted(lua_State* L) { (void)item(L, "isEnchanted"); return pushB(L, false); }
int itGetMaxCount(lua_State* L) { (void)item(L, "getMaxCount"); lua_pushinteger(L, 64); return 1; }
int itGetDamage(lua_State* L) { (void)item(L, "getDamage"); lua_pushinteger(L, 0); return 1; }
int itGetRarity(lua_State* L) { (void)item(L, "getRarity"); return pushS(L, "COMMON"); }
int itToStackString(lua_State* L) { auto& i = item(L, "toStackString"); return pushS(L, i.id + (i.count > 1 ? " " + std::to_string(i.count) : "")); }
int itCopy(lua_State* L) { pushItem(L, item(L, "copy")); return 1; }
int itEquipSlot(lua_State* L) { auto& i = item(L, "getEquipmentSlot"); return pushS(L, i.isArmor ? "HEAD" : "MAINHAND"); }
int itIndex(lua_State* L) {
    auto& i = item(L, "__index");
    std::string k = lua_isstring(L, 2) ? lua_tostring(L, 2) : "";
    if (k == "id") return pushS(L, i.id);
    if (k == "tag") return itGetTag(L);
    return 0;
}
int itToString(lua_State* L) { auto& i = item(L, "__tostring"); return pushS(L, i.id + " (ItemStack)"); }
int itEq(lua_State* L) {
    void* a = luaL_testudata(L, 1, (std::string("fb:") + typeName(tItem)).c_str());
    void* b = luaL_testudata(L, 2, (std::string("fb:") + typeName(tItem)).c_str());
    return pushB(L, a && b && static_cast<ItemUD*>(a)->it.id == static_cast<ItemUD*>(b)->it.id &&
                        static_cast<ItemUD*>(a)->it.count == static_cast<ItemUD*>(b)->it.count);
}

// ---------------------------------------------------------------- other entities (vehicle): value userdata
struct EntUD {
    EntityRef e;
};
void pushEntity(lua_State* L, EntityRef const& e) {
    if (!e.valid) { lua_pushnil(L); return; }
    void* m = lua_newuserdata(L, sizeof(EntUD));
    new (m) EntUD{e};
    luaL_setmetatable(L, (std::string("fb:") + typeName(tEntity)).c_str());
}
EntityRef& ent(lua_State* L, char const* fn) {
    void* p = luaL_testudata(L, 1, (std::string("fb:") + typeName(tEntity)).c_str());
    if (!p) luaL_error(L, "bad argument 1 to '%s' (EntityAPI expected)", fn);
    return static_cast<EntUD*>(p)->e;
}
int enGC(lua_State* L) { static_cast<EntUD*>(lua_touserdata(L, 1))->~EntUD(); return 0; }
int enGetType(lua_State* L) { return pushS(L, ent(L, "getType").type); }
int enGetPos(lua_State* L) { pushVec3(L, ent(L, "getPos").pos); return 1; }
int enGetRot(lua_State* L) { auto& e = ent(L, "getRot"); pushVec2(L, e.rot.x, e.rot.y); return 1; }
int enGetUUID(lua_State* L) { return pushS(L, ent(L, "getUUID").uuid); }
int enIsLoaded(lua_State* L) { (void)ent(L, "isLoaded"); return pushB(L, true); }
int enGetName(lua_State* L) { return pushS(L, ent(L, "getName").type); }
int enFalse(lua_State* L) { return pushB(L, false); }
int enNil(lua_State* L) { lua_pushnil(L); return 1; }
int enToString(lua_State* L) { return pushS(L, ent(L, "__tostring").type + " (Entity)"); }

// ---------------------------------------------------------------- player
int plGetPos(lua_State* L) {
    auto& p = P(L);
    if (lua_isnumber(L, 2)) {
        float d = float(lua_tonumber(L, 2));
        pushVec3(L, {p.prevPos.x + (p.pos.x - p.prevPos.x) * d, p.prevPos.y + (p.pos.y - p.prevPos.y) * d,
                     p.prevPos.z + (p.pos.z - p.prevPos.z) * d});
    } else pushVec3(L, p.pos);
    return 1;
}
int plGetRot(lua_State* L) {
    auto& p = P(L);
    float d = lua_isnumber(L, 2) ? float(lua_tonumber(L, 2)) : 1.0f;
    pushVec2(L, lerpf(p.prevPitch, p.pitch, d), lerpf(p.prevYaw, p.yaw, d));
    return 1;
}
int plGetVelocity(lua_State* L) { pushVec3(L, P(L).velocity); return 1; }
int plGetLookDir(lua_State* L) {
    auto& p = P(L);
    double pi = toRad(p.pitch), ya = toRad(p.yaw);
    pushVec3(L, {-std::sin(ya) * std::cos(pi), -std::sin(pi), std::cos(ya) * std::cos(pi)});
    return 1;
}
int plGetBodyYaw(lua_State* L) {
    auto& p = P(L);
    float d = lua_isnumber(L, 2) ? float(lua_tonumber(L, 2)) : 1.0f;
    return pushN(L, lerpf(p.prevBodyYaw, p.bodyYaw, d));
}
int plGetEyeHeight(lua_State* L) { return pushN(L, P(L).eyeHeight); }
int plGetEyePos(lua_State* L) { auto& p = P(L); pushVec3(L, {p.pos.x, p.pos.y + p.eyeHeight, p.pos.z}); return 1; }
int plGetName(lua_State* L) { return pushS(L, P(L).name); }
int plGetUUID(lua_State* L) { return pushS(L, P(L).uuid); }
int plGetType(lua_State* L) { (void)L; return pushS(L, "minecraft:player"); }
int plIsLoaded(lua_State* L) { return pushB(L, P(L).loaded); }
int plGetPose(lua_State* L) { return pushS(L, P(L).pose); }
#define PB(fn, field) int fn(lua_State* L) { return pushB(L, P(L).field); }
#define PN(fn, field) int fn(lua_State* L) { return pushN(L, P(L).field); }
PB(plIsOnGround, onGround) PB(plIsSprinting, sprinting) PB(plIsCrouching, crouching) PB(plIsClimbing, climbing)
PB(plIsBlocking, blocking) PB(plIsFishing, fishing) PB(plIsSwimming, swimming) PB(plIsGliding, gliding)
PB(plIsInWater, inWater) PB(plIsUnderwater, underwater) PB(plIsInLava, inLava) PB(plIsOnFire, onFire)
PB(plIsInvisible, invisible) PB(plIsAlive, alive) PB(plIsUsingItem, usingItem) PB(plIsFlying, flying)
PB(plIsSleeping, sleeping) PB(plIsSwinging, swinging) PB(plIsLeftHanded, leftHanded)
PN(plGetHealth, health) PN(plGetMaxHealth, maxHealth) PN(plGetFood, food) PN(plGetSaturation, saturation)
PN(plGetArmor, armor) PN(plGetAbsorption, absorption) PN(plGetExpLevel, experienceLevel)
PN(plGetExpProgress, experienceProgress) PN(plGetHurtTime, hurtTime) PN(plGetDeathTime, deathTime)
PN(plGetSwingTime, swingTime) PN(plGetAir, airSupply) PN(plGetMaxAir, maxAir) PN(plGetAge, age)
#undef PB
#undef PN
int plIsWet(lua_State* L) { auto& p = P(L); return pushB(L, p.inWater || p.underwater); }
int plIsInRain(lua_State* L) { (void)L; return pushB(L, false); }
int plGetHeldItem(lua_State* L) { auto& p = P(L); pushItem(L, optBool(L, 2, false) ? p.offHand : p.mainHand); return 1; }
int plGetActiveItem(lua_State* L) { pushItem(L, P(L).activeItem); return 1; }
int plGetActiveHand(lua_State* L) { return pushS(L, P(L).activeHandOff ? "OFF_HAND" : "MAIN_HAND"); }
int plGetSwingArm(lua_State* L) {
    auto& p = P(L);
    if (!p.swinging) { lua_pushnil(L); return 1; }
    return pushS(L, p.swingOffhand ? "OFF_HAND" : "MAIN_HAND");
}
int plGetActiveItemTime(lua_State* L) { auto& p = P(L); lua_pushinteger(L, p.usingItem ? p.activeItem.maxUseDuration - p.useItemRemaining : 0); return 1; }
int plGetItem(lua_State* L) {
    auto& p = P(L);
    int   s = int(optNum(L, 2, 1));
    switch (s) {
    case 1: pushItem(L, p.mainHand); break;
    case 2: pushItem(L, p.offHand); break;
    case 3: pushItem(L, p.boots); break;
    case 4: pushItem(L, p.legs); break;
    case 5: pushItem(L, p.chest); break;
    case 6: pushItem(L, p.helmet); break;
    default: pushItem(L, ItemState{});
    }
    return 1;
}
int plGetVehicle(lua_State* L) { pushEntity(L, P(L).vehicle); return 1; }
int plGetPassengers(lua_State* L) { lua_newtable(L); return 1; }
int plGetDimension(lua_State* L) { return pushS(L, P(L).dimension); }
int plGetGamemode(lua_State* L) { return pushS(L, P(L).gamemode); }
int plIsCreative(lua_State* L) { return pushB(L, P(L).gamemode == "CREATIVE"); }
int plHasAvatar(lua_State* L) { (void)L; return pushB(L, true); }
int plGetNbt(lua_State* L) { lua_newtable(L); return 1; }
int plGetBoundingBox(lua_State* L) { (void)L; pushVec3(L, {0.6, 1.8, 0.6}); return 1; }
int plGetVariable(lua_State* L) {
    // other avatars' avatar:store() values: we only know our own -> registry "fb.store"
    lua_getfield(L, LUA_REGISTRYINDEX, "fb.store");
    if (lua_isnoneornil(L, 2)) return 1;
    lua_pushvalue(L, 2);
    lua_gettable(L, -2);
    return 1;
}
int plGetTargetedBlock(lua_State* L) { lua_pushnil(L); return 1; }
int plGetTargetedEntity(lua_State* L) { lua_pushnil(L); return 1; }
int plGetFrozenTicks(lua_State* L) { lua_pushinteger(L, 0); (void)L; return 1; }
int plIsVisuallySwimming(lua_State* L) { auto& p = P(L); return pushB(L, p.swimming); }
int plRiptide(lua_State* L) { (void)L; return pushB(L, false); }
int plGetPermissionLevel(lua_State* L) { lua_pushinteger(L, 4); (void)L; return 1; }
int plGetChargedAttackDelay(lua_State* L) { (void)L; return pushN(L, 12.5); }
int plIsSneaking(lua_State* L) { return pushB(L, P(L).crouching); }
int plGetTeamInfo(lua_State* L) { lua_pushnil(L); return 1; }
int plGetModelType(lua_State* L) { (void)L; return pushS(L, "DEFAULT"); }
int plIsSkinLayerVisible(lua_State* L) { (void)L; return pushB(L, true); }
int plToString(lua_State* L) { return pushS(L, P(L).name + " (Player)"); }
int plGetPlayerName(lua_State* L) { return plGetName(L); }

// ---------------------------------------------------------------- world
struct BlockUD { std::string id; int x, y, z; };
void pushBlock(lua_State* L, std::string const& id, int x, int y, int z) {
    void* m = lua_newuserdata(L, sizeof(BlockUD));
    new (m) BlockUD{id, x, y, z};
    luaL_setmetatable(L, (std::string("fb:") + typeName(tBlock)).c_str());
}
BlockUD& blk(lua_State* L) {
    void* p = luaL_testudata(L, 1, (std::string("fb:") + typeName(tBlock)).c_str());
    if (!p) luaL_error(L, "BlockState expected");
    return *static_cast<BlockUD*>(p);
}
int blGC(lua_State* L) { static_cast<BlockUD*>(lua_touserdata(L, 1))->~BlockUD(); return 0; }
int blGetID(lua_State* L) { return pushS(L, blk(L).id); }
int blIsAir(lua_State* L) { auto& b = blk(L); return pushB(L, b.id == "minecraft:air" || b.id == "minecraft:cave_air" || b.id == "minecraft:void_air"); }
int blGetPos(lua_State* L) { auto& b = blk(L); pushVec3(L, {double(b.x), double(b.y), double(b.z)}); return 1; }
int blIsSolid(lua_State* L) { auto& b = blk(L); return pushB(L, b.id != "minecraft:air" && b.id != "minecraft:water" && b.id != "minecraft:lava"); }
int blGetProps(lua_State* L) { lua_newtable(L); return 1; }
int blIndex(lua_State* L) { auto& b = blk(L); if (lua_isstring(L, 2) && std::string(lua_tostring(L, 2)) == "id") return pushS(L, b.id); return 0; }
int blToString(lua_State* L) { return pushS(L, blk(L).id + " (BlockState)"); }

Vec3 posArg(lua_State* L, int idx) { return parseVec3(L, idx, "world"); }
int wLight(lua_State* L) { Vec3 p = posArg(L, 1); lua_pushinteger(L, av(L)->host->world().lightLevel(int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z)))); return 1; }
int wSky(lua_State* L) { Vec3 p = posArg(L, 1); lua_pushinteger(L, av(L)->host->world().skyLight(int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z)))); return 1; }
int wBlockLight(lua_State* L) { Vec3 p = posArg(L, 1); lua_pushinteger(L, av(L)->host->world().blockLight(int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z)))); return 1; }
int wBlockState(lua_State* L) {
    Vec3 p = posArg(L, 1);
    int  x = int(std::floor(p.x)), y = int(std::floor(p.y)), z = int(std::floor(p.z));
    pushBlock(L, av(L)->host->world().blockId(x, y, z), x, y, z);
    return 1;
}
int wBiome(lua_State* L) { Vec3 p = posArg(L, 1); return pushS(L, av(L)->host->world().biome(int(p.x), int(p.y), int(p.z))); }
int wTime(lua_State* L) { return pushN(L, double(av(L)->host->world().time())); }
int wTimeOfDay(lua_State* L) { return pushN(L, double(av(L)->host->world().dayTime())); }
int wDayTime(lua_State* L) { return pushN(L, double(av(L)->host->world().dayTime() % 24000)); }
int wDimension(lua_State* L) { return pushS(L, av(L)->host->world().dimension()); }
int wRain(lua_State* L) { return pushN(L, av(L)->host->world().rainGradient()); }
int wThunder(lua_State* L) { return pushB(L, av(L)->host->world().thundering()); }
int wMoon(lua_State* L) { lua_pushinteger(L, int((av(L)->host->world().dayTime() / 24000) % 8)); return 1; }
int wExists(lua_State* L) { return pushB(L, P(L).loaded); }
int wPlayers(lua_State* L) {
    lua_newtable(L);
    lua_getglobal(L, "player");
    lua_setfield(L, -2, P(L).name.c_str());
    return 1;
}
int wEmpty(lua_State* L) { lua_newtable(L); return 1; }

// ---------------------------------------------------------------- client
int cFPS(lua_State* L) { lua_pushinteger(L, int(av(L)->host->fps())); return 1; }
int cPaused(lua_State* L) { return pushB(L, av(L)->host->paused()); }
int cVersion(lua_State* L) { return pushS(L, av(L)->host->version()); }
int cFiguraVersion(lua_State* L) { (void)L; return pushS(L, "0.1.6+1.20.1"); }
int cCompareVersions(lua_State* L) {
    auto parse = [](std::string s) {
        std::vector<int> v;
        size_t i = 0;
        while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.')) i++;
        s = s.substr(0, i);
        size_t p = 0;
        while (p <= s.size()) {
            size_t q = s.find('.', p);
            if (q == std::string::npos) q = s.size();
            v.push_back(q > p ? std::atoi(s.substr(p, q - p).c_str()) : 0);
            p = q + 1;
        }
        return v;
    };
    auto a = parse(luaL_checkstring(L, 1)), b = parse(luaL_checkstring(L, 2));
    size_t n = std::max(a.size(), b.size());
    for (size_t i = 0; i < n; i++) {
        int x = i < a.size() ? a[i] : 0, y = i < b.size() ? b[i] : 0;
        if (x != y) { lua_pushinteger(L, x < y ? -1 : 1); return 1; }
    }
    lua_pushinteger(L, 0);
    return 1;
}
int cSystemTime(lua_State* L) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return pushN(L, double(ms));
}
int cWindowSize(lua_State* L) { auto* h = av(L)->host; pushVec2(L, h->windowWidth(), h->windowHeight()); return 1; }
int cHud(lua_State* L) { return pushB(L, av(L)->host->isHudEnabled()); }
int cCameraPos(lua_State* L) { pushVec3(L, av(L)->host->cameraPos()); return 1; }
int cCameraRot(lua_State* L) { pushVec3(L, av(L)->host->cameraRot()); return 1; }
int cServerBrand(lua_State* L) { return pushS(L, av(L)->host->serverBrand()); }
int cSingleplayer(lua_State* L) { (void)L; return pushB(L, true); }
int cChatText(lua_State* L) { return pushS(L, av(L)->host->chatText()); }
int cLang(lua_State* L) { (void)L; return pushS(L, "en_us"); }
int cFalse(lua_State* L) { (void)L; return pushB(L, false); }
int cTrue(lua_State* L) { (void)L; return pushB(L, true); }
int cDate(lua_State* L) {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    lua_newtable(L);
    lua_pushinteger(L, tm.tm_year + 1900); lua_setfield(L, -2, "year");
    lua_pushinteger(L, tm.tm_mon + 1); lua_setfield(L, -2, "month");
    lua_pushinteger(L, tm.tm_mday); lua_setfield(L, -2, "day");
    lua_pushinteger(L, tm.tm_hour); lua_setfield(L, -2, "hour");
    lua_pushinteger(L, tm.tm_min); lua_setfield(L, -2, "minute");
    lua_pushinteger(L, tm.tm_sec); lua_setfield(L, -2, "second");
    lua_pushinteger(L, tm.tm_yday + 1); lua_setfield(L, -2, "year_day");
    lua_pushinteger(L, tm.tm_wday + 1); lua_setfield(L, -2, "week_day");
    return 1;
}
int cTextWidth(lua_State* L) { size_t n = 0; luaL_checklstring(L, 1, &n); lua_pushinteger(L, int(n) * 6); return 1; }
int cFrameTime(lua_State* L) { return pushN(L, av(L)->tickDelta); }
int cMousePos(lua_State* L) { (void)L; pushVec2(L, 0, 0); return 1; }
int cGuiScale(lua_State* L) { (void)L; return pushN(L, 2); }

// ---------------------------------------------------------------- host
int hIsHost(lua_State* L) { (void)L; return pushB(L, true); }
int hIsJumping(lua_State* L) { return pushB(L, P(L).jumping); }
int hIsFlying(lua_State* L) { return pushB(L, P(L).flying); }
int hSendChat(lua_State* L) { av(L)->host->sendChat(luaL_checkstring(L, 2)); return ret1(L); }
int hActionbar(lua_State* L) { av(L)->host->setActionbar(optStr(L, 2, "")); return ret1(L); }
int hChain(lua_State* L) { return ret1(L); }
int hGetSlot(lua_State* L) { pushItem(L, P(L).mainHand); return 1; }
int hAttackCharge(lua_State* L) { (void)L; return pushN(L, 1); }
int hGetScreen(lua_State* L) { lua_pushnil(L); return 1; }
int hStatus(lua_State* L) { lua_newtable(L); return 1; }

// ---------------------------------------------------------------- renderer: setters stored, getters read back
int rIndex(lua_State* L) {
    std::string k = luaL_checkstring(L, 2);
    Avatar*     a = av(L);
    if (k.rfind("set", 0) == 0 || k.rfind("offset", 0) == 0) {
        lua_pushstring(L, k.c_str());
        lua_pushcclosure(L, [](lua_State* L2) {
            std::string key = lua_tostring(L2, lua_upvalueindex(1));
            Avatar*     a2  = avatarOf(L2);
            if (key == "setShadowRadius") a2->shadowRadius = lua_isnoneornil(L2, 2) ? -1.0f : float(lua_tonumber(L2, 2));
            a2->renderer[key.substr(3)] = lua_isnoneornil(L2, 2) ? "" : (lua_isstring(L2, 2) ? lua_tostring(L2, 2) : (lua_toboolean(L2, 2) ? "true" : "false"));
            lua_settop(L2, 1);
            return 1;
        }, 1);
        return 1;
    }
    if (k == "isFirstPerson") { lua_pushcfunction(L, [](lua_State* L2) { return pushB(L2, avatarOf(L2)->host->isFirstPerson()); }); return 1; }
    if (k == "isCameraBackwards") { lua_pushcfunction(L, cFalse); return 1; }
    if (k == "getShadowRadius") { lua_pushcfunction(L, [](lua_State* L2) { float r = avatarOf(L2)->shadowRadius; if (r < 0) lua_pushnil(L2); else lua_pushnumber(L2, r); return 1; }); return 1; }
    if (k.rfind("get", 0) == 0 || k.rfind("should", 0) == 0) {
        lua_pushstring(L, k.substr(k.rfind("get", 0) == 0 ? 3 : 6).c_str());
        lua_pushcclosure(L, [](lua_State* L2) {
            Avatar* a2 = avatarOf(L2);
            auto    it = a2->renderer.find(lua_tostring(L2, lua_upvalueindex(1)));
            if (it == a2->renderer.end() || it->second.empty()) lua_pushnil(L2);
            else if (it->second == "true" || it->second == "false") lua_pushboolean(L2, it->second == "true");
            else lua_pushstring(L2, it->second.c_str());
            return 1;
        }, 1);
        return 1;
    }
    (void)a;
    return 0;
}
int rNewIndex(lua_State* L) {
    std::string k = luaL_checkstring(L, 2);
    av(L)->renderer[k] = lua_isstring(L, 3) ? lua_tostring(L, 3) : (lua_toboolean(L, 3) ? "true" : "false");
    return 0;
}

// ---------------------------------------------------------------- vanilla_model
struct VPart {
    std::string name;
    bool        visible = true;
    Vec3        pos, rot, scale{1, 1, 1}, offsetRot;
    std::vector<std::string> children;
};
std::vector<std::unique_ptr<VPart>>& vparts(lua_State* L) {
    static std::map<lua_State*, std::vector<std::unique_ptr<VPart>>> m;
    return m[L];
}
VPart* vp(lua_State* L, char const* fn) { return check<VPart>(L, 1, tVanillaPart, fn); }
void   vSetVis(Avatar* a, VPart* p, bool v, std::map<std::string, VPart*> const& byName) {
    p->visible             = v;
    a->vanillaVisible[p->name] = v;
    for (auto& c : p->children)
        if (auto it = byName.find(c); it != byName.end()) vSetVis(a, it->second, v, byName);
}
int vpSetVisible(lua_State* L) {
    VPart* p = vp(L, "setVisible");
    std::map<std::string, VPart*> byName;
    for (auto& q : vparts(L)) byName[q->name] = q.get();
    vSetVis(av(L), p, lua_isnoneornil(L, 2) ? true : lua_toboolean(L, 2) != 0, byName);
    return ret1(L);
}
int vpGetVisible(lua_State* L) { return pushB(L, vp(L, "getVisible")->visible); }
int vpSetPos(lua_State* L) { vp(L, "setPos")->pos = parseVec3(L, 2, "setPos"); return ret1(L); }
int vpSetRot(lua_State* L) { vp(L, "setRot")->rot = parseVec3(L, 2, "setRot"); return ret1(L); }
int vpSetOffsetRot(lua_State* L) { vp(L, "setOffsetRot")->offsetRot = parseVec3(L, 2, "setOffsetRot"); return ret1(L); }
int vpSetScale(lua_State* L) { vp(L, "setScale")->scale = parseVec3(L, 2, "setScale", 1); return ret1(L); }
int vpGetPos(lua_State* L) { pushVec3(L, vp(L, "getPos")->pos); return 1; }
int vpGetRot(lua_State* L) { pushVec3(L, vp(L, "getRot")->rot); return 1; }
int vpGetScale(lua_State* L) { pushVec3(L, vp(L, "getScale")->scale); return 1; }
int vpGetOriginRot(lua_State* L) {
    VPart* p = vp(L, "getOriginRot");
    auto&  vpose = av(L)->vanillaPose;
    VanillaPart const* d = nullptr;
    if (p->name == "HEAD" || p->name == "HAT") d = &vpose.head;
    else if (p->name == "BODY" || p->name == "JACKET") d = &vpose.body;
    else if (p->name == "LEFT_ARM" || p->name == "LEFT_SLEEVE") d = &vpose.leftArm;
    else if (p->name == "RIGHT_ARM" || p->name == "RIGHT_SLEEVE") d = &vpose.rightArm;
    else if (p->name == "LEFT_LEG" || p->name == "LEFT_PANTS") d = &vpose.leftLeg;
    else if (p->name == "RIGHT_LEG" || p->name == "RIGHT_PANTS") d = &vpose.rightLeg;
    pushVec3(L, d ? d->rot : Vec3{});
    return 1;
}
int vpGetOriginPos(lua_State* L) { (void)vp(L, "getOriginPos"); pushVec3(L, {}); return 1; }
int vpGetOriginScale(lua_State* L) { (void)vp(L, "getOriginScale"); pushVec3(L, {1, 1, 1}); return 1; }
int vpGetName(lua_State* L) { return pushS(L, vp(L, "getName")->name); }
int vpChain(lua_State* L) { return ret1(L); }
int vpToString(lua_State* L) { return pushS(L, vp(L, "__tostring")->name + " (VanillaModelPart)"); }

// ---------------------------------------------------------------- nameplate
struct NPart { std::string name, text; bool visible = true; };
NPart* np(lua_State* L, char const* fn) { return check<NPart>(L, 1, tNameplate, fn); }
int npSetVisible(lua_State* L) { np(L, "setVisible")->visible = lua_toboolean(L, 2) != 0; return ret1(L); }
int npIsVisible(lua_State* L) { return pushB(L, np(L, "isVisible")->visible); }
int npSetText(lua_State* L) { np(L, "setText")->text = optStr(L, 2, ""); return ret1(L); }
int npGetText(lua_State* L) { return pushS(L, np(L, "getText")->text); }
int npChain(lua_State* L) { return ret1(L); }

// ---------------------------------------------------------------- sounds
struct SoundObj {
    std::string id;
    bool        custom = false;
    Vec3        pos;
    float       volume = 1, pitch = 1;
    bool        loop = false, playing = false;
    int         handle = 0;
};
int gSoundHandle = 1;
bool isCustomSound(Avatar* a, std::string const& id) { return a->soundEvents.count(id) > 0; }
SoundObj* so(lua_State* L, char const* fn) { return check<SoundObj>(L, 1, tSound, fn); }
std::vector<std::unique_ptr<SoundObj>>& soundPool(lua_State* L) {
    static std::map<lua_State*, std::vector<std::unique_ptr<SoundObj>>> m;
    return m[L];
}
SoundObj* newSound(lua_State* L, std::string const& id) {
    auto s    = std::make_unique<SoundObj>();
    s->id     = id;
    s->custom = isCustomSound(av(L), id);
    s->pos    = P(L).pos;
    s->handle = gSoundHandle++;
    auto& pool = soundPool(L);
    if (pool.size() > 512) pool.erase(pool.begin(), pool.begin() + 256);
    pool.push_back(std::move(s));
    return pool.back().get();
}
int sPlay(lua_State* L) {
    SoundObj* s = so(L, "play");
    s->playing  = true;
    av(L)->host->playSound(s->id, s->custom, s->pos, s->volume, s->pitch, s->loop, s->handle);
    return ret1(L);
}
int sStop(lua_State* L) { SoundObj* s = so(L, "stop"); s->playing = false; av(L)->host->stopSound(s->handle); return ret1(L); }
int sSetPos(lua_State* L) { so(L, "setPos")->pos = parseVec3(L, 2, "setPos"); return ret1(L); }
int sSetVolume(lua_State* L) { so(L, "setVolume")->volume = float(optNum(L, 2, 1)); return ret1(L); }
int sSetPitch(lua_State* L) { so(L, "setPitch")->pitch = float(optNum(L, 2, 1)); return ret1(L); }
int sSetLoop(lua_State* L) { so(L, "setLoop")->loop = optBool(L, 2, false); return ret1(L); }
int sIsPlaying(lua_State* L) { return pushB(L, so(L, "isPlaying")->playing); }
int sGetPos(lua_State* L) { pushVec3(L, so(L, "getPos")->pos); return 1; }
int sGetVolume(lua_State* L) { return pushN(L, so(L, "getVolume")->volume); }
int sGetPitch(lua_State* L) { return pushN(L, so(L, "getPitch")->pitch); }
int sChain(lua_State* L) { return ret1(L); }
int sToString(lua_State* L) { return pushS(L, so(L, "__tostring")->id + " (Sound)"); }
int sndPlaySound(lua_State* L) {
    // sounds:playSound(id, pos, volume, pitch, loop) ; pos may be x,y,z numbers
    std::string id = luaL_checkstring(L, 2);
    SoundObj*   s  = newSound(L, id);
    int         i  = 3;
    if (toVec(L, 3, nullptr)) { s->pos = parseVec3(L, 3, "playSound"); i = 4; }
    else if (lua_isnumber(L, 3) && lua_isnumber(L, 4) && lua_isnumber(L, 5)) { s->pos = parseVec3(L, 3, "playSound"); i = 6; }
    s->volume = float(optNum(L, i, 1));
    s->pitch  = float(optNum(L, i + 1, 1));
    s->loop   = optBool(L, i + 2, false);
    s->playing = true;
    av(L)->host->playSound(s->id, s->custom, s->pos, s->volume, s->pitch, s->loop, s->handle);
    pushRef(L, s, tSound);
    return 1;
}
int sndStopSound(lua_State* L) {
    Avatar* a = av(L);
    if (lua_isnoneornil(L, 2)) a->host->stopSound(-1);
    else {
        std::string id = luaL_checkstring(L, 2);
        for (auto& s : soundPool(L))
            if (s->id == id && s->playing) { s->playing = false; a->host->stopSound(s->handle); }
    }
    return ret1(L);
}
int sndIndex(lua_State* L) {
    if (!lua_isstring(L, 2)) return 0;
    pushRef(L, newSound(L, lua_tostring(L, 2)), tSound);
    return 1;
}
int sndIsPresent(lua_State* L) { (void)L; return pushB(L, true); }
int sndGetCustom(lua_State* L) {
    lua_newtable(L);
    int i = 1;
    for (auto& [k, v] : av(L)->soundEvents) { lua_pushstring(L, k.c_str()); lua_rawseti(L, -2, i++); }
    return 1;
}

// ---------------------------------------------------------------- avatar
void pushJson(lua_State* L, lyrics::json::Value const& v) {
    using T = lyrics::json::Value::Type;
    switch (v.type()) {
    case T::Null: lua_pushnil(L); break;
    case T::Bool: lua_pushinteger(L, v.asBool() ? 1 : 0); break; // NBT ByteTag
    case T::Number: lua_pushnumber(L, v.asNumber()); break;
    case T::String: lua_pushstring(L, v.asString().c_str()); break;
    case T::Array:
        lua_createtable(L, int(v.size()), 0);
        for (size_t i = 0; i < v.size(); i++) { pushJson(L, v[i]); lua_rawseti(L, -2, int(i + 1)); }
        break;
    case T::Object:
        if (v.has("__file")) { lua_pushstring(L, v["__file"].asString().c_str()); break; }
        if (v.has("__float")) { lua_pushnumber(L, std::strtod(v["__float"].str().c_str(), nullptr)); break; }
        lua_newtable(L);
        for (auto const& [k, x] : v.members()) { pushJson(L, x); lua_setfield(L, -2, k.c_str()); }
        break;
    }
}
int aGetNBT(lua_State* L) {
    Avatar* a = av(L);
    if (!a->nbtRef) {
        if (a->nbtJson) pushJson(L, *a->nbtJson);
        else lua_newtable(L);
        a->nbtRef = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, a->nbtRef);
    return 1;
}
int aGetName(lua_State* L) { return pushS(L, av(L)->name); }
int aGetAuthors(lua_State* L) { return pushS(L, av(L)->author); }
int aGetColor(lua_State* L) { return pushS(L, av(L)->color); }
int aSetColor(lua_State* L) { (void)L; return ret1(L); }
int aGetVersion(lua_State* L) { (void)L; return pushS(L, "0.1.6+1.20.1"); }
int aGetUUID(lua_State* L) { return pushS(L, P(L).uuid); }
int aGetEntityName(lua_State* L) { return pushS(L, P(L).name); }
int aStore(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "fb.store");
    lua_pushvalue(L, 2);
    lua_pushvalue(L, 3);
    lua_settable(L, -3);
    return ret1(L);
}
int aTrue(lua_State* L) { (void)L; return pushB(L, true); }
int aPermission(lua_State* L) { (void)L; return pushS(L, "MAX"); }
int aZero(lua_State* L) { lua_pushinteger(L, 0); (void)L; return 1; }
int aMaxInst(lua_State* L) { lua_pushinteger(L, lua_Integer(av(L)->instructionLimit)); return 1; }
int aCurInst(lua_State* L) { lua_pushinteger(L, lua_Integer(av(L)->instructionsUsed)); return 1; }

// ---------------------------------------------------------------- particles (not on Bedrock yet: accepted)
int ptIndex(lua_State* L) {
    lua_pushcfunction(L, [](lua_State* L2) { lua_settop(L2, 1); return 1; });
    return 1;
}

void registerSingleton(lua_State* L, char const* global, TypeDef const& d, uint32_t& tid, void* anchor) {
    tid = registerType(L, d);
    pushRef(L, anchor, tid);
    lua_setglobal(L, global);
}
} // namespace

void openEntityApis(Avatar& a) {
    lua_State* L = a.L;
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "fb.store");

    { // ItemStack
        TypeDef d;
        d.name    = "ItemStack";
        d.methods = {{"getID", itGetID}, {"getCount", itGetCount}, {"getName", itGetName}, {"getUseAction", itGetUseAction},
                     {"isTool", itIsTool}, {"isArmor", itIsArmor}, {"isFood", itIsFood}, {"isBlockItem", itIsBlockItem},
                     {"getUseDuration", itGetUseDuration}, {"getTag", itGetTag}, {"getTags", itGetTags},
                     {"isEnchanted", itIsEnchanted}, {"getMaxCount", itGetMaxCount}, {"getDamage", itGetDamage},
                     {"getMaxDamage", itGetDamage}, {"isDamageable", itIsEnchanted}, {"getRarity", itGetRarity},
                     {"toStackString", itToStackString}, {"copy", itCopy}, {"getEquipmentSlot", itEquipSlot},
                     {"isStackable", itIsEnchanted}, {"getRepairCost", itGetDamage}, {"getPopTime", itGetDamage},
                     {"hasGlint", itIsEnchanted}, {"getBlockstate", enNil}};
        d.index    = itIndex;
        d.tostring = itToString;
        d.meta     = {{"__gc", itGC}, {"__eq", itEq}};
        tItem      = registerType(L, d);
    }
    { // other entities
        TypeDef d;
        d.name    = "EntityAPI";
        d.methods = {{"getType", enGetType}, {"getPos", enGetPos}, {"getRot", enGetRot}, {"getUUID", enGetUUID},
                     {"isLoaded", enIsLoaded}, {"getName", enGetName}, {"isOnGround", enFalse}, {"getVehicle", enNil},
                     {"getNbt", plGetNbt}, {"hasAvatar", enFalse}, {"getVelocity", plGetVelocity}};
        d.tostring = enToString;
        d.meta     = {{"__gc", enGC}};
        tEntity    = registerType(L, d);
    }
    { // player
        TypeDef d;
        d.name    = "PlayerAPI";
        d.methods = {
            {"getPos", plGetPos}, {"getRot", plGetRot}, {"getVelocity", plGetVelocity}, {"getLookDir", plGetLookDir},
            {"getBodyYaw", plGetBodyYaw}, {"getEyeHeight", plGetEyeHeight}, {"getEyePos", plGetEyePos},
            {"getName", plGetName}, {"getUUID", plGetUUID}, {"getType", plGetType}, {"isLoaded", plIsLoaded},
            {"getPose", plGetPose}, {"isOnGround", plIsOnGround}, {"isSprinting", plIsSprinting},
            {"isCrouching", plIsCrouching}, {"isSneaking", plIsSneaking}, {"isClimbing", plIsClimbing},
            {"isBlocking", plIsBlocking}, {"isFishing", plIsFishing}, {"isSwimming", plIsSwimming},
            {"isVisuallySwimming", plIsVisuallySwimming}, {"isGliding", plIsGliding}, {"isInWater", plIsInWater},
            {"isUnderwater", plIsUnderwater}, {"isInLava", plIsInLava}, {"isOnFire", plIsOnFire},
            {"isInvisible", plIsInvisible}, {"isAlive", plIsAlive}, {"isUsingItem", plIsUsingItem},
            {"isFlying", plIsFlying}, {"isSleeping", plIsSleeping}, {"isSwingingArm", plIsSwinging},
            {"isLeftHanded", plIsLeftHanded}, {"isWet", plIsWet}, {"isInRain", plIsInRain}, {"getHealth", plGetHealth},
            {"getMaxHealth", plGetMaxHealth}, {"getFood", plGetFood}, {"getSaturation", plGetSaturation},
            {"getArmor", plGetArmor}, {"getAbsorptionAmount", plGetAbsorption}, {"getExperienceLevel", plGetExpLevel},
            {"getExperienceProgress", plGetExpProgress}, {"getHurtTime", plGetHurtTime}, {"getDeathTime", plGetDeathTime},
            {"getSwingTime", plGetSwingTime}, {"getAir", plGetAir}, {"getMaxAir", plGetMaxAir}, {"getAge", plGetAge},
            {"getHeldItem", plGetHeldItem}, {"getActiveItem", plGetActiveItem}, {"getActiveHand", plGetActiveHand},
            {"getSwingArm", plGetSwingArm}, {"getActiveItemTime", plGetActiveItemTime}, {"getItem", plGetItem},
            {"getVehicle", plGetVehicle}, {"getPassengers", plGetPassengers}, {"getDimensionName", plGetDimension},
            {"getGamemode", plGetGamemode}, {"isCreative", plIsCreative}, {"hasAvatar", plHasAvatar},
            {"getNbt", plGetNbt}, {"getBoundingBox", plGetBoundingBox}, {"getVariable", plGetVariable},
            {"getTargetedBlock", plGetTargetedBlock}, {"getTargetedEntity", plGetTargetedEntity},
            {"getFrozenTicks", plGetFrozenTicks}, {"riptideSpinning", plRiptide},
            {"getPermissionLevel", plGetPermissionLevel}, {"getChargedAttackDelay", plGetChargedAttackDelay},
            {"getTeamInfo", plGetTeamInfo}, {"getModelType", plGetModelType},
            {"isSkinLayerVisible", plIsSkinLayerVisible}, {"getPlayerName", plGetPlayerName},
            {"isHamster", enFalse}, {"hasContainer", enFalse}, {"hasInventory", enFalse}, {"isGlowing", enFalse},
            {"isSilent", enFalse}, {"isSpectator", enFalse}, {"getControllingPassenger", enNil}, {"getControlledVehicle", plGetVehicle}};
        d.tostring = plToString;
        static int anchor;
        registerSingleton(L, "player", d, tPlayer, &anchor);
        lua_getglobal(L, "player");
        lua_setglobal(L, "user");
    }
    { // world
        TypeDef b;
        b.name    = "BlockState";
        b.methods = {{"getID", blGetID}, {"isAir", blIsAir}, {"getPos", blGetPos}, {"isSolidBlock", blIsSolid},
                     {"isFullCube", blIsSolid}, {"isOpaque", blIsSolid}, {"getProperties", blGetProps}, {"hasCollision", blIsSolid}};
        b.index    = blIndex;
        b.tostring = blToString;
        b.meta     = {{"__gc", blGC}};
        tBlock     = registerType(L, b);
        TypeDef d;
        d.name    = "WorldAPI";
        d.methods = {{"getLightLevel", wLight}, {"getSkyLightLevel", wSky}, {"getBlockLightLevel", wBlockLight},
                     {"getBlockState", wBlockState}, {"getBiome", wBiome}, {"getTime", wTime},
                     {"getTimeOfDay", wTimeOfDay}, {"getDayTime", wDayTime}, {"getDimension", wDimension},
                     {"getRainGradient", wRain}, {"isThundering", wThunder}, {"getMoonPhase", wMoon},
                     {"exists", wExists}, {"getPlayers", wPlayers}, {"getEntities", wEmpty}, {"getBlocks", wEmpty},
                     {"isOpenSky", cTrue}, {"getSpawnPoint", cMousePos}, {"getStrongRedstonePower", aZero},
                     {"getRedstonePower", aZero}, {"getBuildHeight", cMousePos}, {"avatarVars", wEmpty}};
        static int anchor;
        registerSingleton(L, "world", d, tWorld, &anchor);
        // Figura's world API is called with '.' (world.getTime()) AND ':' in scripts; WorldAPI functions are static.
        // Wrap: methods ignore a leading WorldAPI self.
        luaL_dostring(L, R"(
            local w = world
            local mt = getmetatable(w)
            local idx = mt.__index
            mt.__index = function(self, k)
              local f = idx(self, k)
              if type(f) ~= "function" then return f end
              return function(a, ...)
                if a == w then return f(...) end
                return f(a, ...)
              end
            end)");
    }
    { // client (static functions too)
        TypeDef d;
        d.name    = "ClientAPI";
        d.methods = {{"getFPS", cFPS}, {"isPaused", cPaused}, {"getVersion", cVersion}, {"getVersionName", cVersion},
                     {"getFiguraVersion", cFiguraVersion}, {"compareVersions", cCompareVersions},
                     {"getSystemTime", cSystemTime}, {"getWindowSize", cWindowSize}, {"getScaledWindowSize", cWindowSize},
                     {"isHudEnabled", cHud}, {"getCameraPos", cCameraPos}, {"getCameraRot", cCameraRot},
                     {"getServerBrand", cServerBrand}, {"isSingleplayer", cSingleplayer}, {"getChatText", cChatText},
                     {"getActiveLang", cLang}, {"hasShaderPack", cFalse}, {"hasShaderPackMod", cFalse},
                     {"isModLoaded", cFalse}, {"hasIrisShader", cFalse}, {"getDate", cDate}, {"getTextWidth", cTextWidth},
                     {"getFrameTime", cFrameTime}, {"getMousePos", cMousePos}, {"getGuiScale", cGuiScale},
                     {"isWindowFocused", cTrue}, {"isDebugOverlayEnabled", cFalse}, {"getEntityCount", aZero},
                     {"getParticleCount", aZero}, {"getSoundCount", aZero}, {"getJavaVersion", cVersion},
                     {"getAllocatedMemory", aZero}, {"getMaxMemory", aZero}, {"getUsedMemory", aZero},
                     {"getCurrentEffect", enNil}, {"getViewer", enNil}, {"getTabList", hStatus}};
        static int anchor;
        registerSingleton(L, "client", d, tClient, &anchor);
        luaL_dostring(L, R"(
            local c = client
            local mt = getmetatable(c)
            local idx = mt.__index
            mt.__index = function(self, k)
              local f = idx(self, k)
              if type(f) ~= "function" then return f end
              return function(a, ...)
                if a == c then return f(...) end
                return f(a, ...)
              end
            end)");
    }
    { // host
        TypeDef d;
        d.name    = "HostAPI";
        d.methods = {{"isHost", hIsHost}, {"isJumping", hIsJumping}, {"isFlying", hIsFlying}, {"sendChatMessage", hSendChat},
                     {"sendChatCommand", hSendChat}, {"setActionbar", hActionbar}, {"setTitle", hChain},
                     {"setSubtitle", hChain}, {"setTitleTimes", hChain}, {"clearTitle", hChain}, {"swingArm", hChain},
                     {"getSlot", hGetSlot}, {"isChatOpen", cFalse}, {"isContainerOpen", cFalse},
                     {"isCursorUnlocked", cFalse}, {"setUnlockCursor", hChain}, {"getScreen", hGetScreen},
                     {"getAttackCharge", hAttackCharge}, {"isAvatarUploaded", cTrue}, {"getStatusEffects", hStatus},
                     {"getChatColor", enNil}, {"setChatColor", hChain}, {"appendChatHistory", hChain},
                     {"getChatMessage", enNil}, {"setChatMessage", hChain}, {"isChatVerified", cTrue},
                     {"getReachDistance", cGuiScale}, {"getAir", plGetAir}, {"setClipboard", hChain}, {"getClipboard", enNil},
                     {"writeToLog", hChain}, {"warnToLog", hChain}, {"getPickBlock", enNil}, {"getPickEntity", enNil}};
        static int anchor;
        registerSingleton(L, "host", d, tHost, &anchor);
    }
    { // renderer
        TypeDef d;
        d.name     = "RendererAPI";
        d.index    = rIndex;
        d.newindex = rNewIndex;
        static int anchor;
        registerSingleton(L, "renderer", d, tRenderer, &anchor);
    }
    { // vanilla_model
        TypeDef d;
        d.name    = "VanillaModelPart";
        d.methods = {{"setVisible", vpSetVisible}, {"visible", vpSetVisible}, {"getVisible", vpGetVisible},
                     {"setPos", vpSetPos}, {"pos", vpSetPos}, {"setRot", vpSetRot}, {"rot", vpSetRot},
                     {"setOffsetRot", vpSetOffsetRot}, {"offsetRot", vpSetOffsetRot}, {"setScale", vpSetScale},
                     {"scale", vpSetScale}, {"getPos", vpGetPos}, {"getRot", vpGetRot}, {"getScale", vpGetScale},
                     {"getOriginRot", vpGetOriginRot}, {"getOriginPos", vpGetOriginPos},
                     {"getOriginScale", vpGetOriginScale}, {"getOriginVisible", vpGetVisible}, {"getName", vpGetName},
                     {"setOffsetScale", vpChain}, {"offsetScale", vpChain}, {"getOffsetRot", vpGetRot}};
        d.tostring   = vpToString;
        tVanillaPart = registerType(L, d);
        auto& vps    = vparts(L);
        vps.clear();
        struct Def { char const* n; std::vector<char const*> ch; };
        std::vector<Def> defs = {
            {"ALL", {"PLAYER", "ARMOR", "CAPE", "ELYTRA", "HELD_ITEMS", "PARROTS"}},
            {"PLAYER", {"OUTER_LAYER", "INNER_LAYER"}},
            {"OUTER_LAYER", {"HAT", "JACKET", "LEFT_SLEEVE", "RIGHT_SLEEVE", "LEFT_PANTS", "RIGHT_PANTS"}},
            {"INNER_LAYER", {"HEAD", "BODY", "LEFT_ARM", "RIGHT_ARM", "LEFT_LEG", "RIGHT_LEG"}},
            {"HEAD", {}}, {"BODY", {}}, {"LEFT_ARM", {}}, {"RIGHT_ARM", {}}, {"LEFT_LEG", {}}, {"RIGHT_LEG", {}},
            {"HAT", {}}, {"JACKET", {}}, {"LEFT_SLEEVE", {}}, {"RIGHT_SLEEVE", {}}, {"LEFT_PANTS", {}}, {"RIGHT_PANTS", {}},
            {"CAPE", {"CAPE_MODEL"}}, {"CAPE_MODEL", {}}, {"FAKE_CAPE", {}},
            {"ARMOR", {"HELMET", "CHESTPLATE", "LEGGINGS", "BOOTS"}},
            {"HELMET", {"HELMET_HEAD", "HELMET_HAT"}}, {"HELMET_HEAD", {}}, {"HELMET_HAT", {}},
            {"CHESTPLATE", {"CHESTPLATE_BODY", "CHESTPLATE_LEFT_ARM", "CHESTPLATE_RIGHT_ARM"}},
            {"CHESTPLATE_BODY", {}}, {"CHESTPLATE_LEFT_ARM", {}}, {"CHESTPLATE_RIGHT_ARM", {}},
            {"LEGGINGS", {"LEGGINGS_BODY", "LEGGINGS_LEFT_LEG", "LEGGINGS_RIGHT_LEG"}},
            {"LEGGINGS_BODY", {}}, {"LEGGINGS_LEFT_LEG", {}}, {"LEGGINGS_RIGHT_LEG", {}},
            {"BOOTS", {"BOOTS_LEFT_LEG", "BOOTS_RIGHT_LEG"}}, {"BOOTS_LEFT_LEG", {}}, {"BOOTS_RIGHT_LEG", {}},
            {"ELYTRA", {"LEFT_ELYTRA", "RIGHT_ELYTRA"}}, {"LEFT_ELYTRA", {}}, {"RIGHT_ELYTRA", {}},
            {"HELD_ITEMS", {"LEFT_ITEM", "RIGHT_ITEM"}}, {"LEFT_ITEM", {}}, {"RIGHT_ITEM", {}},
            {"PARROTS", {"LEFT_PARROT", "RIGHT_PARROT"}}, {"LEFT_PARROT", {}}, {"RIGHT_PARROT", {}}};
        lua_newtable(L); // vanilla_model
        for (auto& df : defs) {
            auto p  = std::make_unique<VPart>();
            p->name = df.n;
            for (auto c : df.ch) p->children.emplace_back(c);
            pushRef(L, p.get(), tVanillaPart);
            lua_setfield(L, -2, df.n);
            vps.push_back(std::move(p));
        }
        lua_setglobal(L, "vanilla_model");
    }
    { // nameplate
        TypeDef d;
        d.name    = "NameplateCustomization";
        d.methods = {{"setVisible", npSetVisible}, {"visible", npSetVisible}, {"isVisible", npIsVisible},
                     {"setText", npSetText}, {"text", npSetText}, {"getText", npGetText}, {"setPos", npChain},
                     {"pos", npChain}, {"setPivot", npChain}, {"setScale", npChain}, {"scale", npChain},
                     {"setBackgroundColor", npChain}, {"setShadow", npChain}, {"setOutline", npChain},
                     {"setOutlineColor", npChain}, {"setLight", npChain}};
        tNameplate = registerType(L, d);
        static NPart chat{"CHAT"}, entity{"ENTITY"}, list{"LIST"}, all{"ALL"};
        lua_newtable(L);
        for (NPart* n : {&chat, &entity, &list, &all}) {
            pushRef(L, n, tNameplate);
            lua_setfield(L, -2, n->name.c_str());
        }
        lua_setglobal(L, "nameplate");
    }
    { // sounds
        TypeDef s;
        s.name    = "Sound";
        s.methods = {{"play", sPlay}, {"stop", sStop}, {"pause", sStop}, {"setPos", sSetPos}, {"pos", sSetPos},
                     {"setVolume", sSetVolume}, {"volume", sSetVolume}, {"setPitch", sSetPitch}, {"pitch", sSetPitch},
                     {"setLoop", sSetLoop}, {"loop", sSetLoop}, {"isPlaying", sIsPlaying}, {"getPos", sGetPos},
                     {"getVolume", sGetVolume}, {"getPitch", sGetPitch}, {"setAttenuation", sChain},
                     {"attenuation", sChain}, {"setSubtitle", sChain}, {"subtitle", sChain}};
        s.tostring = sToString;
        tSound     = registerType(L, s);
        TypeDef d;
        d.name    = "SoundAPI";
        d.methods = {{"playSound", sndPlaySound}, {"stopSound", sndStopSound}, {"isPresent", sndIsPresent},
                     {"getCustomSounds", sndGetCustom}, {"newSound", sChain}};
        d.index   = sndIndex;
        static int anchor;
        registerSingleton(L, "sounds", d, tSounds, &anchor);
    }
    { // avatar
        TypeDef d;
        d.name    = "AvatarAPI";
        d.methods = {{"getNBT", aGetNBT}, {"getName", aGetName}, {"getAuthors", aGetAuthors}, {"getColor", aGetColor},
                     {"setColor", aSetColor}, {"getVersion", aGetVersion}, {"getUUID", aGetUUID},
                     {"getEntityName", aGetEntityName}, {"store", aStore}, {"canEditVanillaModel", aTrue},
                     {"canEditNameplate", aTrue}, {"canRenderOffscreen", aTrue}, {"canUseCustomSounds", aTrue},
                     {"canHaveCustomSkull", aTrue}, {"getPermissionLevel", aPermission}, {"getComplexity", aZero},
                     {"getMaxComplexity", aMaxInst}, {"getCurrentInstructions", aCurInst},
                     {"getMaxInstructions", aMaxInst}, {"getInitCount", aZero}, {"getTickCount", aZero},
                     {"getRenderCount", aZero}, {"getWorldTickCount", aZero}, {"getWorldRenderCount", aZero},
                     {"getAnimationCount", aZero}, {"getMaxAnimationCount", aMaxInst}, {"hasTexture", aTrue},
                     {"hasScriptError", cFalse}, {"isLocal", aTrue}, {"getSize", aZero}, {"getVolume", aZero},
                     {"getRemainingParticles", aMaxInst}, {"getRemainingSounds", aMaxInst}, {"getMaxParticles", aMaxInst},
                     {"getMaxSounds", aMaxInst}, {"getMaxTickCount", aMaxInst}, {"getMaxRenderCount", aMaxInst},
                     {"getMaxWorldTickCount", aMaxInst}, {"getMaxWorldRenderCount", aMaxInst},
                     {"getMaxInitCount", aMaxInst}, {"getMaxTexturesSize", aMaxInst}, {"getBadges", cLang}};
        static int anchor;
        registerSingleton(L, "avatar", d, tAvatarAPI, &anchor);
    }
    { // particles
        TypeDef d;
        d.name  = "ParticleAPI";
        d.index = ptIndex;
        static int anchor;
        registerSingleton(L, "particles", d, tParticles, &anchor);
    }
}

} // namespace bf::figura
