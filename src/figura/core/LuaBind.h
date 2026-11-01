// FiguraBridge Lua binding helpers (Lua 5.2 C API, compiled as C++ so lua_error unwinds C++ frames).
// Mirrors Figura's LuaTypeManager: every exposed Java class = one metatable whose __index is a FUNCTION
// (methods first, then the class's own __index(key)), so scripts can wrap it (GSAnimBlend does). The metatables are
// published to scripts as `figuraMetatables.<TypeName>`.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "../../../third_party/lua52/lauxlib.h"
#include "../../../third_party/lua52/lua.h"
#include "../../../third_party/lua52/lualib.h"

#include "figura/core/FbMath.h"

namespace bf::figura::lua {

/// Userdata layout for reference objects (one cached userdata per C++ object, so == and table keys work).
struct RefUD {
    void*    ptr;
    uint32_t type;
};

struct TypeDef {
    char const*            name;          // Figura type name (shown by type(), key in figuraMetatables)
    std::vector<luaL_Reg>  methods;       // name -> C function (called with self as arg 1)
    lua_CFunction          index    = nullptr; // fallback __index(self, key) after methods; may be null
    lua_CFunction          newindex = nullptr; // __newindex(self, key, value); null = error like Figura
    lua_CFunction          tostring = nullptr;
    std::vector<luaL_Reg>  meta;          // extra metamethods (__add, __eq, __len, __call ...)
};

/// Register a type: creates metatable "fb:<name>", stores it in figuraMetatables. Returns type id.
uint32_t registerType(lua_State* L, TypeDef const& def);
uint32_t typeId(char const* name);
char const* typeName(uint32_t id);

/// Push a reference object (cached). ptr null -> nil.
void  pushRef(lua_State* L, void* ptr, uint32_t type);
/// Forget a cached object (when the C++ object dies).
void  forgetRef(lua_State* L, void* ptr);
void* toRef(lua_State* L, int idx, uint32_t type);              // null if not that type
void* checkRef(lua_State* L, int idx, uint32_t type, char const* fn);
template <class T> T* check(lua_State* L, int idx, uint32_t type, char const* fn) { return static_cast<T*>(checkRef(L, idx, type, fn)); }
template <class T> T* to(lua_State* L, int idx, uint32_t type) { return static_cast<T*>(toRef(L, idx, type)); }
/// Figura type name of any value at idx (for type() and error messages).
std::string figuraTypeName(lua_State* L, int idx);

// ---- vectors (value types) ----
uint32_t vecType(int n);                       // 2, 3, 4
void     pushVec(lua_State* L, double const* v, int n);
inline void pushVec3(lua_State* L, Vec3 v) { double d[3] = {v.x, v.y, v.z}; pushVec(L, d, 3); }
inline void pushVec2(lua_State* L, double x, double y) { double d[2] = {x, y}; pushVec(L, d, 2); }
inline void pushVec4(lua_State* L, double x, double y, double z, double w) { double d[4] = {x, y, z, w}; pushVec(L, d, 4); }
double*  toVec(lua_State* L, int idx, int* n); // null if not a vector
/// LuaUtils.parseVec3(fn, x, y, z): (vec3) or (x, y, z numbers; nil -> def)
Vec3     parseVec3(lua_State* L, int idx, char const* fn, double def = 0);
/// optional variant: if the arg at idx is nil and the next two are nil too -> returns false
bool     optVec3(lua_State* L, int idx, Vec3* out);
void     openVectors(lua_State* L);            // registers Vector2/3/4 + vectors API + `vec`
void     openMatrices(lua_State* L);

// ---- matrices (value types, Figura FiguraMat4 / FiguraMat3) ----
void   pushMat4(lua_State* L, Mat4 const& m);
Mat4*  toMat4(lua_State* L, int idx);

// ---- misc helpers ----
double      optNum(lua_State* L, int idx, double def);
bool        optBool(lua_State* L, int idx, bool def);
std::string optStr(lua_State* L, int idx, char const* def);
[[noreturn]] void raise(lua_State* L, std::string const& msg);
int         pushStrings(lua_State* L, std::vector<std::string> const& v); // as a Lua array table

} // namespace bf::figura::lua
