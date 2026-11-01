// FiguraBridge: shared pieces of the ModelPart / render task / texture Lua APIs (LuaApiModels.cpp, LuaTask.cpp,
// LuaTex.cpp): Figura LuaUtils argument parsing, FiguraMat3 bridging, the in-memory FiguraTexture and helpers.
#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "figura/core/LuaBind.h"
#include "figura/core/Model.h"

namespace bf::figura {

class Avatar;

extern uint32_t tVertex, tTextTask, tItemTask, tBlockTask, tSpriteTask, tEntityTask;

// ---------------------------------------------------------------- LuaUtils (Figura argument parsing)
struct V2 { double x = 0, y = 0; };
struct V4 { double x = 0, y = 0, z = 0, w = 0; };
/// LuaUtils.parseVec2(fn, x, y, dx, dy): (vec2) or (x, y numbers; nil -> default)
V2   luParseVec2(lua_State* L, int idx, char const* fn, double dx = 0, double dy = 0);
/// LuaUtils.parseVec3(fn, x, y, z, defaults)
Vec3 luParseVec3(lua_State* L, int idx, char const* fn, double dx = 0, double dy = 0, double dz = 0);
/// LuaUtils.parseOneArgVec: one number -> all three components
Vec3 luParseOneArgVec(lua_State* L, int idx, char const* fn, double def);
/// LuaUtils.parseVec4(fn, x, y, z, w, defaults): vec3 -> (x, y, z, defaultW), vec4, or numbers
V4   luParseVec4(lua_State* L, int idx, char const* fn, double dx, double dy, double dz, double dw);
/// Java (int) cast of a double (saturating, NaN -> 0)
int  javaInt(double d);
/// Java Double.toString
std::string javaDoubleStr(double d);
/// float precision as Figura stores it ((double)(float)v)
inline double fl(double v) { return double(float(v)); }

// LightTexture / OverlayTexture packing (Minecraft 1.20)
inline int lightPack(int block, int sky) { return block << 4 | sky << 20; }
inline int lightBlock(int p) { return p >> 4 & 0xFFFF; }
inline int lightSky(int p) { return p >> 20 & 0xFFFF; }
inline int overlayPack(int u, int v) { return u | v << 16; }

// ---------------------------------------------------------------- FiguraMat3 bridging (Matrix3 lives in LuaBind)
/// Push a Figura Matrix3 (row-major m[r*3+c]) through matrices.mat3; pushes nil if Matrix3 is not available.
void pushMat3(lua_State* L, double const* m);
/// Read a Matrix3 argument (fields v11..v33). Returns false if the value isn't a Matrix3.
bool readMat3(lua_State* L, int idx, double* m);
/// Remember the `matrices` table (call once at API open time, before scripts can replace the global).
void rememberMatrices(lua_State* L);

/// PartCustomization normal matrix (recalculate()'s normal part, or the inverse transpose after setMatrix).
void partNormalMatrix(Part const& p, double* m);

// ---------------------------------------------------------------- Lua-owned objects
/// Allocate a T inside a Lua full userdata (destroyed by __gc at lua_close / when unreachable) and push the cached
/// reference object for it (type `type`). The reference keeps the storage alive. Leaves the reference on the stack.
void* newOwnedRaw(lua_State* L, size_t size, void (*dtor)(void*), uint32_t type);
template <class T, class... A> T* newOwned(lua_State* L, uint32_t type, A&&... a) {
    void* mem = newOwnedRaw(L, sizeof(T), [](void* p) { static_cast<T*>(p)->~T(); }, type);
    return new (mem) T(std::forward<A>(a)...);
}

// ---------------------------------------------------------------- FiguraTexture (in-memory pixels, NativeImage ABGR)
struct Tex {
    std::string           name, location;
    int                   w = 0, h = 0;
    bool                  sized = false;   // w/h known
    bool                  decoded = false; // px valid
    std::vector<uint32_t> px;              // NativeImage RGBA format: int = ABGR (r in the low byte)
    std::vector<uint32_t> backup;
    bool                  hasBackup = false, modified = false, dirty = true;
    std::filesystem::path file;            // avatar texture file (lazy decode)
    std::string           bytes;           // encoded PNG (lazy decode)
    int                   fileIndex = -1;  // Avatar::textureFiles index (rendered by Bedrock), -1 = script texture
    bool                  avatarTexture = false;
    bool                  uploaded = false;  // pushed to Bedrock at least once (texSync)
};
extern uint32_t tTexture;
/// Ensure dimensions are known (reads the PNG header lazily).
void texSize(Tex& t);
/// Ensure pixels are decoded.
void texDecode(lua_State* L, Tex& t);
/// Avatar texture by Figura name (renderer.textures), custom first like AvatarRenderer.getTexture
Tex* texByName(lua_State* L, std::string const& name, bool customOnly, bool avatarOnly);

/// FiguraTextureSet as loaded from the avatar NBT ("textures"."data"): name + main/emissive/specular/normal
struct TexSet {
    std::string name;
    Tex*        t[4] = {nullptr, nullptr, nullptr, nullptr};
    int         width() const;
    int         height() const;
};
std::vector<TexSet>& texSets(lua_State* L);

void openTextureApi(Avatar& a);
/// push changed textures to Bedrock (texture:update()): edited avatar textures over their own pack path, script
/// textures shown with setPrimaryTexture("CUSTOM") over their runtime slot. Called once per frame.
void texSync(Avatar& a);
void openRenderTaskApi(Avatar& a);
void ensureGeometry(lua_State* L, Part* p); // fill vertices / textures / texture size from the NBT (lazy)
int  pushVertexList(lua_State* L, std::deque<Vertex>& list);

// ItemStack / BlockState arguments (LuaUtils.parseItemStack / parseBlockState): keep the Lua value (registry ref)
int  refItemArg(lua_State* L, int idx, char const* fn, char const* typeName); // 0 = nil/EMPTY

// base64 + png (LuaTex.cpp)
std::string b64Encode(std::string const& in);
bool        b64Decode(std::string const& in, std::string& out, std::string* err);
bool        pngDecode(std::string const& data, int& w, int& h, std::vector<uint32_t>& abgr, std::string* err);
std::string pngEncode(int w, int h, std::vector<uint32_t> const& abgr);

} // namespace bf::figura
