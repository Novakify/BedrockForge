# Knives (Bedrock Forge Cosmetics) - full handoff

Updated 2026-11-19 02:15 (America/Chicago). Give this whole file to the next chat. Everything here is on `main`
of `Novakify/BedrockForge` unless marked otherwise.

## 0. Ground rules (Fernando's, verbatim - they bind every session)
"Personal use only; all models, textures and sounds must be our own originals, not ripped from other clients."
"New modules default OFF, in the Cosmetics tab of my Right-Shift menu. Rules: no auth/anti-cheat/integrity bypass;
back up before replacing DLLs (game closed only); ask before binding keys; don't join Hive unattended; read
docs/VISUAL_STANDARD.md first and judge in-game frames, not code."
Also: follow CLAUDE.md (short branch from current `main`, PR + merge via REST `gh api`, everything lands on `main`).
Fernando has permission from Purxiy's creators to use their Knives module as a reference (settings / feel); our
models, textures and sounds stay our own. Talk to him like a chill buddy, short and practical.

## 1. What it is
A visual-only Knives module: any sword you hold turns into one of our own 3D knives (Karambit, Balisong, Dagger)
with draw, inspect and idle tricks, two attack swings that alternate per click, slash light, hit sparks and our own
synthesized sounds. Only your client sees it; nothing is sent to the server (works on Hive with no server change).

Where it lives in game: Right Shift -> MODS -> Cosmetics tab (wand icon pill) -> "Knives" row (toggle on the right,
settings on the gear or right-click the row). The "BF Cosmetics" resource pack must be active (dev pack on the PC;
for Hive it has to be in global resources).

## 2. How it works (architecture)
- Pack generator: `tools/Cosmetics/` (pure Python, no hand edits to the pack):
  - `knives.py` models (poly_mesh), atlases, bones (`wfx_item -> bfk_fix -> bfk_pose -> bfk_spin -> parts`, tip
    locator on `bfk_blade`); `materials.py` procedural painters (Damascus, stonewash, mirror, cocobolo, DLC ti...).
  - `finishes.py` NEW: 4 finishes per knife made from each knife's own atlas.
  - `anims.py` all clips + particles + fx texture; `sounds.py` synthesized sounds (+ quiet variants).
  - `build_pack.py` writes `packs/BFCosmetics` (attachables for every vanilla sword, render controller, files).
    Run: `python3 -I tools/Cosmetics/build_pack.py <bedrock-samples>/resource_pack/textures/items`
    (sparse clone of github.com/Mojang/bedrock-samples for the vanilla sword fallback textures).
  - `preview.py`, `preview_anim.py` offline renders / animation contact sheets.
- Bedrock Forge side: `src/client/Cosmetics.cpp` (module, settings, per-frame logic), help lines in
  `src/client/Descriptions.cpp`, hook `cosmetics::noteLocalHit` from `GameMode::attack` (Modules.cpp).
- BF writes public Molang variables on the local player every frame they change (cached writes, 1 s refresh):
  `bfknife` (0 vanilla, 1 karambit, 2 balisong, 3 dagger), timers `bfkdraw/bfkinsp/bfkatk/bfkhit` (s since event,
  99 = idle), parities `bfkinsn/bfkatn/bfkhitn`, `bfkspd`, `bfkopt` bitmask (1 arcs, 2 hit fx, 4 sounds, 8 quiet,
  16 glint OFF, 32 third person OFF), `bfkfin` (finish 0-3), `bfkslash` (0 trail, 1 crescent, 2 streak),
  `bfkscl/bfkox/bfkoy` (first-person size/offset), `bfkcr/bfkcg/bfkcb` (effect colour). Other players get rest
  values every 0.5 s (2 s when off) so their swords stay vanilla.
- The attachable's `pre_animation` copies them (`c.owning_entity->v.*`) into `v.bfk_*`; every clip is a plain
  animation whose `anim_time_update` = BF's clock x speed, gated by a condition (click parity picks swing1/swing2).

### Hard-won lessons (don't relearn these)
1. An animation with an empty `"bones": {}` makes Bedrock drop the WHOLE animation file. Never emit it.
2. Animation controllers never ran on the attachable; clips driven by `anim_time_update` do. Keep it that way.
3. A variable that was never written made the sword vanish -> BF always writes rest values.
4. Device-commit gotcha: re-committing a file to the SAME device path kept the old bytes. Use a new file name each
   time and check md5. Deletes aren't allowed on the PC (use mv / `tar --overwrite`).
5. Minecraft clicks via computer use: mouse_move, then left_mouse_down / left_mouse_up (plain clicks do nothing).
   Launch "Minecraft.windows" from the LeviLauncher instance (never the Store "Minecraft for Windows").
6. Mojang MCPE-160960: particle locators on attachables may spawn at the entity origin instead of the locator.

## 3. Built + verified IN GAME (2026-11-17/18)
- All three knives render in first person, held right; third person shows the knife (pause-screen preview).
- Karambit draw, swing1/swing2 and idle trick run off BF's timing.
- First-person framing via `bfk_fix` (scale 0.75, +7 y).

## 4. Built OFFLINE (merged, NOT verified in game yet)
PR #59 "Knives: customization" + PR "offline polish". A newer DLL containing these settings was installed on the PC
on 2026-11-19 ~01:45 (it contains the new setting ids) - confirm BUILD_INFO / that the menu shows "Applies to".
- **Applies to**: All swords / By sword type (Wooden, Stone, Copper, Iron, Golden, Diamond, Netherite: each Main
  knife / Karambit / Balisong / Dagger / Vanilla) / One hotbar slot (1-9) / Renamed swords (anvil name containing
  karambit, balisong or butterfly, dagger, or knife = main knife).
- **Finish**: Random (default, rerolls every draw, never the same twice) / Factory / Blackout (black DLC, raw edge
  line) / Gold (TiN) / Fade (anodised straw -> violet -> blue). 12 textures, render-controller index
  `(knife-1)*4 + finish`.
- **Effect colour**: Random (default, new every swing) / Steel / Violet / Ember / Gold / Crimson. Passed to the
  particle emitter with `pre_effect_script` (`v.cr = v.bfk_cr; ...`); particles tint white-hot -> colour -> fade,
  and fall back to steel white if the variable doesn't arrive.
- **Slash style**: Matched to the knife (default: karambit Crescent, balisong Trail, dagger Thrust streak) / Trail /
  Crescent / Thrust streak. New particles `bfk:crescent` (32x32 supersampled crescent sprite) and `bfk:streak`
  (thrust burst of fine lines).
- **Size / left-right / up-down** sliders (first person), **Knife in third person** toggle, **Enchantment glint**
  toggle, **Quiet sounds** (".q" sound events at 0.45 volume).
- Existing: Knife (main), inspect key I (VK 73), idle tricks + interval, speed, draw, sounds, slash effects, hit
  effects.
- Swings re-tuned: karambit slashes no longer cancel against the ring roll; balisong and dagger thrusts ~2x reach
  with a wrist snap. Balisong held 10% smaller in first person. Balisong pocket chamfers toned down.
- Perf: Molang writes cached (only on change + 1 s refresh). 74 animate entries per sword attachable, all start with
  `v.bfk == n` so other players' swords cost only failed compares. Particles per event: trail 6, crescent 1,
  streak 9, hit ~10 sparks + 1 cut, all additive and 0.1-0.3 s. VRAM ~17 MB for the 12 atlases.
- Docs: `docs/COSMETICS.md` ("Knives customization" + audit), `docs/img/cosmetics/finishes.png`.

## 5. Needs TESTING in game (in this order)
1. Menu shows the new settings (Applies to, the 7 sword pickers, Finish, Effect colour, Slash style, sliders...).
   If the gear doesn't open settings with remote clicks, try right-click on the row / a held click, or edit
   `mods/BedrockForgeCore/config/modules.json` with the game closed.
2. Every knife: draw (non-sword -> sword AND sword -> sword), swing1/swing2 (click repeatedly), inspect with I
   (twice to chain), idle trick (~12 s still).
3. Slash light: visible, at the blade tip (MCPE-160960 risk), and in the matched style per knife; then each style.
4. Effect colour: Random changes per swing; fixed colours apply. If everything is white, `pre_effect_script` didn't
   pass the colour -> switch to per-colour particle variants in the generator.
5. Finish: Random rerolls on draw; each fixed finish shows the right texture.
6. Applies to: By sword type (e.g. iron = Balisong, diamond = Dagger, wooden = Vanilla), One hotbar slot,
   Renamed swords (anvil).
7. Size/offset sliders move the knife the expected way (axes are the item frame - may need swapping/sign flips).
8. Third person off (F5 = vanilla sword), glint off on an enchanted sword, quiet sounds, hit a mob for sparks +
   cut + sound.
9. Third person of each knife (F5): idle, run, swing.
10. Colours in a neutral place (plains at noon) - the "test" world area looked heavily blue (world/shader grading).
11. FPS with Knives on vs off while swinging.
Judge real frames against docs/VISUAL_STANDARD.md (2-3 honest art-director passes); fix in the generators /
Cosmetics.cpp only, then rebuild + reinstall + re-check. Restore any setting you changed.

## 6. Known risks / open items
- MCPE-160960 particle locator bug (fallback: emit without locator, or draw the trail in BF / World FX).
- pre_effect_script colour hand-off unverified.
- Balisong / dagger draws and inspects not watched in motion yet; balisong handle flip is the riskiest one.
- Knives settings gear didn't respond to remote clicks once (might be the remote input only).
- Enchant glint look on the knives not checked.
- Only the local player sees knives (by design).

## 7. Ideas to make them stand out (nobody's weapons pack does these on Bedrock)
Ranked by wow-per-effort. All visual-only, client-side, our own art.

**Feel / animation**
1. **Real balisong manipulation**: different idle tricks per knife chosen at random from a set of 4-6 (aerials,
   chaplin, zen rollover, twirl; karambit spins around the finger ring; dagger flip-and-catch by the blade). Idle
   tricks never repeat twice in a row.
2. **Combo swing chain**: a 3rd and 4th swing when you click fast (combo window ~0.5 s), ending in a finisher
   (karambit reverse-hook rip, balisong flip-stab, dagger double stab). BF already counts clicks.
3. **Crit swing**: confirmed crits (BF already knows `crit`) play a heavier swing + bigger flash + deeper sound.
4. **Sprint carry**: knife reversed along the forearm while sprinting (CS-style), flips back on stop.
5. **Inspect "look at it" pose**: the inspect brings the blade close and rotates it slowly under the light so the
   Damascus / fade catches highlights (the eye-candy moment), with a tiny sheen sweep.

**Light / material**
6. **Moving specular sheen**: a thin highlight band that slides along the blade as you turn your camera (UV-offset
   overlay layer in the atlas or a second animated texture) - makes the steel read as real metal, not paint.
7. **Edge glint**: a 1-frame star glint on the edge at the end of the draw and the inspect (tiny particle at the
   tip).
8. **Heat finish that reacts**: Fade finish slowly brightens while you combo, cools back after (texture swap
   between 2-3 heat steps via render-controller index).
9. **Kill stamp**: optional "notch counter" - after a kill (BF hook) the next draw shows a fresh small etched
   tally mark on the ricasso for that session (texture variants 0-5 notches).
10. **Engraving / name etch**: your name or "NOVAK" laser-etched on the blade (atlas text pass), toggle in
    settings.

**Effects**
11. **Blade afterimage**: 2-3 faint ghost copies of the knife geometry trailing the swing (extra bones with the
    same mesh, delayed animation offsets, additive translucent material) - motion-blur look nobody does on Bedrock.
12. **Hit decal**: a small glowing cut line that stays on the mob for ~0.3 s where you hit it.
13. **Finisher burst on kill**: shards of light in the effect colour + a low "shing".
14. **Weather / biome reactions**: rain beads that run off the blade, frost on the blade in snow biomes, blade
    reflects the sky colour (tint from the time of day).
15. **Team colour mode**: effect colour follows your Hive team colour (read from your armour/name colour).

**UI / collection**
16. **Knife "case opening"**: a little Cosmetics screen in the Right Shift menu that rolls a finish + effect
    colour with a CS-style reel and plays our sound, then equips it. Pure eye candy, fits the glass UI.
17. **Inspect card**: while inspecting, a small glass UI tag shows knife name, finish, "float" (wear) and pattern
    seed - wear/pattern drive the texture (seeded wear masks), so every knife is unique.
18. **Preview stand**: a rotating knife display in the Cosmetics menu (like Garage cars) to pick finishes before
    using them.

**More knives (same pipeline)**
19. Bowie, Talon (curved), Kunai with ring + wrap, Tanto, Huntsman, a folding Flipper with a real lock.
20. **Per-knife unique sound sets** and a different draw for each (folder flick-open, kunai pulled from a sheath).

Suggested next build order: test pass (section 5) -> fix what fails -> idle trick sets + combo chain + crit swing
(1-3) -> sheen + edge glint (6-7) -> afterimage (11) -> inspect card with wear/pattern seed (17) -> case opening
(16) -> more knives (19).
