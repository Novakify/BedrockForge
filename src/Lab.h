// Renderer Foundation - lab switches (config/Lab.txt), read once when the game starts. Original code for Renderer Foundation.
//   vvtest 1       every custom sky off for this session: Bedrock Forge's Sky module (Living Skies / LyricsForge),
//                  World FX skies (Cloud Sea / Novak Glass) and the Haze / Mist overlays. The game's own sky (vanilla
//                  or Vibrant Visuals) is drawn, so resource-pack VV settings can be judged on their own.
//   vvtest_once 1  switch vvtest back to 0 in the file as soon as it is read: the next launch is normal again.
//   pix 1          load PIX's WinPixGpuCapturer.dll before the game makes its D3D12 device, so PIX can attach to a
//                  LeviLauncher-started game and take GPU captures.
//   pix_once 1     same one-launch behaviour for pix.
//   pix_dll <path> optional: exact path of WinPixGpuCapturer.dll (default: newest C:\Program Files\Microsoft PIX\*\).
#pragma once
#include <filesystem>

namespace rf::lab {
void init(std::filesystem::path const& configDir);   // call first in Mod::load(), before any D3D12 hook
bool vvTest();                                         // true for the whole session when vvtest was on at start
bool pixLoaded();
} // namespace rf::lab
