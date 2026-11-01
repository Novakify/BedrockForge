// FiguraBridge Lua API: Animation (Figura Animation's Lua methods) and AnimationAPI (`animations` global).
#include <algorithm>
#include <cmath>

#include "figura/core/Avatar.h"
#include "figura/core/LuaApi.h"
#include "figura/core/LuaBind.h"

namespace bf::figura {

using namespace lua;

uint32_t tAnimation = 0, tAnimationAPI = 0;

namespace {
Animation* self(lua_State* L, char const* fn) { return check<Animation>(L, 1, tAnimation, fn); }
int        ret(lua_State* L) { lua_settop(L, 1); return 1; }
AnimClock const& clk(lua_State* L) { return avatarOf(L)->clock; }

int isPlaying(lua_State* L) { lua_pushboolean(L, self(L, "isPlaying")->playState == PlayState::Playing); return 1; }
int isPaused(lua_State* L) { lua_pushboolean(L, self(L, "isPaused")->playState == PlayState::Paused); return 1; }
int isStopped(lua_State* L) { lua_pushboolean(L, self(L, "isStopped")->playState == PlayState::Stopped); return 1; }
int isHolding(lua_State* L) { lua_pushboolean(L, self(L, "isHolding")->playState == PlayState::Holding); return 1; }
int play(lua_State* L) { self(L, "play")->play(clk(L)); return ret(L); }
int pause(lua_State* L) { self(L, "pause")->pause(clk(L)); return ret(L); }
int stop(lua_State* L) { self(L, "stop")->stop(); return ret(L); }
int restart(lua_State* L) { self(L, "restart")->restart(clk(L)); return ret(L); }
int setPlaying(lua_State* L) { self(L, "setPlaying")->setPlaying(lua_toboolean(L, 2) != 0, clk(L)); return ret(L); }
int getTime(lua_State* L) { lua_pushnumber(L, self(L, "getTime")->time); return 1; }
int setTime(lua_State* L) { self(L, "setTime")->setTime(float(optNum(L, 2, 0))); return ret(L); }
int getPlayState(lua_State* L) { lua_pushstring(L, playStateName(self(L, "getPlayState")->playState)); return 1; }
int newCode(lua_State* L) {
    Animation* a = self(L, "newCode");
    a->newCode(float(optNum(L, 2, 0)), luaL_checkstring(L, 3));
    return ret(L);
}
#define FLOAT_PROP(get, set, field)                                                          \
    int get(lua_State* L) { lua_pushnumber(L, self(L, #get)->field); return 1; }          \
    int set(lua_State* L) { self(L, #set)->field = float(optNum(L, 2, 0)); return ret(L); }
FLOAT_PROP(getBlend, setBlend, blend)
FLOAT_PROP(getOffset, setOffset, offset)
FLOAT_PROP(getStartDelay, setStartDelay, startDelay)
FLOAT_PROP(getLoopDelay, setLoopDelay, loopDelay)
FLOAT_PROP(getLength, setLength, length)
#undef FLOAT_PROP
int setOverride(lua_State* L) { self(L, "setOverride")->setOverride(lua_toboolean(L, 2) != 0); return ret(L); }
int getOverrideRot(lua_State* L) { lua_pushboolean(L, self(L, "getOverrideRot")->overrideRot()); return 1; }
int getOverridePos(lua_State* L) { lua_pushboolean(L, self(L, "getOverridePos")->overridePos()); return 1; }
int getOverrideScale(lua_State* L) { lua_pushboolean(L, self(L, "getOverrideScale")->overrideScale()); return 1; }
int setOverrideRot(lua_State* L) { Animation* a = self(L, "setOverrideRot"); a->override = lua_toboolean(L, 2) ? a->override | 1 : a->override & 6; return ret(L); }
int setOverridePos(lua_State* L) { Animation* a = self(L, "setOverridePos"); a->override = lua_toboolean(L, 2) ? a->override | 2 : a->override & 5; return ret(L); }
int setOverrideScale(lua_State* L) { Animation* a = self(L, "setOverrideScale"); a->override = lua_toboolean(L, 2) ? a->override | 4 : a->override & 3; return ret(L); }
int getLoop(lua_State* L) { lua_pushstring(L, loopModeName(self(L, "getLoop")->loop)); return 1; }
int setLoop(lua_State* L) {
    Animation* a = self(L, "setLoop");
    std::string s = luaL_checkstring(L, 2);
    if (!loopModeFromName(s, a->loop)) return luaL_error(L, "Illegal LoopMode: \"%s\".", s.c_str());
    return ret(L);
}
int getPriority(lua_State* L) { lua_pushinteger(L, self(L, "getPriority")->priority); return 1; }
int setPriority(lua_State* L) { self(L, "setPriority")->priority = int(optNum(L, 2, 0)); return ret(L); }
int getSpeed(lua_State* L) { lua_pushnumber(L, self(L, "getSpeed")->speed); return 1; }
int setSpeed(lua_State* L) {
    Animation* a = self(L, "setSpeed");
    if (lua_isnoneornil(L, 2)) a->setSpeed(std::nullopt);
    else a->setSpeed(float(luaL_checknumber(L, 2)));
    return ret(L);
}
int getName(lua_State* L) { lua_pushstring(L, self(L, "getName")->name.c_str()); return 1; }
int animIndex(lua_State* L) { // Animation.__index: only "name"
    Animation* a = self(L, "__index");
    if (lua_isstring(L, 2) && std::string(lua_tostring(L, 2)) == "name") { lua_pushstring(L, a->name.c_str()); return 1; }
    return 0;
}
int animToString(lua_State* L) { lua_pushfstring(L, "%s (Animation)", self(L, "__tostring")->name.c_str()); return 1; }

// ---- AnimationAPI
int apiIndex(lua_State* L) { // animations.<modelName> -> table name -> Animation
    Avatar* av = avatarOf(L);
    if (!lua_isstring(L, 2)) return 0;
    std::string mdl = lua_tostring(L, 2);
    bool any = false;
    lua_newtable(L);
    for (auto& a : av->animations)
        if (a && a->modelName == mdl) {
            pushRef(L, a.get(), tAnimation);
            lua_setfield(L, -2, a->name.c_str());
            any = true;
        }
    if (!any) { lua_pop(L, 1); lua_pushnil(L); }
    return 1;
}
int apiGetAnimations(lua_State* L) {
    Avatar* av = avatarOf(L);
    lua_newtable(L);
    int i = 1;
    for (auto& a : av->animations)
        if (a) { pushRef(L, a.get(), tAnimation); lua_rawseti(L, -2, i++); }
    return 1;
}
int apiGetPlaying(lua_State* L) {
    Avatar* av   = avatarOf(L);
    bool    hold = optBool(L, 2, false);
    lua_newtable(L);
    int i = 1;
    for (auto& a : av->animations)
        if (a && (a->playState == PlayState::Playing || (hold && a->playState == PlayState::Holding))) {
            pushRef(L, a.get(), tAnimation);
            lua_rawseti(L, -2, i++);
        }
    return 1;
}
int apiStopAll(lua_State* L) {
    Avatar* av = avatarOf(L);
    for (auto& a : av->animations)
        if (a) a->stop();
    lua_settop(L, 1);
    return 1;
}
} // namespace

void openAnimationApi(Avatar& a) {
    lua_State* L = a.L;
    TypeDef d;
    d.name    = "Animation";
    d.methods = {
        {"isPlaying", isPlaying}, {"isPaused", isPaused}, {"isStopped", isStopped}, {"isHolding", isHolding},
        {"play", play}, {"pause", pause}, {"stop", stop}, {"restart", restart}, {"setPlaying", setPlaying},
        {"playing", setPlaying}, {"getTime", getTime}, {"setTime", setTime}, {"time", setTime},
        {"getPlayState", getPlayState}, {"newCode", newCode}, {"code", newCode}, {"getBlend", getBlend},
        {"setBlend", setBlend}, {"blend", setBlend}, {"getOffset", getOffset}, {"setOffset", setOffset},
        {"offset", setOffset}, {"getStartDelay", getStartDelay}, {"setStartDelay", setStartDelay},
        {"startDelay", setStartDelay}, {"getLoopDelay", getLoopDelay}, {"setLoopDelay", setLoopDelay},
        {"loopDelay", setLoopDelay}, {"getLength", getLength}, {"setLength", setLength}, {"length", setLength},
        {"setOverride", setOverride}, {"override", setOverride}, {"getOverrideRot", getOverrideRot},
        {"getOverridePos", getOverridePos}, {"getOverrideScale", getOverrideScale},
        {"setOverrideRot", setOverrideRot}, {"overrideRot", setOverrideRot}, {"setOverridePos", setOverridePos},
        {"overridePos", setOverridePos}, {"setOverrideScale", setOverrideScale}, {"overrideScale", setOverrideScale},
        {"getLoop", getLoop}, {"setLoop", setLoop}, {"loop", setLoop}, {"getPriority", getPriority},
        {"setPriority", setPriority}, {"priority", setPriority}, {"getSpeed", getSpeed}, {"setSpeed", setSpeed},
        {"speed", setSpeed}, {"getName", getName}};
    d.index    = animIndex;
    d.tostring = animToString;
    tAnimation = registerType(L, d);

    TypeDef api;
    api.name    = "AnimationAPI";
    api.methods = {{"getAnimations", apiGetAnimations}, {"getPlaying", apiGetPlaying}, {"stopAll", apiStopAll}};
    api.index   = apiIndex;
    api.tostring = [](lua_State* L2) { lua_pushstring(L2, "AnimationAPI"); return 1; };
    tAnimationAPI = registerType(L, api);
    static int anchor;
    pushRef(L, &anchor, tAnimationAPI);
    lua_setglobal(L, "animations");
}

} // namespace bf::figura
