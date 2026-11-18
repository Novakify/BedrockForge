// LyricsFlow port - [BF] colours from cover art (new feature). Original code for Bedrock Forge.
#pragma once
#include <cstdint>

namespace bf::lyrics {

/// The cover's strongest vivid colour, pushed to a bright neon (0xRRGGBB). Weighs every pixel by how saturated and
/// bright it is, picks the hue band with the most weight and averages it. Grey / dark covers give `fallback`.
uint32_t coverAccent(uint8_t const* rgba, int w, int h, uint32_t fallback);

} // namespace bf::lyrics
