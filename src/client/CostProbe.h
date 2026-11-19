#pragma once
// [BF] FPS cost badge: measure a module's real cost (frame time on vs off). See CostProbe.cpp.
#include <string>

namespace bedrock_forge::client::cost {
struct Result { float msOn = 0, msOff = 0, fpsOn = 0, fpsOff = 0; };
bool start(std::string const& id);          // false: one is already running / no such module
bool running(std::string* id = nullptr);
bool result(std::string const& id, Result& out);
void tick();                                // every HUD frame
} // namespace bedrock_forge::client::cost
