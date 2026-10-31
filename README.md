# Renderer Foundation

RenderDragon enhancement mod for **Minecraft Bedrock 1.26.51.01**, built on LeviLamina.
A modernized fork of [BetterRenderDragon](https://github.com/ddf8196/BetterRenderDragon) by ddf8196 and
contributors - **AGPL-3.0** (see `LICENSE`, `NOTICE.md`).

## Features
- **Shaders from resource packs** - `renderer/materials/*.material.bin` inside your active resource packs replace
  the game's shaders (e.g. Java -> Bedrock converted packs that ship Bedrock shaders).
- **Shaderpack folders** - drop packs into `mods/RendererFoundation/shaderpacks/<Name>/` and pick one in the overlay.
- **Reload shaders** (experimental on 1.26) - button or a configurable key.
- **Vibrant Visuals toggle** - switches the game's own graphics mode where the game allows it.
- **ImGui overlay** - `F6` to show/hide; performance, shader and graphics panels, recently served materials.
- **Spatial GUI + Cinematic Interactions** (ports of the Java mods by tastytrash and Southware, MIT) - container
  screens float in the world, and work stations (crafting table, furnace, smoker, blast furnace, anvil, grindstone,
  ...) play as cinematic scenes: the camera leans in, the vanilla screen is hidden and you work on the block itself.
  Per station: Cinematic / Spatial / Vanilla in `F6 > Cinematic Interactions (stations)`; a scene that cannot play
  where you stand falls back to Spatial GUI. The anvil's hammer is "Just a Hammer" by RDiamonds (CC BY 4.0).
  Credits and licences: `NOTICE.md`.

Load order inside a pack folder: resource packs > selected shaderpack > vanilla.

## Install (LeviLamina 26.51.x, client)
Copy the `RendererFoundation` folder from the `rf-builds` branch into `<instance>/mods/`. Keep a backup of anything
you replace. The overlay needs the mouse free (open the pause menu, then press F6).

## Build
`xmake f -a x64 -m release -p windows -y --target_type=client && xmake` (clang-cl, LLVM 22). CI does this on push.

## Status
Alpha: compiles in CI; not yet verified in game. See `NOTICE.md` for what differs from upstream.
