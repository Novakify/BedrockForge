// FiguraBridge: registers all Figura APIs into an avatar's Lua state + the native hooks the Lua prelude uses.
#include "figura/core/LuaApi.h"

#include <cstdio>
#include <cstdlib>
#include <map>

#include "figura/core/Avatar.h"
#include "figura/core/LuaBind.h"
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
    return &a->keybinds[i - 1];
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
int hKeyVanilla(lua_State* L) {
    lua_pushstring(L, avatarOf(L)->host->vanillaKeyBinding(optStr(L, 1, "")).c_str());
    return 1;
}
// config store: per avatar name, in memory (the Bedrock host may persist it later)
std::map<std::string, std::map<std::string, std::string>> gConfig;
int hCfgSave(lua_State* L) {
    std::string n = optStr(L, 1, "default"), k = optStr(L, 2, "");
    if (lua_isnoneornil(L, 3)) gConfig[n].erase(k);
    else gConfig[n][k] = lua_isstring(L, 3) ? lua_tostring(L, 3) : (lua_toboolean(L, 3) ? "true" : "false");
    return 0;
}
int hCfgLoad(lua_State* L) {
    std::string n = optStr(L, 1, "default");
    auto&       m = gConfig[n];
    if (lua_isnoneornil(L, 2)) {
        lua_newtable(L);
        for (auto& [k, v] : m) { lua_pushstring(L, v.c_str()); lua_setfield(L, -2, k.c_str()); }
        return 1;
    }
    auto it = m.find(optStr(L, 2, ""));
    if (it == m.end()) { lua_pushnil(L); return 1; }
    if (it->second == "true" || it->second == "false") lua_pushboolean(L, it->second == "true");
    else if (lua_stringtonumber_compat(it->second)) lua_pushnumber(L, std::strtod(it->second.c_str(), nullptr));
    else lua_pushstring(L, it->second.c_str());
    return 1;
}
void toJson(lua_State* L, int idx, std::string& out, int depth) {
    idx = lua_absindex(L, idx);
    switch (lua_type(L, idx)) {
    case LUA_TNUMBER: {
        char b[64];
        std::snprintf(b, sizeof b, "%.17g", lua_tonumber(L, idx));
        out += b;
        break;
    }
    case LUA_TBOOLEAN: out += lua_toboolean(L, idx) ? "true" : "false"; break;
    case LUA_TSTRING: {
        out += '"';
        for (char c : std::string(lua_tostring(L, idx))) {
            if (c == '"' || c == '\\') { out += '\\'; out += c; }
            else if (c == '\n') out += "\\n";
            else out += c;
        }
        out += '"';
        break;
    }
    case LUA_TTABLE: {
        if (depth > 32) { out += "null"; break; }
        size_t n = lua_rawlen(L, idx);
        if (n > 0) {
            out += '[';
            for (size_t i = 1; i <= n; i++) {
                if (i > 1) out += ',';
                lua_rawgeti(L, idx, int(i));
                toJson(L, -1, out, depth + 1);
                lua_pop(L, 1);
            }
            out += ']';
        } else {
            out += '{';
            bool first = true;
            lua_pushnil(L);
            while (lua_next(L, idx)) {
                if (!first) out += ',';
                first = false;
                lua_pushvalue(L, -2);
                out += '"';
                out += lua_tostring(L, -1) ? lua_tostring(L, -1) : "?";
                out += "\":";
                lua_pop(L, 1);
                toJson(L, -1, out, depth + 1);
                lua_pop(L, 1);
            }
            out += '}';
        }
        break;
    }
    default: out += "null";
    }
}
int hToJson(lua_State* L) {
    std::string s;
    toJson(L, 1, s, 0);
    lua_pushstring(L, s.c_str());
    return 1;
}
void pushJsonValue(lua_State* L, lyrics::json::Value const& v) {
    using T = lyrics::json::Value::Type;
    switch (v.type()) {
    case T::Bool: lua_pushboolean(L, v.asBool()); break;
    case T::Number: lua_pushnumber(L, v.asNumber()); break;
    case T::String: lua_pushstring(L, v.asString().c_str()); break;
    case T::Array:
        lua_newtable(L);
        for (size_t i = 0; i < v.size(); i++) { pushJsonValue(L, v[i]); lua_rawseti(L, -2, int(i + 1)); }
        break;
    case T::Object:
        lua_newtable(L);
        for (auto const& [k, x] : v.members()) { pushJsonValue(L, x); lua_setfield(L, -2, k.c_str()); }
        break;
    default: lua_pushnil(L);
    }
}
int hParseJson(lua_State* L) {
    auto v = lyrics::json::parse(luaL_checkstring(L, 1));
    if (!v) return luaL_error(L, "invalid json");
    pushJsonValue(L, *v);
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

    lua_newtable(L); // __fb
    luaL_Reg fns[] = {{"print", hPrint}, {"keyNew", hKeyNew}, {"keyRef", hKeyRef}, {"keyDown", hKeyDown},
                      {"keySetKey", hKeySetKey}, {"keyEnable", hKeyEnable}, {"keyVanilla", hKeyVanilla},
                      {"cfgSave", hCfgSave}, {"cfgLoad", hCfgLoad}, {"toJson", hToJson}, {"parseJson", hParseJson},
                      {nullptr, nullptr}};
    luaL_setfuncs(L, fns, 0);
    lua_newtable(L); // registry setters
    for (char const* n : {"callEvent", "keyEdge", "wheel"}) {
        lua_pushstring(L, (std::string("fb.") + n).c_str());
        lua_pushcclosure(L, regSetter, 1);
        lua_setfield(L, -2, n);
    }
    lua_setfield(L, -2, "registry");
    lua_setglobal(L, "__fb");
}

} // namespace bf::figura
