#pragma once
// Bedrock Forge SDK - InputAPI (v0)
// Registers a remappable key through LeviLamina's client KeyRegistry. The key
// appears in Minecraft's own Controls settings, so players can rebind it.

#include <functional>
#include <string_view>

#include "ll/api/input/KeyRegistry.h"

namespace bf::input {

/// Windows virtual-key codes for the keys our examples use.
namespace vk {
inline constexpr int J  = 0x4A;
inline constexpr int K  = 0x4B;
inline constexpr int F8 = 0x77;
} // namespace vk

/// Bind `name` (default key `keyCode`) and call `onDown` when it is pressed.
/// suspendable=true means the handler doesn't fire while a menu/chat has focus.
inline void bindKey(std::string_view name, int keyCode, std::function<void()> onDown) {
    auto& key = ll::input::KeyRegistry::getInstance().getOrCreateKey(name, {keyCode}, true);
    key.registerButtonDownHandler([cb = std::move(onDown)](::FocusImpact, ::IClientInstance&) { cb(); }, true);
}

} // namespace bf::input
