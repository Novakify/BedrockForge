# GeckoBridge: GeckoLib 4/5 models + animations on Bedrock (research, branch `research/geckobridge`)

Research done 2026-10-30 (cloud session). No engine hooks written: this is the plan to agree on first.
Tools: `tools/GeckoBridge/` (README there). Earlier groundwork this builds on: `tools/ModPorter` (already ports
GeckoLib mobs: Sculk Horde, Dweller, From The Caves) and branch `geckobridge/compat-audit-20261025` (audit scripts).

## Verdict (short)
- **GeckoLib's asset formats ARE Bedrock's formats.** GeckoLib reads Bedrock geometry (1.12.0 to 1.21.20) and
  Bedrock actor animations (1.8.0) and only adds a few keys. Geometry, bone names, pivots, UVs, rotation signs,
  pixel units, loop modes and almost all Molang carry over unchanged. Proven numerically below.
- The one real format gap is **easing** (GeckoLib: 30+ curves per keyframe; Bedrock: linear/catmullrom). It
  converts **offline** by baking eased segments into linear keyframes: measured max error over 534 animations of
  3 real mods = **0.06 degrees / 0.006 px** (one outlier 0.23 deg on easeInOutCirc). No runtime needed.
- What GeckoLib does in **Java code** is the hard part: which animation plays when (controllers are lambdas),
  triggered animations, bones moved or hidden by code, custom render layers. That needs per-pattern conversion
  + a small runtime, mostly **Script API + entity properties** (no C++). Native BF/RF code only for a short list.
- So the hybrid you wanted is the right shape: **~85% offline converter, ~15% runtime**, and the runtime is
  mostly a generated behaviour-pack script, not engine hooks.

## Sources read
- GeckoLib **5.5.7** (your 3 jars: Fabric/Forge/NeoForge 26.3; the format loader `com/geckolib/loading` and the easing/loop classes `animation/object` are byte-identical in all three, only loader glue and events differ) + its source
  `github.com/bernie-g/geckolib` `main` (= 5.5.7, changelog checked). Loader: `loading/definition/{geometry,animation}`,
  `animation/object/EasingType`, `animation/object/LoopType`, `loading/math/MolangQueries`, `MathParser`,
  `renderer/layer/builtin/*`, `util/RenderUtil.getEmissiveResource`, `constant/DefaultAnimations`.
- GeckoLib **4** = branch `1.21.1` (`software/bernie/geckolib`): `BakedAnimationsAdapter`, `AnimationController`.
- GeckoLib licence: **MIT** (jar LICENSE.txt). The easing maths in `gbanim.py` is a port and credits it.
- Bedrock: bedrock.dev Animations (1.26.40), MS Learn material docs, wiki.bedrock.dev materials/glowing textures.

## Test mods (3 real GeckoLib mods, all GeckoLib 4 namespace) - `scan.py` output
Downloaded from Modrinth (sha512 checked) into the cloud session only. Not in the repo.

| | Cave Dweller Evolved 1.7.0 (1.20.1) | Naturalist 5.0pre4 (1.20.1) | Sculk Horde 0.12.7 (1.20.1) |
|---|---|---|---|
| Licence | MIT | mods.toml says MIT, **Modrinth says "Custom"** - ask | Apache-2.0 |
| Geo files / bones / cubes | 1 / 18 / 33 | 30 / 374 / 375 | 50 / 633 / 682 |
| Geo features | per-face UV, cube rotation | box UV, mirror, inflate, **locators** (20) | per-face + box UV, inflate, locators, `reset` |
| Geo identifier clash | - | - | **`geometry.enderman.v1.8` = vanilla name** |
| Animations / keyframes | 13 / 405 | 334 / 19,918 | 187 / 6,574 |
| Easings | easeOut/InSine, Quad, Cubic, InOutCirc | mostly linear + 140 catmullrom, a few Elastic/Circ | **2,222 easeInOutSine**, Back, Elastic, Bounce, **step + easingArgs** (115) |
| Molang | none | 1,167 keyframes: `query.anim_time`, `math.sin/cos/clamp`, `q.target_y_rotation`, `query.life_time`, `query.vertical_speed` | 84: `query.anim_time`, `math.sin/cos`, `q.target_x/y_rotation`, `query.is_on_ground` |
| Loops | true / hold_on_last_frame / once | all three | all three |
| Markers | - | **250 sound keyframes** | 2 timeline (custom instruction) |
| Other anim keys | `geckolib_format_version` | `anim_time_update` (16), `override_previous_animation`, pre/post | `override_previous_animation`, pre/post |
| Glow | custom layer: `cave_dweller_eyes_texture.png`; legacy GL3 `glowsections` .mcmeta (dead) | 1 `_glowmask` (alligator) | **25 `_glowmask`** (AutoGlowingGeoLayer) |
| Java side (class refs) | 1 controller, `setCustomAnimations` head bone setRotX/Y, custom GeoRenderLayer | 24 renderers, `thenLoop/thenPlay`, `setAnimationSpeed` (12), head look via `setCustomAnimations` (17), `setHidden` (5), sound keyframe handler | **triggerAnim / triggerableAnim** (46/26), transitionLength (21), DefaultAnimations, several concurrent controllers, `BlockAndItemGeoLayer`, `ItemArmorGeoLayer`, GeoBlockEntity |

Also on your PC (from the 10-25 audit, not re-checked): `cave_dweller-1.6.4-neoforge-26.3.jar` (**GeckoLib 5**),
Otherworld 0.9.2.12 (GL4, All Rights Reserved), Belowland 0.2.5.6 (GL4). Asset formats are the same in GL4 and GL5;
only the Java API differs (GL5 `com.geckolib`, render states/DataTickets, `adjustModelBonesForRender`), which matters
for the controller extractor, not for the asset converter.

## Format mapping

Legend: **1:1** = same result, **bake** = offline conversion with a measured, bounded error, **approx** = close but
not identical, **RT** = needs a runtime piece, **no** = not possible on Bedrock (as far as found).

### Geometry (`*.geo.json`)
| GeckoLib | Bedrock | Status |
|---|---|---|
| format 1.12.0 / 1.14 / 1.16 / 1.19.30 / 1.21.0 / 1.21.20 | same schema; write 1.12.0 or 1.16.0 | 1:1 |
| bones, parent, pivot, rotation, mirror, inflate, cubes (box/per-face UV, uv_rotation), locators, binding, poly_mesh, texture_meshes | same keys | 1:1 |
| description.identifier | must be unique: mods reuse vanilla names (`geometry.enderman.v1.8`) | 1:1 after **rename** to `geometry.gb.<mod>.<entity>` (converter does it) |
| one model file per entity, chosen by Java `getModelResource()` (can change at runtime, e.g. variants) | client entity `geometry` map + render controller arrays | 1:1 static; variants = **RT** (property-driven render controller) |

### Animations (`*.animation.json`)
| GeckoLib | Bedrock | Status |
|---|---|---|
| bones / rotation / position / scale, degrees, pixels, Bedrock rotation signs (GeckoLib negates x,y internally for Java model space - same picture) | same | 1:1 |
| `loop`: true / false / `"hold_on_last_frame"` / `"loop"` / `"play_once"` | true / false / `"hold_on_last_frame"` | 1:1 (string names mapped) |
| `animation_length` (absent = last keyframe) | same | 1:1 |
| keyframe `{"vector": [...]}` objects, single-value channels | plain `[x,y,z]` | 1:1 (rewrite; Bedrock logs errors on `vector` objects - ModPorter E-022) |
| `pre` / `post` | same | 1:1 |
| `"easing"` + `"easingArgs"` (linear, step(n), 27 sine/quad/cubic/quart/quint/expo/circ, back/elastic/bounce(arg)) | not supported | **bake**: eased segment -> linear sub-keyframes, adaptive until error <= 0.05 deg / 0.005 px / 0.001 scale |
| `lerp_mode`/`easing: catmullrom` (GeckoLib spline uses keyframe i-2 and i+1) | `lerp_mode: catmullrom` exists but Bedrock's segment rules differ | **bake** (exact GeckoLib spline) |
| eased segment between **Molang** values | - | **bake** as `math.lerp(A, B, w)` with GeckoLib's weight w per sample (exact shape, Bedrock evaluates A/B live) |
| `anim_time_update`, `blend_weight`, `start_delay`, `loop_delay`, `override_previous_animation`, `relative_to` (GeckoLib ignores most of these) | Bedrock honours them | 1:1 copy (Bedrock may look *more* correct than Java here - note per mod) |
| `geckolib_format_version` | unknown key | drop |
| `sound_effects` keyframes (`effect: modid:sound`) | animation `sound_effects` -> client entity `sound_effects` short name -> `sound_definitions.json` + .ogg (Java .ogg plays on Bedrock) | 1:1 (converter M1; prototype drops them and reports) |
| `particle_effects` keyframes (+locator) | animation `particle_effects` -> client entity particle short names | vanilla particles: approx (name map); mod particles: need a particle port (Shine/convert_assets work) |
| `timeline` (custom instructions -> Java handler code) | Bedrock timeline only runs Molang | **RT** per pattern (usually a sound/particle/flag); unknown = drop + report |

Measured with `gbanim.measure_error` (GeckoLib maths `gl_eval` vs Bedrock linear `bedrock_eval`, 240 Hz,
jump instants of pre/post/step excluded because both sides are two-valued there):

| Mod | Anims | Max error rot / pos / scale | Keyframes before -> after |
|---|---|---|---|
| Cave Dweller | 13 | 0.23 deg (easeInOutCirc, vertical tangent) / 0.003 / 0 | 405 -> 1,604 |
| Naturalist | 334 | 0.055 deg / 0.0055 / 0.001 | 19,940 -> 21,643 |
| Sculk Horde | 187 | 0.052 deg / 0.0056 / 0.001 | 6,585 -> 55,963 (JSON 583 KB -> 2.0 MB) |

Size knob: Sculk Horde at tolerance 0.25 deg = 27,557 keyframes, 0.5 deg = 21,060, 1 deg = 16,152.
Molang-driven channels (1,161 in Naturalist) can't be compared numerically without a Molang interpreter; their
shape is preserved by construction (same w as GeckoLib).

### Molang
| GeckoLib | Bedrock | Status |
|---|---|---|
| `query.anim_time`, `life_time`, `health`, `is_on_ground`, `head_x/y_rotation`, `ground_speed`, `vertical_speed`, `yaw_speed`, `is_*` ... (79 of GeckoLib 5.5.7's 82 query constants use Bedrock query names; the other 3 are below) | same names | 1:1 (values come from Bedrock's own entity state - mob must actually move/fly etc.) |
| `math.*` (sin/cos in degrees in both), lerp, lerprotate, hermite_blend, die_roll, min_angle, random... | same | 1:1 |
| `math.to_rad`, `math.to_deg` | missing in Bedrock | rewritten to `* 0.01745...` / `* 57.29...` (done) |
| `query.limb_swing`, `query.limb_swing_amount` | `query.modified_distance_moved`, `query.modified_move_speed` | approx (done) |
| `query.controller_speed` | - | approx: 1.0, or the speed baked into `anim_time_update` (RT for dynamic speed) |
| `variable.*` / `v.*` set from Java (`MathParser.setVariable` / registered variables) | `variable.*` in client `pre_animation`, or entity **properties** (`client_sync`) read with `q.property` | **RT** (Script API writes the property) |
| GeckoLib Molang that Bedrock rejects (ModPorter E-023: lost operator `q.anim_time * 10 5` drops the whole animation) | - | converter flags `molang_suspect` in the report |

### Animation controllers (Java code -> Bedrock JSON)
| GeckoLib | Bedrock | Status |
|---|---|---|
| `AnimationController` + state handler lambda (`if moving -> walk else idle`) | animation controller states + Molang transitions | **convert by pattern** from bytecode (javap): `isMoving`, `DefaultAnimations.genericWalkIdleController` etc. ModPorter already maps idle/walk/attack. Unknown lambdas = report + per-mod overlay |
| several controllers at once (Sculk Enderman: movement + twitch + tendrils) | several entries in `scripts.animate` / several controllers | 1:1 |
| `RawAnimation.thenPlay/thenLoop/thenWait/thenPlayAndHold/thenPlayXTimes` chains | controller states chained by `q.all_animations_finished` / `q.state_time` | 1:1 by construction |
| `transitionLength(ticks)` (GeckoLib: lerp from current pose to the new animation) | `blend_transition` seconds (cross-fade between states) | approx |
| `setAnimationSpeed` | `anim_time_update: "q.anim_time + q.delta_time * k"` | 1:1 for constants; dynamic = RT property |
| `triggerableAnim` + `triggerAnim(...)` from server code (attacks, abilities) | Script API sets an int entity property (`client_sync`) -> controller state plays it -> resets | **RT** (generated script, no C++) |
| `setCustomAnimations` / `adjustModelBonesForRender` code: head follows look (setRotX/Y on "head") | an extra always-on animation: head rotation `[q.target_x_rotation, q.target_y_rotation, 0]` (how vanilla Bedrock does it) | approx by pattern (Naturalist 17x, Cave Dweller 1x) |
| same code: `setHidden(bone)` by state (saddle, chest, baby parts) | render controller `part_visibility` with Molang | 1:1 by pattern + property |
| arbitrary bone maths in code | - | per-mod overlay (hand), or **no** |

### Rendering
| GeckoLib | Bedrock | Status |
|---|---|---|
| main texture, `getTextureResource()` per variant | client entity textures + render controller arrays | 1:1 / variants RT (property) |
| `AutoGlowingGeoLayer`: `<tex>_glowmask.png` drawn as a 2nd pass. Checked in source: GL5 `EmissiveRenderType` and GL4 `AutoGlowingTexture` = TRANSLUCENT blend, full-sky light, no shading, no culling, no depth write (NOT additive "eyes") | 2nd render controller, same geometry, glowmask texture, `ignore_lighting`, RP material `gb_glow:entity_alphablend` + `DisableCulling` - **converter default "overlay", chosen as the 1:1 match**. Fallback `"glow": "alpha"`: glowmask baked into base alpha + `entity_emissive_alpha` (only if RenderDragon ignores the custom material) | 1:1 expected - confirm in game |
| custom `GeoRenderLayer` with another texture (Cave Dweller eyes) | same as above, texture name from bytecode string | approx by pattern |
| `BlockAndItemGeoLayer` / `ItemInHandGeoLayer` (item held on a bone) | Bedrock draws held items on `rightitem`/`leftitem` locators of vanilla-like rigs only | approx (bone renamed to `rightItem` + `item_hand` binding) or **no** for arbitrary bones |
| `ItemArmorGeoLayer` (armor on custom rig) | - | **no** generally (vanilla armor needs the humanoid rig) |
| `CustomBoneTextureGeoLayer` (different texture per bone) | multiple render controllers with `part_visibility` | approx |
| translucent render types (`entityTranslucent`) | `entity_alphablend` | 1:1 |
| `GeckoLibAnimatedTexture` (.mcmeta animation, frame interpolation) | no flipbook for entities: UV-offset animation in render controller (`uv_anim`) | approx (no interpolation) or RT (RF texture swap) |
| `GeoItem` (animated items), `GeoArmorRenderer` | **attachables** (geometry + animations + render controllers - same formats) | approx (first-person transforms differ) |
| `GeoBlockEntity` (animated blocks) | custom blocks can't animate geometry -> dummy entity on the block | approx |
| `GeoReplacedEntity` (re-skins a vanilla mob) | override the vanilla client entity in the RP | 1:1 |

## Converter design (offline, Python, extends ModPorter)
GeckoBridge becomes ModPorter's GeckoLib backend instead of a second porter. Stages:
1. **Scan** (`scan.py`, done): feature inventory + licence from mods.toml/fabric.mod.json -> decide early.
2. **Extract** (ModPorter `extract.py` + new GeckoLib pass, M1): per GeoModel class, the model/texture/animation
   paths (`DefaultedEntityGeoModel` = `geo/<path>.geo.json` convention; else ldc strings), controllers per entity
   (javap: controller names, `thenX` chains, DefaultAnimations constants, transition ticks, speed), triggerable
   animation names, render layers (glowmask / custom texture names), code bone patterns (head look, setHidden).
   GL4 and GL5 differ only in class/method names here - one table per version.
3. **Convert assets** (`convert.py` + `gbanim.py`, M0 done): geometry rename, animation bake + Molang fix,
   textures, glow layer, client entity, render controllers, sounds (M1), particles (M2).
4. **Generate behaviour** (ModPorter generate.py + overlays): BP entity with the mod's stats/AI, plus the
   GeckoBridge **runtime script** (below) when triggers/variables were found.
5. **Validate + prove**: ModPorter `validate.py` (0 errors on the proof), `preview.py/js` side-by-side render
   (GeckoLib maths vs converted keyframes), bake-error table in `report.json`; then the in-game check.

Naming: entity `gb:<entity>` (proof) / mod namespace in real ports, geometry `geometry.gb.<mod>.<entity>`,
animations `animation.gb.<mod>.<entity>.<name>` (fixes the `animation.model.*` collisions ModPorter hit).

## Runtime design (only where needed)
Order of preference: generated JSON > generated Script API > Bedrock Forge native > Renderer Foundation native.
1. **Trigger bridge (Script API, M2):** each `triggerableAnim` gets an index; the BP entity declares an int
   property `gb:trig` (`client_sync: true`) + `gb:trig_n` counter; mod logic ports call `gbTrigger(entity, "slam")`;
   the generated animation controller enters the state while `q.property('gb:trig') == i` and the script resets it
   after the animation length. Same for `variable.*` the Java code sets (float properties) and dynamic speed.
   Limits: Bedrock caps properties per entity (verify the current number) - pack several triggers in one int.
2. **Pattern library (generated JSON, M2):** head look, hide/show bones, walk speed scaling, sit/sleep poses.
3. **BF native (only if a test mod needs it):** GeckoLib-only queries with no Bedrock source (e.g. exact
   `limb_swing`), per-frame bone overrides from code that Molang can't express.
4. **RF native (later, optional):** per-pixel emissive strength, animated
   texture interpolation. Not needed for M0-M2.
5. **Not planned:** a native GeckoLib keyframe evaluator inside Bedrock. Baking is exact enough; only revisit if
   the keyframe growth (x8.5 for Sculk Horde) ever costs load time or FPS.

## Milestones
- **M0 (this session, done except the in-game look):** research, `scan.py`, easing baker with measured error,
  `convert.py` proof pack (Sculk Enderman: model + `idle` + `idle.tendrils` concurrent + glowmask), preview render
  showing Bedrock == GeckoLib, validator 0 errors. **Open: install + see it in game** (needs folder access).
- **M1 - static mobs 1:1:** sound keyframes, glow mode chosen in game, geometry variants, controller extraction for
  the common patterns (idle/walk/run/attack/death, DefaultAnimations), all 3 test mods converted with their
  report.json; merge into ModPorter (`--gecko` path). In game: every mob of the 3 mods spawns + animates, 0 content errors.
- **M2 - behaviour-linked animation:** trigger bridge + variable properties + head-look/hide-bone patterns;
  Sculk Horde attacks play their triggered animations; Naturalist sleep/eat/sit.
- **M3 - items, armor, blocks:** attachables for GeoItem/GeoArmor, dummy entities for GeoBlockEntity,
  particle keyframes (with the particle converter).
- **M4 - GeckoLib 5 Java side:** controller extraction tables for `com.geckolib` (render states, DataTickets);
  test on Cave Dweller 1.6.4 NeoForge 26.3 (GL5) from your PC.
- **M5 (optional):** RF emissive/eyes material, animated textures.

## Risks
- **Java logic is the real work**: controllers are code; the extractor covers patterns, odd mods need overlays.
- **Glow look**: overlay mode copies GeckoLib's blend; the risk is only whether RenderDragon accepts the custom RP material (fallback: alpha mode, also in the proof pack).
- **File growth** from baking (x8.5 keyframes on easing-heavy mods); tolerance knob exists.
- **Molang edge cases**: one bad expression makes Bedrock drop the whole animation (seen before) - the report
  flags suspects; a real Molang parser check would be safer (M1).
- **Bedrock behaviour differences** for keys GeckoLib ignores (`anim_time_update`, `blend_weight`...): Bedrock may
  play them "more correctly" than Java did; diff per mod.
- **Entity properties limit** for the trigger bridge; property changes are server->client synced (fine single-player).
- **Licences**: most GeckoLib mods are ARR; even permissive ones need credit (Apache NOTICE, MIT notice).
- **Repo is PUBLIC right now** (GitHub API says public): never commit jars, extracted assets or converted packs.

## Licences / permissions - questions for Fernando before anything is shared
- Sculk Horde (proof): Apache-2.0 -> sharing allowed with credit + licence + "changes" note (CREDITS.txt in the
  packs has it). Still your call before it leaves your PC.
- Cave Dweller Evolved: MIT (credit + notice). Naturalist: mods.toml MIT but Modrinth lists a custom licence ->
  treat as **private only** until checked. Otherworld: ARR -> private only.
- GeckoLib itself: MIT; `gbanim.py` credits the ported easing maths.

## Proof: how to install + check (needs your PC)
Files: `GeckoBridgeProofRP` + `GeckoBridgeProofBP` (sent in chat as a zip; rebuild with
`convert.py tools/GeckoBridge/specs/proof_sculk_enderman.json OUT`, jar path fixed). Copy into
`<instance>\Minecraft Bedrock\Users\Shared\games\com.mojang\development_resource_packs` / `development_behavior_packs`
(game closed - dev packs are only discovered at start), add both to a test world (or a copy of My World), then in
the world: `/summon gb:sculk_enderman ~ ~ ~3` (or the spawn egg "Spawn Sculk Enderman (GeckoBridge)").
Expected: the enderman stands still and loops its 6 s idle with the tendril sway on top, cyan glowmask parts
fullbright at night; ContentLog 0 errors. Compare with `preview_idle.png`.
