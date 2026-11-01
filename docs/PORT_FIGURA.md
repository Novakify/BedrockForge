# FiguraBridge: Figura avatars on Bedrock, coexisting with GeckoBridge (research, branch `research/geckobridge`)

Research done 2026-10-31 (cloud session). No engine hooks written: this is the plan to agree on first.
Roadmap position: **after GeckoBridge** (it reuses GeckoBridge's model/animation work). The house rule still applies:
current mods/shaders stable before any new port starts.

Goal (Fernando, 2026-10-31): every feature of Figura **and** GeckoLib working together, seamlessly, with Bedrock Forge,
Renderer Foundation and ShaderForge: one all-in-one Java-to-Bedrock client.

## Verdict (short)
- **Figura is not a GeckoLib mod and is not a GeckoBridge test case.** It ships its own Blockbench parser, its own
  keyframe animator, its own renderer and a full **Lua 5.2 scripting runtime** (luaj). It only touches GeckoLib through
  2 compat mixins (`GeckolibGeoRendererMixin`, `GeckolibGeoArmorRendererMixin`) so the two don't fight over the player.
- GeckoBridge works **offline** (mod assets -> Bedrock JSON) because GeckoLib assets are fixed per mod. Figura can't:
  avatars are user content loaded at runtime and Lua moves bones, swaps textures and spawns render tasks **every frame**.
  So FiguraBridge is a **native Bedrock Forge runtime**, the biggest native feature on the roadmap.
- It's doable, and in one way easier than on Java: Lua is a C library. Bedrock Forge can embed real Lua directly
  (no luaj port needed). The bulk of the work is re-implementing Figura's Lua API (~20 API areas) on top of Bedrock.
- The coexistence answer is one shared layer, **ModelCore** (below): one rig format, one animation evaluator, one
  draw path through Renderer Foundation. Figura avatars and GeckoBridge mobs both sit on it, so shaders, Vibrant
  Visuals lighting and the client menu treat them the same.

## Source read
`Figura.jar` = Figura **0.1.6+1.21.1, Fabric** (Architectury build, common + fabric), sent by Fernando 2026-10-31.
Inspected in the cloud session only; the jar is **not** in the repo.

| Area | What's in the jar |
|---|---|
| Size | 614 classes; nested: luaj-core/jse 3.0.8-figura, nv-websocket-client, mixinextras, 4 Fabric API modules |
| Hooks | 86 common mixins + 3 Fabric mixins: player model + all render layers (armor, cape, elytra, held items, head, parrots), entity/level/game renderer, camera, GUI (chat, tab list, pause, inventory, debug), fonts, input, particles, sound |
| Models | `parsers/BlockbenchParser2`: `.bbmodel` cubes, **meshes**, groups, textures, animations |
| Animation | `animation/*`: keyframes, loop modes, play states, interpolation **LINEAR / CATMULLROM / BEZIER / STEP**, Lua instruction keyframes |
| Rendering | `ImmediateAvatarRenderer` (own vertex buffers), 18 render types (CUTOUT, TRANSLUCENT, EMISSIVE, EYES, END_PORTAL, GLINT, LINES, BLURRY ...), render tasks (block, item, entity, text, sprite) |
| Lua API | animations, avatar, client, config, file, host, raycast, renderer, textures, atlas, action_wheel, data, entity, events, json, keybind, math, nameplate, net, particle, ping, sound, vanilla_model, world |
| Online | `backend2`: auth, HTTP, websocket, trust/permissions (avatar cloud + pings to other players) |

## Feature map

Legend: **1:1**, **approx**, **RT** = Bedrock Forge native runtime, **no** = not possible / not planned.

| Figura | Bedrock path | Status |
|---|---|---|
| Load avatars from a local folder (`avatar.json`, `.bbmodel`, `.lua`, `.png`, `.ogg`) | BF loads them unchanged from `C:\BedrockModding\figura\avatars` | RT, **goal: drop-in, no conversion** |
| Blockbench cubes, meshes, groups, per-face UV | ModelCore rig (shared with GeckoBridge geo) | RT 1:1 |
| Keyframe animations + 4 interpolations | ModelCore native evaluator (Figura maths ported, credited) | RT 1:1 |
| Lua scripts (events: tick, render, entity_init, key_press ...) | embedded Lua 5.2 (C, MIT) + C++ bindings, sandboxed | RT |
| Instruction limits / trust levels | keep Figura's per-avatar instruction caps so a heavy script can't tank FPS | RT 1:1 |
| `vanilla_model` (hide/move head, body, arms, legs, armor, cape, elytra, held items) | hooks on Bedrock's player actor renderer, bone name map `HEAD -> head`, `RIGHT_ARM -> rightArm` ... | RT approx (Bedrock rig differs a little) |
| Custom textures, texture edits from Lua, UV scroll | RF dynamic textures | RT |
| Render types (cutout/translucent/emissive/eyes/glint/lines) | RF materials - same set GeckoBridge glow already needs | RT 1:1 / END_PORTAL + BLURRY approx |
| Render tasks (items, blocks, text, sprites on bones) | RF draws Bedrock item/block meshes at bone transforms | RT approx |
| Camera, nameplate, action wheel, keybinds | BF camera hook (Cinematic already has one), BF client menu (action wheel = a page in the neon-purple menu) | RT |
| Sounds, particles from Lua | Bedrock sound engine; vanilla particles 1:1, custom via the Shine particle work | RT approx |
| `file`, `config`, `data`, `json`, `raycast`, `world`, `entity` | C++ bindings over Bedrock level/actor data | RT |
| `net` / HTTP from avatars | **off by default** (same as Figura's default trust) | RT, opt-in |
| Avatar cloud, pings, other players seeing your avatar | Bedrock servers (Hive, CubeCraft) don't know Figura | **no** - local only; only you see it (same as LyricsFlow) |
| Chat/tab-list/font emoji features | BF UI hooks | later / approx |

## ModelCore: the shared layer (how Figura + GeckoLib coexist)
One native module in Bedrock Forge. Both bridges plug into it instead of owning separate pipelines.

1. **One rig format.** Bedrock geo (what GeckoBridge emits) and Figura `.bbmodel` both load into the same in-memory
   rig: bones, pivots, cubes, meshes, UVs, locators.
2. **One animation evaluator.** Native evaluator with GeckoLib's easings (already ported in `gbanim.py`) +
   Figura's 4 interpolations. GeckoBridge keeps its **offline bake** as the default (pure resource packs, zero runtime
   cost); the evaluator is the native fallback the GeckoBridge doc listed as "not planned" - FiguraBridge needs it anyway,
   so it becomes free to use for GeckoLib mobs whose baked files get too big.
3. **One draw path.** Everything ModelCore renders goes through **Renderer Foundation** materials, so ShaderForge
   packs, Fancy and Vibrant Visuals light/shadow Figura avatars and GeckoBridge mobs exactly like vanilla entities.
   Glow/emissive is one shared material set (GeckoBridge glowmask = Figura EMISSIVE/EYES).
4. **One owner per entity.** Figura owns players with an avatar; GeckoBridge owns its converted mobs/items/armor.
   Where they meet (GeckoBridge armor/attachable worn by a Figura player) the Bedrock version of Figura's two GeckoLib
   compat mixins applies: Figura's `vanilla_model.ARMOR` visibility controls the GeckoBridge armor too.
5. **One budget.** Frame-time budget shared with LyricsFlow, Cinematic and Shine VFX: per-avatar instruction caps,
   per-frame vertex caps, all visible in the BF debug overlay. Target: no FPS dip in BedWars on the RTX 4060.
6. **One menu.** Avatar picker, trust settings and the action wheel live in the BF client menu (neon purple glass),
   next to LyricsFlow and Shaders.

## Status: F1 started (2026-11-01, branch `figurabridge/f1-bbmodel` from main)
GeckoBridge is past M5, so the "after GeckoBridge M2" gate is open. Fernando re-sent Figura.jar (same 0.1.6+1.21.1 build).
- **Done: `.bbmodel` -> ModelCore rig** (`tools/ModelCore/bbmodel.py`, tests `tools/ModelCore/test_bbmodel.py`).
  Reads Blockbench 4 (inline outliner groups) and 5 (top-level `groups`) like Figura's BlockbenchV4Model/V5Model.
  Cubes (box + per-face UV, inflate, rotation), meshes -> Bedrock `poly_mesh`, groups -> bones, locators.
  Same axis/UV rules as Blockbench's Bedrock exporter, so avatars and GeckoBridge mobs share one rig format.
- Each bone carries `figura_parent` = Figura's ParentType for its group name (copied from
  `org.figuramc.figura.model.ParentType`: prefix match, enum order, quirks kept). `BEDROCK_PLAYER_BONE` maps
  Head/Body/arms/legs/Cape to the Bedrock player rig bones the native side will follow.
- Animations are kept raw in `rig.source_meta["animations"]` for F2.
- **Next in F1:** native side (C++) - load the rig in BF, hide the vanilla player, draw through RF following the pose.
  Needs: 2 test avatars (one plain, one scripted) that Fernando made or has permission for, and a PC build/test pass.

### F1-lite test pack (2026-11-01)
`tools/FiguraBridge/avatar_pack.py` turns an avatar folder into a resource pack that replaces the player model
(vanilla skeleton, no vanilla cubes, avatar groups parented by ParentType, vanilla player animations drive it).
Tested in the cloud preview with Gakuto1112's MIT avatars FiguraAvatarTemplate + StrawberryMiku (52 bones,
135 cubes, all kept). Sent to Fernando as .mcpack files; NOT YET SEEN IN GAME. No Lua: Miku's hair physics, dress
tilt when crouching and fork item are F3/F4. To do in the loader: honour per-texture uv_width/uv_height when it
differs from the project resolution (Blockbench 4.9+), multi-texture avatars.

## Handoff trigger for the Java->Bedrock pipeline chat
Fernando has another chat waiting to fold FiguraBridge into the pipeline (`tools/JavaPort/port.py`). The signal is:
**git tag `figurabridge-ready` on `main` + `docs/FIGURABRIDGE_HANDOFF.md`** (entry points, formats, test avatars,
known gaps). It is created only when F1-F5 below pass in game: native avatar load (no resource pack), Blockbench
animations 1:1, Lua core with sandbox, wider API, coexistence/FPS pass. Until then FiguraBridge is not "100%".
The pipeline chat can check: `git ls-remote --tags https://github.com/Novakify/BedrockForge figurabridge-ready`.

## Third player-model owner: Cosmetics And Attachments Mod (CAAM)
`cosmetics_and_attachments_mod-1.0.0` by **TideBorn** (Forge 1.20.1, optional GeckoLib 4). Sent by Fernando
2026-11-01 with an MIT licence file and a PERMISSION_NOTE (his account of TideBorn's permission, not a signed grant).
Private use only, credit TideBorn, ask before anything is shared. Jar inspected in the cloud only, not in the repo.
- It is a **library**, not content: other mods register attachments through `CAAMApi` / `CAAMCoreAPI`. Parts:
  bone-mounted models on player bones (HEAD/BODY/arms/legs/DETACHED), its own player animation system
  (JSON keyframes, ease functions, loop types, pose accumulator), hide masks for vanilla parts, first-person
  full-body mode, beams between attachment points, impact rings + screen shake, "hero flight" poses, disguises,
  GeckoLib compat (GeckoLib models as attachments), server<->client sync packets.
- **Fit:** the same ModelCore rule "one owner per entity" gets a stack instead: Figura avatar (base body) ->
  CAAM attachments (mounted on ModelCore bones) -> GeckoBridge armor. CAAM's hide masks and Figura's
  `vanilla_model` both feed one per-bone visibility mask. CAAM player animations run on the ModelCore evaluator.
  Beams / impact rings reuse the Shine VFX + Cinematic screen-shake work.
- Needs a mod that actually uses CAAM to test anything visible (CAAM alone adds nothing on screen).

## Milestones (after GeckoBridge M2)
- **F0 - research (this doc).** Pick 2 test avatars: a simple one (model + 1 animation, no Lua) and a scripted one.
- **F1 - ModelCore + static avatar:** load `.bbmodel`, hide the vanilla player, draw the avatar through RF, follow the
  player's pose. Check on Fancy + Vibrant Visuals.
- **F2 - animations:** native evaluator, Blockbench animations play 1:1 (compare against Java side by side).
- **F3 - Lua core:** embedded Lua, sandbox + instruction caps, events (tick/render/init), models/animations/
  vanilla_model APIs. The scripted test avatar runs.
- **F4 - wider API:** textures, render tasks, particles, sounds, camera, nameplate, keybinds, action wheel in BF menu.
- **F5 - coexistence pass:** GeckoBridge mobs + Figura avatar + LyricsFlow + Cinematic + a ShaderForge pack in one
  world, FPS check, armor-overlap case.

## Risks
- **Size.** ~20 Lua API areas re-implemented natively; this is months, not a weekend. F1-F3 already gives most of
  what people use Figura for (custom model + animations + basic scripts).
- **Bedrock player rig ≠ Java's.** Bone names, pivots and the arm/leg layering differ; `vanilla_model` offsets need a
  measured map (one-time).
- **Scripts written for Java behaviour** (Java entity data, Java block names, Java item IDs) will return Bedrock
  values; ID translation tables needed for the common cases.
- **Render hooks are version-fragile** (same as RF); gate per game version like the other BF features.
- **Security:** avatars run code. Sandbox Lua (no `os`, `io`, `require` of native libs), keep Figura's trust levels.

## Licence / permission
- The jar as published declared **PolyForm Noncommercial 1.0.0** (upstream repo uses it too). Fernando reports
  permission from the Figura creator for this conversion; the copy he sent has its metadata changed to MIT with a
  `PERMISSION_NOTE.md` recording that this is his account, not a signed grant.
- Private, never-shared personal use fits PolyForm Noncommercial anyway. Before anything leaves his PC: confirm scope
  with the rights holders. Credit: skyrina, lumelia, UnlikePaladin, omoflop + contributors listed in fabric.mod.json.
- luaj (MIT) is not needed - we embed the reference Lua C implementation (MIT).
- Figura avatars belong to their makers; test with avatars Fernando made or has permission for.
