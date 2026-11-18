# Loadouts

One click sets up a whole play style. Open the menu (Right Shift), **Loadouts** is the first tab.

## Easiest way (no file editing)
1. Set things up the way you want: shaderpack, Vibrant on/off, modules, which mods are on, which DLL build.
2. Loadouts tab -> type a name (e.g. `cinematic`) -> click **+ Save as "cinematic"**.
3. Later, click that card and everything goes back to exactly that setup.

The save pins each mod's exact DLL. If that DLL only exists as the live file, a copy is made next to it
(`<Mod>.saved-<hash>.dll`) so the loadout can always bring it back.

## What applies when
| Part | When |
|---|---|
| Module on/off + settings, Vibrant Visuals | instantly |
| Shaderpack | ForgeRender reloads materials right away; if it looks half-switched, restart |
| Mods on/off, DLL builds | next launch (the status line says **RESTART to finish**) |

LeviLamina and Bedrock Forge itself are never switched off or swapped.

## Editing by hand
`mods\BedrockForgeCore\loadouts\<Name>.json`. Every key is optional; anything left out stays as it is.
```json
{
  "description": "Anime shaders + cinematic",
  "shaderpack": "Anime",
  "modules": { "shaderpacks": { "settings": { "vibrant": true } }, "zoom": { "enabled": true } },
  "mods":    { "RendererFoundation": true },
  "dlls":    { "RendererFoundation": "RendererFoundation.sgm1-4d7df40.dll" }
}
```
- `mods`: folder names under `mods\`. Off = its `manifest.json` is renamed to `manifest.json.bfoff`.
- `dlls`: a file inside that mod's folder; it becomes the mod's real DLL (the name in its manifest `entry`).
  The running DLL is renamed aside first (Windows allows that) and the temp copy is removed on the next launch.
