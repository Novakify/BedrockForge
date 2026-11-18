// FiguraBridge Lua API: json (JsonAPI + JsonArray/JsonObject/JsonBuilder/JsonSerializer), data (DataAPI + Buffer,
// InputStream, OutputStream, Future), file (FileAPI), net (NetworkingAPI + HttpAPI/HttpRequestBuilder/HttpResponse)
// and resources (ResourcesAPI). Port of Figura 0.1.6 org.figuramc.figura.lua.api.{json,data,net}, FileAPI and
// FiguraLuaJson (exported as figuraToJson / figuraParseJson for the toJson / parseJson globals).
//
// Conventions copied from LuaTypeManager: instance methods called with '.' raise "Use a colon (:) ..."; @LuaNotNil
// arguments raise "bad argument: <m> <i> do not allow nil values, expected <T>"; wrong argument types raise
// "Invalid argument <i> to function <m>. Expected <T>, but got <t>"; Java exceptions surface as
// "vm error: <java.class.Name>: <message>" (LuaError(Throwable)). JSON output follows Gson 2.10 JsonWriter.
//
// Host policy: networking behaves like Figura with "allow_networking" OFF (the default); FileAPI is the host-only API
// rooted at Avatar::dataDir (empty = not a host environment -> refuses). Paths may not be absolute or contain "..".
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <tuple>
#include <iterator>
#include <vector>

#include "figura/core/Avatar.h"
#include "figura/core/LuaApi.h"
#include "figura/core/LuaBind.h"
#include "lyrics/core/Json.h"

namespace bf::figura {

namespace data {
bool gunzip(std::vector<uint8_t> const& in, std::vector<uint8_t>& out, std::string* err); // LuaDataGzip.cpp
}

using namespace lua;

namespace {

uint32_t tJsonAPI, tJsonArray, tJsonObject, tJsonBuilder, tJsonSerializer;
uint32_t tDataAPI, tBuffer, tInputStream, tOutputStream, tFuture;
uint32_t tFileAPI, tNetworkingAPI, tHttpAPI, tHttpRequestBuilder, tHttpResponse, tResourcesAPI;

char kNilSentinel;   // stands for a Java-side null stored in a container (JsonArray/JsonObject contents)
char kBufferCount;   // registry key: Avatar.openBuffers.size()
int  gApiAnchors[8]; // identities of the singleton API objects

// =====================================================================================================================
// errors / arguments (LuaTypeManager wrappers)
// =====================================================================================================================
[[noreturn]] void fail(lua_State* L, std::string const& msg) {
    luaL_where(L, 1);
    std::string w = lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);
    raise(L, w + msg);
}
/// LuaError(Throwable): "vm error: " + throwable.toString()
[[noreturn]] void javaFail(lua_State* L, std::string const& cls, std::string const& msg = std::string(), bool hasMsg = true) {
    fail(L, "vm error: " + cls + (hasMsg ? ": " + msg : std::string()));
}
[[noreturn]] void ioFail(lua_State* L, std::string const& msg) { javaFail(L, "java.io.IOException", msg); }

std::string actualType(lua_State* L, int i) {
    if (lua_type(L, i) == LUA_TUSERDATA) return figuraTypeName(L, i);
    return lua_type(L, i) == LUA_TNONE ? "nil" : luaL_typename(L, i);
}
bool isNil(lua_State* L, int i) { return lua_isnoneornil(L, i); }
void notNil(lua_State* L, int i, char const* fn, char const* expected) {
    if (isNil(L, i)) fail(L, std::string("bad argument: ") + fn + " " + std::to_string(i) + " do not allow nil values, expected " + expected);
}
[[noreturn]] void badArg(lua_State* L, int i, char const* fn, char const* expected) {
    fail(L, "Invalid argument " + std::to_string(i) + " to function " + fn + ". Expected " + expected + ", but got " + actualType(L, i));
}
int64_t javaLong(double d) {
    if (std::isnan(d)) return 0;
    if (d >= 9.2233720368547758e18) return INT64_MAX;
    if (d <= -9.2233720368547758e18) return INT64_MIN;
    return int64_t(d);
}
int32_t javaInt(double d) { return int32_t(uint32_t(uint64_t(javaLong(d)))); } // LuaJ checkint: (int) (long) v

std::optional<double> argNum(lua_State* L, int i, char const* fn, char const* expected, bool required) {
    if (isNil(L, i)) {
        if (required) notNil(L, i, fn, expected);
        return std::nullopt;
    }
    if (!lua_isnumber(L, i)) badArg(L, i, fn, expected);
    return lua_tonumber(L, i);
}
std::optional<int32_t> argInt(lua_State* L, int i, char const* fn, bool required = false) {
    auto d = argNum(L, i, fn, "Integer", required);
    if (!d) return std::nullopt;
    return javaInt(*d);
}
std::optional<int64_t> argLong(lua_State* L, int i, char const* fn, bool required = false) {
    auto d = argNum(L, i, fn, "Integer", required);
    if (!d) return std::nullopt;
    return javaLong(*d);
}
std::optional<std::string> argStr(lua_State* L, int i, char const* fn, bool required = false) {
    if (isNil(L, i)) {
        if (required) notNil(L, i, fn, "String");
        return std::nullopt;
    }
    if (!lua_isstring(L, i)) badArg(L, i, fn, "String");
    lua_pushvalue(L, i); // don't convert a number in place
    size_t      n;
    char const* s = lua_tolstring(L, -1, &n);
    std::string r(s, n);
    lua_pop(L, 1);
    return r;
}
template <class T>
T* self(lua_State* L, uint32_t type, char const* m) {
    T* p = to<T>(L, 1, type);
    if (!p)
        fail(L, std::string("Use a colon (:) to call ") + m + " on a " + typeName(type) + ", instead of a dot.\nFor example, change ." + m + "( to :" + m + "(");
    return p;
}
int pushNil(lua_State* L) { lua_pushnil(L); return 1; }
int pushBool(lua_State* L, bool b) { lua_pushboolean(L, b); return 1; }
int pushInt(lua_State* L, int64_t v) { lua_pushnumber(L, double(v)); return 1; }
int pushStr(lua_State* L, std::string const& s) { lua_pushlstring(L, s.data(), s.size()); return 1; }

// =====================================================================================================================
// owned userdata objects (RefUD layout so LuaBind's to/check work; one Lua object per C++ object; __gc deletes)
// =====================================================================================================================
template <class T>
T* newObj(lua_State* L, uint32_t type, T* obj) {
    auto* ud = static_cast<RefUD*>(lua_newuserdata(L, sizeof(RefUD)));
    ud->ptr  = nullptr;
    ud->type = type;
    luaL_setmetatable(L, (std::string("fb:") + typeName(type)).c_str());
    ud->ptr = obj;
    lua_newtable(L); // uservalue: Lua values the object holds
    lua_setuservalue(L, -2);
    return obj;
}
template <class T>
int gcObj(lua_State* L) {
    auto* ud = static_cast<RefUD*>(lua_touserdata(L, 1));
    if (ud && ud->ptr) {
        delete static_cast<T*>(ud->ptr);
        ud->ptr = nullptr;
    }
    return 0;
}
/// uservalue[slot] (nil sentinel -> nil)
void pushHeld(lua_State* L, int obj, int slot) {
    lua_getuservalue(L, obj);
    lua_rawgeti(L, -1, slot);
    lua_remove(L, -2);
    if (lua_touserdata(L, -1) == &kNilSentinel) { lua_pop(L, 1); lua_pushnil(L); }
}
/// uservalue[slot] = value at idx (nil stored as the sentinel)
void setHeld(lua_State* L, int obj, int slot, int idx) {
    obj = lua_absindex(L, obj);
    idx = lua_absindex(L, idx);
    lua_getuservalue(L, obj);
    if (isNil(L, idx)) lua_pushlightuserdata(L, &kNilSentinel);
    else lua_pushvalue(L, idx);
    lua_rawseti(L, -2, slot);
    lua_pop(L, 1);
}

// =====================================================================================================================
// Java strings: Lua bytes <-> UTF-16 (LuaString.tojstring / LuaString.valueOf), charsets of Buffer/FileAPI
// =====================================================================================================================
std::u16string toU16(std::string_view s) {
    std::u16string r;
    r.reserve(s.size());
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = s[i];
        uint32_t      cp;
        int           len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else { r += char16_t(0xFFFD); i++; continue; }
        if (i + len > n) { r += char16_t(0xFFFD); i++; continue; }
        bool ok = true;
        for (int k = 1; k < len; k++) {
            unsigned char cc = s[i + k];
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            cp = cp << 6 | (cc & 0x3F);
        }
        if (!ok) { r += char16_t(0xFFFD); i++; continue; }
        if (len == 4) {
            cp -= 0x10000;
            r += char16_t(0xD800 + (cp >> 10));
            r += char16_t(0xDC00 + (cp & 0x3FF));
        } else r += char16_t(cp);
        i += len;
    }
    return r;
}
void putUtf8(std::string& o, uint32_t cp) {
    if (cp < 0x80) o += char(cp);
    else if (cp < 0x800) { o += char(0xC0 | cp >> 6); o += char(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { o += char(0xE0 | cp >> 12); o += char(0x80 | (cp >> 6 & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
    else { o += char(0xF0 | cp >> 18); o += char(0x80 | (cp >> 12 & 0x3F)); o += char(0x80 | (cp >> 6 & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
}
std::string fromU16(std::u16string_view s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        uint32_t c = s[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) {
            putUtf8(o, 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00));
            i++;
        } else putUtf8(o, c); // lone surrogates: 3 bytes, like LuaString.valueOf(String)
    }
    return o;
}

enum class Charset { Utf8, Utf16, Utf16BE, Utf16LE, Ascii, Latin1 };
Charset charsetOf(std::optional<std::string> const& enc) {
    if (!enc) return Charset::Utf8;
    std::string e = *enc;
    for (auto& c : e) c = char(std::tolower((unsigned char)c));
    if (e == "utf_16" || e == "utf16") return Charset::Utf16;
    if (e == "utf_16be" || e == "utf16be") return Charset::Utf16BE;
    if (e == "utf_16le" || e == "utf16le") return Charset::Utf16LE;
    if (e == "ascii") return Charset::Ascii;
    if (e == "iso_8859_1" || e == "iso88591") return Charset::Latin1;
    return Charset::Utf8;
}
/// String.getBytes(charset)
std::string encodeJava(std::u16string const& s, Charset cs) {
    std::string o;
    auto isHi = [](char16_t c) { return c >= 0xD800 && c < 0xDC00; };
    auto isLo = [](char16_t c) { return c >= 0xDC00 && c < 0xE000; };
    switch (cs) {
    case Charset::Utf8:
        for (size_t i = 0; i < s.size(); i++) {
            char16_t c = s[i];
            if (isHi(c) && i + 1 < s.size() && isLo(s[i + 1])) { putUtf8(o, 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00)); i++; }
            else if (isHi(c) || isLo(c)) o += '?';
            else putUtf8(o, c);
        }
        break;
    case Charset::Utf16:
    case Charset::Utf16BE:
    case Charset::Utf16LE: {
        bool le = cs == Charset::Utf16LE;
        if (cs == Charset::Utf16) { o += char(0xFE); o += char(0xFF); }
        for (size_t i = 0; i < s.size(); i++) {
            char16_t c = s[i];
            if (isHi(c) && i + 1 < s.size() && isLo(s[i + 1])) {
                for (char16_t u : {c, s[i + 1]}) {
                    if (le) { o += char(u & 0xFF); o += char(u >> 8); }
                    else { o += char(u >> 8); o += char(u & 0xFF); }
                }
                i++;
                continue;
            }
            if (isHi(c) || isLo(c)) c = u'\xFFFD';
            if (le) { o += char(c & 0xFF); o += char(c >> 8); }
            else { o += char(c >> 8); o += char(c & 0xFF); }
        }
        break;
    }
    case Charset::Ascii:
    case Charset::Latin1: {
        unsigned lim = cs == Charset::Ascii ? 0x80 : 0x100;
        for (size_t i = 0; i < s.size(); i++) {
            char16_t c = s[i];
            if (isHi(c) && i + 1 < s.size() && isLo(s[i + 1])) { o += '?'; i++; }
            else o += c < lim ? char(c) : '?';
        }
        break;
    }
    }
    return o;
}
/// new String(bytes, charset)
std::u16string decodeJava(std::string const& b, Charset cs) {
    std::u16string r;
    switch (cs) {
    case Charset::Utf8: {
        // Java's decoder: well-formed sequences only (no surrogates / overlongs), malformed -> U+FFFD
        size_t i = 0, n = b.size();
        while (i < n) {
            unsigned char c = b[i];
            if (c < 0x80) { r += char16_t(c); i++; continue; }
            int      len = 0;
            uint32_t cp = 0, min = 0;
            if (c >= 0xC2 && c <= 0xDF) { len = 2; cp = c & 0x1F; min = 0x80; }
            else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; min = 0x800; }
            else if (c >= 0xF0 && c <= 0xF4) { len = 4; cp = c & 0x07; min = 0x10000; }
            if (!len) { r += u'\xFFFD'; i++; continue; }
            int k = 1;
            for (; k < len && i + k < n; k++) {
                unsigned char cc = b[i + k];
                if ((cc & 0xC0) != 0x80) break;
                cp = cp << 6 | (cc & 0x3F);
                if (k == 1 && ((len == 3 && ((c == 0xE0 && cc < 0xA0) || (c == 0xED && cc >= 0xA0))) ||
                               (len == 4 && ((c == 0xF0 && cc < 0x90) || (c == 0xF4 && cc >= 0x90))))) break;
            }
            if (k < len || cp < min) { r += u'\xFFFD'; i += std::max(1, k); continue; }
            if (cp >= 0x10000) { cp -= 0x10000; r += char16_t(0xD800 + (cp >> 10)); r += char16_t(0xDC00 + (cp & 0x3FF)); }
            else r += char16_t(cp);
            i += len;
        }
        break;
    }
    case Charset::Utf16:
    case Charset::Utf16BE:
    case Charset::Utf16LE: {
        bool   le = cs == Charset::Utf16LE;
        size_t i  = 0;
        if (cs == Charset::Utf16 && b.size() >= 2) {
            unsigned char b0 = b[0], b1 = b[1];
            if (b0 == 0xFE && b1 == 0xFF) i = 2;
            else if (b0 == 0xFF && b1 == 0xFE) { le = true; i = 2; }
        }
        std::u16string units;
        for (; i + 1 < b.size(); i += 2) {
            unsigned char x = b[i], y = b[i + 1];
            units += char16_t(le ? (y << 8 | x) : (x << 8 | y));
        }
        for (size_t k = 0; k < units.size(); k++) {
            char16_t c = units[k];
            if (c >= 0xD800 && c < 0xDC00) {
                if (k + 1 < units.size() && units[k + 1] >= 0xDC00 && units[k + 1] < 0xE000) { r += c; r += units[++k]; }
                else r += u'\xFFFD';
            } else if (c >= 0xDC00 && c < 0xE000) r += u'\xFFFD';
            else r += c;
        }
        if (b.size() % 2) r += u'\xFFFD';
        break;
    }
    case Charset::Ascii:
        for (unsigned char c : b) r += c < 0x80 ? char16_t(c) : u'\xFFFD';
        break;
    case Charset::Latin1:
        for (unsigned char c : b) r += char16_t(c);
        break;
    }
    return r;
}
int32_t javaHash(std::u16string const& s) {
    uint32_t h = 0;
    for (char16_t c : s) h = 31 * h + c;
    return int32_t(h);
}

/// Java's iteration order of a HashMap<String, ?> that never shrinks: buckets of the current table, insertion order
/// inside a bucket (treeified bins are ignored).
struct JavaHashOrder {
    struct E {
        uint64_t seq;
        uint32_t spread;
    };
    std::map<std::string, E> m;
    uint64_t                 next = 0;
    uint32_t                 cap  = 16;
    bool insert(std::string const& k) { // false if present
        if (m.count(k)) return false;
        uint32_t h = uint32_t(javaHash(toU16(k)));
        m[k]       = {next++, h ^ (h >> 16)};
        if (m.size() > cap * 3 / 4) cap *= 2;
        return true;
    }
    void erase(std::string const& k) { m.erase(k); }
    void clear() { m.clear(); }
    std::vector<std::string> keys() const {
        std::vector<std::pair<std::pair<uint32_t, uint64_t>, std::string>> v;
        for (auto const& [k, e] : m) v.push_back({{e.spread & (cap - 1), e.seq}, k});
        std::sort(v.begin(), v.end());
        std::vector<std::string> r;
        for (auto& x : v) r.push_back(x.second);
        return r;
    }
};

// =====================================================================================================================
// Java Double.toString
// =====================================================================================================================
std::string javaDouble(double d) {
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
    if (d == 0) return std::signbit(d) ? "-0.0" : "0.0";
    char buf[64];
    int  p = 1;
    for (; p <= 17; p++) {
        std::snprintf(buf, sizeof buf, "%.*e", p - 1, d);
        if (std::strtod(buf, nullptr) == d) break;
    }
    // buf: [-]D[.DDD]e[+-]XX
    std::string s = buf, digits;
    bool        neg = s[0] == '-';
    size_t      e   = s.find('e');
    for (size_t i = neg ? 1 : 0; i < e; i++)
        if (s[i] != '.') digits += s[i];
    int exp = std::atoi(s.c_str() + e + 1);
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    std::string out = neg ? "-" : "";
    double      a   = std::fabs(d);
    if (a >= 1e-3 && a < 1e7) {
        if (exp >= 0) {
            std::string ip = digits.substr(0, std::min<size_t>(digits.size(), size_t(exp) + 1));
            while (ip.size() < size_t(exp) + 1) ip += '0';
            std::string fp = digits.size() > size_t(exp) + 1 ? digits.substr(size_t(exp) + 1) : "0";
            out += ip + "." + fp;
        } else {
            out += "0." + std::string(size_t(-exp - 1), '0') + digits;
        }
    } else {
        out += digits.substr(0, 1) + "." + (digits.size() > 1 ? digits.substr(1) : "0") + "E" + std::to_string(exp);
    }
    return out;
}
/// LuaValue.tojstring() of a number (LuaInteger / LuaDouble)
std::string luajNumStr(double v) {
    if (v == std::floor(v) && std::fabs(v) < 9.2e18) return std::to_string(int64_t(v));
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
    return javaDouble(v);
}

// =====================================================================================================================
// JSON DOM (Gson JsonElement), Lua -> JSON (LuaUtils.asJsonValue), Gson JsonWriter, lenient Gson JsonReader
// =====================================================================================================================
struct JNode {
    enum T : uint8_t { Null, Bool, Int, Num, Str, Arr, Obj } t = Null;
    bool                                       b = false;
    int32_t                                    i = 0;
    double                                     d = 0;
    std::string                                s; // UTF-8 (Lua bytes)
    std::vector<JNode>                         a;
    std::vector<std::string>                   keys; // object members, in JsonObject (insertion) order
    std::vector<JNode>                         vals;
    void add(std::string k, JNode v) { // JsonObject.add: replaces in place
        for (size_t i = 0; i < keys.size(); i++)
            if (keys[i] == k) { vals[i] = std::move(v); return; }
        keys.push_back(std::move(k));
        vals.push_back(std::move(v));
    }
};

struct JsonArrayObj {
    int n = 0; // contents live in uservalue[1..n]
};
struct JsonObjectObj {
    JavaHashOrder keys; // contents live in uservalue[key]
};

bool isFiguraTable(lua_State* L, int idx) { // our table-backed Figura objects (Keybind...) count as userdata
    if (!lua_getmetatable(L, idx)) return false;
    lua_pushstring(L, "__type");
    lua_rawget(L, -2);
    bool r = !lua_isnil(L, -1);
    lua_pop(L, 2);
    return r;
}
/// JsonAPI.isSerializable
bool isSerializable(lua_State* L, int idx) {
    switch (lua_type(L, idx)) {
    case LUA_TNONE:
    case LUA_TNIL:
    case LUA_TBOOLEAN:
    case LUA_TNUMBER:
    case LUA_TSTRING: return true;
    case LUA_TTABLE: return !isFiguraTable(L, idx);
    case LUA_TUSERDATA: return toRef(L, idx, tJsonArray) || toRef(L, idx, tJsonObject);
    default: return false;
    }
}
std::string keyString(lua_State* L, int idx) { // LuaValue.tojstring of a table key
    switch (lua_type(L, idx)) {
    case LUA_TSTRING: {
        size_t      n;
        char const* s = lua_tolstring(L, idx, &n);
        return std::string(s, n);
    }
    case LUA_TNUMBER: return luajNumStr(lua_tonumber(L, idx));
    case LUA_TBOOLEAN: return lua_toboolean(L, idx) ? "true" : "false";
    default: {
        size_t      n;
        char const* s = luaL_tolstring(L, idx, &n);
        std::string r(s, n);
        lua_pop(L, 1);
        return r;
    }
    }
}

void toNode(lua_State* L, int idx, JNode& out, int depth);
void heldArrayToNode(lua_State* L, int obj, JNode& out, int depth) {
    auto* a = to<JsonArrayObj>(L, obj, tJsonArray);
    out.t   = JNode::Arr;
    if (!a) return; // finalized by a script calling __gc
    for (int i = 1; i <= a->n; i++) {
        pushHeld(L, obj, i);
        JNode e;
        toNode(L, -1, e, depth + 1);
        lua_pop(L, 1);
        out.a.push_back(std::move(e));
    }
}
void heldObjectToNode(lua_State* L, int obj, JNode& out, int depth) {
    auto* o = to<JsonObjectObj>(L, obj, tJsonObject);
    out.t   = JNode::Obj;
    if (!o) return;
    for (auto const& k : o->keys.keys()) {
        lua_getuservalue(L, obj);
        lua_pushlstring(L, k.data(), k.size());
        lua_rawget(L, -2);
        lua_remove(L, -2);
        if (lua_touserdata(L, -1) == &kNilSentinel) { lua_pop(L, 1); lua_pushnil(L); }
        JNode e;
        toNode(L, -1, e, depth + 1);
        lua_pop(L, 1);
        out.add(k, std::move(e));
    }
}
void toNode(lua_State* L, int idx, JNode& out, int depth) {
    idx = lua_absindex(L, idx);
    if (depth > 200) javaFail(L, "java.lang.StackOverflowError", "", false);
    luaL_checkstack(L, 8, "json");
    switch (lua_type(L, idx)) {
    case LUA_TBOOLEAN: out.t = JNode::Bool; out.b = lua_toboolean(L, idx) != 0; return;
    case LUA_TSTRING: {
        size_t      n;
        char const* s = lua_tolstring(L, idx, &n);
        out.t         = JNode::Str;
        out.s.assign(s, n);
        return;
    }
    case LUA_TNUMBER: {
        double v = lua_tonumber(L, idx);
        if (v == std::floor(v) && v >= -2147483648.0 && v <= 2147483647.0) { out.t = JNode::Int; out.i = int32_t(v); }
        else { out.t = JNode::Num; out.d = v; }
        return;
    }
    case LUA_TTABLE: {
        if (isFiguraTable(L, idx)) return;
        bool   allNum = true;
        double maxKey = 0;
        lua_pushnil(L);
        while (lua_next(L, idx)) {
            lua_pop(L, 1);
            if (lua_type(L, -1) != LUA_TNUMBER) allNum = false;
            else maxKey = std::max(maxKey, lua_tonumber(L, -1));
        }
        if (allNum && lua_rawlen(L, idx) > 0) {
            out.t   = JNode::Arr;
            int len = javaInt(maxKey);
            for (int i = 1; i <= len; i++) {
                lua_rawgeti(L, idx, i);
                JNode e;
                toNode(L, -1, e, depth + 1);
                lua_pop(L, 1);
                out.a.push_back(std::move(e));
            }
            return;
        }
        out.t = JNode::Obj;
        lua_pushnil(L);
        while (lua_next(L, idx)) {
            std::string k = keyString(L, -2);
            JNode       e;
            toNode(L, -1, e, depth + 1);
            lua_pop(L, 1);
            out.add(std::move(k), std::move(e));
        }
        return;
    }
    case LUA_TUSERDATA:
        if (toRef(L, idx, tJsonArray)) heldArrayToNode(L, idx, out, depth);
        else if (toRef(L, idx, tJsonObject)) heldObjectToNode(L, idx, out, depth);
        return;
    default: return; // nil, functions, threads -> null
    }
}

struct WriterOpts {
    bool pretty = false, html = true, serializeNulls = false;
};
void writeString(std::u16string& w, std::string const& utf8, bool html) {
    static char const hex[] = "0123456789abcdef";
    w += u'"';
    for (char16_t c : toU16(utf8)) {
        char const* rep = nullptr;
        char        tmp[8];
        if (c < 128) {
            switch (c) {
            case '"': rep = "\\\""; break;
            case '\\': rep = "\\\\"; break;
            case '\t': rep = "\\t"; break;
            case '\b': rep = "\\b"; break;
            case '\n': rep = "\\n"; break;
            case '\r': rep = "\\r"; break;
            case '\f': rep = "\\f"; break;
            case '<': case '>': case '&': case '=': case '\'':
                if (html) { std::snprintf(tmp, sizeof tmp, "\\u%04x", unsigned(c)); rep = tmp; }
                break;
            default:
                if (c < 0x20) { tmp[0] = '\\'; tmp[1] = 'u'; tmp[2] = '0'; tmp[3] = '0'; tmp[4] = hex[c >> 4]; tmp[5] = hex[c & 15]; tmp[6] = 0; rep = tmp; }
            }
        } else if (c == 0x2028) rep = "\\u2028";
        else if (c == 0x2029) rep = "\\u2029";
        if (rep) for (char const* p = rep; *p; p++) w += char16_t(*p);
        else w += c;
    }
    w += u'"';
}
void writeAscii(std::u16string& w, std::string const& s) { for (char c : s) w += char16_t((unsigned char)c); }
void newline(std::u16string& w, WriterOpts const& o, int level) {
    if (!o.pretty) return;
    w += u'\n';
    for (int i = 0; i < level; i++) w += u"  ";
}
void writeNode(std::u16string& w, JNode const& n, WriterOpts const& o, int level) {
    switch (n.t) {
    case JNode::Null: writeAscii(w, "null"); break;
    case JNode::Bool: writeAscii(w, n.b ? "true" : "false"); break;
    case JNode::Int: writeAscii(w, std::to_string(n.i)); break;
    case JNode::Num: writeAscii(w, javaDouble(n.d)); break;
    case JNode::Str: writeString(w, n.s, o.html); break;
    case JNode::Arr: {
        w += u'[';
        bool any = false;
        for (auto const& e : n.a) {
            if (any) w += u',';
            newline(w, o, level + 1);
            writeNode(w, e, o, level + 1);
            any = true;
        }
        if (any) newline(w, o, level);
        w += u']';
        break;
    }
    case JNode::Obj: {
        w += u'{';
        bool any = false;
        for (size_t m = 0; m < n.keys.size(); m++) {
            std::string const& k = n.keys[m];
            JNode const&       v = n.vals[m];
            if (v.t == JNode::Null && !o.serializeNulls) continue;
            if (any) w += u',';
            newline(w, o, level + 1);
            writeString(w, k, o.html);
            writeAscii(w, o.pretty ? ": " : ":");
            writeNode(w, v, o, level + 1);
            any = true;
        }
        if (any) newline(w, o, level);
        w += u'}';
        break;
    }
    }
}
std::string writeJson(JNode const& n, WriterOpts const& o) {
    std::u16string w;
    writeNode(w, n, o, 0);
    return fromU16(w);
}

/// Gson JsonParser.parseString (lenient JsonReader). Errors: cause = Throwable.toString() of the JsonSyntaxException
/// cause ("" when it has none), msg = JsonSyntaxException message.
struct JsonErr {
    std::string cause, msg;
};
class GsonReader {
public:
    explicit GsonReader(std::u16string s) : s_(std::move(s)) {}
    JNode parseDocument() {
        // consumeNonExecutePrefix
        size_t p0 = pos_;
        skipWs(false);
        if (s_.compare(pos_, 5, u")]}'\n") == 0) pos_ += 5;
        else pos_ = p0;
        if (!skipWs(false)) return JNode{}; // empty document -> JsonNull
        JNode v = readValue();
        if (v.t != JNode::Null && skipWs(false)) throw JsonErr{"", "Did not consume the entire document."};
        return v;
    }

private:
    std::u16string s_;
    size_t         pos_ = 0, lineStart_ = 0;
    int            line_ = 0;
    struct Frame {
        bool        arr;
        int         idx = 0;
        std::string name;
        bool        hasName = false;
    };
    std::vector<Frame> stack_;

    std::string path() const {
        std::string p = "$";
        for (auto const& f : stack_) {
            if (f.arr) p += "[" + std::to_string(f.idx) + "]";
            else if (f.hasName) p += "." + f.name;
        }
        return p;
    }
    std::string loc() const { return " at line " + std::to_string(line_ + 1) + " column " + std::to_string(pos_ - lineStart_ + 1) + " path " + path(); }
    [[noreturn]] void syntax(std::string const& m) const {
        throw JsonErr{"com.google.gson.stream.MalformedJsonException: " + m + loc(), ""};
    }
    [[noreturn]] void eof() const { throw JsonErr{"java.io.EOFException: End of input" + loc(), ""}; }

    /// nextNonWhitespace: returns true with pos_ AT the char, false at end of input.
    bool skipWs(bool) {
        while (pos_ < s_.size()) {
            char16_t c = s_[pos_];
            if (c == '\n') { pos_++; line_++; lineStart_ = pos_; continue; }
            if (c == ' ' || c == '\r' || c == '\t') { pos_++; continue; }
            if (c == '/' && pos_ + 1 < s_.size()) {
                if (s_[pos_ + 1] == '*') {
                    size_t e = s_.find(u"*/", pos_ + 2);
                    if (e == std::u16string::npos) { pos_ = s_.size(); syntax("Unterminated comment"); }
                    for (size_t i = pos_; i < e; i++)
                        if (s_[i] == '\n') { line_++; lineStart_ = i + 1; }
                    pos_ = e + 2;
                    continue;
                }
                if (s_[pos_ + 1] == '/') { skipLine(); continue; }
                return true;
            }
            if (c == '#') { skipLine(); continue; }
            return true;
        }
        return false;
    }
    void skipLine() {
        while (pos_ < s_.size() && s_[pos_] != '\n' && s_[pos_] != '\r') pos_++;
        if (pos_ < s_.size() && s_[pos_] == '\n') { pos_++; line_++; lineStart_ = pos_; }
        else if (pos_ < s_.size()) pos_++;
    }
    static bool isLiteral(char16_t c) {
        switch (c) {
        case '/': case '\\': case ';': case '#': case '=': case '{': case '}': case '[': case ']': case ':': case ',':
        case ' ': case '\t': case '\f': case '\r': case '\n': return false;
        default: return true;
        }
    }
    void valueDone() {
        if (!stack_.empty() && stack_.back().arr) stack_.back().idx++;
    }
    JNode readValue() {
        if (!skipWs(true)) eof();
        char16_t c = s_[pos_];
        JNode    n;
        if (c == '{') { pos_++; readObject(n); valueDone(); return n; }
        if (c == '[') { pos_++; readArray(n); valueDone(); return n; }
        if (c == '"' || c == '\'') {
            pos_++;
            n.t = JNode::Str;
            n.s = fromU16(readQuoted(c));
            valueDone();
            return n;
        }
        // keywords
        for (auto [kw, up, t] : {std::tuple{u"true", u"TRUE", 1}, std::tuple{u"false", u"FALSE", 2}, std::tuple{u"null", u"NULL", 3}}) {
            size_t L = std::char_traits<char16_t>::length(kw), i = 0;
            for (; i < L; i++)
                if (pos_ + i >= s_.size() || (s_[pos_ + i] != kw[i] && s_[pos_ + i] != up[i])) break;
            if (i == L && (pos_ + L >= s_.size() || !isLiteral(s_[pos_ + L]))) {
                pos_ += L;
                if (t == 3) n.t = JNode::Null;
                else { n.t = JNode::Bool; n.b = t == 1; }
                valueDone();
                return n;
            }
        }
        size_t numEnd = peekNumber();
        if (numEnd) {
            std::string num = fromU16(s_.substr(pos_, numEnd - pos_));
            pos_            = numEnd;
            n.t             = JNode::Num;
            n.d             = std::strtod(num.c_str(), nullptr);
            valueDone();
            return n;
        }
        if (!isLiteral(c)) syntax("Expected value");
        size_t st = pos_;
        while (pos_ < s_.size() && isLiteral(s_[pos_])) pos_++;
        n.t = JNode::Str;
        n.s = fromU16(s_.substr(st, pos_ - st));
        valueDone();
        return n;
    }
    /// end position of a strict JSON number starting at pos_ (followed by a non-literal), 0 if none
    size_t peekNumber() const {
        size_t i = pos_, n = s_.size();
        if (i < n && s_[i] == '-') i++;
        if (i >= n || s_[i] < '0' || s_[i] > '9') return 0;
        if (s_[i] == '0') {
            i++;
            if (i < n && s_[i] >= '0' && s_[i] <= '9') return 0;
        } else
            while (i < n && s_[i] >= '0' && s_[i] <= '9') i++;
        if (i < n && s_[i] == '.') {
            i++;
            if (i >= n || s_[i] < '0' || s_[i] > '9') return 0;
            while (i < n && s_[i] >= '0' && s_[i] <= '9') i++;
        }
        if (i < n && (s_[i] == 'e' || s_[i] == 'E')) {
            i++;
            if (i < n && (s_[i] == '+' || s_[i] == '-')) i++;
            if (i >= n || s_[i] < '0' || s_[i] > '9') return 0;
            while (i < n && s_[i] >= '0' && s_[i] <= '9') i++;
        }
        if (i < n && isLiteral(s_[i])) return 0;
        return i;
    }
    std::u16string readQuoted(char16_t q) {
        std::u16string r;
        while (true) {
            if (pos_ >= s_.size()) syntax("Unterminated string");
            char16_t c = s_[pos_++];
            if (c == q) return r;
            if (c == '\n') { line_++; lineStart_ = pos_; }
            if (c != '\\') { r += c; continue; }
            if (pos_ >= s_.size()) syntax("Unterminated escape sequence");
            char16_t e = s_[pos_++];
            switch (e) {
            case 'u': {
                if (pos_ + 4 > s_.size()) syntax("Unterminated escape sequence");
                unsigned v = 0;
                for (int k = 0; k < 4; k++) {
                    char16_t h = s_[pos_ + k];
                    v <<= 4;
                    if (h >= '0' && h <= '9') v += h - '0';
                    else if (h >= 'a' && h <= 'f') v += h - 'a' + 10;
                    else if (h >= 'A' && h <= 'F') v += h - 'A' + 10;
                    else throw JsonErr{"java.lang.NumberFormatException: \\u" + fromU16(s_.substr(pos_, 4)), ""};
                }
                pos_ += 4;
                r += char16_t(v);
                break;
            }
            case 't': r += u'\t'; break;
            case 'b': r += u'\b'; break;
            case 'n': r += u'\n'; break;
            case 'r': r += u'\r'; break;
            case 'f': r += u'\f'; break;
            case '\n': line_++; lineStart_ = pos_; r += e; break;
            case '\'': case '"': case '\\': case '/': r += e; break;
            default: syntax("Invalid escape sequence");
            }
        }
    }
    void readArray(JNode& n) {
        n.t = JNode::Arr;
        stack_.push_back(Frame{true, 0, std::string(), false});
        bool first = true;
        while (true) {
            if (!skipWs(true)) eof();
            char16_t c = s_[pos_];
            if (c == ']' && first) { pos_++; break; }
            if (c == ',' || c == ';' || c == ']') { // lenient: empty element = null
                n.a.push_back(JNode{});
                valueDone();
            } else n.a.push_back(readValue());
            first = false;
            if (!skipWs(true)) eof();
            c = s_[pos_++];
            if (c == ']') break;
            if (c != ',' && c != ';') syntax("Unterminated array");
            if (!skipWs(true)) eof();
            if (s_[pos_] == ']') { // ",]" -> trailing null
                n.a.push_back(JNode{});
                valueDone();
                pos_++;
                break;
            }
        }
        stack_.pop_back();
    }
    void readObject(JNode& n) {
        n.t = JNode::Obj;
        stack_.push_back(Frame{false, 0, std::string(), false});
        bool first = true;
        while (true) {
            if (!skipWs(true)) eof();
            char16_t c = s_[pos_++];
            std::string name;
            if (c == '}' && first) break;
            if (c == '"' || c == '\'') name = fromU16(readQuoted(c));
            else {
                pos_--;
                if (c == '}' || !isLiteral(c)) { pos_++; syntax("Expected name"); }
                size_t st = pos_;
                while (pos_ < s_.size() && isLiteral(s_[pos_])) pos_++;
                name = fromU16(s_.substr(st, pos_ - st));
            }
            first                 = false;
            stack_.back().name    = name;
            stack_.back().hasName = true;
            if (!skipWs(true)) eof();
            c = s_[pos_++];
            if (c == '=') {
                if (pos_ < s_.size() && s_[pos_] == '>') pos_++;
            } else if (c != ':') syntax("Expected ':'");
            n.add(name, readValue());
            if (!skipWs(true)) eof();
            c = s_[pos_++];
            if (c == '}') break;
            if (c != ',' && c != ';') syntax("Unterminated object");
        }
        stack_.pop_back();
    }
};

JNode parseGson(std::string const& text) { return GsonReader(toU16(text)).parseDocument(); }

/// FiguraJsonSerializer.luaValueFromElement / JsonUtils.asLuaValue
void pushNode(lua_State* L, JNode const& n) {
    luaL_checkstack(L, 4, "json");
    switch (n.t) {
    case JNode::Null: lua_pushnil(L); break;
    case JNode::Bool: lua_pushboolean(L, n.b); break;
    case JNode::Int: lua_pushnumber(L, n.i); break;
    case JNode::Num: lua_pushnumber(L, n.d); break;
    case JNode::Str: lua_pushlstring(L, n.s.data(), n.s.size()); break;
    case JNode::Arr:
        lua_createtable(L, int(n.a.size()), 0);
        for (size_t i = 0; i < n.a.size(); i++) {
            pushNode(L, n.a[i]);
            lua_rawseti(L, -2, int(i + 1));
        }
        break;
    case JNode::Obj:
        lua_createtable(L, 0, int(n.keys.size()));
        for (size_t m = 0; m < n.keys.size(); m++) {
            lua_pushlstring(L, n.keys[m].data(), n.keys[m].size());
            pushNode(L, n.vals[m]);
            lua_rawset(L, -3);
        }
        break;
    }
}

// =====================================================================================================================
// JsonAPI
// =====================================================================================================================
/// static-method QOL of LuaTypeManager: json:foo(x) == json.foo(x)
int staticBase(lua_State* L) { return toRef(L, 1, tJsonAPI) ? 2 : 1; }

int jaNewArray(lua_State* L) { newObj(L, tJsonArray, new JsonArrayObj); return 1; }
int jaNewObject(lua_State* L) { newObj(L, tJsonObject, new JsonObjectObj); return 1; }
struct JsonBuilderObj {
    bool prettyPrinting = false, htmlEscaping = true, serializeNils = false;
};
struct JsonSerializerObj {
    WriterOpts opts;
};
int jaNewBuilder(lua_State* L) { newObj(L, tJsonBuilder, new JsonBuilderObj); return 1; }
int jaIsSerializable(lua_State* L) { return pushBool(L, isSerializable(L, staticBase(L))); }

// ---- JsonArray
JsonArrayObj* arr(lua_State* L, char const* m) { return self<JsonArrayObj>(L, tJsonArray, m); }
void checkSerializableArr(lua_State* L, int idx) {
    if (!isSerializable(L, idx))
        javaFail(L, "java.lang.IllegalArgumentException", std::string("Type ") + (lua_type(L, idx) == LUA_TNONE ? "nil" : luaL_typename(L, idx)) + " can't be serialized");
}
void checkIndex(lua_State* L, int64_t i, int n) {
    if (i < 0 || i >= n) javaFail(L, "java.lang.IndexOutOfBoundsException", "Index " + std::to_string(i) + " out of bounds for length " + std::to_string(n));
}
/// ArrayList.equals-style comparison of two Lua values (LuaValue.equals)
bool javaEquals(lua_State* L, int a, int b) {
    a = lua_absindex(L, a);
    b = lua_absindex(L, b);
    bool an = lua_isnoneornil(L, a) || lua_touserdata(L, a) == &kNilSentinel;
    bool bn = lua_isnoneornil(L, b) || lua_touserdata(L, b) == &kNilSentinel;
    if (an || bn) return an && bn;
    if (lua_type(L, a) != lua_type(L, b)) return false;
    if (lua_type(L, a) == LUA_TUSERDATA) return lua_compare(L, a, b, LUA_OPEQ) != 0;
    return lua_rawequal(L, a, b) != 0;
}
int arrGet(lua_State* L) {
    JsonArrayObj* a = arr(L, "get");
    int32_t       i = *argInt(L, 2, "get", true);
    checkIndex(L, int64_t(i) - 1, a->n);
    pushHeld(L, 1, i);
    return 1;
}
int arrSize(lua_State* L) { return pushInt(L, arr(L, "size")->n); }
int indexOfImpl(lua_State* L, JsonArrayObj* a, int v, bool last) {
    for (int k = 0; k < a->n; k++) {
        int i = last ? a->n - k : k + 1;
        lua_getuservalue(L, 1);
        lua_rawgeti(L, -1, i);
        bool eq = javaEquals(L, -1, v);
        lua_pop(L, 2);
        if (eq) return i;
    }
    return 0;
}
int arrContains(lua_State* L) { JsonArrayObj* a = arr(L, "contains"); return pushBool(L, indexOfImpl(L, a, 2, false) > 0); }
int arrIndexOf(lua_State* L) { JsonArrayObj* a = arr(L, "indexOf"); return pushInt(L, indexOfImpl(L, a, 2, false)); }
int arrLastIndexOf(lua_State* L) { JsonArrayObj* a = arr(L, "lastIndexOf"); return pushInt(L, indexOfImpl(L, a, 2, true)); }
void shift(lua_State* L, JsonArrayObj* a, int from, int delta) { // move [from..n] by delta (+1 / -1)
    lua_getuservalue(L, 1);
    if (delta > 0)
        for (int i = a->n; i >= from; i--) { lua_rawgeti(L, -1, i); lua_rawseti(L, -2, i + 1); }
    else {
        for (int i = from; i <= a->n; i++) { lua_rawgeti(L, -1, i); lua_rawseti(L, -2, i - 1); }
        lua_pushnil(L);
        lua_rawseti(L, -2, a->n);
    }
    lua_pop(L, 1);
}
void arrAppend(lua_State* L, JsonArrayObj* a, int v) {
    checkSerializableArr(L, v);
    a->n++;
    setHeld(L, 1, a->n, v);
}
int arrAdd(lua_State* L) { JsonArrayObj* a = arr(L, "add"); arrAppend(L, a, 2); return pushBool(L, true); }
int arrInsert(lua_State* L) {
    JsonArrayObj* a = arr(L, "insert");
    int32_t       i = *argInt(L, 2, "insert", true);
    checkSerializableArr(L, 3);
    int64_t at = int64_t(i) - 1;
    if (at < 0 || at > a->n) javaFail(L, "java.lang.IndexOutOfBoundsException", "Index: " + std::to_string(at) + ", Size: " + std::to_string(a->n));
    shift(L, a, int(at) + 1, 1);
    a->n++;
    setHeld(L, 1, int(at) + 1, 3);
    return 0;
}
int arrSetImpl(lua_State* L, JsonArrayObj* a, int64_t i, int v) {
    checkSerializableArr(L, v);
    checkIndex(L, i - 1, a->n);
    setHeld(L, 1, int(i), v);
    return 0;
}
int arrSet(lua_State* L) {
    JsonArrayObj* a = arr(L, "set");
    int32_t       i = *argInt(L, 2, "set", true);
    return arrSetImpl(L, a, i, 3);
}
int arrRemoveAt(lua_State* L) {
    JsonArrayObj* a = arr(L, "removeAt");
    int32_t       i = *argInt(L, 2, "removeAt", true);
    checkIndex(L, int64_t(i) - 1, a->n);
    pushHeld(L, 1, i);
    shift(L, a, i + 1, -1);
    a->n--;
    return 1;
}
int arrRemove(lua_State* L) {
    JsonArrayObj* a = arr(L, "remove");
    int           i = indexOfImpl(L, a, 2, false);
    if (!i) return pushBool(L, false);
    shift(L, a, i + 1, -1);
    a->n--;
    return pushBool(L, true);
}
int arrClear(lua_State* L) {
    JsonArrayObj* a = arr(L, "clear");
    lua_newtable(L);
    lua_setuservalue(L, 1);
    a->n = 0;
    return 0;
}
bool isJavaInt(lua_State* L, int i) { // LuaValue.isint()
    if (lua_type(L, i) != LUA_TNUMBER) return false;
    double d = lua_tonumber(L, i);
    return d == std::floor(d) && d >= -2147483648.0 && d <= 2147483647.0;
}
int arrIndex(lua_State* L) {
    JsonArrayObj* a = arr(L, "__index");
    if (!isJavaInt(L, 2)) return 0;
    int i = int(lua_tonumber(L, 2));
    checkIndex(L, int64_t(i) - 1, a->n);
    pushHeld(L, 1, i);
    return 1;
}
int arrNewIndex(lua_State* L) {
    JsonArrayObj* a = arr(L, "__newindex");
    if (!isJavaInt(L, 2)) return 0;
    int i = int(lua_tonumber(L, 2));
    if (i < 1 || i > a->n + 1)
        javaFail(L, "java.lang.IndexOutOfBoundsException", "Index must be in range [1; " + std::to_string(a->n) + "+1], got " + std::to_string(i));
    if (i == a->n + 1) arrAppend(L, a, 3);
    else arrSetImpl(L, a, i, 3);
    return 0;
}
int arrLen(lua_State* L) { return pushInt(L, arr(L, "__len")->n); }
int arrIter(lua_State* L) { // JsonArrayIterator
    auto*   a   = to<JsonArrayObj>(L, 1, tJsonArray);
    int32_t ind = javaInt(luaL_checknumber(L, 2)) + 1;
    if (!a || ind > a->n) return 0;
    lua_pushinteger(L, ind);
    if (ind < 1) checkIndex(L, int64_t(ind) - 1, a->n);
    pushHeld(L, 1, ind);
    return 2;
}
int arrIPairs(lua_State* L) {
    arr(L, "__ipairs");
    lua_pushcfunction(L, arrIter);
    lua_pushvalue(L, 1);
    lua_pushinteger(L, 0);
    return 3;
}
int arrToString(lua_State* L) { lua_pushstring(L, "JsonArray"); return 1; }

// ---- JsonObject
JsonObjectObj* obj(lua_State* L, char const* m) { return self<JsonObjectObj>(L, tJsonObject, m); }
void objPush(lua_State* L, int o, std::string const& k) { // contents.get(k)
    lua_getuservalue(L, o);
    lua_pushlstring(L, k.data(), k.size());
    lua_rawget(L, -2);
    lua_remove(L, -2);
    if (lua_touserdata(L, -1) == &kNilSentinel) { lua_pop(L, 1); lua_pushnil(L); }
}
int objPut(lua_State* L, JsonObjectObj* o, std::string const& k, int v) {
    v = lua_absindex(L, v);
    if (!isSerializable(L, v)) {
        size_t      n;
        char const* s = luaL_tolstring(L, v, &n);
        std::string d(s, n);
        lua_pop(L, 1);
        javaFail(L, "java.lang.IllegalArgumentException", "Type " + d + " can't be serialized");
    }
    objPush(L, 1, k); // previous
    o->keys.insert(k);
    lua_getuservalue(L, 1);
    lua_pushlstring(L, k.data(), k.size());
    if (isNil(L, v)) lua_pushlightuserdata(L, &kNilSentinel);
    else lua_pushvalue(L, v);
    lua_rawset(L, -3);
    lua_pop(L, 1);
    return 1;
}
int objSize(lua_State* L) { return pushInt(L, int64_t(obj(L, "size")->keys.m.size())); }
int objGet(lua_State* L) {
    obj(L, "get");
    objPush(L, 1, *argStr(L, 2, "get", true));
    return 1;
}
int objContainsKey(lua_State* L) {
    JsonObjectObj* o = obj(L, "containsKey");
    return pushBool(L, o->keys.m.count(*argStr(L, 2, "containsKey", true)) > 0);
}
int objPutM(lua_State* L) {
    JsonObjectObj* o = obj(L, "put");
    std::string    k = *argStr(L, 2, "put", true);
    return objPut(L, o, k, 3);
}
int objRemove(lua_State* L) {
    JsonObjectObj* o = obj(L, "remove");
    std::string    k = *argStr(L, 2, "remove", true);
    objPush(L, 1, k);
    o->keys.erase(k);
    lua_getuservalue(L, 1);
    lua_pushlstring(L, k.data(), k.size());
    lua_pushnil(L);
    lua_rawset(L, -3);
    lua_pop(L, 1);
    return 1;
}
int objClear(lua_State* L) {
    JsonObjectObj* o = obj(L, "clear");
    o->keys.clear();
    lua_newtable(L);
    lua_setuservalue(L, 1);
    return 0;
}
int objContainsValue(lua_State* L) {
    JsonObjectObj* o = obj(L, "containsValue");
    for (auto const& k : o->keys.keys()) {
        lua_getuservalue(L, 1);
        lua_pushlstring(L, k.data(), k.size());
        lua_rawget(L, -2);
        bool eq = javaEquals(L, -1, 2);
        lua_pop(L, 2);
        if (eq) return pushBool(L, true);
    }
    return pushBool(L, false);
}
int objIndex(lua_State* L) {
    obj(L, "__index");
    if (lua_type(L, 2) != LUA_TSTRING) return 0;
    objPush(L, 1, lua_tostring(L, 2));
    return 1;
}
int objNewIndex(lua_State* L) {
    JsonObjectObj* o = obj(L, "__newindex");
    if (lua_type(L, 2) != LUA_TSTRING) return 0;
    size_t      n;
    char const* s = lua_tolstring(L, 2, &n);
    objPut(L, o, std::string(s, n), 3);
    return 0;
}
int objIter(lua_State* L) { // JsonObjectIterator; upvalue 1 = key snapshot
    int nk = int(lua_rawlen(L, lua_upvalueindex(1)));
    if (nk == 0) return 0;
    if (lua_type(L, 1) != LUA_TUSERDATA) fail(L, std::string("bad argument: userdata expected, got ") + luaL_typename(L, 1));
    if (!toRef(L, 1, tJsonObject)) return 0;
    int next = 1;
    if (lua_type(L, 2) == LUA_TSTRING) {
        int ind = -1;
        for (int i = 1; i <= nk; i++) {
            lua_rawgeti(L, lua_upvalueindex(1), i);
            bool eq = lua_rawequal(L, -1, 2) != 0;
            lua_pop(L, 1);
            if (eq) { ind = i - 1; break; }
        }
        if (ind < nk - 1) next = ind + 2;
        else return 0;
    }
    lua_rawgeti(L, lua_upvalueindex(1), next);
    objPush(L, 1, lua_tostring(L, -1));
    return 2;
}
int objPairs(lua_State* L) {
    JsonObjectObj* o = obj(L, "__pairs");
    auto           k = o->keys.keys();
    pushStrings(L, k);
    lua_pushcclosure(L, objIter, 1);
    lua_pushvalue(L, 1);
    lua_pushnil(L);
    return 3;
}
int emptyIter(lua_State* L) { (void)L; return 0; }
int objIPairs(lua_State* L) {
    obj(L, "__ipairs");
    lua_pushcfunction(L, emptyIter);
    lua_pushvalue(L, 1);
    lua_pushinteger(L, 0);
    return 3;
}
int objToString(lua_State* L) { lua_pushstring(L, "JsonObject"); return 1; }

// ---- JsonBuilder / JsonSerializer
JsonBuilderObj* bld(lua_State* L, char const* m) { return self<JsonBuilderObj>(L, tJsonBuilder, m); }
int bldBuild(lua_State* L) {
    JsonBuilderObj* b = bld(L, "build");
    auto*           s = newObj(L, tJsonSerializer, new JsonSerializerObj);
    s->opts.pretty         = b->prettyPrinting;
    s->opts.html           = b->htmlEscaping;
    s->opts.serializeNulls = b->serializeNils;
    return 1;
}
bool* bldField(JsonBuilderObj* b, std::string const& k) {
    if (k == "prettyPrinting") return &b->prettyPrinting;
    if (k == "htmlEscaping") return &b->htmlEscaping;
    if (k == "serializeNils") return &b->serializeNils;
    return nullptr;
}
int bldIndex(lua_State* L) {
    JsonBuilderObj* b = bld(L, "__index");
    if (lua_type(L, 2) != LUA_TSTRING) return 0;
    bool* f = bldField(b, lua_tostring(L, 2));
    if (!f) return 0;
    return pushBool(L, *f);
}
int bldNewIndex(lua_State* L) {
    JsonBuilderObj* b = bld(L, "__newindex");
    if (lua_type(L, 2) != LUA_TSTRING) return 0;
    bool* f = bldField(b, lua_tostring(L, 2));
    if (!f) return 0;
    if (lua_type(L, 3) != LUA_TBOOLEAN) fail(L, std::string("bad argument: boolean expected, got ") + (lua_type(L, 3) == LUA_TNONE ? "nil" : luaL_typename(L, 3)));
    *f = lua_toboolean(L, 3) != 0;
    return 0;
}
int bldToString(lua_State* L) { lua_pushstring(L, "JsonBuilder"); return 1; }

int serSerialize(lua_State* L) {
    auto* s = self<JsonSerializerObj>(L, tJsonSerializer, "serialize");
    if (!isSerializable(L, 2))
        javaFail(L, "java.lang.IllegalArgumentException", std::string("Type of provided value (") + luaL_typename(L, 2) + ") is not serializable");
    JNode n;
    toNode(L, 2, n, 0);
    return pushStr(L, writeJson(n, s->opts));
}
int serDeserialize(lua_State* L) {
    self<JsonSerializerObj>(L, tJsonSerializer, "deserialize");
    auto str = argStr(L, 2, "deserialize");
    if (!str) javaFail(L, "java.lang.NullPointerException", "Cannot invoke \"String.length()\" because \"s\" is null");
    JNode       n;
    std::string err;
    try {
        n = parseGson(*str);
    } catch (JsonErr const& e) {
        err = "com.google.gson.JsonSyntaxException: " + (e.cause.empty() ? e.msg : e.cause);
    }
    if (!err.empty()) fail(L, "vm error: " + err);
    pushNode(L, n);
    return 1;
}
int serToString(lua_State* L) { lua_pushstring(L, "JsonSerializer"); return 1; }

// =====================================================================================================================
// streams
// =====================================================================================================================
struct InStream {
    virtual ~InStream() = default;
    virtual int     read(lua_State* L)                         = 0; // -1 at end
    virtual int64_t readBulk(lua_State* L, uint8_t* b, int64_t n) = 0; // InputStream.read(byte[]): -1 at end
    virtual int64_t skip(lua_State* L, int64_t n)              = 0;
    virtual int     available(lua_State* L)                    = 0;
    virtual void    close()                                    = 0;
    virtual void    mark(int) {}
    virtual void    reset(lua_State* L) { ioFail(L, "mark/reset not supported"); }
    virtual bool    markSupported() { return false; }
    virtual char const* closedMessage() const = 0; // non-null once closed
};
/// FileInputStream
struct FileIn final : InStream {
    std::FILE* f = nullptr;
    ~FileIn() override { close(); }
    void check(lua_State* L) const {
        if (!f) ioFail(L, "Stream Closed");
    }
    int read(lua_State* L) override {
        check(L);
        int c = std::fgetc(f);
        return c == EOF ? -1 : c;
    }
    int64_t readBulk(lua_State* L, uint8_t* b, int64_t n) override {
        check(L);
        if (n == 0) return 0;
        size_t r = std::fread(b, 1, size_t(n), f);
        return r == 0 ? -1 : int64_t(r);
    }
    int64_t skip(lua_State* L, int64_t n) override {
        check(L);
        long cur = std::ftell(f);
        if (cur + n < 0) ioFail(L, "Invalid argument");
        std::fseek(f, long(cur + n), SEEK_SET);
        return n;
    }
    int available(lua_State* L) override {
        check(L);
        long cur = std::ftell(f);
        std::fseek(f, 0, SEEK_END);
        long end = std::ftell(f);
        std::fseek(f, cur, SEEK_SET);
        return int(std::max<long>(0, std::min<long>(end - cur, INT32_MAX)));
    }
    void close() override {
        if (f) std::fclose(f);
        f = nullptr;
    }
    char const* closedMessage() const override { return f ? nullptr : "Stream Closed"; }
};
/// GZIPInputStream over an in-memory resource (decoded up front)
struct GzipIn final : InStream {
    std::vector<uint8_t> data;
    size_t               pos    = 0;
    bool                 eofHit = false, closed = false;
    void check(lua_State* L) const {
        if (closed) ioFail(L, "Stream closed");
    }
    int read(lua_State* L) override {
        check(L);
        if (pos >= data.size()) { eofHit = true; return -1; }
        return data[pos++];
    }
    int64_t readBulk(lua_State* L, uint8_t* b, int64_t n) override {
        check(L);
        if (n == 0) return 0;
        if (pos >= data.size()) { eofHit = true; return -1; }
        int64_t k = std::min<int64_t>(n, int64_t(data.size() - pos));
        std::memcpy(b, data.data() + pos, size_t(k));
        pos += size_t(k);
        return k;
    }
    int64_t skip(lua_State* L, int64_t n) override {
        if (n < 0) javaFail(L, "java.lang.IllegalArgumentException", "negative skip length");
        check(L);
        int64_t k = std::min<int64_t>(n, int64_t(data.size() - pos));
        pos += size_t(k);
        if (k < n) eofHit = true;
        return k;
    }
    int available(lua_State* L) override {
        check(L);
        return eofHit ? 0 : 1;
    }
    void close() override { closed = true; }
    char const* closedMessage() const override { return closed ? "Stream closed" : nullptr; }
};
struct InputStreamObj {
    std::unique_ptr<InStream> s;
    bool                      asyncOnly = false;
};
/// FileOutputStream
struct OutputStreamObj {
    std::FILE* f = nullptr;
    ~OutputStreamObj() {
        if (f) std::fclose(f);
    }
    void write(lua_State* L, int b) {
        if (!f) ioFail(L, "Stream Closed");
        std::fputc(b & 0xFF, f);
    }
};

struct FutureObj {
    bool        done = false, hasError = false;
    std::string error; // LuaError message
};
void pushFuture(lua_State* L, int valueIdx, std::string const* error) {
    valueIdx  = lua_absindex(L, valueIdx);
    auto* fut = newObj(L, tFuture, new FutureObj);
    fut->done = true;
    if (error) {
        fut->hasError = true;
        fut->error    = *error;
    } else setHeld(L, -1, 1, valueIdx);
}
int futIsDone(lua_State* L) { return pushBool(L, self<FutureObj>(L, tFuture, "isDone")->done); }
int futGetValue(lua_State* L) { self<FutureObj>(L, tFuture, "getValue"); pushHeld(L, 1, 1); return 1; }
int futGetOrError(lua_State* L) {
    FutureObj* f = self<FutureObj>(L, tFuture, "getOrError");
    if (f->hasError) raise(L, f->error);
    pushHeld(L, 1, 1);
    return 1;
}
int futThrowError(lua_State* L) {
    FutureObj* f = self<FutureObj>(L, tFuture, "throwError");
    if (f->hasError) raise(L, f->error);
    return 0;
}
int futToString(lua_State* L) {
    FutureObj* f = self<FutureObj>(L, tFuture, "__tostring");
    lua_pushstring(L, f->done ? "Future(isDone=true)" : "Future(isDone=false)");
    return 1;
}

InputStreamObj* ins(lua_State* L, char const* m) { return self<InputStreamObj>(L, tInputStream, m); }
int isRead(lua_State* L) {
    InputStreamObj* s = ins(L, "read");
    if (s->asyncOnly) ioFail(L, "This stream supports only async read");
    return pushInt(L, s->s->read(L));
}
int isReadAsync(lua_State* L) {
    InputStreamObj* s     = ins(L, "readAsync");
    auto            lim   = argInt(L, 2, "readAsync");
    int64_t         limit = lim ? *lim : s->s->available(L);
    if (limit < 0) {
        std::string e = "vm error: java.util.concurrent.CompletionException: java.lang.NegativeArraySizeException: " + std::to_string(limit);
        pushFuture(L, 0, &e);
        return 1;
    }
    if (char const* cm = s->s->closedMessage()) {
        std::string e = std::string("vm error: java.util.concurrent.CompletionException: org.luaj.vm2.LuaError: vm error: java.io.IOException: ") + cm;
        pushFuture(L, 0, &e);
        return 1;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(limit));
    int64_t              len = s->s->readBulk(L, bytes.data(), limit);
    if (len < 0) len = 0;
    lua_pushlstring(L, reinterpret_cast<char const*>(bytes.data()), size_t(len));
    pushFuture(L, -1, nullptr);
    return 1;
}
int isSkip(lua_State* L) {
    InputStreamObj* s = ins(L, "skip");
    return pushInt(L, s->s->skip(L, argLong(L, 2, "skip").value_or(0)));
}
int isAvailable(lua_State* L) { InputStreamObj* s = ins(L, "available"); return pushInt(L, s->s->available(L)); }
int isClose(lua_State* L) { ins(L, "close")->s->close(); return 0; }
int isMark(lua_State* L) { InputStreamObj* s = ins(L, "mark"); s->s->mark(argInt(L, 2, "mark").value_or(0)); return 0; }
int isReset(lua_State* L) { ins(L, "reset")->s->reset(L); return 0; }
int isMarkSupported(lua_State* L) { return pushBool(L, ins(L, "markSupported")->s->markSupported()); }
int isIsAsyncOnly(lua_State* L) { return pushBool(L, ins(L, "isAsyncOnly")->asyncOnly); }
OutputStreamObj* checkOutArg(lua_State* L, int i, char const* fn, bool notNull) {
    if (isNil(L, i)) {
        if (notNull) notNil(L, i, fn, "OutputStream");
        return nullptr;
    }
    auto* o = to<OutputStreamObj>(L, i, tOutputStream);
    if (!o) badArg(L, i, fn, "OutputStream");
    return o;
}
int isTransferTo(lua_State* L) {
    InputStreamObj*  s = ins(L, "transferTo");
    OutputStreamObj* o = checkOutArg(L, 2, "transferTo", false);
    if (!o) javaFail(L, "java.lang.NullPointerException", "out");
    int64_t total = 0;
    uint8_t buf[8192];
    for (;;) {
        int64_t n = s->s->readBulk(L, buf, sizeof buf);
        if (n < 0) break;
        for (int64_t i = 0; i < n; i++) o->write(L, buf[i]);
        total += n;
    }
    return pushInt(L, total);
}
int isToString(lua_State* L) { lua_pushstring(L, "InputStream"); return 1; }

OutputStreamObj* outs(lua_State* L, char const* m) { return self<OutputStreamObj>(L, tOutputStream, m); }
int osWrite(lua_State* L) {
    OutputStreamObj* o = outs(L, "write");
    o->write(L, argInt(L, 2, "write").value_or(0));
    return 0;
}
int osFlush(lua_State* L) {
    OutputStreamObj* o = outs(L, "flush");
    if (o->f) std::fflush(o->f);
    return 0;
}
int osClose(lua_State* L) {
    OutputStreamObj* o = outs(L, "close");
    if (o->f) std::fclose(o->f);
    o->f = nullptr;
    return 0;
}
int osToString(lua_State* L) { lua_pushstring(L, "OutputStream"); return 1; }

// =====================================================================================================================
// DataAPI + Buffer
// =====================================================================================================================
int maxBufferSize(lua_State* L) { return int(std::clamp(avatarOf(L)->tune("bufferSize", 2147483647.0), 0.0, 2147483647.0)); }
int maxBuffersCount(lua_State* L) { return int(std::clamp(avatarOf(L)->tune("buffersCount", 32.0), 0.0, 2147483647.0)); }
int& openBuffers(lua_State* L) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, &kBufferCount);
    int* p = static_cast<int*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (!p) {
        p  = static_cast<int*>(lua_newuserdata(L, sizeof(int)));
        *p = 0;
        lua_rawsetp(L, LUA_REGISTRYINDEX, &kBufferCount);
    }
    return *p;
}

struct BufferObj {
    std::vector<uint8_t> buf;
    int                  length = 0, position = 0;
    bool                 closed = false, listed = false;
};
constexpr int kStep = 512;

int dataCreateBuffer(lua_State* L) {
    self<int>(L, tDataAPI, "createBuffer");
    auto len = argInt(L, 2, "createBuffer");
    int  max = maxBufferSize(L);
    if (!len) {
        int& open = openBuffers(L);
        if (open > maxBuffersCount(L)) fail(L, "You have exceed the max amount of open buffers");
        if (kStep > max) fail(L, "Unable to create buffer because max capacity is less than default buffer size (512)");
        auto* b   = newObj(L, tBuffer, new BufferObj);
        b->buf.resize(kStep);
        b->listed = true;
        open++;
        return 1;
    }
    if (*len > max) fail(L, "Unable to create a buffer with capacity %s");
    if (*len < 0) javaFail(L, "java.lang.NegativeArraySizeException", std::to_string(*len));
    auto* b = newObj(L, tBuffer, new BufferObj);
    b->buf.resize(size_t(*len));
    return 1;
}
int dataToString(lua_State* L) { lua_pushstring(L, "DataAPI"); return 1; }

BufferObj* buf(lua_State* L, char const* m) { return self<BufferObj>(L, tBuffer, m); }
void checkClosed(lua_State* L, BufferObj* b) {
    if (b->closed) fail(L, "This byte buffer is closed and cant be used anymore");
}
int bRead1(lua_State* L, BufferObj* b) {
    checkClosed(L, b);
    if (b->position == b->length) return -1;
    return b->buf[size_t(b->position++)];
}
void bWrite1(lua_State* L, BufferObj* b, int v) {
    checkClosed(L, b);
    if (b->position == b->length) {
        b->length++;
        int max = maxBufferSize(L);
        if (b->length > max)
            fail(L, "Can't increase this buffer capacity to " + std::to_string(b->length) + ", max capacity is " + std::to_string(max));
        if (size_t(b->length) > b->buf.size()) b->buf.resize(size_t(std::min<int64_t>(int64_t(b->buf.size()) + kStep, max)));
    }
    if (size_t(b->position) >= b->buf.size())
        javaFail(L, "java.lang.ArrayIndexOutOfBoundsException", "Index " + std::to_string(b->position) + " out of bounds for length " + std::to_string(b->buf.size()));
    b->buf[size_t(b->position++)] = uint8_t(v & 0xFF);
}
std::string readN(lua_State* L, BufferObj* b, int64_t count) {
    checkClosed(L, b);
    int64_t sz = std::min<int64_t>(count, b->length - b->position);
    if (sz < 0) javaFail(L, "java.lang.NegativeArraySizeException", std::to_string(sz));
    std::string r;
    for (int64_t i = 0; i < count; i++) {
        int c = bRead1(L, b);
        if (c == -1) break;
        r += char(c);
    }
    return r;
}
uint64_t joinBE(std::string const& s) {
    uint64_t v = 0;
    for (size_t i = 0; i < s.size(); i++) v |= uint64_t((unsigned char)s[s.size() - 1 - i]) << (i * 8);
    return v;
}
uint64_t joinLE(std::string const& s) {
    uint64_t v = 0;
    for (size_t i = 0; i < s.size(); i++) v |= uint64_t((unsigned char)s[i]) << (i * 8);
    return v;
}
#define BUF_READ(fname, n, join, expr)                                     \
    int fname(lua_State* L) {                                              \
        BufferObj* b = buf(L, &#fname[2]);                                 \
        checkClosed(L, b);                                                 \
        uint64_t v = join(readN(L, b, n));                                 \
        (void)v;                                                           \
        return expr;                                                       \
    }
BUF_READ(b_readShort, 2, joinBE, pushInt(L, int16_t(uint16_t(v))))
BUF_READ(b_readUShort, 2, joinBE, pushInt(L, int64_t(v)))
BUF_READ(b_readInt, 4, joinBE, pushInt(L, int32_t(uint32_t(v))))
BUF_READ(b_readLong, 8, joinBE, pushInt(L, int64_t(v)))
BUF_READ(b_readShortLE, 2, joinLE, pushInt(L, int16_t(uint16_t(v))))
BUF_READ(b_readUShortLE, 2, joinLE, pushInt(L, int64_t(v)))
BUF_READ(b_readIntLE, 4, joinLE, pushInt(L, int32_t(uint32_t(v))))
BUF_READ(b_readLongLE, 8, joinLE, pushInt(L, int64_t(v)))
#undef BUF_READ
float  bitsToFloat(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }
double bitsToDouble(uint64_t u) { double d; std::memcpy(&d, &u, 8); return d; }
uint32_t floatToBits(float f) {
    if (std::isnan(f)) return 0x7fc00000u;
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
uint64_t doubleToBits(double d) {
    if (std::isnan(d)) return 0x7ff8000000000000ull;
    uint64_t u;
    std::memcpy(&u, &d, 8);
    return u;
}
int b_readFloat(lua_State* L) { BufferObj* b = buf(L, "readFloat"); checkClosed(L, b); lua_pushnumber(L, bitsToFloat(uint32_t(joinBE(readN(L, b, 4))))); return 1; }
int b_readDouble(lua_State* L) { BufferObj* b = buf(L, "readDouble"); checkClosed(L, b); lua_pushnumber(L, bitsToDouble(joinBE(readN(L, b, 8)))); return 1; }
int b_readFloatLE(lua_State* L) { BufferObj* b = buf(L, "readFloatLE"); checkClosed(L, b); lua_pushnumber(L, bitsToFloat(uint32_t(joinLE(readN(L, b, 4))))); return 1; }
int b_readDoubleLE(lua_State* L) { BufferObj* b = buf(L, "readDoubleLE"); checkClosed(L, b); lua_pushnumber(L, bitsToDouble(joinLE(readN(L, b, 8)))); return 1; }
int b_read(lua_State* L) { BufferObj* b = buf(L, "read"); return pushInt(L, bRead1(L, b)); }

int64_t lenArg(lua_State* L, BufferObj* b, char const* fn) {
    auto l = argInt(L, 2, fn);
    if (!l) {
        checkClosed(L, b);
        return b->length - b->position;
    }
    return std::max(*l, 0);
}
int b_readString(lua_State* L) {
    BufferObj* b = buf(L, "readString");
    checkClosed(L, b);
    int64_t len = lenArg(L, b, "readString");
    Charset cs  = charsetOf(argStr(L, 3, "readString"));
    return pushStr(L, fromU16(decodeJava(readN(L, b, len), cs)));
}
std::string base64Encode(std::string const& in) {
    static char const t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string       o;
    size_t            i = 0;
    for (; i + 2 < in.size(); i += 3) {
        uint32_t v = uint32_t((unsigned char)in[i]) << 16 | uint32_t((unsigned char)in[i + 1]) << 8 | (unsigned char)in[i + 2];
        o += t[v >> 18]; o += t[v >> 12 & 63]; o += t[v >> 6 & 63]; o += t[v & 63];
    }
    if (i + 1 == in.size()) {
        uint32_t v = uint32_t((unsigned char)in[i]) << 16;
        o += t[v >> 18]; o += t[v >> 12 & 63]; o += "==";
    } else if (i + 2 == in.size()) {
        uint32_t v = uint32_t((unsigned char)in[i]) << 16 | uint32_t((unsigned char)in[i + 1]) << 8;
        o += t[v >> 18]; o += t[v >> 12 & 63]; o += t[v >> 6 & 63]; o += '=';
    }
    return o;
}
/// java.util.Base64.getDecoder().decode(String) (string -> ISO-8859-1 bytes first)
bool base64Decode(std::u16string const& s, std::string& out, std::string& err) {
    auto val = [](char16_t c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    uint32_t bits = 0;
    int      shift = 18;
    size_t   i = 0, n = s.size();
    char     hb[16];
    while (i < n) {
        char16_t c   = s[i++];
        int      raw = c < 0x100 ? c : '?';
        int      b   = val(char16_t(raw));
        if (b < 0) {
            if (raw == '=') {
                if ((shift == 6 && (i == n || s[i++] != '=')) || shift == 18) {
                    err = "Input byte array has wrong 4-byte ending unit";
                    return false;
                }
                break;
            }
            std::snprintf(hb, sizeof hb, "%x", unsigned(raw));
            err = std::string("Illegal base64 character ") + hb;
            return false;
        }
        bits |= uint32_t(b) << shift;
        shift -= 6;
        if (shift < 0) {
            out += char(bits >> 16); out += char(bits >> 8 & 0xFF); out += char(bits & 0xFF);
            shift = 18;
            bits  = 0;
        }
    }
    if (shift == 6) out += char(bits >> 16);
    else if (shift == 0) { out += char(bits >> 16); out += char(bits >> 8 & 0xFF); }
    else if (shift == 12) {
        err = "Last unit does not have enough valid bits";
        return false;
    }
    if (i < n) {
        err = "Input byte array has incorrect ending byte at " + std::to_string(i);
        return false;
    }
    return true;
}
int b_readBase64(lua_State* L) {
    BufferObj* b   = buf(L, "readBase64");
    int64_t    len = lenArg(L, b, "readBase64");
    return pushStr(L, base64Encode(readN(L, b, len)));
}
int b_readByteArray(lua_State* L) {
    BufferObj* b   = buf(L, "readByteArray");
    int64_t    len = lenArg(L, b, "readByteArray");
    return pushStr(L, readN(L, b, len));
}
int b_write(lua_State* L) {
    BufferObj* b = buf(L, "write");
    int        v = *argInt(L, 2, "write", true);
    bWrite1(L, b, v);
    return 0;
}
void writeBytes(lua_State* L, BufferObj* b, std::string const& s) { for (char c : s) bWrite1(L, b, (unsigned char)c); }
void writeBE(lua_State* L, BufferObj* b, uint64_t v, int n) { for (int i = n - 1; i >= 0; i--) bWrite1(L, b, int(v >> (i * 8) & 0xFF)); }
void writeLE(lua_State* L, BufferObj* b, uint64_t v, int n) { for (int i = 0; i < n; i++) bWrite1(L, b, int(v >> (i * 8) & 0xFF)); }
int writeShortImpl(lua_State* L, char const* fn, bool le, bool unsignedV) {
    BufferObj* b = buf(L, fn);
    int32_t    v = *argInt(L, 2, fn, true);
    checkClosed(L, b);
    int lo = unsignedV ? 0 : -32768, hi = unsignedV ? 65535 : 32767;
    if (v < lo || v > hi) fail(L, "Value " + std::to_string(v) + " is out of range [" + std::to_string(lo) + "; " + std::to_string(hi) + "]");
    if (le) writeLE(L, b, uint16_t(v), 2);
    else writeBE(L, b, uint16_t(v), 2);
    return 0;
}
int b_writeShort(lua_State* L) { return writeShortImpl(L, "writeShort", false, false); }
int b_writeUShort(lua_State* L) { return writeShortImpl(L, "writeUShort", false, true); }
int b_writeShortLE(lua_State* L) { return writeShortImpl(L, "writeShortLE", true, false); }
int b_writeUShortLE(lua_State* L) { return writeShortImpl(L, "writeUShortLE", true, true); }
int b_writeInt(lua_State* L) { BufferObj* b = buf(L, "writeInt"); int32_t v = *argInt(L, 2, "writeInt", true); checkClosed(L, b); writeBE(L, b, uint32_t(v), 4); return 0; }
int b_writeIntLE(lua_State* L) { BufferObj* b = buf(L, "writeIntLE"); int32_t v = *argInt(L, 2, "writeIntLE", true); checkClosed(L, b); writeLE(L, b, uint32_t(v), 4); return 0; }
int b_writeLong(lua_State* L) { BufferObj* b = buf(L, "writeLong"); int64_t v = *argLong(L, 2, "writeLong", true); checkClosed(L, b); writeBE(L, b, uint64_t(v), 8); return 0; }
int b_writeLongLE(lua_State* L) { BufferObj* b = buf(L, "writeLongLE"); int64_t v = *argLong(L, 2, "writeLongLE", true); checkClosed(L, b); writeLE(L, b, uint64_t(v), 8); return 0; }
int b_writeFloat(lua_State* L) { BufferObj* b = buf(L, "writeFloat"); double v = *argNum(L, 2, "writeFloat", "Number", true); checkClosed(L, b); writeBE(L, b, floatToBits(float(v)), 4); return 0; }
int b_writeFloatLE(lua_State* L) { BufferObj* b = buf(L, "writeFloatLE"); double v = *argNum(L, 2, "writeFloatLE", "Number", true); checkClosed(L, b); writeLE(L, b, floatToBits(float(v)), 4); return 0; }
int b_writeDouble(lua_State* L) { BufferObj* b = buf(L, "writeDouble"); double v = *argNum(L, 2, "writeDouble", "Number", true); checkClosed(L, b); writeBE(L, b, doubleToBits(v), 8); return 0; }
int b_writeDoubleLE(lua_State* L) { BufferObj* b = buf(L, "writeDoubleLE"); double v = *argNum(L, 2, "writeDoubleLE", "Number", true); checkClosed(L, b); writeLE(L, b, doubleToBits(v), 8); return 0; }
int b_writeString(lua_State* L) {
    BufferObj*  b = buf(L, "writeString");
    std::string s = *argStr(L, 2, "writeString", true);
    Charset     cs = charsetOf(argStr(L, 3, "writeString"));
    checkClosed(L, b);
    std::string bytes = encodeJava(toU16(s), cs);
    writeBytes(L, b, bytes);
    return pushInt(L, int64_t(bytes.size()));
}
int b_writeBase64(lua_State* L) {
    BufferObj*  b = buf(L, "writeBase64");
    std::string s = *argStr(L, 2, "writeBase64", true);
    checkClosed(L, b);
    std::string bytes, err;
    if (!base64Decode(toU16(s), bytes, err)) javaFail(L, "java.lang.IllegalArgumentException", err);
    writeBytes(L, b, bytes);
    return pushInt(L, int64_t(bytes.size()));
}
int b_writeByteArray(lua_State* L) {
    BufferObj* b = buf(L, "writeByteArray");
    notNil(L, 2, "writeByteArray", "AnyType");
    checkClosed(L, b);
    if (lua_type(L, 2) != LUA_TSTRING) fail(L, std::string("Expected string, got ") + luaL_typename(L, 2));
    size_t      n;
    char const* s = lua_tolstring(L, 2, &n);
    writeBytes(L, b, std::string(s, n));
    return pushInt(L, int64_t(n));
}
int b_getLength(lua_State* L) { BufferObj* b = buf(L, "getLength"); checkClosed(L, b); return pushInt(L, b->length); }
int b_getPosition(lua_State* L) { BufferObj* b = buf(L, "getPosition"); checkClosed(L, b); return pushInt(L, b->position); }
int b_setPosition(lua_State* L) {
    BufferObj* b = buf(L, "setPosition");
    int32_t    p = *argInt(L, 2, "setPosition", true);
    checkClosed(L, b);
    b->position = std::max(std::min(p, b->length), 0);
    return 0;
}
int b_clear(lua_State* L) { BufferObj* b = buf(L, "clear"); checkClosed(L, b); b->position = b->length = 0; return 0; }
int b_available(lua_State* L) { BufferObj* b = buf(L, "available"); checkClosed(L, b); return pushInt(L, b->length - b->position); }
int b_getMaxCapacity(lua_State* L) { buf(L, "getMaxCapacity"); return pushInt(L, maxBufferSize(L)); }
int b_readFromStream(lua_State* L) {
    BufferObj* b = buf(L, "readFromStream");
    notNil(L, 2, "readFromStream", "InputStream");
    auto* s = to<InputStreamObj>(L, 2, tInputStream);
    if (!s) badArg(L, 2, "readFromStream", "InputStream");
    auto amount = argInt(L, 3, "readFromStream");
    checkClosed(L, b);
    int64_t max = maxBufferSize(L);
    int64_t am  = amount ? std::max<int64_t>(std::min<int64_t>(*amount, max - b->position), 0) : max - b->position;
    int64_t i   = 0;
    for (; i < am; i++) {
        if (s->asyncOnly) ioFail(L, "This stream supports only async read");
        int c = s->s->read(L);
        if (c == -1) break;
        bWrite1(L, b, c);
    }
    return pushInt(L, i);
}
int b_writeToStream(lua_State* L) {
    BufferObj*       b = buf(L, "writeToStream");
    OutputStreamObj* o = checkOutArg(L, 2, "writeToStream", true);
    auto             amount = argInt(L, 3, "writeToStream");
    checkClosed(L, b);
    int64_t avail = b->length - b->position;
    int64_t am    = amount ? std::max<int64_t>(std::min<int64_t>(*amount, avail), -1) : avail;
    for (int64_t i = 0; i < am; i++) o->write(L, bRead1(L, b));
    return pushInt(L, am);
}
int b_close(lua_State* L) {
    BufferObj* b = buf(L, "close");
    if (!b->closed) {
        b->closed = true;
        b->buf.clear();
        b->buf.shrink_to_fit();
    }
    if (b->listed) {
        b->listed = false;
        openBuffers(L)--;
    }
    return 0;
}
int b_isClosed(lua_State* L) { return pushBool(L, buf(L, "isClosed")->closed); }
int b_toString(lua_State* L) { lua_pushstring(L, "Buffer"); return 1; }

// =====================================================================================================================
// FileAPI (host only, sandboxed to Avatar::dataDir)
// =====================================================================================================================
namespace fs = std::filesystem;
fs::path u8p(std::string const& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string pathStr(fs::path const& p) {
    auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
bool fileHost(lua_State* L) {
    return !avatarOf(L)->dataDir.empty();
}
fs::path dataRoot(lua_State* L) {
    std::error_code ec;
    fs::path        r = fs::absolute(avatarOf(L)->dataDir, ec);
    return (ec ? avatarOf(L)->dataDir : r).lexically_normal();
}
/// relativizePath + isPathAllowed. Rejects absolute / rooted paths, "..", NUL and symlinks along the way.
std::optional<fs::path> resolvePath(lua_State* L, std::string const& path) {
    if (!fileHost(L)) return std::nullopt;
    if (path.find('\0') != std::string::npos) return std::nullopt;
    fs::path p = u8p(path);
    if (p.is_absolute() || p.has_root_name() || p.has_root_directory()) return std::nullopt;
    for (auto const& part : p)
        if (part == "..") return std::nullopt;
    fs::path root = dataRoot(L);
    fs::path cur  = root;
    for (auto const& part : p.lexically_normal()) {
        if (part.empty() || part == ".") continue;
        cur /= part;
        std::error_code ec;
        if (fs::is_symlink(fs::symlink_status(cur, ec))) return std::nullopt;
    }
    std::error_code ec;
    if (fs::is_symlink(fs::symlink_status(root, ec))) return std::nullopt;
    return cur;
}
fs::path securityCheck(lua_State* L, std::string const& path) {
    if (!fileHost(L)) fail(L, "You can't use FileAPI outside of host environment");
    auto p = resolvePath(L, path);
    if (!p) {
        // symlinks get their own message in Figura
        if (fileHost(L) && path.find("..") == std::string::npos) {
            fs::path rp = u8p(path);
            if (!rp.is_absolute() && !rp.has_root_path()) {
                std::error_code ec;
                if (fs::is_symlink(fs::symlink_status(dataRoot(L) / rp, ec))) fail(L, "Symbolic links are not allowed in FileAPI " + path + "!");
            }
        }
        fail(L, "Path " + path + " is not allowed in FileAPI");
    }
    return *p;
}
std::string sysPath(fs::path const& p) { return pathStr(p); }
#ifdef _WIN32
char const* kNotFound = "The system cannot find the file specified";
char const* kDenied   = "Access is denied";
#else
char const* kNotFound = "No such file or directory";
char const* kDenied   = "Is a directory";
#endif
[[noreturn]] void fileNotFound(lua_State* L, fs::path const& p, bool dir = false) {
    javaFail(L, "java.io.FileNotFoundException", sysPath(p) + " (" + (dir ? kDenied : kNotFound) + ")");
}
std::FILE* openFile(fs::path const& p, char const* mode) {
#ifdef _WIN32
    std::wstring m(mode, mode + std::strlen(mode));
    return _wfopen(p.c_str(), m.c_str());
#else
    return std::fopen(p.c_str(), mode);
#endif
}
fs::path checkedPath(lua_State* L, char const* fn) { return securityCheck(L, *argStr(L, 2, fn, true)); }

int fAllowed(lua_State* L) { self<int>(L, tFileAPI, "allowed"); return pushBool(L, fileHost(L)); }
int fIsPathAllowed(lua_State* L) {
    self<int>(L, tFileAPI, "isPathAllowed");
    return pushBool(L, resolvePath(L, *argStr(L, 2, "isPathAllowed", true)).has_value());
}
int fExists(lua_State* L) {
    self<int>(L, tFileAPI, "exists");
    std::error_code ec;
    return pushBool(L, fs::exists(checkedPath(L, "exists"), ec));
}
int fIsFile(lua_State* L) {
    self<int>(L, tFileAPI, "isFile");
    std::error_code ec;
    return pushBool(L, fs::is_regular_file(checkedPath(L, "isFile"), ec));
}
int fIsDirectory(lua_State* L) {
    self<int>(L, tFileAPI, "isDirectory");
    std::error_code ec;
    return pushBool(L, fs::is_directory(checkedPath(L, "isDirectory"), ec));
}
int fOpenReadStream(lua_State* L) {
    self<int>(L, tFileAPI, "openReadStream");
    fs::path        p = checkedPath(L, "openReadStream");
    std::error_code ec;
    if (fs::is_directory(p, ec)) fileNotFound(L, p, true);
    std::FILE* f = openFile(p, "rb");
    if (!f) fileNotFound(L, p);
    auto* in = new FileIn;
    in->f    = f;
    auto* s  = newObj(L, tInputStream, new InputStreamObj);
    s->s.reset(in);
    return 1;
}
OutputStreamObj* openWrite(lua_State* L, fs::path const& p) {
    std::error_code ec;
    if (fs::is_directory(p, ec)) fileNotFound(L, p, true);
    std::FILE* f = openFile(p, "wb");
    if (!f) fileNotFound(L, p);
    auto* o = newObj(L, tOutputStream, new OutputStreamObj);
    o->f    = f;
    return o;
}
int fOpenWriteStream(lua_State* L) {
    self<int>(L, tFileAPI, "openWriteStream");
    openWrite(L, checkedPath(L, "openWriteStream"));
    return 1;
}
int fReadString(lua_State* L) {
    self<int>(L, tFileAPI, "readString");
    std::string path = *argStr(L, 2, "readString", true);
    Charset     cs   = charsetOf(argStr(L, 3, "readString"));
    fs::path    p    = securityCheck(L, path);
    std::error_code ec;
    if (fs::is_directory(p, ec)) fileNotFound(L, p, true);
    std::ifstream f(p, std::ios::binary);
    if (!f) fileNotFound(L, p);
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return pushStr(L, fromU16(decodeJava(data, cs)));
}
int fWriteString(lua_State* L) {
    self<int>(L, tFileAPI, "writeString");
    std::string path = *argStr(L, 2, "writeString", true);
    std::string data = *argStr(L, 3, "writeString", true);
    Charset     cs   = charsetOf(argStr(L, 4, "writeString"));
    OutputStreamObj* o = openWrite(L, securityCheck(L, path));
    std::string      bytes = encodeJava(toU16(data), cs);
    std::fwrite(bytes.data(), 1, bytes.size(), o->f);
    std::fclose(o->f);
    o->f = nullptr;
    return 0;
}
int fMkdir(lua_State* L) {
    self<int>(L, tFileAPI, "mkdir");
    fs::path        p = checkedPath(L, "mkdir");
    std::error_code ec;
    if (fs::exists(p, ec) || !fs::is_directory(p.parent_path(), ec)) return pushBool(L, false);
    return pushBool(L, fs::create_directory(p, ec) && !ec);
}
int fMkdirs(lua_State* L) {
    self<int>(L, tFileAPI, "mkdirs");
    fs::path        p = checkedPath(L, "mkdirs");
    std::error_code ec;
    if (fs::exists(p, ec)) return pushBool(L, false);
    return pushBool(L, fs::create_directories(p, ec) && !ec);
}
int fDelete(lua_State* L) {
    self<int>(L, tFileAPI, "delete");
    fs::path        p = checkedPath(L, "delete");
    std::error_code ec;
    if (fs::is_directory(fs::symlink_status(p, ec)) && !fs::is_empty(p, ec)) return pushBool(L, false);
    return pushBool(L, fs::remove(p, ec) && !ec);
}
int fList(lua_State* L) {
    self<int>(L, tFileAPI, "list");
    fs::path        p = checkedPath(L, "list");
    std::error_code ec;
    if (!fs::is_directory(p, ec)) return pushNil(L);
    std::vector<std::string> names;
    for (auto it = fs::directory_iterator(p, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        names.push_back(pathStr(it->path().filename()));
    return pushStrings(L, names);
}
int fToString(lua_State* L) { lua_pushstring(L, "FileAPI"); return 1; }

// =====================================================================================================================
// NetworkingAPI / HttpAPI (networking disabled in config: Figura's default)
// =====================================================================================================================
constexpr bool kAllowNetworking = false; // Configs.ALLOW_NETWORKING
int netIsNetworkingAllowed(lua_State* L) {
    self<int>(L, tNetworkingAPI, "isNetworkingAllowed");
    return pushBool(L, kAllowNetworking); // && isHost && permission NETWORKING >= 1
}
/// new URL(link) -> {protocol, port} or MalformedURLException message
bool parseUrl(std::string spec, std::string& err, int& port) {
    port = -1;
    auto trim = [](std::string& s) {
        size_t a = 0, b = s.size();
        while (a < b && (unsigned char)s[a] <= ' ') a++;
        while (b > a && (unsigned char)s[b - 1] <= ' ') b--;
        s = s.substr(a, b - a);
    };
    std::string orig = spec;
    trim(spec);
    if (spec.size() >= 4 && (spec.compare(0, 4, "url:") == 0 || spec.compare(0, 4, "URL:") == 0)) spec = spec.substr(4);
    std::string proto;
    for (size_t i = 0; i < spec.size(); i++) {
        char c = spec[i];
        if (c == '/') break;
        if (c == ':') {
            std::string p = spec.substr(0, i);
            bool        ok = !p.empty() && std::isalpha((unsigned char)p[0]);
            for (char x : p) ok = ok && (std::isalnum((unsigned char)x) || x == '.' || x == '+' || x == '-');
            if (ok) {
                proto = p;
                for (auto& x : proto) x = char(std::tolower((unsigned char)x));
                spec = spec.substr(i + 1);
            }
            break;
        }
    }
    if (proto.empty()) { err = "no protocol: " + orig; return false; }
    static char const* known[] = {"http", "https", "ftp", "file", "jar", "mailto", "jrt"};
    if (std::find_if(std::begin(known), std::end(known), [&](char const* k) { return proto == k; }) == std::end(known)) {
        err = "unknown protocol: " + proto;
        return false;
    }
    if (spec.compare(0, 2, "//") == 0) {
        size_t      end  = spec.find_first_of("/?#", 2);
        std::string auth = spec.substr(2, end == std::string::npos ? std::string::npos : end - 2);
        size_t      at   = auth.rfind('@');
        if (at != std::string::npos) auth = auth.substr(at + 1);
        size_t colon = auth.rfind(':');
        if (!auth.empty() && auth[0] == '[') {
            size_t rb = auth.find(']');
            colon     = rb != std::string::npos && rb + 1 < auth.size() && auth[rb + 1] == ':' ? rb + 1 : std::string::npos;
        }
        if (colon != std::string::npos) {
            std::string ps = auth.substr(colon + 1);
            if (!ps.empty()) {
                bool digits = true;
                for (char c : ps) digits = digits && std::isdigit((unsigned char)c);
                if (!digits || ps.size() > 9) { err = "For input string: \"" + ps + "\""; return false; }
                port = std::atoi(ps.c_str());
            }
        }
    }
    return true;
}
int netIsLinkAllowed(lua_State* L) {
    self<int>(L, tNetworkingAPI, "isLinkAllowed");
    auto link = argStr(L, 2, "isLinkAllowed");
    if (!link) fail(L, "Link is malformed: null (null)");
    std::string err;
    int         port;
    if (!parseUrl(*link, err, port)) fail(L, "Link is malformed: " + *link + " (" + err + ")");
    if (port != -1 && port != 80 && port != 443) fail(L, "Port " + std::to_string(port) + " not allowed, only 80 (HTTP) and 443 (HTTPS) are permitted.");
    return pushBool(L, false); // RestrictionLevel WHITELIST (default) with an empty filter list
}
int netIndex(lua_State* L) {
    self<int>(L, tNetworkingAPI, "__index");
    if (lua_isstring(L, 2) && std::strcmp(lua_tostring(L, 2), "http") == 0) {
        pushRef(L, &gApiAnchors[5], tHttpAPI);
        return 1;
    }
    return 0;
}
int netToString(lua_State* L) { lua_pushstring(L, "NetworkingAPI"); return 1; }

struct HttpBuilderObj {
    std::string                        uri, method = "GET";
    std::map<std::string, std::string> headers;
};
int httpRequest(lua_State* L) {
    self<int>(L, tHttpAPI, "request");
    std::string uri = *argStr(L, 2, "request", true);
    auto*       b   = newObj(L, tHttpRequestBuilder, new HttpBuilderObj);
    b->uri          = uri;
    return 1;
}
int httpToString(lua_State* L) { lua_pushstring(L, "HttpAPI"); return 1; }
HttpBuilderObj* hb(lua_State* L, char const* m) { return self<HttpBuilderObj>(L, tHttpRequestBuilder, m); }
int hbUri(lua_State* L) { HttpBuilderObj* b = hb(L, "uri"); b->uri = *argStr(L, 2, "uri", true); lua_settop(L, 1); return 1; }
int hbMethod(lua_State* L) { HttpBuilderObj* b = hb(L, "method"); b->method = argStr(L, 2, "method").value_or("GET"); lua_settop(L, 1); return 1; }
int hbBody(lua_State* L) {
    hb(L, "body");
    if (!isNil(L, 2) && !toRef(L, 2, tInputStream) && !toRef(L, 2, tBuffer)) fail(L, "Invalid request body type");
    setHeld(L, 1, 1, 2);
    lua_settop(L, 1);
    return 1;
}
int hbHeader(lua_State* L) {
    HttpBuilderObj* b = hb(L, "header");
    std::string     h = *argStr(L, 2, "header", true);
    auto            v = argStr(L, 3, "header");
    if (!v) b->headers.erase(h);
    else b->headers[h] = *v;
    lua_settop(L, 1);
    return 1;
}
int hbGetUri(lua_State* L) { return pushStr(L, hb(L, "getUri")->uri); }
int hbGetMethod(lua_State* L) { return pushStr(L, hb(L, "getMethod")->method); }
int hbGetBody(lua_State* L) { hb(L, "getBody"); pushHeld(L, 1, 1); return 1; }
int hbGetHeaders(lua_State* L) {
    HttpBuilderObj* b = hb(L, "getHeaders");
    lua_newtable(L);
    for (auto const& [k, v] : b->headers) {
        lua_pushlstring(L, v.data(), v.size());
        lua_setfield(L, -2, k.c_str());
    }
    return 1;
}
int hbSend(lua_State* L) {
    hb(L, "send");
    // NetworkingAPI.securityCheck: isHost -> Configs.ALLOW_NETWORKING (off) -> ...
    fail(L, "Networking is disabled in config");
}
int hbToString(lua_State* L) {
    HttpBuilderObj* b = hb(L, "__tostring");
    return pushStr(L, "HttpRequestBuilder(" + b->method + ":" + b->uri + ")");
}
struct HttpResponseObj {
    int code = 0;
};
int hrGetData(lua_State* L) { self<HttpResponseObj>(L, tHttpResponse, "getData"); pushHeld(L, 1, 1); return 1; }
int hrGetResponseCode(lua_State* L) { return pushInt(L, self<HttpResponseObj>(L, tHttpResponse, "getResponseCode")->code); }
int hrGetHeaders(lua_State* L) { self<HttpResponseObj>(L, tHttpResponse, "getHeaders"); pushHeld(L, 1, 2); return 1; }
int hrToString(lua_State* L) { return pushStr(L, "HttpResponse(" + std::to_string(self<HttpResponseObj>(L, tHttpResponse, "__tostring")->code) + ")"); }

// =====================================================================================================================
// ResourcesAPI (Avatar.resources: path -> gzip bytes, from the NBT "resources" compound)
// =====================================================================================================================
lyrics::json::Value const* resourceTable(Avatar* av) {
    if (!av->nbtJson) return nullptr;
    auto const& r = (*av->nbtJson)["resources"];
    return r.isObject() ? &r : nullptr;
}
bool readResourceBytes(Avatar* av, lyrics::json::Value const& v, std::vector<uint8_t>& out) {
    if (v.isArray()) { // plain NBT byte array
        for (auto const& b : v.items()) out.push_back(uint8_t(int(b.asNumber()) & 0xFF));
        return true;
    }
    if (!v.isObject() || !v["__file"].isString()) return false;
    fs::path rel = u8p(v["__file"].asString());
    if (rel.is_absolute() || rel.has_root_path()) return false;
    for (auto const& part : rel)
        if (part == "..") return false;
    std::ifstream f(av->dir / rel, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}
int resGetImpl(lua_State* L, std::string const& path) {
    Avatar* av = avatarOf(L);
    auto*   t  = resourceTable(av);
    if (!t || !t->has(path)) return pushNil(L);
    std::vector<uint8_t> gz, raw;
    readResourceBytes(av, (*t)[path], gz);
    std::string err;
    if (!data::gunzip(gz, raw, &err)) fail(L, "vm error: " + err);
    auto* in = new GzipIn;
    in->data = std::move(raw);
    auto* s  = newObj(L, tInputStream, new InputStreamObj);
    s->s.reset(in);
    return 1;
}
int resGet(lua_State* L) {
    self<int>(L, tResourcesAPI, "get");
    return resGetImpl(L, *argStr(L, 2, "get", true));
}
int resIndex(lua_State* L) {
    self<int>(L, tResourcesAPI, "__index");
    return resGetImpl(L, *argStr(L, 2, "__index", true));
}
int resGetPaths(lua_State* L) {
    self<int>(L, tResourcesAPI, "getPaths");
    JavaHashOrder order;
    if (auto* t = resourceTable(avatarOf(L)))
        for (auto const& [k, v] : t->members()) order.insert(k);
    return pushStrings(L, order.keys());
}
int resToString(lua_State* L) { lua_pushstring(L, "ResourcesAPI"); return 1; }

int strTo(lua_State* L, char const* s) { lua_pushstring(L, s); return 1; }

} // namespace

// =====================================================================================================================
// FiguraLuaJson (global toJson / parseJson) - exported for LuaApi.cpp
// =====================================================================================================================
/// toJson(value): LuaUtils.asJsonValue(value).toString() (JsonElement.toString: compact, no HTML escaping, nulls kept)
int figuraToJson(lua_State* L) {
    if (lua_gettop(L) == 0) return 0;
    JNode n;
    toNode(L, 1, n, 0);
    WriterOpts o;
    o.html           = false;
    o.serializeNulls = true;
    return pushStr(L, writeJson(n, o));
}
/// parseJson(string): JsonParser.parseString -> JsonUtils.asLuaValue; "Failed to parse malformed Json: " + cause
int figuraParseJson(lua_State* L) {
    std::string s = luaL_checkstring(L, 1);
    JNode       n;
    std::string err;
    bool        bad = false;
    try {
        n = parseGson(s);
    } catch (JsonErr const& e) {
        bad = true;
        err = e.cause.empty() ? "null" : e.cause;
    }
    if (bad) fail(L, "Failed to parse malformed Json: " + err);
    pushNode(L, n);
    return 1;
}

void openDataApis(Avatar& a) {
    lua_State* L = a.L;
    auto reg = [&](char const* name, std::vector<luaL_Reg> methods, lua_CFunction tostring, lua_CFunction index = nullptr,
                   lua_CFunction newindex = nullptr, std::vector<luaL_Reg> meta = {}) {
        TypeDef d;
        d.name     = name;
        d.methods  = std::move(methods);
        d.index    = index;
        d.newindex = newindex;
        d.tostring = tostring;
        d.meta     = std::move(meta);
        return registerType(L, d);
    };

    // ---- json
    tJsonAPI = reg("JsonAPI", {{"newBuilder", jaNewBuilder}, {"newArray", jaNewArray}, {"newObject", jaNewObject}, {"isSerializable", jaIsSerializable}},
                   [](lua_State* L2) { return strTo(L2, "JsonAPI"); });
    tJsonArray = reg("JsonArray",
                     {{"get", arrGet}, {"size", arrSize}, {"contains", arrContains}, {"add", arrAdd}, {"insert", arrInsert}, {"set", arrSet},
                      {"removeAt", arrRemoveAt}, {"remove", arrRemove}, {"indexOf", arrIndexOf}, {"lastIndexOf", arrLastIndexOf}, {"clear", arrClear}},
                     arrToString, arrIndex, arrNewIndex,
                     {{"__len", arrLen}, {"__ipairs", arrIPairs}, {"__pairs", arrIPairs}, {"__gc", gcObj<JsonArrayObj>}});
    tJsonObject = reg("JsonObject",
                      {{"size", objSize}, {"get", objGet}, {"containsKey", objContainsKey}, {"put", objPutM}, {"remove", objRemove},
                       {"clear", objClear}, {"containsValue", objContainsValue}},
                      objToString, objIndex, objNewIndex, {{"__pairs", objPairs}, {"__ipairs", objIPairs}, {"__gc", gcObj<JsonObjectObj>}});
    tJsonBuilder    = reg("JsonBuilder", {{"build", bldBuild}}, bldToString, bldIndex, bldNewIndex, {{"__gc", gcObj<JsonBuilderObj>}});
    tJsonSerializer = reg("JsonSerializer", {{"serialize", serSerialize}, {"deserialize", serDeserialize}}, serToString, nullptr, nullptr,
                          {{"__gc", gcObj<JsonSerializerObj>}});

    // ---- data
    tDataAPI = reg("DataAPI", {{"createBuffer", dataCreateBuffer}}, dataToString);
    tBuffer  = reg("Buffer",
                   {{"read", b_read}, {"readShort", b_readShort}, {"readUShort", b_readUShort}, {"readInt", b_readInt}, {"readLong", b_readLong},
                    {"readFloat", b_readFloat}, {"readDouble", b_readDouble}, {"readShortLE", b_readShortLE}, {"readUShortLE", b_readUShortLE},
                    {"readIntLE", b_readIntLE}, {"readLongLE", b_readLongLE}, {"readFloatLE", b_readFloatLE}, {"readDoubleLE", b_readDoubleLE},
                    {"readString", b_readString}, {"readBase64", b_readBase64}, {"readByteArray", b_readByteArray}, {"write", b_write},
                    {"writeShort", b_writeShort}, {"writeUShort", b_writeUShort}, {"writeInt", b_writeInt}, {"writeLong", b_writeLong},
                    {"writeFloat", b_writeFloat}, {"writeDouble", b_writeDouble}, {"writeShortLE", b_writeShortLE},
                    {"writeUShortLE", b_writeUShortLE}, {"writeIntLE", b_writeIntLE}, {"writeLongLE", b_writeLongLE},
                    {"writeFloatLE", b_writeFloatLE}, {"writeDoubleLE", b_writeDoubleLE}, {"writeString", b_writeString},
                    {"writeBase64", b_writeBase64}, {"writeByteArray", b_writeByteArray}, {"getLength", b_getLength},
                    {"getPosition", b_getPosition}, {"setPosition", b_setPosition}, {"clear", b_clear}, {"available", b_available},
                    {"getMaxCapacity", b_getMaxCapacity}, {"readFromStream", b_readFromStream}, {"writeToStream", b_writeToStream},
                    {"close", b_close}, {"isClosed", b_isClosed}},
                   b_toString, nullptr, nullptr, {{"__gc", gcObj<BufferObj>}});
    tInputStream = reg("InputStream",
                       {{"read", isRead}, {"readAsync", isReadAsync}, {"skip", isSkip}, {"available", isAvailable}, {"close", isClose},
                        {"mark", isMark}, {"reset", isReset}, {"markSupported", isMarkSupported}, {"isAsyncOnly", isIsAsyncOnly},
                        {"transferTo", isTransferTo}},
                       isToString, nullptr, nullptr, {{"__gc", gcObj<InputStreamObj>}});
    tOutputStream = reg("OutputStream", {{"write", osWrite}, {"flush", osFlush}, {"close", osClose}}, osToString, nullptr, nullptr,
                        {{"__gc", gcObj<OutputStreamObj>}});
    tFuture = reg("Future", {{"isDone", futIsDone}, {"getValue", futGetValue}, {"getOrError", futGetOrError}, {"throwError", futThrowError}},
                  futToString, nullptr, nullptr, {{"__gc", gcObj<FutureObj>}});

    // ---- file / net / resources
    tFileAPI = reg("FileAPI",
                   {{"isPathAllowed", fIsPathAllowed}, {"allowed", fAllowed}, {"exists", fExists}, {"isFile", fIsFile},
                    {"isDirectory", fIsDirectory}, {"openReadStream", fOpenReadStream}, {"openWriteStream", fOpenWriteStream},
                    {"readString", fReadString}, {"writeString", fWriteString}, {"mkdir", fMkdir}, {"mkdirs", fMkdirs}, {"delete", fDelete},
                    {"list", fList}},
                   fToString);
    tNetworkingAPI = reg("NetworkingAPI", {{"isNetworkingAllowed", netIsNetworkingAllowed}, {"isLinkAllowed", netIsLinkAllowed}}, netToString, netIndex);
    tHttpAPI       = reg("HttpAPI", {{"request", httpRequest}}, httpToString);
    tHttpRequestBuilder = reg("HttpRequestBuilder",
                              {{"uri", hbUri}, {"method", hbMethod}, {"body", hbBody}, {"header", hbHeader}, {"getUri", hbGetUri},
                               {"getMethod", hbGetMethod}, {"getBody", hbGetBody}, {"getHeaders", hbGetHeaders}, {"send", hbSend}},
                              hbToString, nullptr, nullptr, {{"__gc", gcObj<HttpBuilderObj>}});
    tHttpResponse = reg("HttpResponse", {{"getData", hrGetData}, {"getResponseCode", hrGetResponseCode}, {"getHeaders", hrGetHeaders}}, hrToString,
                        nullptr, nullptr, {{"__gc", gcObj<HttpResponseObj>}});
    tResourcesAPI = reg("ResourcesAPI", {{"get", resGet}, {"getPaths", resGetPaths}}, resToString, resIndex);

    struct G {
        char const* name;
        int         anchor;
        uint32_t    type;
    } globals[] = {{"json", 0, tJsonAPI}, {"data", 1, tDataAPI}, {"file", 2, tFileAPI}, {"net", 3, tNetworkingAPI}, {"resources", 4, tResourcesAPI}};
    for (auto const& g : globals) {
        pushRef(L, &gApiAnchors[g.anchor], g.type);
        lua_setglobal(L, g.name);
    }
}

} // namespace bf::figura
