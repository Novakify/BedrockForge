// FiguraBridge Lua binding core. Vectors / matrices / VectorsAPI / MatricesAPI live in LuaMath.cpp.
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

/// Generic __index: methods table (upvalue 1), then metamethods by name ("__add", "__index" ... from the metatable,
/// upvalue 3, so `obj.__add` is reachable like Figura's whitelisted metamethods), then the type's own fallback
/// (upvalue 2, may be nil).
int genericIndex(lua_State* L) {
    lua_pushvalue(L, 2);
    lua_rawget(L, lua_upvalueindex(1));
    if (!lua_isnil(L, -1)) return 1;
    lua_pop(L, 1);
    if (lua_type(L, 2) == LUA_TSTRING) {
        char const* k = lua_tostring(L, 2);
        if (k[0] == '_' && k[1] == '_') {
            lua_pushvalue(L, 2);
            lua_rawget(L, lua_upvalueindex(3));
            if (lua_isfunction(L, -1)) return 1;
            lua_pop(L, 1);
        }
    }
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
    lua_pushvalue(L, -3); // the metatable itself (cycle mt -> __index closure -> mt is fine for the GC)
    lua_pushcclosure(L, genericIndex, 3);
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

void pushTypeMetatable(lua_State* L, uint32_t type) { luaL_getmetatable(L, mtName(type).c_str()); }

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

} // namespace bf::figura::lua
