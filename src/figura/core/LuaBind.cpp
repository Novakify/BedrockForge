// FiguraBridge Lua binding core + Figura vectors/matrices (org.figuramc.figura.math.vector / matrix, VectorsAPI,
// MatricesAPI) - port.
#include "figura/core/LuaBind.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

namespace bf::figura::lua {

namespace {
std::vector<std::string>& typeNames() {
    static std::vector<std::string> v{"<none>"};
    return v;
}
std::mutex gTypeMutex;
char const kCacheKey = 0; // registry[&kCacheKey] = object cache (lightuserdata -> userdata)

std::string mtName(uint32_t id) { return "fb:" + typeNames()[id]; }

/// Generic __index: methods table (upvalue 1), then the type's own fallback (upvalue 2, may be nil).
int genericIndex(lua_State* L) {
    lua_pushvalue(L, 2);
    lua_rawget(L, lua_upvalueindex(1));
    if (!lua_isnil(L, -1)) return 1;
    lua_pop(L, 1);
    if (lua_isnil(L, lua_upvalueindex(2))) return 0;
    lua_pushvalue(L, lua_upvalueindex(2));
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 2);
    lua_call(L, 2, 1);
    return 1;
}
int defaultNewIndex(lua_State* L) {
    std::string k = lua_isstring(L, 2) ? lua_tostring(L, 2) : "?";
    return luaL_error(L, "Cannot assign value on key \"%s\"", k.c_str());
}
} // namespace

uint32_t typeId(char const* name) {
    std::lock_guard lk(gTypeMutex);
    auto& v = typeNames();
    for (size_t i = 1; i < v.size(); i++)
        if (v[i] == name) return uint32_t(i);
    v.emplace_back(name);
    return uint32_t(v.size() - 1);
}
char const* typeName(uint32_t id) {
    std::lock_guard lk(gTypeMutex);
    auto& v = typeNames();
    return id < v.size() ? v[id].c_str() : "?";
}

uint32_t registerType(lua_State* L, TypeDef const& def) {
    uint32_t id = typeId(def.name);
    luaL_newmetatable(L, mtName(id).c_str()); // mt
    // methods table
    lua_newtable(L);
    for (auto const& r : def.methods) {
        if (!r.name) continue;
        lua_pushcfunction(L, r.func);
        lua_setfield(L, -2, r.name);
    }
    if (def.index) lua_pushcfunction(L, def.index);
    else lua_pushnil(L);
    lua_pushcclosure(L, genericIndex, 2);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, def.newindex ? def.newindex : defaultNewIndex);
    lua_setfield(L, -2, "__newindex");
    if (def.tostring) {
        lua_pushcfunction(L, def.tostring);
        lua_setfield(L, -2, "__tostring");
    }
    for (auto const& r : def.meta) {
        if (!r.name) continue;
        lua_pushcfunction(L, r.func);
        lua_setfield(L, -2, r.name);
    }
    lua_pushstring(L, def.name);
    lua_setfield(L, -2, "__type");
    // figuraMetatables[name] = mt
    lua_getglobal(L, "figuraMetatables");
    if (lua_istable(L, -1)) {
        lua_pushvalue(L, -2);
        lua_setfield(L, -2, def.name);
    }
    lua_pop(L, 2);
    return id;
}

static void pushCache(lua_State* L) {
    lua_pushlightuserdata(L, (void*)&kCacheKey);
    lua_rawget(L, LUA_REGISTRYINDEX);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushlightuserdata(L, (void*)&kCacheKey);
        lua_pushvalue(L, -2);
        lua_rawset(L, LUA_REGISTRYINDEX);
    }
}

void pushRef(lua_State* L, void* ptr, uint32_t type) {
    if (!ptr) { lua_pushnil(L); return; }
    pushCache(L);
    lua_pushlightuserdata(L, ptr);
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) {
        // cache hit: check type matches (a struct can host different "views"; cache by ptr+type pair otherwise)
        auto* ud = static_cast<RefUD*>(lua_touserdata(L, -1));
        if (ud && ud->type == type) {
            lua_remove(L, -2);
            return;
        }
    }
    lua_pop(L, 1);
    auto* ud = static_cast<RefUD*>(lua_newuserdata(L, sizeof(RefUD)));
    ud->ptr  = ptr;
    ud->type = type;
    luaL_setmetatable(L, mtName(type).c_str());
    lua_pushlightuserdata(L, ptr);
    lua_pushvalue(L, -2);
    lua_rawset(L, -4);
    lua_remove(L, -2);
}

void forgetRef(lua_State* L, void* ptr) {
    pushCache(L);
    lua_pushlightuserdata(L, ptr);
    lua_pushnil(L);
    lua_rawset(L, -3);
    lua_pop(L, 1);
}

void* toRef(lua_State* L, int idx, uint32_t type) {
    void* p = luaL_testudata(L, idx, mtName(type).c_str());
    if (!p) return nullptr;
    return static_cast<RefUD*>(p)->ptr;
}

std::string figuraTypeName(lua_State* L, int idx) {
    if (lua_type(L, idx) == LUA_TUSERDATA && lua_getmetatable(L, idx)) {
        lua_getfield(L, -1, "__type");
        std::string n = lua_isstring(L, -1) ? lua_tostring(L, -1) : "userdata";
        lua_pop(L, 2);
        return n;
    }
    if (lua_type(L, idx) == LUA_TTABLE && lua_getmetatable(L, idx)) {
        lua_pushstring(L, "__type");
        lua_rawget(L, -2);
        if (lua_isstring(L, -1)) {
            std::string n = lua_tostring(L, -1);
            lua_pop(L, 2);
            return n;
        }
        lua_pop(L, 2);
    }
    return luaL_typename(L, idx);
}

void* checkRef(lua_State* L, int idx, uint32_t type, char const* fn) {
    void* p = toRef(L, idx, type);
    if (!p)
        luaL_error(L, "bad argument %d to '%s' (%s expected, got %s)", idx, fn, typeName(type), figuraTypeName(L, idx).c_str());
    return p;
}

double optNum(lua_State* L, int idx, double def) { return lua_isnoneornil(L, idx) ? def : luaL_checknumber(L, idx); }
bool   optBool(lua_State* L, int idx, bool def) { return lua_isnoneornil(L, idx) ? def : lua_toboolean(L, idx) != 0; }
std::string optStr(lua_State* L, int idx, char const* def) {
    if (lua_isnoneornil(L, idx)) return def ? def : "";
    size_t n;
    char const* s = luaL_checklstring(L, idx, &n);
    return std::string(s, n);
}
void raise(lua_State* L, std::string const& msg) {
    lua_pushstring(L, msg.c_str());
    lua_error(L);
    for (;;) {} // unreachable
}
int pushStrings(lua_State* L, std::vector<std::string> const& v) {
    lua_createtable(L, int(v.size()), 0);
    for (size_t i = 0; i < v.size(); i++) {
        lua_pushstring(L, v[i].c_str());
        lua_rawseti(L, -2, int(i + 1));
    }
    return 1;
}

// =====================================================================================================================
// Vectors (FiguraVec2 / FiguraVec3 / FiguraVec4): mutable value userdata {n, x, y, z, w}
// =====================================================================================================================
namespace {
struct VecUD {
    int    n;
    double v[4];
};
uint32_t gVecT[5] = {0, 0, 0, 0, 0};
char const* kVecNames[5] = {"", "", "Vector2", "Vector3", "Vector4"};

std::string javaDouble(double d) {
    // Java Double.toString-ish (enough for vector printing): integral -> "1.0"
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
    char b[64];
    if (d == std::floor(d) && std::fabs(d) < 1e7) {
        std::snprintf(b, sizeof b, "%.1f", d);
    } else {
        std::snprintf(b, sizeof b, "%.17g", d);
        // shortest round-trip
        for (int p = 1; p <= 17; p++) {
            char t[64];
            std::snprintf(t, sizeof t, "%.*g", p, d);
            if (std::strtod(t, nullptr) == d) { std::snprintf(b, sizeof b, "%s", t); break; }
        }
    }
    return b;
}

VecUD* newVecUD(lua_State* L, int n) {
    auto* u = static_cast<VecUD*>(lua_newuserdata(L, sizeof(VecUD)));
    u->n = n;
    u->v[0] = u->v[1] = u->v[2] = u->v[3] = 0;
    luaL_setmetatable(L, mtName(gVecT[n]).c_str());
    return u;
}
VecUD* testVec(lua_State* L, int idx) {
    for (int n = 2; n <= 4; n++)
        if (auto* p = luaL_testudata(L, idx, mtName(gVecT[n]).c_str())) return static_cast<VecUD*>(p);
    return nullptr;
}
VecUD* checkVec(lua_State* L, int idx, char const* fn) {
    VecUD* u = testVec(L, idx);
    if (!u) luaL_error(L, "bad argument %d to '%s' (vector expected, got %s)", idx, fn, figuraTypeName(L, idx).c_str());
    return u;
}
int swz(char c) {
    switch (c) {
    case '1': case 'x': case 'r': return 0;
    case '2': case 'y': case 'g': return 1;
    case '3': case 'z': case 'b': return 2;
    case '4': case 'w': case 'a': return 3;
    case '_': return 4;
    default: return -1;
    }
}

int vecIndex(lua_State* L) { // fallback after methods: swizzles
    VecUD* u = checkVec(L, 1, "__index");
    if (lua_type(L, 2) == LUA_TNUMBER) {
        int i = int(lua_tointeger(L, 2));
        if (i >= 1 && i <= u->n) { lua_pushnumber(L, u->v[i - 1]); return 1; }
        return 0;
    }
    if (!lua_isstring(L, 2)) return 0;
    size_t      len;
    char const* s = lua_tolstring(L, 2, &len);
    if (len == 0 || len > 4) return 0;
    double vals[4];
    for (size_t i = 0; i < len; i++) {
        int k = swz(s[i]);
        if (k < 0 || (k < 4 && k >= u->n)) return 0;
        vals[i] = k == 4 ? 0.0 : u->v[k];
    }
    if (len == 1) { lua_pushnumber(L, vals[0]); return 1; }
    VecUD* r = newVecUD(L, int(len));
    for (size_t i = 0; i < len; i++) r->v[i] = vals[i];
    return 1;
}
int vecNewIndex(lua_State* L) {
    VecUD*      u = checkVec(L, 1, "__newindex");
    size_t      len;
    char const* key = luaL_checklstring(L, 2, &len);
    if (len == 1) {
        if (!lua_isnumber(L, 3)) return luaL_error(L, "Invalid call to __newindex - value assigned to key %s must be number.", key);
        int k = swz(key[0]);
        if (k < 0 || (k < 4 && k >= u->n)) return luaL_error(L, "Invalid key to vector __newindex: %s", key);
        if (k < 4) u->v[k] = lua_tonumber(L, 3);
        return 0;
    }
    VecUD* o = testVec(L, 3);
    if (o && size_t(o->n) == len) {
        double vals[4] = {o->v[0], o->v[1], o->v[2], o->v[3]};
        for (size_t i = 0; i < len; i++) {
            int k = swz(key[i]);
            if (k < 0 || (k < 4 && k >= u->n)) return luaL_error(L, "Invalid key to __newindex: invalid swizzle character: %c", key[i]);
            if (k < 4) u->v[k] = vals[i];
        }
        return 0;
    }
    return luaL_error(L, "Invalid call to __newindex - vector swizzles must be the same size.");
}

int vecToString(lua_State* L) {
    VecUD*      u = checkVec(L, 1, "__tostring");
    std::string s = "{";
    for (int i = 0; i < u->n; i++) {
        if (i) s += ", ";
        s += javaDouble(u->v[i]);
    }
    s += "}";
    lua_pushstring(L, s.c_str());
    return 1;
}

// binary arithmetic: vec op vec (same size), vec op number, number op vec (for add/mul)
template <class F>
int vecArith(lua_State* L, char const* name, F f, bool allowNumLeft) {
    VecUD* a = testVec(L, 1);
    VecUD* b = testVec(L, 2);
    if (a && b) {
        if (a->n != b->n) return luaL_error(L, "Invalid types to %s: Vector%d, Vector%d", name, a->n, b->n);
        VecUD* r = newVecUD(L, a->n);
        for (int i = 0; i < a->n; i++) r->v[i] = f(a->v[i], b->v[i]);
        return 1;
    }
    if (a && lua_isnumber(L, 2)) {
        double d = lua_tonumber(L, 2);
        VecUD* r = newVecUD(L, a->n);
        for (int i = 0; i < a->n; i++) r->v[i] = f(a->v[i], d);
        return 1;
    }
    if (b && lua_isnumber(L, 1) && allowNumLeft) {
        double d = lua_tonumber(L, 1);
        VecUD* r = newVecUD(L, b->n);
        for (int i = 0; i < b->n; i++) r->v[i] = f(d, b->v[i]);
        return 1;
    }
    return luaL_error(L, "Invalid types to %s: %s, %s", name, figuraTypeName(L, 1).c_str(), figuraTypeName(L, 2).c_str());
}
int vAdd(lua_State* L) { return vecArith(L, "__add", [](double a, double b) { return a + b; }, true); }
int vSub(lua_State* L) { return vecArith(L, "__sub", [](double a, double b) { return a - b; }, true); }
int vMul(lua_State* L);
int vDiv(lua_State* L) {
    if (lua_isnumber(L, 2) && testVec(L, 1) && lua_tonumber(L, 2) == 0) return luaL_error(L, "Attempt to divide vector by 0");
    return vecArith(L, "__div", [](double a, double b) { return a / b; }, true);
}
int vMod(lua_State* L) {
    if (lua_isnumber(L, 2) && testVec(L, 1) && lua_tonumber(L, 2) == 0) return luaL_error(L, "Attempt to reduce vector by 0");
    return vecArith(L, "__mod", [](double a, double b) { double m = std::fmod(a, b); if (m != 0 && ((m < 0) != (b < 0))) m += b; return m; }, false);
}
int vPow(lua_State* L) { return vecArith(L, "__pow", [](double a, double b) { return std::pow(a, b); }, false); }
int vUnm(lua_State* L) {
    VecUD* a = checkVec(L, 1, "__unm");
    VecUD* r = newVecUD(L, a->n);
    for (int i = 0; i < a->n; i++) r->v[i] = -a->v[i];
    return 1;
}
int vLen(lua_State* L) { lua_pushinteger(L, checkVec(L, 1, "__len")->n); return 1; }
int vEq(lua_State* L) {
    VecUD* a = testVec(L, 1);
    VecUD* b = testVec(L, 2);
    bool   eq = a && b && a->n == b->n;
    for (int i = 0; eq && i < a->n; i++) eq = a->v[i] == b->v[i];
    lua_pushboolean(L, eq);
    return 1;
}
int vLt(lua_State* L) {
    VecUD* a = checkVec(L, 1, "__lt");
    VecUD* b = checkVec(L, 2, "__lt");
    bool   r = a->n == b->n;
    for (int i = 0; r && i < a->n; i++) r = a->v[i] < b->v[i];
    lua_pushboolean(L, r);
    return 1;
}
int vLe(lua_State* L) {
    VecUD* a = checkVec(L, 1, "__le");
    VecUD* b = checkVec(L, 2, "__le");
    bool   r = a->n == b->n;
    for (int i = 0; r && i < a->n; i++) r = a->v[i] <= b->v[i];
    lua_pushboolean(L, r);
    return 1;
}

// read "vector or numbers" args starting at idx into out (n comps), default def
void readArgs(lua_State* L, int idx, int n, double* out, double def) {
    if (VecUD* o = testVec(L, idx)) {
        for (int i = 0; i < n; i++) out[i] = i < o->n ? o->v[i] : def;
        return;
    }
    for (int i = 0; i < n; i++) out[i] = optNum(L, idx + i, def);
}

#define VEC_SELF VecUD* u = checkVec(L, 1, __func__)
int mSet(lua_State* L) { VEC_SELF; readArgs(L, 2, u->n, u->v, 0); lua_settop(L, 1); return 1; }
int mReset(lua_State* L) { VEC_SELF; u->v[0] = u->v[1] = u->v[2] = u->v[3] = 0; lua_settop(L, 1); return 1; }
int mAdd(lua_State* L) { VEC_SELF; double a[4]; readArgs(L, 2, u->n, a, 0); for (int i = 0; i < u->n; i++) u->v[i] += a[i]; lua_settop(L, 1); return 1; }
int mSub(lua_State* L) { VEC_SELF; double a[4]; readArgs(L, 2, u->n, a, 0); for (int i = 0; i < u->n; i++) u->v[i] -= a[i]; lua_settop(L, 1); return 1; }
int mMul(lua_State* L) { VEC_SELF; double a[4]; readArgs(L, 2, u->n, a, 1); for (int i = 0; i < u->n; i++) u->v[i] *= a[i]; lua_settop(L, 1); return 1; }
int mDiv(lua_State* L) { VEC_SELF; double a[4]; readArgs(L, 2, u->n, a, 1); for (int i = 0; i < u->n; i++) u->v[i] /= a[i]; lua_settop(L, 1); return 1; }
int mReduce(lua_State* L) { VEC_SELF; double a[4]; readArgs(L, 2, u->n, a, 1); for (int i = 0; i < u->n; i++) { double m = std::fmod(u->v[i], a[i]); if (m != 0 && ((m < 0) != (a[i] < 0))) m += a[i]; u->v[i] = m; } lua_settop(L, 1); return 1; }
int mOffset(lua_State* L) { VEC_SELF; double d = luaL_checknumber(L, 2); for (int i = 0; i < u->n; i++) u->v[i] += d; lua_settop(L, 1); return 1; }
int mScale(lua_State* L) { VEC_SELF; double d = luaL_checknumber(L, 2); for (int i = 0; i < u->n; i++) u->v[i] *= d; lua_settop(L, 1); return 1; }
int mLength(lua_State* L) { VEC_SELF; double s = 0; for (int i = 0; i < u->n; i++) s += u->v[i] * u->v[i]; lua_pushnumber(L, std::sqrt(s)); return 1; }
int mLengthSq(lua_State* L) { VEC_SELF; double s = 0; for (int i = 0; i < u->n; i++) s += u->v[i] * u->v[i]; lua_pushnumber(L, s); return 1; }
int mCopy(lua_State* L) { VEC_SELF; VecUD* r = newVecUD(L, u->n); std::memcpy(r->v, u->v, sizeof r->v); return 1; }
int mNormalize(lua_State* L) { VEC_SELF; double s = 0; for (int i = 0; i < u->n; i++) s += u->v[i] * u->v[i]; s = std::sqrt(s); if (s != 0) for (int i = 0; i < u->n; i++) u->v[i] /= s; lua_settop(L, 1); return 1; }
int mNormalized(lua_State* L) { mCopy(L); lua_replace(L, 1); lua_settop(L, 1); return mNormalize(L); }
int mDot(lua_State* L) { VEC_SELF; VecUD* o = checkVec(L, 2, "dot"); double s = 0; for (int i = 0; i < std::min(u->n, o->n); i++) s += u->v[i] * o->v[i]; lua_pushnumber(L, s); return 1; }
int mCross(lua_State* L) {
    VEC_SELF; VecUD* o = checkVec(L, 2, "cross");
    double x = u->v[1] * o->v[2] - u->v[2] * o->v[1], y = u->v[2] * o->v[0] - u->v[0] * o->v[2], z = u->v[0] * o->v[1] - u->v[1] * o->v[0];
    u->v[0] = x; u->v[1] = y; u->v[2] = z; lua_settop(L, 1); return 1;
}
int mCrossed(lua_State* L) { mCopy(L); lua_replace(L, 1); return mCross(L); }
int mToRad(lua_State* L) { VEC_SELF; for (int i = 0; i < u->n; i++) u->v[i] = toRad(u->v[i]); lua_settop(L, 1); return 1; }
int mToDeg(lua_State* L) { VEC_SELF; for (int i = 0; i < u->n; i++) u->v[i] = toDeg(u->v[i]); lua_settop(L, 1); return 1; }
int mFloor(lua_State* L) { VEC_SELF; VecUD* r = newVecUD(L, u->n); for (int i = 0; i < u->n; i++) r->v[i] = std::floor(u->v[i]); return 1; }
int mCeil(lua_State* L) { VEC_SELF; VecUD* r = newVecUD(L, u->n); for (int i = 0; i < u->n; i++) r->v[i] = std::ceil(u->v[i]); return 1; }
int mUnpack(lua_State* L) { VEC_SELF; for (int i = 0; i < u->n; i++) lua_pushnumber(L, u->v[i]); return u->n; }
int mApplyFunc(lua_State* L) {
    VEC_SELF; luaL_checktype(L, 2, LUA_TFUNCTION);
    for (int i = 0; i < u->n; i++) { lua_pushvalue(L, 2); lua_pushnumber(L, u->v[i]); lua_pushinteger(L, i + 1); lua_call(L, 2, 1); u->v[i] = luaL_checknumber(L, -1); lua_pop(L, 1); }
    lua_settop(L, 1); return 1;
}
int mClampLength(lua_State* L) {
    VEC_SELF; double mn = optNum(L, 2, 0), mx = optNum(L, 3, HUGE_VAL);
    double s = 0; for (int i = 0; i < u->n; i++) s += u->v[i] * u->v[i]; s = std::sqrt(s);
    if (s > 0) { double t = std::clamp(s, mn, mx) / s; for (int i = 0; i < u->n; i++) u->v[i] *= t; }
    lua_settop(L, 1); return 1;
}
int mClamped(lua_State* L) { mCopy(L); lua_replace(L, 1); return mClampLength(L); }
int mAugmented(lua_State* L) {
    VEC_SELF; if (u->n >= 4) return luaL_error(L, "Cannot augment a Vector4");
    VecUD* r = newVecUD(L, u->n + 1); std::memcpy(r->v, u->v, sizeof r->v); r->v[u->n] = optNum(L, 2, 1); return 1;
}
int mSize(lua_State* L) { VEC_SELF; lua_pushinteger(L, u->n); return 1; }
int mToStringM(lua_State* L) { return vecToString(L); }
int mDistance(lua_State* L) { VEC_SELF; VecUD* o = checkVec(L, 2, "distanceTo"); double s = 0; for (int i = 0; i < u->n; i++) { double d = u->v[i] - o->v[i]; s += d * d; } lua_pushnumber(L, std::sqrt(s)); return 1; }
int mTransform(lua_State* L) {
    VEC_SELF; Mat4* m = toMat4(L, 2);
    if (!m) return luaL_error(L, "transform expects a matrix");
    if (u->n == 3) { Vec3 r = m->apply({u->v[0], u->v[1], u->v[2]}); u->v[0] = r.x; u->v[1] = r.y; u->v[2] = r.z; }
    else if (u->n == 4) {
        double x = u->v[0], y = u->v[1], z = u->v[2], w = u->v[3];
        u->v[0] = m->v11 * x + m->v12 * y + m->v13 * z + m->v14 * w;
        u->v[1] = m->v21 * x + m->v22 * y + m->v23 * z + m->v24 * w;
        u->v[2] = m->v31 * x + m->v32 * y + m->v33 * z + m->v34 * w;
        u->v[3] = m->v41 * x + m->v42 * y + m->v43 * z + m->v44 * w;
    }
    lua_settop(L, 1); return 1;
}
#undef VEC_SELF

int vMul(lua_State* L) {
    VecUD* a = testVec(L, 1);
    Mat4*  mb = toMat4(L, 2);
    if (a && mb && a->n >= 3) { // vec * mat (Figura: matrix applies to vector)
        VecUD* r = newVecUD(L, a->n); std::memcpy(r->v, a->v, sizeof r->v);
        lua_replace(L, 1); lua_settop(L, 2);
        return mTransform(L);
    }
    return vecArith(L, "__mul", [](double x, double y) { return x * y; }, true);
}

// ---------------------------------------------------------------- vectors API
int apiVec(lua_State* L) {
    int n = lua_gettop(L);
    if (n < 2 || n > 4) return luaL_error(L, "Invalid arguments to vec(), needs 2, 3, or 4 numbers.");
    VecUD* r = newVecUD(L, n);
    for (int i = 0; i < n; i++) r->v[i] = luaL_checknumber(L, i + 1);
    return 1;
}
template <int N>
int apiVecN(lua_State* L) {
    VecUD* r = newVecUD(L, N);
    for (int i = 0; i < N; i++) r->v[i] = optNum(L, i + 1, 0);
    return 1;
}
void hsvToRgb(double h, double s, double v, double* out) {
    h = std::fmod(h, 1.0); if (h < 0) h += 1;
    int    i = int(h * 6); double f = h * 6 - i, p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    double r, g, b;
    switch (i % 6) { case 0: r = v; g = t; b = p; break; case 1: r = q; g = v; b = p; break; case 2: r = p; g = v; b = t; break;
                     case 3: r = p; g = q; b = v; break; case 4: r = t; g = p; b = v; break; default: r = v; g = p; b = q; }
    out[0] = r; out[1] = g; out[2] = b;
}
int apiHsvToRgb(lua_State* L) { double a[3]; readArgs(L, 1, 3, a, 0); VecUD* r = newVecUD(L, 3); hsvToRgb(a[0], a[1], a[2], r->v); return 1; }
int apiRgbToHsv(lua_State* L) {
    double a[3]; readArgs(L, 1, 3, a, 0);
    double mx = std::max({a[0], a[1], a[2]}), mn = std::min({a[0], a[1], a[2]}), d = mx - mn, h = 0;
    if (d != 0) { if (mx == a[0]) h = std::fmod((a[1] - a[2]) / d, 6.0); else if (mx == a[1]) h = (a[2] - a[0]) / d + 2; else h = (a[0] - a[1]) / d + 4; h /= 6; if (h < 0) h += 1; }
    VecUD* r = newVecUD(L, 3); r->v[0] = h; r->v[1] = mx == 0 ? 0 : d / mx; r->v[2] = mx; return 1;
}
int apiHexToRgb(lua_State* L) {
    std::string s = optStr(L, 1, "");
    if (!s.empty() && s[0] == '#') s.erase(0, 1);
    if (s.size() == 3) s = std::string{s[0], s[0], s[1], s[1], s[2], s[2]};
    unsigned v = 0; std::sscanf(s.c_str(), "%x", &v);
    VecUD* r = newVecUD(L, 3); r->v[0] = ((v >> 16) & 255) / 255.0; r->v[1] = ((v >> 8) & 255) / 255.0; r->v[2] = (v & 255) / 255.0; return 1;
}
int apiRgbToHex(lua_State* L) {
    double a[3]; readArgs(L, 1, 3, a, 0); char b[16];
    std::snprintf(b, sizeof b, "%02x%02x%02x", int(std::clamp(a[0], 0.0, 1.0) * 255), int(std::clamp(a[1], 0.0, 1.0) * 255), int(std::clamp(a[2], 0.0, 1.0) * 255));
    lua_pushstring(L, b); return 1;
}
int apiIntToRgb(lua_State* L) { long long v = (long long)luaL_checknumber(L, 1); VecUD* r = newVecUD(L, 3); r->v[0] = ((v >> 16) & 255) / 255.0; r->v[1] = ((v >> 8) & 255) / 255.0; r->v[2] = (v & 255) / 255.0; return 1; }
int apiRgbToInt(lua_State* L) { double a[3]; readArgs(L, 1, 3, a, 0); lua_pushinteger(L, (int(a[0] * 255) & 255) << 16 | (int(a[1] * 255) & 255) << 8 | (int(a[2] * 255) & 255)); return 1; }
int apiAngleToDir(lua_State* L) {
    double a[2]; readArgs(L, 1, 2, a, 0);
    double p = toRad(a[0]), y = toRad(a[1]);
    VecUD* r = newVecUD(L, 3); r->v[0] = -std::sin(y) * std::cos(p); r->v[1] = -std::sin(p); r->v[2] = std::cos(y) * std::cos(p); return 1;
}
int apiRotateAroundAxis(lua_State* L) {
    double ang = luaL_checknumber(L, 1);
    double v[3]; readArgs(L, 2, 3, v, 0);
    int ai = testVec(L, 2) ? 3 : 5;
    double ax[3]; readArgs(L, ai, 3, ax, 0);
    double len = std::sqrt(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]); if (len == 0) len = 1;
    for (double& d : ax) d /= len;
    double c = std::cos(toRad(ang)), s = std::sin(toRad(ang)), dot = v[0] * ax[0] + v[1] * ax[1] + v[2] * ax[2];
    double cr[3] = {ax[1] * v[2] - ax[2] * v[1], ax[2] * v[0] - ax[0] * v[2], ax[0] * v[1] - ax[1] * v[0]};
    VecUD* r = newVecUD(L, 3);
    for (int i = 0; i < 3; i++) r->v[i] = v[i] * c + cr[i] * s + ax[i] * dot * (1 - c);
    return 1;
}
} // namespace

uint32_t vecType(int n) { return gVecT[n]; }

void pushVec(lua_State* L, double const* v, int n) {
    VecUD* r = newVecUD(L, n);
    for (int i = 0; i < n; i++) r->v[i] = v[i];
}
double* toVec(lua_State* L, int idx, int* n) {
    VecUD* u = testVec(L, idx);
    if (!u) return nullptr;
    if (n) *n = u->n;
    return u->v;
}
Vec3 parseVec3(lua_State* L, int idx, char const* fn, double def) {
    if (VecUD* u = testVec(L, idx)) {
        if (u->n != 3) luaL_error(L, "Illegal argument to %s(): vector of size 3 expected", fn);
        return {u->v[0], u->v[1], u->v[2]};
    }
    if (!lua_isnoneornil(L, idx) && !lua_isnumber(L, idx))
        luaL_error(L, "Illegal argument to %s(): %s", fn, figuraTypeName(L, idx).c_str());
    return {optNum(L, idx, def), optNum(L, idx + 1, def), optNum(L, idx + 2, def)};
}
bool optVec3(lua_State* L, int idx, Vec3* out) {
    if (lua_isnoneornil(L, idx) && lua_isnoneornil(L, idx + 1) && lua_isnoneornil(L, idx + 2)) return false;
    *out = parseVec3(L, idx, "vec3", 0);
    return true;
}

void openVectors(lua_State* L) {
    for (int n = 2; n <= 4; n++) {
        TypeDef d;
        d.name    = kVecNames[n];
        d.methods = {{"set", mSet}, {"reset", mReset}, {"add", mAdd}, {"sub", mSub}, {"mul", mMul}, {"div", mDiv},
                     {"reduce", mReduce}, {"offset", mOffset}, {"scale", mScale}, {"length", mLength},
                     {"lengthSquared", mLengthSq}, {"copy", mCopy}, {"normalize", mNormalize}, {"normalized", mNormalized},
                     {"dot", mDot}, {"toRad", mToRad}, {"toDeg", mToDeg}, {"floor", mFloor}, {"ceil", mCeil},
                     {"unpack", mUnpack}, {"applyFunc", mApplyFunc}, {"clampLength", mClampLength}, {"clamped", mClamped},
                     {"augmented", mAugmented}, {"size", mSize}, {"toString", mToStringM}, {"transform", mTransform}};
        if (n == 3) {
            d.methods.push_back({"cross", mCross});
            d.methods.push_back({"crossed", mCrossed});
            d.methods.push_back({"distanceTo", mDistance});
        }
        d.index    = vecIndex;
        d.newindex = vecNewIndex;
        d.tostring = vecToString;
        d.meta     = {{"__add", vAdd}, {"__sub", vSub}, {"__mul", vMul}, {"__div", vDiv}, {"__mod", vMod}, {"__pow", vPow},
                      {"__unm", vUnm}, {"__len", vLen}, {"__eq", vEq}, {"__lt", vLt}, {"__le", vLe}};
        gVecT[n] = registerType(L, d);
    }
    luaL_Reg api[] = {{"vec", apiVec}, {"vec2", apiVecN<2>}, {"vec3", apiVecN<3>}, {"vec4", apiVecN<4>},
                      {"hsvToRGB", apiHsvToRgb}, {"rgbToHSV", apiRgbToHsv}, {"hexToRGB", apiHexToRgb},
                      {"rgbToHex", apiRgbToHex}, {"intToRGB", apiIntToRgb}, {"rgbToInt", apiRgbToInt},
                      {"angleToDir", apiAngleToDir}, {"rotateAroundAxis", apiRotateAroundAxis}, {nullptr, nullptr}};
    lua_newtable(L);
    luaL_setfuncs(L, api, 0);
    lua_setglobal(L, "vectors");
}

// =====================================================================================================================
// Matrices (FiguraMat4, minimal but enough for matrix uses in avatars)
// =====================================================================================================================
namespace {
uint32_t gMat4T = 0;
Mat4*    checkMat(lua_State* L, int i, char const* fn) {
    Mat4* m = toMat4(L, i);
    if (!m) luaL_error(L, "bad argument %d to '%s' (Matrix4 expected)", i, fn);
    return m;
}
int matIndex(lua_State* L) {
    Mat4* m = checkMat(L, 1, "__index");
    if (lua_type(L, 2) == LUA_TNUMBER) { // column vector
        int c = int(lua_tointeger(L, 2)) - 1;
        if (c < 0 || c > 3) return 0;
        pushVec4(L, m->at(0, c), m->at(1, c), m->at(2, c), m->at(3, c));
        return 1;
    }
    size_t n; char const* k = lua_tolstring(L, 2, &n);
    if (k && n == 3 && k[0] == 'v') { int r = k[1] - '1', c = k[2] - '1'; if (r >= 0 && r < 4 && c >= 0 && c < 4) { lua_pushnumber(L, m->at(r, c)); return 1; } }
    return 0;
}
int matNewIndex(lua_State* L) {
    Mat4* m = checkMat(L, 1, "__newindex");
    size_t n; char const* k = luaL_checklstring(L, 2, &n);
    if (n == 3 && k[0] == 'v') { int r = k[1] - '1', c = k[2] - '1'; if (r >= 0 && r < 4 && c >= 0 && c < 4) { m->at(r, c) = luaL_checknumber(L, 3); return 0; } }
    return luaL_error(L, "Cannot assign value on key \"%s\"", k);
}
int matToString(lua_State* L) {
    Mat4* m = checkMat(L, 1, "__tostring"); std::string s = "[";
    for (int r = 0; r < 4; r++) { s += "\n  "; for (int c = 0; c < 4; c++) { s += javaDouble(m->at(r, c)); if (c < 3) s += ", "; } }
    s += "\n]"; lua_pushstring(L, s.c_str()); return 1;
}
int matMul(lua_State* L) {
    Mat4* a = toMat4(L, 1); Mat4* b = toMat4(L, 2);
    if (a && b) { pushMat4(L, *a * *b); return 1; }
    if (a && lua_isnumber(L, 2)) { Mat4 r = *a; double d = lua_tonumber(L, 2); for (int i = 0; i < 16; i++) (&r.v11)[i] *= d; pushMat4(L, r); return 1; }
    int n; double* v = toVec(L, 2, &n);
    if (a && v) { Vec3 r = a->apply({v[0], v[1], v[2]}); pushVec3(L, r); return 1; }
    return luaL_error(L, "Invalid types to __mul");
}
int mmTranslate(lua_State* L) { Mat4* m = checkMat(L, 1, "translate"); Vec3 v = parseVec3(L, 2, "translate"); m->translate(v.x, v.y, v.z); lua_settop(L, 1); return 1; }
int mmScale(lua_State* L) { Mat4* m = checkMat(L, 1, "scale"); Vec3 v = parseVec3(L, 2, "scale", 1); m->scale(v); lua_settop(L, 1); return 1; }
int mmRotate(lua_State* L) { Mat4* m = checkMat(L, 1, "rotate"); Vec3 v = parseVec3(L, 2, "rotate"); m->rotateZYX(v.x, v.y, v.z); lua_settop(L, 1); return 1; }
int mmRotateX(lua_State* L) { Mat4* m = checkMat(L, 1, "rotateX"); m->rotateX(luaL_checknumber(L, 2)); lua_settop(L, 1); return 1; }
int mmRotateY(lua_State* L) { Mat4* m = checkMat(L, 1, "rotateY"); m->rotateY(luaL_checknumber(L, 2)); lua_settop(L, 1); return 1; }
int mmRotateZ(lua_State* L) { Mat4* m = checkMat(L, 1, "rotateZ"); m->rotateZ(luaL_checknumber(L, 2)); lua_settop(L, 1); return 1; }
int mmCopy(lua_State* L) { pushMat4(L, *checkMat(L, 1, "copy")); return 1; }
int mmReset(lua_State* L) { checkMat(L, 1, "reset")->reset(); lua_settop(L, 1); return 1; }
int mmApply(lua_State* L) { Mat4* m = checkMat(L, 1, "apply"); Vec3 v = parseVec3(L, 2, "apply"); pushVec3(L, m->apply(v)); return 1; }
int mmMultiply(lua_State* L) { Mat4* m = checkMat(L, 1, "multiply"); Mat4* o = checkMat(L, 2, "multiply"); *m = *o * *m; lua_settop(L, 1); return 1; }
int mmRightMultiply(lua_State* L) { Mat4* m = checkMat(L, 1, "rightMultiply"); Mat4* o = checkMat(L, 2, "rightMultiply"); *m = *m * *o; lua_settop(L, 1); return 1; }
int apiMat4(lua_State* L) { Mat4 m; for (int c = 0; c < 4; c++) if (double* v = toVec(L, c + 1, nullptr)) for (int r = 0; r < 4; r++) m.at(r, c) = v[r]; pushMat4(L, m); return 1; }
int apiTranslate4(lua_State* L) { Mat4 m; Vec3 v = parseVec3(L, 1, "translate4"); m.translate(v.x, v.y, v.z); pushMat4(L, m); return 1; }
int apiScale4(lua_State* L) { Mat4 m; Vec3 v = parseVec3(L, 1, "scale4", 1); m.scale(v); pushMat4(L, m); return 1; }
int apiRotation4(lua_State* L) { Mat4 m; Vec3 v = parseVec3(L, 1, "rotation4"); m.rotateZYX(v.x, v.y, v.z); pushMat4(L, m); return 1; }
} // namespace

void pushMat4(lua_State* L, Mat4 const& m) {
    auto* p = static_cast<Mat4*>(lua_newuserdata(L, sizeof(Mat4)));
    *p      = m;
    luaL_setmetatable(L, mtName(gMat4T).c_str());
}
Mat4* toMat4(lua_State* L, int idx) {
    if (!gMat4T) return nullptr;
    return static_cast<Mat4*>(luaL_testudata(L, idx, mtName(gMat4T).c_str()));
}
void openMatrices(lua_State* L) {
    TypeDef d;
    d.name     = "Matrix4";
    d.methods  = {{"translate", mmTranslate}, {"scale", mmScale}, {"rotate", mmRotate}, {"rotateX", mmRotateX},
                  {"rotateY", mmRotateY}, {"rotateZ", mmRotateZ}, {"copy", mmCopy}, {"reset", mmReset},
                  {"apply", mmApply}, {"multiply", mmMultiply}, {"rightMultiply", mmRightMultiply}};
    d.index    = matIndex;
    d.newindex = matNewIndex;
    d.tostring = matToString;
    d.meta     = {{"__mul", matMul}};
    gMat4T     = registerType(L, d);
    luaL_Reg api[] = {{"mat4", apiMat4}, {"translate4", apiTranslate4}, {"scale4", apiScale4},
                      {"rotation4", apiRotation4}, {nullptr, nullptr}};
    lua_newtable(L);
    luaL_setfuncs(L, api, 0);
    lua_setglobal(L, "matrices");
}

} // namespace bf::figura::lua
