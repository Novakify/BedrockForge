// FiguraBridge math API - port of Figura 0.1.6 org.figuramc.figura.math.vector.FiguraVec2/3/4,
// math.matrix.FiguraMat2/3/4, lua.api.math.VectorsAPI / MatricesAPI and the bits of utils.LuaUtils / MathUtils /
// ColorUtils they use. Semantics follow the Java code line by line: argument conversion like LuaTypeManager (nil ->
// 0 for primitive doubles, nil -> default for boxed Doubles, numeric strings accepted as numbers), mutate-vs-copy,
// cached determinant / inverse quirks, error messages, number formatting (DecimalFormat "0.#####", RoundingMode.DOWN).
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <string>

#include "figura/core/Avatar.h"
#include "figura/core/LuaBind.h"

namespace bf::figura::lua {

namespace {

// ---------------------------------------------------------------------------------------------------------------------
// Java helpers
// ---------------------------------------------------------------------------------------------------------------------
constexpr double kDegToRad = 0.017453292519943295; // Math.PI / 180 == Math.toRadians factor (Java 9+)
constexpr double kRadToDeg = 57.29577951308232;    // 180 / Math.PI == Math.toDegrees factor
constexpr double kJavaMinValue = 4.9e-324;         // Double.MIN_VALUE

int32_t jD2I(double d) {
    if (std::isnan(d)) return 0;
    if (d >= 2147483647.0) return INT32_MAX;
    if (d <= -2147483648.0) return INT32_MIN;
    return int32_t(d);
}
int64_t jD2L(double d) {
    if (std::isnan(d)) return 0;
    if (d >= 9.2233720368547758e18) return INT64_MAX;
    if (d <= -9.2233720368547758e18) return INT64_MIN;
    return int64_t(d);
}
double jMod(double a, double b) { return std::fmod(a, b); } // Java % on doubles == C fmod

/// Shortest round-trip decimal digits of |d| -> (digits, exponent of first digit).
void shortestDigits(double d, std::string& digits, int& exp10) {
    char buf[64];
    for (int p = 1; p <= 17; p++) {
        std::snprintf(buf, sizeof buf, "%.*e", p - 1, d);
        if (std::strtod(buf, nullptr) == d) break;
    }
    digits.clear();
    char const* s = buf;
    for (; *s && *s != 'e'; s++)
        if (*s >= '0' && *s <= '9') digits += *s;
    exp10 = *s == 'e' ? std::atoi(s + 1) : 0;
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
}

/// FiguraLuaPrinter.df: new DecimalFormat("0.#####") with RoundingMode.DOWN (LOG_NUMBER_LENGTH default 5).
std::string jDf(double d) {
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d > 0 ? "\xE2\x88\x9E" : "-\xE2\x88\x9E";
    bool        neg = std::signbit(d);
    double      a   = std::fabs(d);
    std::string ip = "0", fp;
    if (a != 0) {
        std::string dg;
        int         e;
        shortestDigits(a, dg, e);
        int point = e + 1; // digits before the decimal point
        if (point <= 0) {
            fp = std::string(size_t(-point), '0') + dg;
        } else if (size_t(point) >= dg.size()) {
            ip = dg + std::string(size_t(point) - dg.size(), '0');
        } else {
            ip = dg.substr(0, size_t(point));
            fp = dg.substr(size_t(point));
        }
    }
    if (fp.size() > 5) fp.resize(5);
    while (!fp.empty() && fp.back() == '0') fp.pop_back();
    std::string r = neg ? "-" : "";
    r += ip;
    if (!fp.empty()) r += "." + fp;
    return r;
}

/// Java Double.toString-ish (for messages that print a raw value).
std::string jDoubleStr(double d) {
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
    if (d == 0) return std::signbit(d) ? "-0.0" : "0.0";
    double a = std::fabs(d);
    std::string dg;
    int e;
    shortestDigits(a, dg, e);
    std::string r = std::signbit(d) ? "-" : "";
    if (a >= 1e-3 && a < 1e7) {
        int point = e + 1;
        if (point <= 0) r += "0." + std::string(size_t(-point), '0') + dg;
        else if (size_t(point) >= dg.size()) r += dg + std::string(size_t(point) - dg.size(), '0') + ".0";
        else r += dg.substr(0, size_t(point)) + "." + dg.substr(size_t(point));
    } else {
        r += dg.substr(0, 1) + "." + (dg.size() > 1 ? dg.substr(1) : "0") + "E" + std::to_string(e);
    }
    return r;
}

// ---------------------------------------------------------------------------------------------------------------------
// value userdata
// ---------------------------------------------------------------------------------------------------------------------
constexpr uint64_t kVecMagic = 0x46625665634D6167ull; // "FbVecMag"
constexpr uint64_t kMatMagic = 0x46624D61744D6167ull; // "FbMatMag"

struct VecUD {
    double   v[4];
    uint64_t magic;
    int      n;
};
/// m first so toMat4 can hand out a Mat4* (16 doubles, row-major v11..v44). For n < 4 the first n*n doubles hold the
/// matrix row-major with stride n.
struct MatUD {
    Mat4     m;
    uint64_t magic;
    int      n;
    double   cDet;     // FiguraMatrix.cachedDeterminant (NaN = none)
    bool     hasInv;   // FiguraMatrix.cachedInverse != null
    double   inv[16];
    double   snap[16]; // values the caches belong to (guards against C++ code writing through toMat4)
    double*       d() { return &m.v11; }
    double const* d() const { return &m.v11; }
    double&       at(int r, int c) { return d()[r * n + c]; }
    double        at(int r, int c) const { return d()[r * n + c]; }
};

char const kVecMtKey[5] = {};
char const kMatMtKey[5] = {};
uint32_t   gVecT[5]     = {};
uint32_t   gMatT[5]     = {};
uint32_t   gVectorsApiT = 0, gMatricesApiT = 0;
struct ApiObj { int dummy; };
ApiObj     gVectorsApi, gMatricesApi;

void pushKeyed(lua_State* L, void const* key) {
    lua_pushlightuserdata(L, const_cast<void*>(key));
    lua_rawget(L, LUA_REGISTRYINDEX);
}

VecUD* testVec(lua_State* L, int idx) {
    if (lua_type(L, idx) != LUA_TUSERDATA || lua_rawlen(L, idx) != sizeof(VecUD)) return nullptr;
    auto* u = static_cast<VecUD*>(lua_touserdata(L, idx));
    return u->magic == kVecMagic ? u : nullptr;
}
VecUD* testVecN(lua_State* L, int idx, int n) {
    VecUD* u = testVec(L, idx);
    return u && u->n == n ? u : nullptr;
}
MatUD* testMat(lua_State* L, int idx) {
    if (lua_type(L, idx) != LUA_TUSERDATA || lua_rawlen(L, idx) != sizeof(MatUD)) return nullptr;
    auto* u = static_cast<MatUD*>(lua_touserdata(L, idx));
    return u->magic == kMatMagic ? u : nullptr;
}
MatUD* testMatN(lua_State* L, int idx, int n) {
    MatUD* u = testMat(L, idx);
    return u && u->n == n ? u : nullptr;
}

VecUD* newVec(lua_State* L, int n) {
    auto* u  = static_cast<VecUD*>(lua_newuserdata(L, sizeof(VecUD)));
    u->v[0]  = u->v[1] = u->v[2] = u->v[3] = 0;
    u->magic = kVecMagic;
    u->n     = n;
    pushKeyed(L, &kVecMtKey[n]);
    lua_setmetatable(L, -2);
    return u;
}
VecUD* newVec(lua_State* L, int n, double const* vals) {
    VecUD* u = newVec(L, n);
    for (int i = 0; i < n; i++) u->v[i] = vals[i];
    return u;
}
VecUD* newVec2(lua_State* L, double x, double y) { double a[2] = {x, y}; return newVec(L, 2, a); }
VecUD* newVec3(lua_State* L, double x, double y, double z) { double a[3] = {x, y, z}; return newVec(L, 3, a); }
VecUD* newVec4(lua_State* L, double x, double y, double z, double w) { double a[4] = {x, y, z, w}; return newVec(L, 4, a); }

MatUD* newMat(lua_State* L, int n) { // identity
    auto* u = static_cast<MatUD*>(lua_newuserdata(L, sizeof(MatUD)));
    std::memset(static_cast<void*>(u), 0, sizeof(MatUD));
    u->magic = kMatMagic;
    u->n     = n;
    for (int i = 0; i < n; i++) u->at(i, i) = 1;
    u->cDet   = std::numeric_limits<double>::quiet_NaN();
    u->hasInv = false;
    pushKeyed(L, &kMatMtKey[n]);
    lua_setmetatable(L, -2);
    return u;
}
MatUD* newMatCopy(lua_State* L, MatUD const* o) { // FiguraMatN.copy(): fresh caches
    MatUD* u = newMat(L, o->n);
    std::memcpy(u->d(), o->d(), sizeof(double) * size_t(o->n * o->n));
    return u;
}

// ---- matrix cache (FiguraMatrix.cachedInverse / cachedDeterminant) ----
void snapshot(MatUD* u) { std::memcpy(u->snap, u->d(), sizeof(double) * size_t(u->n * u->n)); }
void invalidate(MatUD* u) {
    u->cDet   = std::numeric_limits<double>::quiet_NaN();
    u->hasInv = false;
}
void guard(MatUD* u) { // values changed behind our back -> caches are meaningless
    if ((!std::isnan(u->cDet) || u->hasInv) && std::memcmp(u->snap, u->d(), sizeof(double) * size_t(u->n * u->n)) != 0)
        invalidate(u);
}

// ---------------------------------------------------------------------------------------------------------------------
// LuaTypeManager-style argument conversion
// ---------------------------------------------------------------------------------------------------------------------
/// FiguraDocsManager.getNameFor(value class) / LuaValue.typename()
std::string actualType(lua_State* L, int idx) {
    if (lua_type(L, idx) == LUA_TUSERDATA) return figuraTypeName(L, idx);
    return luaL_typename(L, idx);
}
/// Class.getSimpleName() of LuaTypeManager.luaToJava(value)
std::string simpleName(lua_State* L, int idx) {
    switch (lua_type(L, idx)) {
    case LUA_TNIL: case LUA_TNONE: return "null";
    case LUA_TNUMBER: { double d = lua_tonumber(L, idx); return d == std::floor(d) && std::fabs(d) < 2147483648.0 ? "Integer" : "Double"; }
    case LUA_TSTRING: if (lua_isnumber(L, idx)) { double d = lua_tonumber(L, idx); return d == std::floor(d) && std::fabs(d) < 2147483648.0 ? "Integer" : "Double"; } return "String";
    case LUA_TBOOLEAN: return "Boolean";
    case LUA_TTABLE: return "LuaTable";
    case LUA_TFUNCTION: return lua_iscfunction(L, idx) ? "LuaFunction" : "LuaClosure";
    case LUA_TUSERDATA:
        if (VecUD* v = testVec(L, idx)) return "FiguraVec" + std::to_string(v->n);
        if (MatUD* m = testMat(L, idx)) return "FiguraMat" + std::to_string(m->n);
        return figuraTypeName(L, idx);
    default: return luaL_typename(L, idx);
    }
}
std::string vecToString(VecUD const* u);
std::string matToString(MatUD const* u);
/// String.valueOf(luaToJava(value)) (for messages that print the value)
std::string javaValueString(lua_State* L, int idx) {
    switch (lua_type(L, idx)) {
    case LUA_TNIL: case LUA_TNONE: return "null";
    case LUA_TNUMBER: case LUA_TSTRING:
        if (lua_isnumber(L, idx)) {
            double d = lua_tonumber(L, idx);
            if (d == std::floor(d) && std::fabs(d) < 2147483648.0) return std::to_string(int64_t(d));
            return jDoubleStr(d);
        }
        return lua_tostring(L, idx);
    case LUA_TBOOLEAN: return lua_toboolean(L, idx) ? "true" : "false";
    case LUA_TUSERDATA:
        if (VecUD* v = testVec(L, idx)) return vecToString(v);
        if (MatUD* m = testMat(L, idx)) return matToString(m);
        return figuraTypeName(L, idx);
    default: {
        char b[64];
        std::snprintf(b, sizeof b, "%s: %p", luaL_typename(L, idx), lua_topointer(L, idx));
        return b;
    }
    }
}

[[noreturn]] void badArg(lua_State* L, int idx, char const* fn, char const* expected) {
    raise(L, std::string("Invalid argument ") + std::to_string(idx) + " to function " + fn + ". Expected " + expected +
                 ", but got " + actualType(L, idx));
}
[[noreturn]] void notNil(lua_State* L, int idx, char const* fn, char const* expected) {
    raise(L, std::string("bad argument: ") + fn + " " + std::to_string(idx) + " do not allow nil values, expected " + expected);
}
/// `double` parameter: nil -> 0
double jDouble(lua_State* L, int idx, char const* fn) {
    if (lua_isnoneornil(L, idx)) return 0;
    if (!lua_isnumber(L, idx)) badArg(L, idx, fn, "Number");
    return lua_tonumber(L, idx);
}
/// `Double` parameter: nil -> nullopt
std::optional<double> jOptDouble(lua_State* L, int idx, char const* fn) {
    if (lua_isnoneornil(L, idx)) return std::nullopt;
    if (!lua_isnumber(L, idx)) badArg(L, idx, fn, "Number");
    return lua_tonumber(L, idx);
}
/// `int` parameter (LuaValue.checkint): nil -> 0
int jInt(lua_State* L, int idx, char const* fn) {
    if (lua_isnoneornil(L, idx)) return 0;
    if (!lua_isnumber(L, idx)) badArg(L, idx, fn, "Integer");
    return int32_t(jD2L(lua_tonumber(L, idx)));
}
std::string vecName(int n) { return "Vector" + std::to_string(n); }
std::string matName(int n) { return "Matrix" + std::to_string(n); }
/// FiguraVecN parameter (nullable unless notNil)
VecUD* jVec(lua_State* L, int idx, int n, char const* fn, bool required) {
    if (lua_isnoneornil(L, idx)) {
        if (required) notNil(L, idx, fn, vecName(n).c_str());
        return nullptr;
    }
    VecUD* u = testVecN(L, idx, n);
    if (!u) badArg(L, idx, fn, vecName(n).c_str());
    return u;
}
MatUD* jMat(lua_State* L, int idx, int n, char const* fn, bool required) {
    if (lua_isnoneornil(L, idx)) {
        if (required) notNil(L, idx, fn, matName(n).c_str());
        return nullptr;
    }
    MatUD* u = testMatN(L, idx, n);
    if (!u) badArg(L, idx, fn, matName(n).c_str());
    return u;
}
/// instance method receiver
[[noreturn]] void colonError(lua_State* L, char const* fn, std::string const& type) {
    raise(L, std::string("Use a colon (:) to call ") + fn + " on a " + type + ", instead of a dot.\nFor example, change ." +
                 fn + "( to :" + fn + "(");
}
[[noreturn]] void operatorSelfError(lua_State* L, char const* op, bool binary, std::string const& type) {
    raise(L, std::string(binary ? "Expected left-hand side of " : "Expected subject of ") + op + " to be a " + type +
                 "; actually got " + actualType(L, 1));
}
template <int N> VecUD* selfVec(lua_State* L, char const* fn) {
    VecUD* u = testVecN(L, 1, N);
    if (!u) colonError(L, fn, vecName(N));
    return u;
}
template <int N> VecUD* selfVecOp(lua_State* L, char const* op, bool binary) {
    VecUD* u = testVecN(L, 1, N);
    if (!u) operatorSelfError(L, op, binary, vecName(N));
    return u;
}
template <int N> MatUD* selfMat(lua_State* L, char const* fn) {
    MatUD* u = testMatN(L, 1, N);
    if (!u) colonError(L, fn, matName(N));
    guard(u);
    return u;
}
template <int N> MatUD* selfMatOp(lua_State* L, char const* op, bool binary) {
    MatUD* u = testMatN(L, 1, N);
    if (!u) operatorSelfError(L, op, binary, matName(N));
    guard(u);
    return u;
}
/// "Object" parameter kinds after luaToJava
bool isJNumber(lua_State* L, int idx) { return lua_type(L, idx) == LUA_TNUMBER || (lua_type(L, idx) == LUA_TSTRING && lua_isnumber(L, idx)); }

/// LuaUtils.parseVec2(fn, x, y, dx, dy)
void parseVec2(lua_State* L, int xi, std::optional<double> y, char const* fn, double* out, double dx = 0, double dy = 0) {
    if (VecUD* v = testVecN(L, xi, 2)) { out[0] = v->v[0]; out[1] = v->v[1]; return; }
    if (lua_isnoneornil(L, xi) || isJNumber(L, xi)) {
        out[0] = lua_isnoneornil(L, xi) ? dx : lua_tonumber(L, xi);
        out[1] = y ? *y : dy;
        return;
    }
    raise(L, std::string("Illegal argument to ") + fn + "(): " + simpleName(L, xi));
}
/// LuaUtils.parseVec3(fn, x, y, z, dx, dy, dz)
void parseVec3J(lua_State* L, int xi, std::optional<double> y, std::optional<double> z, char const* fn, double* out,
                double dx = 0, double dy = 0, double dz = 0) {
    if (VecUD* v = testVecN(L, xi, 3)) { out[0] = v->v[0]; out[1] = v->v[1]; out[2] = v->v[2]; return; }
    if (lua_isnoneornil(L, xi) || isJNumber(L, xi)) {
        out[0] = lua_isnoneornil(L, xi) ? dx : lua_tonumber(L, xi);
        out[1] = y ? *y : dy;
        out[2] = z ? *z : dz;
        return;
    }
    raise(L, std::string("Illegal argument to ") + fn + "(): " + simpleName(L, xi));
}
/// LuaUtils.parseOneArgVec: a single number fills all three components
void parseOneArgVec(lua_State* L, int xi, std::optional<double> y, std::optional<double> z, char const* fn, double* out, double def) {
    double d = isJNumber(L, xi) ? lua_tonumber(L, xi) : def;
    parseVec3J(L, xi, y, z, fn, out, d, d, d);
}
/// (Object x, Double y, Double z) signature: args at xi, xi+1, xi+2
void argsVec3(lua_State* L, int xi, char const* fn, double* out) {
    auto y = jOptDouble(L, xi + 1, fn), z = jOptDouble(L, xi + 2, fn);
    parseVec3J(L, xi, y, z, fn, out);
}
/// static API functions accept `api:fn(...)` too (LuaTypeManager "dirty hack")
void dropApiSelf(lua_State* L, ApiObj* api, uint32_t type) {
    if (lua_type(L, 1) == LUA_TUSERDATA && toRef(L, 1, type) == api) lua_remove(L, 1);
}

// ---------------------------------------------------------------------------------------------------------------------
// vectors
// ---------------------------------------------------------------------------------------------------------------------
std::string vecToString(VecUD const* u) {
    std::string s = "{";
    for (int i = 0; i < u->n; i++) {
        s += jDf(u->v[i]);
        if (i < u->n - 1) s += ", ";
    }
    return s + "}";
}
int swz(char c, int n) { // component index, 4 = '_', -1 = invalid for this size
    int k;
    switch (c) {
    case '1': case 'x': case 'r': k = 0; break;
    case '2': case 'y': case 'g': k = 1; break;
    case '3': case 'z': case 'b': k = 2; break;
    case '4': case 'w': case 'a': k = 3; break;
    case '_': return 4;
    default: return -1;
    }
    return k < n ? k : -1;
}
/// key -> Java String (arg.toString() of luaToJava(key)); false if not convertible
bool keyString(lua_State* L, int idx, std::string& out) {
    if (lua_type(L, idx) == LUA_TNUMBER) {
        double d = lua_tonumber(L, idx);
        if (d == std::floor(d) && std::fabs(d) < 2147483648.0) out = std::to_string(int64_t(d));
        else out = jDoubleStr(d);
        return true;
    }
    if (lua_type(L, idx) == LUA_TSTRING) {
        size_t      n;
        char const* s = lua_tolstring(L, idx, &n);
        if (lua_isnumber(L, idx)) { // luaToJava turns numeric strings into numbers
            double d = lua_tonumber(L, idx);
            if (d == std::floor(d) && std::fabs(d) < 2147483648.0) { out = std::to_string(int64_t(d)); return true; }
            out = jDoubleStr(d);
            return true;
        }
        out.assign(s, n);
        return true;
    }
    if (lua_type(L, idx) == LUA_TBOOLEAN) { out = lua_toboolean(L, idx) ? "true" : "false"; return true; }
    return false;
}

template <int N> int vIndex(lua_State* L) { // FiguraVecN.__index(Object arg)
    VecUD* u = selfVecOp<N>(L, "indexing", true);
    std::string s;
    if (!keyString(L, 2, s)) return 0;
    size_t len = s.size();
    if (len == 0 || len > 4) return 0;
    double vals[4];
    for (size_t i = 0; i < len; i++) {
        int k = swz(s[i], N);
        if (k < 0) return 0;
        vals[i] = k == 4 ? 0.0 : u->v[k];
    }
    if (len == 1) { lua_pushnumber(L, vals[0]); return 1; }
    newVec(L, int(len), vals);
    return 1;
}
template <int N> int vNewIndex(lua_State* L) { // FiguraVecN.__newindex(@LuaNotNil String key, Object value)
    VecUD* u = selfVecOp<N>(L, "indexing", true);
    std::string key;
    if (lua_isnoneornil(L, 2)) notNil(L, 2, "__newindex", "String");
    if (lua_type(L, 2) != LUA_TNUMBER && lua_type(L, 2) != LUA_TSTRING) badArg(L, 2, "__newindex", "String");
    keyString(L, 2, key);
    size_t len = key.size();
    if (len == 1) {
        if (isJNumber(L, 3)) {
            double d = lua_tonumber(L, 3);
            int    k = swz(key[0], N);
            if (k < 0) raise(L, "Invalid key to vector __newindex: " + key);
            if (k < 4) u->v[k] = d;
            return 0;
        }
        raise(L, "Invalid call to __newindex - value assigned to key " + key + " must be number.");
    }
    VecUD* o = testVec(L, 3);
    if (o && size_t(o->n) == len) {
        double vals[4] = {o->v[0], o->v[1], o->n > 2 ? o->v[2] : 0, o->n > 3 ? o->v[3] : 0};
        for (size_t i = 0; i < len; i++) {
            int k = swz(key[i], N);
            if (k < 0) raise(L, std::string("Invalid key to __newindex: invalid swizzle character: ") + key[i]);
            if (k < 4) u->v[k] = vals[i];
        }
        return 0;
    }
    raise(L, "Invalid call to __newindex - vector swizzles must be the same size.");
}
template <int N> int vToString(lua_State* L) {
    VecUD* u = selfVecOp<N>(L, "tostring", false);
    lua_pushstring(L, vecToString(u).c_str());
    return 1;
}
template <int N> int vToStringM(lua_State* L) {
    VecUD* u = selfVec<N>(L, "toString");
    lua_pushstring(L, vecToString(u).c_str());
    return 1;
}

// ---- metamethods (static ones take (a, b) in any order) ----
template <int N> [[noreturn]] void invalidTypes(lua_State* L, char const* op) {
    raise(L, std::string("Invalid types to ") + op + ": " + simpleName(L, 1) + ", " + simpleName(L, 2));
}
template <int N> int vAdd(lua_State* L) {
    VecUD *a = testVecN(L, 1, N), *b = testVecN(L, 2, N);
    if (a && b) { VecUD* r = newVec(L, N, a->v); for (int i = 0; i < N; i++) r->v[i] += b->v[i]; return 1; }
    if (a && isJNumber(L, 2)) { double d = lua_tonumber(L, 2); VecUD* r = newVec(L, N, a->v); for (int i = 0; i < N; i++) r->v[i] += d; return 1; }
    if (b && isJNumber(L, 1)) { double d = lua_tonumber(L, 1); VecUD* r = newVec(L, N, b->v); for (int i = 0; i < N; i++) r->v[i] += d; return 1; }
    invalidTypes<N>(L, "__add");
}
template <int N> int vSub(lua_State* L) {
    VecUD *a = testVecN(L, 1, N), *b = testVecN(L, 2, N);
    if (a && b) { VecUD* r = newVec(L, N, a->v); for (int i = 0; i < N; i++) r->v[i] -= b->v[i]; return 1; }
    if (a && isJNumber(L, 2)) { double d = -lua_tonumber(L, 2); VecUD* r = newVec(L, N, a->v); for (int i = 0; i < N; i++) r->v[i] += d; return 1; }
    if (b && isJNumber(L, 1)) { // vec.scaled(-1).offset(d)
        double d = lua_tonumber(L, 1); VecUD* r = newVec(L, N, b->v);
        for (int i = 0; i < N; i++) r->v[i] = r->v[i] * -1 + d;
        return 1;
    }
    invalidTypes<N>(L, "__sub");
}
void transformVec(double* v, int n, MatUD const* m) {
    double r[4];
    for (int i = 0; i < n; i++) {
        double s = m->at(i, 0) * v[0];
        for (int j = 1; j < n; j++) s += m->at(i, j) * v[j];
        r[i] = s;
    }
    for (int i = 0; i < n; i++) v[i] = r[i];
}
template <int N> int vMul(lua_State* L) {
    VecUD *a = testVecN(L, 1, N), *b = testVecN(L, 2, N);
    if (a && b) { VecUD* r = newVec(L, N, a->v); for (int i = 0; i < N; i++) r->v[i] *= b->v[i]; return 1; }
    if (a && isJNumber(L, 2)) { double d = lua_tonumber(L, 2); VecUD* r = newVec(L, N, a->v); for (int i = 0; i < N; i++) r->v[i] *= d; return 1; }
    if (a) {
        if (MatUD* m = testMatN(L, 2, N)) { guard(m); VecUD* r = newVec(L, N, a->v); transformVec(r->v, N, m); return 1; }
    }
    if (b && isJNumber(L, 1)) { double d = lua_tonumber(L, 1); VecUD* r = newVec(L, N, b->v); for (int i = 0; i < N; i++) r->v[i] *= d; return 1; }
    invalidTypes<N>(L, "__mul");
}
template <int N> int vDiv(lua_State* L) {
    VecUD *a = testVecN(L, 1, N), *b = testVecN(L, 2, N);
    if (a) {
        if (b) { VecUD* r = newVec(L, N, a->v); for (int i = 0; i < N; i++) r->v[i] /= b->v[i]; return 1; }
        if (isJNumber(L, 2)) {
            double d = lua_tonumber(L, 2);
            if (d == 0) raise(L, "Attempt to divide vector by 0");
            double f = 1 / d;
            VecUD* r = newVec(L, N, a->v);
            for (int i = 0; i < N; i++) r->v[i] *= f;
            return 1;
        }
    } else if (b && isJNumber(L, 1)) {
        double d = lua_tonumber(L, 1);
        VecUD* r = newVec(L, N);
        for (int i = 0; i < N; i++) r->v[i] = d / b->v[i];
        return 1;
    }
    invalidTypes<N>(L, "__div");
}
void reduceVals(double* v, double const* m, int n) {
    for (int i = 0; i < n; i++) v[i] = jMod(jMod(v[i], m[i]) + m[i], m[i]);
}
template <int N> int vMod(lua_State* L) { // instance: __mod(@LuaNotNil Object rhs)
    VecUD* u = selfVecOp<N>(L, "%", true);
    if (lua_isnoneornil(L, 2)) notNil(L, 2, "__mod", "AnyType");
    if (isJNumber(L, 2)) {
        double d = lua_tonumber(L, 2);
        if (d == 0) raise(L, "Attempt to reduce vector by 0");
        double m[4] = {d, d, d, d};
        VecUD* r = newVec(L, N, u->v);
        reduceVals(r->v, m, N);
        return 1;
    }
    if (VecUD* o = testVecN(L, 2, N)) { VecUD* r = newVec(L, N, u->v); reduceVals(r->v, o->v, N); return 1; }
    raise(L, std::string("Invalid types to __mod: FiguraVec") + std::to_string(N) + ", " + simpleName(L, 2));
}
int vEq(lua_State* L) {
    VecUD *a = testVec(L, 1), *b = testVec(L, 2);
    bool   eq = a && b && a->n == b->n;
    for (int i = 0; eq && i < a->n; i++) eq = a->v[i] == b->v[i];
    lua_pushboolean(L, eq);
    return 1;
}
int vUnm(lua_State* L) {
    VecUD* a = testVec(L, 1);
    if (!a) raise(L, "Expected subject of - to be a Vector; actually got " + actualType(L, 1));
    VecUD* r = newVec(L, a->n, a->v);
    for (int i = 0; i < a->n; i++) r->v[i] *= -1;
    return 1;
}
int vLen(lua_State* L) {
    VecUD* a = testVec(L, 1);
    if (!a) raise(L, "Expected subject of # to be a Vector; actually got " + actualType(L, 1));
    lua_pushinteger(L, a->n);
    return 1;
}
template <int N> int vLt(lua_State* L) {
    VecUD* a = selfVecOp<N>(L, "<", true);
    VecUD* b = jVec(L, 2, N, "__lt", true);
    bool   r = true;
    for (int i = 0; r && i < N; i++) r = a->v[i] < b->v[i];
    lua_pushboolean(L, r);
    return 1;
}
template <int N> int vLe(lua_State* L) {
    VecUD* a = selfVecOp<N>(L, "<=", true);
    VecUD* b = jVec(L, 2, N, "__le", true);
    bool   r = true;
    for (int i = 0; r && i < N; i++) r = a->v[i] <= b->v[i];
    lua_pushboolean(L, r);
    return 1;
}

// ---- methods ----
/// set/add/sub/mul/div/reduce argument parsing per size. Vec2/Vec3: (Object x, double y[, double z]) through
/// LuaUtils.parseVecN; Vec4: (@LuaNotNil Object x, double y, double z, double w), vector or number only.
template <int N> void vecArgs(lua_State* L, char const* fn, double* out) {
    if constexpr (N == 2) {
        double y = jDouble(L, 3, fn);
        parseVec2(L, 2, y, fn, out);
    } else if constexpr (N == 3) {
        double y = jDouble(L, 3, fn), z = jDouble(L, 4, fn);
        parseVec3J(L, 2, y, z, fn, out);
    } else {
        if (lua_isnoneornil(L, 2)) notNil(L, 2, fn, "AnyType");
        double y = jDouble(L, 3, fn), z = jDouble(L, 4, fn), w = jDouble(L, 5, fn);
        if (VecUD* o = testVecN(L, 2, 4)) { std::memcpy(out, o->v, sizeof(double) * 4); return; }
        if (isJNumber(L, 2)) { out[0] = lua_tonumber(L, 2); out[1] = y; out[2] = z; out[3] = w; return; }
        raise(L, std::string("Illegal type to ") + fn + "(): " + simpleName(L, 2));
    }
}
template <int N> int mSet(lua_State* L) { VecUD* u = selfVec<N>(L, "set"); double a[4]; vecArgs<N>(L, "set", a); for (int i = 0; i < N; i++) u->v[i] = a[i]; lua_settop(L, 1); return 1; }
template <int N> int mAdd(lua_State* L) { VecUD* u = selfVec<N>(L, "add"); double a[4]; vecArgs<N>(L, "add", a); for (int i = 0; i < N; i++) u->v[i] += a[i]; lua_settop(L, 1); return 1; }
template <int N> int mSub(lua_State* L) { VecUD* u = selfVec<N>(L, "sub"); double a[4]; vecArgs<N>(L, "sub", a); for (int i = 0; i < N; i++) u->v[i] -= a[i]; lua_settop(L, 1); return 1; }
template <int N> int mMul(lua_State* L) { VecUD* u = selfVec<N>(L, "mul"); double a[4]; vecArgs<N>(L, "mul", a); for (int i = 0; i < N; i++) u->v[i] *= a[i]; lua_settop(L, 1); return 1; }
template <int N> int mDiv(lua_State* L) { VecUD* u = selfVec<N>(L, "div"); double a[4]; vecArgs<N>(L, "div", a); for (int i = 0; i < N; i++) u->v[i] /= a[i]; lua_settop(L, 1); return 1; }
template <int N> int mReduce(lua_State* L) { VecUD* u = selfVec<N>(L, "reduce"); double a[4]; vecArgs<N>(L, "reduce", a); reduceVals(u->v, a, N); lua_settop(L, 1); return 1; }
template <int N> int mOffset(lua_State* L) { VecUD* u = selfVec<N>(L, "offset"); double d = jDouble(L, 2, "offset"); for (int i = 0; i < N; i++) u->v[i] += d; lua_settop(L, 1); return 1; }
template <int N> int mScale(lua_State* L) { VecUD* u = selfVec<N>(L, "scale"); double d = jDouble(L, 2, "scale"); for (int i = 0; i < N; i++) u->v[i] *= d; lua_settop(L, 1); return 1; }
template <int N> int mReset(lua_State* L) { VecUD* u = selfVec<N>(L, "reset"); for (int i = 0; i < N; i++) u->v[i] = 0; lua_settop(L, 1); return 1; }
template <int N> int mUnpack(lua_State* L) { VecUD* u = selfVec<N>(L, "unpack"); for (int i = 0; i < N; i++) lua_pushnumber(L, u->v[i]); return N; }
double lenSq(double const* v, int n) { double s = v[0] * v[0]; for (int i = 1; i < n; i++) s += v[i] * v[i]; return s; }
template <int N> int mLengthSq(lua_State* L) { VecUD* u = selfVec<N>(L, "lengthSquared"); lua_pushnumber(L, lenSq(u->v, N)); return 1; }
template <int N> int mLength(lua_State* L) { VecUD* u = selfVec<N>(L, "length"); lua_pushnumber(L, std::sqrt(lenSq(u->v, N))); return 1; }
template <int N> int mCopy(lua_State* L) { VecUD* u = selfVec<N>(L, "copy"); newVec(L, N, u->v); return 1; }
template <int N> int mDot(lua_State* L) {
    VecUD* u = selfVec<N>(L, "dot");
    VecUD* o = jVec(L, 2, N, "dot", true);
    double s = u->v[0] * o->v[0];
    for (int i = 1; i < N; i++) s += u->v[i] * o->v[i];
    lua_pushnumber(L, s);
    return 1;
}
void normalizeVals(double* v, int n) {
    double len = std::sqrt(lenSq(v, n));
    if (len > 0) { double f = 1 / len; for (int i = 0; i < n; i++) v[i] *= f; }
}
template <int N> int mNormalize(lua_State* L) { VecUD* u = selfVec<N>(L, "normalize"); normalizeVals(u->v, N); lua_settop(L, 1); return 1; }
template <int N> int mNormalized(lua_State* L) { VecUD* u = selfVec<N>(L, "normalized"); VecUD* r = newVec(L, N, u->v); normalizeVals(r->v, N); return 1; }
void clampVals(double* v, int n, std::optional<double> mn, std::optional<double> mx) {
    double minL = mn ? *mn : 0, maxL = mx ? *mx : HUGE_VAL;
    double len = std::sqrt(lenSq(v, n));
    if (len == 0) return;
    double f;
    if (len < minL) f = minL / len;
    else if (len > maxL) f = maxL / len;
    else return;
    for (int i = 0; i < n; i++) v[i] *= f;
}
template <int N> int mClampLength(lua_State* L) {
    VecUD* u = selfVec<N>(L, "clampLength");
    auto mn = jOptDouble(L, 2, "clampLength"), mx = jOptDouble(L, 3, "clampLength");
    clampVals(u->v, N, mn, mx);
    lua_settop(L, 1);
    return 1;
}
template <int N> int mClamped(lua_State* L) {
    VecUD* u = selfVec<N>(L, "clamped");
    auto mn = jOptDouble(L, 2, "clamped"), mx = jOptDouble(L, 3, "clamped");
    VecUD* r = newVec(L, N, u->v);
    clampVals(r->v, N, mn, mx);
    return 1;
}
template <int N> int mToRad(lua_State* L) { VecUD* u = selfVec<N>(L, "toRad"); VecUD* r = newVec(L, N, u->v); for (int i = 0; i < N; i++) r->v[i] *= kDegToRad; return 1; }
template <int N> int mToDeg(lua_State* L) { VecUD* u = selfVec<N>(L, "toDeg"); VecUD* r = newVec(L, N, u->v); for (int i = 0; i < N; i++) r->v[i] *= kRadToDeg; return 1; }
template <int N> int mFloor(lua_State* L) { VecUD* u = selfVec<N>(L, "floor"); VecUD* r = newVec(L, N); for (int i = 0; i < N; i++) r->v[i] = std::floor(u->v[i]); return 1; }
template <int N> int mCeil(lua_State* L) { VecUD* u = selfVec<N>(L, "ceil"); VecUD* r = newVec(L, N); for (int i = 0; i < N; i++) r->v[i] = std::ceil(u->v[i]); return 1; }
template <int N> int mApplyFunc(lua_State* L) {
    VecUD* u = selfVec<N>(L, "applyFunc");
    if (lua_isnoneornil(L, 2)) notNil(L, 2, "applyFunc", "Function");
    if (!lua_isfunction(L, 2)) badArg(L, 2, "applyFunc", "Function");
    for (int i = 0; i < N; i++) {
        lua_pushvalue(L, 2);
        lua_pushnumber(L, u->v[i]);
        lua_pushinteger(L, i + 1);
        lua_call(L, 2, 1);
        u->v[i] = lua_tonumber(L, -1); // LuaValue.todouble(): non-numbers -> 0
        lua_pop(L, 1);
    }
    lua_settop(L, 1);
    return 1;
}
template <int N> int mAugmented(lua_State* L) {
    VecUD* u = selfVec<N>(L, "augmented");
    auto   d = jOptDouble(L, 2, "augmented");
    VecUD* r = newVec(L, N + 1, u->v);
    r->v[N] = d ? *d : 1;
    return 1;
}
template <int N> int mTransform(lua_State* L) {
    VecUD* u = selfVec<N>(L, "transform");
    MatUD* m = jMat(L, 2, N, "transform", N != 2); // FiguraVec2.transform is nullable (nil -> unchanged)
    if (m) { guard(m); transformVec(u->v, N, m); }
    lua_settop(L, 1);
    return 1;
}
void crossVals(double* v, double const* o) {
    double nx = v[1] * o[2] - v[2] * o[1];
    double ny = v[2] * o[0] - v[0] * o[2];
    double nz = v[0] * o[1] - v[1] * o[0];
    v[0] = nx; v[1] = ny; v[2] = nz;
}
int mCross(lua_State* L) { VecUD* u = selfVec<3>(L, "cross"); VecUD* o = jVec(L, 2, 3, "cross", true); crossVals(u->v, o->v); lua_settop(L, 1); return 1; }
int mCrossed(lua_State* L) { VecUD* u = selfVec<3>(L, "crossed"); VecUD* o = jVec(L, 2, 3, "crossed", true); double o3[3] = {o->v[0], o->v[1], o->v[2]}; VecUD* r = newVec(L, 3, u->v); crossVals(r->v, o3); return 1; }

template <int N> void registerVec(lua_State* L) {
    TypeDef d;
    d.name    = N == 2 ? "Vector2" : N == 3 ? "Vector3" : "Vector4";
    d.methods = {{"set", mSet<N>}, {"reset", mReset<N>}, {"add", mAdd<N>}, {"sub", mSub<N>}, {"mul", mMul<N>},
                 {"div", mDiv<N>}, {"reduce", mReduce<N>}, {"offset", mOffset<N>}, {"scale", mScale<N>},
                 {"unpack", mUnpack<N>}, {"transform", mTransform<N>}, {"lengthSquared", mLengthSq<N>},
                 {"length", mLength<N>}, {"copy", mCopy<N>}, {"dot", mDot<N>}, {"normalize", mNormalize<N>},
                 {"normalized", mNormalized<N>}, {"clampLength", mClampLength<N>}, {"clamped", mClamped<N>},
                 {"toRad", mToRad<N>}, {"toDeg", mToDeg<N>}, {"floor", mFloor<N>}, {"ceil", mCeil<N>},
                 {"applyFunc", mApplyFunc<N>}, {"toString", vToStringM<N>}};
    if constexpr (N < 4) d.methods.push_back({"augmented", mAugmented<N>});
    if constexpr (N == 3) {
        d.methods.push_back({"cross", mCross});
        d.methods.push_back({"crossed", mCrossed});
    }
    d.index    = vIndex<N>;
    d.newindex = vNewIndex<N>;
    d.tostring = vToString<N>;
    d.meta     = {{"__add", vAdd<N>}, {"__sub", vSub<N>}, {"__mul", vMul<N>}, {"__div", vDiv<N>}, {"__mod", vMod<N>},
                  {"__unm", vUnm}, {"__len", vLen}, {"__eq", vEq}, {"__lt", vLt<N>}, {"__le", vLe<N>}};
    gVecT[N] = registerType(L, d);
    lua_pushlightuserdata(L, (void*)&kVecMtKey[N]);
    pushTypeMetatable(L, gVecT[N]);
    lua_rawset(L, LUA_REGISTRYINDEX);
}

// ---------------------------------------------------------------------------------------------------------------------
// VectorsAPI
// ---------------------------------------------------------------------------------------------------------------------
#define VAPI_SELF dropApiSelf(L, &gVectorsApi, gVectorsApiT)

int apiVec(lua_State* L) {
    VAPI_SELF;
    std::optional<double> a[4];
    for (int i = 0; i < 4; i++) a[i] = jOptDouble(L, i + 1, "vec");
    if (!a[0]) raise(L, "Illegal argument to vec(): x, null");
    if (a[3]) {
        if (!a[1]) raise(L, "Illegal argument to vec(): y,null");
        if (!a[2]) raise(L, "Illegal argument to vec(): z,null");
        newVec4(L, *a[0], *a[1], *a[2], *a[3]);
        return 1;
    }
    if (a[2]) {
        if (!a[1]) raise(L, "Illegal argument to vec(): y,null");
        newVec3(L, *a[0], *a[1], *a[2]);
        return 1;
    }
    if (a[1]) { newVec2(L, *a[0], *a[1]); return 1; }
    raise(L, "Invalid arguments to vec(), needs at least 2 numbers!");
}
template <int N> int apiVecN(lua_State* L) {
    VAPI_SELF;
    char const* fn = N == 2 ? "vec2" : N == 3 ? "vec3" : "vec4";
    double a[4];
    for (int i = 0; i < N; i++) a[i] = jDouble(L, i + 1, fn);
    newVec(L, N, a);
    return 1;
}
/// ColorUtils.intToRGB: channels / 255f (float division, widened)
void intToRGB(int32_t c, double* out) {
    out[0] = double(float(float((c >> 16) & 0xFF) / 255.0f));
    out[1] = double(float(float((c >> 8) & 0xFF) / 255.0f));
    out[2] = double(float(float(c & 0xFF) / 255.0f));
}
int32_t rgbToInt(double const* rgb) {
    uint32_t hex = uint32_t(jD2I(rgb[0] * 0xFF));
    hex = (hex << 8) + uint32_t(jD2I(rgb[1] * 0xFF));
    hex = (hex << 8) + uint32_t(jD2I(rgb[2] * 0xFF));
    return int32_t(hex);
}
int apiRgbToInt(lua_State* L) { VAPI_SELF; double a[3]; argsVec3(L, 1, "rgbToInt", a); lua_pushinteger(L, rgbToInt(a)); return 1; }
int apiIntToRgb(lua_State* L) { VAPI_SELF; int c = jInt(L, 1, "intToRGB"); double a[3]; intToRGB(c, a); newVec(L, 3, a); return 1; }
int apiRgbToHex(lua_State* L) {
    VAPI_SELF;
    double a[3];
    argsVec3(L, 1, "rgbToHex", a);
    char b[16];
    std::snprintf(b, sizeof b, "%x", uint32_t(rgbToInt(a))); // Integer.toHexString
    std::string s = b;
    if (s.size() < 6) s = std::string(6 - s.size(), '0') + s;
    lua_pushstring(L, s.c_str());
    return 1;
}
bool iequals(std::string const& a, char const* b) {
    size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; i++)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}
int apiHexToRgb(lua_State* L) { // ColorUtils.userInputHex(hex, vec3())
    VAPI_SELF;
    if (lua_isnoneornil(L, 1)) notNil(L, 1, "hexToRGB", "String");
    if (!lua_isstring(L, 1)) badArg(L, 1, "hexToRGB", "String");
    std::string s = lua_tostring(L, 1);
    static struct { char const* n; int32_t c; } const kNamed[] = {
        // ColorUtils.Colors
        {"AWESOME_BLUE", 0x5EA5FF}, {"PURPLE", 0xA672EF}, {"FIGURA_BLUE", 0x00F0FF}, {"SOFT_BLUE", 0x99BBEE},
        {"FIGURA_RED", 0xFF2400}, {"ORANGE", 0xFFC400}, {"CHEESE", 0xF8C53A}, {"LUA_LOG", 0x5555FF},
        {"LUA_ERROR", 0xFF5555}, {"LUA_PING", 0xA155DA}, {"DEFAULT", 0x5AAAFF}, {"DISCORD", 0x5865F2},
        {"KOFI", 0x27AAE0}, {"GITHUB", 0xFFFFFF}, {"MODRINTH", 0x1BD96A}, {"CURSEFORGE", 0xF16436},
    };
    static struct { char const* n; int32_t c; } const kChat[] = { // ChatFormatting colors (valueOf(upper))
        {"BLACK", 0x000000}, {"DARK_BLUE", 0x0000AA}, {"DARK_GREEN", 0x00AA00}, {"DARK_AQUA", 0x00AAAA},
        {"DARK_RED", 0xAA0000}, {"DARK_PURPLE", 0xAA00AA}, {"GOLD", 0xFFAA00}, {"GRAY", 0xAAAAAA},
        {"DARK_GRAY", 0x555555}, {"BLUE", 0x5555FF}, {"GREEN", 0x55FF55}, {"AQUA", 0x55FFFF}, {"RED", 0xFF5555},
        {"LIGHT_PURPLE", 0xFF55FF}, {"YELLOW", 0xFFFF55}, {"WHITE", 0xFFFFFF},
    };
    double out[3] = {0, 0, 0};
    for (auto const& c : kNamed)
        if (iequals(s, c.n)) { intToRGB(c.c, out); newVec(L, 3, out); return 1; }
    {
        std::string up = s;
        for (char& ch : up) ch = char(std::toupper((unsigned char)ch)); // Locale.US
        for (auto const& c : kChat)
            if (up == c.n) { intToRGB(c.c, out); newVec(L, 3, out); return 1; }
    }
    // hexStringToRGB
    bool blank = true;
    for (char ch : s) if (!std::isspace((unsigned char)ch)) blank = false;
    if (!blank) {
        std::string hex = s;
        if (!hex.empty() && hex[0] == '#') hex.erase(0, 1);
        if (hex.size() == 3) hex = std::string{hex[0], hex[0], hex[1], hex[1], hex[2], hex[2]};
        else if (hex.size() < 6) hex.append(6 - hex.size(), '0');
        std::string h6 = hex.substr(0, 6);
        // Integer.parseInt(h6, 16): optional sign then hex digits
        size_t i = 0;
        bool   neg = false, ok = true;
        if (!h6.empty() && (h6[0] == '-' || h6[0] == '+')) { neg = h6[0] == '-'; i = 1; }
        if (i >= h6.size()) ok = false;
        int64_t v = 0;
        for (; ok && i < h6.size(); i++) {
            char ch = h6[i];
            int  dgt = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
            if (dgt < 0) ok = false;
            else v = v * 16 + dgt;
        }
        if (ok) intToRGB(int32_t(neg ? -v : v), out);
    }
    newVec(L, 3, out);
    return 1;
}
int apiHsvToRgb(lua_State* L) { // java.awt.Color.HSBtoRGB on floats
    VAPI_SELF;
    double a[3];
    argsVec3(L, 1, "hsvToRGB", a);
    float hue = float(a[0]), sat = float(a[1]), bri = float(a[2]);
    int   r = 0, g = 0, b = 0;
    if (sat == 0) {
        r = g = b = jD2I(bri * 255.0f + 0.5f);
    } else {
        float h = (hue - float(std::floor(hue))) * 6.0f;
        float f = h - float(std::floor(h));
        float p = bri * (1.0f - sat);
        float q = bri * (1.0f - sat * f);
        float t = bri * (1.0f - (sat * (1.0f - f)));
        auto  I = [](float x) { return jD2I(x * 255.0f + 0.5f); };
        switch (jD2I(h)) {
        case 0: r = I(bri); g = I(t); b = I(p); break;
        case 1: r = I(q); g = I(bri); b = I(p); break;
        case 2: r = I(p); g = I(bri); b = I(t); break;
        case 3: r = I(p); g = I(q); b = I(bri); break;
        case 4: r = I(t); g = I(p); b = I(bri); break;
        case 5: r = I(bri); g = I(p); b = I(q); break;
        default: break;
        }
    }
    int32_t c = int32_t(0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b));
    double  out[3];
    intToRGB(c, out);
    newVec(L, 3, out);
    return 1;
}
int apiRgbToHsv(lua_State* L) { // java.awt.Color.RGBtoHSB((int)(r*255f) ...)
    VAPI_SELF;
    double a[3];
    argsVec3(L, 1, "rgbToHSV", a);
    int   r = jD2I(a[0] * 255.0f), g = jD2I(a[1] * 255.0f), b = jD2I(a[2] * 255.0f);
    int   cmax = std::max({r, g, b}), cmin = std::min({r, g, b});
    float hue, sat, bri = float(cmax) / 255.0f;
    sat = cmax != 0 ? float(cmax - cmin) / float(cmax) : 0;
    if (sat == 0) hue = 0;
    else {
        float rc = float(cmax - r) / float(cmax - cmin), gc = float(cmax - g) / float(cmax - cmin), bc = float(cmax - b) / float(cmax - cmin);
        if (r == cmax) hue = bc - gc;
        else if (g == cmax) hue = 2.0f + rc - bc;
        else hue = 4.0f + gc - rc;
        hue = hue / 6.0f;
        if (hue < 0) hue = hue + 1.0f;
    }
    newVec3(L, hue, sat, bri);
    return 1;
}
int apiAngleToDir(lua_State* L) {
    VAPI_SELF;
    double yaw = jDouble(L, 2, "angleToDir");
    double v[2];
    parseVec2(L, 1, yaw, "angleToDir", v);
    double radPitch = v[0] * kDegToRad, radYaw = -v[1] * kDegToRad;
    double c = std::cos(radPitch);
    newVec3(L, std::sin(radYaw) * c, -std::sin(radPitch), std::cos(radYaw) * c);
    return 1;
}
/// JOML Quaternionf.mul(q): this = this * q (fma-free path)
void quatMul(float& x, float& y, float& z, float& w, float qx2, float qy2, float qz2, float qw2) {
    float nx = w * qx2 + (x * qw2 + (y * qz2 + -z * qy2));
    float ny = w * qy2 + (-x * qz2 + (y * qw2 + z * qx2));
    float nz = w * qz2 + (x * qy2 + (-y * qx2 + z * qw2));
    float nw = w * qw2 + (-x * qx2 + (-y * qy2 + -z * qz2));
    x = nx; y = ny; z = nz; w = nw;
}
int apiRotateAroundAxis(lua_State* L) {
    VAPI_SELF;
    // (double angle, Object x, Object y, Double z, Object w, Double t, Double h)
    char const* fn    = "rotateAroundAxis";
    double      angle = jDouble(L, 1, fn);
    auto        z = jOptDouble(L, 4, fn), t = jOptDouble(L, 6, fn), h = jOptDouble(L, 7, fn);
    double      a[3], b[3];
    auto        isNil = [&](int i) { return lua_isnoneornil(L, i); };
    // LuaUtils.parse2Vec3(fn, x=2, y=3, z=4, w=5, t=6, h=7, xIndex=2)
    if (testVecN(L, 2, 3)) {
        parseVec3J(L, 2, std::nullopt, std::nullopt, fn, a);
        if (testVecN(L, 3, 3)) {
            parseVec3J(L, 3, std::nullopt, std::nullopt, fn, b);
        } else if (isNil(3) || isJNumber(L, 3)) {
            if (isNil(5) || isJNumber(L, 5)) {
                std::optional<double> w;
                if (!isNil(5)) w = lua_tonumber(L, 5);
                parseVec3J(L, 3, z, w, fn, b);
            } else {
                raise(L, std::string("Illegal argument at position") + "23" + "to " + fn + "(): " + javaValueString(L, 5));
            }
        } else {
            raise(L, std::string("Illegal argument at position ") + "21" + " to " + fn + "(): " + javaValueString(L, 3));
        }
    } else if ((isJNumber(L, 2) && isNil(3)) || isJNumber(L, 3)) {
        std::optional<double> y;
        if (!isNil(3)) y = lua_tonumber(L, 3);
        parseVec3J(L, 2, y, z, fn, a);
        if (testVecN(L, 5, 3)) {
            parseVec3J(L, 5, std::nullopt, std::nullopt, fn, b);
        } else if (isNil(5) || isJNumber(L, 5)) {
            parseVec3J(L, 5, t, h, fn, b);
        } else {
            raise(L, std::string("Illegal argument at position ") + "23" + " to " + fn + "(): " + javaValueString(L, 5));
        }
    } else {
        raise(L, std::string("Illegal argument at position ") + "2" + " to " + fn + "(): " + javaValueString(L, 2));
    }
    // MathUtils.rotateAroundAxis: JOML Quaternionf (float) q * v * conj(q)
    normalizeVals(b, 3);
    float vx = float(a[0]), vy = float(a[1]), vz = float(a[2]), vw = 0;
    float ax = float(b[0]), ay = float(b[1]), az = float(b[2]);
    float ang   = float(float(angle) * float(kDegToRad)); // JOML Math.toRadians(float)
    float hang  = ang / 2.0f;
    float sinA  = float(std::sin(double(hang)));
    float vLen  = float(std::sqrt(double(ax * ax + ay * ay + az * az)));
    float qx = ax / vLen * sinA, qy = ay / vLen * sinA, qz = az / vLen * sinA, qw = float(std::cos(double(hang)));
    float rx = qx, ry = qy, rz = qz, rw = qw;
    quatMul(rx, ry, rz, rw, vx, vy, vz, vw);
    quatMul(rx, ry, rz, rw, -qx, -qy, -qz, qw);
    newVec3(L, rx, ry, rz);
    return 1;
}
/// Camera.rotation() as a JOML Matrix3f (rotationYXZ(-yaw, pitch, 0)), row-major into m.
void cameraRotation(lua_State* L, double* m, Vec3* pos) {
    Avatar* av  = avatarOf(L);
    Vec3    rot = av && av->host ? av->host->cameraRot() : Vec3{};
    if (pos) *pos = av && av->host ? av->host->cameraPos() : Vec3{};
    float angleY = float(-rot.y) * float(kPi / 180), angleX = float(rot.x) * float(kPi / 180);
    float sx = std::sin(angleX * 0.5f), cx = std::cos(angleX * 0.5f);
    float sy = std::sin(angleY * 0.5f), cy = std::cos(angleY * 0.5f);
    float sz = 0, cz = 1;
    float yx = cy * sx, yy = sy * cx, yz = sy * sx, yw = cy * cx;
    float x = yx * cz + yy * sz, y = yy * cz - yx * sz, z = yw * sz - yz * cz, w = yw * cz + yz * sz;
    float w2 = w * w, x2 = x * x, y2 = y * y, z2 = z * z;
    float zw = z * w, dzw = zw + zw, xy = x * y, dxy = xy + xy, xz = x * z, dxz = xz + xz, yw2 = y * w, dyw = yw2 + yw2;
    float yz2 = y * z, dyz = yz2 + yz2, xw = x * w, dxw = xw + xw;
    // JOML mCR (column C, row R) -> row-major [R*3+C]
    float m00 = w2 + x2 - z2 - y2, m01 = dxy + dzw, m02 = dxz - dyw;
    float m10 = -dzw + dxy, m11 = y2 - z2 + w2 - x2, m12 = dyz + dxw;
    float m20 = dyw + dxz, m21 = dyz - dxw, m22 = z2 - y2 - x2 + w2;
    double r[9] = {m00, m10, m20, m01, m11, m21, m02, m12, m22};
    std::memcpy(m, r, sizeof r);
}
void invert3(double* v); // FiguraMat3.invert on row-major values
int apiToCameraSpace(lua_State* L) {
    VAPI_SELF;
    double p[3];
    argsVec3(L, 1, "toCameraSpace", p);
    double m[9];
    Vec3   cam;
    cameraRotation(L, m, &cam);
    invert3(m);
    double v[3] = {p[0] - cam.x, p[1] - cam.y, p[2] - cam.z};
    double r[3];
    for (int i = 0; i < 3; i++) r[i] = m[i * 3] * v[0] + m[i * 3 + 1] * v[1] + m[i * 3 + 2] * v[2];
    newVec(L, 3, r);
    return 1;
}
int apiWorldToScreenSpace(lua_State* L) {
    VAPI_SELF;
    double p[3];
    argsVec3(L, 1, "worldToScreenSpace", p);
    double m[9];
    Vec3   cam;
    cameraRotation(L, m, &cam);
    // inverse of a rotation (Matrix3f.invert) == transpose
    double d[3] = {p[0] - cam.x, p[1] - cam.y, p[2] - cam.z};
    float  cs[3];
    for (int i = 0; i < 3; i++) cs[i] = float(m[i]) * float(d[0]) + float(m[3 + i]) * float(d[1]) + float(m[6 + i]) * float(d[2]);
    Avatar* av = avatarOf(L);
    float   W  = av && av->host ? float(av->host->windowWidth()) : 1920.f, H = av && av->host ? float(av->host->windowHeight()) : 1080.f;
    float   fov = 70.0f;                     // options fov (host has no FOV query)
    float   zNear = 0.05f, zFar = 12 * 16 * 4.0f; // render distance 12 chunks
    float   hf = std::tan(fov * float(kPi / 180) * 0.5f);
    float   aspect = W / H;
    float   px = cs[0] / (hf * aspect), py = cs[1] / hf;
    float   pz = cs[2] * ((zFar + zNear) / (zNear - zFar)) + (zFar + zFar) * zNear / (zNear - zFar);
    float   pw = -cs[2];
    newVec4(L, px / pw, py / pw, pz / pw, std::sqrt(lenSq(d, 3)));
    return 1;
}
#undef VAPI_SELF

// ---------------------------------------------------------------------------------------------------------------------
// matrices
// ---------------------------------------------------------------------------------------------------------------------
std::string matToString(MatUD const* u) { // FiguraMatrix.getString
    std::string s = "\n[";
    int         cnt = u->n * u->n;
    for (int i = 0; i < cnt; i++) {
        if (i % u->n == 0) s += "\n\t";
        s += jDf(u->d()[i]);
        if (i < cnt - 1) s += ", ";
    }
    return s + "\n]";
}
double calcDet(MatUD const* u) {
    double const* v = u->d();
    if (u->n == 2) return v[0] * v[3] - v[1] * v[2];
    if (u->n == 3) {
        double v11 = v[0], v12 = v[1], v13 = v[2], v21 = v[3], v22 = v[4], v23 = v[5], v31 = v[6], v32 = v[7], v33 = v[8];
        double sub11 = v22 * v33 - v23 * v32;
        double sub12 = v21 * v33 - v23 * v31;
        double sub13 = v21 * v32 - v22 * v31;
        return v11 * sub11 - v12 * sub12 + v13 * sub13;
    }
    Mat4 const& m = u->m;
    double A2323 = m.v33 * m.v44 - m.v34 * m.v43;
    double A1323 = m.v32 * m.v44 - m.v34 * m.v42;
    double A1223 = m.v32 * m.v43 - m.v33 * m.v42;
    double A0323 = m.v31 * m.v44 - m.v34 * m.v41;
    double A0223 = m.v31 * m.v43 - m.v33 * m.v41;
    double A0123 = m.v31 * m.v42 - m.v32 * m.v41;
    return m.v11 * (m.v22 * A2323 - m.v23 * A1323 + m.v24 * A1223)
         - m.v12 * (m.v21 * A2323 - m.v23 * A0323 + m.v24 * A0223)
         + m.v13 * (m.v21 * A1323 - m.v22 * A0323 + m.v24 * A0123)
         - m.v14 * (m.v21 * A1223 - m.v22 * A0223 + m.v23 * A0123);
}
double det(MatUD* u) { // FiguraMatrix.det()
    if (!std::isnan(u->cDet)) return u->cDet;
    u->cDet = calcDet(u);
    if (u->cDet == 0) u->cDet = kJavaMinValue;
    snapshot(u);
    return u->cDet;
}
void transposeVals(double* v, int n) {
    for (int r = 0; r < n; r++)
        for (int c = r + 1; c < n; c++) std::swap(v[r * n + c], v[c * n + r]);
}
void invert3(double* v) {
    double v11 = v[0], v12 = v[1], v13 = v[2], v21 = v[3], v22 = v[4], v23 = v[5], v31 = v[6], v32 = v[7], v33 = v[8];
    double sub11 = v22 * v33 - v23 * v32;
    double sub12 = v21 * v33 - v23 * v31;
    double sub13 = v21 * v32 - v22 * v31;
    double sub21 = v12 * v33 - v13 * v32;
    double sub22 = v11 * v33 - v13 * v31;
    double sub23 = v11 * v32 - v12 * v31;
    double sub31 = v12 * v23 - v13 * v22;
    double sub32 = v11 * v23 - v13 * v21;
    double sub33 = v11 * v22 - v12 * v21;
    double det = v11 * sub11 - v12 * sub12 + v13 * sub13;
    if (det == 0) det = kJavaMinValue;
    det = 1 / det;
    // set(n11, n21, n31, n12, n22, n32, n13, n23, n33) - column-major arguments
    double a[9] = {det * sub11, -det * sub12, det * sub13, -det * sub21, det * sub22, -det * sub23, det * sub31, -det * sub32, det * sub33};
    for (int c = 0; c < 3; c++)
        for (int r = 0; r < 3; r++) v[r * 3 + c] = a[c * 3 + r];
}
void invert4(double* out) {
    double v11 = out[0], v12 = out[1], v13 = out[2], v14 = out[3], v21 = out[4], v22 = out[5], v23 = out[6], v24 = out[7];
    double v31 = out[8], v32 = out[9], v33 = out[10], v34 = out[11], v41 = out[12], v42 = out[13], v43 = out[14], v44 = out[15];
    double A2323 = v33 * v44 - v34 * v43;
    double A1323 = v32 * v44 - v34 * v42;
    double A1223 = v32 * v43 - v33 * v42;
    double A0323 = v31 * v44 - v34 * v41;
    double A0223 = v31 * v43 - v33 * v41;
    double A0123 = v31 * v42 - v32 * v41;
    double A2313 = v23 * v44 - v24 * v43;
    double A1313 = v22 * v44 - v24 * v42;
    double A1213 = v22 * v43 - v23 * v42;
    double A2312 = v23 * v34 - v24 * v33;
    double A1312 = v22 * v34 - v24 * v32;
    double A1212 = v22 * v33 - v23 * v32;
    double A0313 = v21 * v44 - v24 * v41;
    double A0213 = v21 * v43 - v23 * v41;
    double A0312 = v21 * v34 - v24 * v31;
    double A0212 = v21 * v33 - v23 * v31;
    double A0113 = v21 * v42 - v22 * v41;
    double A0112 = v21 * v32 - v22 * v31;
    double det = v11 * (v22 * A2323 - v23 * A1323 + v24 * A1223)
               - v12 * (v21 * A2323 - v23 * A0323 + v24 * A0223)
               + v13 * (v21 * A1323 - v22 * A0323 + v24 * A0123)
               - v14 * (v21 * A1223 - v22 * A0223 + v23 * A0123);
    if (det == 0) det = kJavaMinValue;
    det = 1 / det;
    // set(column-major args) then transpose() == these values row-major
    double a[16] = {
        det * (v22 * A2323 - v23 * A1323 + v24 * A1223),
        det * -(v12 * A2323 - v13 * A1323 + v14 * A1223),
        det * (v12 * A2313 - v13 * A1313 + v14 * A1213),
        det * -(v12 * A2312 - v13 * A1312 + v14 * A1212),
        det * -(v21 * A2323 - v23 * A0323 + v24 * A0223),
        det * (v11 * A2323 - v13 * A0323 + v14 * A0223),
        det * -(v11 * A2313 - v13 * A0313 + v14 * A0213),
        det * (v11 * A2312 - v13 * A0312 + v14 * A0212),
        det * (v21 * A1323 - v22 * A0323 + v24 * A0123),
        det * -(v11 * A1323 - v12 * A0323 + v14 * A0123),
        det * (v11 * A1313 - v12 * A0313 + v14 * A0113),
        det * -(v11 * A1312 - v12 * A0312 + v14 * A0112),
        det * -(v21 * A1223 - v22 * A0223 + v23 * A0123),
        det * (v11 * A1223 - v12 * A0223 + v13 * A0123),
        det * -(v11 * A1213 - v12 * A0213 + v13 * A0113),
        det * (v11 * A1212 - v12 * A0212 + v13 * A0112),
    };
    std::memcpy(out, a, sizeof a);
}
void invertMat(MatUD* u) { // FiguraMatN.invert()
    int n = u->n, cnt = n * n;
    if (n == 2) {
        double d = det(u);
        double v11 = u->d()[0], v12 = u->d()[1], v21 = u->d()[2], v22 = u->d()[3];
        // set(n11 = v22/det, n21 = -v21/det, n12 = -v12/det, n22 = v11/det)
        u->d()[0] = v22 / d; u->d()[2] = -v21 / d; u->d()[1] = -v12 / d; u->d()[3] = v11 / d;
        invalidate(u);
        return;
    }
    double capture[16];
    std::memcpy(capture, u->d(), sizeof(double) * size_t(cnt));
    if (u->hasInv) {
        std::memcpy(u->d(), u->inv, sizeof(double) * size_t(cnt));
    } else if (n == 3) {
        invert3(u->d());
    } else {
        invert4(u->d());
    }
    invalidate(u); // set() invalidates; cachedDeterminant = 1/NaN stays NaN
    std::memcpy(u->inv, capture, sizeof(double) * size_t(cnt));
    u->hasInv = true;
    snapshot(u);
}
/// this = o * this (FiguraMatN.multiply) or this = this * o (rightMultiply)
void mulInto(MatUD* u, MatUD const* o, bool right) {
    int    n = u->n;
    double a[16], b[16], r[16];
    std::memcpy(a, right ? u->d() : o->d(), sizeof(double) * size_t(n * n));
    std::memcpy(b, right ? o->d() : u->d(), sizeof(double) * size_t(n * n));
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            double s = a[i * n] * b[j];
            for (int k = 1; k < n; k++) s += a[i * n + k] * b[k * n + j];
            r[i * n + j] = s;
        }
    std::memcpy(u->d(), r, sizeof(double) * size_t(n * n));
    invalidate(u);
}
void rotateX(MatUD* u, double deg) {
    double r = deg * kDegToRad, c = std::cos(r), s = std::sin(r);
    for (int j = 0; j < u->n; j++) {
        double a = u->at(1, j), b = u->at(2, j);
        u->at(1, j) = c * a - s * b;
        u->at(2, j) = s * a + c * b;
    }
    invalidate(u);
}
void rotateY(MatUD* u, double deg) {
    double r = deg * kDegToRad, c = std::cos(r), s = std::sin(r);
    for (int j = 0; j < u->n; j++) {
        double a = u->at(0, j), b = u->at(2, j);
        u->at(0, j) = c * a + s * b;
        u->at(2, j) = c * b - s * a;
    }
    invalidate(u);
}
void rotateZ(MatUD* u, double deg) {
    double r = deg * kDegToRad, c = std::cos(r), s = std::sin(r);
    for (int j = 0; j < u->n; j++) {
        double a = u->at(0, j), b = u->at(1, j);
        u->at(0, j) = c * a - s * b;
        u->at(1, j) = c * b + s * a;
    }
    invalidate(u);
}
void rotateZYX(MatUD* u, double x, double y, double z) {
    x *= kDegToRad; y *= kDegToRad; z *= kDegToRad;
    double a = std::cos(x), b = std::sin(x), c = std::cos(y), d = std::sin(y), e = std::cos(z), f = std::sin(z);
    double bc = b * c, ac = a * c, ce = c * e, cf = c * f;
    double p1 = (b * d * e - a * f), p2 = (a * d * e + b * f), p3 = (a * e + b * d * f), p4 = (a * d * f - b * e);
    for (int j = 0; j < u->n; j++) {
        double m1 = u->at(0, j), m2 = u->at(1, j), m3 = u->at(2, j);
        u->at(0, j) = ce * m1 + p1 * m2 + p2 * m3;
        u->at(1, j) = cf * m1 + p3 * m2 + p4 * m3;
        u->at(2, j) = -d * m1 + bc * m2 + ac * m3;
    }
    invalidate(u);
}
void scaleRows(MatUD* u, double const* s, int rows) {
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < u->n; j++) u->at(i, j) *= s[i];
    invalidate(u);
}
void translateRows(MatUD* u, double const* t, int rows) { // rows i += t_i * last row
    int l = u->n - 1;
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < u->n; j++) u->at(i, j) += t[i] * u->at(l, j);
    invalidate(u);
}

// ---- metamethods ----
template <int N> int matIndex(lua_State* L) { // FiguraMatN.__index(String)
    MatUD* u = selfMatOp<N>(L, "indexing", true);
    std::string k;
    if (lua_type(L, 2) != LUA_TNUMBER && lua_type(L, 2) != LUA_TSTRING) return 0;
    keyString(L, 2, k);
    auto col = [&](int c) { double v[4]; for (int r = 0; r < N; r++) v[r] = u->at(r, c); newVec(L, N, v); return 1; };
    auto row = [&](int r) { double v[4]; for (int c = 0; c < N; c++) v[c] = u->at(r, c); newVec(L, N, v); return 1; };
    if (k.size() == 1 && k[0] >= '1' && k[0] < '1' + N) return col(k[0] - '1');
    if (k.size() == 2 && k[0] == 'c' && k[1] >= '1' && k[1] < '1' + N) return col(k[1] - '1');
    if (k.size() == 2 && k[0] == 'r' && k[1] >= '1' && k[1] < '1' + N) return row(k[1] - '1');
    if (k.size() == 3 && k[0] == 'v' && k[1] >= '1' && k[1] < '1' + N && k[2] >= '1' && k[2] < '1' + N) {
        lua_pushnumber(L, u->at(k[1] - '1', k[2] - '1'));
        return 1;
    }
    return 0;
}
template <int N> int matNewIndex(lua_State* L) {
    MatUD* u = selfMatOp<N>(L, "indexing", true);
    if (lua_isnoneornil(L, 2)) notNil(L, 2, "__newindex", "String");
    if (lua_type(L, 2) != LUA_TNUMBER && lua_type(L, 2) != LUA_TSTRING) badArg(L, 2, "__newindex", "String");
    std::string k;
    keyString(L, 2, k);
    if (VecUD* v = testVecN(L, 3, N)) {
        if ((k.size() == 1 && k[0] >= '1' && k[0] < '1' + N) || (k.size() == 2 && k[0] == 'c' && k[1] >= '1' && k[1] < '1' + N)) {
            int c = k.back() - '1';
            for (int r = 0; r < N; r++) u->at(r, c) = v->v[r];
        } else if (k.size() == 2 && k[0] == 'r' && k[1] >= '1' && k[1] < '1' + N) {
            int r = k[1] - '1';
            for (int c = 0; c < N; c++) u->at(r, c) = v->v[c];
        }
        if (!std::isnan(u->cDet) || u->hasInv) snapshot(u); // Figura does not invalidate here (stale caches kept)
        return 0;
    }
    if (isJNumber(L, 3)) {
        if (k.size() == 3 && k[0] == 'v' && k[1] >= '1' && k[1] < '1' + N && k[2] >= '1' && k[2] < '1' + N)
            u->at(k[1] - '1', k[2] - '1') = lua_tonumber(L, 3);
        if (!std::isnan(u->cDet) || u->hasInv) snapshot(u);
        return 0;
    }
    raise(L, "Illegal arguments to " + matName(N) + " __newindex: " + k + ", " + javaValueString(L, 3));
}
template <int N> int matToStringMeta(lua_State* L) {
    MatUD* u = selfMatOp<N>(L, "tostring", false);
    lua_pushstring(L, matToString(u).c_str());
    return 1;
}
template <int N> int matAdd(lua_State* L) {
    MatUD* u = selfMatOp<N>(L, "+", true);
    MatUD* o = jMat(L, 2, N, "__add", true);
    MatUD* r = newMatCopy(L, u);
    for (int i = 0; i < N * N; i++) r->d()[i] += o->d()[i];
    return 1;
}
template <int N> int matSub(lua_State* L) {
    MatUD* u = selfMatOp<N>(L, "-", true);
    MatUD* o = jMat(L, 2, N, "__sub", true);
    MatUD* r = newMatCopy(L, u);
    for (int i = 0; i < N * N; i++) r->d()[i] -= o->d()[i];
    return 1;
}
template <int N> int matMul(lua_State* L) { // instance __mul(@LuaNotNil Object o)
    MatUD* u = selfMatOp<N>(L, "*", true);
    if (lua_isnoneornil(L, 2)) notNil(L, 2, "__mul", "AnyType");
    if (MatUD* o = testMatN(L, 2, N)) { // mat.times(this) = o.copy().multiply(this) = this * o
        guard(o);
        MatUD* r = newMatCopy(L, o);
        mulInto(r, u, false);
        return 1;
    }
    if (VecUD* v = testVecN(L, 2, N)) {
        VecUD* r = newVec(L, N, v->v);
        transformVec(r->v, N, u);
        return 1;
    }
    if (isJNumber(L, 2)) {
        double d = lua_tonumber(L, 2);
        MatUD* r = newMatCopy(L, u);
        for (int i = 0; i < N * N; i++) r->d()[i] *= d;
        return 1;
    }
    raise(L, "Invalid types to " + matName(N) + " __mul: " + simpleName(L, 2));
}
int matEq(lua_State* L) {
    MatUD *a = testMat(L, 1), *b = testMat(L, 2);
    bool   eq = a && b && a->n == b->n;
    for (int i = 0; eq && i < a->n * a->n; i++) eq = a->d()[i] == b->d()[i];
    lua_pushboolean(L, eq);
    return 1;
}
template <int N> int matLen(lua_State* L) { selfMatOp<N>(L, "#", false); lua_pushinteger(L, N); return 1; }

// ---- methods ----
template <int N> int mmCopy(lua_State* L) { newMatCopy(L, selfMat<N>(L, "copy")); return 1; }
template <int N> int mmGetColumn(lua_State* L) {
    MatUD* u = selfMat<N>(L, "getColumn");
    int    c = jInt(L, 2, "getColumn");
    if (c < 1 || c > N) raise(L, "Column must be 1 to " + std::to_string(N));
    double v[4];
    for (int r = 0; r < N; r++) v[r] = u->at(r, c - 1);
    newVec(L, N, v);
    return 1;
}
template <int N> int mmGetRow(lua_State* L) {
    MatUD* u = selfMat<N>(L, "getRow");
    int    r = jInt(L, 2, "getRow");
    if (r < 1 || r > N) raise(L, "Row must be 1 to " + std::to_string(N));
    double v[4];
    for (int c = 0; c < N; c++) v[c] = u->at(r - 1, c);
    newVec(L, N, v);
    return 1;
}
template <int N> int mmSet(lua_State* L) {
    MatUD* u = selfMat<N>(L, "set");
    MatUD* o = jMat(L, 2, N, "set", true);
    std::memcpy(u->d(), o->d(), sizeof(double) * N * N);
    invalidate(u);
    lua_settop(L, 1);
    return 1;
}
template <int N> int mmMultiply(lua_State* L) { MatUD* u = selfMat<N>(L, "multiply"); MatUD* o = jMat(L, 2, N, "multiply", true); mulInto(u, o, false); lua_settop(L, 1); return 1; }
template <int N> int mmRightMultiply(lua_State* L) { MatUD* u = selfMat<N>(L, "rightMultiply"); MatUD* o = jMat(L, 2, N, "rightMultiply", true); mulInto(u, o, true); lua_settop(L, 1); return 1; }
template <int N> int mmTranspose(lua_State* L) {
    MatUD* u = selfMat<N>(L, "transpose");
    transposeVals(u->d(), N);
    u->hasInv = false; // transposing doesn't invalidate the determinant
    snapshot(u);
    lua_settop(L, 1);
    return 1;
}
template <int N> int mmTransposed(lua_State* L) { MatUD* u = selfMat<N>(L, "transposed"); MatUD* r = newMatCopy(L, u); transposeVals(r->d(), N); return 1; }
template <int N> int mmInvert(lua_State* L) { MatUD* u = selfMat<N>(L, "invert"); invertMat(u); lua_settop(L, 1); return 1; }
template <int N> int mmInverted(lua_State* L) { MatUD* u = selfMat<N>(L, "inverted"); MatUD* r = newMatCopy(L, u); invertMat(r); return 1; }
template <int N> int mmDet(lua_State* L) { MatUD* u = selfMat<N>(L, "det"); lua_pushnumber(L, det(u)); return 1; }
template <int N> int mmReset(lua_State* L) {
    MatUD* u = selfMat<N>(L, "reset");
    invalidate(u);
    for (int r = 0; r < N; r++)
        for (int c = 0; c < N; c++) u->at(r, c) = r == c ? 1 : 0;
    lua_settop(L, 1);
    return 1;
}
template <int N> int mmAdd(lua_State* L) { MatUD* u = selfMat<N>(L, "add"); MatUD* o = jMat(L, 2, N, "add", true); for (int i = 0; i < N * N; i++) u->d()[i] += o->d()[i]; invalidate(u); lua_settop(L, 1); return 1; }
template <int N> int mmSub(lua_State* L) { MatUD* u = selfMat<N>(L, "sub"); MatUD* o = jMat(L, 2, N, "sub", true); for (int i = 0; i < N * N; i++) u->d()[i] -= o->d()[i]; invalidate(u); lua_settop(L, 1); return 1; }
template <int N> int mmScale(lua_State* L) {
    MatUD* u = selfMat<N>(L, "scale");
    double s[3];
    if constexpr (N == 2) {
        auto y = jOptDouble(L, 3, "scale");
        parseVec2(L, 2, y, "scale", s, 1, 1);
        scaleRows(u, s, 2);
    } else {
        auto y = jOptDouble(L, 3, "scale"), z = jOptDouble(L, 4, "scale");
        parseOneArgVec(L, 2, y, z, "scale", s, 1);
        scaleRows(u, s, 3);
    }
    lua_settop(L, 1);
    return 1;
}
int mm2Rotate(lua_State* L) { // FiguraMat2.rotate(Double) - no invalidate() in Figura
    MatUD* u = selfMat<2>(L, "rotate");
    auto   d = jOptDouble(L, 2, "rotate");
    if (d) {
        double r = *d * kDegToRad, c = std::cos(r), s = std::sin(r);
        double v11 = u->at(0, 0), v12 = u->at(0, 1), v21 = u->at(1, 0), v22 = u->at(1, 1);
        u->at(0, 0) = c * v11 - s * v21;
        u->at(0, 1) = c * v12 - s * v22;
        u->at(1, 0) = c * v21 + s * v11;
        u->at(1, 1) = c * v22 + s * v12;
        if (!std::isnan(u->cDet) || u->hasInv) snapshot(u);
    }
    lua_settop(L, 1);
    return 1;
}
template <int N> int mmRotate(lua_State* L) {
    MatUD* u = selfMat<N>(L, "rotate");
    double a[3];
    argsVec3(L, 2, "rotate", a);
    rotateZYX(u, a[0], a[1], a[2]);
    lua_settop(L, 1);
    return 1;
}
template <int N> int mmRotateX(lua_State* L) { MatUD* u = selfMat<N>(L, "rotateX"); rotateX(u, jDouble(L, 2, "rotateX")); lua_settop(L, 1); return 1; }
template <int N> int mmRotateY(lua_State* L) { MatUD* u = selfMat<N>(L, "rotateY"); rotateY(u, jDouble(L, 2, "rotateY")); lua_settop(L, 1); return 1; }
template <int N> int mmRotateZ(lua_State* L) { MatUD* u = selfMat<N>(L, "rotateZ"); rotateZ(u, jDouble(L, 2, "rotateZ")); lua_settop(L, 1); return 1; }
int mm3Translate(lua_State* L) {
    MatUD* u = selfMat<3>(L, "translate");
    auto   y = jOptDouble(L, 3, "translate");
    double t[2];
    parseVec2(L, 2, y, "translate", t);
    translateRows(u, t, 2);
    lua_settop(L, 1);
    return 1;
}
int mm4Translate(lua_State* L) {
    MatUD* u = selfMat<4>(L, "translate");
    double t[3];
    argsVec3(L, 2, "translate", t);
    translateRows(u, t, 3);
    lua_settop(L, 1);
    return 1;
}
template <int N> int mmAugmented(lua_State* L) {
    MatUD* u = selfMat<N>(L, "augmented");
    MatUD* r = newMat(L, N + 1);
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) r->at(i, j) = u->at(i, j);
    return 1;
}
template <int N> int mmDeaugmented(lua_State* L) {
    MatUD* u = selfMat<N>(L, "deaugmented");
    MatUD* r = newMat(L, N - 1);
    for (int i = 0; i < N - 1; i++)
        for (int j = 0; j < N - 1; j++) r->at(i, j) = u->at(i, j);
    return 1;
}
int mm2Apply(lua_State* L) { // double apply(double x): (M * (x, 1)).x
    MatUD* u = selfMat<2>(L, "apply");
    double v[2] = {jDouble(L, 2, "apply"), 1};
    transformVec(v, 2, u);
    lua_pushnumber(L, v[0]);
    return 1;
}
int mm2ApplyDir(lua_State* L) {
    MatUD* u = selfMat<2>(L, "applyDir");
    double v[2] = {jDouble(L, 2, "applyDir"), 0};
    transformVec(v, 2, u);
    lua_pushnumber(L, v[0]);
    return 1;
}
template <bool Dir> int mm3Apply(lua_State* L) { // FiguraVec2 apply(Object x, Double y) / applyDir(Object x, Double y, Double z)
    char const* fn = Dir ? "applyDir" : "apply";
    MatUD*      u  = selfMat<3>(L, fn);
    auto        y  = jOptDouble(L, 3, fn);
    if constexpr (Dir) (void)jOptDouble(L, 4, fn);
    double p[2];
    parseVec2(L, 2, y, fn, p);
    double v[3] = {p[0], p[1], Dir ? 0.0 : 1.0};
    transformVec(v, 3, u);
    newVec2(L, v[0], v[1]);
    return 1;
}
template <bool Dir> int mm4Apply(lua_State* L) {
    char const* fn = Dir ? "applyDir" : "apply";
    MatUD*      u  = selfMat<4>(L, fn);
    double      p[3];
    argsVec3(L, 2, fn, p);
    double v[4] = {p[0], p[1], p[2], Dir ? 0.0 : 1.0};
    transformVec(v, 4, u);
    newVec3(L, v[0], v[1], v[2]);
    return 1;
}

template <int N> void registerMat(lua_State* L) {
    TypeDef d;
    d.name    = N == 2 ? "Matrix2" : N == 3 ? "Matrix3" : "Matrix4";
    d.methods = {{"copy", mmCopy<N>}, {"getColumn", mmGetColumn<N>}, {"getRow", mmGetRow<N>}, {"set", mmSet<N>},
                 {"multiply", mmMultiply<N>}, {"rightMultiply", mmRightMultiply<N>}, {"transpose", mmTranspose<N>},
                 {"transposed", mmTransposed<N>}, {"invert", mmInvert<N>}, {"inverted", mmInverted<N>},
                 {"det", mmDet<N>}, {"reset", mmReset<N>}, {"add", mmAdd<N>}, {"sub", mmSub<N>}, {"scale", mmScale<N>}};
    if constexpr (N == 2) {
        d.methods.push_back({"rotate", mm2Rotate});
        d.methods.push_back({"augmented", mmAugmented<2>});
        d.methods.push_back({"apply", mm2Apply});
        d.methods.push_back({"applyDir", mm2ApplyDir});
    } else {
        d.methods.push_back({"rotate", mmRotate<N>});
        d.methods.push_back({"rotateX", mmRotateX<N>});
        d.methods.push_back({"rotateY", mmRotateY<N>});
        d.methods.push_back({"rotateZ", mmRotateZ<N>});
        d.methods.push_back({"deaugmented", mmDeaugmented<N>});
        if constexpr (N == 3) {
            d.methods.push_back({"translate", mm3Translate});
            d.methods.push_back({"augmented", mmAugmented<3>});
            d.methods.push_back({"apply", mm3Apply<false>});
            d.methods.push_back({"applyDir", mm3Apply<true>});
        } else {
            d.methods.push_back({"translate", mm4Translate});
            d.methods.push_back({"apply", mm4Apply<false>});
            d.methods.push_back({"applyDir", mm4Apply<true>});
        }
    }
    d.index    = matIndex<N>;
    d.newindex = matNewIndex<N>;
    d.tostring = matToStringMeta<N>;
    d.meta     = {{"__add", matAdd<N>}, {"__sub", matSub<N>}, {"__mul", matMul<N>}, {"__eq", matEq}, {"__len", matLen<N>}};
    gMatT[N]   = registerType(L, d);
    lua_pushlightuserdata(L, (void*)&kMatMtKey[N]);
    pushTypeMetatable(L, gMatT[N]);
    lua_rawset(L, LUA_REGISTRYINDEX);
}

// ---------------------------------------------------------------------------------------------------------------------
// MatricesAPI
// ---------------------------------------------------------------------------------------------------------------------
#define MAPI_SELF dropApiSelf(L, &gMatricesApi, gMatricesApiT)
template <int N> int apiMat(lua_State* L) {
    MAPI_SELF;
    char const* fn = N == 2 ? "mat2" : N == 3 ? "mat3" : "mat4";
    VecUD*      cols[4];
    int         nils = 0;
    for (int i = 0; i < N; i++) {
        cols[i] = jVec(L, i + 1, N, fn, false);
        if (!cols[i]) nils++;
    }
    if (nils == N) { newMat(L, N); return 1; }
    if (nils) raise(L, std::string("Invalid arguments to ") + fn + "(), needs 0 or " + std::to_string(N) + " arguments!");
    MatUD* r = newMat(L, N);
    for (int c = 0; c < N; c++)
        for (int rr = 0; rr < N; rr++) r->at(rr, c) = cols[c]->v[rr];
    return 1;
}
int apiRotation2(lua_State* L) {
    MAPI_SELF;
    double d = jDouble(L, 1, "rotation2");
    MatUD* u = newMat(L, 2);
    double r = d * kDegToRad, c = std::cos(r), s = std::sin(r);
    u->at(0, 0) = c * 1 - s * 0; u->at(0, 1) = c * 0 - s * 1;
    u->at(1, 0) = c * 0 + s * 1; u->at(1, 1) = c * 1 + s * 0;
    return 1;
}
template <int N> int apiRotation(lua_State* L) {
    MAPI_SELF;
    double a[3];
    argsVec3(L, 1, N == 3 ? "rotation3" : "rotation4", a);
    MatUD* u = newMat(L, N);
    rotateZYX(u, a[0], a[1], a[2]);
    return 1;
}
template <int N, int Axis> int apiAxisRotation(lua_State* L) {
    MAPI_SELF;
    static char const* names[2][3] = {{"xRotation3", "yRotation3", "zRotation3"}, {"xRotation4", "yRotation4", "zRotation4"}};
    double d = jDouble(L, 1, names[N - 3][Axis]);
    MatUD* u = newMat(L, N);
    if (Axis == 0) rotateX(u, d);
    else if (Axis == 1) rotateY(u, d);
    else rotateZ(u, d);
    return 1;
}
int apiScale2(lua_State* L) {
    MAPI_SELF;
    auto   y = jOptDouble(L, 2, "scale2");
    double s[2];
    parseVec2(L, 1, y, "scale2", s, 1, 1);
    MatUD* u = newMat(L, 2);
    scaleRows(u, s, 2);
    return 1;
}
template <int N> int apiScale(lua_State* L) {
    MAPI_SELF;
    char const* fn = N == 3 ? "scale3" : "scale4";
    auto        y = jOptDouble(L, 2, fn), z = jOptDouble(L, 3, fn);
    double      s[3];
    parseOneArgVec(L, 1, y, z, fn, s, 1);
    MatUD* u = newMat(L, N);
    scaleRows(u, s, 3);
    return 1;
}
int apiTranslate3(lua_State* L) {
    MAPI_SELF;
    auto   y = jOptDouble(L, 2, "translate3");
    double t[2];
    parseVec2(L, 1, y, "translate3", t);
    MatUD* u = newMat(L, 3);
    translateRows(u, t, 2);
    return 1;
}
int apiTranslate4(lua_State* L) {
    MAPI_SELF;
    double t[3];
    argsVec3(L, 1, "translate4", t);
    MatUD* u = newMat(L, 4);
    translateRows(u, t, 3);
    return 1;
}
#undef MAPI_SELF

int apiToString(lua_State* L) {
    lua_pushstring(L, toRef(L, 1, gVectorsApiT) ? "VectorsAPI" : "MatricesAPI");
    return 1;
}

} // namespace

// =====================================================================================================================
// exported helpers
// =====================================================================================================================
uint32_t vecType(int n) { return n >= 2 && n <= 4 ? gVecT[n] : 0; }

void pushVec(lua_State* L, double const* v, int n) { newVec(L, n, v); }
double* toVec(lua_State* L, int idx, int* n) {
    VecUD* u = testVec(L, idx);
    if (!u) return nullptr;
    if (n) *n = u->n;
    return u->v;
}
Vec3 parseVec3(lua_State* L, int idx, char const* fn, double def) {
    if (VecUD* u = testVec(L, idx)) {
        if (u->n != 3) raise(L, std::string("Illegal argument to ") + fn + "(): FiguraVec" + std::to_string(u->n));
        return {u->v[0], u->v[1], u->v[2]};
    }
    if (!lua_isnoneornil(L, idx) && !isJNumber(L, idx))
        raise(L, std::string("Illegal argument to ") + fn + "(): " + simpleName(L, idx));
    return {optNum(L, idx, def), optNum(L, idx + 1, def), optNum(L, idx + 2, def)};
}
bool optVec3(lua_State* L, int idx, Vec3* out) {
    if (lua_isnoneornil(L, idx) && lua_isnoneornil(L, idx + 1) && lua_isnoneornil(L, idx + 2)) return false;
    *out = parseVec3(L, idx, "vec3", 0);
    return true;
}

void pushMat4(lua_State* L, Mat4 const& m) {
    MatUD* u = newMat(L, 4);
    u->m     = m;
}
Mat4* toMat4(lua_State* L, int idx) {
    MatUD* u = testMatN(L, idx, 4);
    return u ? &u->m : nullptr;
}
void pushMat(lua_State* L, double const* rowMajor, int n) {
    MatUD* u = newMat(L, n);
    std::memcpy(u->d(), rowMajor, sizeof(double) * size_t(n * n));
}
double* toMat(lua_State* L, int idx, int* n) {
    MatUD* u = testMat(L, idx);
    if (!u) return nullptr;
    if (n) *n = u->n;
    return u->d();
}

void openVectors(lua_State* L) {
    registerVec<2>(L);
    registerVec<3>(L);
    registerVec<4>(L);
    TypeDef d;
    d.name    = "VectorsAPI";
    d.methods = {{"vec", apiVec}, {"vec2", apiVecN<2>}, {"vec3", apiVecN<3>}, {"vec4", apiVecN<4>},
                 {"rgbToInt", apiRgbToInt}, {"intToRGB", apiIntToRgb}, {"hexToRGB", apiHexToRgb},
                 {"hsvToRGB", apiHsvToRgb}, {"rgbToHSV", apiRgbToHsv}, {"rgbToHex", apiRgbToHex},
                 {"rotateAroundAxis", apiRotateAroundAxis}, {"toCameraSpace", apiToCameraSpace},
                 {"worldToScreenSpace", apiWorldToScreenSpace}, {"angleToDir", apiAngleToDir}};
    d.tostring   = apiToString;
    gVectorsApiT = registerType(L, d);
    pushRef(L, &gVectorsApi, gVectorsApiT);
    lua_setglobal(L, "vectors");
}

void openMatrices(lua_State* L) {
    registerMat<2>(L);
    registerMat<3>(L);
    registerMat<4>(L);
    TypeDef d;
    d.name    = "MatricesAPI";
    d.methods = {{"mat2", apiMat<2>}, {"mat3", apiMat<3>}, {"mat4", apiMat<4>}, {"rotation2", apiRotation2},
                 {"rotation3", apiRotation<3>}, {"xRotation3", apiAxisRotation<3, 0>}, {"yRotation3", apiAxisRotation<3, 1>},
                 {"zRotation3", apiAxisRotation<3, 2>}, {"rotation4", apiRotation<4>}, {"xRotation4", apiAxisRotation<4, 0>},
                 {"yRotation4", apiAxisRotation<4, 1>}, {"zRotation4", apiAxisRotation<4, 2>}, {"scale2", apiScale2},
                 {"scale3", apiScale<3>}, {"scale4", apiScale<4>}, {"translate3", apiTranslate3},
                 {"translate4", apiTranslate4}};
    d.tostring     = apiToString;
    gMatricesApiT  = registerType(L, d);
    pushRef(L, &gMatricesApi, gMatricesApiT);
    lua_setglobal(L, "matrices");
}

} // namespace bf::figura::lua
