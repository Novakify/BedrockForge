#pragma once
// Bedrock Forge SDK - InputAPI (v0)
// Binds a key to a callback. LeviLamina's client KeyRegistry handlers never fired in game on 1.26.51 (J / K /
// LyricsFlow's keys were dead), so keys are polled instead, the same way the Right Shift menu key works: once per
// UI frame, edge-triggered, only while the game window has focus and the plain HUD screen is on top (typing in chat
// or a menu never triggers them). Not rebindable in Controls for now.

#include <functional>
#include <string_view>

namespace bf::input {

/// Windows virtual-key codes for the keys our examples use.
namespace vk {
inline constexpr int J  = 0x4A;
inline constexpr int K  = 0x4B;
inline constexpr int F8 = 0x77;
} // namespace vk

/// Bind `name` (key `keyCode`, Windows VK) and call `onDown` when it is pressed in the world (HUD screen, focused).
void bindKey(std::string_view name, int keyCode, std::function<void()> onDown);

} // namespace bf::input
