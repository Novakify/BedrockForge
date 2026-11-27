# Lab switches (`mods/RendererFoundation/config/Lab.txt`)

Read once when the game starts. RF writes a commented template the first time it runs. Restart the game after a change.

| Line | Does |
|---|---|
| `vvtest 1` | **VV test session.** Every custom sky is off: Bedrock Forge's Sky module (Living Skies / LyricsForge pool), the World FX skies (Cloud Sea, Novak Glass), and the Haze / Mist overlays. The game draws its own sky (vanilla, or the Vibrant Visuals deferred sky), so a VV resource pack's atmospherics / lighting / fog / grading can be judged on their own. Ctrl+F6 is ignored, and `WorldFx.txt` is **not saved** during the session, so your Cloud Sea / Novak Glass choice is untouched. |
| `vvtest_once 1` | `vvtest` goes back to `0` as soon as it is read, so the next launch is normal again. |
| `pix 1` | Loads PIX's `WinPixGpuCapturer.dll` before the game creates its D3D12 device. Then launch through LeviLauncher as usual and in PIX use **Attach** → `Minecraft.Windows.exe` → GPU Capture. (Launching the game from PIX doesn't work for the LeviLauncher instance.) |
| `pix_once 1` | Same one-launch behaviour for `pix`. |
| `pix_dll <path>` | Optional: exact capturer path. Default: newest `C:\Program Files\Microsoft PIX\<version>\WinPixGpuCapturer.dll`. |

Log lines to look for in `logs/latest.log`: `[RF] lab: VV test session ...`, `[RF] lab: PIX GPU capturer loaded from ...`.

Why: VV lights the world from the sky (sky probe / IBL), so any custom sky also tints the terrain. VV pack A/B tests need the game's own sky, and packs are cached per session, so each subpack needs its own restart.
