// FiguraBridge Lua API: ModelPart (FiguraModelPart's Lua methods) and textures (TextureAPI / FiguraTexture).
#include <algorithm>
#include <cmath>

#include "figura/core/Avatar.h"
#include "figura/core/LuaApi.h"
#include "figura/core/LuaBind.h"

namespace bf::figura {

using namespace lua;

uint32_t tModelPart = 0, tTexture = 0, tRenderTask = 0;

namespace {

Part* self(lua_State* L, char const* fn) { return check<Part>(L, 1, tModelPart, fn); }
int   ret(lua_State* L) { lua_settop(L, 1); return 1; }

// ---- transforms
#define VEC_SETTER(fnName, field, def)                                   \
    int fnName(lua_State* L) {                                           \
        Part* p = self(L, #fnName);                                      \
        p->field = parseVec3(L, 2, #fnName, def);                        \
        return ret(L);                                                   \
    }
#define VEC_GETTER(fnName, expr)                                         \
    int fnName(lua_State* L) {                                           \
        Part* p = self(L, #fnName);                                      \
        pushVec3(L, expr);                                               \
        return 1;                                                        \
    }
VEC_SETTER(setPos, position, 0)
VEC_SETTER(setRot, rotation, 0)
VEC_SETTER(setOffsetRot, offsetRot, 0)
VEC_SETTER(setScale, scale, 1)
VEC_SETTER(setOffsetScale, offsetScale, 1)
VEC_SETTER(setPivot, pivot, 0)
VEC_SETTER(setOffsetPivot, offsetPivot, 0)
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
VEC_GETTER(getColor, p->color)
VEC_GETTER(getPrimaryColor, p->color)
VEC_GETTER(getSecondaryColor, p->color2)
#undef VEC_SETTER
#undef VEC_GETTER

int getPositionMatrix(lua_State* L) {
    Part* p = self(L, "getPositionMatrix");
    p->recalculate();
    pushMat4(L, p->positionMatrix);
    return 1;
}
int setMatrix(lua_State* L) {
    Part* p = self(L, "setMatrix");
    Mat4* m = toMat4(L, 2);
    if (!m) return luaL_error(L, "setMatrix expects a Matrix4");
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
    Part* o = check<Part>(L, 2, tModelPart, "isChildOf");
    lua_pushboolean(L, p->isChildOf(o));
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
    std::string s = optStr(L, 2, "None");
    p->parentType = parentTypeGet(s);
    p->vanillaVisible.reset();
    return ret(L);
}
int setPrimaryRenderType(lua_State* L) { self(L, "setPrimaryRenderType")->primaryRenderType = optStr(L, 2, ""); return ret(L); }
int setSecondaryRenderType(lua_State* L) { self(L, "setSecondaryRenderType")->secondaryRenderType = optStr(L, 2, ""); return ret(L); }
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
    Vec3 c  = parseVec3(L, 2, "setColor", 1);
    p->color = p->color2 = c;
    return ret(L);
}
int setPrimaryColor(lua_State* L) { self(L, "setPrimaryColor")->color = parseVec3(L, 2, "setPrimaryColor", 1); return ret(L); }
int setSecondaryColor(lua_State* L) { self(L, "setSecondaryColor")->color2 = parseVec3(L, 2, "setSecondaryColor", 1); return ret(L); }
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
    if (lua_isnoneornil(L, 2)) p->light.reset();
    else {
        double b = 0, s = 0;
        if (double* v = toVec(L, 2, nullptr)) { b = v[0]; s = v[1]; }
        else { b = luaL_checknumber(L, 2); s = optNum(L, 3, b); }
        p->light = (int(s) << 20) | (int(b) << 4);
    }
    return ret(L);
}
int getLight(lua_State* L) {
    Part* p = self(L, "getLight");
    if (!p->light) { lua_pushnil(L); return 1; }
    pushVec2(L, (*p->light >> 4) & 0xF, (*p->light >> 20) & 0xF);
    return 1;
}
int setOverlay(lua_State* L) {
    Part* p = self(L, "setOverlay");
    if (lua_isnoneornil(L, 2)) p->overlay.reset();
    else p->overlay = int(luaL_checknumber(L, 2));
    return ret(L);
}
int getOverlay(lua_State* L) { (void)self(L, "getOverlay"); lua_pushnil(L); return 1; }
int overrideVanillaRot(lua_State* L) { lua_pushboolean(L, self(L, "overrideVanillaRot")->overrideVanillaRot()); return 1; }
int overrideVanillaPos(lua_State* L) { lua_pushboolean(L, self(L, "overrideVanillaPos")->overrideVanillaPos()); return 1; }
int overrideVanillaScale(lua_State* L) { lua_pushboolean(L, self(L, "overrideVanillaScale")->overrideVanillaScale()); return 1; }

// ---- textures
TexKind kindOf(std::string s) {
    for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    if (s == "CUSTOM") return TexKind::Custom;
    if (s == "SKIN") return TexKind::Skin;
    if (s == "CAPE") return TexKind::Cape;
    if (s == "ELYTRA") return TexKind::Elytra;
    if (s == "RESOURCE") return TexKind::Resource;
    if (s == "PRIMARY") return TexKind::Primary;
    if (s == "SECONDARY") return TexKind::Secondary;
    if (s == "SPECULAR") return TexKind::Specular;
    if (s == "NORMAL") return TexKind::Normal;
    return TexKind::Default;
}
void setTex(lua_State* L, TextureOverride& t, char const* fn) {
    if (lua_isnoneornil(L, 2)) {
        t = {};
        return;
    }
    std::string type = luaL_checkstring(L, 2);
    t.set  = true;
    t.kind = kindOf(type);
    t.file = -1;
    if (t.kind == TexKind::Custom) {
        auto* f = static_cast<TextureFile*>(toRef(L, 3, tTexture));
        if (!f) luaL_error(L, "%s: Invalid texture override type: CUSTOM needs a Texture", fn);
        t.file = f->index;
    } else if (t.kind == TexKind::Resource) {
        t.resource = optStr(L, 3, "");
    }
}
int setPrimaryTexture(lua_State* L) { setTex(L, self(L, "setPrimaryTexture")->primaryTexture, "setPrimaryTexture"); return ret(L); }
int setSecondaryTexture(lua_State* L) { setTex(L, self(L, "setSecondaryTexture")->secondaryTexture, "setSecondaryTexture"); return ret(L); }
int pushTexOverride(lua_State* L, TextureOverride const& t) {
    if (!t.set) { lua_pushnil(L); return 1; }
    static char const* names[] = {"DEFAULT", "CUSTOM", "SKIN", "CAPE", "ELYTRA", "RESOURCE", "PRIMARY", "SECONDARY", "SPECULAR", "NORMAL"};
    lua_pushstring(L, names[int(t.kind)]);
    Avatar* a = avatarOf(L);
    if (t.kind == TexKind::Custom && t.file >= 0 && t.file < int(a->textureFiles.size())) {
        pushRef(L, &a->textureFiles[t.file], tTexture);
        return 2;
    }
    if (t.kind == TexKind::Resource) { lua_pushstring(L, t.resource.c_str()); return 2; }
    return 1;
}
int getPrimaryTexture(lua_State* L) { return pushTexOverride(L, self(L, "getPrimaryTexture")->primaryTexture); }
int getSecondaryTexture(lua_State* L) { return pushTexOverride(L, self(L, "getSecondaryTexture")->secondaryTexture); }
int getTextures(lua_State* L) {
    Part*   p = self(L, "getTextures");
    Avatar* a = avatarOf(L);
    lua_newtable(L);
    int s = p->defaultSet;
    if (s >= 0 && s < int(a->textureSets.size())) {
        int i = 1;
        for (int f : {a->textureSets[s].d, a->textureSets[s].e})
            if (f >= 0) { pushRef(L, &a->textureFiles[f], tTexture); lua_rawseti(L, -2, i++); }
    }
    return 1;
}
int setUV(lua_State* L) {
    Part* p = self(L, "setUV");
    if (double* v = toVec(L, 2, nullptr)) p->uvOffset = {v[0], v[1], 0};
    else p->uvOffset = {optNum(L, 2, 0), optNum(L, 3, 0), 0};
    return ret(L);
}
int getUV(lua_State* L) { Part* p = self(L, "getUV"); pushVec2(L, p->uvOffset.x, p->uvOffset.y); return 1; }

// ---- tree editing
int removeP(lua_State* L) {
    Part* p = self(L, "remove");
    if (p->parent) {
        auto& c = p->parent->children;
        c.erase(std::remove(c.begin(), c.end(), p), c.end());
        p->parent = nullptr;
    }
    return ret(L);
}
int addChild(lua_State* L) {
    Part* p = self(L, "addChild");
    Part* c = check<Part>(L, 2, tModelPart, "addChild");
    if (c == p || p->isChildOf(c)) return luaL_error(L, "Cannot add a parent as its own child");
    if (c->parent) {
        auto& v = c->parent->children;
        v.erase(std::remove(v.begin(), v.end(), c), v.end());
    }
    c->parent = p;
    p->children.push_back(c);
    return ret(L);
}
int removeChild(lua_State* L) {
    Part* p = self(L, "removeChild");
    Part* c = check<Part>(L, 2, tModelPart, "removeChild");
    auto& v = p->children;
    if (std::find(v.begin(), v.end(), c) != v.end()) {
        v.erase(std::remove(v.begin(), v.end(), c), v.end());
        c->parent = nullptr;
    }
    return ret(L);
}
int moveTo(lua_State* L) {
    Part* p = self(L, "moveTo");
    Part* n = check<Part>(L, 2, tModelPart, "moveTo");
    if (p->parent) {
        auto& v = p->parent->children;
        v.erase(std::remove(v.begin(), v.end(), p), v.end());
    }
    p->parent = n;
    n->children.push_back(p);
    return ret(L);
}
int newPart(lua_State* L) {
    // Bedrock can't add bones at runtime: the part exists for scripts (transforms, children moves) but has no geometry
    Part*   p = self(L, "newPart");
    Avatar* a = avatarOf(L);
    Part*   c = a->addPart(luaL_checkstring(L, 2), p);
    c->parentType = lua_isstring(L, 3) ? parentTypeGet(lua_tostring(L, 3)) : ParentType::None;
    a->out.resize(a->parts.size());
    a->compiledPivots.push_back({});
    pushRef(L, c, tModelPart);
    return 1;
}
int copyP(lua_State* L) {
    Part*   p = self(L, "copy");
    Avatar* a = avatarOf(L);
    Part*   c = a->addPart(luaL_checkstring(L, 2), nullptr);
    std::string n = c->name;
    int idx = c->index;
    *c        = Part(*p);
    c->name   = n;
    c->index  = idx;
    c->parent = nullptr;
    c->children.clear();
    a->out.resize(a->parts.size());
    a->compiledPivots.push_back(p->pivot);
    pushRef(L, c, tModelPart);
    return 1;
}

// ---- render tasks (API objects only; Bedrock draws none yet)
struct Task {
    std::string name, type, text;
    bool        visible = true;
    Vec3        pos, rot, scale{1, 1, 1};
};
std::vector<std::unique_ptr<Task>> gTasks;
int newTaskOf(lua_State* L, char const* type) {
    Part* p = self(L, type);
    auto  t = std::make_unique<Task>();
    t->name = luaL_checkstring(L, 2);
    t->type = type;
    pushRef(L, t.get(), tRenderTask);
    lua_pushvalue(L, -1);
    p->tasks[t->name] = luaL_ref(L, LUA_REGISTRYINDEX);
    gTasks.push_back(std::move(t));
    return 1;
}
int newText(lua_State* L) { return newTaskOf(L, "TEXT"); }
int newItem(lua_State* L) { return newTaskOf(L, "ITEM"); }
int newBlock(lua_State* L) { return newTaskOf(L, "BLOCK"); }
int newSprite(lua_State* L) { return newTaskOf(L, "SPRITE"); }
int newEntity(lua_State* L) { return newTaskOf(L, "ENTITY"); }
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
int removeTask(lua_State* L) {
    Part* p = self(L, "removeTask");
    if (lua_isnoneornil(L, 2)) p->tasks.clear();
    else if (lua_isstring(L, 2)) p->tasks.erase(lua_tostring(L, 2));
    return ret(L);
}
// generic chainable task setters
int taskChain(lua_State* L) { lua_settop(L, 1); return 1; }
int taskIndex(lua_State* L) {
    Task* t = check<Task>(L, 1, tRenderTask, "__index");
    std::string k = luaL_checkstring(L, 2);
    if (k == "getName") { lua_pushcfunction(L, [](lua_State* L2) { lua_pushstring(L2, check<Task>(L2, 1, tRenderTask, "getName")->name.c_str()); return 1; }); return 1; }
    if (k == "getText") { lua_pushcfunction(L, [](lua_State* L2) { lua_pushstring(L2, check<Task>(L2, 1, tRenderTask, "getText")->text.c_str()); return 1; }); return 1; }
    if (k == "setText" || k == "text") { lua_pushcfunction(L, [](lua_State* L2) { check<Task>(L2, 1, tRenderTask, "setText")->text = optStr(L2, 2, ""); lua_settop(L2, 1); return 1; }); return 1; }
    if (k == "isVisible" || k == "getVisible") { lua_pushcfunction(L, [](lua_State* L2) { lua_pushboolean(L2, check<Task>(L2, 1, tRenderTask, "isVisible")->visible); return 1; }); return 1; }
    if (k == "setVisible" || k == "visible") { lua_pushcfunction(L, [](lua_State* L2) { check<Task>(L2, 1, tRenderTask, "setVisible")->visible = lua_toboolean(L2, 2); lua_settop(L2, 1); return 1; }); return 1; }
    if (k.rfind("get", 0) == 0) { lua_pushcfunction(L, [](lua_State* L2) { lua_pushnil(L2); return 1; }); return 1; }
    (void)t;
    lua_pushcfunction(L, taskChain); // set*/everything else: accepted and ignored (chainable)
    return 1;
}

// ---- metamethods
int partIndex(lua_State* L) {
    Part* p = self(L, "__index");
    if (!lua_isstring(L, 2)) return 0;
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
    std::string k = luaL_checkstring(L, 2);
    int* slot = k == "preRender" ? &p->preRender : k == "midRender" ? &p->midRender : k == "postRender" ? &p->postRender : nullptr;
    if (!slot) return luaL_error(L, "Cannot assign value on key \"%s\"", k.c_str());
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

// ---- Texture (FiguraTexture): the avatar's own textures (read-only on Bedrock)
TextureFile* tself(lua_State* L, char const* fn) { return check<TextureFile>(L, 1, tTexture, fn); }
int texGetName(lua_State* L) { lua_pushstring(L, tself(L, "getName")->figuraName.c_str()); return 1; }
int texGetPath(lua_State* L) { lua_pushstring(L, tself(L, "getPath")->file.c_str()); return 1; }
int texGetDims(lua_State* L) { auto* t = tself(L, "getDimensions"); pushVec2(L, t->w, t->h); return 1; }
int texGetPixel(lua_State* L) { (void)tself(L, "getPixel"); pushVec4(L, 1, 1, 1, 1); return 1; }
int texChain(lua_State* L) { lua_settop(L, 1); return 1; }
int texToString(lua_State* L) { lua_pushfstring(L, "%s (Texture)", tself(L, "__tostring")->figuraName.c_str()); return 1; }

// TextureAPI
int texApiIndex(lua_State* L) {
    Avatar* a = avatarOf(L);
    if (!lua_isstring(L, 2)) return 0;
    std::string k = lua_tostring(L, 2);
    int idx = a->fileIndexByFiguraName(k);
    if (idx >= 0) { pushRef(L, &a->textureFiles[idx], tTexture); return 1; }
    lua_pushnil(L);
    return 1;
}
int texApiGetTextures(lua_State* L) {
    Avatar* a = avatarOf(L);
    lua_newtable(L);
    int i = 1;
    for (auto& f : a->textureFiles) { pushRef(L, &f, tTexture); lua_rawseti(L, -2, i++); }
    return 1;
}
int texApiGet(lua_State* L) { lua_settop(L, 2); lua_pushvalue(L, 2); lua_replace(L, 2); return texApiIndex(L); }
int texApiUnsupported(lua_State* L) { return luaL_error(L, "FiguraBridge: dynamic textures are not supported on Bedrock yet"); }

} // namespace

void openModelApi(Avatar& a) {
    lua_State* L = a.L;
    TypeDef d;
    d.name    = "ModelPart";
    d.methods = {
        {"setPos", setPos}, {"pos", setPos}, {"getPos", getPos}, {"setRot", setRot}, {"rot", setRot}, {"getRot", getRot},
        {"setOffsetRot", setOffsetRot}, {"offsetRot", setOffsetRot}, {"getOffsetRot", getOffsetRot},
        {"setScale", setScale}, {"scale", setScale}, {"getScale", getScale}, {"setOffsetScale", setOffsetScale},
        {"offsetScale", setOffsetScale}, {"getOffsetScale", getOffsetScale}, {"setPivot", setPivot}, {"pivot", setPivot},
        {"getPivot", getPivot}, {"setOffsetPivot", setOffsetPivot}, {"offsetPivot", setOffsetPivot},
        {"getOffsetPivot", getOffsetPivot}, {"getAnimPos", getAnimPos}, {"getAnimRot", getAnimRot},
        {"getAnimScale", getAnimScale}, {"getTruePos", getTruePos}, {"getTrueRot", getTrueRot},
        {"getTrueScale", getTrueScale}, {"getTruePivot", getTruePivot}, {"getPositionMatrix", getPositionMatrix},
        {"getPositionMatrixRaw", getPositionMatrix}, {"setMatrix", setMatrix}, {"matrix", setMatrix},
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
        {"getSecondaryTexture", getSecondaryTexture}, {"getTextures", getTextures}, {"setUV", setUV}, {"uv", setUV},
        {"getUV", getUV}, {"setUVPixels", setUV}, {"uvPixels", setUV}, {"remove", removeP}, {"addChild", addChild},
        {"removeChild", removeChild}, {"moveTo", moveTo}, {"newPart", newPart}, {"copy", copyP},
        {"newText", newText}, {"newItem", newItem}, {"newBlock", newBlock}, {"newSprite", newSprite},
        {"newEntity", newEntity}, {"getTask", getTask}, {"removeTask", removeTask}, {"setPreRender", setPreRender},
        {"preRender", setPreRender}, {"setMidRender", setMidRender}, {"midRender", setMidRender},
        {"setPostRender", setPostRender}, {"postRender", setPostRender}};
    d.index    = partIndex;
    d.newindex = partNewIndex;
    d.tostring = partToString;
    tModelPart = registerType(L, d);

    TypeDef rt;
    rt.name     = "RenderTask";
    rt.index    = taskIndex;
    rt.newindex = nullptr;
    tRenderTask = registerType(L, rt);

    TypeDef td;
    td.name     = "Texture";
    td.methods  = {{"getName", texGetName}, {"getPath", texGetPath}, {"getDimensions", texGetDims},
                   {"getPixel", texGetPixel}, {"setPixel", texChain}, {"update", texChain}, {"restore", texChain},
                   {"fill", texChain}, {"applyFunc", texChain}, {"applyMatrix", texChain}};
    td.tostring = texToString;
    tTexture    = registerType(L, td);

    pushRef(L, a.root, tModelPart);
    lua_setglobal(L, "models");

    TypeDef ta;
    ta.name    = "TextureAPI";
    ta.methods = {{"getTextures", texApiGetTextures}, {"get", texApiGet}, {"newTexture", texApiUnsupported},
                  {"read", texApiUnsupported}, {"copy", texApiUnsupported}, {"fromVanilla", texApiUnsupported}};
    ta.index   = texApiIndex;
    uint32_t tTexApi = registerType(L, ta);
    static int texApiAnchor;
    pushRef(L, &texApiAnchor, tTexApi);
    lua_setglobal(L, "textures");
}

} // namespace bf::figura
