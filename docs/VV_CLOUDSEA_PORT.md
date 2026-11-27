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

## Facts learned today (corrections to VV_TESTED.md)
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
