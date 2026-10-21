// Renderer Foundation - see LLShim.h, Notice.h.
#include "LLShim.h"

#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"

namespace rf::ll_shim {
int hook(void* target, void* detour, void** original) {
    return ll::memory::hook(target, detour, original, ll::memory::HookPriority::Normal);
}
void unhook(void* target, void* detour) { ll::memory::unhook(target, detour); }
void info(std::string const& msg) { ll::mod::NativeMod::current()->getLogger().info("{}", msg); }
void warn(std::string const& msg) { ll::mod::NativeMod::current()->getLogger().warn("{}", msg); }
void error(std::string const& msg) { ll::mod::NativeMod::current()->getLogger().error("{}", msg); }
} // namespace rf::ll_shim
