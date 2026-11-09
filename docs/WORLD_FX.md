# World FX (Bedrock Forge + Renderer Foundation) - plan

Status 2026-11-09 (evening): **nothing has run in game yet.** Built: probe v2 (RF `worldfx/rf`, also in the live RF
line `lyricsforge-cinematic/rf`, inert until its button is pressed), tag test pack v0.2 (`packs/WorldFxTagTest`),
analyzer + self-tests (`tools/WorldFx/`). Plan v2 below: **worlds, not filters**. Open before more Hive-specific
work: Hive's answer on render mods (next section). Next at the PC: the probe checklist.

## What Fernando wants (2026-11-09)
- His **Hive Bed Wars / Sky Wars games should look crazy, unique and unrecognizable** - things no Bedrock mod has
  done, "spectacular". It gets a much bigger reaction when it works on a server than in single player.
- His architecture idea: a **resource pack as the foundation** (it tags blocks and gives the base look) with RF's
  rendering built on top. The plan below leans on it even more than v1 did (alpha tags).
- **Two variants** (15:18):
  1. **Reactive Maps** - exclusive to LyricsForge: the map reacts to the song.
  2. **World FX standalone** - nothing to do with LyricsForge and no sound at all: the map itself is crazy, dynamic
     and never seen before.
- Styles he asked for (16:01): a super vibrant world; retro looks (Sonic CD / Super Mario Land era inspiration).
- 17:02: re-think the whole approach, best scenery ideas, make it ground-breaking.
- Standing tastes that apply here: no strobing or flicker, nothing cluttered, premium not cheap, previews before
  installs, skies / looks rotate by themselves instead of being picked each time, effects must not get in the way
  of fights (he asked for combat-aware lyrics for the same reason).

## Hive rules (read on Hive's own page, 2026-11-09)
Source: https://support.playhive.com/allowed-and-denied-mods/ (linked from https://playhive.com/rules).
- The page is a **whitelist**: a modification that is not listed should be assumed not allowed.
- Listed as allowed: resource packs that change the interface or animations, toggle sprint, toggle sneak, full
  bright, FOV changers, hitbox viewers that don't show through blocks, capes or other cosmetics, CPS counters,
  armor HUD, auto GG.
- Banned outright - any modification *or client that has the feature*: hacked clients, Java-to-Bedrock proxies,
  auto-clickers, entity radars / minimaps, gameplay macros, **world or asset downloaders**, x-ray / anything that
  sees through blocks, **freelook / 360 perspective**, no hurt cam, all / auto sprint, chat on kill.
- Not mentioned at all: shaders, renderer mods, DLL clients, auto re-queue, replay.

What that means here:
1. **Shaders / renderer mods are not on the list.** By the page's own rule that is "assume not allowed". Fernando
   planned to ask on Hive's Discord; that answer (in writing, from staff) is now the first thing to get - before
   more Hive-specific build time and before any clip goes public. Glass Armor is the easiest part to defend
   ("capes or other cosmetics" are listed).
2. **No camera that leaves the player's eyes.** v1 of this plan had "slow orbit camera on win": removed. The
   LyricsForge Showcase camera and the menu's camera swing fall under the same line and should stay off on Hive.
3. **Nothing from a server is written to disk.** The probe's atlas picture is opt-in (single-player), Glass Armor's
   texture dump is a debug box (off). Block / entity data the client already has is used in memory only.
4. **No effect shows anything through blocks or over walls**: everything is depth-tested against what is on
   screen; no tall beacons on beds or gens (they would mark positions over walls).
5. World FX never sends chat or commands, never removes the hurt camera, never automates an input.
6. Outside World FX but on the same page - Bedrock Forge itself has features the page names: a `no_hurt_cam`
   module (VisualModules.cpp; "No Hurt Cam" is on the banned list), Auto Sprint (re-presses sprint by itself;
   "All / Auto Sprint" is on the banned list), the menu's camera swing and the Showcase camera (freelook
   territory), and Auto Re-queue / the time changer (not on the allowed list). The page bans a client that *has* a
   listed feature, not only one that uses it. Fernando's call; proposal: a **Hive-safe profile** that removes or
   hard-disables rule-listed features whenever The Hive is detected (ServerAuto already knows when that is).

## What is actually new (honest check)
Good-looking lighting is not new on Bedrock: RenderDragon shader packs (Newb X and the deferred / Vibrant Visuals
packs) already ship nicer light, water reflections, god rays and skies, loaded through BetterRenderDragon-style
loaders (a current fork supports hot-reloading shaders). What I could not find anywhere, and what this plan is about:
- a world that **knows the match** (beds, kills, block places, your team colour) and reacts to it;
- **depth-aware art styles in Fancy** (ink outlines, soft corner shadows, world-anchored patterns, refraction) -
  material shaders alone cannot do these, they have no access to the finished frame or its depth;
- **scenery in the void**: a backdrop world around and under the islands that lights the map;
- one art direction across blocks, glass, armor, sky, HUD and events (the "Novak glass" identity).

## Approach v2: worlds, not filters
A filter over vanilla lighting always reads as a filter. A look is built as a **kit** with the same six parts:

| Part | What it does | What already exists |
|---|---|---|
| Block lighting | how every block is lit and coloured, inside the block shader where light and texture are still separate | ShaderForge packs: **LyricsLight** (directional sky light with face normals from the world position, water reflections + sun glints, glowing lamps / ores, wind in leaves; Soft / Extreme), `Anime` (cel-banded light, cool shadows), Dread, Cursed Fog |
| Screen pass | depth-aware pass before the HUD: ink lines, corner shadows, haze, grade, patterns | probe v2 finds the hook point; Glass Armor proves the in-frame copy of colour + depth |
| Backdrop | the sky and everything around / below the islands | RF's sky engine draws whole scenes in place of the game's sky (LyricsForge skies), weather particles depth-tested |
| Special surfaces | glass that bends light, armor, beds, gens | Glass Armor (all players' armor, team dye), M2 refraction glass |
| Moments | what happens on bed breaks, kills, respawn, win | LyricsForge's Director + world-space text engine; ServerAuto reads Hive chat / titles |
| HUD | hotbar, titles, kill feed in the same style | glass hotbar pack, Spatial GUI (containers as 3D panels) |

### How it draws: three layers
1. **Block shaders (ShaderForge material packs, exists and is stronger than v1 of this plan assumed).** The
   RenderChunk pixel shader gets the atlas UV, the lightmap UV, the vertex colour and the fog amount; LyricsLight
   also has the **world position** per pixel (its own vertex shader), face normals derived from it, and the camera
   position + time (`// @uniform ViewPositionAndTime`; ShaderForge can add any bgfx / material uniform by name).
   So a lot needs no depth copy at all: cel light, directional sunset light, **neon lines on every block edge**
   (from the fraction of the world position), snow / petals / frost on up-facing faces, world-anchored caustics
   and patterns, team-colour glow by alpha tag. Limits: it cannot read the finished frame or depth, and **Bedrock
   Forge cannot feed it live values yet** (the fog-colour route never reached the shaders, see VisualModules.cpp) -
   that is what the World FX bus below is for.
2. **Screen pass before the HUD (new).** Copies the world's colour + depth where the world is complete and draws
   the depth-aware part. Probe v2 answers where that is. Two candidates from the Glass Armor probe's frame
   (Fancy, 1.26.51: every world pipeline draws to RGBA8 + D24S8 with one root signature, and the frame contains a
   `Blit` and `ItemInHand*` family):
   - **the game's own Blit** - if that is the world -> back buffer copy, colour-only looks (Hyper Vibrant, CRT,
     film, palette styles) can ship as a *replaced Blit shader* through ShaderForge: no hooks, under the HUD by
     construction. Depth looks hook the same draw the Glass Armor way (game VS + our PS + extra scene / depth inputs).
   - **the first UI draw** (v1's plan) - only if depth is still intact there; the hand pass may clear it.
3. **Backdrop (sky engine, exists).** New scenes for the engine that already replaces the game's sky in frame.
   For Bed Wars they are anchored in world space (RF's camera capture has the world camera position and basis), so
   they shift as you bridge instead of sitting at infinity.
Plus the Glass Armor technique for special draws (refraction glass on the terrain's blended draws, beds).

### The World FX bus (engine piece, after M1) 🧪
Block shaders need live values to react (current look, drive block, event positions and times, team colour) and,
for reflections or refraction, extra textures. Glass Armor already builds an **extended root signature** = the
game's + extra inputs, per armor draw. All world pipelines share one root signature (Glass Armor probe), so the same
extension applied when the pipelines are *created* gives every replaced world shader a World FX constant buffer
and textures (scene copy, depth, backdrop colours) with no per-draw work: RF swaps the root signature at pipeline
creation and wherever the game binds it, and binds the extra inputs once per command list. Open questions the
probes must answer first: does the game create any world pipeline through pipeline-state streams, and which
pipelines share that root signature on Vibrant Visuals. Until the bus exists, a look's block lighting is fixed at
launch and everything live happens in the screen pass.

### Knowing what is what: tags a shader can read
v1 found tagged textures by reading the atlas back and handing the shader a rectangle table. Simpler and more
robust (Fernando's pack-as-foundation idea taken literally):
- **Alpha tags.** Opaque blocks ignore the texture's alpha. The pack stores a block class there (team block, gen
  block, bridge block ...) and the block shader reads it from the texel it samples anyway. No atlas lookup, works
  with any atlas layout. Tag test pack v0.2 tags team concrete with alpha 224..239; the probe reports whether the
  atlas keeps the value, and the in-game check is that tagged concrete looks normal.
- **A per-pixel ID in the frame's alpha.** The world target is RGBA8 and the opaque terrain shader's alpha output
  is unused, so the block shader can write its tag there and the screen pass knows what every pixel is (glow on
  gens, no ink on glass, haze over team blocks) without any table. The analyzer writes `*_alpha.png` for every
  colour snapshot to show what vanilla leaves in that channel.
- WFX1 stamps + atlas rectangles stay as the fallback and as the measuring tool (they are how the probe reports
  what the atlas did to our texels). Vanilla-content matching covers glass when a pack above ours replaces it.
- Beds and other block entities are Actor draws: Glass Armor's texture recognition handles those.

### Knowing what happens: the game's own events
v1 read Hive's chat and titles only. The client receives far more, and Bedrock Forge already hooks packet handlers
(`ServerAuto`: TextPacket, SetTitlePacket). Read-only, nothing sent:
- block updates near you (a block placed or broken, a bed gone) -> placement / break effects at the exact spot
- level events and explosions -> shockwaves, debris
- actor events (hurt, death), arm swings, entity removal -> hit sparks, slash arcs, death effects
- your own state: health, speed, team colour (leather dye, Glass Armor already reads it), respawn
- Hive's chat / titles for the match story (bed destroyed, eliminations, win)
Effects from these are drawn only where the thing is on screen and unoccluded.

### Lighting core (part of M1, shared by every look)
From the same depth copy as the ink lines, in one pass: **soft corner shadows** (ambient occlusion), **distance
haze** in the backdrop's colour (far islands sink into the air), **sun shafts** from the backdrop's sun, **glow** on
tagged emissive blocks, and the grade. This is what makes any style look expensive instead of filtered.

### Look lab: approve the art before the shader exists
- Now, without the PC: Fernando sends 2-3 of his own Hive Bed Wars screenshots -> each world / style mocked up on
  them (stills, colour-only parts exact, depth parts approximated) -> he picks the hero look.
- After the probe: real frames with depth -> the analyzer already writes `preview_outlines / preview_ao /
  preview_cel` on the probed frame; next step is a small interactive page (WebGL, the same shader maths) with
  sliders he can open on his phone and send the numbers back from.

### Quality rules
- **One hero world finished end to end** (below) before the other looks: quality over count.
- Two tiers per look: **PvP-safe** (cheap, the crosshair zone and players stay clean) and **Showcase** (for
  recording: depth of field, heavier clouds, reflections). Budget target for PvP-safe: about 1.5 ms per frame on
  the RTX 4060, measured, not guessed.
- **Combat-aware**: intensity eases down by itself when you take or deal damage and comes back in quiet moments.
- Patterns stick to the world, not the screen. A new look sweeps out from the player instead of popping.
- No strobing: storms glow slowly, flashes are single soft pulses.
- Looks and backdrops rotate by themselves (a different world each match, never one of the last three).
- Your **team colour** runs through every look: sky accent, neon edges, HUD accent, glass tint.

## Two variants, one engine
Both run on the same RF World FX engine. Every effect reads one small **drive** block each frame (energy, pulse,
flash, accent colour, match phase, event timers); the two variants differ in **who fills it** and in their own
exclusive looks. Only one drives at a time: Reactive Maps while it is on and a song plays, otherwise the standalone
look (if on).

| | **Reactive Maps** (LyricsForge) | **World FX standalone** |
|---|---|---|
| Lives in | the LyricsForge panel | Bedrock Forge client menu, Visual page |
| Driven by | the song: LyricsForge Director (`BfDirectorV1`: beat, bass, drop, cover colours, colour words, sky colour, 8-band spectrum, chapters) - already read by Glass Armor | the match: the game's events above, your movement, health, team colour, time |
| Sky | LyricsForge's own skies (his choice there: pure black void below the crosshair on sky maps) | the backdrop worlds below |
| Needs | LyricsForge running + a song | nothing - works in every match, no audio |

### Reactive Maps (exclusive to LyricsForge)
- **Beat outlines**: ink lines thicken and glow on the kick.
- **Bass blocks**: team wool / concrete breathe with the bass (alpha tags).
- **Drop flood**: on a drop the map floods into the cover's palette for a bar; glass bends harder.
- **Colour-word world**: "pink", "blue" in the lyrics recolour the map (LyricsForge already does it for the lyrics).
- **Chapter styles**: verse = anime cel, chorus = neon, outro = fades to manga ink.
- **Spectrum skyline**: the far horizon band becomes the song's EQ.
- **Black hole void** (Sky Wars) throbbing with the bass.
- Side benefit of the depth copy: LyricsForge's existing world light (sky colour on the ground) can mask by real
  depth instead of guessing the ground from the horizon line, as `postfx.hlsl` has to today.

### World FX standalone (no LyricsForge, no sound)
- **Backdrop worlds** and **moments** below, plus the **style bank**.
- **Living match**: the map's mood escalates by itself - golden hour at the start, dusk as beds fall, a starry or
  eclipse sky for the last fight.
- **Movement**: sprint speed lines; every block you place materialises with a hologram scan line; your bridge
  leaves a fading light trail; Sky Wars "danger lens": the void warps more the closer you are to an edge.

### Backdrop worlds (scenery bank, standalone)
In Bed Wars and Sky Wars a large part of the screen is empty sky. That emptiness is the biggest canvas there is.
Each backdrop is a procedural scene for the sky engine (art only from assets Fernando supplies), world-anchored,
drawn behind the islands, and tied into the map by four tricks: **it lights the map** (its colour on surfaces that
face it: lava glows on the undersides of islands, an ocean bounces cyan), **the map marks it** (island shadows on
cloud tops, reflections in water - from what is on screen), **shared haze**, and **weather particles** in front.
- **Cloud Sea, golden hour** (hero pick): anime-film sky. The islands float over a rolling sea of cloud, towers of
  cumulus catch the low sun, light shafts cut through the gaps, islands throw soft shadows on the cloud tops. Fall
  into the void and you sink into cloud. Dusk and a starry-night version come free with the time arc.
- **Mirror Ocean**: glass-calm water far below reflecting the islands and a sunset; bridges look like they cross
  the sky.
- **Orbit**: a planet's curve below with an aurora on its rim and a ringed moon rising; the Sky Wars version is his
  Event Horizon black hole.
- **Abyss**: the whole match under water - light ripples slide over every block (world-anchored caustics), shafts
  from the surface, drifting motes, huge shadows passing far away.
- **Lava World**: a lava sea whose glow lights the undersides of the islands, embers rising past the bridges, heat
  shimmer near the edges.
- **Neon City**: night, rain, a city grid far below; up-facing blocks look wet and reflect the neon.
- **Sakura Dusk**: pink cloud banks, petals drifting and settling on the islands, team wool glowing like lanterns.
- **Frozen Aurora**: snow settles on top faces as the match goes on, an ice sea below, slow aurora above.
- **Storm Front**: a dark cloud sea lit from inside by slow, soft glows (no flashes), rain streaks, wet surfaces.
- 🧪 **Giants**: something colossal far away - sky whales, a ringed planet's shadow, floating mountains with
  waterfalls. Needs his art or a lot of care to not look cheap.
- 🧪 Per-map art direction: recognise the Hive map and give it its own backdrop.
Settling effects (snow, petals, frost, moss, sand) are one block-shader trick: up-facing faces (normal from the
world position) + world-anchored noise; building up over the match needs the bus (or a slow time ramp).

### Moments (standalone; all cosmetic, all only where visible)
- **Your bed breaks**: the world drains to grey except team colours, letterbox, one soft shake. **Their bed**: a
  crack of light in the sky in their team colour.
- **Respawn**: you die into a grey ghost world; colour floods back as a wave from your spawn when you return.
- **Your kill**: the victim bursts into glass shards (matches the armor) with an air shockwave.
- **Words in the world** (the lyric text engine): comic "KRAK!" when a bed breaks in Comic, score popups on kills in
  16-Bit, clean neon in the glass look. Cinematic title cards replace the plain "BED DESTROYED" text.
- **Flow State**: a kill streak brings an aura on your own arms and sword, speed lines and deeper ink - a nod to
  his Blue Lock pack. Fades when the streak ends.
- **Gens and beds**: gens shimmer with heat and glow; your bed glows like a hearth and cracks when it goes. Nothing
  taller than the block itself.
- **Explosions**: TNT and fireballs get a refraction ring and embers.
- **Void death**: you sink into the backdrop (cloud, water, the black hole's stretch) instead of just dying.
- **Victory poster**: on a win the last frame is frozen, styled and titled, and saved as a PNG - a ready thumbnail.
  (A still, not a camera move: orbit cameras are freelook under Hive's rules.)
- **Shop hologram**: Spatial GUI already floats container screens in the world; the Hive shop as a glass panel
  next to the shopkeeper (to be tried on Hive - their pack restyles that screen).

### Style bank (shared engine: standalone rotates or picks, Reactive Maps switches them with the song)
Each style is a kit (block lighting + screen pass), not only a post filter. Fernando asked for a super vibrant look
and retro looks.
- **Hyper Vibrant** (his idea): vibrance (dull colours boosted most, team colours protected from clipping), local
  contrast on luminance, shadows leaning violet / highlights warm, bloom on saturated colours, distance haze tinted
  pastel instead of grey. Pixar / Fortnite-level pop without the cheap blown-out look.
- **Anime** (the hero world's style): cel light in the block shader (exists), thin tinted ink lines, corner shadows.
- **Novak Glass** (signature): the client's neon purple + white glass identity as a world - dark glossy surfaces,
  neon lines on every block edge (block shader, from the world position) in the team colour, frosted depth haze,
  refraction glass everywhere, glass armor.
- **16-Bit** (Sonic CD era inspiration, nothing copied): low internal resolution, limited vibrant palette with ordered
  dithering, 1-px outlines at that resolution, depth quantised into parallax-style layers, banded gradient sky.
- **Handheld** (Super Mario Land era inspiration): 4-shade green or grey palette, chunky pixel grid + LCD cell lines,
  dithering, a touch of LCD ghosting (blend with the last frame).
- **CRT Arcade**: scanlines, slight curvature, phosphor glow, colour bleed, vignette; stacks on 16-Bit.
- **Comic** (Spider-Verse vibe): bold ink, halftone dots in shadows, CMY misregistration that grows with distance.
- **Manga**: black and white, screentone, ink; kills as a single impact panel.
- **Cinematic Film**: teal-orange grade, halation (red glow round highlights), anamorphic streaks on bright lights,
  grain, optional letterbox.
- **Storybook**: painterly (Kuwahara) shapes, soft watercolour edges, paper grain - hand-painted anime backgrounds.
- **HD-2D**: crisp pixels + strong depth of field + bloom + light shafts (Showcase tier).
- **Toy World**: tilt-shift, Sky Wars islands as dioramas (Showcase tier).
- **Stained Glass**: the world split into glowing world-anchored cells with dark lead lines.
- **Blueprint**: the map becomes a technical drawing - white edge + grid lines on blue.
- **Cotton Candy**: soft pastel grade, bloom, slow sparkles.
- **Matrix / ASCII** (fun mode): the frame drawn as glyphs (LyricsForge's glyph atlas).

### Shared by both
The lighting core, refraction glass, reflections on glossy floors / water, team-colour light bleed, the tags.
🧪 later: curved horizon (own vertex shader), 3D-deep block textures (parallax), real sun shadows (needs the
terrain drawn a second time from the sun - the same hard frontier as mirrors).
Never in either: anything that shows players or blocks through walls, live views of other places, cameras that
leave the player's eyes.

## M0: the probe (v2, built 2026-11-09 evening)
RF `src/WorldFx.{h,cpp}`: F6 > **World FX** > **Probe the next frame** (needs `"uiCaptureSpike": true`, already on
for Glass Armor). It waits for the shader names, warms the trackers for 2 frames, records 1 frame and writes
`mods/RendererFoundation/logs/worldfx/<time>/`:
- `report.txt`: the **frame timeline** (runs of draws per command list in order, with their colour / depth
  targets), every RenderChunk pipeline (vertex shader output signature = what a replacement pixel shader must
  accept, blend, depth, draws), every other pipeline merged by shader + state, textures bound by terrain draws,
  the camera capture, the materials RF served from packs.
- **Stage snapshots** `stageN_before_<Family>_colour / _depth`: colour + depth right before the first draw of the
  families that follow the terrain - always the hand pass (`ItemInHand*`) and the game's `Blit`, plus up to three
  others - and `colour_at_ui / world_colour / world_depth` right before the first UI draw. Each `.raw + .json + .bmp`.
- `stamps.json`: where the tag pack's WFX1 stamps sit in the atlas candidates (searched in memory).
- `atlasN.raw / .bmp`: **only when the F6 box "Also save the block texture sheet as a picture" is ticked.**
  Single-player test worlds only: on a server the atlas holds the server pack's textures.
Safety: inert until the button is pressed; game render targets are never held after the copy; stale tracker
pointers are only touched under SEH; readback buffers are released a few seconds after the files are written.

Why v2 (three blind spots in v1, found on review before anything ran):
1. Terrain was recognised by the game's own pixel-shader names only. LyricsLight replaces RenderChunk's pixel *and*
   vertex shaders, so with his usual shaderpack on v1 would have reported "no terrain". RF's MaterialLoader now
   names the shaders of every material it serves from a pack (`learnServedMaterial`), and an unreplaced vertex
   shader is the second fallback.
2. v1 copied the world only at the first UI draw. If the hand pass clears depth, that copy holds just the hand.
   The stage snapshots + timeline show where depth is last complete and which draw is the right hook.
3. v1 always wrote the atlas picture - on Hive that would have put their pack's textures on disk.

Analyzer (no game needed): `python3 -I tools/WorldFx/analyze_probe.py <probe folder>` -> `analysis/verdict.md`:
frame order, every snapshot as PNG (+ `*_alpha.png`), **which snapshot still holds the complete world depth**,
`preview_outlines / preview_ao / preview_cel` on that exact frame, terrain passes (opaque / cutout / blended, by
state), tags found per group, **whether the alpha tags survived**, and how the atlas stored our texels (exact /
premultiplied) when its picture was saved.

Checked in the cloud (2026-11-09): `tools/WorldFx/test_stamps.py --cpp <rf>/src/WorldFx.cpp` - all 72 textures
carry their id, the 16 alpha-tagged ones carry their tag and the soft stamp reports it, a fake atlas with edge
padding finds all 72 rectangles exactly with no stray hits, BGRA order works, and the C++ `findStamps` compiled
standalone finds the same stamps as the Python port. `tools/WorldFx/fake_probe.py` writes fake v2 probes (ray-cast
Bed Wars island with the real textures): the analyzer reads the normal case and the awkward one
(`--hand-clears-depth --no-atlas-picture`) end to end and names the right snapshot. RF compiles green in CI (`acd4679`, clang-cl).
Not checked, because it cannot be without the game: every D3D12 path of the probe itself.

Already known from the Glass Armor probe (2026-11-06, Fancy, `docs/glass_armor/probe_m0_20261106.txt` on
`research/glass-armor`): world pipelines draw to RGBA8 + D24S8 (standard Z) with one root signature; the
RenderChunk vertex shader hands the atlas UV to the pixel shader as `TEXCOORD0.xy`; blended terrain uses
`less_eq, no depth write, blend on`, cutout terrain `cull none`.

What M0 must answer in game:
1. Frame order: where do the hand pass, the game's Blit and the first UI draw sit, and into which targets?
2. Where is the world's depth last complete? Same size as the colour (render scale)?
3. What is in the colour target's alpha channel at those points - is it free for a per-pixel ID?
4. Atlas: plain RGBA8? One texture or an array? Do stamps and **alpha tags** survive exactly? Does tagged concrete
   look normal in game?
5. Which terrain pass draws stained glass (blended, expected) and plain glass (cutout, expected)?
6. On Hive (only once Hive has OK'd render mods): the same in a lobby and a Bed Wars match; does Hive's pack
   replace glass / wool / concrete textures (stamps missing)?

## M0 test checklist (at the PC, Fancy first)
1. Game closed. Install the live RF build `lyricsforge-cinematic-rf-builds` built from `acd4679` or later
   (BUILD_INFO.txt; back up the old `RendererFoundation.dll`).
2. Copy `packs/WorldFxTagTest` (v0.2) into the instance's `resource_packs`, turn it on as a global pack at the top.
3. **Single-player test world**: stand where plain glass, stained glass, panes, wool and **concrete** are in view,
   holding an item, HUD on. Look: does concrete look exactly like normal concrete? Esc > F6 > World FX > tick
   "Also save the block texture sheet as a picture" > "Probe the next frame" > wait for "done - report written".
   Once with the usual shaderpack on, once with shaderpack off (vanilla shaders).
4. Run the analyzer on each folder; read `verdict.md`, look at the previews.
5. Hive (after Hive's OK): **box unticked**; once in the lobby, once in a Bed Wars match with glass in view. Then
   once on Vibrant Visuals.
6. The probe stays in the live line (inert until the button is pressed); nothing to undo.

## Milestones (v2)
- **M0 probe v2** - built, run it at the PC (checklist above). **H0: Hive's answer on render mods.**
- **L0 look lab**: mock-ups on his own screenshots now; real frames after the probe. He picks the hero world.
- **M1 screen pass core** at the hook point the probe names: colour + depth copy, one pass = corner shadows,
  haze, sun shafts, ink lines (skip glass via the ID, thin far away, tinted not black), grade (Hyper Vibrant
  first). The drive block exists from here. Toggle + strength in the BF menu Visual page, F6 mirror. If the Blit
  route works: colour-only styles as a replaced Blit shader first (fastest thing to see in game).
- **H1 hero world, end to end (standalone)**: Cloud Sea backdrop (world-anchored, lights the map) + Anime block
  lighting writing tags + M1 + three moments (your bed breaks, respawn colour wave, victory poster) + team colour.
  PvP-safe and Showcase tiers, combat-aware. Preview approved before each in-game install.
- **M2 refraction glass** on the terrain's blended draws (Glass Armor's flush + extended root signature).
- **Standalone track**: S1 event reader (packets + chat / titles -> `BF_GetWorldEvents`, like `BF_GetDirector`) and
  the remaining moments; S2 more backdrops; S3 more styles; S4 living match + movement effects.
- **Reactive track**: R1 drive from `BfDirectorV1` (beat outlines, bass blocks, drop flood); R2 chapter styles +
  colour-word world; R3 spectrum skyline + black hole void.
- Then Vibrant Visuals, then a Hive pass (lobby, Bed Wars, Sky Wars, FPS on the RTX 4060).

## Files
- `tools/WorldFx/wfx_stamps.py` - WFX1 stamp format (stamp / find / pair into rectangles), mirrors WorldFx.cpp
- `tools/WorldFx/make_tag_pack.py` - builds `packs/WorldFxTagTest` v0.2 (72 vanilla textures: glass, panes, team
  wool, team concrete with alpha tags, oak planks, end stone, obsidian, diamond / emerald block) + `tag_ids.json`
- `tools/WorldFx/test_stamps.py` - self-test (+ `--cpp` parity check against RF's C++)
- `tools/WorldFx/analyze_probe.py` - probe folder -> verdict, previews, tags.json
- `tools/WorldFx/fake_probe.py` - fake v2 probes for testing the analyzer
- RF `src/WorldFx.{h,cpp}` on `worldfx/rf` (merged into the live RF line)
