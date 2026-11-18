// FiguraBridge Lua API: the game-facing Figura 0.1.6 APIs - entities (EntityAPI / LivingEntityAPI / PlayerAPI /
// ViewerAPI / NullEntity), ItemStack, BlockState, Biome, world, client, host, renderer, nameplate, avatar, sounds,
// particles, raycast and vanilla_model. Port of org.figuramc.figura.lua.api.** (same names, arguments, nil-vs-value
// returns, defaults and chainable setters). Values come from Avatar::player / IHost / IWorldQuery; when the game can't
// provide something, Java 1.20.1 defaults (LuaApiEntityData.h tables) or Figura's own defaults are returned.
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "figura/core/Avatar.h"
#include "figura/core/LuaApi.h"
#include "figura/core/LuaApiEntityData.h"
#include "figura/core/LuaBind.h"
#include "lyrics/core/Json.h"

namespace bf::figura {

using namespace lua;

namespace {
uint32_t tEntity = 0, tLiving = 0, tPlayer = 0, tViewer = 0, tNullEntity = 0, tItem = 0, tBlock = 0, tBiome = 0,
         tWorld = 0, tClient = 0, tHost = 0, tRenderer = 0, tNameplateAPI = 0, tNameplate = 0, tEntityNameplate = 0,
         tNameplateGroup = 0, tAvatarAPI = 0, tSounds = 0, tSound = 0, tParticles = 0, tParticle = 0, tRaycast = 0,
         tVanillaAPI = 0, tVanillaGroup = 0, tVanillaPart = 0;

constexpr int kIntMax = 2147483647;

Avatar*            av(lua_State* L) { return avatarOf(L); }
PlayerState const& P(lua_State* L) { return avatarOf(L)->player; }
IWorldQuery&       W(lua_State* L) { return avatarOf(L)->host->world(); }
int                ret1(lua_State* L) { lua_settop(L, 1); return 1; }
int                pushB(lua_State* L, bool b) { lua_pushboolean(L, b); return 1; }
int                pushN(lua_State* L, double d) { lua_pushnumber(L, d); return 1; }
int                pushI(lua_State* L, long long i) { lua_pushinteger(L, lua_Integer(i)); return 1; }
int                pushS(lua_State* L, std::string const& s) { lua_pushlstring(L, s.data(), s.size()); return 1; }
int                pushNil(lua_State* L) { lua_pushnil(L); return 1; }
int                retNil(lua_State* L) { lua_pushnil(L); return 1; }
int                retFalse(lua_State* L) { return pushB(L, false); }
int                retTrue(lua_State* L) { return pushB(L, true); }
int                retZero(lua_State* L) { return pushI(L, 0); }
int                retNone(lua_State* L) { (void)L; return 0; }
int                retEmptyTable(lua_State* L) { lua_newtable(L); return 1; }
float              lerpf(float a, float b, float t) { return a + (b - a) * t; }
bool               isNil(lua_State* L, int i) { return lua_isnoneornil(L, i); }
std::string        mtOf(uint32_t t) { return std::string("fb:") + typeName(t); }
void*              testUD(lua_State* L, int i, uint32_t t) { return luaL_testudata(L, i, mtOf(t).c_str()); }
template <class T> T* newUD(lua_State* L, uint32_t t, T&& v) {
    void* m = lua_newuserdata(L, sizeof(T));
    T*    o = new (m) T(std::move(v));
    luaL_setmetatable(L, mtOf(t).c_str());
    return o;
}
template <class T> int gcUD(lua_State* L) { static_cast<T*>(lua_touserdata(L, 1))->~T(); return 0; }
/// Static Figura API functions (world.*, client.*) are called both as `world.f(...)` and `world:f(...)`.
template <lua_CFunction F, uint32_t* T> int stat(lua_State* L) {
    if (lua_type(L, 1) == LUA_TUSERDATA && toRef(L, 1, *T)) lua_remove(L, 1);
    return F(L);
}
std::string upper(std::string s) { for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c))); return s; }
std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c))); return s; }
bool        endsWith(std::string const& s, char const* e) { size_t n = std::strlen(e); return s.size() >= n && s.compare(s.size() - n, n, e) == 0; }
bool        has(std::string const& s, char const* sub) { return s.find(sub) != std::string::npos; }
std::string stripNs(std::string const& id) { return id.rfind("minecraft:", 0) == 0 ? id.substr(10) : id; }
/// ResourceLocation(path).toString(): default namespace minecraft
std::string resLoc(std::string const& s) { return s.find(':') == std::string::npos ? "minecraft:" + s : s; }
void        notNil(lua_State* L, int i, char const* fn) {
    if (isNil(L, i)) luaL_error(L, "bad argument %d to '%s' (value expected, got nil)", i, fn);
}
std::string checkStr(lua_State* L, int i, char const* fn) {
    notNil(L, i, fn);
    size_t n = 0;
    char const* s = luaL_checklstring(L, i, &n);
    return std::string(s, n);
}

// ---------------------------------------------------------------- vector argument parsing (LuaUtils)
Vec2 parseVec2(lua_State* L, int i, char const* fn, double dx = 0, double dy = 0) {
    int n = 0;
    if (double* v = toVec(L, i, &n)) {
        if (n != 2) luaL_error(L, "Illegal argument to %s(): Vector%d", fn, n);
        return {v[0], v[1]};
    }
    if (!isNil(L, i) && lua_type(L, i) != LUA_TNUMBER) luaL_error(L, "Illegal argument to %s(): %s", fn, figuraTypeName(L, i).c_str());
    return {optNum(L, i, dx), optNum(L, i + 1, dy)};
}
Vec4 parseVec4(lua_State* L, int i, char const* fn, double dx, double dy, double dz, double dw) {
    int n = 0;
    if (double* v = toVec(L, i, &n)) {
        if (n == 3) return {v[0], v[1], v[2], dw};
        if (n == 4) return {v[0], v[1], v[2], v[3]};
        luaL_error(L, "Illegal argument to %s(): Vector%d", fn, n);
    }
    if (!isNil(L, i) && lua_type(L, i) != LUA_TNUMBER) luaL_error(L, "Illegal argument to %s(): %s", fn, figuraTypeName(L, i).c_str());
    return {optNum(L, i, dx), optNum(L, i + 1, dy), optNum(L, i + 2, dz), optNum(L, i + 3, dw)};
}
std::optional<Vec3> nullableVec3(lua_State* L, int i, char const* fn) {
    if (isNil(L, i)) return std::nullopt;
    return parseVec3(L, i, fn);
}
Vec3 oneArgVec(lua_State* L, int i, char const* fn, double def) {
    double d = lua_type(L, i) == LUA_TNUMBER ? lua_tonumber(L, i) : def;
    return parseVec3(L, i, fn, d);
}
/// LuaUtils.parseVec with sizes {3}: a Vector3 (1 arg) or 3 numbers (nil = 0). Advances i.
Vec3 seqVec3(lua_State* L, int& i, char const* fn) {
    int n = 0;
    if (double* v = toVec(L, i, &n)) {
        if (n != 3) luaL_error(L, "Illegal argument at position %d to %s(): Expected Vector3, recieved Vector%d", i, fn, n);
        i += 1;
        return {v[0], v[1], v[2]};
    }
    if (!isNil(L, i) && lua_type(L, i) != LUA_TNUMBER)
        luaL_error(L, "Illegal argument at position %d to %s(): Expected Vector3 or Number, recieved %s", i, fn, figuraTypeName(L, i).c_str());
    Vec3 r;
    double* c[3] = {&r.x, &r.y, &r.z};
    for (int k = 0; k < 3; k++) {
        if (isNil(L, i + k)) *c[k] = 0;
        else if (lua_type(L, i + k) == LUA_TNUMBER) *c[k] = lua_tonumber(L, i + k);
        else luaL_error(L, "Illegal argument at position %d to %s(): Expected Number, recieved %s", i + k, fn, figuraTypeName(L, i + k).c_str());
    }
    i += 3;
    return r;
}
int  blockFloor(double d) { return int(std::floor(d)); }
void pushVec3o(lua_State* L, std::optional<Vec3> const& v) { if (v) pushVec3(L, *v); else lua_pushnil(L); }
Vec3 intToRGB(uint32_t c) { return {((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0}; }
uint32_t rgbToInt(Vec3 v) {
    auto ch = [](double d) { return uint32_t(std::clamp(int(d * 255), 0, 255)); };
    return (ch(v.x) << 16) | (ch(v.y) << 8) | ch(v.z);
}
std::string rgbToHex(Vec3 v) { char b[16]; std::snprintf(b, sizeof b, "%06x", rgbToInt(v)); return b; }
Vec3 hsvToRgb(double h, double s, double v) { // Mth.hsvToRgb
    int    i = int(h * 6.0f) % 6;
    double f = h * 6.0 - std::floor(h * 6.0), p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    switch (i) {
    case 0: return {v, t, p};
    case 1: return {q, v, p};
    case 2: return {p, v, t};
    case 3: return {p, q, v};
    case 4: return {t, p, v};
    default: return {v, p, q};
    }
}
Vec3 lookDir(double pitch, double yaw) {
    double pi = toRad(pitch), ya = toRad(yaw);
    return {-std::sin(ya) * std::cos(pi), -std::sin(pi), std::cos(ya) * std::cos(pi)};
}

// ---------------------------------------------------------------- text (JSON components -> plain text, font metrics)
void flattenJson(lyrics::json::Value const& v, std::string& out) {
    if (v.isString()) { out += v.asString(); return; }
    if (v.isArray()) { for (auto const& x : v.items()) flattenJson(x, out); return; }
    if (!v.isObject()) return;
    if (v.has("text")) out += v["text"].str();
    else if (v.has("translate")) out += v["translate"].str();
    else if (v.has("keybind")) out += v["keybind"].str();
    else if (v.has("selector")) out += v["selector"].str();
    if (v.has("extra")) flattenJson(v["extra"], out);
}
/// TextUtils.tryParseJson(text).getString()
std::string plainText(std::string const& s) {
    size_t i = s.find_first_not_of(" \t\r\n");
    if (i == std::string::npos || (s[i] != '{' && s[i] != '[' && s[i] != '"')) return s;
    auto j = lyrics::json::parse(s);
    if (!j || !(j->isObject() || j->isArray() || j->isString())) return s;
    std::string r;
    flattenJson(*j, r);
    return r;
}
std::string jsonQuote(std::string const& s) {
    std::string r = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { r += '\\'; r += c; }
        else if (c == '\n') r += "\\n";
        else r += c;
    }
    return r + "\"";
}
/// Minecraft default font advance (glyph width + 1)
int charWidth(unsigned c) {
    switch (c) {
    case ' ': return 4;
    case '!': case '\'': case ',': case '.': case ':': case ';': case 'i': case '|': return 2;
    case '`': case 'l': return 3;
    case 'I': case '[': case ']': case 't': return 4;
    case '"': case '(': case ')': case '*': case '<': case '>': case 'f': case 'k': case '{': case '}': return 5;
    case '@': case '~': return 7;
    default: return 6;
    }
}
int textWidth(std::string const& line) {
    int w = 0;
    for (size_t i = 0; i < line.size(); i++) {
        unsigned char c = static_cast<unsigned char>(line[i]);
        if (c == 0xC2 && i + 1 < line.size() && static_cast<unsigned char>(line[i + 1]) == 0xA7) { i += 2; continue; } // §x
        if (c >= 0x80) { // one UTF-8 code point = one glyph (wide-ish)
            if ((c & 0xE0) == 0xC0) i += 1; else if ((c & 0xF0) == 0xE0) i += 2; else if ((c & 0xF8) == 0xF0) i += 3;
            w += c >= 0xE0 ? 9 : 6;
            continue;
        }
        w += charWidth(c);
    }
    return w;
}
std::vector<std::string> splitLines(std::string const& s) {
    std::vector<std::string> r;
    size_t p = 0;
    for (;;) {
        size_t q = s.find('\n', p);
        r.push_back(s.substr(p, q == std::string::npos ? std::string::npos : q - p));
        if (q == std::string::npos) break;
        p = q + 1;
    }
    return r;
}

// ---------------------------------------------------------------- Java 1.20.1 tables
template <class T, size_t N> T const* findById(T const (&arr)[N], std::string const& id) {
    static std::unordered_map<std::string, T const*> m = [&] {
        std::unordered_map<std::string, T const*> r;
        for (auto const& x : arr) r[x.id] = &x;
        return r;
    }();
    auto it = m.find(stripNs(id));
    return it == m.end() ? nullptr : it->second;
}
mcdata::Item const*       itemInfo(std::string const& id) { return findById(mcdata::kItems, id); }
mcdata::Block const*      blockInfo(std::string const& id) { return findById(mcdata::kBlocks, id); }
mcdata::Biome const*      biomeInfo(std::string const& id) { return findById(mcdata::kBiomes, id); }
mcdata::EntityType const* entityInfo(std::string const& id) { return findById(mcdata::kEntities, id); }
template <size_t N> bool inList(char const* const (&arr)[N], std::string const& id) {
    static std::set<std::string> s(std::begin(arr), std::end(arr));
    return s.count(stripNs(id)) > 0;
}
bool knownParticle(std::string const& id) { return inList(mcdata::kParticles, id); }
bool knownSound(std::string const& id) { return inList(mcdata::kSounds, id); }

// ---------------------------------------------------------------- ItemStack (value userdata)
struct ItemUD { ItemState it; };
/// ItemStack with every "unknown" field resolved from the item table (ItemStackAPI over a real ItemStack).
ItemState resolveItem(ItemState it) {
    if (it.id.empty()) it.id = "minecraft:air";
    if (it.id.find(':') == std::string::npos) it.id = "minecraft:" + it.id;
    std::string n = stripNs(it.id);
    if (n == "air" || n == "cave_air" || n == "void_air") it.count = 0;
    mcdata::Item const* info = itemInfo(it.id);
    if (it.maxCount < 0) it.maxCount = info ? info->stack : 64;
    if (it.maxDamage < 0) it.maxDamage = info ? info->durability : 0;
    if (info && info->food && !it.isFood) it.isFood = true;
    if (!it.isBlockItem && blockInfo(it.id) && n != "air") it.isBlockItem = true;
    if (!it.isTool) it.isTool = endsWith(n, "_pickaxe") || endsWith(n, "_axe") || endsWith(n, "_shovel") || endsWith(n, "_hoe");
    if (!it.isArmor) it.isArmor = endsWith(n, "_helmet") || endsWith(n, "_chestplate") || endsWith(n, "_leggings") || endsWith(n, "_boots");
    if (it.useAction.empty() || it.useAction == "NONE") {
        if (n == "bow") it.useAction = "BOW";
        else if (n == "crossbow") it.useAction = "CROSSBOW";
        else if (n == "shield") it.useAction = "BLOCK";
        else if (n == "trident") it.useAction = "SPEAR";
        else if (n == "spyglass") it.useAction = "SPYGLASS";
        else if (n == "goat_horn") it.useAction = "TOOT_HORN";
        else if (n == "brush") it.useAction = "BRUSH";
        else if (n == "potion" || n == "milk_bucket" || n == "honey_bottle") it.useAction = "DRINK";
        else if (it.isFood) it.useAction = "EAT";
        else it.useAction = "NONE";
    }
    if (it.maxUseDuration <= 0) {
        if (it.useAction == "BOW" || it.useAction == "BLOCK" || it.useAction == "SPEAR" || it.useAction == "SPYGLASS" || it.useAction == "BRUSH")
            it.maxUseDuration = it.useAction == "SPYGLASS" ? 1200 : it.useAction == "BRUSH" ? 200 : 72000;
        else if (it.useAction == "CROSSBOW") it.maxUseDuration = 25 + 3;
        else if (it.useAction == "TOOT_HORN") it.maxUseDuration = 140;
        else if (n == "honey_bottle") it.maxUseDuration = 40;
        else if (it.useAction == "DRINK") it.maxUseDuration = 32;
        else if (it.useAction == "EAT") it.maxUseDuration = info && info->food == 2 ? 16 : 32;
    }
    if (it.rarity.empty()) {
        static std::set<std::string> epic{"enchanted_golden_apple", "command_block", "chain_command_block", "repeating_command_block",
                                          "structure_block", "jigsaw", "barrier", "light", "debug_stick", "dragon_egg", "structure_void"};
        static std::set<std::string> rare{"golden_apple", "beacon", "conduit", "end_crystal", "elytra", "trident",
                                          "music_disc_13", "music_disc_cat", "music_disc_blocks", "music_disc_chirp",
                                          "music_disc_far", "music_disc_mall", "music_disc_mellohi", "music_disc_stal",
                                          "music_disc_strad", "music_disc_ward", "music_disc_11", "music_disc_wait",
                                          "music_disc_otherside", "music_disc_5", "music_disc_pigstep", "music_disc_relic",
                                          "mojang_banner_pattern"};
        static std::set<std::string> unc{"experience_bottle", "dragon_breath", "nether_star", "totem_of_undying", "heart_of_the_sea",
                                         "enchanted_book", "creeper_head", "zombie_head", "skeleton_skull", "wither_skeleton_skull",
                                         "player_head", "dragon_head", "piglin_head", "creeper_banner_pattern", "skull_banner_pattern",
                                         "netherite_upgrade_smithing_template", "sentry_armor_trim_smithing_template"};
        it.rarity = epic.count(n) ? "EPIC" : rare.count(n) ? "RARE" : (unc.count(n) || endsWith(n, "_armor_trim_smithing_template")) ? "UNCOMMON" : "COMMON";
        if (it.glint && it.rarity == "COMMON") it.rarity = "RARE";
        else if (it.glint && it.rarity == "UNCOMMON") it.rarity = "RARE";
    }
    static std::set<std::string> foil{"enchanted_golden_apple", "enchanted_book", "experience_bottle", "nether_star",
                                      "end_crystal", "debug_stick", "written_book"};
    if (foil.count(n)) it.glint = true;
    if (it.name.empty()) it.name = info ? info->name : n;
    return it;
}
void pushItem(lua_State* L, ItemState const& it) { newUD(L, tItem, ItemUD{resolveItem(it)}); }
ItemState& item(lua_State* L, int i, char const* fn) {
    void* p = testUD(L, i, tItem);
    if (!p) luaL_error(L, "bad argument %d to '%s' (ItemStack expected, got %s)", i, fn, figuraTypeName(L, i).c_str());
    return static_cast<ItemUD*>(p)->it;
}
std::map<std::string, std::string> itemTag(ItemState const& i) {
    auto t = i.nbt;
    if (i.maxDamage > 0 && !t.count("Damage")) t["Damage"] = std::to_string(i.damage);
    if (i.charged) t["Charged"] = "1b";
    return t;
}
void pushNbtValue(lua_State* L, std::string const& v) {
    char* end = nullptr;
    std::string num = v;
    if (!num.empty() && std::strchr("bBsSlLfFdD", num.back()) && num.size() > 1) num.pop_back();
    double d = std::strtod(num.c_str(), &end);
    if (!num.empty() && end && *end == 0) { lua_pushnumber(L, d); return; }
    if (v.size() >= 2 && (v[0] == '{' || v[0] == '[')) {
        if (auto j = lyrics::json::parse(v)) {
            std::function<void(lyrics::json::Value const&)> push = [&](lyrics::json::Value const& x) {
                if (x.isNumber()) lua_pushnumber(L, x.asNumber());
                else if (x.isBool()) lua_pushinteger(L, x.asBool() ? 1 : 0);
                else if (x.isString()) lua_pushstring(L, x.asString().c_str());
                else if (x.isArray()) { lua_newtable(L); int k = 1; for (auto const& e : x.items()) { push(e); lua_rawseti(L, -2, k++); } }
                else if (x.isObject()) { lua_newtable(L); for (auto const& [kk, e] : x.members()) { push(e); lua_setfield(L, -2, kk.c_str()); } }
                else lua_pushnil(L);
            };
            push(*j);
            return;
        }
    }
    lua_pushlstring(L, v.data(), v.size());
}
int itGetTag(lua_State* L) {
    auto& i = item(L, 1, "getTag");
    lua_newtable(L);
    for (auto const& [k, v] : itemTag(i)) { pushNbtValue(L, v); lua_setfield(L, -2, k.c_str()); }
    return 1;
}
std::string tagString(ItemState const& i) {
    auto t = itemTag(i);
    if (t.empty()) return "";
    std::string s = "{";
    bool first = true;
    for (auto const& [k, v] : t) { s += (first ? "" : ",") + k + ":" + v; first = false; }
    return s + "}";
}
int itGetID(lua_State* L) { return pushS(L, item(L, 1, "getID").id); }
int itGetCount(lua_State* L) { return pushI(L, item(L, 1, "getCount").count); }
int itGetDamage(lua_State* L) { return pushI(L, item(L, 1, "getDamage").damage); }
int itGetPopTime(lua_State* L) { return pushI(L, item(L, 1, "getPopTime").popTime); }
int itHasGlint(lua_State* L) { return pushB(L, item(L, 1, "hasGlint").glint); }
int itGetTags(lua_State* L) {
    auto const& i = item(L, 1, "getTags");
    std::vector<std::string> t = i.tags;
    if (t.empty()) { // a few common vanilla item tags
        std::string n = stripNs(i.id);
        auto add = [&](char const* s) { t.push_back(s); };
        if (endsWith(n, "_planks")) add("minecraft:planks");
        if (endsWith(n, "_log") || endsWith(n, "_wood") || endsWith(n, "_stem") || endsWith(n, "_hyphae")) add("minecraft:logs");
        if (endsWith(n, "_wool")) add("minecraft:wool");
        if (endsWith(n, "_sword")) add("minecraft:swords");
        if (endsWith(n, "_axe")) add("minecraft:axes");
        if (endsWith(n, "_pickaxe")) add("minecraft:pickaxes");
        if (endsWith(n, "_shovel")) add("minecraft:shovels");
        if (endsWith(n, "_hoe")) add("minecraft:hoes");
        if (endsWith(n, "_sword") || endsWith(n, "_axe") || endsWith(n, "_pickaxe") || endsWith(n, "_shovel") || endsWith(n, "_hoe") || n == "trident")
            add("minecraft:tools");
        if (i.isArmor) add("minecraft:trimmable_armor");
        if (endsWith(n, "_leaves")) add("minecraft:leaves");
        if (endsWith(n, "_sapling")) add("minecraft:saplings");
        if (n == "coal" || n == "charcoal") add("minecraft:coals");
        if (n == "arrow" || n == "spectral_arrow" || n == "tipped_arrow") add("minecraft:arrows");
        if (has(n, "music_disc_")) add("minecraft:music_discs");
        if (endsWith(n, "_boat") || endsWith(n, "_raft")) add("minecraft:boats");
    }
    return pushStrings(L, t);
}
int itIsBlockItem(lua_State* L) { return pushB(L, item(L, 1, "isBlockItem").isBlockItem); }
int itIsFood(lua_State* L) { return pushB(L, item(L, 1, "isFood").isFood); }
int itGetUseAction(lua_State* L) { return pushS(L, item(L, 1, "getUseAction").useAction); }
int itGetName(lua_State* L) { return pushS(L, plainText(item(L, 1, "getName").name)); }
int itGetMaxCount(lua_State* L) { return pushI(L, item(L, 1, "getMaxCount").maxCount); }
int itGetRarity(lua_State* L) { return pushS(L, item(L, 1, "getRarity").rarity); }
int itIsEnchantable(lua_State* L) {
    auto const& i = item(L, 1, "isEnchantable");
    std::string n = stripNs(i.id);
    return pushB(L, ((i.maxCount == 1 && i.maxDamage > 0) || n == "book") && !i.glint);
}
int itGetMaxDamage(lua_State* L) { return pushI(L, item(L, 1, "getMaxDamage").maxDamage); }
int itIsDamageable(lua_State* L) { auto const& i = item(L, 1, "isDamageable"); return pushB(L, i.maxDamage > 0); }
int itIsStackable(lua_State* L) { auto const& i = item(L, 1, "isStackable"); return pushB(L, i.maxCount > 1 && (i.maxDamage <= 0 || i.damage == 0)); }
int itGetRepairCost(lua_State* L) { auto const& i = item(L, 1, "getRepairCost"); auto it = i.nbt.find("RepairCost"); return pushI(L, it == i.nbt.end() ? 0 : std::atoi(it->second.c_str())); }
int itGetUseDuration(lua_State* L) { return pushI(L, item(L, 1, "getUseDuration").maxUseDuration); }
int itToStackString(lua_State* L) { auto const& i = item(L, 1, "toStackString"); return pushS(L, i.id + tagString(i)); }
int itIsArmor(lua_State* L) { return pushB(L, item(L, 1, "isArmor").isArmor); }
int itIsTool(lua_State* L) { return pushB(L, item(L, 1, "isTool").isTool); }
int itGetEquipmentSlot(lua_State* L) {
    std::string n = stripNs(item(L, 1, "getEquipmentSlot").id);
    if (endsWith(n, "_helmet") || n == "carved_pumpkin" || endsWith(n, "_head") || endsWith(n, "_skull")) return pushS(L, "HEAD");
    if (endsWith(n, "_chestplate") || n == "elytra") return pushS(L, "CHEST");
    if (endsWith(n, "_leggings")) return pushS(L, "LEGS");
    if (endsWith(n, "_boots")) return pushS(L, "FEET");
    if (n == "shield") return pushS(L, "OFFHAND");
    return pushS(L, "MAINHAND");
}
int itCopy(lua_State* L) { pushItem(L, item(L, 1, "copy")); return 1; }
void pushBlock(lua_State* L, std::string const& id, int x, int y, int z, std::map<std::string, std::string> const& props, bool hasPos);
int itGetBlockstate(lua_State* L) {
    auto const& i = item(L, 1, "getBlockstate");
    if (!i.isBlockItem || !blockInfo(i.id)) return pushNil(L);
    pushBlock(L, resLoc(i.id), 0, 0, 0, {}, false);
    return 1;
}
int itEq(lua_State* L) {
    void* a = testUD(L, 1, tItem);
    void* b = testUD(L, 2, tItem);
    if (!a || !b) return pushB(L, false);
    auto const& x = static_cast<ItemUD*>(a)->it;
    auto const& y = static_cast<ItemUD*>(b)->it;
    return pushB(L, x.count == y.count && x.id == y.id && itemTag(x) == itemTag(y));
}
int itIndex(lua_State* L) {
    auto& i = item(L, 1, "__index");
    if (lua_type(L, 2) != LUA_TSTRING) return 0;
    std::string k = lua_tostring(L, 2);
    if (k == "id") return pushS(L, i.id);
    if (k == "tag") { lua_settop(L, 1); return itGetTag(L); }
    return 0;
}
int itToString(lua_State* L) { auto const& i = item(L, 1, "__tostring"); return pushS(L, i.id + " x" + std::to_string(i.count) + " (ItemStack)"); }

/// LuaUtils.parseItemStack / world.newItem: "minecraft:diamond_sword{Damage:3}"
bool parseItemString(std::string s, ItemState& out) {
    size_t i = s.find_first_not_of(' ');
    if (i == std::string::npos) return false;
    s = s.substr(i);
    std::string id = s, tag;
    if (size_t b = s.find('{'); b != std::string::npos) { id = s.substr(0, b); tag = s.substr(b); }
    while (!id.empty() && id.back() == ' ') id.pop_back();
    id = lower(id);
    if (id.rfind("minecraft:", 0) != 0 && id.find(':') != std::string::npos) return false;
    if (!itemInfo(id)) return false;
    out    = ItemState{};
    out.id = resLoc(id);
    out.count = 1;
    if (tag.size() > 2) { // {Key:value,...} (flat SNBT only)
        std::string body = tag.substr(1, tag.size() - 2);
        int depth = 0;
        std::string cur;
        auto flush = [&] {
            size_t c = cur.find(':');
            if (c != std::string::npos) {
                std::string k = cur.substr(0, c), v = cur.substr(c + 1);
                auto trim = [](std::string& x) { while (!x.empty() && x.front() == ' ') x.erase(0, 1); while (!x.empty() && x.back() == ' ') x.pop_back(); };
                trim(k); trim(v);
                if (k == "Damage") out.damage = std::atoi(v.c_str());
                else if (k == "Enchantments" || k == "StoredEnchantments") out.glint = v.size() > 2;
                else if (k == "Charged") out.charged = v.rfind("1", 0) == 0 || v == "true";
                out.nbt[k] = v;
            }
            cur.clear();
        };
        for (char c : body) {
            if (c == '{' || c == '[') depth++;
            if (c == '}' || c == ']') depth--;
            if (c == ',' && depth == 0) flush(); else cur += c;
        }
        flush();
    }
    return true;
}
ItemState itemArg(lua_State* L, int i, char const* fn) { // ItemStack or string
    if (isNil(L, i)) return ItemState{};
    if (void* p = testUD(L, i, tItem)) return static_cast<ItemUD*>(p)->it;
    if (lua_type(L, i) == LUA_TSTRING) {
        ItemState it;
        if (!parseItemString(lua_tostring(L, i), it)) luaL_error(L, "Could not parse item stack from string: %s", lua_tostring(L, i));
        return it;
    }
    luaL_error(L, "Illegal argument to %s(): %s", fn, figuraTypeName(L, i).c_str());
    return {};
}

// ---------------------------------------------------------------- BlockState (value userdata)
struct BlockUD {
    std::string id;
    int         x = 0, y = 0, z = 0;
    bool        hasPos = false;
    std::map<std::string, std::string> props;
};
std::map<std::string, std::string> defaultProps(std::string const& id) {
    std::map<std::string, std::string> r;
    mcdata::Block const* b = blockInfo(id);
    if (!b || !*b->props) return r;
    std::string s = b->props;
    size_t p = 0;
    while (p < s.size()) {
        size_t q = s.find(',', p);
        if (q == std::string::npos) q = s.size();
        std::string kv = s.substr(p, q - p);
        if (size_t e = kv.find('='); e != std::string::npos) r[kv.substr(0, e)] = kv.substr(e + 1);
        p = q + 1;
    }
    return r;
}
void pushBlock(lua_State* L, std::string const& id, int x, int y, int z, std::map<std::string, std::string> const& props, bool hasPos) {
    BlockUD b;
    b.id     = resLoc(id);
    b.x      = x; b.y = y; b.z = z;
    b.hasPos = hasPos;
    b.props  = defaultProps(b.id);
    for (auto const& [k, v] : props) b.props[k] = v;
    newUD(L, tBlock, std::move(b));
}
void pushWorldBlock(lua_State* L, int x, int y, int z) {
    auto& w = W(L);
    if (!w.chunkLoaded(x, y, z)) { pushBlock(L, "minecraft:void_air", x, y, z, {}, true); return; }
    pushBlock(L, w.blockId(x, y, z), x, y, z, w.blockProperties(x, y, z), true);
}
BlockUD& blk(lua_State* L, char const* fn) {
    void* p = testUD(L, 1, tBlock);
    if (!p) luaL_error(L, "bad argument 1 to '%s' (BlockState expected, got %s)", fn, figuraTypeName(L, 1).c_str());
    return *static_cast<BlockUD*>(p);
}
bool blkAir(BlockUD const& b) { std::string n = stripNs(b.id); return n == "air" || n == "cave_air" || n == "void_air"; }
bool blkFluid(BlockUD const& b) { std::string n = stripNs(b.id); return n == "water" || n == "lava" || n == "bubble_column"; }
bool blkNoCollision(BlockUD const& b) { auto* i = blockInfo(b.id); return i ? (i->flags & 2) != 0 : blkAir(b); }
bool blkTransparent(BlockUD const& b) { auto* i = blockInfo(b.id); return i ? (i->flags & 1) != 0 : blkAir(b); }
int  blGetID(lua_State* L) { return pushS(L, blk(L, "getID").id); }
int  blGetProperties(lua_State* L) {
    auto& b = blk(L, "getProperties");
    lua_newtable(L);
    for (auto const& [k, v] : b.props) { lua_pushstring(L, v.c_str()); lua_setfield(L, -2, k.c_str()); }
    return 1;
}
int blGetPos(lua_State* L) { auto& b = blk(L, "getPos"); pushVec3(L, {double(b.x), double(b.y), double(b.z)}); return 1; }
int blSetPos(lua_State* L) {
    auto& b = blk(L, "setPos");
    Vec3  v = parseVec3(L, 2, "setPos");
    b.x = blockFloor(v.x); b.y = blockFloor(v.y); b.z = blockFloor(v.z);
    b.hasPos = true;
    return ret1(L);
}
int blIsTranslucent(lua_State* L) { auto& b = blk(L, "isTranslucent"); auto* i = blockInfo(b.id); return pushB(L, i ? i->filter == 0 && !blkFluid(b) : blkAir(b)); }
int blGetOpacity(lua_State* L) { auto& b = blk(L, "getOpacity"); auto* i = blockInfo(b.id); return pushI(L, i ? i->filter : 0); }
int blGetMapColor(lua_State* L) {
    auto&       b = blk(L, "getMapColor");
    std::string n = stripNs(b.id);
    uint32_t    c = 0x707070;
    if (blkAir(b)) c = 0;
    else if (n == "water" || n == "bubble_column") c = 0x4040FF;
    else if (n == "lava") c = 0xFF0000;
    else if (n == "grass_block" || n == "slime_block") c = 0x7FB238;
    else if (endsWith(n, "_leaves") || n == "grass" || n == "tall_grass" || n == "fern") c = 0x007C00;
    else if (n == "sand" || n == "sandstone" || endsWith(n, "_sandstone") || n == "birch_planks") c = 0xF7E9A3;
    else if (n == "dirt" || n == "coarse_dirt" || n == "farmland" || n == "jungle_planks") c = 0x976D4D;
    else if (n == "snow" || n == "snow_block" || n == "white_wool" || n == "powder_snow") c = 0xFFFFFF;
    else if (n == "ice" || n == "packed_ice" || n == "blue_ice") c = 0xA0A0FF;
    else if (endsWith(n, "_planks") || endsWith(n, "_log") || n == "oak_wood") c = 0x8F7748;
    else if (n == "netherrack" || n == "nether_bricks") c = 0x700200;
    else if (n == "gold_block") c = 0xFAEE4D;
    else if (n == "diamond_block") c = 0x5CDBD5;
    else if (n == "iron_block") c = 0xA7A7A7;
    pushVec3(L, intToRGB(c));
    return 1;
}
int blIsSolidBlock(lua_State* L) { auto& b = blk(L, "isSolidBlock"); auto* i = blockInfo(b.id); return pushB(L, i && !(i->flags & 3) && i->filter == 15); }
int blIsFullCube(lua_State* L) {
    auto&       b = blk(L, "isFullCube");
    std::string n = stripNs(b.id);
    bool partial = endsWith(n, "_slab") || endsWith(n, "_stairs") || endsWith(n, "_fence") || endsWith(n, "_wall") ||
                   endsWith(n, "_pane") || endsWith(n, "_door") || endsWith(n, "_trapdoor") || endsWith(n, "_carpet") ||
                   n == "snow" || endsWith(n, "_bed") || n == "chest" || n == "trapped_chest" || n == "ender_chest" ||
                   n == "soul_sand" || n == "farmland" || n == "dirt_path" || n == "cactus" || n == "cake" || endsWith(n, "_fence_gate");
    if (endsWith(n, "_slab") && b.props["type"] == "double") partial = false;
    return pushB(L, !blkNoCollision(b) && !partial);
}
int blHasEmissiveLighting(lua_State* L) { std::string n = stripNs(blk(L, "hasEmissiveLighting").id); return pushB(L, n == "magma_block"); }
int blGetHardness(lua_State* L) { auto& b = blk(L, "getHardness"); auto* i = blockInfo(b.id); return pushN(L, i ? i->hardness : 0); }
int blGetComparatorOutput(lua_State* L) { (void)blk(L, "getComparatorOutput"); return pushI(L, 0); }
int blHasBlockEntity(lua_State* L) {
    std::string n = stripNs(blk(L, "hasBlockEntity").id);
    static char const* be[] = {"chest", "furnace", "smoker", "hopper", "dispenser", "dropper", "barrel", "shulker_box", "_sign",
                               "_banner", "_bed", "beacon", "spawner", "brewing_stand", "enchanting_table", "end_portal",
                               "end_gateway", "jukebox", "lectern", "campfire", "beehive", "bee_nest", "bell", "conduit",
                               "comparator", "daylight_detector", "command_block", "structure_block", "jigsaw", "_head",
                               "_skull", "piston_head", "moving_piston", "sculk_sensor", "sculk_catalyst", "sculk_shrieker",
                               "chiseled_bookshelf", "decorated_pot", "suspicious_sand", "suspicious_gravel", "calibrated_sculk_sensor"};
    for (auto s : be) if (s[0] == '_' ? endsWith(n, s) : (n == s || endsWith(n, (std::string("_") + s).c_str()))) return pushB(L, true);
    return pushB(L, false);
}
int blIsOpaque(lua_State* L) { auto& b = blk(L, "isOpaque"); return pushB(L, !blkAir(b) && !blkTransparent(b) && !blkFluid(b)); }
int blEmitsRedstonePower(lua_State* L) {
    std::string n = stripNs(blk(L, "emitsRedstonePower").id);
    return pushB(L, n == "redstone_block" || n == "lever" || endsWith(n, "_button") || n == "redstone_torch" ||
                        n == "redstone_wall_torch" || n == "repeater" || n == "comparator" || n == "observer" ||
                        n == "daylight_detector" || endsWith(n, "_pressure_plate") || n == "tripwire_hook" || n == "target" ||
                        n == "trapped_chest" || has(n, "sculk_sensor") || n == "lectern" || n == "detector_rail" || n == "redstone_wire");
}
int blGetLuminance(lua_State* L) { auto& b = blk(L, "getLuminance"); auto* i = blockInfo(b.id); return pushI(L, i ? i->light : 0); }
int blGetFriction(lua_State* L) {
    std::string n = stripNs(blk(L, "getFriction").id);
    return pushN(L, n == "ice" || n == "packed_ice" || n == "frosted_ice" ? 0.98f : n == "blue_ice" ? 0.989f : n == "slime_block" ? 0.8f : 0.6f);
}
int blGetVelocityMultiplier(lua_State* L) { std::string n = stripNs(blk(L, "getVelocityMultiplier").id); return pushN(L, n == "soul_sand" || n == "honey_block" ? 0.4f : 1.0f); }
int blGetJumpVelocityMultiplier(lua_State* L) { std::string n = stripNs(blk(L, "getJumpVelocityMultiplier").id); return pushN(L, n == "honey_block" ? 0.5f : 1.0f); }
int blGetBlastResistance(lua_State* L) { auto& b = blk(L, "getBlastResistance"); auto* i = blockInfo(b.id); return pushN(L, i ? i->resistance : 0); }
int blAsItem(lua_State* L) {
    auto&     b = blk(L, "asItem");
    ItemState it;
    if (itemInfo(b.id) && !blkAir(b)) { it.id = b.id; it.count = 1; }
    pushItem(L, it);
    return 1;
}
int blGetTags(lua_State* L) {
    auto&       b = blk(L, "getTags");
    std::string n = stripNs(b.id);
    std::vector<std::string> t;
    auto add = [&](char const* s) { t.push_back(s); };
    if (endsWith(n, "_leaves")) { add("minecraft:leaves"); add("minecraft:mineable/hoe"); }
    if (endsWith(n, "_log") || endsWith(n, "_wood") || endsWith(n, "_stem") || endsWith(n, "_hyphae")) { add("minecraft:logs"); add("minecraft:mineable/axe"); }
    if (endsWith(n, "_planks")) { add("minecraft:planks"); add("minecraft:mineable/axe"); }
    if (endsWith(n, "_wool")) add("minecraft:wool");
    if (endsWith(n, "_stairs")) add("minecraft:stairs");
    if (endsWith(n, "_slab")) add("minecraft:slabs");
    if (endsWith(n, "_fence")) add("minecraft:fences");
    if (endsWith(n, "_wall")) add("minecraft:walls");
    if (endsWith(n, "_door")) add("minecraft:doors");
    if (endsWith(n, "_bed")) add("minecraft:beds");
    if (n == "dirt" || n == "grass_block" || n == "podzol" || n == "coarse_dirt" || n == "mycelium" || n == "rooted_dirt" || n == "moss_block" || n == "mud")
        { add("minecraft:dirt"); add("minecraft:mineable/shovel"); }
    if (n == "sand" || n == "red_sand" || n == "gravel" || n == "clay" || n == "snow" || n == "snow_block" || n == "soul_sand" || n == "soul_soil")
        add("minecraft:mineable/shovel");
    if (n == "stone" || n == "cobblestone" || n == "deepslate" || n == "andesite" || n == "diorite" || n == "granite" || n == "tuff" ||
        has(n, "_ore") || endsWith(n, "_bricks") || n == "obsidian" || n == "netherrack" || n == "basalt" || n == "blackstone")
        add("minecraft:mineable/pickaxe");
    if (n == "ice" || n == "packed_ice" || n == "blue_ice" || n == "frosted_ice") add("minecraft:ice");
    if (n == "fire" || n == "soul_fire") add("minecraft:fire");
    if (n == "ladder" || n == "vine" || n == "scaffolding" || n == "twisting_vines" || n == "weeping_vines" || n == "cave_vines") add("minecraft:climbable");
    if (has(n, "flower") || n == "dandelion" || n == "poppy" || n == "blue_orchid" || n == "allium" || endsWith(n, "_tulip") ||
        n == "oxeye_daisy" || n == "cornflower" || n == "lily_of_the_valley")
        add("minecraft:flowers");
    return pushStrings(L, t);
}
int blHasCollision(lua_State* L) { auto& b = blk(L, "hasCollision"); return pushB(L, !blkNoCollision(b)); }
void pushShape(lua_State* L, std::vector<std::pair<Vec3, Vec3>> const& boxes) {
    lua_createtable(L, int(boxes.size()), 0);
    for (size_t i = 0; i < boxes.size(); i++) {
        lua_createtable(L, 2, 0);
        pushVec3(L, boxes[i].first);
        lua_rawseti(L, -2, 1);
        pushVec3(L, boxes[i].second);
        lua_rawseti(L, -2, 2);
        lua_rawseti(L, -2, int(i + 1));
    }
}
std::vector<std::pair<Vec3, Vec3>> blockShape(BlockUD& b, bool collision) {
    std::string n = stripNs(b.id);
    if (blkAir(b) || blkFluid(b) || (collision && blkNoCollision(b))) return {};
    if (endsWith(n, "_slab")) {
        std::string t = b.props["type"];
        if (t == "top") return {{{0, 0.5, 0}, {1, 1, 1}}};
        if (t == "double") return {{{0, 0, 0}, {1, 1, 1}}};
        return {{{0, 0, 0}, {1, 0.5, 1}}};
    }
    if (endsWith(n, "_carpet")) return {{{0, 0, 0}, {1, 0.0625, 1}}};
    if (n == "snow") { double l = std::max(1, std::atoi(b.props["layers"].c_str())); return {{{0, 0, 0}, {1, (collision ? l - 1 : l) / 8.0, 1}}}; }
    if (endsWith(n, "_bed")) return {{{0, 0, 0}, {1, 0.5625, 1}}};
    if (n == "chest" || n == "trapped_chest" || n == "ender_chest") return {{{0.0625, 0, 0.0625}, {0.9375, 0.875, 0.9375}}};
    if (n == "farmland" || n == "dirt_path") return {{{0, 0, 0}, {1, 0.9375, 1}}};
    if (n == "soul_sand" && collision) return {{{0, 0, 0}, {1, 0.875, 1}}};
    if (blkNoCollision(b)) return {{{0.25, 0, 0.25}, {0.75, 0.75, 0.75}}};
    return {{{0, 0, 0}, {1, 1, 1}}};
}
int blGetCollisionShape(lua_State* L) { pushShape(L, blockShape(blk(L, "getCollisionShape"), true)); return 1; }
int blGetOutlineShape(lua_State* L) { pushShape(L, blockShape(blk(L, "getOutlineShape"), false)); return 1; }
int blGetSounds(lua_State* L) {
    std::string n = stripNs(blk(L, "getSounds").id);
    std::string g = "stone";
    if (endsWith(n, "_planks") || endsWith(n, "_log") || endsWith(n, "_wood") || n == "chest" || n == "crafting_table" || n == "bookshelf") g = "wood";
    else if (n == "grass_block" || endsWith(n, "_leaves") || n == "grass" || n == "hay_block" || n == "moss_block") g = "grass";
    else if (n == "sand" || n == "red_sand" || n == "dirt" || n == "coarse_dirt") g = n == "sand" || n == "red_sand" ? "sand" : "gravel";
    else if (n == "gravel") g = "gravel";
    else if (endsWith(n, "_wool") || endsWith(n, "_carpet")) g = "wool";
    else if (has(n, "glass") || n == "ice" || n == "packed_ice") g = "glass";
    else if (n == "snow" || n == "snow_block") g = "snow";
    else if (n == "iron_block" || n == "gold_block" || n == "diamond_block" || n == "emerald_block" || n == "netherite_block") g = "metal";
    lua_newtable(L);
    lua_pushnumber(L, 1); lua_setfield(L, -2, "pitch");
    lua_pushnumber(L, 1); lua_setfield(L, -2, "volume");
    for (char const* k : {"break", "fall", "hit", "place", "step"}) {
        std::string s = "minecraft:block." + g + "." + k;
        lua_pushstring(L, s.c_str());
        lua_setfield(L, -2, k);
    }
    return 1;
}
int blGetFluidTags(lua_State* L) {
    auto&       b = blk(L, "getFluidTags");
    std::string n = stripNs(b.id);
    std::vector<std::string> t;
    if (n == "water" || n == "bubble_column" || b.props["waterlogged"] == "true" || n == "kelp" || n == "kelp_plant" || n == "seagrass" || n == "tall_seagrass")
        t.push_back("minecraft:water");
    if (n == "lava") t.push_back("minecraft:lava");
    return pushStrings(L, t);
}
int blGetEntityData(lua_State* L) { (void)blk(L, "getEntityData"); return pushNil(L); }
std::string stateString(BlockUD const& b) {
    std::string s = b.id;
    if (!b.props.empty()) {
        s += "[";
        bool first = true;
        for (auto const& [k, v] : b.props) { s += (first ? "" : ",") + k + "=" + v; first = false; }
        s += "]";
    }
    return s;
}
int blToStateString(lua_State* L) { return pushS(L, stateString(blk(L, "toStateString")) + "{}"); }
int blGetTextures(lua_State* L) {
    auto& b = blk(L, "getTextures");
    lua_newtable(L);
    if (blkAir(b) || blkFluid(b)) return 1;
    std::string tex = "minecraft:textures/block/" + stripNs(b.id);
    for (char const* face : {"DOWN", "UP", "NORTH", "SOUTH", "WEST", "EAST", "PARTICLE"}) {
        lua_createtable(L, 1, 0);
        lua_pushstring(L, tex.c_str());
        lua_rawseti(L, -2, 1);
        lua_setfield(L, -2, face);
    }
    lua_newtable(L);
    lua_setfield(L, -2, "NONE");
    return 1;
}
int blIsAir(lua_State* L) { return pushB(L, blkAir(blk(L, "isAir"))); }
int blEq(lua_State* L) {
    auto* a = static_cast<BlockUD*>(testUD(L, 1, tBlock));
    auto* b = static_cast<BlockUD*>(testUD(L, 2, tBlock));
    return pushB(L, a && b && a->id == b->id && a->props == b->props);
}
int blIndex(lua_State* L) {
    auto& b = blk(L, "__index");
    if (lua_type(L, 2) != LUA_TSTRING) return 0;
    std::string k = lua_tostring(L, 2);
    if (k == "id") return pushS(L, b.id);
    if (k == "properties") { lua_settop(L, 1); return blGetProperties(L); }
    return 0;
}
int blToString(lua_State* L) { return pushS(L, blk(L, "__tostring").id + " (BlockState)"); }

/// BlockStateArgument: "minecraft:oak_stairs[facing=east]{...}"
bool parseBlockString(std::string s, std::string& id, std::map<std::string, std::string>& props) {
    size_t i = s.find_first_not_of(' ');
    if (i == std::string::npos) return false;
    s = s.substr(i);
    size_t br = s.find_first_of("[{");
    id = lower(s.substr(0, br));
    while (!id.empty() && id.back() == ' ') id.pop_back();
    if (id.rfind("minecraft:", 0) != 0 && id.find(':') != std::string::npos) return false;
    auto* info = blockInfo(id);
    if (!info) return false;
    id = resLoc(id);
    if (br != std::string::npos && s[br] == '[') {
        size_t e = s.find(']', br);
        if (e == std::string::npos) return false;
        std::string body = s.substr(br + 1, e - br - 1);
        auto def = defaultProps(id);
        size_t p = 0;
        while (p < body.size()) {
            size_t q = body.find(',', p);
            if (q == std::string::npos) q = body.size();
            std::string kv = body.substr(p, q - p);
            if (size_t eq = kv.find('='); eq != std::string::npos) {
                std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
                auto trim = [](std::string& x) { while (!x.empty() && x.front() == ' ') x.erase(0, 1); while (!x.empty() && x.back() == ' ') x.pop_back(); };
                trim(k); trim(v);
                if (!def.count(k)) return false; // unknown property
                props[k] = v;
            }
            p = q + 1;
        }
    }
    return true;
}

// ---------------------------------------------------------------- Biome (value userdata)
struct BiomeUD {
    std::string id;
    int         x = 0, y = 0, z = 0;
};
BiomeUD& bio(lua_State* L, char const* fn) {
    void* p = testUD(L, 1, tBiome);
    if (!p) luaL_error(L, "bad argument 1 to '%s' (Biome expected, got %s)", fn, figuraTypeName(L, 1).c_str());
    return *static_cast<BiomeUD*>(p);
}
float biomeBaseTemp(BiomeUD const& b) { auto* i = biomeInfo(b.id); return i ? i->temperature : 0.8f; }
float biomeTempAt(BiomeUD const& b) { // Biome.getHeightAdjustedTemperature (noise term left out)
    float t = biomeBaseTemp(b);
    if (stripNs(b.id) == "frozen_ocean" || stripNs(b.id) == "deep_frozen_ocean") t = std::min(t, 0.2f);
    return b.y > 80 ? t - (float(b.y) - 80.0f) * 0.05f / 40.0f : t;
}
float biomeDownfall(BiomeUD const& b) {
    std::string n = stripNs(b.id);
    auto* i = biomeInfo(b.id);
    if (i && !i->precipitation) return 0;
    if (has(n, "jungle") || has(n, "swamp") || n == "mangrove_swamp") return 0.9f;
    if (n == "mushroom_fields") return 1.0f;
    if (has(n, "forest") || has(n, "taiga") || n == "river" || n == "cherry_grove" || n == "dark_forest") return has(n, "taiga") ? 0.8f : 0.8f;
    if (has(n, "ocean") || n == "beach" || has(n, "snowy") || has(n, "frozen") || n == "ice_spikes" || n == "grove" ||
        has(n, "slopes") || has(n, "peaks") || n == "the_end" || has(n, "end_") || n == "small_end_islands")
        return 0.5f;
    if (n == "meadow") return 0.8f;
    if (has(n, "windswept")) return 0.3f;
    return 0.4f;
}
Vec3 colormap(double temp, double down, Vec3 hotWet, Vec3 hotDry, Vec3 cold) {
    double t = std::clamp(temp, 0.0, 1.0), d = std::clamp(down, 0.0, 1.0) * t;
    double wc = 1 - t, wd = t - d, ww = d;
    return {(cold.x * wc + hotDry.x * wd + hotWet.x * ww) / 255.0, (cold.y * wc + hotDry.y * wd + hotWet.y * ww) / 255.0,
            (cold.z * wc + hotDry.z * wd + hotWet.z * ww) / 255.0};
}
int bioGetPos(lua_State* L) { auto& b = bio(L, "getPos"); pushVec3(L, {double(b.x), double(b.y), double(b.z)}); return 1; }
int bioSetPos(lua_State* L) {
    auto& b = bio(L, "setPos");
    Vec3  v = parseVec3(L, 2, "setPos");
    b.x = blockFloor(v.x); b.y = blockFloor(v.y); b.z = blockFloor(v.z);
    return ret1(L);
}
int bioGetTags(lua_State* L) {
    auto&       b = bio(L, "getTags");
    std::string n = stripNs(b.id);
    auto*       i = biomeInfo(b.id);
    std::string dim = i ? i->dimension : "overworld";
    std::vector<std::string> t;
    if (dim == "nether") t.push_back("minecraft:is_nether");
    else if (dim == "end") t.push_back("minecraft:is_end");
    else t.push_back("minecraft:is_overworld");
    if (has(n, "ocean")) t.push_back("minecraft:is_ocean");
    if (has(n, "deep_") && has(n, "ocean")) t.push_back("minecraft:is_deep_ocean");
    if (n == "beach" || n == "snowy_beach") t.push_back("minecraft:is_beach");
    if (n == "river" || n == "frozen_river") t.push_back("minecraft:is_river");
    if (has(n, "forest") || n == "grove") t.push_back("minecraft:is_forest");
    if (has(n, "taiga")) t.push_back("minecraft:is_taiga");
    if (has(n, "jungle")) t.push_back("minecraft:is_jungle");
    if (has(n, "savanna")) t.push_back("minecraft:is_savanna");
    if (has(n, "badlands")) t.push_back("minecraft:is_badlands");
    if (has(n, "peaks") || has(n, "slopes") || n == "meadow" || n == "grove" || n == "cherry_grove") t.push_back("minecraft:is_mountain");
    if (has(n, "windswept") || has(n, "hills")) t.push_back("minecraft:is_hill");
    return pushStrings(L, t);
}
int bioGetTemperature(lua_State* L) { return pushN(L, biomeBaseTemp(bio(L, "getTemperature"))); }
int bioGetPrecipitation(lua_State* L) {
    auto& b = bio(L, "getPrecipitation");
    auto* i = biomeInfo(b.id);
    if (i && !i->precipitation) return pushS(L, "NONE");
    return pushS(L, biomeTempAt(b) < 0.15f ? "SNOW" : "RAIN");
}
int bioGetSkyColor(lua_State* L) {
    auto&  b = bio(L, "getSkyColor");
    auto*  i = biomeInfo(b.id);
    if (i && std::string(i->dimension) == "end") { pushVec3(L, {0, 0, 0}); return 1; }
    double t = std::clamp(biomeBaseTemp(b) / 3.0, -1.0, 1.0);
    Vec3   c = hsvToRgb(0.62222224 - t * 0.05, 0.5 + t * 0.1, 1.0);
    pushVec3(L, intToRGB(rgbToInt(c)));
    return 1;
}
int bioGetFoliageColor(lua_State* L) {
    auto&       b = bio(L, "getFoliageColor");
    std::string n = stripNs(b.id);
    if (has(n, "badlands")) { pushVec3(L, intToRGB(0x9E814D)); return 1; }
    if (n == "swamp") { pushVec3(L, intToRGB(0x6A7039)); return 1; }
    if (n == "mangrove_swamp") { pushVec3(L, intToRGB(0x8DB127)); return 1; }
    pushVec3(L, colormap(biomeBaseTemp(b), biomeDownfall(b), {0x1A, 0xBF, 0x00}, {0xAE, 0xA4, 0x2A}, {0x60, 0xA1, 0x7B}));
    return 1;
}
int bioGetGrassColor(lua_State* L) {
    auto&       b = bio(L, "getGrassColor");
    std::string n = stripNs(b.id);
    if (has(n, "badlands")) { pushVec3(L, intToRGB(0x90814D)); return 1; }
    if (n == "swamp" || n == "mangrove_swamp") { pushVec3(L, intToRGB(0x6A7039)); return 1; }
    if (n == "cherry_grove") { pushVec3(L, intToRGB(0xB6DB61)); return 1; }
    Vec3 c = colormap(biomeBaseTemp(b), biomeDownfall(b), {0x47, 0xCD, 0x33}, {0xBF, 0xB7, 0x55}, {0x80, 0xB4, 0x97});
    if (n == "dark_forest") { uint32_t i = rgbToInt(c); i = ((i & 0xFEFEFE) + 0x28340A) >> 1; c = intToRGB(i); }
    pushVec3(L, c);
    return 1;
}
int bioGetFogColor(lua_State* L) {
    std::string n = stripNs(bio(L, "getFogColor").id);
    uint32_t c = 0xC0D8FF;
    if (n == "nether_wastes") c = 0x330808;
    else if (n == "soul_sand_valley") c = 0x1B4745;
    else if (n == "crimson_forest") c = 0x330303;
    else if (n == "warped_forest") c = 0x1A051A;
    else if (n == "basalt_deltas") c = 0x685F70;
    else if (n == "the_end" || has(n, "end_") || n == "small_end_islands" || n == "the_void") c = n == "the_void" ? 0xC0D8FF : 0xA080A0;
    pushVec3(L, intToRGB(c));
    return 1;
}
int bioGetWaterColor(lua_State* L) {
    std::string n = stripNs(bio(L, "getWaterColor").id);
    uint32_t c = 0x3F76E4;
    if (n == "swamp") c = 0x617B64;
    else if (n == "mangrove_swamp") c = 0x3A7A6A;
    else if (n == "warm_ocean") c = 0x43D5EE;
    else if (n == "lukewarm_ocean" || n == "deep_lukewarm_ocean") c = 0x45ADF2;
    else if (n == "cold_ocean" || n == "deep_cold_ocean" || n == "snowy_taiga" || n == "snowy_beach") c = n == "snowy_taiga" || n == "snowy_beach" ? 0x3D57D6 : 0x3D57D6;
    else if (n == "frozen_ocean" || n == "deep_frozen_ocean" || n == "frozen_river") c = 0x3938C9;
    else if (n == "cherry_grove") c = 0x5DB7EF;
    else if (n == "meadow") c = 0x0E4ECF;
    pushVec3(L, intToRGB(c));
    return 1;
}
int bioGetWaterFogColor(lua_State* L) {
    std::string n = stripNs(bio(L, "getWaterFogColor").id);
    uint32_t c = 0x050533;
    if (n == "swamp") c = 0x232317;
    else if (n == "mangrove_swamp") c = 0x4D7A60;
    else if (n == "warm_ocean") c = 0x041F33;
    else if (n == "lukewarm_ocean" || n == "deep_lukewarm_ocean") c = 0x041633;
    else if (n == "cold_ocean" || n == "deep_cold_ocean") c = 0x050533;
    else if (n == "frozen_ocean" || n == "deep_frozen_ocean" || n == "frozen_river") c = 0x050533;
    else if (n == "cherry_grove") c = 0x5DB7EF;
    pushVec3(L, intToRGB(c));
    return 1;
}
int bioGetDownfall(lua_State* L) { return pushN(L, biomeDownfall(bio(L, "getDownfall"))); }
int bioIsHot(lua_State* L) { return pushB(L, biomeTempAt(bio(L, "isHot")) > 1.0f); }
int bioIsCold(lua_State* L) { return pushB(L, biomeTempAt(bio(L, "isCold")) < 0.15f); }
int bioEq(lua_State* L) {
    auto* a = static_cast<BiomeUD*>(testUD(L, 1, tBiome));
    auto* b = static_cast<BiomeUD*>(testUD(L, 2, tBiome));
    return pushB(L, a && b && a->id == b->id);
}
int bioIndex(lua_State* L) {
    auto& b = bio(L, "__index");
    if (lua_type(L, 2) == LUA_TSTRING && std::string(lua_tostring(L, 2)) == "id") return pushS(L, b.id);
    return 0;
}
int bioToString(lua_State* L) { return pushS(L, bio(L, "__tostring").id + " (Biome)"); }

// ---------------------------------------------------------------- entities
// One userdata layout for every entity type: `self` = the avatar's own player (reads Avatar::player live), else an
// EntityRef snapshot. kind: 0 EntityAPI, 1 LivingEntityAPI, 2 PlayerAPI, 3 ViewerAPI.
struct EntUD {
    uint8_t   kind = 0;
    bool      self = false;
    EntityRef e;
};
EntUD* toEnt(lua_State* L, int i) {
    if (lua_type(L, i) != LUA_TUSERDATA) return nullptr;
    for (uint32_t t : {tPlayer, tEntity, tLiving, tViewer})
        if (void* p = testUD(L, i, t)) return static_cast<EntUD*>(p);
    return nullptr;
}
EntUD* ent(lua_State* L, char const* fn) {
    EntUD* u = toEnt(L, 1);
    if (!u) luaL_error(L, "bad argument 1 to '%s' (EntityAPI expected, got %s)", fn, figuraTypeName(L, 1).c_str());
    return u;
}
void pushSelfPlayer(lua_State* L) { lua_getfield(L, LUA_REGISTRYINDEX, "fb.player"); }
void pushEntity(lua_State* L, EntityRef const& e) {
    if (!e.valid) { lua_pushnil(L); return; }
    auto const& p = P(L);
    if (e.isPlayer && !p.uuid.empty() && e.uuid == p.uuid) { pushSelfPlayer(L); return; }
    EntUD u;
    u.e    = e;
    auto* info = entityInfo(e.type);
    bool  living = e.living || e.isPlayer || (info && info->living);
    u.kind = e.isPlayer || stripNs(e.type) == "player" ? 2 : living ? 1 : 0;
    newUD(L, u.kind == 2 ? tPlayer : u.kind == 1 ? tLiving : tEntity, std::move(u));
}
float entWidth(EntityRef const& e) { if (e.width >= 0) return e.width; auto* i = entityInfo(e.type); return i ? i->width : 0.6f; }
float entHeight(EntityRef const& e) { if (e.height >= 0) return e.height; auto* i = entityInfo(e.type); return i ? i->height : 1.8f; }
float entEye(EntityRef const& e) { return e.eyeHeight >= 0 ? e.eyeHeight : entHeight(e) * 0.85f; }
std::string entType(EntUD const* u) { return u->self ? "minecraft:player" : resLoc(u->e.type); }
std::string entName(lua_State* L, EntUD const* u) {
    if (u->self) return P(L).name;
    if (!u->e.name.empty()) return u->e.name;
    auto* i = entityInfo(u->e.type);
    return i ? i->name : stripNs(u->e.type);
}
Vec3 entPos(lua_State* L, EntUD const* u, double d) {
    if (u->self) { auto const& p = P(L); return p.prevPos + (p.pos - p.prevPos) * d; }
    Vec3 prev = u->e.prevPos == Vec3{} ? u->e.pos : u->e.prevPos;
    return prev + (u->e.pos - prev) * d;
}
float entEyeHeight(lua_State* L, EntUD const* u) { return u->self ? P(L).eyeHeight : entEye(u->e); }
Vec3  entLook(lua_State* L, EntUD const* u) { return u->self ? lookDir(P(L).pitch, P(L).yaw) : lookDir(u->e.rot.x, u->e.rot.y); }
Vec3  entVel(lua_State* L, EntUD const* u) { return u->self ? P(L).velocity : u->e.velocity; }

#define ENT_FN(name, selfExpr, otherExpr)                                                                            \
    int name(lua_State* L) {                                                                                         \
        EntUD*             u = ent(L, #name);                                                                        \
        PlayerState const& p = P(L);                                                                                 \
        EntityRef const&   e = u->e;                                                                                 \
        (void)p; (void)e;                                                                                            \
        if (u->self) return selfExpr;                                                                                \
        return otherExpr;                                                                                            \
    }

int enIsLoaded(lua_State* L) { EntUD* u = ent(L, "isLoaded"); return pushB(L, u->self ? P(L).loaded : true); }
int enGetPos(lua_State* L) { EntUD* u = ent(L, "getPos"); pushVec3(L, entPos(L, u, optNum(L, 2, 1))); return 1; }
int enGetRot(lua_State* L) {
    EntUD* u = ent(L, "getRot");
    float  d = float(optNum(L, 2, 1));
    if (u->self) { auto const& p = P(L); pushVec2(L, lerpf(p.prevPitch, p.pitch, d), lerpf(p.prevYaw, p.yaw, d)); }
    else pushVec2(L, u->e.rot.x, u->e.rot.y);
    return 1;
}
int enGetUUID(lua_State* L) { EntUD* u = ent(L, "getUUID"); return pushS(L, u->self ? P(L).uuid : u->e.uuid); }
int enGetType(lua_State* L) { return pushS(L, entType(ent(L, "getType"))); }
int enGetVelocity(lua_State* L) { pushVec3(L, entVel(L, ent(L, "getVelocity"))); return 1; }
int enGetLookDir(lua_State* L) { pushVec3(L, entLook(L, ent(L, "getLookDir"))); return 1; }
ENT_FN(enGetFrozenTicks, pushI(L, p.frozenTicks), pushI(L, 0))
ENT_FN(enGetMaxAir, pushI(L, p.maxAir), pushI(L, 300))
ENT_FN(enGetDimensionName, pushS(L, p.dimension), pushS(L, W(L).dimension()))
ENT_FN(enGetPose, pushS(L, p.pose), pushS(L, e.pose))
int enGetVehicle(lua_State* L) { EntUD* u = ent(L, "getVehicle"); if (u->self) pushEntity(L, P(L).vehicle); else lua_pushnil(L); return 1; }
ENT_FN(enIsOnGround, pushB(L, p.onGround), pushB(L, e.onGround))
int enGetEyeHeight(lua_State* L) { EntUD* u = ent(L, "getEyeHeight"); return pushN(L, entEyeHeight(L, u)); }
int enGetBoundingBox(lua_State* L) {
    EntUD* u = ent(L, "getBoundingBox");
    if (u->self) {
        auto const& p = P(L);
        double h = p.sleeping ? 0.2 : (p.swimming || p.gliding || p.pose == "SPIN_ATTACK") ? 0.6 : p.crouching ? 1.5 : p.pose == "DYING" ? 0.2 : 1.8;
        double w = p.sleeping || p.pose == "DYING" ? 0.2 : 0.6;
        pushVec3(L, {w, h, w});
    } else pushVec3(L, {entWidth(u->e), entHeight(u->e), entWidth(u->e)});
    return 1;
}
int enGetName(lua_State* L) { return pushS(L, entName(L, ent(L, "getName"))); }
ENT_FN(enIsWet, pushB(L, p.inWater || p.underwater || p.inRain), pushB(L, e.inWater || e.underwater))
ENT_FN(enIsInWater, pushB(L, p.inWater || p.underwater), pushB(L, e.inWater || e.underwater))
ENT_FN(enIsUnderwater, pushB(L, p.underwater), pushB(L, e.underwater))
ENT_FN(enIsInLava, pushB(L, p.inLava), pushB(L, e.inLava))
ENT_FN(enIsInRain, pushB(L, p.inRain), pushB(L, false))
ENT_FN(enHasAvatar, pushB(L, true), pushB(L, e.hasAvatar))
ENT_FN(enIsSprinting, pushB(L, p.sprinting), pushB(L, e.sprinting))
int enGetEyeY(lua_State* L) { EntUD* u = ent(L, "getEyeY"); return pushN(L, entPos(L, u, 1).y + entEyeHeight(L, u)); }
ENT_FN(enIsGlowing, pushB(L, p.glowing), pushB(L, e.glowing))
ENT_FN(enIsInvisible, pushB(L, p.invisible), pushB(L, e.invisible))
ENT_FN(enIsSilent, pushB(L, p.silent), pushB(L, e.silent))
ENT_FN(enIsSneaking, pushB(L, p.crouching), pushB(L, e.crouching))
ENT_FN(enIsCrouching, pushB(L, p.crouching && p.pose == "CROUCHING" ? true : p.crouching), pushB(L, e.crouching))
int enIsMoving(lua_State* L) {
    EntUD* u      = ent(L, "isMoving");
    bool   ignoreY = lua_toboolean(L, 2) != 0;
    Vec3   v      = u->self ? P(L).pos - P(L).prevPos : (u->e.prevPos == Vec3{} ? u->e.velocity : u->e.pos - u->e.prevPos);
    if (u->self && v == Vec3{}) v = P(L).velocity;
    return pushB(L, v.x != 0 || (!ignoreY && v.y != 0) || v.z != 0);
}
int enIsFalling(lua_State* L) {
    EntUD* u = ent(L, "isFalling");
    bool   g = u->self ? P(L).onGround : u->e.onGround;
    return pushB(L, !g && entVel(L, u).y < 0);
}
int enGetItem(lua_State* L) {
    EntUD* u   = ent(L, "getItem");
    int    idx = int(luaL_checkinteger(L, 2)) - 1;
    if (idx < 0) return pushNil(L);
    if (u->self) {
        auto const& p = P(L);
        ItemState const* s[] = {&p.mainHand, &p.offHand, &p.boots, &p.legs, &p.chest, &p.helmet};
        if (idx >= 6) return pushNil(L);
        pushItem(L, *s[idx]);
        return 1;
    }
    if (u->kind == 0) return pushNil(L);
    auto const& e = u->e;
    ItemState const* s[] = {&e.mainHand, &e.offHand, &e.boots, &e.legs, &e.chest, &e.helmet};
    if (idx >= 6) return pushNil(L);
    pushItem(L, *s[idx]);
    return 1;
}
void setNum(lua_State* L, char const* k, double v) { lua_pushnumber(L, v); lua_setfield(L, -2, k); }
void setStr(lua_State* L, char const* k, std::string const& v) { lua_pushstring(L, v.c_str()); lua_setfield(L, -2, k); }
void pushNumList(lua_State* L, std::initializer_list<double> v) {
    lua_createtable(L, int(v.size()), 0);
    int i = 1;
    for (double d : v) { lua_pushnumber(L, d); lua_rawseti(L, -2, i++); }
}
/// UUIDUtil.uuidToIntArray
bool uuidInts(std::string const& s, int32_t out[4]) {
    std::string h;
    for (char c : s) if (c != '-') h += c;
    if (h.size() != 32 || s.size() != 36) return false;
    for (char c : h) if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    for (int i = 0; i < 4; i++) out[i] = int32_t(uint32_t(std::stoul(h.substr(i * 8, 8), nullptr, 16)));
    return true;
}
void pushItemNbt(lua_State* L, ItemState const& it) {
    ItemState r = resolveItem(it);
    lua_newtable(L);
    setStr(L, "id", r.id);
    setNum(L, "Count", r.count);
    auto tag = itemTag(r);
    if (!tag.empty()) {
        lua_newtable(L);
        for (auto const& [k, v] : tag) { pushNbtValue(L, v); lua_setfield(L, -2, k.c_str()); }
        lua_setfield(L, -2, "tag");
    }
}
int enGetNbt(lua_State* L) {
    EntUD* u = ent(L, "getNbt");
    lua_newtable(L);
    Vec3        pos = entPos(L, u, 1), vel = entVel(L, u);
    std::string uuid = u->self ? P(L).uuid : u->e.uuid;
    pushNumList(L, {pos.x, pos.y, pos.z}); lua_setfield(L, -2, "Pos");
    pushNumList(L, {vel.x, vel.y, vel.z}); lua_setfield(L, -2, "Motion");
    if (u->self) { auto const& p = P(L); pushNumList(L, {p.yaw, p.pitch}); }
    else pushNumList(L, {u->e.rot.y, u->e.rot.x});
    lua_setfield(L, -2, "Rotation");
    int32_t ui[4];
    if (uuidInts(uuid, ui)) { pushNumList(L, {double(ui[0]), double(ui[1]), double(ui[2]), double(ui[3])}); lua_setfield(L, -2, "UUID"); }
    setNum(L, "FallDistance", 0);
    setNum(L, "PortalCooldown", 0);
    setNum(L, "Invulnerable", 0);
    if (u->self) {
        auto const& p = P(L);
        setNum(L, "OnGround", p.onGround ? 1 : 0);
        setNum(L, "Air", p.airSupply);
        setNum(L, "Fire", p.onFire ? 1 : -20);
        setNum(L, "Health", p.health);
        setNum(L, "AbsorptionAmount", p.absorption);
        setNum(L, "HurtTime", p.hurtTime);
        setNum(L, "DeathTime", p.deathTime);
        setNum(L, "HurtByTimestamp", 0);
        setNum(L, "FallFlying", p.gliding ? 1 : 0);
        setNum(L, "foodLevel", p.food);
        setNum(L, "foodSaturationLevel", p.saturation);
        setNum(L, "foodExhaustionLevel", p.exhaustion);
        setNum(L, "foodTickTimer", 0);
        setNum(L, "XpLevel", p.experienceLevel);
        setNum(L, "XpP", p.experienceProgress);
        setNum(L, "XpTotal", 0);
        setNum(L, "XpSeed", 0);
        setNum(L, "Score", 0);
        setNum(L, "SelectedItemSlot", p.selectedSlot);
        setNum(L, "SleepTimer", 0);
        setNum(L, "TicksFrozen", p.frozenTicks);
        int gm = p.gamemode == "CREATIVE" ? 1 : p.gamemode == "ADVENTURE" ? 2 : p.gamemode == "SPECTATOR" ? 3 : 0;
        setNum(L, "playerGameType", gm);
        setStr(L, "Dimension", p.dimension);
        lua_newtable(L); // abilities
        setNum(L, "flying", p.flying ? 1 : 0);
        setNum(L, "mayfly", gm == 1 || gm == 3 ? 1 : 0);
        setNum(L, "instabuild", gm == 1 ? 1 : 0);
        setNum(L, "invulnerable", gm == 1 || gm == 3 ? 1 : 0);
        setNum(L, "mayBuild", gm == 2 || gm == 3 ? 0 : 1);
        setNum(L, "flySpeed", 0.05);
        setNum(L, "walkSpeed", 0.1);
        lua_setfield(L, -2, "abilities");
        lua_newtable(L); // Inventory
        int k = 1;
        auto add = [&](ItemState const& it, int slot) {
            if (resolveItem(it).empty()) return;
            pushItemNbt(L, it);
            setNum(L, "Slot", slot);
            lua_rawseti(L, -2, k++);
        };
        for (size_t i = 0; i < p.inventory.size() && i < 36; i++) add(p.inventory[i], int(i));
        if (p.inventory.empty()) add(p.mainHand, p.selectedSlot);
        add(p.boots, 100); add(p.legs, 101); add(p.chest, 102); add(p.helmet, 103); add(p.offHand, -106);
        lua_setfield(L, -2, "Inventory");
        lua_newtable(L);
        lua_setfield(L, -2, "EnderItems");
        lua_newtable(L);
        int ei = 1;
        for (auto const& ef : p.effects) {
            lua_newtable(L);
            setStr(L, "id", ef.name);
            setNum(L, "Amplifier", ef.amplifier);
            setNum(L, "Duration", ef.duration);
            setNum(L, "ShowParticles", ef.visible ? 1 : 0);
            lua_rawseti(L, -2, ei++);
        }
        lua_setfield(L, -2, "ActiveEffects");
    } else {
        auto const& e = u->e;
        setNum(L, "OnGround", e.onGround ? 1 : 0);
        setNum(L, "Air", 300);
        setNum(L, "Fire", e.onFire ? 1 : -1);
        if (!e.name.empty()) setStr(L, "CustomName", jsonQuote(e.name));
        if (u->kind >= 1) setNum(L, "Health", e.health);
    }
    return 1;
}
ENT_FN(enIsOnFire, pushB(L, p.onFire), pushB(L, e.onFire))
ENT_FN(enIsAlive, pushB(L, p.alive), pushB(L, e.alive))
ENT_FN(enGetPermissionLevel, pushI(L, p.permissionLevel), pushI(L, 0))
int enGetPassengers(lua_State* L) {
    EntUD* u = ent(L, "getPassengers");
    lua_newtable(L);
    if (!u->self) return 1;
    int i = 1;
    for (auto const& e : P(L).passengers) { pushEntity(L, e); lua_rawseti(L, -2, i++); }
    return 1;
}
int enGetControllingPassenger(lua_State* L) {
    EntUD* u = ent(L, "getControllingPassenger");
    if (u->self) return pushNil(L); // players never have a controlling passenger
    return pushNil(L);
}
int enGetControlledVehicle(lua_State* L) {
    EntUD* u = ent(L, "getControlledVehicle");
    if (!u->self || !P(L).vehicle.valid) return pushNil(L);
    std::string t = stripNs(P(L).vehicle.type);
    // boats, horses, pigs/striders (with a stick) are controlled by their first passenger: the player
    if (has(t, "boat") || has(t, "raft") || t == "horse" || t == "donkey" || t == "mule" || t == "camel" || t == "skeleton_horse" ||
        t == "zombie_horse" || t == "pig" || t == "strider" || t == "llama")
        pushEntity(L, P(L).vehicle);
    else lua_pushnil(L);
    return 1;
}
int enHasContainer(lua_State* L) {
    EntUD* u = ent(L, "hasContainer");
    if (u->self) return pushB(L, false);
    std::string t = stripNs(u->e.type);
    return pushB(L, t == "chest_boat" || t == "chest_minecart" || t == "hopper_minecart");
}
int enHasInventory(lua_State* L) {
    EntUD* u = ent(L, "hasInventory");
    if (u->self) return pushB(L, false);
    std::string t = stripNs(u->e.type);
    return pushB(L, t == "chest_boat" || t == "chest_minecart" || t == "hopper_minecart" || t == "horse" || t == "donkey" ||
                        t == "mule" || t == "llama" || t == "trader_llama" || t == "camel" || t == "skeleton_horse" || t == "zombie_horse");
}
void pushBlockHit(lua_State* L, BlockHit const& h) {
    pushWorldBlock(L, h.x, h.y, h.z);
    pushVec3(L, h.pos);
    lua_pushstring(L, h.face.c_str());
}
int enGetTargetedBlock(lua_State* L) {
    EntUD* u      = ent(L, "getTargetedBlock");
    bool   ignoreLiquids = lua_toboolean(L, 2) != 0;
    double dist   = std::clamp(optNum(L, 3, 20), -20.0, 20.0);
    Vec3   eye    = entPos(L, u, 1);
    eye.y += entEyeHeight(L, u);
    Vec3   end    = eye + entLook(L, u) * dist;
    pushBlockHit(L, W(L).raycastBlock(eye, end, "OUTLINE", ignoreLiquids ? "NONE" : "ANY"));
    return 3;
}
int enGetTargetedEntity(lua_State* L) {
    EntUD* u    = ent(L, "getTargetedEntity");
    double dist = std::clamp(optNum(L, 2, 20), 0.0, 20.0);
    Vec3   eye  = entPos(L, u, 1);
    eye.y += entEyeHeight(L, u);
    Vec3      look = entLook(L, u);
    BlockHit  bh   = W(L).raycastBlock(eye, eye + look * dist, "OUTLINE", "NONE");
    if (bh.hit) dist = (bh.pos - eye).length();
    EntityHit eh = W(L).raycastEntity(eye, eye + look * dist);
    if (!eh.hit || !eh.entity.valid) return pushNil(L);
    pushEntity(L, eh.entity);
    pushVec3(L, eh.pos);
    return 2;
}
int enGetNearestEntity(lua_State* L) {
    EntUD*      u = ent(L, "getNearestEntity");
    std::string type;
    if (!isNil(L, 2)) {
        type = luaL_checkstring(L, 2);
        std::string t = lower(type);
        bool ok = !t.empty() && t.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-.:/") == std::string::npos && std::count(t.begin(), t.end(), ':') <= 1;
        if (!ok) luaL_error(L, "Invalid entity type: %s", type.c_str());
        type = resLoc(t);
    }
    double r   = optNum(L, 3, 20);
    Vec3   pos = entPos(L, u, 1);
    std::string selfUuid = u->self ? P(L).uuid : u->e.uuid;
    auto   all = W(L).entities(pos - Vec3{r, r, r}, pos + Vec3{r, r, r});
    if (!u->self && P(L).loaded) { // the avatar's player is an entity of the world too
        EntityRef me;
        me.valid = true; me.isPlayer = true; me.living = true; me.type = "minecraft:player"; me.uuid = P(L).uuid; me.pos = P(L).pos;
        Vec3 d = me.pos - pos;
        if (std::abs(d.x) <= r && std::abs(d.y) <= r && std::abs(d.z) <= r) all.push_back(me);
    }
    EntityRef const* best = nullptr;
    double           bd   = 1e300;
    for (auto const& e : all) {
        if (!e.valid || e.uuid == selfUuid) continue;
        if (!type.empty() && resLoc(e.type) != type) continue;
        double d = (e.pos - pos).length();
        if (d < bd) { bd = d; best = &e; }
    }
    if (!best) return pushNil(L);
    pushEntity(L, *best);
    return 1;
}
int enGetVariable(lua_State* L) {
    EntUD* u = ent(L, "getVariable");
    if (u->self) lua_getfield(L, LUA_REGISTRYINDEX, "fb.store");
    else lua_newtable(L);
    // ReadOnlyLuaTable copy
    lua_newtable(L);
    lua_pushnil(L);
    while (lua_next(L, -3)) { lua_pushvalue(L, -2); lua_insert(L, -2); lua_settable(L, -4); }
    lua_remove(L, -2);
    if (isNil(L, 2)) return 1;
    lua_pushvalue(L, 2);
    lua_gettable(L, -2);
    return 1;
}
int enIsLiving(lua_State* L) { return pushB(L, ent(L, "isLiving")->kind >= 1); }
int enIsPlayer(lua_State* L) { return pushB(L, ent(L, "isPlayer")->kind >= 2); }
int enEq(lua_State* L) {
    EntUD* a = toEnt(L, 1);
    EntUD* b = toEnt(L, 2);
    if (!a || !b) return pushB(L, false);
    std::string ua = a->self ? P(L).uuid : a->e.uuid, ub = b->self ? P(L).uuid : b->e.uuid;
    return pushB(L, ua == ub);
}
int enToString(lua_State* L) {
    EntUD* u = ent(L, "__tostring");
    if (u->kind >= 2) return pushS(L, entName(L, u) + " (Player)");
    std::string t = entType(u);
    std::string s = !u->self && !u->e.name.empty() ? u->e.name + " (" + t + ")" : t;
    return pushS(L, s + (u->kind == 1 ? " (LivingEntity)" : " (Entity)"));
}

// LivingEntityAPI
int leGetBodyYaw(lua_State* L) {
    EntUD* u = ent(L, "getBodyYaw");
    float  d = float(optNum(L, 2, 1));
    if (u->self) return pushN(L, lerpf(P(L).prevBodyYaw, P(L).bodyYaw, d));
    return pushN(L, u->e.bodyYaw);
}
int leGetHeldItem(lua_State* L) {
    EntUD* u   = ent(L, "getHeldItem");
    bool   off = lua_toboolean(L, 2) != 0;
    if (u->self) pushItem(L, off ? P(L).offHand : P(L).mainHand);
    else pushItem(L, off ? u->e.offHand : u->e.mainHand);
    return 1;
}
int leGetActiveItem(lua_State* L) { EntUD* u = ent(L, "getActiveItem"); pushItem(L, u->self && P(L).usingItem ? P(L).activeItem : ItemState{}); return 1; }
int leGetActiveItemTime(lua_State* L) {
    EntUD* u = ent(L, "getActiveItemTime");
    if (!u->self || !P(L).usingItem) return pushI(L, 0);
    auto const& p = P(L);
    return pushI(L, std::max(0, resolveItem(p.activeItem).maxUseDuration - p.useItemRemaining));
}
ENT_FN(leGetHealth, pushN(L, p.health), pushN(L, e.health))
ENT_FN(leGetMaxHealth, pushN(L, p.maxHealth), pushN(L, e.maxHealth))
ENT_FN(leGetArmor, pushN(L, p.armor), pushN(L, e.armor))
ENT_FN(leGetDeathTime, pushN(L, p.deathTime), pushN(L, 0))
ENT_FN(leGetArrowCount, pushI(L, p.arrowCount), pushI(L, 0))
ENT_FN(leGetStingerCount, pushI(L, p.stingerCount), pushI(L, 0))
ENT_FN(leIsLeftHanded, pushB(L, p.leftHanded), pushB(L, false))
ENT_FN(leIsUsingItem, pushB(L, p.usingItem), pushB(L, false))
ENT_FN(leGetActiveHand, pushS(L, p.activeHandOff ? "OFF_HAND" : "MAIN_HAND"), pushS(L, "MAIN_HAND"))
ENT_FN(leIsClimbing, pushB(L, p.climbing), pushB(L, false))
ENT_FN(leGetSwingTime, pushI(L, p.swingTime), pushI(L, 0))
ENT_FN(leIsSwingingArm, pushB(L, p.swinging), pushB(L, false))
ENT_FN(leGetSwingArm, p.swinging ? pushS(L, p.swingOffhand ? "OFF_HAND" : "MAIN_HAND") : pushNil(L), pushNil(L))
ENT_FN(leGetSwingDuration, pushI(L, p.swingDuration), pushI(L, 6))
ENT_FN(leGetAbsorptionAmount, pushN(L, p.absorption), pushN(L, 0))
int leIsSensitiveToWater(lua_State* L) {
    EntUD* u = ent(L, "isSensitiveToWater");
    if (u->self) return pushB(L, false);
    std::string t = stripNs(u->e.type);
    return pushB(L, t == "enderman" || t == "blaze" || t == "snow_golem" || t == "strider");
}
int leGetEntityCategory(lua_State* L) {
    EntUD* u = ent(L, "getEntityCategory");
    if (u->self) return pushS(L, "UNDEFINED");
    std::string t = stripNs(u->e.type);
    static std::set<std::string> undead{"zombie", "husk", "drowned", "zombie_villager", "zombified_piglin", "skeleton", "stray",
                                        "wither_skeleton", "skeleton_horse", "zombie_horse", "phantom", "wither", "zoglin"};
    static std::set<std::string> arth{"spider", "cave_spider", "silverfish", "endermite", "bee"};
    static std::set<std::string> water{"guardian", "elder_guardian", "cod", "salmon", "pufferfish", "tropical_fish", "squid",
                                       "glow_squid", "dolphin", "axolotl", "tadpole", "turtle"};
    static std::set<std::string> ill{"pillager", "vindicator", "evoker", "illusioner", "ravager", "witch"};
    if (undead.count(t)) return pushS(L, "UNDEAD");
    if (arth.count(t)) return pushS(L, "ARTHROPOD");
    if (water.count(t)) return pushS(L, "WATER");
    if (ill.count(t)) return pushS(L, "ILLAGER");
    return pushS(L, "UNDEFINED");
}
ENT_FN(leIsGliding, pushB(L, p.gliding), pushB(L, e.pose == "FALL_FLYING"))
ENT_FN(leIsBlocking, pushB(L, p.blocking), pushB(L, false))
ENT_FN(leIsVisuallySwimming, pushB(L, p.swimming || p.pose == "SWIMMING"), pushB(L, e.pose == "SWIMMING"))
ENT_FN(leRiptideSpinning, pushB(L, p.riptide || p.pose == "SPIN_ATTACK"), pushB(L, e.pose == "SPIN_ATTACK"))

// PlayerAPI
ENT_FN(plGetFood, pushI(L, int(p.food)), pushI(L, 20))
ENT_FN(plGetSaturation, pushN(L, p.saturation), pushN(L, 5))
ENT_FN(plGetExhaustion, pushN(L, p.exhaustion), pushN(L, 0))
ENT_FN(plGetExperienceProgress, pushN(L, p.experienceProgress), pushN(L, 0))
ENT_FN(plGetExperienceLevel, pushI(L, p.experienceLevel), pushI(L, 0))
ENT_FN(plGetModelType, pushS(L, upper(p.modelType)), pushS(L, "DEFAULT"))
ENT_FN(plGetGamemode, pushS(L, upper(p.gamemode)), pushNil(L))
ENT_FN(plHasCape, pushB(L, p.hasCape), pushB(L, false))
ENT_FN(plHasSkin, pushB(L, p.hasSkin), pushB(L, false))
int plIsSkinLayerVisible(lua_State* L) {
    (void)ent(L, "isSkinLayerVisible");
    std::string part = upper(checkStr(L, 2, "isSkinLayerVisible"));
    if (part == "LEFT_PANTS" || part == "RIGHT_PANTS") part += "_LEG";
    static std::set<std::string> ok{"CAPE", "JACKET", "LEFT_SLEEVE", "RIGHT_SLEEVE", "LEFT_PANTS_LEG", "RIGHT_PANTS_LEG", "HAT"};
    if (!ok.count(part)) luaL_error(L, "Invalid player model part: %s", lua_tostring(L, 2));
    if (part == "CAPE") return pushB(L, P(L).hasCape);
    return pushB(L, true);
}
ENT_FN(plIsFishing, pushB(L, p.fishing), pushB(L, false))
float attackDelay(ItemState const& it) {
    std::string n = stripNs(it.id);
    double speed = 4.0;
    if (endsWith(n, "_sword")) speed = 1.6;
    else if (n == "trident") speed = 1.1;
    else if (endsWith(n, "_shovel")) speed = 1.0;
    else if (endsWith(n, "_pickaxe")) speed = 1.2;
    else if (endsWith(n, "_axe")) speed = n == "wooden_axe" || n == "stone_axe" ? 0.8 : n == "iron_axe" ? 0.9 : 1.0;
    else if (endsWith(n, "_hoe")) speed = n == "stone_hoe" ? 2.0 : n == "iron_hoe" ? 3.0 : (n == "diamond_hoe" || n == "netherite_hoe") ? 4.0 : 1.0;
    return float(1.0 / speed * 20.0);
}
ENT_FN(plGetChargedAttackDelay, pushN(L, attackDelay(p.mainHand)), pushN(L, attackDelay(e.mainHand)))
int plGetShoulderEntity(lua_State* L) { (void)ent(L, "getShoulderEntity"); lua_newtable(L); return 1; }
int plGetTeamInfo(lua_State* L) { (void)ent(L, "getTeamInfo"); return pushNil(L); }
int plGetCooldownPercent(lua_State* L) {
    (void)ent(L, "getCooldownPercent");
    notNil(L, 2, "getCooldownPercent");
    (void)item(L, 2, "getCooldownPercent");
    return pushN(L, 0);
}

// ViewerAPI (the local player, with host-only data)
int viGetAttackCharge(lua_State* L) { (void)ent(L, "getAttackCharge"); return pushN(L, P(L).attackCharge); }
int viIsJumping(lua_State* L) { (void)ent(L, "isJumping"); return pushB(L, P(L).jumping); }
int viIsFlying(lua_State* L) { (void)ent(L, "isFlying"); return pushB(L, P(L).flying); }
int viGetReachDistance(lua_State* L) { (void)ent(L, "getReachDistance"); auto const& p = P(L); return pushN(L, p.gamemode == "CREATIVE" ? std::max(5.0f, p.reachDistance) : p.reachDistance); }
int viGetAir(lua_State* L) { (void)ent(L, "getAir"); return pushI(L, P(L).airSupply); }
void pushEffects(lua_State* L) {
    lua_newtable(L);
    int i = 1;
    for (auto const& e : P(L).effects) {
        lua_newtable(L);
        setStr(L, "name", e.name);
        setNum(L, "amplifier", e.amplifier);
        setNum(L, "duration", e.duration);
        lua_pushboolean(L, e.visible);
        lua_setfield(L, -2, "visible");
        lua_rawseti(L, -2, i++);
    }
}
int viGetStatusEffects(lua_State* L) { (void)ent(L, "getStatusEffects"); pushEffects(L); return 1; }

int nullIndex(lua_State* L) {
    return luaL_error(L, "Tried to access the Entity API before its initialization in the ENTITY_INIT event. To check if the entity exists, use \"<entity>:isLoaded()\"");
}
int nullToString(lua_State* L) { return pushS(L, "Uninitialized Entity API"); }

// ---------------------------------------------------------------- world (static functions)
Vec3 wPos(lua_State* L, int i, char const* fn) { return parseVec3(L, i, fn); }
#define WPOS(fn) Vec3 v = wPos(L, 1, fn); int x = blockFloor(v.x), y = blockFloor(v.y), z = blockFloor(v.z)
int wGetBiome(lua_State* L) {
    WPOS("getBiome");
    BiomeUD b{resLoc(W(L).biome(x, y, z)), x, y, z};
    newUD(L, tBiome, std::move(b));
    return 1;
}
int wGetBlockState(lua_State* L) { WPOS("getBlockState"); pushWorldBlock(L, x, y, z); return 1; }
int wIsChunkLoaded(lua_State* L) { WPOS("isChunkLoaded"); return pushB(L, W(L).chunkLoaded(x, y, z)); }
bool blockPosLess(int ax, int ay, int az, int bx, int by, int bz) { // Vec3i.compareTo: y, then z, then x
    if (ay != by) return ay < by;
    if (az != bz) return az < bz;
    return ax < bx;
}
int wGetBlocks(lua_State* L) {
    int  i = 1;
    Vec3 a = seqVec3(L, i, "getBlocks"), b = seqVec3(L, i, "getBlocks");
    int  x0 = blockFloor(a.x), y0 = blockFloor(a.y), z0 = blockFloor(a.z);
    int  x1 = std::min(x0 + 8, blockFloor(b.x)), y1 = std::min(y0 + 8, blockFloor(b.y)), z1 = std::min(z0 + 8, blockFloor(b.z));
    if (blockPosLess(x1, y1, z1, x0, y0, z0)) luaL_error(L, "Your max value can't be smaller than your min!");
    lua_newtable(L);
    if (!W(L).chunkLoaded(x0, y0, z0) || !W(L).chunkLoaded(x1, y1, z1)) return 1;
    int k = 1;
    for (int z = z0; z <= z1; z++)       // BlockPos.betweenClosedStream: x fastest, then y, then z
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) { pushWorldBlock(L, x, y, z); lua_rawseti(L, -2, k++); }
    return 1;
}
int wGetMapData(lua_State* L) { checkStr(L, 1, "getMapData"); return pushNil(L); }
int wGetRedstonePower(lua_State* L) { WPOS("getRedstonePower"); return pushI(L, W(L).redstonePower(x, y, z, false)); }
int wGetStrongRedstonePower(lua_State* L) { WPOS("getStrongRedstonePower"); return pushI(L, W(L).redstonePower(x, y, z, true)); }
int wGetTime(lua_State* L) { return pushN(L, double(W(L).time()) + optNum(L, 1, 0)); }
int wGetTimeOfDay(lua_State* L) { return pushN(L, double(W(L).dayTime()) + optNum(L, 1, 0)); }
int wGetDayTime(lua_State* L) { return pushN(L, std::fmod(double(W(L).dayTime()) + optNum(L, 1, 0), 24000.0)); }
int wGetDay(lua_State* L) { return pushN(L, std::floor((double(W(L).dayTime()) + optNum(L, 1, 0)) / 24000.0)); }
int wGetMoonPhase(lua_State* L) { return pushI(L, W(L).moonPhase()); }
int wGetRainGradient(lua_State* L) { (void)optNum(L, 1, 1); return pushN(L, W(L).rainGradient()); }
int wIsThundering(lua_State* L) { return pushB(L, W(L).thundering()); }
int wGetLightLevel(lua_State* L) { WPOS("getLightLevel"); if (!W(L).chunkLoaded(x, y, z)) return pushNil(L); return pushI(L, W(L).lightLevel(x, y, z)); }
int wGetSkyLightLevel(lua_State* L) { WPOS("getSkyLightLevel"); if (!W(L).chunkLoaded(x, y, z)) return pushNil(L); return pushI(L, W(L).skyLight(x, y, z)); }
int wGetBlockLightLevel(lua_State* L) { WPOS("getBlockLightLevel"); if (!W(L).chunkLoaded(x, y, z)) return pushNil(L); return pushI(L, W(L).blockLight(x, y, z)); }
int wGetHeight(lua_State* L) {
    Vec2        p = parseVec2(L, 1, "getHeight");
    int         hi = toVec(L, 1, nullptr) ? 2 : 3;
    std::string hm = isNil(L, hi) ? "MOTION_BLOCKING" : upper(luaL_checkstring(L, hi));
    static std::set<std::string> ok{"WORLD_SURFACE_WG", "WORLD_SURFACE", "OCEAN_FLOOR_WG", "OCEAN_FLOOR", "MOTION_BLOCKING", "MOTION_BLOCKING_NO_LEAVES"};
    int x = int(p.x), z = int(p.y);
    if (!W(L).chunkLoaded(x, 0, z)) return pushNil(L);
    if (!ok.count(hm)) luaL_error(L, "Invalid heightmap type provided");
    return pushI(L, W(L).height(x, z, hm));
}
int wIsOpenSky(lua_State* L) { WPOS("isOpenSky"); if (!W(L).chunkLoaded(x, y, z)) return pushNil(L); return pushB(L, W(L).canSeeSky(x, y, z)); }
int wGetDimension(lua_State* L) { return pushS(L, W(L).dimension()); }
int wGetPlayers(lua_State* L) {
    lua_newtable(L);
    for (auto const& e : W(L).players()) {
        if (!e.valid) continue;
        EntityRef r = e;
        r.isPlayer  = true;
        pushEntity(L, r);
        std::string n = r.name.empty() ? r.uuid : r.name;
        lua_setfield(L, -2, n.c_str());
    }
    if (P(L).loaded) {
        pushSelfPlayer(L);
        lua_setfield(L, -2, P(L).name.c_str());
    }
    return 1;
}
int wGetEntities(lua_State* L) {
    int  i = 1;
    Vec3 a = seqVec3(L, i, "getEntities"), b = seqVec3(L, i, "getEntities");
    Vec3 mn{std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}, mx{std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
    lua_newtable(L);
    int k = 1;
    auto const& p = P(L);
    if (p.loaded) { // the avatar's player AABB
        double w = 0.3, h = 1.8;
        if (p.pos.x + w >= mn.x && p.pos.x - w <= mx.x && p.pos.y + h >= mn.y && p.pos.y <= mx.y && p.pos.z + w >= mn.z && p.pos.z - w <= mx.z) {
            pushSelfPlayer(L);
            lua_rawseti(L, -2, k++);
        }
    }
    for (auto const& e : W(L).entities(mn, mx)) {
        if (!e.valid || (!p.uuid.empty() && e.uuid == p.uuid)) continue;
        pushEntity(L, e);
        lua_rawseti(L, -2, k++);
    }
    return 1;
}
bool validUuid(std::string const& s) { int32_t t[4]; return uuidInts(s, t); }
int wGetEntity(lua_State* L) {
    std::string id = checkStr(L, 1, "getEntity");
    if (!validUuid(id)) luaL_error(L, "Invalid UUID");
    if (lower(id) == lower(P(L).uuid) && P(L).loaded) { pushSelfPlayer(L); return 1; }
    pushEntity(L, W(L).entityByUUID(id));
    return 1;
}
int wAvatarVars(lua_State* L) {
    lua_newtable(L);
    lua_getfield(L, LUA_REGISTRYINDEX, "fb.store");
    lua_setfield(L, -2, P(L).uuid.c_str());
    return 1;
}
int wNewBlock(lua_State* L) {
    std::string s = checkStr(L, 1, "newBlock");
    Vec3        v = parseVec3(L, 2, "newBlock");
    std::string id;
    std::map<std::string, std::string> props;
    if (!parseBlockString(s, id, props)) luaL_error(L, "Could not parse block state from string: %s", s.c_str());
    pushBlock(L, id, blockFloor(v.x), blockFloor(v.y), blockFloor(v.z), props, true);
    return 1;
}
int wNewItem(lua_State* L) {
    std::string s = checkStr(L, 1, "newItem");
    ItemState   it;
    if (!parseItemString(s, it)) luaL_error(L, "Could not parse item stack from string: %s", s.c_str());
    if (!isNil(L, 2)) it.count = int(luaL_checkinteger(L, 2));
    if (!isNil(L, 3)) it.damage = int(luaL_checkinteger(L, 3));
    pushItem(L, it);
    return 1;
}
int wExists(lua_State* L) { return pushB(L, P(L).loaded); }
int wGetBuildHeight(lua_State* L) { lua_pushinteger(L, W(L).minBuildHeight()); lua_pushinteger(L, W(L).maxBuildHeight()); return 2; }
int wGetSpawnPoint(lua_State* L) { Vec3 s = W(L).spawnPoint(); pushVec3(L, {std::floor(s.x), std::floor(s.y), std::floor(s.z)}); return 1; }
int wToString(lua_State* L) { return pushS(L, "WorldAPI"); }

// ---------------------------------------------------------------- client (static functions)
int cGetFPS(lua_State* L) { return pushI(L, int(av(L)->host->fps())); }
int cGetFPSString(lua_State* L) {
    char b[96];
    std::snprintf(b, sizeof b, "%d fps T: inf fancy fancy-clouds B: 2", int(av(L)->host->fps()));
    return pushS(L, b);
}
int cIsPaused(lua_State* L) { return pushB(L, av(L)->host->paused()); }
int cGetVersion(lua_State* L) { return pushS(L, av(L)->host->version()); }
int cIsSnapshot(lua_State* L) { return pushB(L, false); }
int cGetClientBrand(lua_State* L) { return pushS(L, "fabric"); }
int cGetServerBrand(lua_State* L) {
    if (!P(L).loaded) return pushNil(L);
    return pushS(L, av(L)->host->singleplayer() ? "Integrated" : av(L)->host->serverBrand());
}
int cGetChunkStatistics(lua_State* L) { return pushS(L, "C: 0/0 (s) D: 12, pC: 000, pU: 00, aB: 0"); }
int cGetEntityStatistics(lua_State* L) { char b[64]; std::snprintf(b, sizeof b, "E: %d/%d, B: 0, SD: 12", av(L)->host->entityCount(), av(L)->host->entityCount()); return pushS(L, b); }
int cGetSoundStatistics(lua_State* L) { return pushS(L, "Sounds: 0/247 + 0/8 (Mood 0%)"); }
int cGetEntityCount(lua_State* L) { return pushI(L, P(L).loaded ? std::max(1, av(L)->host->entityCount()) : 0); }
int cGetParticleCount(lua_State* L) { return pushS(L, "0"); }
int cGetCurrentEffect(lua_State* L) {
    auto const& e = av(L)->render.postEffect;
    if (e.empty()) return pushNil(L);
    return pushS(L, e);
}
int cGetJavaVersion(lua_State* L) { return pushS(L, "17.0.8"); }
int cGetUsedMemory(lua_State* L) { return pushI(L, 512LL * 1024 * 1024); }
int cGetMaxMemory(lua_State* L) { return pushI(L, 4096LL * 1024 * 1024); }
int cGetAllocatedMemory(lua_State* L) { return pushI(L, 1024LL * 1024 * 1024); }
int cIsWindowFocused(lua_State* L) { return pushB(L, av(L)->host->windowFocused()); }
int cIsHudEnabled(lua_State* L) { return pushB(L, av(L)->host->isHudEnabled()); }
int cIsDebugOverlayEnabled(lua_State* L) { return pushB(L, av(L)->host->isDebugOverlay()); }
int cGetWindowSize(lua_State* L) { auto* h = av(L)->host; pushVec2(L, h->windowWidth(), h->windowHeight()); return 1; }
int cGetFOV(lua_State* L) { return pushN(L, av(L)->host->fov()); }
int cGetSystemTime(lua_State* L) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return pushI(L, ms);
}
int cGetMousePos(lua_State* L) { Vec3 m = av(L)->host->mousePos(); pushVec2(L, m.x, m.y); return 1; }
int cGetScaledWindowSize(lua_State* L) {
    auto*  h = av(L)->host;
    double s = std::max(1.0, h->guiScale());
    pushVec2(L, std::ceil(h->windowWidth() / s), std::ceil(h->windowHeight() / s));
    return 1;
}
int cGetGuiScale(lua_State* L) { return pushN(L, av(L)->host->guiScale()); }
int cGetCameraPos(lua_State* L) { pushVec3(L, av(L)->host->cameraPos()); return 1; }
int cGetCameraRot(lua_State* L) { pushVec3(L, av(L)->host->cameraRot()); return 1; }
int cGetCameraDir(lua_State* L) { Vec3 r = av(L)->host->cameraRot(); pushVec3(L, lookDir(r.x, r.y)); return 1; }
int cGetTextWidth(lua_State* L) {
    std::string s = plainText(checkStr(L, 1, "getTextWidth"));
    int w = 0;
    for (auto const& l : splitLines(s)) w = std::max(w, textWidth(l));
    return pushI(L, w);
}
int cGetTextHeight(lua_State* L) {
    std::string s = isNil(L, 1) ? "" : plainText(luaL_checkstring(L, 1));
    int n = int(splitLines(s).size());
    return pushI(L, n * 9 + std::max(n - 1, 0));
}
int cGetTextDimensions(lua_State* L) {
    std::string s    = plainText(checkStr(L, 1, "getTextDimensions"));
    int         maxW = int(optNum(L, 2, 0));
    bool        wrap = isNil(L, 3) || lua_toboolean(L, 3);
    std::vector<std::string> lines;
    for (auto const& l : splitLines(s)) {
        if (!wrap || maxW <= 0 || textWidth(l) <= maxW) { lines.push_back(l); continue; }
        std::string cur;
        size_t      p = 0;
        while (p <= l.size()) { // word wrap (break long words)
            size_t q = l.find(' ', p);
            std::string word = l.substr(p, q == std::string::npos ? std::string::npos : q - p);
            std::string cand = cur.empty() ? word : cur + " " + word;
            if (textWidth(cand) <= maxW || cur.empty()) {
                if (textWidth(cand) > maxW && cur.empty()) {
                    std::string part;
                    for (char c : word) { if (textWidth(part + c) > maxW && !part.empty()) { lines.push_back(part); part.clear(); } part += c; }
                    cur = part;
                } else cur = cand;
            } else { lines.push_back(cur); cur = word; }
            if (q == std::string::npos) break;
            p = q + 1;
        }
        lines.push_back(cur);
    }
    int w = 0;
    for (auto const& l : lines) w = std::max(w, std::min(textWidth(l), maxW > 0 && !wrap ? maxW : 1 << 30));
    int n = int(lines.size());
    pushVec2(L, w, n * 9 + std::max(n - 1, 0));
    return 1;
}
int cGetActiveLang(lua_State* L) { return pushS(L, av(L)->host->language()); }
int cIsModLoaded(lua_State* L) {
    std::string id = isNil(L, 1) ? "" : luaL_checkstring(L, 1);
    return pushB(L, id == "figura" || id == "minecraft" || id == "java" || id == "fabricloader");
}
int cHasShaderPackMod(lua_State* L) { return pushB(L, false); }
int cHasShaderPack(lua_State* L) { return pushB(L, false); }
int cGetShaderPackName(lua_State* L) { return pushS(L, ""); }
int cHasResource(lua_State* L) {
    std::string p = checkStr(L, 1, "hasResource");
    std::string n = lower(p);
    if (n.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-./:") != std::string::npos) luaL_error(L, "Non [a-z0-9/._-] character in path of location: %s", p.c_str());
    return pushB(L, false);
}
int cGetActiveResourcePacks(lua_State* L) { return pushStrings(L, av(L)->host->resourcePacks()); }
int cGetFiguraVersion(lua_State* L) { return pushS(L, "0.1.6+1.20.1"); }
/// org.figuramc.figura.utils.Version: major.minor.patch[-pre][+build]
struct Ver {
    int  v[3] = {0, 0, 0};
    std::string pre;
    bool invalid = false;
};
Ver parseVer(std::string s) {
    Ver r;
    if (size_t b = s.find('+'); b != std::string::npos) s = s.substr(0, b);
    if (size_t d = s.find('-'); d != std::string::npos) { r.pre = s.substr(d + 1); s = s.substr(0, d); }
    int    i = 0;
    size_t p = 0;
    if (s.empty()) { r.invalid = true; return r; }
    while (p <= s.size() && i < 3) {
        size_t q = s.find('.', p);
        std::string part = s.substr(p, q == std::string::npos ? std::string::npos : q - p);
        if (part.empty() || part.find_first_not_of("0123456789") != std::string::npos) { r.invalid = true; return r; }
        r.v[i++] = std::atoi(part.c_str());
        if (q == std::string::npos) break;
        p = q + 1;
    }
    return r;
}
int cCompareVersions(lua_State* L) {
    std::string a = checkStr(L, 1, "compareVersions"), b = checkStr(L, 2, "compareVersions");
    Ver x = parseVer(a), y = parseVer(b);
    if (x.invalid) luaL_error(L, "Cannot parse version \"%s\"", a.c_str());
    if (y.invalid) luaL_error(L, "Cannot parse version \"%s\"", b.c_str());
    for (int i = 0; i < 3; i++) if (x.v[i] != y.v[i]) return pushI(L, x.v[i] < y.v[i] ? -1 : 1);
    if (x.pre == y.pre) return pushI(L, 0);
    if (x.pre.empty()) return pushI(L, 1);  // release > pre-release
    if (y.pre.empty()) return pushI(L, -1);
    return pushI(L, x.pre < y.pre ? -1 : 1);
}
int pushUuidInts(lua_State* L, int32_t const v[4]) { for (int i = 0; i < 4; i++) lua_pushinteger(L, v[i]); return 4; }
int cGenerateUUID(lua_State* L) {
    static std::mt19937_64 rng{std::random_device{}()};
    uint64_t hi = rng(), lo = rng();
    hi = (hi & ~0xF000ULL) | 0x4000ULL;            // version 4
    lo = (lo & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
    int32_t v[4] = {int32_t(uint32_t(hi >> 32)), int32_t(uint32_t(hi)), int32_t(uint32_t(lo >> 32)), int32_t(uint32_t(lo))};
    return pushUuidInts(L, v);
}
int cIntUUIDToString(lua_State* L) {
    uint32_t v[4];
    for (int i = 0; i < 4; i++) v[i] = uint32_t(int32_t(int64_t(optNum(L, i + 1, 0))));
    char b[40];
    std::snprintf(b, sizeof b, "%08x-%04x-%04x-%04x-%04x%08x", v[0], v[1] >> 16, v[1] & 0xFFFF, v[2] >> 16, v[2] & 0xFFFF, v[3]);
    return pushS(L, b);
}
int cUuidToIntArray(lua_State* L) {
    std::string s = isNil(L, 1) ? "" : luaL_checkstring(L, 1);
    int32_t     v[4];
    // java UUID.fromString accepts shorter groups; we accept the canonical form
    if (!uuidInts(s, v)) luaL_error(L, "Failed to parse uuid");
    return pushUuidInts(L, v);
}
int cGetViewer(lua_State* L) { lua_getfield(L, LUA_REGISTRYINDEX, "fb.viewer"); return 1; }
int cGetCameraEntity(lua_State* L) { if (!P(L).loaded) return pushNil(L); pushSelfPlayer(L); return 1; }
int cIsIntegratedServer(lua_State* L) { return pushB(L, av(L)->host->singleplayer()); }
int cGetServerData(lua_State* L) {
    auto* h = av(L)->host;
    lua_newtable(L);
    setStr(L, "name", h->serverName());
    if (!h->serverIp().empty() || h->singleplayer()) setStr(L, "ip", h->singleplayer() ? "local" : h->serverIp());
    setStr(L, "motd", h->singleplayer() ? h->serverName() : h->serverMotd());
    return 1;
}
int cGetDate(lua_State* L) {
    auto        now = std::chrono::system_clock::now();
    std::time_t t   = std::chrono::system_clock::to_time_t(now);
    int         ms  = int(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm     tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    lua_newtable(L);
    setNum(L, "day", tm.tm_mday);
    setNum(L, "month", tm.tm_mon + 1);
    setNum(L, "year", tm.tm_year + 1900);
    setNum(L, "hour", tm.tm_hour);
    setNum(L, "minute", tm.tm_min);
    setNum(L, "second", tm.tm_sec);
    setNum(L, "millisecond", ms);
    char wk[8];
    std::strftime(wk, sizeof wk, "%U", &tm); // week of year (Sunday first, like Calendar US)
    setNum(L, "week", std::atoi(wk) + 1);
    setNum(L, "year_day", tm.tm_yday + 1);
    setNum(L, "week_day", tm.tm_wday + 1);
    lua_pushboolean(L, tm.tm_isdst > 0);
    lua_setfield(L, -2, "daylight_saving");
    char b[64];
    std::strftime(b, sizeof b, "%z", &tm); setStr(L, "timezone", b);
    std::strftime(b, sizeof b, "%Z", &tm); setStr(L, "timezone_name", b);
    setStr(L, "era", tm.tm_year + 1900 > 0 ? "AD" : "BC");
    static char const* months[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
    static char const* days[]   = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    setStr(L, "month_name", months[tm.tm_mon]);
    setStr(L, "day_name", days[tm.tm_wday]);
    return 1;
}
int cGetFrameTime(lua_State* L) { return pushN(L, av(L)->tickDelta); }
int pushComponent(lua_State* L, std::string const& s) { if (s.empty()) return pushNil(L); return pushS(L, "{\"text\":" + jsonQuote(s) + "}"); }
int cGetActionbar(lua_State* L) { return pushComponent(L, av(L)->host->actionbarText()); }
int cGetTitle(lua_State* L) { return pushComponent(L, av(L)->host->titleText()); }
int cGetSubtitle(lua_State* L) { return pushComponent(L, av(L)->host->subtitleText()); }
int cGetScoreboard(lua_State* L) { lua_newtable(L); return 1; }
int cListAtlases(lua_State* L) {
    return pushStrings(L, {"minecraft:textures/atlas/blocks.png", "minecraft:textures/atlas/signs.png", "minecraft:textures/atlas/banner_patterns.png",
                           "minecraft:textures/atlas/shield_patterns.png", "minecraft:textures/atlas/chest.png", "minecraft:textures/atlas/beds.png",
                           "minecraft:textures/atlas/shulker_boxes.png", "minecraft:textures/atlas/decorated_pot.png",
                           "minecraft:textures/atlas/armor_trims.png", "minecraft:textures/atlas/particles.png",
                           "minecraft:textures/atlas/paintings.png", "minecraft:textures/atlas/mob_effects.png"});
}
int cGetAtlas(lua_State* L) {
    checkStr(L, 1, "getAtlas");
    // TextureAtlasAPI lives with the texture APIs: they may publish a constructor as registry "fb.getAtlas"
    lua_getfield(L, LUA_REGISTRYINDEX, "fb.getAtlas");
    if (!lua_isfunction(L, -1)) return pushNil(L);
    lua_pushvalue(L, 1);
    lua_call(L, 1, 1);
    return 1;
}
int cGetTabList(lua_State* L) {
    lua_newtable(L);
    std::vector<std::string> pl = av(L)->host->tabList();
    if (P(L).loaded && std::find(pl.begin(), pl.end(), P(L).name) == pl.end()) pl.insert(pl.begin(), P(L).name);
    pushStrings(L, pl);
    lua_setfield(L, -2, "players");
    return 1;
}
int cGetTranslatedString(lua_State* L) {
    std::string key = checkStr(L, 1, "getTranslatedString");
    // no language files on Bedrock: Component.translatable falls back to the key; %s args are only used when known
    return pushS(L, key);
}
std::string regName(std::string n) { n = lower(n); return n.rfind("minecraft:", 0) == 0 ? n.substr(10) : n; }
int cGetRegistry(lua_State* L) {
    std::string name = checkStr(L, 1, "getRegistry");
    std::string n    = regName(name);
    std::vector<std::string> out;
    auto addAll = [&](auto const& arr) { for (auto const& x : arr) out.push_back(std::string("minecraft:") + x.id); };
    auto addStr = [&](auto const& arr) { for (char const* x : arr) out.push_back(std::string("minecraft:") + x); };
    if (n == "block") addAll(mcdata::kBlocks);
    else if (n == "item") addAll(mcdata::kItems);
    else if (n == "entity_type") addAll(mcdata::kEntities);
    else if (n == "particle_type") addStr(mcdata::kParticles);
    else if (n == "sound_event") addStr(mcdata::kSounds);
    else {
        static std::set<std::string> known{"activity", "attribute", "banner_pattern", "block_entity_type", "block_predicate_type", "cat_variant",
            "chunk_status", "command_argument_type", "custom_stat", "decorated_pot_patterns", "enchantment", "float_provider_type",
            "fluid", "frog_variant", "game_event", "height_provider_type", "instrument", "int_provider_type", "loot_condition_type",
            "loot_function_type", "loot_nbt_provider_type", "loot_number_provider_type", "loot_pool_entry_type", "loot_score_provider_type",
            "memory_module_type", "menu", "mob_effect", "painting_variant", "point_of_interest_type", "pos_rule_test", "position_source_type",
            "potion", "recipe_serializer", "recipe_type", "rule_block_entity_modifier", "rule_test", "schedule", "sensor_type", "sniffer_state",
            "stat_type", "villager_profession", "villager_type", "worldgen/biome_source", "worldgen/block_state_provider_type",
            "worldgen/carver", "worldgen/chunk_generator", "worldgen/density_function_type", "worldgen/feature",
            "worldgen/feature_size_type", "worldgen/foliage_placer_type", "worldgen/material_condition", "worldgen/material_rule",
            "worldgen/placement_modifier_type", "worldgen/root_placer_type", "worldgen/structure_piece", "worldgen/structure_placement",
            "worldgen/structure_pool_element", "worldgen/structure_processor", "worldgen/structure_type", "worldgen/tree_decorator_type",
            "worldgen/trunk_placer_type"};
        if (!known.count(n)) luaL_error(L, "Registry %s does not exist", name.c_str());
    }
    return pushStrings(L, out);
}
int cGetEnum(lua_State* L) {
    std::string name = upper(checkStr(L, 1, "getEnum"));
    static std::map<std::string, std::vector<std::string>> enums{
        {"ENTITY_POSES", {"STANDING", "FALL_FLYING", "SLEEPING", "SWIMMING", "SPIN_ATTACK", "CROUCHING", "LONG_JUMPING", "DYING", "CROAKING",
                          "USING_TONGUE", "SITTING", "ROARING", "SNIFFING", "EMERGING", "DIGGING"}},
        {"USE_ACTIONS", {"NONE", "EAT", "DRINK", "BLOCK", "BOW", "SPEAR", "CROSSBOW", "SPYGLASS", "TOOT_HORN", "BRUSH"}},
        {"PLAYER_MODEL_PARTS", {"CAPE", "JACKET", "LEFT_SLEEVE", "RIGHT_SLEEVE", "LEFT_PANTS", "RIGHT_PANTS", "HAT"}},
        {"ITEM_RENDER_TYPES", {"NONE", "THIRD_PERSON_LEFT_HAND", "THIRD_PERSON_RIGHT_HAND", "FIRST_PERSON_LEFT_HAND",
                               "FIRST_PERSON_RIGHT_HAND", "HEAD", "GUI", "GROUND", "FIXED"}},
        {"PLAY_STATES", {"STOPPED", "PAUSED", "PLAYING"}},
        {"LOOP_MODES", {"LOOP", "ONCE", "HOLD"}},
        {"RENDER_MODES", {"FIGURA_GUI", "PAPERDOLL", "MINECRAFT_GUI", "FIRST_PERSON", "FIRST_PERSON_WORLD", "RENDER", "WORLD", "OTHER"}},
        {"BLOCK_RAYCAST_TYPE", {"COLLIDER", "OUTLINE", "VISUAL", "FALLDAMAGE_RESETTING"}},
        {"FLUID_RAYCAST_TYPE", {"NONE", "SOURCE_ONLY", "ANY", "WATER"}},
        {"HEIGHTMAP_TYPE", {"WORLD_SURFACE_WG", "WORLD_SURFACE", "OCEAN_FLOOR_WG", "OCEAN_FLOOR", "MOTION_BLOCKING", "MOTION_BLOCKING_NO_LEAVES"}},
        {"STRING_ENCODINGS", {"utf8", "utf_8", "utf16", "utf_16", "utf16be", "utf_16_be", "utf16le", "utf_16_le", "ascii", "iso88591", "iso_8859_1"}},
        {"POST_EFFECTS", {"notch", "fxaa", "art", "bumpy", "blobs2", "pencil", "color_convolve", "deconverge", "flip", "invert", "ntsc",
                          "outline", "phosphor", "scan_pincushion", "sobel", "bits", "desaturate", "green", "blur", "wobble", "blobs",
                          "antialias", "creeper", "spider"}},
        {"TEXTURE_TYPES", {"SKIN", "CAPE", "ELYTRA", "RESOURCE", "PRIMARY", "SECONDARY", "SPECULAR", "NORMAL", "CUSTOM"}},
        {"KEYBINDS", {}}, {"PARENT_TYPES", {}}, {"RENDER_TYPES", {}}, {"KEY_IDS", {}}, {"COLORS", {}}, {"REGISTRIES", {"block", "item", "entity_type", "particle_type", "sound_event"}}};
    auto it = enums.find(name);
    if (it == enums.end()) luaL_error(L, "Enum %s does not exist", lua_tostring(L, 1));
    return pushStrings(L, it->second);
}
int cToString(lua_State* L) { return pushS(L, "ClientAPI"); }

// ---------------------------------------------------------------- host
struct HostState {
    bool                    unlockCursor = false;
    std::optional<uint32_t> chatColor;
};
HostState& hs(lua_State* L) {
    static std::map<lua_State*, HostState> m;
    return m[L];
}
int hIsHost(lua_State* L) { return pushB(L, true); }
int hIsCursorUnlocked(lua_State* L) { return pushB(L, hs(L).unlockCursor); }
int hSetUnlockCursor(lua_State* L) { hs(L).unlockCursor = lua_toboolean(L, 2) != 0; return ret1(L); }
int hSetTitleTimes(lua_State* L) { Vec3 t = parseVec3(L, 2, "setTitleTimes"); av(L)->host->setTitleTimes(int(t.x), int(t.y), int(t.z)); return ret1(L); }
int hClearTitle(lua_State* L) { av(L)->host->clearTitle(); return ret1(L); }
int hSetTitle(lua_State* L) { av(L)->host->setTitle(plainText(checkStr(L, 2, "setTitle"))); return ret1(L); }
int hSetSubtitle(lua_State* L) { av(L)->host->setSubtitle(plainText(checkStr(L, 2, "setSubtitle"))); return ret1(L); }
int hSetActionbar(lua_State* L) { av(L)->host->setActionbar(plainText(checkStr(L, 2, "setActionbar"))); return ret1(L); }
int hSendChatMessage(lua_State* L) { av(L)->host->sendChat(checkStr(L, 2, "sendChatMessage")); return ret1(L); }
int hSendChatCommand(lua_State* L) {
    std::string c = checkStr(L, 2, "sendChatCommand");
    av(L)->host->sendCommand(c.rfind("/", 0) == 0 ? c.substr(1) : c);
    return ret1(L);
}
int hAppendChatHistory(lua_State* L) { av(L)->host->appendChatHistory(checkStr(L, 2, "appendChatHistory")); return ret1(L); }
int hGetChatMessage(lua_State* L) { (void)optNum(L, 2, 0); return pushNil(L); }
int hSetChatMessage(lua_State* L) { return ret1(L); }
int hSwingArm(lua_State* L) { av(L)->host->swingArm(lua_toboolean(L, 2) != 0); return ret1(L); }
/// LuaUtils.parseSlot + Entity.getSlot for the local player
int slotIndex(lua_State* L, int i, char const* fn) {
    notNil(L, i, fn);
    if (lua_type(L, i) == LUA_TNUMBER) return int(lua_tointeger(L, i));
    if (lua_type(L, i) != LUA_TSTRING) luaL_error(L, "Invalid type for getSlot: %s", figuraTypeName(L, i).c_str());
    std::string s = lua_tostring(L, i);
    auto num = [&](char const* prefix, int base, int count) -> int {
        size_t n = std::strlen(prefix);
        if (s.rfind(prefix, 0) != 0) return -1000;
        std::string r = s.substr(n);
        if (r.empty() || r.find_first_not_of("0123456789") != std::string::npos) return -2000;
        int k = std::atoi(r.c_str());
        return k >= 0 && k < count ? base + k : -2000;
    };
    static std::map<std::string, int> fixed{{"weapon", 98}, {"weapon.mainhand", 98}, {"weapon.offhand", 99}, {"armor.feet", 100},
                                            {"armor.legs", 101}, {"armor.chest", 102}, {"armor.head", 103}, {"horse.saddle", 400},
                                            {"horse.armor", 401}, {"horse.chest", 499}};
    if (auto it = fixed.find(s); it != fixed.end()) return it->second;
    struct Range { char const* pre; int base, cnt; };
    static Range const ranges[] = {{"container.", 0, 54}, {"hotbar.", 0, 9}, {"inventory.", 9, 27}, {"enderchest.", 200, 27},
                                   {"villager.", 300, 8}, {"horse.", 500, 15}};
    for (Range const& rg : ranges)
        if (int r = num(rg.pre, rg.base, rg.cnt); r != -1000) {
            if (r == -2000) break;
            return r;
        }
    luaL_error(L, "Unable to get slot \"%s\"", s.c_str());
    return 0;
}
ItemState slotItem(PlayerState const& p, int slot) {
    if (slot >= 0 && slot < 36) {
        if (slot < int(p.inventory.size())) return p.inventory[size_t(slot)];
        if (slot == p.selectedSlot) return p.mainHand;
        return {};
    }
    switch (slot) {
    case 98: return p.mainHand;
    case 99: return p.offHand;
    case 100: return p.boots;
    case 101: return p.legs;
    case 102: return p.chest;
    case 103: return p.helmet;
    default: return {};
    }
}
int hGetSlot(lua_State* L) {
    int slot = slotIndex(L, 2, "getSlot");
    auto const& p = P(L);
    if (!p.loaded || !p.alive) { pushItem(L, ItemState{}); return 1; }
    pushItem(L, slotItem(p, slot));
    return 1;
}
int hSetSlot(lua_State* L) {
    if (isNil(L, 2) && isNil(L, 3)) return ret1(L);
    if (P(L).gamemode != "CREATIVE") return ret1(L);
    (void)slotIndex(L, 2, "setSlot");
    (void)itemArg(L, 3, "setSlot");
    return ret1(L); // creative inventory editing is not available on Bedrock
}
int hSetBadge(lua_State* L) { return luaL_error(L, "Congrats, you found this debug easter egg!"); }
int hGetChatColor(lua_State* L) { auto& c = hs(L).chatColor; if (!c) return pushNil(L); return pushI(L, *c); }
int hSetChatColor(lua_State* L) {
    if (isNil(L, 2)) hs(L).chatColor.reset();
    else hs(L).chatColor = rgbToInt(parseVec3(L, 2, "setChatColor"));
    return ret1(L);
}
int hGetChatText(lua_State* L) { auto* h = av(L)->host; if (!h->chatOpen()) return pushNil(L); return pushS(L, h->chatText()); }
int hSetChatText(lua_State* L) { std::string t = checkStr(L, 2, "setChatText"); if (av(L)->host->chatOpen()) av(L)->host->setChatText(t); return ret1(L); }
int hGetScreen(lua_State* L) { std::string s = av(L)->host->screen(); if (s.empty()) return pushNil(L); return pushS(L, s); }
int hGetScreenSlotCount(lua_State* L) { if (!av(L)->host->containerOpen()) return pushNil(L); return pushI(L, 46); }
int hGetScreenSlot(lua_State* L) {
    int slot = slotIndex(L, 2, "getScreenSlot");
    if (!av(L)->host->containerOpen() || slot < 0 || slot >= 46) return pushNil(L);
    pushItem(L, ItemState{});
    return 1;
}
int hIsChatOpen(lua_State* L) { return pushB(L, av(L)->host->chatOpen()); }
int hIsContainerOpen(lua_State* L) { return pushB(L, av(L)->host->containerOpen()); }
int hScreenshot(lua_State* L) { checkStr(L, 2, "screenshot"); return pushNil(L); }
int hIsAvatarUploaded(lua_State* L) { return pushB(L, av(L)->host->avatarUploaded()); }
int hGetStatusEffects(lua_State* L) { pushEffects(L); return 1; }
int hGetClipboard(lua_State* L) { return pushS(L, av(L)->host->clipboard()); }
int hSetClipboard(lua_State* L) { av(L)->host->setClipboard(checkStr(L, 2, "setClipboard")); return ret1(L); }
int hGetAttackCharge(lua_State* L) { return pushN(L, P(L).loaded ? P(L).attackCharge : 0); }
int hIsJumping(lua_State* L) { return pushB(L, P(L).jumping); }
int hIsFlying(lua_State* L) { return pushB(L, P(L).flying); }
int hGetReachDistance(lua_State* L) { auto const& p = P(L); return pushN(L, p.gamemode == "CREATIVE" ? std::max(5.0f, p.reachDistance) : p.reachDistance); }
int hGetAir(lua_State* L) { return pushI(L, P(L).loaded ? P(L).airSupply : 0); }
int hGetPickBlock(lua_State* L) {
    auto const& p = P(L);
    if (!p.loaded) return pushNil(L);
    Vec3 eye = p.pos;
    eye.y += p.eyeHeight;
    double reach = p.gamemode == "CREATIVE" ? std::max(5.0f, p.reachDistance) : p.reachDistance;
    Vec3 end = eye + lookDir(p.pitch, p.yaw) * reach;
    EntityHit eh = W(L).raycastEntity(eye, end);
    BlockHit  bh = W(L).raycastBlock(eye, end, "OUTLINE", "NONE");
    if (eh.hit && (!bh.hit || (eh.pos - eye).length() < (bh.pos - eye).length())) return pushNil(L); // an entity is targeted
    pushBlockHit(L, bh);
    return 3;
}
int hGetPickEntity(lua_State* L) {
    auto const& p = P(L);
    if (!p.loaded) return pushNil(L);
    Vec3 eye = p.pos;
    eye.y += p.eyeHeight;
    double    reach = p.gamemode == "CREATIVE" ? 6.0 : 3.0;
    Vec3      end   = eye + lookDir(p.pitch, p.yaw) * reach;
    BlockHit  bh    = W(L).raycastBlock(eye, end, "OUTLINE", "NONE");
    if (bh.hit) end = bh.pos;
    EntityHit eh = W(L).raycastEntity(eye, end);
    if (!eh.hit) return pushNil(L);
    pushEntity(L, eh.entity);
    return 1;
}
int hIsChatVerified(lua_State* L) { return pushB(L, false); }
int hWriteToLog(lua_State* L) { checkStr(L, 2, "writeToLog"); return 0; }
int hWarnToLog(lua_State* L) { checkStr(L, 2, "warnToLog"); return 0; }
int hIndex(lua_State* L) {
    if (lua_type(L, 2) == LUA_TSTRING && std::string(lua_tostring(L, 2)) == "unlockCursor") return pushB(L, hs(L).unlockCursor);
    return 0;
}
int hNewIndex(lua_State* L) {
    std::string k = checkStr(L, 2, "__newindex");
    if (k == "unlockCursor") { hs(L).unlockCursor = lua_toboolean(L, 3) != 0; return 0; }
    return luaL_error(L, "Cannot assign value on key \"%s\"", k.c_str());
}
int hToString(lua_State* L) { return pushS(L, "HostAPI"); }

// ---------------------------------------------------------------- renderer
Avatar::RenderSettings& RS(lua_State* L) { return avatarOf(L)->render; }
std::optional<bool> optBoolArg(lua_State* L, int i) { if (isNil(L, i)) return std::nullopt; return lua_toboolean(L, i) != 0; }
int pushOptB(lua_State* L, std::optional<bool> const& b) { if (!b) return pushNil(L); return pushB(L, *b); }
int rShouldRenderFire(lua_State* L) { return pushB(L, RS(L).renderFire); }
int rSetRenderFire(lua_State* L) { RS(L).renderFire = lua_toboolean(L, 2) != 0; return ret1(L); }
int rShouldRenderVehicle(lua_State* L) { return pushB(L, RS(L).renderVehicle); }
int rSetRenderVehicle(lua_State* L) { RS(L).renderVehicle = lua_toboolean(L, 2) != 0; return ret1(L); }
int rShouldRenderCrosshair(lua_State* L) { return pushB(L, RS(L).renderCrosshair); }
int rSetRenderCrosshair(lua_State* L) { RS(L).renderCrosshair = lua_toboolean(L, 2) != 0; return ret1(L); }
int rShouldForcePaperdoll(lua_State* L) { return pushB(L, RS(L).forcePaperdoll); }
int rSetForcePaperdoll(lua_State* L) { RS(L).forcePaperdoll = lua_toboolean(L, 2) != 0; return ret1(L); }
int rShouldRenderHUD(lua_State* L) { return pushB(L, RS(L).renderHUD); }
int rSetRenderHUD(lua_State* L) { RS(L).renderHUD = lua_toboolean(L, 2) != 0; return ret1(L); }
int rIsUpsideDown(lua_State* L) { return pushB(L, RS(L).upsideDown.value_or(false)); }
int rSetUpsideDown(lua_State* L) { RS(L).upsideDown = optBoolArg(L, 2); return ret1(L); }
int rSetShadowRadius(lua_State* L) {
    auto& r = RS(L);
    if (isNil(L, 2)) r.shadowRadius.reset();
    else r.shadowRadius = std::clamp(float(luaL_checknumber(L, 2)), 0.0f, 12.0f);
    av(L)->shadowRadius = r.shadowRadius ? *r.shadowRadius : -1.0f;
    return ret1(L);
}
int rGetShadowRadius(lua_State* L) { return pushN(L, RS(L).shadowRadius.value_or(0.5f)); }
int rIsFirstPerson(lua_State* L) { return pushB(L, av(L)->host->isFirstPerson() || P(L).isFirstPerson); }
int rIsCameraBackwards(lua_State* L) { return pushB(L, av(L)->host->isCameraBackwards()); }
#define R_VEC3(Get, Set, field, fnName)                                                                              \
    int Get(lua_State* L) { pushVec3o(L, RS(L).field); return 1; }                                                   \
    int Set(lua_State* L) { RS(L).field = nullableVec3(L, 2, fnName); return ret1(L); }
R_VEC3(rGetCameraPos, rSetCameraPos, cameraPos, "setCameraPos")
R_VEC3(rGetCameraPivot, rSetCameraPivot, cameraPivot, "setCameraPivot")
R_VEC3(rGetCameraOffsetPivot, rSetOffsetCameraPivot, cameraOffsetPivot, "setOffsetCameraPivot")
R_VEC3(rGetCameraRot, rSetCameraRot, cameraRot, "setCameraRot")
R_VEC3(rGetCameraOffsetRot, rSetOffsetCameraRot, cameraOffsetRot, "setOffsetCameraRot")
R_VEC3(rGetOutlineColor, rSetOutlineColor, outlineColor, "setOutlineColor")
R_VEC3(rGetEyeOffset, rSetEyeOffset, eyeOffset, "setEyeOffset")
#undef R_VEC3
void setRefSlot(lua_State* L, int& ref, int idx) {
    if (ref) luaL_unref(L, LUA_REGISTRYINDEX, ref);
    ref = 0;
    if (!isNil(L, idx)) { lua_pushvalue(L, idx); ref = luaL_ref(L, LUA_REGISTRYINDEX); }
}
int pushRefSlot(lua_State* L, int ref) { if (!ref) return pushNil(L); lua_rawgeti(L, LUA_REGISTRYINDEX, ref); return 1; }
int rGetCameraMatrix(lua_State* L) { return pushRefSlot(L, RS(L).cameraMatrixRef); }
int rSetCameraMatrix(lua_State* L) {
    auto& r = RS(L);
    if (isNil(L, 2)) r.cameraMatrix.reset();
    else if (Mat4* m = toMat4(L, 2)) r.cameraMatrix = *m;
    else luaL_error(L, "bad argument 2 to 'setCameraMatrix' (Matrix4 expected, got %s)", figuraTypeName(L, 2).c_str());
    if (r.cameraMatrix) pushMat4(L, *r.cameraMatrix); else lua_pushnil(L); // keep a copy, like Figura keeps the object
    setRefSlot(L, r.cameraMatrixRef, lua_gettop(L));
    lua_pop(L, 1);
    return ret1(L);
}
int rGetCameraNormal(lua_State* L) { return pushRefSlot(L, RS(L).cameraNormalRef); }
int rSetCameraNormal(lua_State* L) {
    if (!isNil(L, 2) && figuraTypeName(L, 2) != "Matrix3")
        luaL_error(L, "bad argument 2 to 'setCameraNormal' (Matrix3 expected, got %s)", figuraTypeName(L, 2).c_str());
    setRefSlot(L, RS(L).cameraNormalRef, 2);
    return ret1(L);
}
int rSetPostEffect(lua_State* L) {
    auto& r = RS(L);
    if (isNil(L, 2)) { r.postEffect.clear(); return ret1(L); }
    std::string e = luaL_checkstring(L, 2);
    static std::set<std::string> known{"notch", "fxaa", "art", "bumpy", "blobs2", "pencil", "color_convolve", "deconverge", "flip",
                                       "invert", "ntsc", "outline", "phosphor", "scan_pincushion", "sobel", "bits", "desaturate",
                                       "green", "blur", "wobble", "blobs", "antialias", "creeper", "spider", "entity_outline", "transparency"};
    std::string path = "shaders/post/" + e + ".json";
    std::string loc  = resLoc(path);
    if (!known.count(stripNs(e))) luaL_error(L, "The post shader %s does not exist or could not be found", loc.c_str());
    r.postEffect = loc;
    return ret1(L);
}
int rGetFOV(lua_State* L) { auto& f = RS(L).fov; if (!f) return pushNil(L); return pushN(L, *f); }
int rSetFOV(lua_State* L) { if (isNil(L, 2)) RS(L).fov.reset(); else RS(L).fov = float(luaL_checknumber(L, 2)); return ret1(L); }
int rGetCrosshairOffset(lua_State* L) { auto& c = RS(L).crosshairOffset; if (!c) return pushNil(L); pushVec2(L, c->x, c->y); return 1; }
int rSetCrosshairOffset(lua_State* L) {
    if (isNil(L, 2)) RS(L).crosshairOffset.reset();
    else RS(L).crosshairOffset = parseVec2(L, 2, "setCrosshairOffset");
    return ret1(L);
}
std::string fireTex(lua_State* L, int i) {
    std::string id  = luaL_checkstring(L, i);
    std::string loc = resLoc(id);
    size_t      c   = loc.find(':');
    std::string ns = loc.substr(0, c), path = loc.substr(c + 1);
    if (path.rfind("textures/", 0) == 0) path = path.substr(9);
    return ns + ":" + path;
}
int rGetPrimaryFireTexture(lua_State* L) { return pushS(L, RS(L).primaryFireTexture); }
int rGetSecondaryFireTexture(lua_State* L) { return pushS(L, RS(L).secondaryFireTexture); }
int rSetPrimaryFireTexture(lua_State* L) { RS(L).primaryFireTexture = isNil(L, 2) ? "" : fireTex(L, 2); return ret1(L); }
int rSetSecondaryFireTexture(lua_State* L) { RS(L).secondaryFireTexture = isNil(L, 2) ? "" : fireTex(L, 2); return ret1(L); }
int rSetRenderLeftArm(lua_State* L) { RS(L).renderLeftArm = optBoolArg(L, 2); return ret1(L); }
int rGetRenderLeftArm(lua_State* L) { return pushOptB(L, RS(L).renderLeftArm); }
int rSetRenderRightArm(lua_State* L) { RS(L).renderRightArm = optBoolArg(L, 2); return ret1(L); }
int rGetRenderRightArm(lua_State* L) { return pushOptB(L, RS(L).renderRightArm); }
int rSetBlockOutlineColor(lua_State* L) {
    if (isNil(L, 2)) RS(L).blockOutlineColor.reset();
    else RS(L).blockOutlineColor = parseVec4(L, 2, "setColor", 0, 0, 0, 0.4);
    return ret1(L);
}
int rGetBlockOutlineColor(lua_State* L) {
    auto& c = RS(L).blockOutlineColor;
    if (!c) return pushNil(L);
    pushVec4(L, c->x, c->y, c->z, c->w);
    return 1;
}
int rSetRootRotationAllowed(lua_State* L) { RS(L).rootRotation = optBoolArg(L, 2); return ret1(L); }
int rGetRootRotationAllowed(lua_State* L) { return pushB(L, RS(L).rootRotation.value_or(true)); }
int rIndex(lua_State* L) {
    if (lua_type(L, 2) != LUA_TSTRING) return 0;
    std::string k = lua_tostring(L, 2);
    auto&       r = RS(L);
    if (k == "renderFire") return pushB(L, r.renderFire);
    if (k == "renderVehicle") return pushB(L, r.renderVehicle);
    if (k == "renderCrosshair") return pushB(L, r.renderCrosshair);
    if (k == "forcePaperdoll") return pushB(L, r.forcePaperdoll);
    if (k == "renderHUD") return pushB(L, r.renderHUD);
    return 0;
}
int rNewIndex(lua_State* L) {
    std::string k = checkStr(L, 2, "__newindex");
    bool        v = lua_toboolean(L, 3) != 0;
    auto&       r = RS(L);
    if (k == "renderFire") r.renderFire = v;
    else if (k == "renderVehicle") r.renderVehicle = v;
    else if (k == "renderCrosshair") r.renderCrosshair = v;
    else if (k == "forcePaperdoll") r.forcePaperdoll = v;
    else if (k == "renderHUD") r.renderHUD = v;
    else return luaL_error(L, "Cannot assign value on key \"%s\"", k.c_str());
    return 0;
}
int rToString(lua_State* L) { return pushS(L, "RendererAPI"); }

// ---------------------------------------------------------------- vanilla_model
struct VPart {
    std::string              name;
    bool                     group = false;
    ParentType               parent = ParentType::None;
    int                      provider = 0; // 0 none, else VanillaPose slot (1 head .. 9 rightElytra)
    std::optional<bool>      visible;
    std::optional<Vec3>      pos, rot, scale, offsetRot, offsetScale;
    std::vector<std::string> children;
};
struct VanillaState {
    std::vector<std::unique_ptr<VPart>> parts;
    std::map<std::string, VPart*>       byName;
};
std::map<lua_State*, VanillaState>& vstates() { static std::map<lua_State*, VanillaState> m; return m; }
VanillaState& vs(lua_State* L) { return vstates()[L]; }
VPart* vpOf(lua_State* L, int i) {
    if (void* p = toRef(L, i, tVanillaPart)) return static_cast<VPart*>(p);
    if (void* p = toRef(L, i, tVanillaGroup)) return static_cast<VPart*>(p);
    return nullptr;
}
VPart* vp(lua_State* L, char const* fn) {
    VPart* p = vpOf(L, 1);
    if (!p) luaL_error(L, "bad argument 1 to '%s' (VanillaPart expected, got %s)", fn, figuraTypeName(L, 1).c_str());
    return p;
}
void vSync(lua_State* L, VPart* p) { // publish to the host-readable Avatar state
    Avatar* a = av(L);
    if (p->visible) a->vanillaVisible[p->name] = *p->visible;
    else a->vanillaVisible.erase(p->name);
    if (p->group) return;
    auto& o       = a->vanillaOverrides[p->name];
    o.visible     = p->visible;
    o.pos         = p->pos;
    o.rot         = p->rot;
    o.scale       = p->scale;
    o.offsetRot   = p->offsetRot;
    o.offsetScale = p->offsetScale;
}
template <class F> void vApply(lua_State* L, VPart* p, F const& f) { // VanillaGroupPart: children first, then itself
    for (auto const& c : p->children)
        if (auto it = vs(L).byName.find(c); it != vs(L).byName.end()) vApply(L, it->second, f);
    f(p);
    vSync(L, p);
}
int vpSetVisible(lua_State* L) { VPart* p = vp(L, "setVisible"); auto v = optBoolArg(L, 2); vApply(L, p, [&](VPart* q) { q->visible = v; }); return ret1(L); }
int vpGetVisible(lua_State* L) { return pushOptB(L, vp(L, "getVisible")->visible); }
int vpSetPos(lua_State* L) { VPart* p = vp(L, "setPos"); auto v = nullableVec3(L, 2, "setPos"); vApply(L, p, [&](VPart* q) { q->pos = v; }); return ret1(L); }
int vpSetRot(lua_State* L) { VPart* p = vp(L, "setRot"); auto v = nullableVec3(L, 2, "setRot"); vApply(L, p, [&](VPart* q) { q->rot = v; }); return ret1(L); }
int vpSetOffsetRot(lua_State* L) { VPart* p = vp(L, "setOffsetRot"); auto v = nullableVec3(L, 2, "setOffsetRot"); vApply(L, p, [&](VPart* q) { q->offsetRot = v; }); return ret1(L); }
int vpSetScale(lua_State* L) {
    VPart* p = vp(L, "setScale");
    std::optional<Vec3> v;
    if (!isNil(L, 2)) v = oneArgVec(L, 2, "setScale", 1);
    vApply(L, p, [&](VPart* q) { q->scale = v; });
    return ret1(L);
}
int vpSetOffsetScale(lua_State* L) {
    VPart* p = vp(L, "setOffsetScale");
    std::optional<Vec3> v;
    if (!isNil(L, 2)) v = oneArgVec(L, 2, "setOffsetScale", 1);
    vApply(L, p, [&](VPart* q) { q->offsetScale = v; });
    return ret1(L);
}
int vpGetPos(lua_State* L) { pushVec3o(L, vp(L, "getPos")->pos); return 1; }
int vpGetRot(lua_State* L) { pushVec3o(L, vp(L, "getRot")->rot); return 1; }
int vpGetScale(lua_State* L) { pushVec3o(L, vp(L, "getScale")->scale); return 1; }
int vpGetOffsetRot(lua_State* L) { pushVec3o(L, vp(L, "getOffsetRot")->offsetRot); return 1; }
int vpGetOffsetScale(lua_State* L) { pushVec3o(L, vp(L, "getOffsetScale")->offsetScale); return 1; }
VanillaPart const* vpose(lua_State* L, VPart const* p) {
    auto const& v = av(L)->vanillaPose;
    switch (p->provider) {
    case 1: return &v.head;
    case 2: return &v.body;
    case 3: return &v.leftArm;
    case 4: return &v.rightArm;
    case 5: return &v.leftLeg;
    case 6: return &v.rightLeg;
    case 7: return &v.cape;
    case 8: return &v.leftElytra;
    case 9: return &v.rightElytra;
    default: return nullptr;
    }
}
int vpGetOriginRot(lua_State* L) { auto* d = vpose(L, vp(L, "getOriginRot")); pushVec3(L, d ? d->rot : Vec3{}); return 1; }
int vpGetOriginPos(lua_State* L) {
    VPart* p = vp(L, "getOriginPos");
    auto*  d = vpose(L, p);
    if (!d) { pushVec3(L, {}); return 1; }
    // VanillaModelPart.save: (parentType.offset - part.xyz) * (1, -1, -1); VanillaPart.pos = (x, y, -z)
    Vec3 off = parentTypeOffset(p->parent);
    pushVec3(L, {off.x - d->pos.x, -(off.y - d->pos.y), -(off.z + d->pos.z)});
    return 1;
}
int vpGetOriginScale(lua_State* L) { auto* d = vpose(L, vp(L, "getOriginScale")); pushVec3(L, d ? d->scale : Vec3{}); return 1; }
int vpGetOriginVisible(lua_State* L) { auto* d = vpose(L, vp(L, "getOriginVisible")); return pushB(L, d ? d->visible : false); }
int vgIndex(lua_State* L) {
    VPart* p = vp(L, "__index");
    if (lua_type(L, 2) != LUA_TSTRING) return 0;
    std::string k = lua_tostring(L, 2);
    if (std::find(p->children.begin(), p->children.end(), k) == p->children.end()) return 0;
    VPart* c = vs(L).byName[k];
    pushRef(L, c, c->group ? tVanillaGroup : tVanillaPart);
    return 1;
}
int vpToString(lua_State* L) { return pushS(L, vp(L, "__tostring")->group ? "VanillaModelGroup" : "VanillaModelPart"); }
int vmIndex(lua_State* L) {
    if (lua_type(L, 2) != LUA_TSTRING) return 0;
    auto& m  = vs(L).byName;
    auto  it = m.find(upper(lua_tostring(L, 2)));
    if (it == m.end()) return 0;
    pushRef(L, it->second, it->second->group ? tVanillaGroup : tVanillaPart);
    return 1;
}
int vmToString(lua_State* L) { return pushS(L, "VanillaModelAPI"); }

// ---------------------------------------------------------------- nameplate
struct NPart {
    int kind = 0; // 0 CHAT 1 ENTITY 2 LIST 3 ALL
};
NPart gNameplates[4] = {{0}, {1}, {2}, {3}};
Avatar::NameplateSettings& NS(lua_State* L) { return avatarOf(L)->nameplate; }
NPart* npOf(lua_State* L, int i) {
    for (uint32_t t : {tNameplate, tEntityNameplate, tNameplateGroup})
        if (void* p = toRef(L, i, t)) return static_cast<NPart*>(p);
    return nullptr;
}
NPart* np(lua_State* L, char const* fn) {
    NPart* p = npOf(L, 1);
    if (!p) luaL_error(L, "bad argument 1 to '%s' (NameplateCustomization expected, got %s)", fn, figuraTypeName(L, 1).c_str());
    return p;
}
std::optional<std::string>& npText(lua_State* L, int kind) {
    auto& n = NS(L);
    return kind == 0 ? n.chatText : kind == 1 ? n.entityText : n.listText;
}
int npSetText(lua_State* L) {
    NPart* p = np(L, "setText");
    std::optional<std::string> t;
    if (!isNil(L, 2)) {
        t = std::string(luaL_checkstring(L, 2));
        std::string plain = plainText(*t);
        size_t      cps   = 0;
        for (unsigned char c : plain) if ((c & 0xC0) != 0x80) cps++;
        if (cps > 64) luaL_error(L, "Text length exceeded limit of 64 characters");
    }
    if (p->kind == 3) for (int k = 0; k < 3; k++) npText(L, k) = t;
    else npText(L, p->kind) = t;
    return ret1(L);
}
int npGetText(lua_State* L) { NPart* p = np(L, "getText"); auto& t = npText(L, p->kind); if (!t) return pushNil(L); return pushS(L, *t); }
int npGetPivot(lua_State* L) { (void)np(L, "getPivot"); pushVec3o(L, NS(L).pivot); return 1; }
int npSetPivot(lua_State* L) { (void)np(L, "setPivot"); NS(L).pivot = nullableVec3(L, 2, "setPivot"); return ret1(L); }
int npGetPos(lua_State* L) { (void)np(L, "getPos"); pushVec3o(L, NS(L).pos); return 1; }
int npSetPos(lua_State* L) { (void)np(L, "setPos"); NS(L).pos = nullableVec3(L, 2, "setPos"); return ret1(L); }
int npGetScale(lua_State* L) { (void)np(L, "getScale"); pushVec3o(L, NS(L).scale); return 1; }
int npSetScale(lua_State* L) {
    (void)np(L, "setScale");
    if (isNil(L, 2)) NS(L).scale.reset();
    else NS(L).scale = oneArgVec(L, 2, "setScale", 1);
    return ret1(L);
}
int npGetBackgroundColor(lua_State* L) {
    (void)np(L, "getBackgroundColor");
    auto& b = NS(L).background;
    if (!b) return pushNil(L);
    uint32_t c = *b; // ColorUtils.intToRGBA: ARGB
    pushVec4(L, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0, ((c >> 24) & 255) / 255.0);
    return 1;
}
int npSetBackgroundColor(lua_State* L) {
    (void)np(L, "setBackgroundColor");
    Vec4 v = parseVec4(L, 2, "setBackgroundColor", 0, 0, 0, 0.25);
    auto ch = [](double d) { return uint32_t(std::clamp(int(d * 255), 0, 255)); };
    NS(L).background = (ch(v.w) << 24) | (ch(v.x) << 16) | (ch(v.y) << 8) | ch(v.z);
    return ret1(L);
}
int npSetOutlineColor(lua_State* L) { (void)np(L, "setOutlineColor"); NS(L).outlineColor = rgbToInt(parseVec3(L, 2, "setOutlineColor")); return ret1(L); }
int npGetLight(lua_State* L) {
    (void)np(L, "getLight");
    auto& l = NS(L).light;
    if (!l) return pushNil(L);
    pushVec2(L, (*l & 0xFFFF) >> 4, (*l >> 16) >> 4);
    return 1;
}
int npSetLight(lua_State* L) {
    (void)np(L, "setLight");
    if (isNil(L, 2)) { NS(L).light.reset(); return ret1(L); }
    Vec2 v = parseVec2(L, 2, "setLight");
    NS(L).light = (int(v.x) << 4) | (int(v.y) << 20); // LightTexture.pack
    return ret1(L);
}
int npIsVisible(lua_State* L) { (void)np(L, "isVisible"); return pushB(L, NS(L).entityVisible); }
int npSetVisible(lua_State* L) { (void)np(L, "setVisible"); NS(L).entityVisible = lua_toboolean(L, 2) != 0; return ret1(L); }
int npHasOutline(lua_State* L) { (void)np(L, "hasOutline"); return pushB(L, NS(L).outline); }
int npSetOutline(lua_State* L) { (void)np(L, "setOutline"); NS(L).outline = lua_toboolean(L, 2) != 0; return ret1(L); }
int npHasShadow(lua_State* L) { (void)np(L, "hasShadow"); return pushB(L, NS(L).shadow); }
int npSetShadow(lua_State* L) { (void)np(L, "setShadow"); NS(L).shadow = lua_toboolean(L, 2) != 0; return ret1(L); }
int npToString(lua_State* L) {
    NPart* p = np(L, "__tostring");
    return pushS(L, p->kind == 3 ? "NameplateCustomizationGroup" : p->kind == 1 ? "EntityNameplateCustomization" : "NameplateCustomization");
}
void pushNameplate(lua_State* L, int kind) {
    pushRef(L, &gNameplates[kind], kind == 3 ? tNameplateGroup : kind == 1 ? tEntityNameplate : tNameplate);
}
int nmIndex(lua_State* L) {
    if (lua_type(L, 2) != LUA_TSTRING) return 0;
    std::string k = upper(lua_tostring(L, 2));
    int kind = k == "CHAT" ? 0 : k == "ENTITY" ? 1 : k == "LIST" ? 2 : k == "ALL" ? 3 : -1;
    if (kind < 0) return 0;
    pushNameplate(L, kind);
    return 1;
}
int nmToString(lua_State* L) { return pushS(L, "NameplateAPI"); }

// ---------------------------------------------------------------- sounds
struct SoundUD {
    std::string id;
    bool        custom = false, known = false;
    Vec3        pos;
    float       volume = 1, pitch = 1, attenuation = 1;
    bool        loop = false, playing = false, started = false;
    std::optional<std::string> subtitle;
    int         handle = 0;
};
int gSoundHandle = 1;
std::map<lua_State*, std::map<int, std::string>>& liveSounds() { static std::map<lua_State*, std::map<int, std::string>> m; return m; }
SoundUD& snd(lua_State* L, char const* fn) {
    void* p = testUD(L, 1, tSound);
    if (!p) luaL_error(L, "bad argument 1 to '%s' (Sound expected, got %s)", fn, figuraTypeName(L, 1).c_str());
    return *static_cast<SoundUD*>(p);
}
bool customSound(Avatar* a, std::string const& id) { return a->soundEvents.count(id) > 0; }
void pushSound(lua_State* L, std::string const& id) {
    SoundUD s;
    s.id     = id;
    s.custom = customSound(av(L), id);
    s.known  = s.custom || knownSound(id);
    if (s.custom || !s.known) s.subtitle = id; // LuaSound(buffer, id) / LuaSound(null, id): Component.literal(id)
    newUD(L, tSound, std::move(s));
}
bool sndIsPlayingNow(lua_State* L, SoundUD& s) {
    if (s.started && s.playing) s.playing = av(L)->host->soundPlaying(s.handle, true);
    if (!s.playing && s.started) liveSounds()[L].erase(s.handle);
    return s.playing;
}
int sPlay(lua_State* L) {
    SoundUD& s = snd(L, "play");
    if (s.playing && sndIsPlayingNow(L, s)) return ret1(L);
    if (!s.known) return ret1(L);
    if (!s.started || s.handle == 0) s.handle = gSoundHandle++;
    s.playing = s.started = true;
    av(L)->host->playSound(s.id, s.custom, s.pos, s.volume, s.pitch, s.loop, s.handle);
    liveSounds()[L][s.handle] = s.id;
    return ret1(L);
}
int sIsPlaying(lua_State* L) { SoundUD& s = snd(L, "isPlaying"); return pushB(L, sndIsPlayingNow(L, s)); }
int sPause(lua_State* L) {
    SoundUD& s = snd(L, "pause");
    if (s.playing && s.started) av(L)->host->pauseSound(s.handle);
    s.playing = false;
    return ret1(L);
}
int sStop(lua_State* L) {
    SoundUD& s = snd(L, "stop");
    if (s.started) { av(L)->host->stopSound(s.handle); liveSounds()[L].erase(s.handle); }
    s.playing = s.started = false;
    s.handle  = 0;
    return ret1(L);
}
void sUpdate(lua_State* L, SoundUD& s) { if (s.started && s.playing) av(L)->host->updateSound(s.handle, s.pos, s.volume, s.pitch, s.loop); }
int sGetPos(lua_State* L) { pushVec3(L, snd(L, "getPos").pos); return 1; }
int sSetPos(lua_State* L) { SoundUD& s = snd(L, "setPos"); s.pos = parseVec3(L, 2, "setPos"); sUpdate(L, s); return ret1(L); }
int sGetVolume(lua_State* L) { return pushN(L, snd(L, "getVolume").volume); }
int sSetVolume(lua_State* L) { SoundUD& s = snd(L, "setVolume"); s.volume = std::min(float(luaL_optnumber(L, 2, 0)), 1.0f); sUpdate(L, s); return ret1(L); }
int sGetAttenuation(lua_State* L) { return pushN(L, snd(L, "getAttenuation").attenuation); }
int sSetAttenuation(lua_State* L) { SoundUD& s = snd(L, "setAttenuation"); s.attenuation = std::max(float(luaL_optnumber(L, 2, 0)), 1.0f); return ret1(L); }
int sGetPitch(lua_State* L) { return pushN(L, snd(L, "getPitch").pitch); }
int sSetPitch(lua_State* L) { SoundUD& s = snd(L, "setPitch"); s.pitch = std::max(float(luaL_optnumber(L, 2, 0)), 0.0f); sUpdate(L, s); return ret1(L); }
int sIsLooping(lua_State* L) { return pushB(L, snd(L, "isLooping").loop); }
int sSetLoop(lua_State* L) { SoundUD& s = snd(L, "setLoop"); s.loop = lua_toboolean(L, 2) != 0; sUpdate(L, s); return ret1(L); }
int sGetSubtitle(lua_State* L) { auto& s = snd(L, "getSubtitle").subtitle; if (!s) return pushNil(L); return pushS(L, *s); }
int sSetSubtitle(lua_State* L) {
    SoundUD& s = snd(L, "setSubtitle");
    if (isNil(L, 2)) { s.subtitle.reset(); return ret1(L); }
    std::string t = luaL_checkstring(L, 2);
    if (plainText(t).size() > 48) luaL_error(L, "Text length exceeded limit of 48 characters");
    s.subtitle = t;
    return ret1(L);
}
int sToString(lua_State* L) { return pushS(L, snd(L, "__tostring").id + " (Sound)"); }
int sndPlaySound(lua_State* L) {
    std::string id = checkStr(L, 2, "playSound");
    Vec3        pos;
    float       volume = 1, pitch = 1;
    bool        loop = lua_toboolean(L, 8) != 0;
    int         n = 0;
    if (double* v = toVec(L, 3, &n); v && n == 3) {
        pos = {v[0], v[1], v[2]};
        if (!isNil(L, 4)) volume = float(luaL_checknumber(L, 4));
        if (!isNil(L, 5)) pitch = float(luaL_checknumber(L, 5));
        if (!isNil(L, 6)) {
            if (lua_type(L, 6) != LUA_TBOOLEAN) luaL_error(L, "Illegal argument to playSound(): %s", luaL_tolstring(L, 6, nullptr));
            loop = lua_toboolean(L, 6) != 0;
        }
    } else if (isNil(L, 3) || lua_type(L, 3) == LUA_TNUMBER) {
        pos = parseVec3(L, 3, "playSound");
        if (!isNil(L, 6)) {
            if (lua_type(L, 6) != LUA_TNUMBER) luaL_error(L, "Illegal argument to playSound(): %s", luaL_tolstring(L, 6, nullptr));
            volume = float(lua_tonumber(L, 6));
        }
        if (!isNil(L, 7)) pitch = float(luaL_checknumber(L, 7));
    } else luaL_error(L, "Illegal argument to playSound(): %s", luaL_tolstring(L, 3, nullptr));
    lua_settop(L, 0);
    pushSound(L, id);
    SoundUD& s = *static_cast<SoundUD*>(lua_touserdata(L, 1));
    s.pos    = pos;
    s.volume = std::min(volume, 1.0f);
    s.pitch  = std::max(pitch, 0.0f);
    s.loop   = loop;
    sPlay(L);
    return 1;
}
int sndStopSound(lua_State* L) {
    Avatar* a    = av(L);
    auto&   live = liveSounds()[L];
    if (isNil(L, 2)) { a->host->stopSound(-1); live.clear(); }
    else {
        std::string id = luaL_checkstring(L, 2);
        for (auto it = live.begin(); it != live.end();) {
            if (it->second == id) { a->host->stopSound(it->first); it = live.erase(it); }
            else ++it;
        }
    }
    return ret1(L);
}
int sndNewSound(lua_State* L) {
    std::string name = checkStr(L, 2, "newSound");
    notNil(L, 3, "newSound");
    if (lua_type(L, 3) == LUA_TTABLE) {
        int n = int(lua_rawlen(L, 3));
        for (int i = 1; i <= n; i++) { lua_rawgeti(L, 3, i); luaL_checkinteger(L, -1); lua_pop(L, 1); }
    } else if (lua_type(L, 3) != LUA_TSTRING) luaL_error(L, "Invalid type for newSound \"%s\"", figuraTypeName(L, 3).c_str());
    // runtime-made sound data can't be registered with Bedrock's sound engine: keep the name (plays nothing)
    auto& ev = av(L)->soundEvents;
    if (!ev.count(name)) ev[name] = "";
    return ret1(L);
}
int sndIsPresent(lua_State* L) {
    if (isNil(L, 2)) return pushB(L, false);
    std::string id = luaL_checkstring(L, 2);
    return pushB(L, customSound(av(L), id) || knownSound(id));
}
int sndGetCustomSounds(lua_State* L) {
    std::vector<std::string> v;
    for (auto const& [k, x] : av(L)->soundEvents) v.push_back(k);
    return pushStrings(L, v);
}
int sndIndex(lua_State* L) {
    if (lua_type(L, 2) != LUA_TSTRING) return 0;
    pushSound(L, lua_tostring(L, 2));
    return 1;
}
int sndToString(lua_State* L) { return pushS(L, "SoundAPI"); }

// ---------------------------------------------------------------- particles
struct ParticleUD {
    std::string  name;
    ParticleSpec spec;
    int          handle = 0;
    bool         removed = false;
    int64_t      spawnMs = 0;
};
ParticleUD& ptc(lua_State* L, char const* fn) {
    void* p = testUD(L, 1, tParticle);
    if (!p) luaL_error(L, "bad argument 1 to '%s' (Particle expected, got %s)", fn, figuraTypeName(L, 1).c_str());
    return *static_cast<ParticleUD*>(p);
}
std::string particleType(std::string const& id) {
    std::string s = id;
    size_t      i = s.find_first_not_of(' ');
    if (i == std::string::npos) return "";
    s = s.substr(i);
    s = s.substr(0, s.find_first_of(" ["));
    return lower(s);
}
void checkParticle(lua_State* L, std::string const& id) {
    std::string t = particleType(id);
    if (t.empty() || (t.find(':') != std::string::npos && t.rfind("minecraft:", 0) != 0) || !knownParticle(t))
        luaL_error(L, "Unknown particle: %s", t.empty() ? id.c_str() : resLoc(t).c_str());
}
int defaultLifetime(std::string const& t) {
    std::string n = stripNs(t);
    if (n == "flame" || n == "soul_fire_flame" || n == "small_flame") return 20;
    if (n == "end_rod") return 70;
    if (n == "heart" || n == "angry_villager" || n == "happy_villager" || n == "note") return 16;
    if (n == "explosion") return 8;
    if (n == "poof" || n == "cloud" || n == "smoke") return 12;
    return 20;
}
ParticleUD newParticleUD(lua_State* L, std::string const& id, Vec3 pos, Vec3 vel) {
    checkParticle(L, id);
    ParticleUD p;
    p.name          = id;
    p.spec.id       = id;
    p.spec.pos      = pos;
    p.spec.velocity = vel;
    p.spec.lifetime = defaultLifetime(particleType(id));
    std::string n   = stripNs(particleType(id));
    if (n == "dust" || n == "dust_color_transition") { // dust r g b scale
        std::string rest = id.substr(id.find(n) + n.size());
        double r = 1, g = 1, b = 1, sc = 1;
        if (std::sscanf(rest.c_str(), " %lf %lf %lf %lf", &r, &g, &b, &sc) >= 3) { p.spec.color = {r, g, b, 1}; p.spec.scale = float(sc); }
    }
    return p;
}
void ptSpawnNow(lua_State* L, ParticleUD& p) {
    if (av(L)->host->paused()) return;
    p.handle  = av(L)->host->spawnParticle(p.spec);
    p.spawnMs = av(L)->host->millis();
    p.removed = false;
}
int ptSpawn(lua_State* L) { ptSpawnNow(L, ptc(L, "spawn")); return ret1(L); }
int ptRemove(lua_State* L) { ParticleUD& p = ptc(L, "remove"); if (p.handle) av(L)->host->removeParticle(p.handle); p.removed = true; return ret1(L); }
int ptIsAlive(lua_State* L) {
    ParticleUD& p = ptc(L, "isAlive");
    if (p.removed) return pushB(L, false);
    if (p.spawnMs && av(L)->host->millis() - p.spawnMs > int64_t(std::max(0, p.spec.lifetime)) * 50) return pushB(L, false);
    return pushB(L, true);
}
int ptGetPos(lua_State* L) { pushVec3(L, ptc(L, "getPos").spec.pos); return 1; }
int ptSetPos(lua_State* L) { ParticleUD& p = ptc(L, "setPos"); p.spec.pos = parseVec3(L, 2, "setPos"); return ret1(L); }
int ptGetVelocity(lua_State* L) { pushVec3(L, ptc(L, "getVelocity").spec.velocity); return 1; }
int ptSetVelocity(lua_State* L) { ParticleUD& p = ptc(L, "setVelocity"); p.spec.velocity = parseVec3(L, 2, "setVelocity"); return ret1(L); }
int ptGetColor(lua_State* L) { auto& c = ptc(L, "getColor").spec.color; pushVec4(L, c.x, c.y, c.z, c.w); return 1; }
int ptSetColor(lua_State* L) {
    ParticleUD& p = ptc(L, "setColor");
    p.spec.color    = parseVec4(L, 2, "setColor", 1, 1, 1, 1);
    p.spec.colorSet = true;
    return ret1(L);
}
int ptGetLifetime(lua_State* L) { return pushI(L, ptc(L, "getLifetime").spec.lifetime); }
int ptSetLifetime(lua_State* L) {
    ParticleUD& p = ptc(L, "setLifetime");
    int a = int(luaL_optinteger(L, 2, 0));
    if (stripNs(particleType(p.name)) == "fishing") a = std::min(a, 60);
    p.spec.lifetime = std::max(a, 0);
    return ret1(L);
}
int ptGetPower(lua_State* L) { return pushN(L, ptc(L, "getPower").spec.power); }
int ptSetPower(lua_State* L) { ParticleUD& p = ptc(L, "setPower"); p.spec.power = float(luaL_optnumber(L, 2, 0)); return ret1(L); }
int ptGetScale(lua_State* L) { return pushN(L, ptc(L, "getScale").spec.scale); }
int ptSetScale(lua_State* L) { ParticleUD& p = ptc(L, "setScale"); p.spec.scale = float(luaL_optnumber(L, 2, 0)); return ret1(L); }
int ptGetGravity(lua_State* L) { return pushN(L, ptc(L, "getGravity").spec.gravity); }
int ptSetGravity(lua_State* L) { ParticleUD& p = ptc(L, "setGravity"); p.spec.gravity = float(luaL_optnumber(L, 2, 0)); return ret1(L); }
int ptHasPhysics(lua_State* L) { return pushB(L, ptc(L, "hasPhysics").spec.physics); }
int ptSetPhysics(lua_State* L) { ParticleUD& p = ptc(L, "setPhysics"); p.spec.physics = lua_toboolean(L, 2) != 0; return ret1(L); }
int ptToString(lua_State* L) { return pushS(L, ptc(L, "__tostring").name + " (Particle)"); }
int paNewParticle(lua_State* L) {
    std::string id = checkStr(L, 2, "newParticle");
    int         i  = 3;
    Vec3        pos = seqVec3(L, i, "newParticle"), vel = seqVec3(L, i, "newParticle");
    ParticleUD& p = *newUD(L, tParticle, newParticleUD(L, id, pos, vel));
    ptSpawnNow(L, p);
    return 1;
}
int paRemoveParticles(lua_State* L) { av(L)->host->removeParticles(); return ret1(L); }
int paIsPresent(lua_State* L) {
    if (isNil(L, 2) || lua_type(L, 2) != LUA_TSTRING) return pushB(L, false);
    std::string t = particleType(lua_tostring(L, 2));
    return pushB(L, !t.empty() && (t.find(':') == std::string::npos || t.rfind("minecraft:", 0) == 0) && knownParticle(t));
}
int paIndex(lua_State* L) {
    if (lua_type(L, 2) != LUA_TSTRING) return 0;
    newUD(L, tParticle, newParticleUD(L, lua_tostring(L, 2), {}, {}));
    return 1;
}
int paToString(lua_State* L) { return pushS(L, "ParticleAPI"); }

// ---------------------------------------------------------------- raycast
int rcBlock(lua_State* L) {
    int  i = 1;
    Vec3 a = seqVec3(L, i, "block"), b = seqVec3(L, i, "block");
    std::string bm = isNil(L, i) ? "COLLIDER" : upper(luaL_checkstring(L, i));
    std::string fm = isNil(L, i + 1) ? "NONE" : upper(luaL_checkstring(L, i + 1));
    static std::set<std::string> bms{"COLLIDER", "OUTLINE", "VISUAL", "FALLDAMAGE_RESETTING"}, fms{"NONE", "SOURCE_ONLY", "ANY", "WATER"};
    if (!bms.count(bm)) luaL_error(L, "Invalid blockRaycastType provided");
    if (!fms.count(fm)) luaL_error(L, "Invalid fluidRaycastType provided");
    pushBlockHit(L, W(L).raycastBlock(a, b, bm, fm));
    return 3;
}
int rcEntity(lua_State* L) {
    int  i = 1;
    Vec3 a = seqVec3(L, i, "entity"), b = seqVec3(L, i, "entity");
    int  fn = isNil(L, i) ? 0 : i;
    if (fn && !lua_isfunction(L, fn)) luaL_error(L, "Illegal argument at position %d to entity(): Expected LuaFunction, recieved %s", fn, figuraTypeName(L, fn).c_str());
    std::vector<std::pair<EntityRef, Vec3>> hits;
    EntityHit h = W(L).raycastEntity(a, b);
    if (h.hit && h.entity.valid) hits.push_back({h.entity, h.pos});
    for (auto const& [e, pos] : hits) {
        if (fn) {
            lua_pushvalue(L, fn);
            pushEntity(L, e);
            lua_call(L, 1, 1);
            bool ok = !(lua_isnil(L, -1) || (lua_isboolean(L, -1) && !lua_toboolean(L, -1)));
            lua_pop(L, 1);
            if (!ok) continue;
        }
        pushEntity(L, e);
        pushVec3(L, pos);
        return 2;
    }
    return pushNil(L);
}
/// AABB.clip + AABBInvoker.getDirection: entry point and face
bool clipAABB(Vec3 mn, Vec3 mx, Vec3 a, Vec3 b, Vec3& hit, char const*& face) {
    double ds = 1.0;
    Vec3   d  = b - a;
    face      = nullptr;
    auto plane = [&](double dd, double start, double planeV, double o1d, double o1s, double o1min, double o1max, double o2d, double o2s,
                     double o2min, double o2max, char const* f) {
        if (std::abs(dd) < 1e-7) return;
        double t = (planeV - start) / dd;
        if (t <= 0 || t >= ds) return;
        double u = o1s + t * o1d, v = o2s + t * o2d;
        if (u > o1min - 1e-7 && u < o1max + 1e-7 && v > o2min - 1e-7 && v < o2max + 1e-7) { ds = t; face = f; }
    };
    if (d.x > 1e-7) plane(d.x, a.x, mn.x, d.y, a.y, mn.y, mx.y, d.z, a.z, mn.z, mx.z, "west");
    else if (d.x < -1e-7) plane(d.x, a.x, mx.x, d.y, a.y, mn.y, mx.y, d.z, a.z, mn.z, mx.z, "east");
    if (d.y > 1e-7) plane(d.y, a.y, mn.y, d.z, a.z, mn.z, mx.z, d.x, a.x, mn.x, mx.x, "down");
    else if (d.y < -1e-7) plane(d.y, a.y, mx.y, d.z, a.z, mn.z, mx.z, d.x, a.x, mn.x, mx.x, "up");
    if (d.z > 1e-7) plane(d.z, a.z, mn.z, d.x, a.x, mn.x, mx.x, d.y, a.y, mn.y, mx.y, "north");
    else if (d.z < -1e-7) plane(d.z, a.z, mx.z, d.x, a.x, mn.x, mx.x, d.y, a.y, mn.y, mx.y, "south");
    if (!face) return false;
    hit = a + d * ds;
    return true;
}
int rcAabb(lua_State* L) {
    int  i = 1;
    Vec3 a = seqVec3(L, i, "aabb"), b = seqVec3(L, i, "aabb");
    if (isNil(L, i)) luaL_error(L, "Illegal argument to aabb(): Expected LuaTable, recieved nil");
    if (!lua_istable(L, i)) luaL_error(L, "Illegal argument at position %d to aabb(): Expected LuaTable, recieved %s", i, figuraTypeName(L, i).c_str());
    int    t = i, n = int(lua_rawlen(L, t));
    double bestD = 1e300;
    int    index = -1;
    Vec3   bestPos;
    char const* bestFace = nullptr;
    for (int k = 1; k <= n; k++) {
        lua_rawgeti(L, t, k);
        if (!lua_istable(L, -1)) luaL_error(L, "Illegal argument at array index %d: Expected table, recieved %s", k, luaL_typename(L, -1));
        lua_rawgeti(L, -1, 1);
        lua_rawgeti(L, -2, 2);
        int n1 = 0, n2 = 0;
        double* v1 = toVec(L, -2, &n1);
        double* v2 = toVec(L, -1, &n2);
        if (!v1 || n1 != 3) luaL_error(L, "Illegal argument to AABB at array index %d at index 1: Expected Vector3, recieved %s", k, figuraTypeName(L, -2).c_str());
        if (!v2 || n2 != 3) luaL_error(L, "Illegal argument to AABB at array index %d at index 2: Expected Vector3, recieved %s", k, figuraTypeName(L, -1).c_str());
        Vec3 mn{std::min(v1[0], v2[0]), std::min(v1[1], v2[1]), std::min(v1[2], v2[2])};
        Vec3 mx{std::max(v1[0], v2[0]), std::max(v1[1], v2[1]), std::max(v1[2], v2[2])};
        lua_pop(L, 3);
        Vec3        hp;
        char const* face = nullptr;
        bool        clipped = clipAABB(mn, mx, a, b, hp, face);
        bool        inside  = a.x >= mn.x && a.x < mx.x && a.y >= mn.y && a.y < mx.y && a.z >= mn.z && a.z < mx.z;
        if (inside) {
            if (bestD >= 0) { index = k; bestPos = clipped ? hp : a; bestFace = clipped ? face : nullptr; bestD = 0; }
        } else if (clipped) {
            Vec3   dd = hp - a;
            double e  = dd.x * dd.x + dd.y * dd.y + dd.z * dd.z;
            if (e < bestD || bestD == 0) { index = k; bestPos = hp; bestFace = face; bestD = e; }
        }
    }
    if (index < 0) return pushNil(L);
    lua_rawgeti(L, t, index);
    pushVec3(L, bestPos);
    if (bestFace) lua_pushstring(L, bestFace); else lua_pushnil(L);
    lua_pushinteger(L, index);
    return 4;
}
int rcToString(lua_State* L) { return pushS(L, "RaycastAPI"); }

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
int aStore(lua_State* L) {
    std::string k = checkStr(L, 2, "store");
    lua_getfield(L, LUA_REGISTRYINDEX, "fb.store");
    lua_pushstring(L, k.c_str());
    lua_pushvalue(L, 3);
    lua_settable(L, -3);
    return ret1(L);
}
int aGetUUID(lua_State* L) { return pushS(L, P(L).uuid); }
int aGetCurrentInstructions(lua_State* L) { return pushI(L, av(L)->instructionsUsed); }
int aGetColor(lua_State* L) { auto& c = av(L)->color; if (c.empty()) return pushNil(L); return pushS(L, c); }
int aSetColor(lua_State* L) {
    bool gNum = isNil(L, 3) || lua_type(L, 3) == LUA_TNUMBER;
    bool noBadge = isNil(L, 5) || std::string(luaL_checkstring(L, 5)).empty();
    if (gNum && noBadge) av(L)->color = rgbToHex(oneArgVec(L, 2, "setColor", 1));
    else (void)oneArgVec(L, 2, "setColor", 1); // badge colors: no badges on Bedrock
    return ret1(L);
}
int aGetBadges(lua_State* L) { return pushS(L, ""); }
int aGetVersion(lua_State* L) { return pushS(L, "0.1.6"); }
int aGetAuthors(lua_State* L) { return pushS(L, av(L)->author); }
int aGetName(lua_State* L) { return pushS(L, av(L)->name); }
int aGetEntityName(lua_State* L) { return pushS(L, P(L).name); }
int aGetSize(lua_State* L) {
    std::error_code ec;
    auto n = std::filesystem::file_size(av(L)->dir / "figura_avatar.json", ec);
    return pushN(L, ec ? 0.0 : double(n));
}
int aHasTexture(lua_State* L) { return pushB(L, !av(L)->textureFiles.empty()); }
int aHasScriptError(lua_State* L) { return pushB(L, av(L)->scriptError); }
int aGetPermissionLevel(lua_State* L) { return pushS(L, "MAX"); }
int aIntMax(lua_State* L) { return pushI(L, kIntMax); }
int aZero(lua_State* L) { return pushI(L, 0); }
int aTextureSize(lua_State* L) { return pushI(L, 2048); }
int aMaxBuffers(lua_State* L) { return pushI(L, 32); }
int aVolume(lua_State* L) { return pushI(L, 100); }
int aAnimationCount(lua_State* L) {
    int n = 0;
    for (auto const& an : av(L)->animations) if (an && an->playState != PlayState::Stopped) n++;
    return pushI(L, n);
}
int aToString(lua_State* L) { return pushS(L, "AvatarAPI"); }

void registerSingleton(lua_State* L, char const* global, TypeDef const& d, uint32_t& tid, void* anchor) {
    tid = registerType(L, d);
    pushRef(L, anchor, tid);
    lua_setglobal(L, global);
}
std::vector<luaL_Reg> cat(std::vector<luaL_Reg> a, std::vector<luaL_Reg> const& b) { a.insert(a.end(), b.begin(), b.end()); return a; }
} // namespace

void openEntityApis(Avatar& a) {
    lua_State* L = a.L;
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "fb.store");
    hs(L)          = HostState{};
    liveSounds()[L].clear();

    { // ItemStack
        TypeDef d;
        d.name    = "ItemStack";
        d.methods = {{"getID", itGetID}, {"getTag", itGetTag}, {"getCount", itGetCount}, {"getDamage", itGetDamage},
                     {"getPopTime", itGetPopTime}, {"hasGlint", itHasGlint}, {"getTags", itGetTags},
                     {"isBlockItem", itIsBlockItem}, {"isFood", itIsFood}, {"getUseAction", itGetUseAction},
                     {"getName", itGetName}, {"getMaxCount", itGetMaxCount}, {"getRarity", itGetRarity},
                     {"isEnchantable", itIsEnchantable}, {"getMaxDamage", itGetMaxDamage}, {"isDamageable", itIsDamageable},
                     {"isStackable", itIsStackable}, {"getRepairCost", itGetRepairCost}, {"getUseDuration", itGetUseDuration},
                     {"toStackString", itToStackString}, {"isArmor", itIsArmor}, {"isTool", itIsTool},
                     {"getEquipmentSlot", itGetEquipmentSlot}, {"copy", itCopy}, {"getBlockstate", itGetBlockstate},
                     {"__eq", itEq}, {"__index", itIndex}};
        d.index    = itIndex;
        d.tostring = itToString;
        d.meta     = {{"__gc", gcUD<ItemUD>}, {"__eq", itEq}};
        tItem      = registerType(L, d);
    }
    { // BlockState
        TypeDef d;
        d.name    = "BlockState";
        d.methods = {{"getID", blGetID}, {"getProperties", blGetProperties}, {"getPos", blGetPos}, {"setPos", blSetPos},
                     {"pos", blSetPos}, {"isTranslucent", blIsTranslucent}, {"getOpacity", blGetOpacity},
                     {"getMapColor", blGetMapColor}, {"isSolidBlock", blIsSolidBlock}, {"isFullCube", blIsFullCube},
                     {"hasEmissiveLighting", blHasEmissiveLighting}, {"getHardness", blGetHardness},
                     {"getComparatorOutput", blGetComparatorOutput}, {"hasBlockEntity", blHasBlockEntity},
                     {"isOpaque", blIsOpaque}, {"emitsRedstonePower", blEmitsRedstonePower}, {"getLuminance", blGetLuminance},
                     {"getFriction", blGetFriction}, {"getVelocityMultiplier", blGetVelocityMultiplier},
                     {"getJumpVelocityMultiplier", blGetJumpVelocityMultiplier}, {"getBlastResistance", blGetBlastResistance},
                     {"asItem", blAsItem}, {"getTags", blGetTags}, {"hasCollision", blHasCollision},
                     {"getCollisionShape", blGetCollisionShape}, {"getOutlineShape", blGetOutlineShape},
                     {"getSounds", blGetSounds}, {"getFluidTags", blGetFluidTags}, {"getEntityData", blGetEntityData},
                     {"toStateString", blToStateString}, {"getTextures", blGetTextures}, {"isAir", blIsAir},
                     {"__eq", blEq}, {"__index", blIndex}};
        d.index    = blIndex;
        d.tostring = blToString;
        d.meta     = {{"__gc", gcUD<BlockUD>}, {"__eq", blEq}};
        tBlock     = registerType(L, d);
    }
    { // Biome
        TypeDef d;
        d.name    = "Biome";
        d.methods = {{"getPos", bioGetPos}, {"setPos", bioSetPos}, {"pos", bioSetPos}, {"getTags", bioGetTags},
                     {"getTemperature", bioGetTemperature}, {"getPrecipitation", bioGetPrecipitation},
                     {"getSkyColor", bioGetSkyColor}, {"getFoliageColor", bioGetFoliageColor},
                     {"getGrassColor", bioGetGrassColor}, {"getFogColor", bioGetFogColor}, {"getWaterColor", bioGetWaterColor},
                     {"getWaterFogColor", bioGetWaterFogColor}, {"getDownfall", bioGetDownfall}, {"isHot", bioIsHot},
                     {"isCold", bioIsCold}, {"__eq", bioEq}, {"__index", bioIndex}};
        d.index    = bioIndex;
        d.tostring = bioToString;
        d.meta     = {{"__gc", gcUD<BiomeUD>}, {"__eq", bioEq}};
        tBiome     = registerType(L, d);
    }
    { // entities: EntityAPI < LivingEntityAPI < PlayerAPI < ViewerAPI
        std::vector<luaL_Reg> em = {
            {"isLoaded", enIsLoaded}, {"getPos", enGetPos}, {"getRot", enGetRot}, {"getUUID", enGetUUID}, {"getType", enGetType},
            {"getVelocity", enGetVelocity}, {"getLookDir", enGetLookDir}, {"getFrozenTicks", enGetFrozenTicks},
            {"getMaxAir", enGetMaxAir}, {"getDimensionName", enGetDimensionName}, {"getPose", enGetPose},
            {"getVehicle", enGetVehicle}, {"isOnGround", enIsOnGround}, {"getEyeHeight", enGetEyeHeight},
            {"getBoundingBox", enGetBoundingBox}, {"getName", enGetName}, {"isWet", enIsWet}, {"isInWater", enIsInWater},
            {"isUnderwater", enIsUnderwater}, {"isInLava", enIsInLava}, {"isInRain", enIsInRain}, {"hasAvatar", enHasAvatar},
            {"isSprinting", enIsSprinting}, {"getEyeY", enGetEyeY}, {"isGlowing", enIsGlowing}, {"isInvisible", enIsInvisible},
            {"isSilent", enIsSilent}, {"isSneaking", enIsSneaking}, {"isCrouching", enIsCrouching}, {"isMoving", enIsMoving},
            {"isFalling", enIsFalling}, {"getItem", enGetItem}, {"getNbt", enGetNbt}, {"isOnFire", enIsOnFire},
            {"isAlive", enIsAlive}, {"getPermissionLevel", enGetPermissionLevel}, {"getPassengers", enGetPassengers},
            {"getControllingPassenger", enGetControllingPassenger}, {"getControlledVehicle", enGetControlledVehicle},
            {"hasContainer", enHasContainer}, {"hasInventory", enHasInventory}, {"getTargetedBlock", enGetTargetedBlock},
            {"getTargetedEntity", enGetTargetedEntity}, {"getNearestEntity", enGetNearestEntity},
            {"getVariable", enGetVariable}, {"isLiving", enIsLiving}, {"isPlayer", enIsPlayer}, {"__eq", enEq},
            {"__tostring", enToString}};
        std::vector<luaL_Reg> lm = cat(em, {
            {"getBodyYaw", leGetBodyYaw}, {"getHeldItem", leGetHeldItem}, {"getActiveItem", leGetActiveItem},
            {"getActiveItemTime", leGetActiveItemTime}, {"getHealth", leGetHealth}, {"getMaxHealth", leGetMaxHealth},
            {"getArmor", leGetArmor}, {"getDeathTime", leGetDeathTime}, {"getArrowCount", leGetArrowCount},
            {"getStingerCount", leGetStingerCount}, {"isLeftHanded", leIsLeftHanded}, {"isUsingItem", leIsUsingItem},
            {"getActiveHand", leGetActiveHand}, {"isClimbing", leIsClimbing}, {"getSwingTime", leGetSwingTime},
            {"isSwingingArm", leIsSwingingArm}, {"getSwingArm", leGetSwingArm}, {"getSwingDuration", leGetSwingDuration},
            {"getAbsorptionAmount", leGetAbsorptionAmount}, {"isSensitiveToWater", leIsSensitiveToWater},
            {"getEntityCategory", leGetEntityCategory}, {"isGliding", leIsGliding}, {"isBlocking", leIsBlocking},
            {"isVisuallySwimming", leIsVisuallySwimming}, {"riptideSpinning", leRiptideSpinning}});
        std::vector<luaL_Reg> pm = cat(lm, {
            {"getFood", plGetFood}, {"getSaturation", plGetSaturation}, {"getExhaustion", plGetExhaustion},
            {"getExperienceProgress", plGetExperienceProgress}, {"getExperienceLevel", plGetExperienceLevel},
            {"getModelType", plGetModelType}, {"getGamemode", plGetGamemode}, {"hasCape", plHasCape}, {"hasSkin", plHasSkin},
            {"isSkinLayerVisible", plIsSkinLayerVisible}, {"isFishing", plIsFishing},
            {"getChargedAttackDelay", plGetChargedAttackDelay}, {"getShoulderEntity", plGetShoulderEntity},
            {"getTeamInfo", plGetTeamInfo}, {"getCooldownPercent", plGetCooldownPercent}});
        std::vector<luaL_Reg> vm = cat(pm, {
            {"getAttackCharge", viGetAttackCharge}, {"isJumping", viIsJumping}, {"isFlying", viIsFlying},
            {"getReachDistance", viGetReachDistance}, {"getAir", viGetAir}, {"getStatusEffects", viGetStatusEffects}});
        auto reg = [&](char const* name, std::vector<luaL_Reg> const& m, uint32_t& t) {
            TypeDef d;
            d.name     = name;
            d.methods  = m;
            d.tostring = enToString;
            d.meta     = {{"__gc", gcUD<EntUD>}, {"__eq", enEq}};
            t          = registerType(L, d);
        };
        reg("EntityAPI", em, tEntity);
        reg("LivingEntityAPI", lm, tLiving);
        reg("PlayerAPI", pm, tPlayer);
        reg("ViewerAPI", vm, tViewer);
        TypeDef n;
        n.name     = "NullEntity";
        n.methods  = {{"isLoaded", retFalse}, {"__index", nullIndex}};
        n.index    = nullIndex;
        n.tostring = nullToString;
        tNullEntity = registerType(L, n);

        EntUD self;
        self.kind = 2;
        self.self = true;
        newUD(L, tPlayer, EntUD(self));
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "fb.player");
        lua_pushvalue(L, -1);
        lua_setglobal(L, "player");
        lua_setglobal(L, "user");
        self.kind = 3;
        newUD(L, tViewer, EntUD(self));
        lua_setfield(L, LUA_REGISTRYINDEX, "fb.viewer");
    }
    { // world (static functions)
        TypeDef d;
        d.name    = "WorldAPI";
        d.methods = {{"getBiome", stat<wGetBiome, &tWorld>}, {"getBlockState", stat<wGetBlockState, &tWorld>},
                     {"isChunkLoaded", stat<wIsChunkLoaded, &tWorld>}, {"getBlocks", stat<wGetBlocks, &tWorld>},
                     {"getMapData", stat<wGetMapData, &tWorld>}, {"getRedstonePower", stat<wGetRedstonePower, &tWorld>},
                     {"getStrongRedstonePower", stat<wGetStrongRedstonePower, &tWorld>}, {"getTime", stat<wGetTime, &tWorld>},
                     {"getTimeOfDay", stat<wGetTimeOfDay, &tWorld>}, {"getDayTime", stat<wGetDayTime, &tWorld>},
                     {"getDay", stat<wGetDay, &tWorld>}, {"getMoonPhase", stat<wGetMoonPhase, &tWorld>},
                     {"getRainGradient", stat<wGetRainGradient, &tWorld>}, {"isThundering", stat<wIsThundering, &tWorld>},
                     {"getLightLevel", stat<wGetLightLevel, &tWorld>}, {"getSkyLightLevel", stat<wGetSkyLightLevel, &tWorld>},
                     {"getBlockLightLevel", stat<wGetBlockLightLevel, &tWorld>}, {"getHeight", stat<wGetHeight, &tWorld>},
                     {"isOpenSky", stat<wIsOpenSky, &tWorld>}, {"getDimension", stat<wGetDimension, &tWorld>},
                     {"getPlayers", stat<wGetPlayers, &tWorld>}, {"getEntities", stat<wGetEntities, &tWorld>},
                     {"getEntity", stat<wGetEntity, &tWorld>}, {"avatarVars", stat<wAvatarVars, &tWorld>},
                     {"newBlock", stat<wNewBlock, &tWorld>}, {"newItem", stat<wNewItem, &tWorld>},
                     {"exists", stat<wExists, &tWorld>}, {"getBuildHeight", stat<wGetBuildHeight, &tWorld>},
                     {"getSpawnPoint", stat<wGetSpawnPoint, &tWorld>}};
        d.tostring = wToString;
        static int anchor;
        registerSingleton(L, "world", d, tWorld, &anchor);
    }
    { // client (static functions)
        TypeDef d;
        d.name = "ClientAPI";
#define C(n, f) {n, stat<f, &tClient>}
        d.methods = {C("getFPS", cGetFPS), C("getFPSString", cGetFPSString), C("isPaused", cIsPaused), C("getVersion", cGetVersion),
                     C("getVersionName", cGetVersion), C("isSnapshot", cIsSnapshot), C("getClientBrand", cGetClientBrand),
                     C("getServerBrand", cGetServerBrand), C("getChunkStatistics", cGetChunkStatistics),
                     C("getEntityStatistics", cGetEntityStatistics), C("getSoundStatistics", cGetSoundStatistics),
                     C("getEntityCount", cGetEntityCount), C("getParticleCount", cGetParticleCount),
                     C("getCurrentEffect", cGetCurrentEffect), C("getJavaVersion", cGetJavaVersion),
                     C("getUsedMemory", cGetUsedMemory), C("getMaxMemory", cGetMaxMemory),
                     C("getAllocatedMemory", cGetAllocatedMemory), C("isWindowFocused", cIsWindowFocused),
                     C("isHudEnabled", cIsHudEnabled), C("isDebugOverlayEnabled", cIsDebugOverlayEnabled),
                     C("getWindowSize", cGetWindowSize), C("getFOV", cGetFOV), C("getSystemTime", cGetSystemTime),
                     C("getMousePos", cGetMousePos), C("getScaledWindowSize", cGetScaledWindowSize),
                     C("getGuiScale", cGetGuiScale), C("getCameraPos", cGetCameraPos), C("getCameraRot", cGetCameraRot),
                     C("getCameraDir", cGetCameraDir), C("getTextWidth", cGetTextWidth), C("getTextHeight", cGetTextHeight),
                     C("getTextDimensions", cGetTextDimensions), C("getActiveLang", cGetActiveLang),
                     C("isModLoaded", cIsModLoaded), C("hasShaderPackMod", cHasShaderPackMod),
                     C("hasShaderPack", cHasShaderPack), C("getShaderPackName", cGetShaderPackName),
                     C("hasResource", cHasResource), C("getActiveResourcePacks", cGetActiveResourcePacks),
                     C("getFiguraVersion", cGetFiguraVersion), C("compareVersions", cCompareVersions),
                     C("generateUUID", cGenerateUUID), C("intUUIDToString", cIntUUIDToString),
                     C("uuidToIntArray", cUuidToIntArray), C("getViewer", cGetViewer), C("getCameraEntity", cGetCameraEntity),
                     C("isIntegratedServer", cIsIntegratedServer), C("getServerData", cGetServerData), C("getDate", cGetDate),
                     C("getFrameTime", cGetFrameTime), C("getActionbar", cGetActionbar), C("getTitle", cGetTitle),
                     C("getSubtitle", cGetSubtitle), C("getScoreboard", cGetScoreboard), C("listAtlases", cListAtlases),
                     C("getAtlas", cGetAtlas), C("getTabList", cGetTabList), C("getTranslatedString", cGetTranslatedString),
                     C("getRegistry", cGetRegistry), C("getEnum", cGetEnum)};
#undef C
        d.tostring = cToString;
        static int anchor;
        registerSingleton(L, "client", d, tClient, &anchor);
    }
    { // host
        TypeDef d;
        d.name    = "HostAPI";
        d.methods = {{"isHost", hIsHost}, {"isCursorUnlocked", hIsCursorUnlocked}, {"setUnlockCursor", hSetUnlockCursor},
                     {"setTitleTimes", hSetTitleTimes}, {"titleTimes", hSetTitleTimes}, {"clearTitle", hClearTitle},
                     {"setTitle", hSetTitle}, {"title", hSetTitle}, {"setSubtitle", hSetSubtitle}, {"subtitle", hSetSubtitle},
                     {"setActionbar", hSetActionbar}, {"actionbar", hSetActionbar}, {"sendChatMessage", hSendChatMessage},
                     {"sendChatCommand", hSendChatCommand}, {"appendChatHistory", hAppendChatHistory},
                     {"getChatMessage", hGetChatMessage}, {"setChatMessage", hSetChatMessage}, {"swingArm", hSwingArm},
                     {"getSlot", hGetSlot}, {"setSlot", hSetSlot}, {"setBadge", hSetBadge}, {"badge", hSetBadge},
                     {"getChatColor", hGetChatColor}, {"setChatColor", hSetChatColor}, {"chatColor", hSetChatColor},
                     {"getChatText", hGetChatText}, {"setChatText", hSetChatText}, {"chatText", hSetChatText},
                     {"getScreen", hGetScreen}, {"getScreenSlotCount", hGetScreenSlotCount}, {"getScreenSlot", hGetScreenSlot},
                     {"isChatOpen", hIsChatOpen}, {"isContainerOpen", hIsContainerOpen}, {"screenshot", hScreenshot},
                     {"isAvatarUploaded", hIsAvatarUploaded}, {"getStatusEffects", hGetStatusEffects},
                     {"getClipboard", hGetClipboard}, {"setClipboard", hSetClipboard}, {"clipboard", hSetClipboard},
                     {"getAttackCharge", hGetAttackCharge}, {"isJumping", hIsJumping}, {"isFlying", hIsFlying},
                     {"getReachDistance", hGetReachDistance}, {"getAir", hGetAir}, {"getPickBlock", hGetPickBlock},
                     {"getPickEntity", hGetPickEntity}, {"isChatVerified", hIsChatVerified}, {"writeToLog", hWriteToLog},
                     {"warnToLog", hWarnToLog}, {"__index", hIndex}, {"__newindex", hNewIndex}};
        d.index    = hIndex;
        d.newindex = hNewIndex;
        d.tostring = hToString;
        static int anchor;
        registerSingleton(L, "host", d, tHost, &anchor);
    }
    { // renderer
        a.render = Avatar::RenderSettings{};
        TypeDef d;
        d.name    = "RendererAPI";
        d.methods = {{"shouldRenderFire", rShouldRenderFire}, {"setRenderFire", rSetRenderFire},
                     {"shouldRenderVehicle", rShouldRenderVehicle}, {"setRenderVehicle", rSetRenderVehicle},
                     {"shouldRenderCrosshair", rShouldRenderCrosshair}, {"setRenderCrosshair", rSetRenderCrosshair},
                     {"shouldForcePaperdoll", rShouldForcePaperdoll}, {"setForcePaperdoll", rSetForcePaperdoll},
                     {"shouldRenderHUD", rShouldRenderHUD}, {"setRenderHUD", rSetRenderHUD},
                     {"isUpsideDown", rIsUpsideDown}, {"setUpsideDown", rSetUpsideDown}, {"upsideDown", rSetUpsideDown},
                     {"setShadowRadius", rSetShadowRadius}, {"shadowRadius", rSetShadowRadius},
                     {"getShadowRadius", rGetShadowRadius}, {"isFirstPerson", rIsFirstPerson},
                     {"isCameraBackwards", rIsCameraBackwards}, {"getCameraPos", rGetCameraPos},
                     {"setCameraPos", rSetCameraPos}, {"cameraPos", rSetCameraPos}, {"getCameraPivot", rGetCameraPivot},
                     {"setCameraPivot", rSetCameraPivot}, {"cameraPivot", rSetCameraPivot},
                     {"getCameraOffsetPivot", rGetCameraOffsetPivot}, {"setOffsetCameraPivot", rSetOffsetCameraPivot},
                     {"offsetCameraPivot", rSetOffsetCameraPivot}, {"getCameraRot", rGetCameraRot},
                     {"setCameraRot", rSetCameraRot}, {"cameraRot", rSetCameraRot}, {"getCameraOffsetRot", rGetCameraOffsetRot},
                     {"setOffsetCameraRot", rSetOffsetCameraRot}, {"offsetCameraRot", rSetOffsetCameraRot},
                     {"getCameraMatrix", rGetCameraMatrix}, {"setCameraMatrix", rSetCameraMatrix},
                     {"cameraMatrix", rSetCameraMatrix}, {"getCameraNormal", rGetCameraNormal},
                     {"setCameraNormal", rSetCameraNormal}, {"cameraNormal", rSetCameraNormal},
                     {"setPostEffect", rSetPostEffect}, {"postEffect", rSetPostEffect}, {"getFOV", rGetFOV},
                     {"setFOV", rSetFOV}, {"fov", rSetFOV}, {"getCrosshairOffset", rGetCrosshairOffset},
                     {"setCrosshairOffset", rSetCrosshairOffset}, {"crosshairOffset", rSetCrosshairOffset},
                     {"getOutlineColor", rGetOutlineColor}, {"setOutlineColor", rSetOutlineColor},
                     {"outlineColor", rSetOutlineColor}, {"getPrimaryFireTexture", rGetPrimaryFireTexture},
                     {"getSecondaryFireTexture", rGetSecondaryFireTexture}, {"setPrimaryFireTexture", rSetPrimaryFireTexture},
                     {"setSecondaryFireTexture", rSetSecondaryFireTexture}, {"primaryFireTexture", rSetPrimaryFireTexture},
                     {"secondaryFireTexture", rSetSecondaryFireTexture}, {"setRenderLeftArm", rSetRenderLeftArm},
                     {"renderLeftArm", rSetRenderLeftArm}, {"getRenderLeftArm", rGetRenderLeftArm},
                     {"setRenderRightArm", rSetRenderRightArm}, {"renderRightArm", rSetRenderRightArm},
                     {"getRenderRightArm", rGetRenderRightArm}, {"setEyeOffset", rSetEyeOffset}, {"eyeOffset", rSetEyeOffset},
                     {"getEyeOffset", rGetEyeOffset}, {"setBlockOutlineColor", rSetBlockOutlineColor},
                     {"getBlockOutlineColor", rGetBlockOutlineColor}, {"blockOutlineColor", rSetBlockOutlineColor},
                     {"setRootRotationAllowed", rSetRootRotationAllowed}, {"rootRotationAllowed", rSetRootRotationAllowed},
                     {"getRootRotationAllowed", rGetRootRotationAllowed}, {"__index", rIndex}, {"__newindex", rNewIndex}};
        d.index    = rIndex;
        d.newindex = rNewIndex;
        d.tostring = rToString;
        static int anchor;
        registerSingleton(L, "renderer", d, tRenderer, &anchor);
    }
    { // vanilla_model
        a.vanillaOverrides.clear();
        std::vector<luaL_Reg> part = {{"getVisible", vpGetVisible}, {"setVisible", vpSetVisible}, {"visible", vpSetVisible},
                                      {"getPos", vpGetPos}, {"setPos", vpSetPos}, {"pos", vpSetPos}, {"getRot", vpGetRot},
                                      {"setRot", vpSetRot}, {"rot", vpSetRot}, {"getOffsetRot", vpGetOffsetRot},
                                      {"setOffsetRot", vpSetOffsetRot}, {"offsetRot", vpSetOffsetRot}, {"getScale", vpGetScale},
                                      {"setScale", vpSetScale}, {"scale", vpSetScale}, {"getOffsetScale", vpGetOffsetScale},
                                      {"setOffsetScale", vpSetOffsetScale}, {"offsetScale", vpSetOffsetScale}};
        TypeDef d;
        d.name     = "VanillaModelPart";
        d.methods  = cat(part, {{"getOriginVisible", vpGetOriginVisible}, {"getOriginRot", vpGetOriginRot},
                                {"getOriginPos", vpGetOriginPos}, {"getOriginScale", vpGetOriginScale}});
        d.tostring = vpToString;
        tVanillaPart = registerType(L, d);
        TypeDef g;
        g.name     = "VanillaModelGroup";
        g.methods  = cat(part, {{"__index", vgIndex}});
        g.index    = vgIndex;
        g.tostring = vpToString;
        tVanillaGroup = registerType(L, g);
        TypeDef abstractPart; // VanillaPart: only published as a metatable name (abstract in Figura)
        abstractPart.name    = "VanillaPart";
        abstractPart.methods = part;
        registerType(L, abstractPart);

        auto& st = vs(L);
        st       = VanillaState{};
        using PT = ParentType;
        struct Def { char const* n; PT pt; int prov; std::vector<char const*> ch; };
        std::vector<Def> defs = {
            {"HEAD", PT::Head, 1, {}}, {"BODY", PT::Body, 2, {}}, {"LEFT_ARM", PT::LeftArm, 3, {}}, {"RIGHT_ARM", PT::RightArm, 4, {}},
            {"LEFT_LEG", PT::LeftLeg, 5, {}}, {"RIGHT_LEG", PT::RightLeg, 6, {}}, {"HAT", PT::Head, 1, {}}, {"JACKET", PT::Body, 2, {}},
            {"LEFT_SLEEVE", PT::LeftArm, 3, {}}, {"RIGHT_SLEEVE", PT::RightArm, 4, {}}, {"LEFT_PANTS", PT::LeftLeg, 5, {}},
            {"RIGHT_PANTS", PT::RightLeg, 6, {}}, {"CAPE_MODEL", PT::Cape, 7, {}}, {"FAKE_CAPE", PT::Cape, 7, {}},
            {"HELMET_ITEM", PT::Head, 0, {}}, {"HELMET_HEAD", PT::Head, 1, {}}, {"HELMET_HAT", PT::Head, 1, {}},
            {"CHESTPLATE_BODY", PT::Body, 2, {}}, {"CHESTPLATE_LEFT_ARM", PT::LeftArm, 3, {}}, {"CHESTPLATE_RIGHT_ARM", PT::RightArm, 4, {}},
            {"LEGGINGS_BODY", PT::Body, 2, {}}, {"LEGGINGS_LEFT_LEG", PT::LeftLeg, 5, {}}, {"LEGGINGS_RIGHT_LEG", PT::RightLeg, 6, {}},
            {"BOOTS_LEFT_LEG", PT::LeftLeg, 5, {}}, {"BOOTS_RIGHT_LEG", PT::RightLeg, 6, {}},
            {"LEFT_ELYTRA", PT::LeftElytra, 8, {}}, {"RIGHT_ELYTRA", PT::RightElytra, 9, {}},
            {"LEFT_ITEM", PT::LeftArm, 0, {}}, {"RIGHT_ITEM", PT::RightArm, 0, {}},
            {"LEFT_PARROT", PT::Body, 0, {}}, {"RIGHT_PARROT", PT::Body, 0, {}},
            {"INNER_LAYER", PT::None, -1, {"HEAD", "BODY", "LEFT_ARM", "RIGHT_ARM", "LEFT_LEG", "RIGHT_LEG"}},
            {"OUTER_LAYER", PT::None, -1, {"HAT", "JACKET", "LEFT_SLEEVE", "RIGHT_SLEEVE", "LEFT_PANTS", "RIGHT_PANTS"}},
            {"PLAYER", PT::None, -1, {"INNER_LAYER", "OUTER_LAYER"}},
            {"CAPE", PT::None, -1, {"CAPE_MODEL", "FAKE_CAPE"}},
            {"HELMET", PT::None, -1, {"HELMET_ITEM", "HELMET_HEAD", "HELMET_HAT"}},
            {"CHESTPLATE", PT::None, -1, {"CHESTPLATE_BODY", "CHESTPLATE_LEFT_ARM", "CHESTPLATE_RIGHT_ARM"}},
            {"LEGGINGS", PT::None, -1, {"LEGGINGS_BODY", "LEGGINGS_LEFT_LEG", "LEGGINGS_RIGHT_LEG"}},
            {"BOOTS", PT::None, -1, {"BOOTS_LEFT_LEG", "BOOTS_RIGHT_LEG"}},
            {"ARMOR", PT::None, -1, {"HELMET", "CHESTPLATE", "LEGGINGS", "BOOTS"}},
            {"ELYTRA", PT::None, -1, {"LEFT_ELYTRA", "RIGHT_ELYTRA"}},
            {"HELD_ITEMS", PT::None, -1, {"LEFT_ITEM", "RIGHT_ITEM"}},
            {"PARROTS", PT::None, -1, {"LEFT_PARROT", "RIGHT_PARROT"}},
            {"ALL", PT::None, -1, {"PLAYER", "CAPE", "ARMOR", "ELYTRA", "HELD_ITEMS", "PARROTS"}}};
        for (auto const& df : defs) {
            auto p      = std::make_unique<VPart>();
            p->name     = df.n;
            p->group    = df.prov < 0;
            p->parent   = df.pt;
            p->provider = std::max(df.prov, 0);
            for (auto c : df.ch) p->children.emplace_back(c);
            st.byName[p->name] = p.get();
            st.parts.push_back(std::move(p));
        }
        TypeDef m;
        m.name     = "VanillaModelAPI";
        m.methods  = {{"__index", vmIndex}};
        m.index    = vmIndex;
        m.tostring = vmToString;
        static int anchor;
        registerSingleton(L, "vanilla_model", m, tVanillaAPI, &anchor);
    }
    { // nameplate
        a.nameplate = Avatar::NameplateSettings{};
        TypeDef c;
        c.name     = "NameplateCustomization";
        c.methods  = {{"getText", npGetText}, {"setText", npSetText}};
        c.tostring = npToString;
        tNameplate = registerType(L, c);
        TypeDef e;
        e.name    = "EntityNameplateCustomization";
        e.methods = {{"getText", npGetText}, {"setText", npSetText}, {"getPivot", npGetPivot}, {"setPivot", npSetPivot},
                     {"pivot", npSetPivot}, {"getPos", npGetPos}, {"setPos", npSetPos}, {"pos", npSetPos},
                     {"getScale", npGetScale}, {"setScale", npSetScale}, {"scale", npSetScale},
                     {"getBackgroundColor", npGetBackgroundColor}, {"setBackgroundColor", npSetBackgroundColor},
                     {"backgroundColor", npSetBackgroundColor}, {"setOutlineColor", npSetOutlineColor},
                     {"outlineColor", npSetOutlineColor}, {"getLight", npGetLight}, {"setLight", npSetLight},
                     {"light", npSetLight}, {"isVisible", npIsVisible}, {"setVisible", npSetVisible}, {"visible", npSetVisible},
                     {"hasOutline", npHasOutline}, {"setOutline", npSetOutline}, {"outline", npSetOutline},
                     {"hasShadow", npHasShadow}, {"setShadow", npSetShadow}, {"shadow", npSetShadow}};
        e.tostring       = npToString;
        tEntityNameplate = registerType(L, e);
        TypeDef g;
        g.name          = "NameplateCustomizationGroup";
        g.methods       = {{"setText", npSetText}};
        g.tostring      = npToString;
        tNameplateGroup = registerType(L, g);
        TypeDef n;
        n.name     = "NameplateAPI";
        n.methods  = {{"__index", nmIndex}};
        n.index    = nmIndex;
        n.tostring = nmToString;
        static int anchor;
        registerSingleton(L, "nameplate", n, tNameplateAPI, &anchor);
    }
    { // sounds
        TypeDef s;
        s.name    = "Sound";
        s.methods = {{"play", sPlay}, {"isPlaying", sIsPlaying}, {"pause", sPause}, {"stop", sStop}, {"getPos", sGetPos},
                     {"setPos", sSetPos}, {"pos", sSetPos}, {"getVolume", sGetVolume}, {"setVolume", sSetVolume},
                     {"volume", sSetVolume}, {"getAttenuation", sGetAttenuation}, {"setAttenuation", sSetAttenuation},
                     {"attenuation", sSetAttenuation}, {"getPitch", sGetPitch}, {"setPitch", sSetPitch}, {"pitch", sSetPitch},
                     {"isLooping", sIsLooping}, {"setLoop", sSetLoop}, {"loop", sSetLoop}, {"getSubtitle", sGetSubtitle},
                     {"setSubtitle", sSetSubtitle}, {"subtitle", sSetSubtitle}};
        s.tostring = sToString;
        s.meta     = {{"__gc", gcUD<SoundUD>}};
        tSound     = registerType(L, s);
        TypeDef d;
        d.name     = "SoundAPI";
        d.methods  = {{"playSound", sndPlaySound}, {"stopSound", sndStopSound}, {"newSound", sndNewSound},
                      {"isPresent", sndIsPresent}, {"getCustomSounds", sndGetCustomSounds}, {"__index", sndIndex}};
        d.index    = sndIndex;
        d.tostring = sndToString;
        static int anchor;
        registerSingleton(L, "sounds", d, tSounds, &anchor);
    }
    { // particles
        TypeDef p;
        p.name    = "Particle";
        p.methods = {{"spawn", ptSpawn}, {"remove", ptRemove}, {"isAlive", ptIsAlive}, {"getPos", ptGetPos}, {"setPos", ptSetPos},
                     {"pos", ptSetPos}, {"getVelocity", ptGetVelocity}, {"setVelocity", ptSetVelocity},
                     {"velocity", ptSetVelocity}, {"getColor", ptGetColor}, {"setColor", ptSetColor}, {"color", ptSetColor},
                     {"getLifetime", ptGetLifetime}, {"setLifetime", ptSetLifetime}, {"lifetime", ptSetLifetime},
                     {"getPower", ptGetPower}, {"setPower", ptSetPower}, {"power", ptSetPower}, {"getScale", ptGetScale},
                     {"getSize", ptGetScale}, {"setScale", ptSetScale}, {"scale", ptSetScale}, {"setSize", ptSetScale},
                     {"size", ptSetScale}, {"getGravity", ptGetGravity}, {"setGravity", ptSetGravity},
                     {"gravity", ptSetGravity}, {"hasPhysics", ptHasPhysics}, {"setPhysics", ptSetPhysics},
                     {"physics", ptSetPhysics}};
        p.tostring = ptToString;
        p.meta     = {{"__gc", gcUD<ParticleUD>}};
        tParticle  = registerType(L, p);
        TypeDef d;
        d.name     = "ParticleAPI";
        d.methods  = {{"newParticle", paNewParticle}, {"removeParticles", paRemoveParticles}, {"isPresent", paIsPresent},
                      {"__index", paIndex}};
        d.index    = paIndex;
        d.tostring = paToString;
        static int anchor;
        registerSingleton(L, "particles", d, tParticles, &anchor);
    }
    { // raycast
        TypeDef d;
        d.name     = "RaycastAPI";
        d.methods  = {{"block", stat<rcBlock, &tRaycast>}, {"entity", stat<rcEntity, &tRaycast>}, {"aabb", stat<rcAabb, &tRaycast>}};
        d.tostring = rcToString;
        static int anchor;
        registerSingleton(L, "raycast", d, tRaycast, &anchor);
    }
    { // avatar
        TypeDef d;
        d.name    = "AvatarAPI";
        d.methods = {{"getNBT", aGetNBT}, {"store", aStore}, {"getUUID", aGetUUID},
                     {"getCurrentInstructions", aGetCurrentInstructions}, {"getColor", aGetColor}, {"setColor", aSetColor},
                     {"color", aSetColor}, {"getBadges", aGetBadges}, {"badges", aGetBadges}, {"getVersion", aGetVersion},
                     {"getAuthors", aGetAuthors}, {"getName", aGetName}, {"getEntityName", aGetEntityName},
                     {"getSize", aGetSize}, {"hasTexture", aHasTexture}, {"hasScriptError", aHasScriptError},
                     {"getPermissionLevel", aGetPermissionLevel}, {"getInitCount", aZero}, {"getEntityInitCount", aZero},
                     {"getMaxInitCount", aIntMax}, {"getTickCount", aZero}, {"getMaxTickCount", aIntMax},
                     {"getRenderCount", aZero}, {"getMaxRenderCount", aIntMax}, {"getWorldTickCount", aZero},
                     {"getMaxWorldTickCount", aIntMax}, {"getWorldRenderCount", aZero}, {"getMaxWorldRenderCount", aIntMax},
                     {"getComplexity", aZero}, {"getMaxComplexity", aIntMax}, {"getRemainingParticles", aIntMax},
                     {"getMaxParticles", aIntMax}, {"getRemainingSounds", aIntMax}, {"getMaxSounds", aIntMax},
                     {"getVolume", aVolume}, {"getAnimationComplexity", aZero}, {"getMaxAnimationComplexity", aIntMax},
                     {"getAnimationCount", aAnimationCount}, {"getMaxAnimationCount", aIntMax}, {"getBuffersCount", aZero},
                     {"getMaxBuffersCount", aMaxBuffers}, {"getMaxTextureSize", aTextureSize}, {"getMaxBufferSize", aIntMax},
                     {"canEditVanillaModel", retTrue}, {"canEditNameplate", retTrue}, {"canRenderOffscreen", retTrue},
                     {"canUseCustomSounds", retTrue}, {"canHaveCustomSkull", retTrue}};
        d.tostring = aToString;
        static int anchor;
        registerSingleton(L, "avatar", d, tAvatarAPI, &anchor);
    }
    (void)retNil; (void)retZero; (void)retNone; (void)retEmptyTable; (void)pushNameplate;
}

} // namespace bf::figura
