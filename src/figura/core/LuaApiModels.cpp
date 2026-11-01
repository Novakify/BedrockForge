// FiguraBridge Lua API: ModelPart (FiguraModelPart's Lua methods, Figura 0.1.6) and Vertex. Render tasks live in
// LuaTask.cpp, textures in LuaTex.cpp. Geometry info (vertices, texture sets, texture size) is read lazily from the
// avatar NBT exactly like FiguraModelPartReader.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "figura/core/Avatar.h"
#include "figura/core/LuaApi.h"
#include "figura/core/LuaBind.h"
#include "figura/core/LuaTex.h"
#include "lyrics/core/Json.h"

namespace bf::figura {

using namespace lua;
namespace json = lyrics::json;

uint32_t tModelPart = 0, tVertex = 0;

// LuaTask.cpp
int partNewText(lua_State* L);
int partNewItem(lua_State* L);
int partNewBlock(lua_State* L);
int partNewSprite(lua_State* L);
int partNewEntity(lua_State* L);
int partAddTask(lua_State* L);
int partRemoveTask(lua_State* L);

// =====================================================================================================================
// geometry (FiguraModelPartReader)
// =====================================================================================================================
namespace {
Vec3 jvec3(json::Value const& n, char const* k, Vec3 def = {}) {
    json::Value const& v = n[k];
    if (!v.isArray() || v.size() < 3) return def;
    return {fl(v[size_t(0)].asNumber()), fl(v[size_t(1)].asNumber()), fl(v[size_t(2)].asNumber())};
}
struct FaceDef {
    char const* dir;
    Vec3        c[4];
    Vec3        n;
};
// ImmutableMap.of order: n, s, e, w, u, d
FaceDef const kFaces[6] = {
    {"n", {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}, {0, 0, -1}},
    {"s", {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}, {0, 0, 1}},
    {"e", {{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}, {1, 0, 0}},
    {"w", {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}, {-1, 0, 0}},
    {"u", {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}, {0, 1, 0}},
    {"d", {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}, {0, -1, 0}},
};
double const kUv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};

void smoothfy(std::map<int, std::deque<Vertex>>& byTex) {
    std::map<std::string, std::vector<Vertex*>> byPos;
    for (auto& [k, list] : byTex)
        for (Vertex& v : list) {
            char key[sizeof(float) * 3];
            std::memcpy(key, &v.x, 4);
            std::memcpy(key + 4, &v.y, 4);
            std::memcpy(key + 8, &v.z, 4);
            byPos[std::string(key, sizeof key)].push_back(&v);
        }
    for (auto& [k, list] : byPos) {
        double x = 0, y = 0, z = 0;
        for (Vertex* v : list) x += v->nx, y += v->ny, z += v->nz;
        double l = std::sqrt(x * x + y * y + z * z);
        if (l > 0) x /= l, y /= l, z /= l;
        for (Vertex* v : list) v->nx = float(x), v->ny = float(y), v->nz = float(z);
    }
}

struct Geo {
    int w = -1, h = -1;
};

Geo buildNode(Avatar& a, std::vector<TexSet>& sets, json::Value const& node, int& idx, bool smooth) {
    Part* p = idx < int(a.parts.size()) ? a.parts[size_t(idx)].get() : nullptr;
    idx++;
    int                               nSets = int(sets.size());
    std::vector<int>                  faces(size_t(nSets), 0);
    std::map<int, std::deque<Vertex>> verts;
    bool                              isCube = node["cube_data"].isObject() && !node["cube_data"].members().empty();
    bool                              isMesh = !isCube && node["mesh_data"].isObject() && !node["mesh_data"].members().empty();
    if (isCube) {
        Vec3   from = jvec3(node, "f"), to = jvec3(node, "t");
        double inf  = node.has("inf") ? fl(node["inf"].asNumber()) : 0;
        from        = from - Vec3{inf, inf, inf};
        to          = to + Vec3{inf, inf, inf};
        Vec3 diff   = to - from;
        auto const& cd = node["cube_data"];
        for (auto const& f : kFaces) {
            if (!cd.has(f.dir)) continue;
            auto const& face = cd[f.dir];
            int         tex  = int(int16_t(face["tex"].asInt(0)));
            if (tex >= 0 && tex < nSets) faces[size_t(tex)]++;
            int    rotation = int(float(face["rot"].asNumber(0)) / 90.0f);
            double uv[4]    = {0, 0, 0, 0};
            if (face["uv"].isArray())
                for (size_t i = 0; i < 4 && i < face["uv"].size(); i++) uv[i] = fl(face["uv"][i].asNumber());
            for (int i = 0; i < 4; i++) {
                Vec3 pos = diff.mul(f.c[i]) + from;
                double const* nu = kUv[((i + rotation) % 4 + 4) % 4];
                Vertex v;
                v.x = float(pos.x), v.y = float(pos.y), v.z = float(pos.z);
                v.u = float(uv[0] + nu[0] * (uv[2] - uv[0]));
                v.v = float(uv[1] + nu[1] * (uv[3] - uv[1]));
                v.nx = float(f.n.x), v.ny = float(f.n.y), v.nz = float(f.n.z);
                verts[tex].push_back(v);
            }
        }
    } else if (isMesh) {
        auto const& md   = node["mesh_data"];
        auto const& vtx  = md["vtx"];
        auto const& uvs  = md["uvs"];
        auto const& texl = md["tex"];
        auto const& fac  = md["fac"];
        size_t      nv   = vtx.size();
        int         best = nv > 255 * 3 ? (nv > 32767 * 3 ? 2 : 1) : 0;
        size_t      vi = 0, uvi = 0;
        for (size_t ti = 0; ti < texl.size(); ti++) {
            int packed   = int(int16_t(texl[ti].asInt()));
            int texId    = packed >> 4;
            int numVerts = packed & 0xf;
            if (texId >= 0 && texId < nSets) faces[size_t(texId)]++;
            float posArr[12] = {}, uvArr[8] = {};
            for (int j = 0; j < numVerts && j < 4; j++) {
                int64_t raw = fac[vi + size_t(j)].asInt();
                size_t  vid = size_t(best == 0 ? (raw & 0xff) : best == 1 ? (raw & 0xffff) : raw);
                for (int k = 0; k < 3; k++) posArr[3 * j + k] = float(vtx[3 * vid + size_t(k)].asNumber());
                uvArr[2 * j]     = float(uvs[uvi + size_t(2 * j)].asNumber());
                uvArr[2 * j + 1] = float(uvs[uvi + size_t(2 * j + 1)].asNumber());
            }
            Vec3 p1{posArr[0], posArr[1], posArr[2]}, p2{posArr[3], posArr[4], posArr[5]}, p3{posArr[6], posArr[7], posArr[8]};
            p3 = p3 - p2;
            p1 = p1 - p2;
            Vec3 c{p3.y * p1.z - p3.z * p1.y, p3.z * p1.x - p3.x * p1.z, p3.x * p1.y - p3.y * p1.x};
            double l = c.length();
            if (l > 0) c = c * (1.0 / l);
            for (int j = 0; j < numVerts && j < 4; j++)
                verts[texId].push_back({posArr[3 * j], posArr[3 * j + 1], posArr[3 * j + 2], uvArr[2 * j], uvArr[2 * j + 1],
                                        float(c.x), float(c.y), float(c.z)});
            if (numVerts == 3)
                verts[texId].push_back({posArr[6], posArr[7], posArr[8], uvArr[4], uvArr[5], float(c.x), float(c.y), float(c.z)});
            vi += size_t(numVerts);
            uvi += size_t(2 * numVerts);
        }
    }
    if (node.has("smo")) smooth = node["smo"].asBool(false);
    if (smooth && !verts.empty()) smoothfy(verts);

    std::vector<Geo> kids;
    for (auto const& c : node["chld"].items()) kids.push_back(buildNode(a, sets, c, idx, smooth));

    // storeTextures
    Geo  g;
    int  w = -1, h = -1;
    bool multi = false;
    for (Geo const& k : kids) {
        if (k.w == -1) { multi = true; break; }
        if (k.w != w || k.h != h) {
            if (w != -1) { multi = true; break; }
            w = k.w;
            h = k.h;
        }
    }
    if (!multi && (isCube || isMesh)) {
        int i = -1;
        for (int j = 0; j < nSets && !multi; j++) {
            if (faces[size_t(j)] > 0) {
                int rw = sets[size_t(j)].width(), rh = sets[size_t(j)].height();
                if ((w != -1 && w != rw) || (h != -1 && h != rh) || i != -1) { multi = true; break; }
                i = j;
                w = rw;
                h = rh;
            }
        }
    }
    if (!multi) g = {w, h};
    if (p && !p->geomInit) {
        p->geomInit = true;
        p->vertices = std::move(verts);
        p->textureSets.clear();
        for (int j = 0; j < nSets; j++) p->textureSets.push_back(j);
        p->textureWidth  = g.w;
        p->textureHeight = g.h;
    }
    return g;
}
} // namespace

void ensureGeometry(lua_State* L, Part* p) {
    if (p->geomInit) return;
    Avatar* a = avatarOf(L);
    if (a->nbtJson && (*a->nbtJson).has("models")) {
        int idx = 0;
        buildNode(*a, texSets(L), (*a->nbtJson)["models"], idx, false);
    }
    for (auto& q : a->parts) q->geomInit = true; // anything left (no NBT node) = no geometry
}

int pushVertexList(lua_State* L, std::deque<Vertex>& list) {
    lua_createtable(L, int(list.size()), 0);
    int i = 1;
    for (Vertex& v : list) {
        pushRef(L, &v, tVertex);
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

// =====================================================================================================================
// Vertex
// =====================================================================================================================
namespace {
Vertex* vself(lua_State* L, char const* fn) { return check<Vertex>(L, 1, tVertex, fn); }
int     vret(lua_State* L) { lua_settop(L, 1); return 1; }
int vGetPos(lua_State* L) { Vertex* v = vself(L, "getPos"); pushVec3(L, {v->x, v->y, v->z}); return 1; }
int vSetPos(lua_State* L) {
    Vertex* v = vself(L, "setPos");
    Vec3    p = luParseVec3(L, 2, "setPos");
    v->x = float(p.x), v->y = float(p.y), v->z = float(p.z);
    return vret(L);
}
int vGetUV(lua_State* L) { Vertex* v = vself(L, "getUV"); pushVec2(L, v->u, v->v); return 1; }
int vSetUV(lua_State* L) {
    Vertex* v = vself(L, "setUV");
    V2      p = luParseVec2(L, 2, "setUV");
    v->u = float(p.x), v->v = float(p.y);
    return vret(L);
}
int vGetNormal(lua_State* L) { Vertex* v = vself(L, "getNormal"); pushVec3(L, {v->nx, v->ny, v->nz}); return 1; }
int vSetNormal(lua_State* L) {
    Vertex* v = vself(L, "setNormal");
    Vec3    p = luParseVec3(L, 2, "setNormal");
    v->nx = float(p.x), v->ny = float(p.y), v->nz = float(p.z);
    return vret(L);
}
int vToString(lua_State* L) { (void)vself(L, "__tostring"); lua_pushstring(L, "Vertex"); return 1; }
} // namespace

// =====================================================================================================================
// ModelPart
// =====================================================================================================================
namespace {

Part* self(lua_State* L, char const* fn) { return check<Part>(L, 1, tModelPart, fn); }
int   ret(lua_State* L) { lua_settop(L, 1); return 1; }

// ---- transforms (PartCustomization setters set needsMatrixRecalculation: a setMatrix() matrix is recomputed)
#define VEC_SETTER(fnName, field)                                        \
    int fnName(lua_State* L) {                                           \
        Part* p = self(L, #fnName);                                      \
        p->field = luParseVec3(L, 2, #fnName);                           \
        p->customMatrix = false;                                         \
        return ret(L);                                                   \
    }
#define ONE_SETTER(fnName, field)                                        \
    int fnName(lua_State* L) {                                           \
        Part* p = self(L, #fnName);                                      \
        p->field = luParseOneArgVec(L, 2, #fnName, 1);                   \
        p->customMatrix = false;                                         \
        return ret(L);                                                   \
    }
#define VEC_GETTER(fnName, expr)                                         \
    int fnName(lua_State* L) {                                           \
        Part* p = self(L, #fnName);                                      \
        pushVec3(L, expr);                                               \
        return 1;                                                        \
    }
VEC_SETTER(setPos, position)
VEC_SETTER(setRot, rotation)
VEC_SETTER(setOffsetRot, offsetRot)
ONE_SETTER(setScale, scale)
ONE_SETTER(setOffsetScale, offsetScale)
VEC_SETTER(setPivot, pivot)
VEC_SETTER(setOffsetPivot, offsetPivot)
VEC_GETTER(getPos, p->position)
VEC_GETTER(getRot, p->rotation)
VEC_GETTER(getOffsetRot, p->offsetRot)
VEC_GETTER(getScale, p->scale)
VEC_GETTER(getOffsetScale, p->offsetScale)
VEC_GETTER(getPivot, p->pivot)
VEC_GETTER(getOffsetPivot, p->offsetPivot)
VEC_GETTER(getAnimPos, p->animPos)
VEC_GETTER(getAnimRot, p->animRot)
VEC_GETTER(getAnimScale, p->animScale)
VEC_GETTER(getTruePos, p->position + p->animPos)
VEC_GETTER(getTrueRot, p->rotation + p->offsetRot + p->animRot)
VEC_GETTER(getTrueScale, p->scale.mul(p->offsetScale).mul(p->animScale))
VEC_GETTER(getTruePivot, p->pivot + p->offsetPivot)
VEC_GETTER(getColor, (p->color + p->color2) * 0.5)
VEC_GETTER(getPrimaryColor, p->color)
VEC_GETTER(getSecondaryColor, p->color2)
#undef VEC_SETTER
#undef ONE_SETTER
#undef VEC_GETTER

int getPositionMatrix(lua_State* L) {
    Part* p = self(L, "getPositionMatrix");
    p->recalculate();
    pushMat4(L, p->positionMatrix);
    return 1;
}
int getPositionMatrixRaw(lua_State* L) { pushMat4(L, self(L, "getPositionMatrixRaw")->positionMatrix); return 1; }
int getNormalMatrix(lua_State* L) {
    Part* p = self(L, "getNormalMatrix");
    p->recalculate();
    double m[9];
    partNormalMatrix(*p, m);
    pushMat3(L, m);
    return 1;
}
int getNormalMatrixRaw(lua_State* L) {
    Part*  p = self(L, "getNormalMatrixRaw");
    double m[9];
    partNormalMatrix(*p, m);
    pushMat3(L, m);
    return 1;
}
int setMatrix(lua_State* L) {
    Part* p = self(L, "setMatrix");
    Mat4* m = toMat4(L, 2);
    if (!m) return luaL_error(L, "bad argument 2 to 'setMatrix' (Matrix4 expected, got %s)", figuraTypeName(L, 2).c_str());
    p->positionMatrix = *m;
    p->customMatrix   = true;
    return ret(L);
}
int partToWorldMatrix(lua_State* L) {
    Part* p = self(L, "partToWorldMatrix");
    Mat4 m;
    std::vector<Part*> chain;
    for (Part* q = p; q; q = q->parent) chain.push_back(q);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        (*it)->recalculate();
        m = m * (*it)->positionMatrix;
    }
    Avatar* a = avatarOf(L);
    Mat4 world;
    world.scale(1.0 / 16, 1.0 / 16, 1.0 / 16);
    world.rotateY(180 - a->player.bodyYaw);
    world.translate(a->player.pos.x, a->player.pos.y, a->player.pos.z);
    pushMat4(L, world * m);
    return 1;
}

// ---- visibility / render
int getVisible(lua_State* L) {
    Part* p = self(L, "getVisible");
    Part* q = p;
    while (q && q->visible) q = q->parent;
    lua_pushboolean(L, q == nullptr || q->visible);
    return 1;
}
int setVisible(lua_State* L) {
    Part* p    = self(L, "setVisible");
    p->visible = lua_toboolean(L, 2) != 0;
    return ret(L);
}
int getName(lua_State* L) { lua_pushstring(L, self(L, "getName")->name.c_str()); return 1; }
int getParent(lua_State* L) { pushRef(L, self(L, "getParent")->parent, tModelPart); return 1; }
int getChildren(lua_State* L) {
    Part* p = self(L, "getChildren");
    lua_newtable(L);
    int i = 1;
    for (Part* c : p->children) {
        pushRef(L, c, tModelPart);
        lua_rawseti(L, -2, i++);
    }
    return 1;
}
int isChildOf(lua_State* L) {
    Part* p = self(L, "isChildOf");
    Part* o = to<Part>(L, 2, tModelPart);
    if (!o && !lua_isnoneornil(L, 2)) return luaL_error(L, "bad argument 2 to 'isChildOf' (ModelPart expected, got %s)", figuraTypeName(L, 2).c_str());
    lua_pushboolean(L, o && p->isChildOf(o));
    return 1;
}
int getType(lua_State* L) {
    Part* p = self(L, "getType");
    lua_pushstring(L, p->type == PartType::Cube ? "CUBE" : p->type == PartType::Mesh ? "MESH" : "GROUP");
    return 1;
}
int getParentType(lua_State* L) { lua_pushstring(L, parentTypeName(self(L, "getParentType")->parentType)); return 1; }
int setParentType(lua_State* L) {
    Part* p = self(L, "setParentType");
    p->parentType = lua_isnoneornil(L, 2) ? ParentType::None : parentTypeGet(luaL_checkstring(L, 2));
    p->vanillaVisible.reset();
    p->customMatrix = false;
    return ret(L);
}
char const* const kRenderTypes[] = {"NONE", "CUTOUT", "CUTOUT_CULL", "CUTOUT_EMISSIVE_SOLID", "TRANSLUCENT",
                                    "TRANSLUCENT_CULL", "EMISSIVE", "EMISSIVE_SOLID", "EYES", "END_PORTAL",
                                    "END_GATEWAY", "TEXTURED_PORTAL", "GLINT", "TEXTURED_GLINT", "LINES",
                                    "LINES_STRIP", "SOLID", "BLURRY"};
std::string upper(std::string s) {
    for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
int setRenderTypeOf(lua_State* L, std::string Part::*field, char const* fn) {
    Part* p = self(L, fn);
    if (lua_isnoneornil(L, 2)) {
        p->*field = "";
        return ret(L);
    }
    std::string s = lua_isstring(L, 2) ? lua_tostring(L, 2) : figuraTypeName(L, 2);
    std::string u = upper(s);
    for (char const* r : kRenderTypes)
        if (u == r) {
            p->*field = u;
            return ret(L);
        }
    return luaL_error(L, "Illegal RenderType: \"%s\".", s.c_str());
}
int setPrimaryRenderType(lua_State* L) { return setRenderTypeOf(L, &Part::primaryRenderType, "setPrimaryRenderType"); }
int setSecondaryRenderType(lua_State* L) { return setRenderTypeOf(L, &Part::secondaryRenderType, "setSecondaryRenderType"); }
int getPrimaryRenderType(lua_State* L) {
    auto& s = self(L, "getPrimaryRenderType")->primaryRenderType;
    if (s.empty()) lua_pushnil(L); else lua_pushstring(L, s.c_str());
    return 1;
}
int getSecondaryRenderType(lua_State* L) {
    auto& s = self(L, "getSecondaryRenderType")->secondaryRenderType;
    if (s.empty()) lua_pushnil(L); else lua_pushstring(L, s.c_str());
    return 1;
}
int setColor(lua_State* L) {
    Part* p = self(L, "setColor");
    Vec3 c  = luParseOneArgVec(L, 2, "setColor", 1);
    p->color = p->color2 = c;
    return ret(L);
}
int setPrimaryColor(lua_State* L) { self(L, "setPrimaryColor")->color = luParseOneArgVec(L, 2, "setPrimaryColor", 1); return ret(L); }
int setSecondaryColor(lua_State* L) { self(L, "setSecondaryColor")->color2 = luParseOneArgVec(L, 2, "setSecondaryColor", 1); return ret(L); }
int setOpacity(lua_State* L) {
    Part* p = self(L, "setOpacity");
    if (lua_isnoneornil(L, 2)) p->alpha.reset();
    else p->alpha = float(luaL_checknumber(L, 2));
    return ret(L);
}
int getOpacity(lua_State* L) {
    Part* p = self(L, "getOpacity");
    if (p->alpha) lua_pushnumber(L, *p->alpha); else lua_pushnil(L);
    return 1;
}
int setLight(lua_State* L) {
    Part* p = self(L, "setLight");
    if (lua_isnoneornil(L, 2)) {
        p->light.reset();
        return ret(L);
    }
    V2 v     = luParseVec2(L, 2, "setLight");
    p->light = lightPack(javaInt(v.x), javaInt(v.y));
    return ret(L);
}
int getLight(lua_State* L) {
    Part* p = self(L, "getLight");
    if (!p->light) { lua_pushnil(L); return 1; }
    pushVec2(L, lightBlock(*p->light), lightSky(*p->light));
    return 1;
}
int setOverlay(lua_State* L) {
    Part* p = self(L, "setOverlay");
    if (lua_isnoneornil(L, 2)) {
        p->overlay.reset();
        return ret(L);
    }
    V2 v       = luParseVec2(L, 2, "setOverlay");
    p->overlay = overlayPack(javaInt(v.x), javaInt(v.y));
    return ret(L);
}
int getOverlay(lua_State* L) {
    Part* p = self(L, "getOverlay");
    if (!p->overlay) { lua_pushnil(L); return 1; }
    pushVec2(L, *p->overlay & 0xFFFF, *p->overlay >> 16);
    return 1;
}
int overrideVanillaRot(lua_State* L) { lua_pushboolean(L, self(L, "overrideVanillaRot")->overrideVanillaRot()); return 1; }
int overrideVanillaPos(lua_State* L) { lua_pushboolean(L, self(L, "overrideVanillaPos")->overrideVanillaPos()); return 1; }
int overrideVanillaScale(lua_State* L) { lua_pushboolean(L, self(L, "overrideVanillaScale")->overrideVanillaScale()); return 1; }

// ---- textures (FiguraTextureSet.OverrideType)
char const* const kOverride[] = {"SKIN", "CAPE", "ELYTRA", "RESOURCE", "PRIMARY", "SECONDARY", "SPECULAR", "NORMAL", "CUSTOM"};
TexKind const     kOverrideKind[] = {TexKind::Skin, TexKind::Cape, TexKind::Elytra, TexKind::Resource, TexKind::Primary,
                                     TexKind::Secondary, TexKind::Specular, TexKind::Normal, TexKind::Custom};
char const* kindName(TexKind k) {
    for (int i = 0; i < 9; i++)
        if (kOverrideKind[i] == k) return kOverride[i];
    return "PRIMARY";
}
int setTex(lua_State* L, bool primary, char const* fn) {
    Part* p = self(L, fn);
    TextureOverride& t = primary ? p->primaryTexture : p->secondaryTexture;
    bool        typeNil = lua_isnoneornil(L, 2);
    std::string type    = typeNil ? "" : (lua_isstring(L, 2) ? lua_tostring(L, 2) : figuraTypeName(L, 2));
    TexKind     kind    = primary ? TexKind::Primary : TexKind::Secondary;
    if (!typeNil) {
        std::string u = upper(type);
        int         i = 0;
        for (; i < 9; i++)
            if (u == kOverride[i]) break;
        if (i == 9) return luaL_error(L, "Invalid texture override type: %s", type.c_str());
        kind = kOverrideKind[i];
    }
    bool hasArg = !lua_isnoneornil(L, 3);
    if (kind == TexKind::Custom) {
        if (!hasArg || !toRef(L, 3, tTexture)) return luaL_error(L, "\"CUSTOM\" texture type requires argument type: Texture");
    } else if (kind == TexKind::Resource) {
        if (!hasArg || lua_type(L, 3) != LUA_TSTRING) return luaL_error(L, "\"RESOURCE\" texture type requires argument type: String");
    } else if (hasArg) {
        return luaL_error(L, "\"%s\n texture type requires no arguments!", kindName(kind));
    }
    if (typeNil) {
        t = {};
        return ret(L);
    }
    t      = {};
    t.set  = true;
    t.kind = kind;
    if (kind == TexKind::Custom) {
        Tex* x = static_cast<Tex*>(toRef(L, 3, tTexture));
        t.tex  = x;
        t.file = x->fileIndex;
    } else if (kind == TexKind::Resource) {
        t.resource = lua_tostring(L, 3);
    }
    return ret(L);
}
int setPrimaryTexture(lua_State* L) { return setTex(L, true, "setPrimaryTexture"); }
int setSecondaryTexture(lua_State* L) { return setTex(L, false, "setSecondaryTexture"); }
int pushTexOverride(lua_State* L, TextureOverride const& t) {
    if (!t.set) {
        lua_pushstring(L, "PRIMARY");
        lua_pushnil(L);
        return 2;
    }
    lua_pushstring(L, kindName(t.kind));
    if (t.kind == TexKind::Custom) pushRef(L, t.tex, tTexture);
    else if (t.kind == TexKind::Resource) lua_pushstring(L, t.resource.c_str());
    else lua_pushnil(L);
    return 2;
}
int getPrimaryTexture(lua_State* L) { return pushTexOverride(L, self(L, "getPrimaryTexture")->primaryTexture); }
int getSecondaryTexture(lua_State* L) { return pushTexOverride(L, self(L, "getSecondaryTexture")->secondaryTexture); }
int getDefinedTextures(lua_State* L, bool primary, char const* fn) {
    Part* p = self(L, fn);
    ensureGeometry(L, p);
    if (primary && lua_isnoneornil(L, 2))
        return luaL_error(L, "Illegal argument nil to getPrimaryDefinedTextures, requires an int");
    int  value = lua_isnoneornil(L, 2) ? -1 : javaInt(luaL_checknumber(L, 2));
    auto& sets = texSets(L);
    TextureOverride const& o = primary ? p->primaryTexture : p->secondaryTexture;
    auto setAt = [&](int i) -> TexSet* {
        if (i < 0 || i >= int(p->textureSets.size())) {
            luaL_error(L, "Index %d out of bounds for length %d", i, int(p->textureSets.size()));
            return nullptr;
        }
        int s = p->textureSets[size_t(i)];
        return s >= 0 && s < int(sets.size()) ? &sets[size_t(s)] : nullptr;
    };
    lua_newtable(L);
    if (!o.set) {
        if (value < 0) return luaL_error(L, "Cannot invoke \"java.lang.Integer.intValue()\" because \"value\" is null");
        TexSet* s = setAt(value);
        static char const* keys[4] = {"main", "emissive", "specular", "normal"};
        for (int i = 0; i < 4; i++)
            if (s && s->t[i]) {
                pushRef(L, s->t[i], tTexture);
                lua_setfield(L, -2, keys[i]);
            }
        return 1;
    }
    TexSet* s   = setAt(value < 0 ? 0 : value);
    Tex*    tex = nullptr;
    switch (o.kind) {
    case TexKind::Custom: tex = static_cast<Tex*>(o.tex); break;
    case TexKind::Primary: tex = s ? s->t[0] : nullptr; break;
    case TexKind::Secondary: tex = s ? s->t[1] : nullptr; break;
    case TexKind::Specular: tex = s ? s->t[2] : nullptr; break;
    case TexKind::Normal: tex = s ? s->t[3] : nullptr; break;
    default: break; // SKIN/CAPE/ELYTRA/RESOURCE are not FiguraTextures
    }
    if (tex) {
        pushRef(L, tex, tTexture);
        lua_setfield(L, -2, "main");
    }
    return 1;
}
int getPrimaryDefinedTextures(lua_State* L) { return getDefinedTextures(L, true, "getPrimaryDefinedTextures"); }
int getSecondaryDefinedTextures(lua_State* L) { return getDefinedTextures(L, false, "getSecondaryDefinedTextures"); }
int getTextures(lua_State* L) {
    Part* p = self(L, "getTextures");
    ensureGeometry(L, p);
    auto& sets = texSets(L);
    lua_newtable(L);
    int i = 1;
    for (int s : p->textureSets)
        if (s >= 0 && s < int(sets.size()))
            for (Tex* t : sets[size_t(s)].t)
                if (t) {
                    pushRef(L, t, tTexture);
                    lua_rawseti(L, -2, i++);
                }
    return 1;
}
int getTextureSize(lua_State* L) {
    Part* p = self(L, "getTextureSize");
    ensureGeometry(L, p);
    if (p->textureWidth == -1 || p->textureHeight == -1) {
        if (p->type == PartType::Group) return luaL_error(L, "Cannot get the texture size of groups!");
        return luaL_error(L, "Cannot get texture size of part, it has multiple different-sized textures!");
    }
    pushVec2(L, p->textureWidth, p->textureHeight);
    return 1;
}
void uvReset(Part* p) {
    double id[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::memcpy(p->uvMatrix, id, sizeof id);
}
void uvTranslate(Part* p, double x, double y) { // FiguraMat3.translate(x, y): left-multiply by T
    double* m = p->uvMatrix;
    for (int c = 0; c < 3; c++) {
        m[c] += x * m[6 + c];
        m[3 + c] += y * m[6 + c];
    }
    p->uvOffset = {m[2], m[5], 0};
}
int setUV(lua_State* L) {
    Part* p = self(L, "setUV");
    uvReset(p);
    V2 uv = luParseVec2(L, 2, "setUV");
    uvTranslate(p, std::fmod(uv.x, 1.0), std::fmod(uv.y, 1.0));
    return ret(L);
}
int getUV(lua_State* L) { Part* p = self(L, "getUV"); pushVec2(L, p->uvMatrix[2], p->uvMatrix[5]); return 1; }
int setUVPixelsOn(lua_State* L, Part* p) {
    ensureGeometry(L, p);
    if (p->textureWidth == -1 || p->textureHeight == -1) {
        if (p->type == PartType::Group) {
            for (Part* c : std::vector<Part*>(p->children)) setUVPixelsOn(L, c);
            return 0;
        }
        return luaL_error(L, "Cannot call setUVPixels on parts with multiple texture sizes!");
    }
    uvReset(p);
    V2 uv = luParseVec2(L, 2, "setUVPixels");
    uvTranslate(p, uv.x / p->textureWidth, uv.y / p->textureHeight);
    return 0;
}
int setUVPixels(lua_State* L) {
    Part* p = self(L, "setUVPixels");
    setUVPixelsOn(L, p);
    return ret(L);
}
int getUVPixels(lua_State* L) {
    Part* p = self(L, "getUVPixels");
    ensureGeometry(L, p);
    if (p->textureWidth == -1 || p->textureHeight == -1) {
        if (p->type == PartType::Group) return luaL_error(L, "Cannot call getUVPixels on groups!");
        return luaL_error(L, "Cannot call getUVPixels on parts with multiple texture sizes!");
    }
    pushVec2(L, p->uvMatrix[2] * p->textureWidth, p->uvMatrix[5] * p->textureHeight);
    return 1;
}
int setUVMatrix(lua_State* L) {
    Part*  p = self(L, "setUVMatrix");
    double m[9];
    if (!readMat3(L, 2, m)) return luaL_error(L, "bad argument 2 to 'setUVMatrix' (Matrix3 expected, got %s)", figuraTypeName(L, 2).c_str());
    std::memcpy(p->uvMatrix, m, sizeof m);
    p->uvOffset = {m[2], m[5], 0};
    return ret(L);
}
int getUVMatrix(lua_State* L) { pushMat3(L, self(L, "getUVMatrix")->uvMatrix); return 1; }

// ---- vertices
int getVertices(lua_State* L) {
    Part* p = self(L, "getVertices");
    ensureGeometry(L, p);
    std::string id   = luaL_checkstring(L, 2);
    auto&       sets = texSets(L);
    int         index = -1;
    for (size_t i = 0; i < p->textureSets.size(); i++) {
        int s = p->textureSets[i];
        if (s >= 0 && s < int(sets.size()) && sets[size_t(s)].name == id) {
            index = int(i);
            break;
        }
    }
    auto it = p->vertices.find(index);
    if (it == p->vertices.end()) {
        lua_pushnil(L);
        return 1;
    }
    return pushVertexList(L, it->second);
}
int getAllVertices(lua_State* L) {
    Part* p = self(L, "getAllVertices");
    ensureGeometry(L, p);
    auto& sets = texSets(L);
    lua_newtable(L);
    for (size_t i = 0; i < p->textureSets.size(); i++) {
        auto it = p->vertices.find(int(i));
        if (it == p->vertices.end()) continue;
        int s = p->textureSets[i];
        pushVertexList(L, it->second);
        lua_setfield(L, -2, s >= 0 && s < int(sets.size()) ? sets[size_t(s)].name.c_str() : "");
    }
    return 1;
}

// ---- tree editing
void eraseFirst(std::vector<Part*>& v, Part* p) {
    auto it = std::find(v.begin(), v.end(), p);
    if (it != v.end()) v.erase(it);
}
int removeChildOf(Part* parent, Part* c) {
    auto& v = parent->children;
    if (std::find(v.begin(), v.end(), c) != v.end()) {
        eraseFirst(v, c);
        c->parent = nullptr;
    }
    return 0;
}
int removeP(lua_State* L) {
    Part* p = self(L, "remove");
    if (p->parent) removeChildOf(p->parent, p);
    return ret(L);
}
int addChildTo(lua_State* L, Part* p, Part* c) {
    if (c == p) return luaL_error(L, "Fractal, cannot parent part to itself");
    for (Part* q = p->parent; q; q = q->parent)
        if (q == c) return luaL_error(L, "Cannot add child that's already parent of this part");
    // Figura: the part is NOT removed from its old parent's list (it can be in several), only re-pointed
    p->children.push_back(c);
    c->parent = p;
    return 0;
}
int addChild(lua_State* L) {
    Part* p = self(L, "addChild");
    Part* c = check<Part>(L, 2, tModelPart, "addChild");
    addChildTo(L, p, c);
    return ret(L);
}
int removeChild(lua_State* L) {
    Part* p = self(L, "removeChild");
    Part* c = check<Part>(L, 2, tModelPart, "removeChild");
    removeChildOf(p, c);
    return ret(L);
}
int moveTo(lua_State* L) {
    Part* p = self(L, "moveTo");
    Part* n = check<Part>(L, 2, tModelPart, "moveTo");
    if (n == p) return luaL_error(L, "Fractal, cannot parent part to itself");
    if (p->parent) eraseFirst(p->parent->children, p);
    n->children.push_back(p);
    p->parent = n;
    return ret(L);
}
int newPart(lua_State* L) {
    // Bedrock can't add bones at runtime: the part exists for scripts (transforms, children moves) but has no geometry
    Part*   p = self(L, "newPart");
    Avatar* a = avatarOf(L);
    ensureGeometry(L, p);
    std::string name = lua_isnoneornil(L, 2) ? "null" : luaL_checkstring(L, 2);
    Part*   c = a->addPart(name, nullptr);
    c->geomInit = true;
    a->out.resize(a->parts.size());
    a->compiledPivots.push_back({});
    addChildTo(L, p, c);
    c->pivot = p->pivot; // newer.setPivot(this.getPivot())
    if (!lua_isnoneornil(L, 3)) c->parentType = parentTypeGet(luaL_checkstring(L, 3));
    pushRef(L, c, tModelPart);
    return 1;
}
int copyP(lua_State* L) {
    Part*   p = self(L, "copy");
    Avatar* a = avatarOf(L);
    ensureGeometry(L, p);
    std::string name = lua_isnoneornil(L, 2) ? p->name : std::string(luaL_checkstring(L, 2));
    Part*   c = a->addPart(name, nullptr);
    int idx = c->index;
    *c        = Part(*p);  // customization, vertices (deep copy), textures, parentType, texture size
    c->name   = name;
    c->index  = idx;
    c->parent = nullptr;   // Figura: children list is shared (new ArrayList<>(children)), parent not set
    c->preRender = c->midRender = c->postRender = 0;
    c->tasks.clear();
    c->animated              = false;
    c->animationOverride     = 0;
    c->lastAnimationPriority = INT32_MIN;
    c->hasPlayerCustomization = false;
    a->out.resize(a->parts.size());
    a->compiledPivots.push_back(p->index >= 0 && p->index < int(a->compiledPivots.size()) ? a->compiledPivots[size_t(p->index)] : p->pivot);
    pushRef(L, c, tModelPart);
    return 1;
}

// ---- render tasks
int getTask(lua_State* L) {
    Part* p = self(L, "getTask");
    if (lua_isnoneornil(L, 2)) {
        lua_newtable(L);
        for (auto& [k, r] : p->tasks) { lua_rawgeti(L, LUA_REGISTRYINDEX, r); lua_setfield(L, -2, k.c_str()); }
        return 1;
    }
    auto it = p->tasks.find(luaL_checkstring(L, 2));
    if (it == p->tasks.end()) lua_pushnil(L); else lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);
    return 1;
}

// ---- metamethods
int partIndex(lua_State* L) {
    Part* p = self(L, "__index");
    if (lua_type(L, 2) != LUA_TSTRING) {
        lua_pushnil(L);
        return 1;
    }
    std::string k = lua_tostring(L, 2);
    if (Part* c = p->child(k)) {
        pushRef(L, c, tModelPart);
        return 1;
    }
    int ref = k == "preRender" ? p->preRender : k == "midRender" ? p->midRender : k == "postRender" ? p->postRender : 0;
    if (ref) lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    else lua_pushnil(L);
    return 1;
}
int partNewIndex(lua_State* L) {
    Part* p = self(L, "__newindex");
    std::string k = lua_isstring(L, 2) ? lua_tostring(L, 2) : "null";
    int* slot = k == "preRender" ? &p->preRender : k == "midRender" ? &p->midRender : k == "postRender" ? &p->postRender : nullptr;
    if (!slot) return luaL_error(L, "Cannot assign value on key \"%s\"", k.c_str());
    if (!lua_isnoneornil(L, 3) && !lua_isfunction(L, 3))
        return luaL_error(L, "bad argument 3 to '__newindex' (function expected, got %s)", figuraTypeName(L, 3).c_str());
    if (*slot) luaL_unref(L, LUA_REGISTRYINDEX, *slot);
    *slot = 0;
    if (lua_isfunction(L, 3)) {
        lua_pushvalue(L, 3);
        *slot = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    return 0;
}
int setRenderFn(lua_State* L, int Part::*slot, char const* fn) {
    Part* p = self(L, fn);
    if (!lua_isnoneornil(L, 2) && !lua_isfunction(L, 2))
        return luaL_error(L, "bad argument 2 to '%s' (function expected, got %s)", fn, figuraTypeName(L, 2).c_str());
    if (p->*slot) luaL_unref(L, LUA_REGISTRYINDEX, p->*slot);
    p->*slot = 0;
    if (lua_isfunction(L, 2)) {
        lua_pushvalue(L, 2);
        p->*slot = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    return ret(L);
}
int setPreRender(lua_State* L) { return setRenderFn(L, &Part::preRender, "setPreRender"); }
int setMidRender(lua_State* L) { return setRenderFn(L, &Part::midRender, "setMidRender"); }
int setPostRender(lua_State* L) { return setRenderFn(L, &Part::postRender, "setPostRender"); }
int partToString(lua_State* L) {
    Part* p = self(L, "__tostring");
    lua_pushfstring(L, "%s (ModelPart)", p->name.c_str());
    return 1;
}

} // namespace

void openModelApi(Avatar& a) {
    lua_State* L = a.L;
    rememberMatrices(L);
    openTextureApi(a);
    openRenderTaskApi(a);

    TypeDef vd;
    vd.name     = "Vertex";
    vd.methods  = {{"getPos", vGetPos}, {"setPos", vSetPos}, {"pos", vSetPos}, {"getUV", vGetUV}, {"setUV", vSetUV},
                   {"uv", vSetUV}, {"getNormal", vGetNormal}, {"setNormal", vSetNormal}, {"normal", vSetNormal}};
    vd.tostring = vToString;
    tVertex     = registerType(L, vd);

    TypeDef d;
    d.name    = "ModelPart";
    d.methods = {
        {"__index", partIndex}, {"__newindex", partNewIndex},
        {"setPos", setPos}, {"pos", setPos}, {"getPos", getPos}, {"setRot", setRot}, {"rot", setRot}, {"getRot", getRot},
        {"setOffsetRot", setOffsetRot}, {"offsetRot", setOffsetRot}, {"getOffsetRot", getOffsetRot},
        {"setScale", setScale}, {"scale", setScale}, {"getScale", getScale}, {"setOffsetScale", setOffsetScale},
        {"offsetScale", setOffsetScale}, {"getOffsetScale", getOffsetScale}, {"setPivot", setPivot}, {"pivot", setPivot},
        {"getPivot", getPivot}, {"setOffsetPivot", setOffsetPivot}, {"offsetPivot", setOffsetPivot},
        {"getOffsetPivot", getOffsetPivot}, {"getAnimPos", getAnimPos}, {"getAnimRot", getAnimRot},
        {"getAnimScale", getAnimScale}, {"getTruePos", getTruePos}, {"getTrueRot", getTrueRot},
        {"getTrueScale", getTrueScale}, {"getTruePivot", getTruePivot}, {"getPositionMatrix", getPositionMatrix},
        {"getPositionMatrixRaw", getPositionMatrixRaw}, {"getNormalMatrix", getNormalMatrix},
        {"getNormalMatrixRaw", getNormalMatrixRaw}, {"setMatrix", setMatrix}, {"matrix", setMatrix},
        {"partToWorldMatrix", partToWorldMatrix}, {"getVisible", getVisible}, {"setVisible", setVisible},
        {"visible", setVisible}, {"getName", getName}, {"getParent", getParent}, {"getChildren", getChildren},
        {"isChildOf", isChildOf}, {"getType", getType}, {"getParentType", getParentType},
        {"setParentType", setParentType}, {"parentType", setParentType},
        {"setPrimaryRenderType", setPrimaryRenderType}, {"primaryRenderType", setPrimaryRenderType},
        {"setSecondaryRenderType", setSecondaryRenderType}, {"secondaryRenderType", setSecondaryRenderType},
        {"getPrimaryRenderType", getPrimaryRenderType}, {"getSecondaryRenderType", getSecondaryRenderType},
        {"setColor", setColor}, {"color", setColor}, {"getColor", getColor}, {"setPrimaryColor", setPrimaryColor},
        {"primaryColor", setPrimaryColor}, {"getPrimaryColor", getPrimaryColor},
        {"setSecondaryColor", setSecondaryColor}, {"secondaryColor", setSecondaryColor},
        {"getSecondaryColor", getSecondaryColor}, {"setOpacity", setOpacity}, {"opacity", setOpacity},
        {"getOpacity", getOpacity}, {"setLight", setLight}, {"light", setLight}, {"getLight", getLight},
        {"setOverlay", setOverlay}, {"overlay", setOverlay}, {"getOverlay", getOverlay},
        {"overrideVanillaRot", overrideVanillaRot}, {"overrideVanillaPos", overrideVanillaPos},
        {"overrideVanillaScale", overrideVanillaScale}, {"setPrimaryTexture", setPrimaryTexture},
        {"primaryTexture", setPrimaryTexture}, {"setSecondaryTexture", setSecondaryTexture},
        {"secondaryTexture", setSecondaryTexture}, {"getPrimaryTexture", getPrimaryTexture},
        {"getSecondaryTexture", getSecondaryTexture}, {"getPrimaryDefinedTextures", getPrimaryDefinedTextures},
        {"getSecondaryDefinedTextures", getSecondaryDefinedTextures}, {"getTextures", getTextures},
        {"getTextureSize", getTextureSize}, {"setUV", setUV}, {"uv", setUV}, {"getUV", getUV},
        {"setUVPixels", setUVPixels}, {"uvPixels", setUVPixels}, {"getUVPixels", getUVPixels},
        {"setUVMatrix", setUVMatrix}, {"uvMatrix", setUVMatrix}, {"getUVMatrix", getUVMatrix},
        {"getVertices", getVertices}, {"getAllVertices", getAllVertices},
        {"remove", removeP}, {"addChild", addChild}, {"removeChild", removeChild}, {"moveTo", moveTo},
        {"newPart", newPart}, {"copy", copyP}, {"newText", partNewText}, {"newItem", partNewItem},
        {"newBlock", partNewBlock}, {"newSprite", partNewSprite}, {"newEntity", partNewEntity},
        {"addTask", partAddTask}, {"getTask", getTask}, {"removeTask", partRemoveTask},
        {"setPreRender", setPreRender}, {"setMidRender", setMidRender}, {"setPostRender", setPostRender}};
    d.index    = partIndex;
    d.newindex = partNewIndex;
    d.tostring = partToString;
    tModelPart = registerType(L, d);

    pushRef(L, a.root, tModelPart);
    lua_setglobal(L, "models");
}

} // namespace bf::figura
