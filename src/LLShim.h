// Renderer Foundation - thin wrapper so files that include d3d12.h never include LeviLamina headers
// (LeviLamina's predefine header declares some D3D12 structs itself). See Notice.h.
#pragma once
#include <string>

namespace rf::ll_shim {
int  hook(void* target, void* detour, void** original);
void unhook(void* target, void* detour);
void info(std::string const& msg);
void warn(std::string const& msg);
void error(std::string const& msg);
} // namespace rf::ll_shim
