# Cloud Sea -> Vibrant Visuals port: where it stands (paused Nov 26 2026, 23:40)

Branches (all pushed, nothing merged into the live lines yet): RF `cloudsea-vv/rf` (builds: `cloudsea-vv-rf-builds`),
BF `cloudsea-vv/bf`. Test build kept on the PC as `mods/RendererFoundation/RendererFoundation.cloudsea-vv-9099019.dll`
(the installed DLL is back to cc25d38). Design notes: RF `docs/VV_LIVE.md`.

## Seen working in game (VV, test world)
- Cloud Sea sky in VV: gradient, sun, high clouds, cloud sea, moon, stars. 120 FPS.
- Sun / moon stand at the game's own compass directions (read from VV's uniforms), so VV's light fits the sky.
- VV Live patches all three groups every frame (0 missed): direct/ambient light, sky colours (lighting, sky probe,
  water), fog volume. Table of 74 shaders generated from the game's materials (`tools/gen_vvlive_table.py`).
- Match layer over VV's HDR frame: team beacon, Figura bed, bed break + shockwave, kill animation (FPS dips to ~90-107
  while one plays).
- Pack layer loads with no content-log errors: atmospherics, lighting, fog, grade, 83 biomes, 266 block texture sets,
  38 equipment texture sets. Height maps visibly shade end stone / planks.
- The cloud sea now covers ground below the deck (build 9099019).

## Not right yet
1. **Brightness.** VV pre-exposes its frame: `colour * (0.18 / lastFrameAverageLuminance + 1e-4)` (read off the
   DeferredShading shader). A fixed sky gain is right at one exposure only: golden hour OK at 0.3, dusk too dark,
   night a blue whiteout. Fix written and pushed (commit after 9099019, "VV exposure probe"): one pixel drawn on the
   game's root signature at its sun/star draw copies the 1x1 luminance texture (t0 there) into a texture of RF's; the
   sky and the match layer multiply by the same factor. **Never run in game yet.** `sky_gain` then becomes an absolute
   scale and needs one calibration (start around 1-5).
2. Not verified by eye: the crit impact frame (0.25 s, too fast for my screenshots), the fog deck from inside,
   bed-break air pulse, final-fight rising clouds, equipment texture sets, wool "cloud" look up close.
3. White "+" flakes in the air in VV (most likely the Cloud Sea pack's own spark particles drawn unlit) - check.
4. Not ported: haze / mist / ambience, motion blur, the menu panorama grab in VV (they read an 8-bit frame).
5. Art-director passes on real frames (none done yet), then merge both branches into the live lines.


## Nov 27 session (in game)
- **Exposure probe works.** Golden hour, dusk and the moonlit night all read right with one setting (sky_gain 12).
  Sheet: Research/VibrantVisuals/captures (vv_cloudsea_phases.jpg).
- **VV Live colour patches land** (proved with `set debug 1`: the ambient on the blocks turns the debug magenta).
- **Night**: the bright moon sky pulled VV's exposure down; fixed by a dimmer night sky (night_sky 0.12) plus
  moonlight x4 / ambient x2. Dark blocks (planks, stone) are still moody at night - VV's look, not a bug.
- **Bed break in VV**: beacon, Figura bed, shockwave ring, pieces, team-colour air pulse all seen. FPS 127-170 during it.
- **White "+" flakes** = vanilla snow (rain turns to snow at that height). Not ours. Test world now doWeatherCycle false.
- **Cloud-deck fog (VV Live fog volume): NOT working.** The writes land in the buffer the Populate shader reads
  (values confirmed in the log), but no visible change even at 100x density. Under investigation; next step is a PIX
  capture of PopulateVolume to see what it actually reads. The pack's own fog works (seen as haze).
  The RF cloud sea already covers everything under the deck, so this only matters when you fall into it.
- Still unverified: crit impact frame (0.25 s), final-fight rising clouds, kill animation recheck at the new exposure.

## Facts learned (Nov 26, corrections to VV_TESTED.md)
- Fog `max_density` is capped at 1.0 by the schema (1.6 invalidated the whole fog file). Thickness comes from the
  scattering coefficients. The earlier "a deck needs 1-3" was wrong.
- Fog heights in the pack are plain world y; the engine's HeightFogScaleBias = (-1/T, (zero - cameraY)/T).
- The game lights the world with the DeferredMixedResolution family, not DeferredShading.
- SetComputeRootConstantBufferView is command-list vtable slot 37.
- Auto-exposure: average of the brightest 20% of pixels, log-luminance clamped to [-3, 4] (data/renderer).
- BF's Sky module (modules.json sky.mode: 0 Living Skies, 1 Cloud Sea, 2 Novak Glass) picks the atmosphere; WorldFx.txt
  `cloudsea` alone gets overwritten.
- Test without the F6 panel: write one line into `mods/RendererFoundation/config/Test.txt`:
  `phase 0|1|2`, `bed`, `kill`, `impact`, `vv` (status into the log), `set <key> <value>` (VvLive.txt keys).

## To resume
Game closed -> `~/inst.sh` in the device VM (DLL from cloudsea-vv-rf-builds + pack into the test world) ->
options.txt graphics_mode 2, modules.json sky.mode 1, world_resource_packs.json with pack c10d5ea0... v0.3.0 ->
launch, world "test" (BFKnivesTest), platforms at 8..24, 226, -84..-58 with the material board at z -70.

## Idea bank: things nobody has done (added Nov 26 night)
Each idea names the lever it rides on. "Proven" = that lever already ran in the game today. "Theory" = the lever
exists in VV's files but RF has not driven it yet. None of these is built. Left out on purpose (already rejected):
low-health effects, hit slashes, effects on normal swings, footstep ripples, generator pulses.

### A. The match plays the weather (levers proven: VV Live writes light / sky / fog uniforms every frame)
1. **Sun that sets on the losing side.** The sun's compass direction drifts toward the island of the team with the
   most beds broken, so the last light of the match falls across their base. (Sun direction is ours to place now;
   moving VV's real shadows with it needs the shadow cascades driven too - theory.)
2. **Your bed is the lighthouse.** While your bed stands, the fog around your own island is thinner and warmer; when
   it breaks the deck closes in on you first. Per-island fog needs a shaped density (see C) - theory.
3. **Kill-streak golden hour.** A run of kills pushes the key light toward molten gold for a few seconds, then it
   cools. Light hue is a proven lever; tie it to the existing kill-run value. Subtle, not a flash.
4. **Final-two eclipse.** When two teams are left the moon slides across the sun: light drops, a corona ring in the
   sky, stars come out, fog goes silver. Sky is ours (proven), light + fog hue proven.
5. **Victory dawn.** The winner gets a real sunrise: the phase runs backwards from night to gold in ~6 s, the deck
   drops away and the fireworks play over it. All proven levers, only choreography.
6. **Overtime storm.** Sudden death: the cloud sea rises to just under the islands (final_rise), lightning lights
   the deck from inside (fog albedo pulse = the bed-break pulse reused), thunder from the existing sound path.

### B. Light that belongs to the players (lever: VV's clustered point lights, 64 on high - theory, not driven yet)
7. **Team-coloured bed glow as real light.** Each bed is a real VV point light in its team colour: it lights nearby
   blocks, players and fog, and reflects off glazed blocks. Goes out when the bed breaks.
8. **Fireball / TNT as travelling lights.** Projectiles carry a real light through the fog deck, so a fireball
   crossing the void lights the clouds under it.
9. **Bridge wake.** The last few blocks you placed hold a faint light of your team colour that fades in 2 s - a
   speed-bridge leaves a glowing trail on the clouds below. (Placement events already reach RF.)
10. **Diamond / emerald generators as lanterns.** Mid and island generators cast their gem's colour into the fog,
    so you can read from across the map where the emeralds are. A steady light, no pulse.

### C. Sculpted air (lever: the fog volume pass; shaping it means replacing PopulateVolume's ramp - theory)
11. **Cloud bridges.** Where the deck is under an island gap, the fog humps up into an arch you can see from above:
    it hints the shortest bridge line without drawing UI.
12. **Wake in the clouds.** Falling players and thrown pearls punch a hole in the deck that slowly closes; the hole
    is lit from below. A kill into the void leaves a visible wound in the sea for a few seconds.
13. **Island shadows on the sea.** Each island drops a soft, real shadow onto the cloud tops (the sea shader already
    knows the map centre and bed positions; shadows from bed-anchored blobs are cheap).
14. **God rays that point at the fight.** Shafts through the high clouds open over wherever the most players are.

### D. Materials that react (lever: texture sets proven loading; live changes via RF are theory)
15. **Wool that drinks the sky.** Cloud-wool's subsurface colour follows the phase: peach at golden hour, rose at
    dusk, pearl-blue under the moon. Pack-only version: three time-keyed looks through VV's lighting, no RF.
16. **Frosted glass that fogs with altitude.** Glass near the deck picks up a breath of condensation.
17. **Sunstone that charges.** Weapons hold more inner glow the longer they've been in sunlight this match and
    discharge on a crit (ties into the impact frame that already exists).
18. **Wet stone after the storm.** After the overtime storm, stone and concrete go glossy (roughness down) and
    mirror the sky for the rest of the match.

### E. Camera and frame (levers: engine configs proven; exposure now understood)
19. **Breath-hold exposure.** Entering the deck dims the world a stop and coming out blooms for half a second, like
    walking out of a tunnel. Exposure is the game's own; nudging it through the luminance it measures is theory.
20. **Death cam into the clouds.** On your final death the camera sinks through the deck: thick fog, then the stars
    of the fallen above you. Sky + fog only.
21. **Record mode.** One switch for recording: locks exposure (no pumping in clips), raises fog-grid resolution
    (4x4 tiles / 128 slices ran at the 120 cap), turns bloom up a touch. Config-level, proven levers.

### F. Pack-only, works for anyone on Hive with VV (no client)
22. **Cloud Sea Lite.** The sky colours, warm light, moonlit night, deck fog and block materials as a plain resource
    pack. Already built (packs/WorldFxCloudSea); needs its own art pass since the game's clock runs it.
23. **Biome-coded maps.** Different atmosphere per biome id, so a map built across two biomes gets two skies.

### Suggested order once the port itself is done
3 and 5 first (pure choreography on proven levers), then 7 (point lights: the biggest visual jump per effort),
then 12 and 13 (the sea reacting to players), then 4 and 6 as the set pieces.

### G. Blocks, weapons, bows / arrows (added Nov 26, 23:45)
Rule for all of these: looks only, no information the vanilla player doesn't have (no aim lines, no seeing through
walls) - Hive bans that and it isn't the point.

Blocks
24. **Sunlight through the cracks.** While you mine a block, its crack stages glow from inside like sun leaking
    through (emissive MERS on the destroy-stage textures). Pack-only if VV accepts texture sets on destroy stages -
    theory, quick to test.
25. **Wool that breaks into weather.** A broken cloud-wool block bursts into a puff that drifts down, thins, and
    sinks into the cloud sea instead of the classic shards (match layer already draws shards in VV).
26. **Obsidian as black mirror.** Bed defence obsidian becomes polished black glass that reflects the Cloud Sea sky
    (texture set: roughness ~0.05). Pack-only, proven lever.
27. **End stone as moon rock.** Craters in the height map, faint silver subsurface at night. Pack-only.
28. **Edge wisps.** Blocks at the void edge of an island trail thin cloud over the drop (fog shaped near island
    edges - theory, same lever as C).

Weapons
29. **Blades that hold the real sky.** Porcelain / sunstone edges are metal in VV, so they reflect VV's sky probe -
    which VV Live already recolours. Swing toward the sunset and the edge catches it. Mostly free once checked.
30. **Crit contrail.** Crits only: the blade leaves a 0.3 s vapour streak lit by the sun, inverting with the impact
    frame. (Normal swings stay clean.)
31. **Tally stars.** Each final kill this match etches a tiny star into your weapon's glow; they reset next match.
    Classic item shader can do it; VV needs the item material path - theory.

Bows and arrows (BF already sends projectile trails; the VV match layer draws them)
32. **Arrow contrails.** Arrows draw thin cloud contrails that linger ~1.5 s, drift with the wind and are lit by the
    sun on one side; the edge takes the shooter's team colour.
33. **Charge flare.** Drawing the bow builds a small glow at the arrow tip; full charge = a tiny sun flare. Needs the
    bow's charge from BF (not sent yet).
34. **Void shots ripple the sea.** An arrow that falls into the void lands as a ring spreading across the cloud tops
    far below.
35. **Pearl landing burst.** Ender pearl lands in a soft cloud burst that hangs a moment (pairs with idea 12).
36. **Snowballs / eggs as cloud puffs.** Tiny puffs instead of the vanilla particles.

### H. Every idea follows the sky (Fernando, Nov 26 23:43)
Rule: nothing has one fixed look. Every effect in A-G reads the same sky state as the Cloud Sea itself and changes
with it - day effects are lit like day, night effects contrast the moonlit night, and the eclipse and the storm each
get their own version. One shared input drives all of them (RF already has it: the phase 0 golden -> 1 dusk -> 2 night,
plus two new states), so they all shift together and never disagree with the sky.

Sky states: **Day** (golden hour), **Dusk**, **Night** (big amber -> silver moon), **Eclipse** (final two teams),
**Storm** (overtime). Transitions blend over the same time the sky takes; nothing pops.

| Idea | Day | Dusk | Night | Eclipse | Storm |
|---|---|---|---|---|---|
| Sunlight through cracks (24) | warm gold leaking out | deep amber | cold moonlight, silver-blue | corona-white thin ring of light | flicker with the lightning |
| Wool breaks into weather (25) | sunlit cream puff | rose puff | pearl-blue puff with a moonlit rim | puff goes dark, edge-lit by the corona | grey puff torn sideways by wind |
| Obsidian mirror (26) | reflects the gold sky | reflects the afterglow | reflects the moon and stars (the contrast piece) | reflects the ring | reflects lightning only |
| End stone moon rock (27) | pale, sun-baked | warm grey | silver subsurface glow | dim, ring-lit | wet, darker |
| Blades hold the sky (29) | sunset streak on the edge | ember edge | moon glint, cool steel | a single white ring glint | blue-white flashes on each strike |
| Crit contrail (30) | sunlit vapour | pink vapour | silver vapour, slight glow (stands out against the dark) | black vapour with a white rim | sparks instead of vapour |
| Tally stars (31) | faint gold flecks | brighter | fully lit like real stars | blaze white | crackle |
| Arrow contrails (32) | lit gold on the sun side | rose | moonlit silver with the team-colour edge glowing (reads clearly at night) | dark trails edged by the corona | torn short by the wind, rain-streaked |
| Charge flare (33) | tiny sun | ember | tiny moon (cool white) | a corona ring at the tip | an electric spark |
| Void ripple (34) | gold ring on sunlit cloud tops | rose ring | silver ring on moonlit tops | ring in the eclipse shadow | ring shattered by the rain |
| Pearl burst (35) | sunlit puff | rose puff | silver puff | dark puff, bright rim | wind-torn puff |
| Bed lights (7) | soft, nearly invisible in daylight | warming up | full team-colour glow pooling on the clouds (the night showpiece) | glow brightens as the sky darkens | flicker as lightning hits |
| Bridge wake (9) | faint | visible | bright team trail on the dark sea | bright | short-lived (rain washes it) |
| Cloud wake (12) | hole lit gold from below | rose | hole lit by the moon, silver edges | dark hole, ring-lit rim | hole closes fast |
| Victory dawn (5) | (it is the day) | - | night -> sunrise is the whole effect | eclipse lifts, ring breaks into dawn | storm clears to a rainbow over the sea |

Design notes:
- Night is about contrast: effects get cooler and brighter against the dark (moon silver, team colours glow), never
  just a dimmer copy of the day look.
- Eclipse is black-and-white with the corona as the only light; team colours survive as edges.
- Storm is motion and flashes: things tear, streak and flicker with the lightning; colours desaturate between strikes.
- Implementation: one shared "sky state" (phase + eclipse + storm weights) sent to every effect - the sky shader,
  the match layer, VV Live and the item / block looks - so they can never drift apart.

### I. Animation (added Nov 26, 23:47) - everything still follows the sky (H)
Levers, from safest to newest:
- **Flipbook textures** (`flipbook_textures.json`): proven in Fernando's packs (Blue Lock team blocks), work on Hive as
  a client pack. Even frame counts or it flickers. In VV the MERS / height maps can be flipbooks too - theory, test it.
- **Attachables** (held items, armor): the Cloud Sea weapons already use them; their geometry can carry looping
  animations. Proven lever.
- **RF match layer**: already draws beds (Figura / cosmetic beds), shards, kills in VV - anything there can move.
- **VV block materials rebuilt with lazurite** (lz126x round-trips the game's materials byte-exact): a real vertex
  sway / ripple in VV's own block shader. Biggest reach, most risk (game updates) - theory, last.

Blocks
37. **Drifting cloud wool.** The wool's texture rolls and billows like a real cloud seen up close (16-32 frame
    flipbook). Day: sunlit billows. Night: a moonlit shimmer crosses it. Storm: fast, dark, boiling. Every main Hive
    block gets its own motion (wool, planks, concrete, stone), with the high-detail treatment on those four.
38. **Breathing team blocks.** Team wool / concrete glow pulses (one breath every ~4 s), brighter at night, soft in
    daylight. Flipbook on the MERS emissive channel - theory.
39. **Cloud place animation** (already agreed idea): a small cloud puffs out and settles into the block in ~0.15 s,
    fast enough for speed bridging. RF match layer (block-place events already reach RF).
40. **Leaves and grass in the wind** in VV: the classic Cloud Sea already sways them; VV needs the lazurite material
    route. Storm = strong gusts, night = barely moving.

Weapons and armor
41. **Living sunstone.** A slow light wave runs up the blade (flipbook glow), like heat shimmer by day and a
    moonbeam sliding along it at night. During the eclipse it stops dead; in the storm it crackles.
42. **Idle hover on held items.** In first person the weapon floats and breathes when you're standing still
    (attachable idle animation), settling as you move.
43. **Enchant glint as moving cloud light.** The glint becomes soft cloud shadows sweeping across the item by day,
    silver moonlight ripples at night.
44. **Armor trims that flow.** Gold / rose-gold edges on the Cloud Sea armor shimmer slowly, like sunlight moving
    over water; at night they turn into a cool silver flow.

Bows and arrows
45. **Bow string glow while drawing** builds with the charge (attachable animation + flipbook), and the limbs bend
    with a faint light along them. Sun-gold by day, moon-silver at night.
46. **Arrows that spin a wisp** of cloud as they fly (the contrail from 32, animated with a slow twist).

Beds (Hive beds are entities - the bed you see is drawn; RF already draws its own beds in the match layer)
47. **Cloud beds that breathe.** The bed's blanket rises and falls slowly, a little cloud curling off the pillow;
    by night the blanket glows faintly in the team colour so you can admire your base from across the map.
48. **Bed under attack.** While enemies are at your bed (BF already knows: `threat`), the cloud around it darkens
    and churns - looks only, the same info Hive already shows you in chat.
49. **Bed break.** The bed collapses into a cloud that blows away over the void, the team-colour light going out
    with it (RF already plays a bed fall; this is its Cloud Sea version).

Limits: hitboxes stay vanilla (no animation changes a shape the server knows). Otherwise visuals come first
(Fernando, Nov 26 23:47): go as bold, animated and alive as looks best - no "keep it calm for PvP" cap.
