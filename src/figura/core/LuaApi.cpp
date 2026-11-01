// FiguraBridge: registers all Figura APIs into an avatar's Lua state + the native hooks the Lua prelude uses.
#include "figura/core/LuaApi.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

#include "figura/core/Avatar.h"
#include "figura/core/LuaBind.h"
#include "figura/core/LuaTex.h"
#include "lyrics/core/Json.h"

namespace bf::figura {

using namespace lua;

bool lua_stringtonumber_compat(std::string const& s);

namespace {
int hPrint(lua_State* L) {
    Avatar* a = avatarOf(L);
    a->host->log(optStr(L, 1, ""));
    return 0;
}
// ---- keybinds (Avatar::keybinds: the host polls them, the prelude keeps FiguraKeybind state)
int hKeyNew(lua_State* L) {
    Avatar*      a = avatarOf(L);
    KeybindState k;
    k.name = optStr(L, 1, "");
    k.key  = optStr(L, 2, "key.keyboard.unknown");
    k.gui  = optBool(L, 3, false);
    a->keybinds.push_back(k);
    lua_pushinteger(L, lua_Integer(a->keybinds.size()));
    return 1;
}
KeybindState* key(lua_State* L) {
    Avatar* a = avatarOf(L);
    int     i = int(luaL_checkinteger(L, 1));
    if (i < 1 || i > int(a->keybinds.size())) luaL_error(L, "bad keybind");
    return &a->keybinds[size_t(i - 1)];
}
int hKeyRef(lua_State* L) {
    KeybindState* k = key(L);
    lua_pushvalue(L, 2);
    k->ref = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}
int hKeyDown(lua_State* L) {
    KeybindState* k = key(L);
    lua_pushboolean(L, k->enabled && avatarOf(L)->host->keyDown(k->key));
    return 1;
}
int hKeySetKey(lua_State* L) { key(L)->key = optStr(L, 2, "key.keyboard.unknown"); return 0; }
int hKeyEnable(lua_State* L) { key(L)->enabled = optBool(L, 2, true); return 0; }
int hKeyGui(lua_State* L) { key(L)->gui = optBool(L, 2, false); return 0; }
int hKeyVanilla(lua_State* L) {
    lua_pushstring(L, avatarOf(L)->host->vanillaKeyBinding(optStr(L, 1, "")).c_str());
    return 1;
}
// ---- config store (ConfigAPI files: figura/config/<name>.json). In memory per process; written to disk when the
// host gives the avatar a data dir (<dataDir>/../config).
std::map<std::string, std::string> gConfig;
std::filesystem::path cfgPath(lua_State* L, std::string const& n) {
    Avatar* a = avatarOf(L);
    if (a->dataDir.empty()) return {};
    return a->dataDir.parent_path() / "config" / (n + ".json");
}
int hCfgRead(lua_State* L) {
    std::string n  = luaL_checkstring(L, 1);
    auto        it = gConfig.find(n);
    if (it != gConfig.end()) {
        lua_pushlstring(L, it->second.data(), it->second.size());
        return 1;
    }
    auto p = cfgPath(L, n);
    if (!p.empty()) {
        std::ifstream f(p, std::ios::binary);
        if (f) {
            std::stringstream ss;
            ss << f.rdbuf();
            gConfig[n] = ss.str();
            lua_pushstring(L, gConfig[n].c_str());
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}
int hCfgWrite(lua_State* L) {
    std::string n = luaL_checkstring(L, 1);
    size_t      len;
    char const* s = luaL_checklstring(L, 2, &len);
    gConfig[n].assign(s, len);
    auto p = cfgPath(L, n);
    if (!p.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        std::ofstream f(p, std::ios::binary);
        if (f) f.write(s, std::streamsize(len));
    }
    return 0;
}
int hAvatarName(lua_State* L) { lua_pushstring(L, avatarOf(L)->name.c_str()); return 1; }

// ---- toJson / parseJson: FiguraLuaJson lives in LuaApiData.cpp (figuraToJson / figuraParseJson)
// ---- misc natives for the prelude
int hRawMt(lua_State* L) {
    if (!lua_getmetatable(L, 1)) lua_pushnil(L);
    return 1;
}
int hUdata(lua_State* L) { // zero-size userdata with the given metatable (Figura objects implemented in Lua)
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_newuserdata(L, 0);
    lua_pushvalue(L, 1);
    lua_setmetatable(L, -2);
    return 1;
}
std::string javaFloatStr(float f) {
    if (std::isnan(f)) return "NaN";
    if (std::isinf(f)) return f > 0 ? "Infinity" : "-Infinity";
    char buf[64];
    int  prec = 1;
    for (; prec <= 9; prec++) {
        std::snprintf(buf, sizeof buf, "%.*e", prec - 1, double(f));
        if (std::strtof(buf, nullptr) == f) break;
    }
    return javaDoubleStr(std::strtod(buf, nullptr));
}
/// FiguraLuaPrinter number text: integral -> LuaDouble.tojstring, else DecimalFormat("0.#####", DOWN)
int hNumFmt(lua_State* L) {
    double d = luaL_checknumber(L, 1);
    if (!std::isnan(d) && d == std::rint(d)) {
        if (std::isinf(d)) { lua_pushstring(L, d > 0 ? "inf" : "-inf"); return 1; }
        if (std::fabs(d) < 9.2e18) {
            char b[32];
            std::snprintf(b, sizeof b, "%lld", static_cast<long long>(d));
            lua_pushstring(L, b);
            return 1;
        }
        lua_pushstring(L, javaFloatStr(float(d)).c_str());
        return 1;
    }
    if (std::isnan(d)) { lua_pushstring(L, "NaN"); return 1; }
    // shortest digits (FloatingDecimal), then truncate to 5 decimals
    char buf[64];
    int  prec = 1;
    for (; prec <= 17; prec++) {
        std::snprintf(buf, sizeof buf, "%.*e", prec - 1, d);
        if (std::strtod(buf, nullptr) == d) break;
    }
    std::string s = buf;
    bool        neg = s[0] == '-';
    if (neg) s.erase(0, 1);
    size_t      e   = s.find('e');
    int         exp = std::atoi(s.c_str() + e + 1);
    std::string digits;
    for (size_t i = 0; i < e; i++)
        if (std::isdigit(static_cast<unsigned char>(s[i]))) digits += s[i];
    std::string ip, fp;
    int         point = exp + 1;
    if (point <= 0) {
        ip = "0";
        fp = std::string(size_t(-point), '0') + digits;
    } else if (point >= int(digits.size())) {
        ip = digits + std::string(size_t(point - int(digits.size())), '0');
    } else {
        ip = digits.substr(0, size_t(point));
        fp = digits.substr(size_t(point));
    }
    if (fp.size() > 5) fp.resize(5);
    while (!fp.empty() && fp.back() == '0') fp.pop_back();
    std::string r = (neg ? "-" : "") + ip + (fp.empty() ? "" : "." + fp);
    lua_pushstring(L, r.c_str());
    return 1;
}
int hMethodNames(lua_State* L) { // method names of a native Figura object (printTable on userdata)
    lua_settop(L, 1);
    lua_newtable(L);
    if (!lua_getmetatable(L, 1)) return 1;
    lua_getfield(L, -1, "__index");
    if (lua_iscfunction(L, -1) && lua_getupvalue(L, -1, 1) && lua_istable(L, -1)) {
        int t = lua_gettop(L), i = 1;
        lua_pushnil(L);
        while (lua_next(L, t)) {
            lua_pop(L, 1);
            if (lua_type(L, -1) == LUA_TSTRING) {
                lua_pushvalue(L, -1);
                lua_rawseti(L, 2, i++); // the result table
            }
        }
    }
    lua_settop(L, 2);
    return 1;
}
int hB64Enc(lua_State* L) {
    size_t      n;
    char const* s = luaL_checklstring(L, 1, &n);
    std::string r = b64Encode(std::string(s, n));
    lua_pushlstring(L, r.data(), r.size());
    return 1;
}
int hB64Dec(lua_State* L) {
    std::string out, err;
    if (!b64Decode(luaL_checkstring(L, 1), out, &err)) return luaL_error(L, "%s", err.c_str());
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}
int regSetter(lua_State* L) {
    lua_pushvalue(L, 1);
    lua_setfield(L, LUA_REGISTRYINDEX, lua_tostring(L, lua_upvalueindex(1)));
    return 0;
}
} // namespace

bool lua_stringtonumber_compat(std::string const& s) {
    if (s.empty()) return false;
    char* end = nullptr;
    std::strtod(s.c_str(), &end);
    return end && *end == 0;
}

void openFiguraApis(Avatar& a) {
    lua_State* L = a.L;
    openModelApi(a);
    openAnimationApi(a);
    openEntityApis(a);
    openDataApis(a);

    lua_newtable(L); // __fb
    luaL_Reg fns[] = {{"print", hPrint}, {"keyNew", hKeyNew}, {"keyRef", hKeyRef}, {"keyDown", hKeyDown},
                      {"keySetKey", hKeySetKey}, {"keyEnable", hKeyEnable}, {"keyGui", hKeyGui},
                      {"keyVanilla", hKeyVanilla}, {"cfgRead", hCfgRead}, {"cfgWrite", hCfgWrite},
                      {"avatarName", hAvatarName}, {"toJson", figuraToJson}, {"parseJson", figuraParseJson},
                      {"rawmt", hRawMt}, {"udata", hUdata}, {"numfmt", hNumFmt}, {"methodNames", hMethodNames},
                      {"b64enc", hB64Enc}, {"b64dec", hB64Dec}, {nullptr, nullptr}};
    luaL_setfuncs(L, fns, 0);
    lua_newtable(L); // registry setters
    for (char const* n : {"callEvent", "keyEdge", "wheel", "wheelScroll", "wheelState", "runPing"}) {
        lua_pushstring(L, (std::string("fb.") + n).c_str());
        lua_pushcclosure(L, regSetter, 1);
        lua_setfield(L, -2, n);
    }
    lua_setfield(L, -2, "registry");
    lua_setglobal(L, "__fb");
}

} // namespace bf::figura
