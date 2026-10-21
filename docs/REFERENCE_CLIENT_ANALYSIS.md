# Reference client analysis: Blueberry Client (static, read-only)

Source: two folders Fernando dropped in Downloads (2026-10-21):

| Folder | What it is |
|---|---|
| `dll refrence for our client` (472 MB, 228 files) | Process dump of a running `Minecraft.Windows.exe` (PID 5178), made with `pd64` (Process Dump). Contains every loaded module: Windows/system DLLs, the game's own DLLs, NVIDIA driver DLLs, **`BlueberryRelease.dll`**, plus 12 `hiddenmodule_*` regions and 2 `codechunk` regions |
| `second dll refrence for our client` (25 MB, 4 files) | Blueberry's per-game-version client DLLs: `1.21.9X`, `1.21.10X`, `1.21.11X`, `1.26.5X` (the last one is from `%APPDATA%\Blueberry Client\client\dlls\`) |

**Licence / how we may use it:** `BlueberryRelease.dll` version info: *Blueberry Client 2.1.3, Mystic Systems LLC,
"Copyright © 2026 Mystic Systems LLC. All rights reserved."* It's proprietary. We **do not copy, decompile into, or link
any of its code or assets**, and we never load or inject it. What we take is public-level knowledge only: its
architecture choices and the *kind* of features a client like this offers, re-implemented from scratch on LeviLamina.
Analysis was static (PE headers, version resources, import tables, readable strings) on Fernando's PC.

## What the binaries show

* **Heavily protected.** The loader DLL is 13.7 MB with a 106 KB `.text` and a 13.5 MB `.rdata` plus a custom `.pdimp`
  section: a packed/virtualised payload. Exports and RTTI are stripped, and feature names and menus are encrypted strings.
  Exactly one module description survived in clear text: *"Displays the name of every user on the server."* (a player-list module).
* **Loader + per-version payloads.** The release DLL loads a payload built for the exact game version (the
  `1.21.9X` … `1.26.5X` files; each is 4.6–9.1 MB of real code). The game-version coupling is the same problem
  LeviLamina solves for us with its versioned headers + `bedrock_runtime_data`.
* **Manual mapping.** The 12 `hiddenmodule_*` regions in the game process are code mapped without the Windows loader
  (classic injector technique). We don't need this: LeviLauncher's PreLoader loads our mod as a normal DLL.
* **Libraries visible in the payloads:** C++/WinRT, spdlog, {fmt}, nlohmann::json, a PNG decoder, WinINet (updates /
  downloads), WinRT `BackgroundMediaPlayer` (a music player), Clipboard. Embedded UI icons: `assets/star_fav.png`,
  `magnifying_glass.png`, `orb.png`, mob heads (creeper, spider, chicken, bee…), i.e. a searchable module menu with
  favourites. Lots of game sound-event names (Hit/Use/Miss…), so it hooks the game's sound / level-event systems.
* **The game itself (from the module list):** D3D12 (`D3D12Core`, `d3d11on12`), Coherent **Gameface** UI
  (`cohtml.WindowsDesktop.dll`, `RenoirCore`) for the new Ore UI screens, **V8** for the Script API, FMOD audio,
  DLSS (`nvngx_dlss`), GameInput, PlayFab multiplayer, XCurl. This is useful for our RenderDragon / UI plans.

## Feature roadmap (what a Blueberry-class client offers → our version)

Display-only and quality-of-life features. **Bold = shipped in Bedrock Forge.**

| Module | Our plan | Status |
|---|---|---|
| **FPS counter** | AfterUIRenderEvent frame counting | **v0 (ClientModules)** |
| **CPS counter** | per-frame edge detection (LL mouse hook later) | **v0** |
| **Keystrokes** | WASD / Space / LMB / RMB widget | **v0** |
| **Coordinates + direction** | `IClientInstance::getLocalPlayer()` position / yaw | **v0** |
| **Clock** | local time | **v0** |
| Player list ("names of every user on the server") | client player list from the Level | next |
| Armor / item HUD | local player's equipment + durability | next |
| Potion effects HUD | local player's MobEffect list | next |
| Zoom | hook the camera FOV (needs a LeviLamina hook) | planned |
| Freelook | detach camera yaw/pitch from the player | planned |
| Fullbright / gamma | gamma option override | planned |
| Toggle sprint / sneak | input mapping | planned |
| Hit colour / block outline colour | RenderDragon uniform hook (RENDERING_PLAN R-series) | planned |
| Chunk borders, waypoints | world-space lines (render hook) | planned |
| Module menu (search + favourites) | our own UI screen | planned |
| Cosmetics (capes) | client-side skin geometry | later |
| Discord rich presence | LeviLauncher already has Discord RPC | skip |

**Never (cheats, against server rules and our project rules):** reach/hitbox extension, kill aura, aim assist,
auto-clicker, anti-knockback, ESP/tracers through walls, fly/speed, scaffold, X-ray, anti-cheat bypasses.
