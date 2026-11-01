// FiguraBridge Lua API registration (one opener per API translation unit).
#pragma once
#include <cstdint>

namespace bf::figura {
class Avatar;
extern uint32_t tModelPart, tTexture, tRenderTask, tAnimation, tAnimationAPI;
void openModelApi(Avatar& a);
void openAnimationApi(Avatar& a);
void openEntityApis(Avatar& a);   // player/user, world, client, host, renderer, nameplate, vanilla_model, sounds, avatar...
} // namespace bf::figura
