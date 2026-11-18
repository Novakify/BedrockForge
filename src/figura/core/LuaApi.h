// FiguraBridge Lua API registration (one opener per API translation unit).
#pragma once
#include <cstdint>
struct lua_State;

namespace bf::figura {
int figuraToJson(lua_State* L);    // LuaApiData.cpp (FiguraLuaJson)
int figuraParseJson(lua_State* L);
class Avatar;
extern uint32_t tModelPart, tTexture, tRenderTask, tAnimation, tAnimationAPI;
void openModelApi(Avatar& a);
void openAnimationApi(Avatar& a);
void openEntityApis(Avatar& a);
void openDataApis(Avatar& a);     // json, data/streams, file, net/http, resources, texture atlas (LuaApiData.cpp)   // player/user, world, client, host, renderer, nameplate, vanilla_model, sounds, avatar...
} // namespace bf::figura
