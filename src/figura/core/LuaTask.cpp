// FiguraBridge Lua API: render tasks (Figura model.rendertasks: RenderTask, TextTask, ItemTask, BlockTask, SpriteTask,
// EntityTask) + the shared LuaUtils-style helpers of LuaTex.h. Bedrock draws no render task yet: every object stores
// all its settings exactly like Figura so scripts read back what they set.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "figura/core/Avatar.h"
#include "figura/core/LuaApi.h"
#include "figura/core/LuaBind.h"
#include "figura/core/LuaTex.h"

namespace bf::figura {

using namespace lua;

uint32_t tRenderTask = 0, tTextTask = 0, tItemTask = 0, tBlockTask = 0, tSpriteTask = 0, tEntityTask = 0;

// =====================================================================================================================
// helpers (LuaTex.h)
// =====================================================================================================================
namespace {
char const kMatricesKey = 0;
char const kOwnedMeta   = 0;

[[noreturn]] void illegalArg(lua_State* L, char const* fn, int idx) {
    std::string t = figuraTypeName(L, idx);
    luaL_error(L, "Illegal argument to %s(): %s", fn, t.c_str());
    for (;;) {}
}
} // namespace

int javaInt(double d) {
    if (std::isnan(d)) return 0;
    if (d >= 2147483647.0) return 2147483647;
    if (d <= -2147483648.0) return -2147483647 - 1;
    return int(d);
}

std::string javaDoubleStr(double d) {
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
    if (d == 0) return std::signbit(d) ? "-0.0" : "0.0";
    // shortest round-tripping digits
    char buf[64];
    int  prec = 1;
    for (; prec <= 17; prec++) {
        std::snprintf(buf, sizeof buf, "%.*e", prec - 1, d);
        if (std::strtod(buf, nullptr) == d) break;
    }
    // buf = [-]d.ddde[+-]xx
    std::string s = buf;
    bool        neg = s[0] == '-';
    if (neg) s.erase(0, 1);
    size_t      e   = s.find('e');
    int         exp = std::atoi(s.c_str() + e + 1);
    std::string digits;
    for (size_t i = 0; i < e; i++)
        if (std::isdigit(static_cast<unsigned char>(s[i]))) digits += s[i];
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    std::string out;
    double      ad = std::fabs(d);
    if (ad >= 1e-3 && ad < 1e7) {
        int point = exp + 1; // digits before the decimal point
        if (point <= 0) {
            out = "0." + std::string(size_t(-point), '0') + digits;
        } else if (point >= int(digits.size())) {
            out = digits + std::string(size_t(point - int(digits.size())), '0') + ".0";
        } else {
            out = digits.substr(0, size_t(point)) + "." + digits.substr(size_t(point));
        }
    } else {
        out = digits.substr(0, 1) + "." + (digits.size() > 1 ? digits.substr(1) : "0") + "E" + std::to_string(exp);
    }
    return neg ? "-" + out : out;
}

V2 luParseVec2(lua_State* L, int idx, char const* fn, double dx, double dy) {
    int n = 0;
    if (double* v = toVec(L, idx, &n)) {
        if (n != 2) illegalArg(L, fn, idx);
        return {v[0], v[1]};
    }
    if (!lua_isnoneornil(L, idx) && !lua_isnumber(L, idx)) illegalArg(L, fn, idx);
    return {optNum(L, idx, dx), optNum(L, idx + 1, dy)};
}

Vec3 luParseVec3(lua_State* L, int idx, char const* fn, double dx, double dy, double dz) {
    int n = 0;
    if (double* v = toVec(L, idx, &n)) {
        if (n != 3) illegalArg(L, fn, idx);
        return {v[0], v[1], v[2]};
    }
    if (!lua_isnoneornil(L, idx) && !lua_isnumber(L, idx)) illegalArg(L, fn, idx);
    return {optNum(L, idx, dx), optNum(L, idx + 1, dy), optNum(L, idx + 2, dz)};
}

Vec3 luParseOneArgVec(lua_State* L, int idx, char const* fn, double def) {
    double d = lua_isnumber(L, idx) ? lua_tonumber(L, idx) : def;
    return luParseVec3(L, idx, fn, d, d, d);
}

V4 luParseVec4(lua_State* L, int idx, char const* fn, double dx, double dy, double dz, double dw) {
    int n = 0;
    if (double* v = toVec(L, idx, &n)) {
        if (n == 3) return {v[0], v[1], v[2], dw};
        if (n == 4) return {v[0], v[1], v[2], v[3]};
        illegalArg(L, fn, idx);
    }
    if (!lua_isnoneornil(L, idx) && !lua_isnumber(L, idx)) illegalArg(L, fn, idx);
    return {optNum(L, idx, dx), optNum(L, idx + 1, dy), optNum(L, idx + 2, dz), optNum(L, idx + 3, dw)};
}

void rememberMatrices(lua_State* L) {
    lua_pushlightuserdata(L, (void*)&kMatricesKey);
    lua_getglobal(L, "matrices");
    lua_rawset(L, LUA_REGISTRYINDEX);
}

void pushMat3(lua_State* L, double const* m) { pushMat(L, m, 3); }

bool readMat3(lua_State* L, int idx, double* m) {
    int     n = 0;
    double* d = toMat(L, idx, &n);
    if (!d || n != 3) return false;
    std::memcpy(m, d, sizeof(double) * 9);
    return true;
}

void partNormalMatrix(Part const& p, double* out) {
    if (p.customMatrix) { // setMatrix: deaugmented, inverted, transposed
        Mat4 const& M = p.positionMatrix;
        double a = M.v11, b = M.v12, c = M.v13, d = M.v21, e = M.v22, f = M.v23, g = M.v31, h = M.v32, i = M.v33;
        double A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
        double D = -(b * i - c * h), E = a * i - c * g, F = -(a * h - b * g);
        double G = b * f - c * e, H = -(a * f - c * d), I = a * e - b * d;
        double det = a * A + b * B + c * C;
        // inverse = adj / det where adj = cofactor^T; transpose of the inverse = cofactor / det
        double co[9] = {A, B, C, D, E, F, G, H, I};
        for (int k = 0; k < 9; k++) out[k] = co[k] / det;
        return;
    }
    double x = p.scale.x * p.animScale.x, y = p.scale.y * p.animScale.y, z = p.scale.z * p.animScale.z;
    double c = std::cbrt(x * y * z);
    Mat4   m;
    m.scale(c == 0 && x == 0 ? 1 : c / x, c == 0 && y == 0 ? 1 : c / y, c == 0 && z == 0 ? 1 : c / z);
    Vec3 r = p.rotation + p.offsetRot + p.animRot;
    if (p.type == PartType::Mesh) {
        m.rotateZ(r.z);
        m.rotateY(r.y);
        m.rotateX(r.x);
    } else {
        m.rotateZYX(r.x, r.y, r.z);
    }
    double o[9] = {m.v11, m.v12, m.v13, m.v21, m.v22, m.v23, m.v31, m.v32, m.v33};
    std::memcpy(out, o, sizeof o);
}

void* newOwnedRaw(lua_State* L, size_t size, void (*dtor)(void*), uint32_t type) {
    struct Box {
        void (*dtor)(void*);
        alignas(16) unsigned char data[1];
    };
    size_t total = offsetof(Box, data) + size;
    auto*  box   = static_cast<Box*>(lua_newuserdata(L, total)); // storage
    box->dtor    = nullptr;
    lua_pushlightuserdata(L, (void*)&kOwnedMeta);
    lua_rawget(L, LUA_REGISTRYINDEX);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushcfunction(L, [](lua_State* L2) -> int {
            auto* b = static_cast<Box*>(lua_touserdata(L2, 1));
            if (b && b->dtor) {
                auto d  = b->dtor;
                b->dtor = nullptr;
                d(b->data);
            }
            return 0;
        });
        lua_setfield(L, -2, "__gc");
        lua_pushlightuserdata(L, (void*)&kOwnedMeta);
        lua_pushvalue(L, -2);
        lua_rawset(L, LUA_REGISTRYINDEX);
    }
    lua_setmetatable(L, -2);
    void* mem = box->data;
    box->dtor = dtor; // set before construction finishes: the caller constructs right after (no Lua call between)
    pushRef(L, mem, type); // reference object
    lua_newtable(L);       // uservalue {storage}
    lua_pushvalue(L, -3);
    lua_rawseti(L, -2, 1);
    lua_setuservalue(L, -2);
    lua_remove(L, -2); // storage (kept alive by the reference's uservalue)
    return mem;
}

int refItemArg(lua_State* L, int idx, char const* fn, char const* typeName) {
    if (lua_isnoneornil(L, idx)) return 0;
    if (lua_type(L, idx) == LUA_TSTRING || (lua_type(L, idx) == LUA_TUSERDATA && figuraTypeName(L, idx) == typeName)) {
        lua_pushvalue(L, idx);
        return luaL_ref(L, LUA_REGISTRYINDEX);
    }
    std::string s = luaL_tolstring(L, idx, nullptr);
    lua_pop(L, 1);
    luaL_error(L, "Illegal argument to %s(): %s", fn, s.c_str());
    return 0;
}

// =====================================================================================================================
// render tasks
// =====================================================================================================================
namespace {

enum class TaskKind { Text, Item, Block, Sprite, Entity };

struct Task {
    TaskKind    kind;
    std::string name;
    Part*       parent = nullptr; // FiguraModelPart that created it
    Part        cust;             // PartCustomization (pos/rot/scale/matrix/light/overlay/visible)
    // TextTask
    bool        hasText = false;
    std::string text;
    std::string alignment = "LEFT";
    bool        shadow = false, outline = false, background = false, seeThrough = false, wrap = true;
    bool        hasOutlineColor = false, hasBackgroundColor = false;
    int         outlineColor = 0, backgroundColor = 0;
    int         opacity = 0xFF, width = 0;
    // ItemTask / BlockTask (Lua value refs)
    int         itemRef = 0;
    std::string displayMode = "NONE";
    bool        left = false;
    // SpriteTask
    Tex*        texture = nullptr;
    std::string texturePath; // texture id ("namespace:path") when set from a string / texture location
    bool        hasTexture = false;
    int         textureW = -1, textureH = -1, sw = 0, sh = 0, regionW = 0, regionH = 0;
    float       u = 0, v = 0;
    int         r = 0xFF, g = 0xFF, b = 0xFF, a = 0xFF;
    std::string renderType = "TRANSLUCENT";
    Vertex*     verts[4] = {nullptr, nullptr, nullptr, nullptr}; // Lua-owned Vertex objects
    int         vertsRef = 0;                                    // table keeping them alive
    // EntityTask
    bool        hasEntity = false;
    std::string entityNbt, entityId;
    Vec3        headRot;
    float       walkDistance = 0;

    Task() { cust.visible = true; }
};

uint32_t typeOf(TaskKind k) {
    switch (k) {
    case TaskKind::Text: return tTextTask;
    case TaskKind::Item: return tItemTask;
    case TaskKind::Block: return tBlockTask;
    case TaskKind::Sprite: return tSpriteTask;
    default: return tEntityTask;
    }
}
char const* const kTaskTypeNames[] = {"TextTask", "ItemTask", "BlockTask", "SpriteTask", "EntityTask"};

Task* toTask(lua_State* L, int idx) {
    for (uint32_t t : {tTextTask, tItemTask, tBlockTask, tSpriteTask, tEntityTask, tRenderTask})
        if (t)
            if (void* p = toRef(L, idx, t)) return static_cast<Task*>(p);
    return nullptr;
}
Task* tself(lua_State* L, char const* fn) {
    Task* t = toTask(L, 1);
    if (!t) luaL_error(L, "bad argument 1 to '%s' (RenderTask expected, got %s)", fn, figuraTypeName(L, 1).c_str());
    return t;
}
Task* kself(lua_State* L, TaskKind k, char const* fn) {
    Task* t = toTask(L, 1);
    if (!t || t->kind != k)
        luaL_error(L, "bad argument 1 to '%s' (%s expected, got %s)", fn, kTaskTypeNames[int(k)], figuraTypeName(L, 1).c_str());
    return t;
}
int ret1(lua_State* L) { lua_settop(L, 1); return 1; }

std::string upper(std::string s) {
    for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// ---- RenderTask (base)
int rtRemove(lua_State* L) {
    Task* t = tself(L, "remove");
    if (t->parent) {
        auto it = t->parent->tasks.find(t->name);
        if (it != t->parent->tasks.end()) {
            luaL_unref(L, LUA_REGISTRYINDEX, it->second);
            t->parent->tasks.erase(it);
        }
    }
    return ret1(L);
}
int rtGetName(lua_State* L) { lua_pushstring(L, tself(L, "getName")->name.c_str()); return 1; }
int rtIsVisible(lua_State* L) { lua_pushboolean(L, tself(L, "isVisible")->cust.visible); return 1; }
int rtSetVisible(lua_State* L) { tself(L, "setVisible")->cust.visible = lua_toboolean(L, 2) != 0; return ret1(L); }
int rtGetLight(lua_State* L) {
    Task* t = tself(L, "getLight");
    if (!t->cust.light) { lua_pushnil(L); return 1; }
    pushVec2(L, lightBlock(*t->cust.light), lightSky(*t->cust.light));
    return 1;
}
int rtSetLight(lua_State* L) {
    Task* t = tself(L, "setLight");
    if (lua_isnoneornil(L, 2)) { t->cust.light.reset(); return ret1(L); }
    V2 v = luParseVec2(L, 2, "setLight");
    t->cust.light = lightPack(javaInt(v.x), javaInt(v.y));
    return ret1(L);
}
int rtGetOverlay(lua_State* L) {
    Task* t = tself(L, "getOverlay");
    if (!t->cust.overlay) { lua_pushnil(L); return 1; }
    pushVec2(L, *t->cust.overlay & 0xFFFF, *t->cust.overlay >> 16);
    return 1;
}
int rtSetOverlay(lua_State* L) {
    Task* t = tself(L, "setOverlay");
    if (lua_isnoneornil(L, 2)) { t->cust.overlay.reset(); return ret1(L); }
    V2 v = luParseVec2(L, 2, "setOverlay");
    t->cust.overlay = overlayPack(javaInt(v.x), javaInt(v.y));
    return ret1(L);
}
int rtGetPos(lua_State* L) { pushVec3(L, tself(L, "getPos")->cust.position); return 1; }
int rtSetPos(lua_State* L) { Task* t = tself(L, "setPos"); t->cust.position = luParseVec3(L, 2, "setPos"); t->cust.customMatrix = false; return ret1(L); }
int rtGetRot(lua_State* L) { pushVec3(L, tself(L, "getRot")->cust.rotation); return 1; }
int rtSetRot(lua_State* L) { Task* t = tself(L, "setRot"); t->cust.rotation = luParseVec3(L, 2, "setRot"); t->cust.customMatrix = false; return ret1(L); }
int rtGetScale(lua_State* L) { pushVec3(L, tself(L, "getScale")->cust.scale); return 1; }
int rtSetScale(lua_State* L) { Task* t = tself(L, "setScale"); t->cust.scale = luParseOneArgVec(L, 2, "setScale", 1); t->cust.customMatrix = false; return ret1(L); }
int rtGetPositionMatrix(lua_State* L) { Task* t = tself(L, "getPositionMatrix"); t->cust.recalculate(); pushMat4(L, t->cust.positionMatrix); return 1; }
int rtGetPositionMatrixRaw(lua_State* L) { Task* t = tself(L, "getPositionMatrixRaw"); t->cust.recalculate(); pushMat4(L, t->cust.positionMatrix); return 1; }
int rtGetNormalMatrix(lua_State* L) {
    Task* t = tself(L, "getNormalMatrix");
    double m[9];
    partNormalMatrix(t->cust, m);
    pushMat3(L, m);
    return 1;
}
int rtSetMatrix(lua_State* L) {
    Task* t = tself(L, "setMatrix");
    Mat4* m = toMat4(L, 2);
    if (!m) return luaL_error(L, "bad argument 2 to 'setMatrix' (Matrix4 expected, got %s)", figuraTypeName(L, 2).c_str());
    t->cust.positionMatrix = *m;
    t->cust.customMatrix   = true;
    return ret1(L);
}
int rtToString(lua_State* L) {
    Task* t = tself(L, "__tostring");
    static char const* kinds[] = {"Text Render Task", "Item Render Task", "Block Render Task", "Sprite Render Task", "Render Task"};
    lua_pushfstring(L, "%s (%s)", t->name.c_str(), kinds[int(t->kind)]);
    return 1;
}

std::vector<luaL_Reg> baseMethods() {
    return {{"remove", rtRemove}, {"getName", rtGetName}, {"isVisible", rtIsVisible}, {"setVisible", rtSetVisible},
            {"visible", rtSetVisible}, {"getLight", rtGetLight}, {"setLight", rtSetLight}, {"light", rtSetLight},
            {"getOverlay", rtGetOverlay}, {"setOverlay", rtSetOverlay}, {"overlay", rtSetOverlay},
            {"getPos", rtGetPos}, {"setPos", rtSetPos}, {"pos", rtSetPos}, {"getRot", rtGetRot}, {"setRot", rtSetRot},
            {"rot", rtSetRot}, {"getScale", rtGetScale}, {"setScale", rtSetScale}, {"scale", rtSetScale},
            {"getPositionMatrix", rtGetPositionMatrix}, {"getPositionMatrixRaw", rtGetPositionMatrixRaw},
            {"getNormalMatrix", rtGetNormalMatrix}, {"getNormalMatrixRaw", rtGetNormalMatrix},
            {"setMatrix", rtSetMatrix}, {"matrix", rtSetMatrix}};
}

// ---- TextTask
#define TT(fn) Task* t = kself(L, TaskKind::Text, fn)
int ttGetText(lua_State* L) { TT("getText"); if (t->hasText) lua_pushlstring(L, t->text.data(), t->text.size()); else lua_pushnil(L); return 1; }
int ttSetText(lua_State* L) {
    TT("setText");
    if (lua_isnoneornil(L, 2)) t->hasText = false;
    else {
        size_t n;
        char const* s = luaL_checklstring(L, 2, &n);
        t->text.assign(s, n);
        t->hasText = true;
    }
    return ret1(L);
}
int ttGetAlignment(lua_State* L) { TT("getAlignment"); lua_pushstring(L, t->alignment.c_str()); return 1; }
int ttSetAlignment(lua_State* L) {
    TT("setAlignment");
    std::string s = lua_isstring(L, 2) ? lua_tostring(L, 2) : "null";
    std::string u = upper(s);
    if (lua_isnoneornil(L, 2) || (u != "LEFT" && u != "RIGHT" && u != "CENTER"))
        return luaL_error(L, "Invalid alignment type \"%s\"", s.c_str());
    t->alignment = u;
    return ret1(L);
}
#define TT_BOOL(get, set, field)                                                                   \
    int get(lua_State* L) { TT(#get); lua_pushboolean(L, t->field); return 1; }                   \
    int set(lua_State* L) { TT(#set); t->field = lua_toboolean(L, 2) != 0; return ret1(L); }
TT_BOOL(ttHasShadow, ttSetShadow, shadow)
TT_BOOL(ttHasOutline, ttSetOutline, outline)
TT_BOOL(ttHasWrap, ttSetWrap, wrap)
TT_BOOL(ttIsSeeThrough, ttSetSeeThrough, seeThrough)
TT_BOOL(ttHasBackground, ttSetBackground, background)
#undef TT_BOOL
int ttGetOutlineColor(lua_State* L) {
    TT("getOutlineColor");
    if (!t->hasOutlineColor) { pushVec3(L, {0, 0, 0}); return 1; }
    int c = t->outlineColor;
    pushVec3(L, {fl(((c >> 16) & 0xFF) / 255.0f), fl(((c >> 8) & 0xFF) / 255.0f), fl((c & 0xFF) / 255.0f)});
    return 1;
}
int ttSetOutlineColor(lua_State* L) {
    TT("setOutlineColor");
    Vec3 v = luParseVec3(L, 2, "setOutlineColor");
    int  hex = javaInt(v.x * 0xFF);
    hex = int(unsigned(hex) << 8) + javaInt(v.y * 0xFF);
    hex = int(unsigned(hex) << 8) + javaInt(v.z * 0xFF);
    t->outlineColor    = hex;
    t->hasOutlineColor = true;
    return ret1(L);
}
int ttGetWidth(lua_State* L) { TT("getWidth"); lua_pushinteger(L, t->width); return 1; }
int ttSetWidth(lua_State* L) { TT("setWidth"); t->width = javaInt(luaL_checknumber(L, 2)); return ret1(L); }
int ttGetBackgroundColor(lua_State* L) {
    TT("getBackgroundColor");
    if (!t->hasBackgroundColor) { lua_pushnil(L); return 1; }
    unsigned c = unsigned(t->backgroundColor);
    pushVec4(L, fl(((c >> 24) & 0xFF) / 255.0f), fl(((c >> 16) & 0xFF) / 255.0f), fl(((c >> 8) & 0xFF) / 255.0f),
             fl((c & 0xFF) / 255.0f));
    return 1;
}
int ttSetBackgroundColor(lua_State* L) {
    TT("setBackgroundColor");
    V4  v   = luParseVec4(L, 2, "setBackgroundColor", 0, 0, 0, fl(0.25f));
    int hex = javaInt(v.x * 0xFF);
    hex = int(unsigned(hex) << 8) + javaInt(v.y * 0xFF);
    hex = int(unsigned(hex) << 8) + javaInt(v.z * 0xFF);
    hex = int(unsigned(hex) << 8) + javaInt(v.w * 0xFF);
    t->backgroundColor    = hex;
    t->hasBackgroundColor = true;
    return ret1(L);
}
int ttGetOpacity(lua_State* L) { TT("getOpacity"); lua_pushnumber(L, fl(t->opacity / float(0xFF))); return 1; }
int ttSetOpacity(lua_State* L) { TT("setOpacity"); t->opacity = javaInt(float(luaL_checknumber(L, 2)) * 0xFF); return ret1(L); }
#undef TT

// ---- ItemTask / BlockTask
char const* const kDisplayModes[] = {"NONE", "THIRD_PERSON_LEFT_HAND", "THIRD_PERSON_RIGHT_HAND", "FIRST_PERSON_LEFT_HAND",
                                     "FIRST_PERSON_RIGHT_HAND", "HEAD", "GUI", "GROUND", "FIXED"};
int itSetItem(lua_State* L) {
    Task* t = kself(L, TaskKind::Item, "setItem");
    int   r = refItemArg(L, 2, "item", "ItemStack");
    if (t->itemRef) luaL_unref(L, LUA_REGISTRYINDEX, t->itemRef);
    t->itemRef = r;
    return ret1(L);
}
int itGetDisplayMode(lua_State* L) { lua_pushstring(L, kself(L, TaskKind::Item, "getDisplayMode")->displayMode.c_str()); return 1; }
int itSetDisplayMode(lua_State* L) {
    Task*       t = kself(L, TaskKind::Item, "setDisplayMode");
    std::string s = lua_isstring(L, 2) ? lua_tostring(L, 2) : "null";
    std::string u = upper(s);
    for (char const* m : kDisplayModes)
        if (u == m && !lua_isnoneornil(L, 2)) {
            t->displayMode = u;
            t->left        = u == "FIRST_PERSON_LEFT_HAND" || u == "THIRD_PERSON_LEFT_HAND";
            return ret1(L);
        }
    return luaL_error(L, "Illegal display mode: \"%s\".", s.c_str());
}
int btSetBlock(lua_State* L) {
    Task* t = kself(L, TaskKind::Block, "setBlock");
    int   r = refItemArg(L, 2, "block", "BlockState");
    if (t->itemRef) luaL_unref(L, LUA_REGISTRYINDEX, t->itemRef);
    t->itemRef = r;
    return ret1(L);
}

// ---- SpriteTask
#define ST(fn) Task* t = kself(L, TaskKind::Sprite, fn)
void recalcVertices(Task* t) {
    float u2 = t->u + t->regionW / float(t->textureW);
    float v2 = t->v + t->regionH / float(t->textureH);
    float w = float(t->sw), h = float(t->sh);
    Vertex nv[4] = {{0, h, 0, t->u, v2, 0, 0, -1}, {w, h, 0, u2, v2, 0, 0, -1}, {w, 0, 0, u2, t->v, 0, 0, -1},
                    {0, 0, 0, t->u, t->v, 0, 0, -1}};
    for (int i = 0; i < 4; i++)
        if (t->verts[i]) *t->verts[i] = nv[i];
}
int stGetTexture(lua_State* L) { ST("getTexture"); if (t->hasTexture) lua_pushstring(L, t->texturePath.c_str()); else lua_pushnil(L); return 1; }
int stSetTexture(lua_State* L) {
    ST("setTexture");
    if (lua_isnoneornil(L, 2)) {
        t->hasTexture = false;
        t->texture    = nullptr;
        return ret1(L);
    }
    bool haveDims = !lua_isnoneornil(L, 3) && !lua_isnoneornil(L, 4);
    V2   dims     = luParseVec2(L, 3, "setTexture");
    if (lua_type(L, 2) == LUA_TSTRING) {
        std::string s = lua_tostring(L, 2);
        // ResourceLocation: default namespace minecraft, invalid -> missing texture
        bool valid = true;
        std::string ns = "minecraft", path = s;
        size_t c = s.find(':');
        if (c != std::string::npos) { ns = c ? s.substr(0, c) : "minecraft"; path = s.substr(c + 1); }
        for (char ch : ns) if (!(std::islower(static_cast<unsigned char>(ch)) || std::isdigit(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-' || ch == '.')) valid = false;
        for (char ch : path) if (!(std::islower(static_cast<unsigned char>(ch)) || std::isdigit(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-' || ch == '.' || ch == '/')) valid = false;
        t->texturePath = valid ? ns + ":" + path : "minecraft:missingno";
        t->texture     = nullptr;
        t->hasTexture  = true;
        if (!haveDims) return luaL_error(L, "Texture dimensions cannot be null");
    } else if (Tex* tx = static_cast<Tex*>(toRef(L, 2, tTexture))) {
        t->texture     = tx;
        t->texturePath = tx->location;
        t->hasTexture  = true;
        if (!haveDims) {
            texSize(*tx);
            dims = {double(tx->w), double(tx->h)};
            // Figura (0.1.6) sets the local width/height but leaves `dimensions` null here: sizes stay unchanged
            recalcVertices(t);
            return ret1(L);
        }
    } else {
        return luaL_error(L, "Illegal argument to setTexture(): %s", figuraTypeName(L, 2).c_str());
    }
    if (dims.x <= 0 || dims.y <= 0)
        return luaL_error(L, "Invalid texture size: %sx%s", lua_tostring(L, 3) ? lua_tostring(L, 3) : "null",
                          lua_tostring(L, 4) ? lua_tostring(L, 4) : "null");
    t->textureW = t->regionW = t->sw = int(std::llround(dims.x));
    t->textureH = t->regionH = t->sh = int(std::llround(dims.y));
    recalcVertices(t);
    return ret1(L);
}
int stGetDimensions(lua_State* L) { ST("getDimensions"); pushVec2(L, t->textureW, t->textureH); return 1; }
int stSetDimensions(lua_State* L) {
    ST("setDimensions");
    V2 v = luParseVec2(L, 2, "setDimensions");
    if (v.x <= 0 || v.y <= 0)
        return luaL_error(L, "Invalid dimensions: %sx%s", javaDoubleStr(v.x).c_str(), javaDoubleStr(v.y).c_str());
    t->textureW = int(std::llround(v.x));
    t->textureH = int(std::llround(v.y));
    recalcVertices(t);
    return ret1(L);
}
int stGetSize(lua_State* L) { ST("getSize"); pushVec2(L, t->sw, t->sh); return 1; }
int stSetSize(lua_State* L) {
    ST("setSize");
    V2 v = luParseVec2(L, 2, "setSize");
    t->sw = int(std::llround(v.x));
    t->sh = int(std::llround(v.y));
    recalcVertices(t);
    return ret1(L);
}
int stGetRegion(lua_State* L) { ST("getRegion"); pushVec2(L, t->regionW, t->regionH); return 1; }
int stSetRegion(lua_State* L) {
    ST("setRegion");
    V2 v = luParseVec2(L, 2, "setRegion");
    t->regionW = int(std::llround(v.x));
    t->regionH = int(std::llround(v.y));
    recalcVertices(t);
    return ret1(L);
}
int stGetUV(lua_State* L) { ST("getUV"); pushVec2(L, t->u, t->v); return 1; }
int stSetUV(lua_State* L) {
    ST("setUV");
    V2 v = luParseVec2(L, 2, "setUV");
    t->u = float(v.x);
    t->v = float(v.y);
    recalcVertices(t);
    return ret1(L);
}
int stGetUVPixels(lua_State* L) {
    ST("getUVPixels");
    if (t->textureW == -1 || t->textureH == -1)
        return luaL_error(L, "Cannot call getUVPixels before defining the texture dimensions!");
    pushVec2(L, double(t->u) * t->textureW, double(t->v) * t->textureH);
    return 1;
}
int stSetUVPixels(lua_State* L) {
    ST("setUVPixels");
    if (t->textureW == -1 || t->textureH == -1)
        return luaL_error(L, "Cannot call setUVPixels before defining the texture dimensions!");
    V2 v = luParseVec2(L, 2, "setUVPixels");
    t->u = float(v.x / t->textureW);
    t->v = float(v.y / t->textureH);
    recalcVertices(t);
    return ret1(L);
}
int stGetColor(lua_State* L) {
    ST("getColor");
    double s = fl(1.0f / 255.0f);
    pushVec4(L, t->r * s, t->g * s, t->b * s, t->a * s);
    return 1;
}
int stSetColor(lua_State* L) {
    ST("setColor");
    V4  v   = luParseVec4(L, 2, "setColor", 0, 0, 0, 1);
    int hex = javaInt(v.x * 0xFF);
    hex = int(unsigned(hex) << 8) + javaInt(v.y * 0xFF);
    hex = int(unsigned(hex) << 8) + javaInt(v.z * 0xFF);
    hex = int(unsigned(hex) << 8) + javaInt(v.w * 0xFF);
    t->r = hex >> 24 & 0xFF;
    t->g = hex >> 16 & 0xFF;
    t->b = hex >> 8 & 0xFF;
    t->a = hex & 0xFF;
    return ret1(L);
}
char const* const kRenderTypes[] = {"NONE", "CUTOUT", "CUTOUT_CULL", "CUTOUT_EMISSIVE_SOLID", "TRANSLUCENT",
                                    "TRANSLUCENT_CULL", "EMISSIVE", "EMISSIVE_SOLID", "EYES", "END_PORTAL",
                                    "END_GATEWAY", "TEXTURED_PORTAL", "GLINT", "TEXTURED_GLINT", "LINES",
                                    "LINES_STRIP", "SOLID", "BLURRY"};
int stGetRenderType(lua_State* L) { ST("getRenderType"); lua_pushstring(L, t->renderType.c_str()); return 1; }
int stSetRenderType(lua_State* L) {
    ST("setRenderType");
    std::string s = lua_isstring(L, 2) ? lua_tostring(L, 2) : "null";
    std::string u = upper(s);
    if (!lua_isnoneornil(L, 2))
        for (char const* r : kRenderTypes)
            if (u == r) {
                t->renderType = u;
                return ret1(L);
            }
    return luaL_error(L, "Illegal RenderType: \"%s\".", s.c_str());
}
int stGetVertices(lua_State* L) {
    ST("getVertices");
    lua_rawgeti(L, LUA_REGISTRYINDEX, t->vertsRef);
    lua_newtable(L); // a fresh list each call (javaToLua(List)), same Vertex objects
    for (int i = 1; i <= 4; i++) {
        lua_rawgeti(L, -2, i);
        lua_rawseti(L, -2, i);
    }
    return 1;
}
#undef ST

// ---- EntityTask
// minimal SNBT check: Figura parses the compound and requires an "id" string (or takes id as the first argument)
bool snbtHasId(std::string const& s, std::string* id) {
    size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
    if (i >= s.size() || s[i] != '{') return false;
    int depth = 0;
    for (size_t k = i; k < s.size(); k++) {
        char c = s[k];
        if (c == '"' || c == '\'') {
            char q = c;
            for (k++; k < s.size() && s[k] != q; k++)
                if (s[k] == '\\') k++;
            continue;
        }
        if (c == '{' || c == '[') depth++;
        else if (c == '}' || c == ']') depth--;
        else if (depth == 1 && (s.compare(k, 2, "id") == 0 || s.compare(k, 4, "\"id\"") == 0)) {
            bool quoted = s[k] == '"';
            if (k > 0 && std::isalnum(static_cast<unsigned char>(s[k - 1]))) continue;
            size_t j = k + (quoted ? 4 : 2);
            while (j < s.size() && std::isspace(static_cast<unsigned char>(s[j]))) j++;
            if (j < s.size() && s[j] == ':') {
                j++;
                while (j < s.size() && std::isspace(static_cast<unsigned char>(s[j]))) j++;
                if (j < s.size() && (s[j] == '"' || s[j] == '\'')) {
                    char   q = s[j];
                    size_t e = s.find(q, j + 1);
                    if (e == std::string::npos) return false;
                    if (id) *id = s.substr(j + 1, e - j - 1);
                    return true;
                }
                size_t e = j;
                while (e < s.size() && s[e] != ',' && s[e] != '}' && !std::isspace(static_cast<unsigned char>(s[e]))) e++;
                std::string v = s.substr(j, e - j);
                // an unquoted id must still be a string tag (not a number)
                char* end = nullptr;
                std::strtod(v.c_str(), &end);
                if (!v.empty() && end && *end == 0) return false;
                if (id) *id = v;
                return !v.empty();
            }
        }
    }
    return false;
}
int etAsEntity(lua_State* L) { (void)kself(L, TaskKind::Entity, "asEntity"); lua_pushnil(L); return 1; } // no Bedrock entity instance
int etSetNbt(lua_State* L) {
    Task*       t   = kself(L, TaskKind::Entity, "setNbt");
    std::string a   = luaL_checkstring(L, 2);
    std::string id;
    if (lua_isnoneornil(L, 3)) {
        std::string tmp;
        if (a.find_first_not_of(" \t\r\n") == std::string::npos || a[a.find_first_not_of(" \t\r\n")] != '{')
            return luaL_error(L, "Expected '{' at position 0: <--[HERE]");
        if (!snbtHasId(a, &id)) return luaL_error(L, "Nbt must contain id");
        t->entityNbt = a;
    } else {
        std::string b = luaL_checkstring(L, 3);
        if (b.find_first_not_of(" \t\r\n") == std::string::npos || b[b.find_first_not_of(" \t\r\n")] != '{')
            return luaL_error(L, "Expected '{' at position 0: <--[HERE]");
        id           = a;
        t->entityNbt = b;
    }
    t->entityId  = id;
    t->hasEntity = true;
    return ret1(L);
}
int etUpdateWalkingDistance(lua_State* L) {
    Task* t = kself(L, TaskKind::Entity, "updateWalkingDistance");
    if (t->hasEntity) t->walkDistance = float(luaL_checknumber(L, 2));
    return ret1(L);
}
int etSetHeadRotation(lua_State* L) {
    Task* t = kself(L, TaskKind::Entity, "setHeadRotation");
    int   n = 0;
    double* v = toVec(L, 2, &n);
    if (!v || n != 2) return luaL_error(L, "bad argument 2 to 'setHeadRotation' (Vector2 expected, got %s)", figuraTypeName(L, 2).c_str());
    if (t->hasEntity) t->headRot = {fl(v[0]), fl(v[1]), 0};
    return ret1(L);
}

// ---- creation (FiguraModelPart.newText/newItem/...)
int newTaskOf(lua_State* L, TaskKind k, char const* fn) {
    Part* p = check<Part>(L, 1, tModelPart, fn);
    std::string name = luaL_checkstring(L, 2);
    Task* t = newOwned<Task>(L, typeOf(k));
    t->kind   = k;
    t->name   = name;
    t->parent = p;
    if (k == TaskKind::Sprite) {
        lua_newtable(L);
        for (int i = 0; i < 4; i++) {
            Vertex* v = newOwned<Vertex>(L, tVertex);
            t->verts[i] = v;
            lua_rawseti(L, -2, i + 1);
        }
        t->vertsRef = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    auto it = p->tasks.find(name);
    if (it != p->tasks.end()) luaL_unref(L, LUA_REGISTRYINDEX, it->second);
    lua_pushvalue(L, -1);
    p->tasks[name] = luaL_ref(L, LUA_REGISTRYINDEX);
    return 1;
}
} // namespace

// called from LuaApiModels.cpp (ModelPart methods)
int partNewText(lua_State* L) { return newTaskOf(L, TaskKind::Text, "newText"); }
int partNewItem(lua_State* L) { return newTaskOf(L, TaskKind::Item, "newItem"); }
int partNewBlock(lua_State* L) { return newTaskOf(L, TaskKind::Block, "newBlock"); }
int partNewSprite(lua_State* L) { return newTaskOf(L, TaskKind::Sprite, "newSprite"); }
int partNewEntity(lua_State* L) { return newTaskOf(L, TaskKind::Entity, "newEntity"); }
int partAddTask(lua_State* L) {
    Part* p = check<Part>(L, 1, tModelPart, "addTask");
    Task* t = toTask(L, 2);
    if (!t) return luaL_error(L, "bad argument 2 to 'addTask' (RenderTask expected, got %s)", figuraTypeName(L, 2).c_str());
    auto it = p->tasks.find(t->name);
    if (it != p->tasks.end()) luaL_unref(L, LUA_REGISTRYINDEX, it->second);
    lua_pushvalue(L, 2);
    p->tasks[t->name] = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_settop(L, 2);
    return 1;
}
int partRemoveTask(lua_State* L) {
    Part* p = check<Part>(L, 1, tModelPart, "removeTask");
    auto  drop = [&](std::string const& n) {
        auto it = p->tasks.find(n);
        if (it != p->tasks.end()) {
            luaL_unref(L, LUA_REGISTRYINDEX, it->second);
            p->tasks.erase(it);
        }
    };
    if (lua_isnoneornil(L, 2)) {
        for (auto& [k, r] : p->tasks) luaL_unref(L, LUA_REGISTRYINDEX, r);
        p->tasks.clear();
    } else if (lua_isnumber(L, 2)) { // LuaTypeManager.luaToJava: numbers (and numeric strings) become Integer/Double
        double d = lua_tonumber(L, 2);
        return luaL_error(L, "Illegal argument to removeTask(): %s", d == std::floor(d) && std::fabs(d) < 2147483648.0 ? "Integer" : "Double");
    } else if (lua_type(L, 2) == LUA_TSTRING) {
        drop(lua_tostring(L, 2));
    } else if (Task* t = toTask(L, 2)) {
        drop(t->name);
    } else {
        return luaL_error(L, "Illegal argument to removeTask(): %s", figuraTypeName(L, 2).c_str());
    }
    lua_settop(L, 1);
    return 1;
}

void openRenderTaskApi(Avatar& a) {
    lua_State* L = a.L;
    auto make = [&](char const* name, std::vector<luaL_Reg> extra) {
        TypeDef d;
        d.name    = name;
        d.methods = baseMethods();
        d.methods.insert(d.methods.end(), extra.begin(), extra.end());
        d.tostring = rtToString;
        return registerType(L, d);
    };
    tRenderTask = make("RenderTask", {});
    tTextTask   = make("TextTask", {
        {"getText", ttGetText}, {"setText", ttSetText}, {"text", ttSetText}, {"getAlignment", ttGetAlignment},
        {"setAlignment", ttSetAlignment}, {"alignment", ttSetAlignment}, {"hasShadow", ttHasShadow},
        {"setShadow", ttSetShadow}, {"shadow", ttSetShadow}, {"hasOutline", ttHasOutline},
        {"setOutline", ttSetOutline}, {"outline", ttSetOutline}, {"getOutlineColor", ttGetOutlineColor},
        {"setOutlineColor", ttSetOutlineColor}, {"outlineColor", ttSetOutlineColor}, {"getWidth", ttGetWidth},
        {"setWidth", ttSetWidth}, {"width", ttSetWidth}, {"hasWrap", ttHasWrap}, {"setWrap", ttSetWrap},
        {"wrap", ttSetWrap}, {"isSeeThrough", ttIsSeeThrough}, {"setSeeThrough", ttSetSeeThrough},
        {"seeThrough", ttSetSeeThrough}, {"hasBackground", ttHasBackground}, {"setBackground", ttSetBackground},
        {"background", ttSetBackground}, {"getBackgroundColor", ttGetBackgroundColor},
        {"setBackgroundColor", ttSetBackgroundColor}, {"backgroundColor", ttSetBackgroundColor},
        {"getOpacity", ttGetOpacity}, {"setOpacity", ttSetOpacity}, {"opacity", ttSetOpacity}});
    tItemTask = make("ItemTask", {{"setItem", itSetItem}, {"item", itSetItem}, {"getDisplayMode", itGetDisplayMode},
                                  {"setDisplayMode", itSetDisplayMode}, {"displayMode", itSetDisplayMode}});
    tBlockTask = make("BlockTask", {{"setBlock", btSetBlock}, {"block", btSetBlock}});
    tSpriteTask = make("SpriteTask", {
        {"getTexture", stGetTexture}, {"setTexture", stSetTexture}, {"texture", stSetTexture},
        {"getDimensions", stGetDimensions}, {"setDimensions", stSetDimensions}, {"dimensions", stSetDimensions},
        {"getSize", stGetSize}, {"setSize", stSetSize}, {"size", stSetSize}, {"getRegion", stGetRegion},
        {"setRegion", stSetRegion}, {"region", stSetRegion}, {"getUV", stGetUV}, {"setUV", stSetUV}, {"uv", stSetUV},
        {"getUVPixels", stGetUVPixels}, {"setUVPixels", stSetUVPixels}, {"uvPixels", stSetUVPixels},
        {"getColor", stGetColor}, {"setColor", stSetColor}, {"color", stSetColor}, {"getRenderType", stGetRenderType},
        {"setRenderType", stSetRenderType}, {"renderType", stSetRenderType}, {"getVertices", stGetVertices}});
    tEntityTask = make("EntityTask", {{"asEntity", etAsEntity}, {"setNbt", etSetNbt},
                                      {"updateWalkingDistance", etUpdateWalkingDistance},
                                      {"setHeadRotation", etSetHeadRotation}});
}

void taskDraws(Avatar& a, Part* p, std::vector<TaskDraw>& out) {
    lua_State* L = a.L;
    if (!L) return;
    for (auto const& [name, ref] : p->tasks) {
        (void)name;
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        Task* t = toTask(L, -1);
        lua_pop(L, 1);
        if (!t || !t->cust.visible || (t->kind != TaskKind::Item && t->kind != TaskKind::Block) || !t->itemRef) continue;
        std::string id;
        lua_rawgeti(L, LUA_REGISTRYINDEX, t->itemRef);
        if (lua_type(L, -1) == LUA_TSTRING) id = lua_tostring(L, -1);
        else {
            lua_getfield(L, -1, "getID");
            if (lua_isfunction(L, -1)) {
                lua_pushvalue(L, -2);
                if (lua_pcall(L, 1, 1, 0) == 0 && lua_type(L, -1) == LUA_TSTRING) id = lua_tostring(L, -1);
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        size_t cut = id.find_first_of("{[ ");
        if (cut != std::string::npos) id.resize(cut);
        if (id.empty()) continue;
        if (id.find(':') == std::string::npos) id = "minecraft:" + id;
        t->cust.recalculate();
        out.push_back({t->kind == TaskKind::Item ? 1 : 2, id, t->cust.positionMatrix});
    }
}

} // namespace bf::figura
