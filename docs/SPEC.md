# From The Fog: Java to Bedrock 1.26 Conversion Spec

Source: `/home/claude/ext/ftf` (LunarEclipseStudios "From The Fog", CC BY-NC-SA 4.0). This is a private, non-commercial adaptation: credit LunarEclipseStudios plus each skin author (the `credit` field per skin, §3.4), and keep the port under CC BY-NC-SA. `WAKE_UP.vbs` was read but not run. It is a Windows MsgBox chain ("They warned you...", "STOP", "RUN RUN...") that opens `https://ftf.lunareclipse.studio/him`. Do not port it.

**Target Java version:** Java 26.x snapshots or 1.21.11 and later. Evidence: the `mannequin` entity, the `max_block_modifications` gamerule, `time of minecraft:overworld query time`, `time_check` with `clock`, item `item_model` and `custom_data` components, and `rotate` and `return run`.

**Namespaces:** `lunareclipse.watching` (main), `lunareclipse.utils` (generic macro helpers), `from_the_fog` (advancements and the config function tag), `minecraft` (load/tick tags), and `watching` (legacy worldgen stubs).

**Repo anomalies.** These references point at files that are missing from the repo, or are bugs that change behaviour. The port should implement the *intended* behaviour.

| # | Where | Problem | Effect in Java | Port decision |
|---|---|---|---|---|
| A1 | `utils/determine_chance/common` | Calls `utils/determine_change/roll` (typo). | The roll never runs. `<mech>_roll` keeps its max value, so it only "succeeds" when the max is clamped to 1 (activity multiplier ≥ max). **Fearful Footsteps, Twisted Tapestries and Sighting Sense almost never fire.** | Implement the intended 1-in-N roll. |
| A2 | `structures/place/start` | Reads `options.structure_chance`, which is never defined. | No roll happens, so dynamic structures **never appear**. | Use the per-structure option (`common`/`default`/`rare` → 1/25, 1/100, 1/200). |
| A3 | `config/initialize_defaults` | Calls missing `config/defaults_edited`. | A user hook for pre-world default overrides. No-op. | Optional. |
| A4 | `debug/message` | Calls missing `debug/message/send_message`. | No-op. | Ignore. |
| A5 | `technical/player_init` advancement | Reward function `lunareclipse.watching:player_init` is missing. | No-op. | Ignore. |
| A6 | `sightings/skin_library/define_skins` | Calls the `#lunareclipse.watching:custom_skins` tag, which is not shipped. | It is a third-party skin-pack hook. | Optional. |
| A7 | `config/config_layout` | `execute if function lunareclipse.darkness:check` is an optional integration with the separate "Lights Out" pack. | Adds the `flickering_flashlight` option. | Drop. |
| A8 | `sightings/player_sleep` | Ignores the `nightmare_sighting` option, and makes no roll when frequency is `insane`. | | Respect the option. |
| A9 | `shrouded_specter/start` with `off` | Calls missing `presets/off`. | No-op, which is the intended "off". | Same. |
| A10 | `leafless_grove` recursion | Removes `positions[index]` on every hop, so extra entries are lost. Its `forceload add ~30 ~30 ~30 ~30` loads only one chunk. | Minor. | Ignore. |
| A11 | `ghost_miner/test`, `sightings/presets/test`, `forloop*/test` etc. | Developer tests (`execute as Bret06`, fixed coordinates). | | Do not port. |

---

## 1. Entry points, state, timers, config

### 1.1 Load and tick

- `#minecraft:load` runs `lunareclipse.watching:main/reload` and `lunareclipse.utils:main/reload`.
- `main/reload`:
  - schedules `timed_ticks/1_second` (1s) and `timed_ticks/5_minutes` (300s);
  - creates team `watching.door_ghost` with `collisionRule never`;
  - sets `gamerule max_block_modifications 999999999`;
  - sets storage `shrine_block_tag = "minecraft:fire"`;
  - creates the objectives (§1.2);
  - then runs `config/initialize_defaults` → `config/config_layout` → `sightings/skin_library/define_skins`.
- `#minecraft:tick` runs `main/tick` every tick:
  1. `as @e[type=#herobrine,tag=watching.herobrine] at @s` → `entity_ticks/herobrine` (§2.4).
  2. `as @a at @s` → `entity_ticks/player` (spot detection, tapestry detection, nightmare end, config tooltip ray, proximity jumpscare).
  3. When `sighting_sense` is on in both the config and the current sighting: every tamed wolf (`@e[type=wolf]` with `Owner`) → `entity_ticks/wolf`.
  4. Door-ghost villagers → `entity_ticks/door_ghost`.
  5. A config pivot with no `watching.opened_config` player within 10 → `config/close_config`.
  6. Markers `watching.structure` without `watching.center` → `structures/remove_base/start` (§4).
  7. When `preview_spinning` is on, the config skin preview armor stand rotates +2° per tick.
- `timed_ticks/1_second` (it reschedules itself every 1s):
  - Give Him Control: run init when enabled with no saved state; run cleanup when disabled and saved state exists.
  - When there is no `sightings.type`, `started_events = 1` and `sighting_timer_rolled` is not 1 → `sightings/spawning_rules/roll_spawn_timer`. Its return value is stored in `sighting_timer_rolled`, so a failed roll retries the next second.
  - `current_day = overworld time / 24000`. When GHC is on and the day changed → `give_him_control/check_day` (randomise options). Then `logged_day = current_day`.
  - When `started_events != 1` → `shrine/haunting_delay/check_day`: if `current_day >= options.haunting_delay` → `start_events`.
  - `as @a` → `advancements/get_day_count`.
  - Structure markers (`watching.structure_marker`) with a player within 16 → `advancements/found_structure`.
  - `advancements` option `true` → grant `from_the_fog:root` to `@a`. Otherwise revoke the whole `from_the_fog:root` tree.
- `timed_ticks/5_minutes` reschedules itself at 300s; the activity multiplier can pull it earlier, §1.6. In order:
  1. Failsafe: no Herobrine entity → `sightings/cleanup`.
  2. Revoke `technical/block_detection/placed_forest` from everyone, so the forest log fires at most once per 5 min per player.
  3. Structures `mossy_pyramid`, `sand_pyramid`, `letter`, `wooden_cross`, unless the option is `off` → `structures/place/start` (§2.21).
  4. `dreadful_donation` preset; `fearful_footsteps/event` as `@r`; `ghost_doors` preset.
  5. **Gate:** return unless `started_events = 1`.
  6. `ghost_miner/start` as `@r`; `haunted_herd/attempt`; presets for `burning_base`, `disappearing_torches`, `leafless_grove`, `chilled_candles`, `crimson_curse`; `twisted_tapestries/attempt` as `@r`; then, when `rekindling_shrine` is on, `block_events/detection/remove/start {block_tag:"minecraft:fire"}`, which relights shrines (§2.17).
  - Each step runs only if its option is `"true"`.
- `#from_the_fog:config` → `config/open_config`. The player runs `/function #from_the_fog:config`.

### 1.2 Scoreboards

| Objective | Type | Use |
|---|---|---|
| `watching.global_values` | dummy | Fake-player globals:<br>- `started_events` (1 = hauntings active)<br>- `sighting_timer_rolled`<br>- `player_spotted`<br>- `glass_spotted`<br>- `spotted_sign`<br>- `sinister_signs_roll`<br>- `has_item`<br>- `teleport_jumpscared`<br>- `suspenseful_spotted`<br>- `current_day`, `logged_day`<br>- `haunting_delay`<br>- `sighting_retry_limit`<br>- `<mechanic>_roll`<br>- `<tag>_raycast`<br>- `ghost_miner_length`<br>- `fearful_footsteps_temp`<br>- `leafless_grove_link`<br>- `chilled_candles_retry`<br>- `config_page_assembly`<br>- `temp_*` |
| `watching.animations` | dummy | - `head_rotation_yaw`, `head_rotation_pitch`, `body_rotation`, `rotation_diff_raw`<br>- `<part>_update`, `<part>_interpolation`, `<part>_axis_{x,y,z}`<br>- `holding_item`, `clamp_reached` |
| `watching.raycast` | dummy | Per-player step counter for the spot ray. `watching.raycast2` and `watching.painting_raycast` are used too but never created; scoreboard commands on them fail harmlessly. |
| `watching.player_time` | `minecraft.custom:minecraft.play_time` | Play-time ticks. |
| `watching.time_played` | dummy | `player_time / 24000`, used for the day-count advancements. |
| `lunareclipse_utils.global_values` | dummy | Loop indices for the utils library. |

### 1.3 Storage (NBT) layout

- **`lunareclipse.watching:config_options`**
  - `options.<id>`: config values, mostly strings `"true"`, `"false"` or enum names. `haunting_delay` and `activity_multiplier` are ints.
  - `options.herobrine_skin.{list[], selected, temp_list}`
  - `config.pages.{1,2,3}`, `config.settings.list.<page>.<n>[]`, `config.settings.option.<id>.<page>{id,type,options,page,offset,experimental}`
  - `config.preview_spinning`, `config.integrations`
  - `locked[]`: optional list of option ids that cannot be changed. Nothing in the pack sets it; it is meant for server admins.
- **`lunareclipse.watching:global_values`**
  - `sightings{type, previous_sighting, custom, spot_fov, jumpscare, light_reactant, sneaky_strike, max_distance, face_player, clamp_rotation, body_rotation, slip_behind, sighting_sense, haunted_herd{mob,variant,yaw,pitch}, queued_sighting{type,countdown}, temp…}`
  - `block_positions.<block_tag>.{positions[{x,y,z,yaw,pitch}], length, selected{…}, temp}`
  - `structures.<name>.{positions[{x,y,z,x_center,y_center,z_center}], length, selected}`
  - `skin_library.{skin.<id>{skin,credit,custom,namespace,parts[],pivots{},variants[],default,assembled[]}, emissive, custom_skin{pack_list,packs,pages}}`
  - `give_him_control.{previous_config, option_list, options}`
  - `signs.temp`, `caution_caption.temp`, `items.temp`, `temp.*`, `debug.temp`
- **`lunareclipse:utils`**: `temp_values.*` loop scratch.

### 1.4 Timers and schedules

- `schedule function` is used for every delay. Main ones: the 1s and 5 min loops; the sighting countdown; `models/schedule_create` (10t after spawn); despawn (`despawn_timer` seconds); sneaky strike (despawn − 3 s); vanish (`*_vanishing_delay` s); slip-behind removal (14t); breathing (32t half-cycle); walking (7t half-cycle); punch keyframes (2t, 3t, then breathing restart at 5t); ghost-door removal (30s); footsteps (0.3s); ghost-miner sounds (12–50t); the activity-multiplier reset.

### 1.5 Chance system

`utils/determine_chance/default {mechanic}` rolls 1..N and succeeds when the result is 1. N comes from `options.<mechanic>_chance`, integer-divided by `activity_multiplier`, with a minimum of 1.

| value | default table N | common table N (bug A1) |
|---|---|---|
| insane (Give Him Control only) | 5 | 3 |
| common | 25 | 6 |
| default | 100 | 25 |
| rare | 200 | 50 |
| scarce | 500 | 125 |

- The "default" table is used by: `burning_base`, `chilled_candles`, `crimson_curse`, `disappearing_torches`, `dreadful_donation`, `ghost_doors`, `leafless_grove`, `ghost_miner`, `haunted_herd`, `sneaky_strike`, `sinister_signs`.
- The "common" table is used by: `fearful_footsteps`, `twisted_tapestries`, `sighting_sense`.
- Predicates: `chances/1` = 1% and `chances/50` = 50%.

### 1.6 Activity multiplier

`utils/activity_multiplier/set {value, delay, event_timer}`:

- sets `options.activity_multiplier = value`;
- after `delay` s, resets it to 1;
- reschedules the 5-minute tick to fire in `event_timer` s.

It divides both chance denominators and sighting countdowns.

| Trigger | Value | Delay | Event timer |
|---|---|---|---|
| Shrine lit; GHC toggled on | 6 | 360 s | 150 s |
| Herobrine punched or shot | 4 | 360 s | 300 s |
| Jumpscare; spotted while GHC is on | 2 | 360 s | 300 s |

### 1.7 Haunting start (`started_events`)

Sightings and most events are disabled until `started_events = 1`. That happens when either:

- the overworld day number reaches `haunting_delay` (default **3** days; changing it in the config resets `started_events`), or
- a valid shrine is lit (§2.17).

`start_events` sets the score. If `eerie_entrance` is on, it also sends the yellow chat line `"Herobrine joined the game"` (translate `eerie_entrance.message.prefix`, the text `Herobrine`, and `suffix`).

### 1.8 Config menu

**Mechanics (Java).** `/function #from_the_fog:config` builds a 3D UI in the world:

- **Entities and placement.** It is made of `item_display`, `text_display` and `interaction` entities, all tagged `watching.config`. The pivot marker sits 3 blocks in front of and 1 above the player, is ground-corrected, and the UI faces the player with pitch 0. The player's `entity_interaction_range` gets +3.
- **Clicking.** A click is detected by the advancements `config/clicked_option` and `config/clicked_button` (`player_interacted_with_entity` on an interaction tagged `watching.icon` or `watching.button`). They schedule a check 2 ticks later. The check finds the interaction whose `interaction` NBT is set, takes the nearest button `item_display`, and runs its `custom_data.function` with that `custom_data` as macro arguments.
- **Tooltips.** Predicate `looking_at_option` plus a 0.5-step raycast (7 blocks) shows a `text_display` tip with the option name, or the skin name and credit.
- **Closing.** The menu closes when the player is more than 10 blocks away; a flash particle plays and the reach modifier is removed.
- **Sounds.** Option click plays `note_block.snare`. Page turn plays `item.book.page_turn` at pitch 1.5. Opening plays `note_block.pling` at pitch 2. Toggles play `block.lever.click`; a locked option plays `pling` at pitch 0.5. Boolean toggles play the custom `lunareclipse.watching:config.setting.<id>.<value>` sound; skin toggles play `armor.equip_leather`.
- **Main menu layout.**
  - Page 1 has 16 icons in a 4×4 grid.
  - Page 2: `twisted_tapestries`, `ghost_doors`, plus `lights_out` if the integration is present.
  - Page 3: `give_him_control` (a single switch).
  - A "next" arrow cycles the pages.
  - A hidden "Super Secret Settings" hotspot is an invisible, scale-0 paper `item_display` plus an interaction at the top-left, present on page 1 when GHC is off.
- **Option pages.** Each option page has an info panel (back button, name, description, big icon) and a settings panel, with an arrow when there are multiple pages. Setting types:
  - `boolean`: switch.
  - `option`: radio slots.
  - `increment`: ± buttons with a number.
  - `skin`: multi-select toggles with head previews and a rotating full-body preview on a "wardrobe" stage, with a pause/play spin button.
- **Experimental options** carry a red "EXPERIMENTAL" tag.

**Options. All ids, choices and defaults** (from `config_layout` and `defaults`):

| Menu page (icon) | Option id | Type / choices | Default |
|---|---|---|---|
| vanishing_delay | creeping_vanishing_delay | option 0.1 / 0.5 / 1 / 2 (s) | "0.1" |
| | dwelling_vanishing_delay | same | "0.5" |
| | stalking_vanishing_delay | same | "0.5" |
| | lurking_vanishing_delay | same | "1" |
| (hidden) | nightmare_vanishing_delay | – | "0.1" |
| (hidden) | ghost_miner_vanishing_delay | – | "0.5" |
| sightings p1 | creeping_sighting | bool | **false** |
| | nightmare_sighting | bool | true |
| | stalking_sighting | bool | true |
| | lurking_sighting | bool | true |
| sightings p2 | dwelling_sighting | bool | true |
| | dwelling_shadow | bool | true |
| | sighting_frequency | common / default / rare / scarce (GHC also uses insane) | default |
| | sighting_window | always / day / night / rain | always |
| sightings p3 | sudden_scare | bool | true |
| | malicious_malfunction | bool | false |
| | suspenseful_sighting | off / default / dynamic (legacy "true"→default, "false"→off) | off |
| | shrouded_specter | off / poof / smoke / portal | off |
| sightings p4 | no_sleep | bool | true |
| | window_watcher | bool | true |
| | sneaky_strike | bool | true |
| | sneaky_strike_chance | chance | default |
| sightings p5 | sighting_sense | bool | true |
| | sighting_sense_chance | chance | default |
| | nametag | off / dynamic / cryptic | off |
| | sighting_noise | off / cave / ghast (legacy "true"→cave) | cave |
| sightings p6 | vanilla_sightings | bool, EXPERIMENTAL | false |
| (no UI) | emissive_layers | bool (UI commented out) | true |
| (no UI) | activity_multiplier | int | 1 |
| skin_library p1–7 | herobrine_skin.list | multi-select (§3.4); an empty list falls back to "default" | ["default"] |
| burning_base | burning_base / burning_base_chance | bool / chance | **false** / default |
| disappearing_torches | disappearing_torches / _chance | | true / default |
| malevolent_shrine p1 | haunting_delay | increment int ≥ 0 (days) | 3 |
| | rekindling_shrine | bool | true |
| | shrine_surprise | bool | false |
| | shrine_strike | bool | false |
| malevolent_shrine p2 | eerie_entrance | bool | false |
| | caution_caption | bool | true |
| | lurking_language | local / swedish | local |
| dreadful_donation | dreadful_donation / _chance / haunted_heirlooms / lurking_language | | true / default / false / local |
| leafless_grove | leafless_grove / _chance | | true / default |
| structures | mossy_pyramid, sand_pyramid, letter, wooden_cross | off / common / default / rare | default, default, **off**, **off** |
| chilled_candles | chilled_candles / _chance | | true / default |
| crimson_curse | crimson_curse / _chance / soul_shift | | true / default / false |
| sinister_signs | sinister_signs / _chance / lurking_language / biome_boards | | true / default / local / true |
| ghost_miner | ghost_miner / _chance | | true / default |
| advancements | advancements | bool | false |
| fearful_footsteps | fearful_footsteps / _chance | | true / default |
| haunted_herd | haunted_herd / _chance | | true / default |
| page 2: twisted_tapestries | twisted_tapestries / _chance | | **false** / default |
| page 2: ghost_doors | ghost_doors / _chance | | true / default |
| page 2: lights_out (integration) | flickering_flashlight | bool | false |
| page 3 | give_him_control | bool | false |
| Super Secret Settings | hoebrine_returns | bool: always use the "hoebrine" skin | false |
| | from_the_pants | bool: render only the legs | false |

Every "chance" choice list is `common / default / rare / scarce`. `lurking_language` is shared by the shrine, donation and signs pages.

**Bedrock UI.**

- Open the menu with:
  - a custom command `/ftf:config`, registered via `system.beforeEvents.startup` → `customCommandRegistry`; or
  - `/scriptevent ftf:config`; and/or
  - a "Fog Journal" item through `world.afterEvents.itemUse`.
- Main screen: an `ActionFormData` with one button per category, plus "Give Him Control" and a hidden SSS entry, e.g. a long-press or 5-tap.
- Category screen: a `ModalFormData` with `toggle` for bools, `dropdown` for enums and `slider` (0–30) for `haunting_delay`.
- Skin library: a paged `ModalFormData` of toggles, about 20 per page; at least one must stay on.
- Keep the custom toggle sounds by calling `player.playSound("ftf.config.setting.<id>.<value>")`.
- Persist config in a world dynamic property, as a JSON string under 32 KB.

### 1.9 Give Him Control (GHC)

- **On enable**, detected by the 1s tick:
  - Back up all options and force: all 5 sighting types on, `sighting_window=always`, `sudden_scare=true`, `eerie_entrance=false`, `haunting_delay=0`.
  - Then **every in-game day** each of these 41 options is set to a random value:
    - `burning_base`, `burning_base_chance`
    - `sighting_frequency`, `dwelling_shadow`, `no_sleep`, `window_watcher`
    - `sneaky_strike`, `sneaky_strike_chance`, `sighting_sense`, `sighting_sense_chance`
    - `disappearing_torches`, `disappearing_torches_chance`
    - `shrine_strike`, `shrine_surprise`, `rekindling_shrine`
    - `dreadful_donation`, `dreadful_donation_chance`, `haunted_heirlooms`
    - `leafless_grove` (listed twice), `leafless_grove_chance`
    - `mossy_pyramid`, `sand_pyramid`, `letter`, `wooden_cross`
    - `chilled_candles`, `chilled_candles_chance`, `crimson_curse`, `crimson_curse_chance`, `soul_shift`
    - `sinister_signs`, `sinister_signs_chance`, `biome_boards`
    - `ghost_miner`, `ghost_miner_chance`, `fearful_footsteps`
    - `haunted_herd`, `haunted_herd_chance`
    - `twisted_tapestries`, `twisted_tapestries_chance`
    - `ghost_doors`, `ghost_doors_chance`
  - Value sets: bools from {true, false}; chances and `sighting_frequency` from {insane, common, default}; structures from {common, default}.
- **On disable:** restore the backup and re-run the defaults.
- **UI theme:** red logo `ui/text_red`; glitched background `main_menu_glitched`; switch model `give_him_control_switch`; title "I Have Control." (scale 1.5); obfuscated rotated description; four caution captions.
- **On toggle:**
  - sounds `elder_guardian.curse` (pitch 0.1), `ambient.cave` (0.25) and `events.sudden_scare.crash` (vol 0.25, pitch 0.25) to players within 15;
  - activity ×6 (360 s, event in 150 s);
  - a **creeping sighting** on the nearest player.
- **GHC-only behaviours:** spotted → activity ×2; head tilt (§2.6); teleport jumpscare (§2.8); a jumpscare also punches (§2.9).

### 1.10 Debug

`debug/toggle` gives or removes the tag `watching.debug_mode`. `debug/message {message,status,info,info_list}` sends a tellraw to debug players, with a font glyph `ꨀ` (failed) or `ꨁ` (passed) and green key/value lines.

Bedrock: a `ftf:debug` dynamic property per player, messages via `player.sendMessage`, and `§a`/`§c` glyph substitutes.

---

## 2. Gameplay features

All distances are in blocks, times in seconds unless marked "t" (ticks).

### 2.1 Sighting scheduler

1. Every 1 s, if there is no active sighting and `started_events = 1`, roll `random 1..7`:
   - 1 → creeping
   - 2–3 → stalking
   - 4–5 → lurking
   - 6–7 → dwelling
   
   If the chosen type is disabled, the roll fails and retries next second. **Nightmare is not in this pool** (§2.10).
2. Countdown in seconds, `random min..max`; at night (overworld time 13000–24000) the night range is used. Divide by `activity_multiplier`.

   | sighting_frequency | day | night |
   |---|---|---|
   | insane | 15–30 | 8–15 |
   | common | 30–60 | 15–30 |
   | default | 60–120 | 30–60 |
   | rare | 120–240 | 60–120 |
   | scarce | 240–360 | 120–180 |

3. When the countdown fires (`summon_sighting`):
   - Abort with cleanup and re-roll if `sighting_window` is `day` and it is night, `night` and it is not night, or `rain` and it is not raining, or if `started_events != 1`.
   - Abort if a door ghost exists.
   - Otherwise run the preset `as @r[gamemode=!spectator] at @s`.

### 2.2 Sighting presets (`sightings/sighting` parameters)

| Preset | Position mode | max_distance | spot_fov | Jumpscare | Light-reactant | Despawn (s) | Sneaky strike |
|---|---|---|---|---|---|---|---|
| creeping | creeping | 16 | 85 | true | false | 30 | true |
| stalking | stalking | 50 | 77 | true | false | 30 | false |
| lurking | lurking | 128 | 66 | true | false | 30 | false |
| dwelling | dwelling | 64 | 75 | true | via `dwelling_shadow` (§2.11) | 30 | true |
| nightmare | nightmare | 16 | 60 | false | false | 60 | false |
| ghost_miner | still | 16 | 60 | false | false | 60 | false |
| shrine_surprise | still | 16 | 0 (cannot be spotted) | false | false | 2 | false |
| haunted_herd | stalking | 50 | 77 | false | false | 30 | false |
| fake (debug) | still | 9999999 | 0 | false | false | 99999999 | false |

**Dimension and underground routing.** "Underground" means y −64..55 and `can_see_sky=false`.

| Preset | End | Nether | Underground | Otherwise |
|---|---|---|---|---|
| stalking | stalking | dwelling | dwelling | stalking |
| lurking | lurking | dwelling | dwelling | lurking |
| dwelling | stalking | dwelling | dwelling | stalking |
| haunted_herd | plain stalking sighting | dwelling | dwelling | haunted herd |

**`spot_fov` meaning.** The check is `distance=..0.<spot_fov>` between the unit point 1 block toward Herobrine and the unit point 1 block along the player's look direction, i.e. chord length *c*. The cone half-angle is θ = 2·asin(c/2):

| spot_fov | 85 | 80 | 77 | 75 | 70 (tapestry) | 66 | 60 | 40 (suspense) | 0 |
|---|---|---|---|---|---|---|---|---|---|
| θ | 50.3° | 47.2° | 45.3° | 44.0° | 41.0° | 38.5° | 34.9° | 23.1° | never |

**`sightings/sighting` flow:**

1. Remove any existing Herobrine. If `sighting_retry_limit >= 5`, reset it and abort.
2. Store the parameters in `sightings.*`. Summon a temporary **spread entity**: an `interaction` 1×2, tags `watching.spread_entity` and `watching.herobrine`.
3. Run `spread_position/<mode>/init` (§2.3), then ground correction.
4. Unless the type is custom: if no player is within 0.4..max_distance of the spread entity, remove it. If it no longer exists and the player is not standing over water, **retry** the same preset, incrementing `sighting_retry_limit`.
5. Schedule `models/despawn` after `despawn_timer` s. If sneaky strike is set and the option is on, schedule `sneaky_strike/strike` at `despawn_timer − 3` s.
6. Forceload the chunk. Schedule `models/schedule_create` at **10t**; it picks the skin and builds the model (§3).
7. Set `face_player=true`, `clamp_rotation=true`, `body_rotation=true`.
8. Sighting-sense roll (common table): on success, `sightings.sighting_sense=true`.

### 2.3 Spawn positioning

**`random_spread {distance_min,max, spacing_min,max, front_spawning}`:**

- Run at the player's position with the player's yaw and pitch.
- The distance sign comes from `random 0..front_spawning`: 0 = behind (negative ^z), 1 = in front.
- Lateral sign is random. Then `tp ^spacing ^ ^distance`.

| Mode | distance | lateral | front allowed | Vertical |
|---|---|---|---|---|
| creeping | 3–5 | 2–3 | no (always behind) | +7 then ground-correct, centre on block |
| dwelling | 6–12 | 0–18 | yes | +7 then ground-correct. Sets `light_reactant=true` if `dwelling_shadow` is on, not in the Nether and light < 8 |
| stalking | 25–46 | 0–35 | yes | surface heightmap `motion_blocking_no_leaves`, centre |
| lurking | 50–100 | 0–50 | yes | same as stalking |
| nightmare | – | – | – | Player gets tag `watching.having_nightmare`. Entity placed 2 blocks from the bed opposite its `facing`: facing west → +2x, east → −2x, south → −2z, north → +2z |
| still | – | – | – | Exactly at the execution position, facing the player |

**Ground correction** (`ground_correction/loop`):

- Up to 320 iterations of 0.1 block, so 32 blocks maximum. Each iteration aligns y and teleports.
- Removed if in liquid (`kelp`, `water`, `lava`).
- Keeps descending while the block below (at −0.1) is in `no_col_blocks` (104 passable blocks: air, plants, leaves, torches, snow layer, carpets, corals…) and not `pause_raycast` (farmland, dirt_path). It also descends while the current block is solid.
- Result: it lands in the first passable cell above a solid block. This lets Herobrine stand in 2-high strip mines.

### 2.4 Per-tick Herobrine logic (`entity_ticks/herobrine`)

Runs for every entity tagged `watching.herobrine`.

**Model parts** (item_displays): animation update and body/head rotation (§3.3).

**Mannequin** (`vanilla_sightings`): teleported to the AI stand each tick and given the head yaw and pitch.

**On the AI armor stand:**

- Pending scripted rotation (`temp.sighting_rotation`): rotate `increment`° per tick until the total reaches `rotation`, then run `command`.
- `face_player`: rotate to face the nearest non-spectator player's **feet** every tick. Store yaw and pitch in scores; the pitch is forced to 15 during a nightmare.
- Ground correction again when the block below is passable or the current block is solid.
- **Removal (`models/remove`)** when any of these holds:
  - the head block (y+1) is solid or leaves;
  - it is standing in liquid;
  - no player is within `max_distance`;
  - it is light-reactant and any player is within 3.
- Light-reactant and light ≥ 8 → `light_reactant=false`.
- Haunted herd and any player within 7 → replace with the real mob (§2.13).
- `slip_behind` left/right → teleport `^±0.1` sideways per tick (yaw only).

**On the hitbox interaction:** any arrow (`#arrows`) within 1.5 of hitbox+1y → `shot_herobrine`. The arrow's Motion is zeroed, Herobrine is removed, and activity goes ×4 (360/300).

**Player tick:** when `sightings.jumpscare=true`, the player is not a spectator, `player_spotted != 1`, and any Herobrine entity is within **2** → `sudden_scare/init`.

### 2.5 Spot detection (player_spotted)

Every tick, per non-spectator player, when `player_spotted != 1` and a Herobrine entity is within **80**:

1. **Cone test.** The player's look direction must be within the `spot_fov` cone (§2.2) of the direction to Herobrine's eyes.
2. **Ray march** (`search`, recursive): from the eyes toward Herobrine in 0.5-block steps, up to 256 steps (128 blocks). The test point is offset −0.1 y. At each step:
   - **Window watcher** (option on): if the block is in the `glass` tag (35 glass/pane/tinted ids), set `glass_spotted=1`.
   - **Retry**: if the spread entity is within 2.2, the model has not spawned yet, so re-run the previous preset (`retry_sighting`).
   - **Advancements**: a Herobrine entity other than the spread entity within 2.2 grants `first_encounter`, `encounter_dwelling_shadow` (if light-reactant) and `encounter_pants` (if `from_the_pants`).
   - **Light-reactant**: if the sighting is light-reactant and the model exists, stop. A dark dwelling Herobrine cannot be spotted; he only vanishes on approach to within 3.
   - **Hit**: any Herobrine entity within 2.2 → `spotted` (run as that entity, at it).
   - **Continue** only while the next block is in `sight_blocks`, which has 190 entries: air, liquids, plants, torches, fences, panes and glass, doors, trapdoors, signs, rails, leaves, carpets etc.

**`spotted`:**

1. If not through glass, set `player_spotted=1`. If the spread entity still exists, remove everything.
2. **Haunted herd:** a player within 20 → instant replace. Otherwise schedule `haunted_herd/spotted` after 1 s.
3. **Not through glass** → `vanish` (§2.6).
4. **Through glass (window watcher):**
   - Schedule `models/remove` in 0.2 s. It is re-armed every tick while the player keeps looking, so he lingers only until the player looks away.
   - Clear `glass_spotted`.
   - Roll sinister signs once per sighting (default table). If `sinister_signs` is on and the roll is 1: after 2 s Herobrine holds a biome-appropriate sign (§2.16) and `spotted_sign=1`, so a sign is placed where he stood when removed.

### 2.6 Vanish, slip behind, head tilt, shrouded specter, sighting noise

**`vanish`:**

1. **Sighting noise.** 50% chance, unless off. Played to `@a` at Herobrine +8y:
   - cave: `minecraft:ambient.cave`, vol 0.7, pitch 0.1;
   - ghast: `minecraft:entity.ghast.hurt`, 0.7 / 1.
2. GHC → activity ×2.
3. **Slip behind check.** Executed at Herobrine with his yaw, at eye height +1. **Left** wins first:
   - the block at `^1 ^ ^1` and the one below it are solid (not `sight_blocks`), i.e. a wall front-left;
   - and `^1 ^1 ^` and `^1 ^ ^` are passable (`no_col_blocks`).
   
   Otherwise the mirrored right check (`^-1`). On success:
   - start the walk animation;
   - set `face_player=false` and `slip_behind=left|right`;
   - he side-steps 0.1 per tick and the body is forced sideways (−8 or +8 per tick past 25°);
   - remove after **14t**, about 1.4 blocks, so he walks behind the cover.
4. **Otherwise** read `<type>_vanishing_delay`: creeping 0.1, dwelling 0.5, stalking 0.5, lurking 1, nightmare 0.1, ghost_miner 0.5, or the custom value.
   - **GHC head tilt:** only for lurking and stalking, no player within 20, and a 1% roll. After 1 s, tilt the head z to −15 (about −17°) over 5t, and the delay becomes 3 s.
   - Schedule `models/remove` after the delay.

**`models/remove`:**

1. **GHC teleport jumpscare.** GHC on, not yet done this sighting, 1% roll, and a random player is *fixated*, i.e. `looking_at` the hitbox. Teleport the AI stand to `^ ^ ^1` in front of that player, clear `player_spotted`, run `sudden_scare/init`, and stop.
2. If `spotted_sign=1` → `signs/init_place` at the AI position.
3. **Shrouded specter** particle at the AI position, unless haunted herd or slip-behind. All are `force @a`, centred 0.5 above, delta 0.25/0.8/0.25, count 50:
   - poof: `minecraft:poof`, speed 0.01;
   - smoke: `minecraft:smoke`, 0.01;
   - portal: `minecraft:portal`, 1.
4. `sightings/cleanup` resets every per-sighting flag:
   - stops the breathing, walking and head-tilt animations and removes the held item;
   - clears `player_spotted`, `sighting_timer_rolled` (allowing the next roll), `teleport_jumpscared`, `glass_spotted`, `sinister_signs_roll`, `spotted_sign` and `suspenseful_spotted`;
   - cancels the sneaky strike, un-forceloads and resets ground correction.
5. Teleport the husk and mannequin to y −512, so there are no drops or death animation. Then kill every Herobrine entity. The armor-stand break sounds are silenced in the RP.

**`models/despawn`** (timer expiry, never seen): grant `missed_herobrine` to the nearest player, clear `spotted_sign`, then remove.

### 2.7 Suspenseful sighting

A second cone of 0.40 (about 23°), evaluated every tick when the option is not off and `spot_fov != "0"`. If `player_spotted == 1` and `suspenseful_spotted != 1`, play a sting once to that player:

- `default` mode: `lunareclipse.watching:events.suspenseful_sighting.default`;
- `dynamic` mode: `...slenderman` when the selected skin is slenderman, `...werewolf` when it is werewolf, otherwise default.

### 2.8 Sudden scare, malicious malfunction, teleport jumpscare

**`sudden_scare/init`** (as the player): set `player_spotted=1` and grant `get_jumpscared`. Then:

- **malicious_malfunction on** → `crash`:
  - run `jumpscare`;
  - play `events.sudden_scare.crash` to the player;
  - after 3t, spawn `particle rain … 666666666 force @p` at Herobrine, deliberately freezing or crashing the client. **No Bedrock equivalent; do not port the crash.** If wanted, fake it harmlessly: black `camera fade` (hold 3 s), the crash sound, and an "Unknown entity error" style title.
- **Else sudden_scare on** → `jumpscare`:
  - `tp` the player to face Herobrine (within 2);
  - GHC → schedule a punch in 2t;
  - play `lunareclipse.watching:events.sudden_scare.jumpscare` (5 random variants), category `ambient`, at Herobrine;
  - remove Herobrine after **10t**;
  - activity ×2 (360/300).
- **Else** just remove.

### 2.9 Sneaky strike, punching and shooting Herobrine

- **Sneaky strike.** Fires at despawn − 3 s (27 s for 30 s sightings), on creeping and dwelling only. Uses the default chance with `sneaky_strike_chance`. On success, `sightings/punch`: if a player is within 6 of the AI, play the punch animation (§3.3) and `damage @p 1 minecraft:player_attack by Herobrine`.
- **Player hits Herobrine.** Advancement `technical/hit_herobrine` (`player_hurt_entity` on the hitbox interaction):
  - `damage @s 2 minecraft:out_of_world`;
  - remove Herobrine;
  - activity ×4 (360/300);
  - grant `punch_herobrine`.
- **Arrow** (§2.4): remove and activity ×4.

### 2.10 Nightmare and No Sleep

- **Nightmare trigger:** advancement `technical/player_sleep` (`slept_in_bed`), only if `started_events=1`. Roll by `sighting_frequency`: common 1/25, default 1/100, rare 1/200, scarce 1/500 (insane: no roll). On 1, and if the player's block is a bed → nightmare preset and advancement `experience_nightmare`.
- **During the nightmare:** Herobrine stands at the foot of the bed, 2 blocks away, looking down (pitch 15, about 6.9° head x). It ends (`end_nightmare` → remove) when the player is no longer in an occupied bed, or after 60 s.
- **No Sleep** (`no_sleep=true`): every model includes an invisible, silent, NoAI, scale-0.01 **husk** passenger (`watching.no_sleep`). Vanilla's "monsters nearby" rule then blocks sleeping near a sighting. When the option is off the husk is removed at creation.

### 2.11 Dwelling Shadow

Dwelling sightings in dark spots (light < 8, not Nether) become light-reactant:

- They cannot be spotted by the ray.
- They vanish when a player comes within 3.
- They lose the trait if the light at their position reaches ≥ 8.
- Encountering one grants `encounter_dwelling_shadow`.

### 2.12 Sighting Sense (dogs)

Applies per sighting after a successful roll (common table; bug A1). Every tamed wolf within 80 of the Herobrine armor stand:

- if sitting, `rotate` to face him every tick;
- every tick, `anger_end_time = gametime + 20` and `angry_at = Herobrine UUID`, so it growls and looks hostile;
- grants `encounter_dog_sense` to players within 15.

### 2.13 Haunted Herd

**Trigger:** 5-minute tick, `started_events`, default chance → `@r` → haunted_herd preset (§2.2). Disguised as a farm animal at stalking range (25–46 blocks).

**Model creation:**

- Mob: frog if the spread point is in swamp or mangrove_swamp (`spawns_frog` tag), otherwise a random pig or cow.
- Variant: `warm` if the biome is `#spawns_warm_variant_farm_animals` (or `_frogs`), `cold` if `#spawns_cold_variant_*`, else `temperate`.
- The skin list is temporarily replaced by `<variant>_<mob>`, e.g. `cold_cow`, then restored.
- No nametag. Jumpscare off.

**On spot:**

- Player within 20 → immediately `summon <mob>` with `variant:"minecraft:<variant>"`, the same yaw and pitch, 0.35 blocks behind, tag `watching.previously_haunted_herd`. The fake is removed.
- Otherwise after 1 s: the animal's **head rotates 180° at 5° per tick** (36t) with `face_player=false`. Then the body clamp is enabled: the body turns toward the head at 4 then 2 degrees per tick. When it is within ±2° (`clamped`), schedule the replace in 1 s.
- Any player within 7 at any time → replace.

Killing the real replaced mob grants `kill_a_haunted_herd_mob`.

The pig and cow models have parts `head`, `body`, `shadow`, front legs (`left_arm`/`right_arm`) and hind legs (`left_leg2`/`right_leg2`). The `shadow` part is a fake ground shadow, hidden (scale 0) at night 13000–22000 (`is_dark`). The frog is `body` + `shadow` only, with no animation.

### 2.14 Ghost Miner

**Trigger:** 5-minute tick, `started_events`, as `@r`. All must hold:

- the player is underground (y ≤ 55, no sky);
- strip-mine check: the blocks left and right of the player at eye level and one below (`^±1 ^ ^`, `^±1 ^-1 ^`, yaw-only) are all in `ghost_miner/surroundings` (base stone overworld, dirt, gravel, all ores);
- default chance.

**Effect:**

1. Grant `encounter_ghost_miner` (radius 10).
2. Pick a side at random: a point 5 blocks to the player's left or right, facing the player's eyes.
3. **Dig the tunnel** from there, rotated 180° (pointing away from the player). There are 20 segments of 5 blocks, so 100 blocks total. Each segment:
   - `fill ^0 ^ ^ ^ ^1 ^5 air replace #mineable` (stone/dirt/ores and water), giving a 1×2 tunnel;
   - places one `redstone_wall_torch` at y+1 on whichever adjacent side (±x/±z) is solid;
   - when underground, plugs liquids in the walls and ceiling with cobblestone and fills floor holes (air, water, lava, glow_lichen) with cobblestone.
4. Spawn the **ghost_miner** sighting, still, aligned to block centre 1 block further from the player, facing the player. After 1 s he holds an **iron pickaxe** (arm raised −15).
5. Mining sounds at Herobrine, category `block`, vol 0.1:
   - `block.stone.hit` pitch 0.5 at 12, 16, 20, 24t, then `block.stone.break` pitch 1 at 28t;
   - again hits at 34, 38, 42, 46t and a break at 50t.
   
   Heard through the wall.

### 2.15 Fearful Footsteps

**Trigger:** 5-minute tick, **no** `started_events` gate, as `@r`, common chance (bug A1).

**Effect:**

1. Remove Herobrine.
2. Pick a start with `random 1..5` (yaw-only): `^4` (left 4), `^-4` (right 4), `^6`, `^-6`, or `^ ^ ^-4` (4 behind).
3. The surface below the start picks the step sound: grass_block → `grass`, deepslate → `deepslate`, `#base_stone_overworld` → `stone`, sand → `sand`, `#planks` → `wood`. Anything else means no event.
4. Play **4 steps, 0.3 s apart**. Each is `playsound minecraft:block.<type>.step player @a` vol 0.2. The position advances 1 block per step in the direction the player faced at the start: walking alongside, or toward the player from behind.

### 2.16 Sinister Signs and item presets

**`signs/init_place`** (at Herobrine when removed after a window sighting):

- **Rotation.** Head yaw maps to the standing-sign `rotation`:

  | Yaw | rotation |
  |---|---|
  | 160..180 and −180..−160 | 8 |
  | 120..159 | 6 |
  | 70..119 | 4 |
  | 30..69 | 2 |
  | −29..29 | 0 |
  | −69..−30 | 14 |
  | −119..−70 | 12 |
  | −159..−120 | 10 |

- **Message:** `random 1..12`.
- **Wood type** (`biome_boards`): taiga → spruce; birch_forest and old_growth_birch_forest → birch; jungle → jungle; savanna → acacia; dark_forest → dark_oak; mangrove_swamp → mangrove; cherry_grove → cherry; crimson_forest → crimson; warped_forest → warped; else oak. With `biome_boards` off it is always oak.
- **Placement:** `fill ~ ~ ~ ~ ~ ~ <type>_sign[rotation=r]{front_text:{messages:[translate sinister_signs.<lang>.message.<n>.line1..4]}} replace #replaceable` (air, short/tall grass, snow, flowers, crops). Message 4 line 3 is replaced with the nearest player's name.

**Messages (en_us):**

| # | Text |
|---|---|
| 1 | "I'm watching..." |
| 2 | "Help" |
| 3 | "I see you." |
| 4 | "Hello there, \<player\>" |
| 5 | "WAKE UP." |
| 6 | "You are not / alone." |
| 7 | "Listen closely." |
| 8 | "Do you hear it / too?" |
| 9 | "Stop." |
| 10 | "Do I frighten / you?" |
| 11 | "You can't escape / this nightmare." |
| 12 | "What are you / looking at / candlehead?" |

The Swedish set is under the `swedish` key.

**Item presets** (`sightings/item/*`):

- `set {item}` starts the holding pose (right arm x −15) and, 1t later, sets the `right_arm_item` display's item.
- `iron_pickaxe` is used by the ghost miner.
- `sign` holds `<biome wood>_sign` (window watcher).
- `remove` clears the item and the pose.

### 2.17 Malevolent Shrine

**Detection.** Placing **fire** triggers advancement `placed_fire` and a raycast from the eyes in 0.5 steps, up to 10 steps (5 blocks), for a fire block. On fire, `shrine/check` passes if either layout matches:

- **classic:** netherrack at y−1, and 8 gold blocks in the ring around the centre at y−2 (the centre and redstone torches are not checked).
- **mod:** netherrack at y−1 and y−2, gold at y−3 and y−4 (a vertical column).

**Effect (`found_shrine`):**

1. Log the fire position.
2. `start_events` if not already started.
3. `shrine_strike` → lightning bolt on the fire.
4. Grant `build_shrine`.
5. Activity ×6 (360 s, events in 150 s).
6. **Caution Caption** (option): play `lunareclipse.watching:event.caution_caption.<lang>.message.<1..4>` to players within 15.
   - The OGG is **silent**; the text appears as the **subtitle**.
   - local: "I'm watching..." / "I see you." / "Listen closely." / "Do you hear it too?"
   - swedish: "Jag ser dig..." / "Jag ser dig." / "Lyssna noga." / "Hör du det också?"
7. **Shrine Surprise** (option): set `player_spotted=1` and spawn the `shrine_surprise` sighting standing **on the shrine fire**. It cannot be spotted and despawns in 2 s.

**Rekindling Shrine.** On each 5-minute tick after start, for each logged fire:

- if the shrine structure is still valid and no player is within 80, refill fire (air → fire);
- if the structure is gone, drop the entry.

### 2.18 Block-event framework

**Detection.** Advancements `technical/block_detection/placed_<x>` use `placed_block` for beds `#beds`, candles `#candles`, `chest`, doors `#doors`, `fire`, `lantern` and torches (`torch`, `wall_torch`). They raycast from the eyes, 0.5 steps, up to 10, to find the placed block. The block position (int x,y,z) plus **the player's yaw and pitch** is prepended to `block_positions.<tag>.positions`, de-duplicated.

Forests: advancement `placed_forest` (`location` in `#is_forest`) logs the player's own position under `#minecraft:is_forest`. It is re-armed every 5 minutes.

**Pruning** (`detection/remove/*`): iterate the list, forceload each position, and remove entries whose block no longer matches. Fire positions use the shrine check instead.

**Random event** (`events/random/init {option, block_tag}`):

1. Prune.
2. Pick a uniform random entry.
3. Forceload it.
4. Run `executables/<option>` positioned at the entry and rotated by the stored yaw and pitch.
5. Reset `<option>_roll`.

**Bedrock:** use `world.afterEvents.playerPlaceBlock`, which gives the exact block so no ray is needed. Store per-dimension lists in dynamic properties.

### 2.19 Block-event executables

All roll the default chance table with `<option>_chance` ÷ activity multiplier.

| Event | Preconditions at the stored block | Effect |
|---|---|---|
| **Burning Base** (default **off**) | Block is a bed, the block below is in `#flammable` (169 ids), no non-spectator within 50 | `fill ~1 ~ ~1 ~-1 ~1 ~-1 fire replace air`: 3×2×3 fire around the bed |
| **Disappearing Torches** | Torch; no non-spectator within 50 | `fill ±25` cube (51³): torches and wall torches → air. Prune lists. |
| **Crimson Curse** | Preset picks torch or lantern lists 50/50; no non-spectator within 50 | ±25 cube: torch → redstone_torch; wall_torch[facing] → redstone_wall_torch[facing] (4 facings). With `soul_shift`: lantern[hanging=t/f] → soul_lantern. Prune. |
| **Chilled Candles** | Candle; if unlit, re-pick another (up to 4 attempts); no non-spectator within 50 | For each of 17 candle colours, ±25 cube, candles=1..4 → `lit=false` |
| **Dreadful Donation** (runs before start) | Chest; no non-spectator within **25** | `loot insert` from `dreadful_donation/<lurking_language>_<haunted_heirlooms>` (§2.19a) |
| **Ghost Doors** (runs before start) | Door (no player-distance check) | Remove other ghosts and any Herobrine. Summon at `^ ^ ^-2` (yaw-only, so 2 blocks back toward where the player stood when placing the door) a **villager** with: Silent, Invulnerable, scale 0.1, nitwit, infinite invisibility, team `watching.door_ghost` (no collision). Its AI wanders and **opens doors**. It despawns (tp to y −512) when standing in a door or bed block, or after 30 s. While it exists no sighting can spawn. |
| **Leafless Grove** | Forest log entry (always consumed); no non-spectator within **78** | Remove non-persistent leaves in a 51×101×51 box (±25 h, ±50 v). Remove logs in the 2-block border ring and border leaves, so cut trees don't float. Chain: repeat at `^ ^ ^50` and `^ ^ ^100` along the stored facing, 3 clears total. |

**2.19a Dreadful Donation loot.** One roll; weights 70 / 30 / 90, plus 5 more when heirlooms are on.

- **70: redstone_torch**, named one of "Help." / "I see you." / "You are not alone." (Swedish set for `swedish`), `custom_data{dreadful_donation, named_donation}`.
- **30: music_disc_11**, with the same names.
- **90: random:** mossy_cobblestone, rotten_flesh or sandstone (`custom_data{dreadful_donation}`).
- **5: haunted_heirlooms** (when on), one of:
  - a painting item preset to a custom variant: `a_distant_memory` 2×2, `redstone_halls` 3×2 or `watching` 2×2;
  - a `music_disc_11` re-skinned (`item_model`) and re-pointed (`jukebox_playable`) to `herobrine_lullaby` (163 s), `deathbed` (157 s) or `hes_from_the_fog` (130 s), comparator 12.

Advancements: `get_donation`, `get_named_donation`, `get_haunted_heirloom` (`inventory_changed` with custom_data).

### 2.20 Twisted Tapestries (default **off**)

**Attempt:** 5-minute tick after start, as `@r`, common chance (bug A1). The nearest painting within 30 whose variant is one of **creebet, backyard, burning_skull, fighters, humble, meditative, pigscene, skeleton, unpacked** is swapped to `lunareclipse.watching:twisted_tapestries/<same>` and tagged `watching.twisted_tapestry`.

**Revert:** every tick per player, if a twisted painting within 60 is inside a 0.70 cone (about 41°) and a 0.5-step ray (256 steps, through `sight_blocks`) gets within 2.2 of it, revert to vanilla. **The painting is only creepy while you are not looking at it.**

### 2.21 Dynamic structure placement (§4 covers worldgen)

`structures/place/start {structure}` runs every 5 minutes for each structure not set to `off`:

1. Roll 1/25, 1/100 or 1/200. In Java this reads the undefined `structure_chance` (bug A2), and the outer `execute if loaded ~ ~ ~` is evaluated at the scheduled function's origin, the world spawn.
2. On success, pick and **remove** a random saved candidate position.
3. Forceload ±24 around its centre.
4. **Collision check:** `execute if blocks ~2 ~ ~2 ~-2 <h> ~-2 ~ ~ ~ masked`, an all-air or unchanged box test. Size is 5×3×5 for letter and 5×6×5 for the others.
5. `place structure lunareclipse.watching:<s>/structure`: a jigsaw with a single pool that picks a random template, pyramid1 or pyramid2, letter H/E/R/O/B/I/N, or the cross.
6. Summon marker `watching.structure_marker {data:{structure}}`. A player within 16 grants `find_any_structure` and `find_<s>`.

### 2.22 Advancements (`from_the_fog`, enabled by the `advancements` option)

`root` (hidden, granted by the 1s tick). All icons are `paper` with an `item_model` override (`ui/advancements/...`).

| Group | Advancements |
|---|---|
| haunting | `build_shrine` (goal), `first_encounter`, `encounter_dwelling_shadow`, `encounter_dog_sense`, `encounter_ghost_miner`, `encounter_pants` (hidden), `experience_nightmare`, `get_jumpscared`, `missed_herobrine`, `punch_herobrine`, `kill_a_haunted_herd_mob` |
| structure | `find_any_structure`, `find_letter` (title `find_glowstone_letter`), `find_sand_pyramid`, `find_wooden_cross`. There is no mossy one, so `find_mossy_pyramid` grants fail silently. |
| time_played | `3_days`, `10_days` (goal), `50_days` (goal), `100_days` (challenge), `200_days` (challenge). Days = play_time / 24000. |
| dreadful_donation | `get_donation`, `get_named_donation`, `get_haunted_heirloom` |
| found_footage (hidden challenges) | `archi`, `comicalbonzo`, `godis`, `the_librarian`: placing a block as specific developer **UUIDs**. `the_librarian/create_liberty`: giving a lantern to an allay. Developer easter eggs; skip. |

**Bedrock:** there are no custom advancements. Implement a script "Journal" of achievements: per-player dynamic property bitset, a toast via `onScreenDisplay.setTitle` or the action bar plus a sound, and an `ActionFormData` list.

### 2.23 Custom sighting API

`/function lunareclipse.watching:sightings/custom/summon {position, max_distance, vanishing_delay, spot_fov, jumpscare, despawn_timer, face_player, light_reactant, sneaky_strike}`

- `position` is any spread mode: creeping, dwelling, lurking, stalking, nightmare or still.
- It sets `custom_vanishing_delay` and clears the scheduled sighting.

Helpers:

- `custom/rotate {location}`: face a location.
- `custom/toggle_facing_player`.

**Bedrock:** `/scriptevent ftf:summon {"position":"creeping",...}` handled via `system.afterEvents.scriptEventReceive`.

### 2.24 Misc options

- **nametag**:
  - `dynamic` → the hitbox `CustomName` is `entity.nametag.<skin>.value`, e.g. "Herobrine";
  - `cryptic` → the same, obfuscated;
  - never shown on the haunted herd;
  - mannequin mode forces the default name.
  
  Bedrock: `entity.nameTag` (§k for obfuscated).
- **hoebrine_returns**: always uses skin `hoebrine` (its credit is "These hoes ain't loyal.") and also for the config preview.
- **from_the_pants**: only the `left_leg` and `right_leg` parts are built.
- **emissive_layers**: emissive parts get `brightness {sky:15, block:15}`. There is no UI for it.
- **vanilla_sightings** (experimental): instead of the item-display model, summon a Java `mannequin` (player-model entity). Settings: profile texture base64 for profile "Cleysen", cape hidden, `pose:"standing"`, `immovable`, `hide_description`, Silent and Invulnerable. It is synced to the AI stand. **Bedrock:** the custom entity already uses player geometry; ignore.

---

## 3. Herobrine representation

### 3.1 Java entity stack

`models/create` → `create_model` summons at the spread entity position:

```
armor_stand  (Invisible, Marker, NoGravity; tags watching.herobrine, watching.ai)   ← root / "AI"
 ├─ interaction  0.6 × 1.8, Rotation [12,0]  (watching.hitbox)   ← click/arrow/nametag target
 ├─ husk  scale 0.01, NoAI, Silent, PersistenceRequired, invisible (watching.no_sleep)  ← blocks sleeping
 ├─ item_display ×2 per part:  base model  +  emissive model (brightness 15 if emissive_layers)
 │     item = white_dye with item_model "<ns>:skins/<skin>/<part>" and ".../emissive/<part>"
 │     tags watching.model, watching.<part>[, watching.emissive]; view_range 1; transformation = pivot
 └─ item_display right_arm_item (item_display: thirdperson_righthand, empty until item/set)
```

- **Parts:** `head`, `body`, `left_arm`, `right_arm`, `right_arm_item`, `left_leg`, `right_leg`. Optional parts are `shadow` and `left_leg2/3`, `right_leg2/3` for animals and spider-like skins; some skins omit parts.
- **Default pivots** (translation in blocks; 1 px = 0.0625):

  | Part | Translation | Notes |
  |---|---|---|
  | head | (0, 1.5, 0) | |
  | body | (0, 0.75, 0) | |
  | left_arm | (0.375, 1.375, 0) | |
  | right_arm | (−0.375, 1.375, 0) | |
  | right_arm_item | (−0.375, 0.8125, 0.3125) | `right_rotation` [0.7, 0, 0, 1], about 70° x |
  | legs | (±0.125, 0.6875, 0) | |

- **Special skins:**
  - `slenderman`: head y 2.5, body 1.5, arms scale 2 at y 2.375.
  - `the_man_from_the_fog`: head y 2.21875 tilted, arms scale 2.
  - Pig and cow: head (0, 0.75, 0.1875), body z −0.25, legs at z −0.0625 and −0.8125.
  - Frog: body y 0.5.
  - `white_eyes`: 6 splayed legs, spider-like.
- **Initial yaw** faces the nearest player's feet. The breathing animation starts.
- **Rendering.** Each part is a Java item model (`models/item/skins/<skin>/<part>.json` → parent `skins/template/<part>`, 64×64 player-skin UV, overlay layer cubes inflated 0.5 px; slim arm templates exist). The texture is `textures/item/skins/<skin>/skin.png` plus `skin_emissive.png`.
- **Core shader** `assets/minecraft/shaders/core/item.fsh` makes any pixel of exact colour **#FEFEFE (0.996)** render at 2.5× brightness. These are the "extra glowy eyes".

### 3.2 Skin selection

`schedule_create`:

1. Drop unknown skins from `options.herobrine_skin.list`.
2. Pick a random entry → `options.herobrine_skin.selected`.
3. If the entry is a **variant group** (`werewolf` → `werewolf_brown`, `werewolf_brown_clothed`, `werewolf_gray`, `werewolf_gray_clothed`), pick randomly inside it.

### 3.3 Animation system

Every part has scoreboard-driven targets. `animations/update {part, axis, rotation, interpolation}` sets `<part>_axis_<axis> = rotation`, `<part>_interpolation`, and `<part>_update=2`. While `update ≥ 1`, the part's tick writes `transformation.left_rotation[x,y,z] = axis × 0.01` (**non-normalised quaternion components with w = 1**), `interpolation_duration = N` and `start_interpolation = -1`. Real angle = 2·atan(0.01·value).

| Animation | Keyframes (value → real angle) |
|---|---|
| Breathing (loop) | `out`: left_arm z +5, right_arm z −5, right_arm_item z +5 (≈5.7°), over 30t. After 32t `in`: all back to 0 over 30t, then after 32t `out` again. One cycle is 64t. Stopping returns the arms to 0. |
| Walking (loop) | `cycle1`: legs ±50 (≈53.1°; left +, right −, also leg2/leg3), arms ∓50 (right +50, left −50), 7t interpolation. After 7t `cycle2` mirrors it; 14t per cycle. Stop: all to 0 over 2t, then restart breathing. Walking stops breathing. |
| Punch | `start`: stop breathing; right_arm x −110 (≈−95.5°), z −35 (≈−38.6°), y −5; left_arm x −5; body y −5; 4t interpolation. Body yaw snaps to head yaw. +2t `next`: right_arm x −110, y 0, z +25 (≈28.1°) over 5t; left_arm x +5; body y +5 (4t). +3t `end`: right arm x/y/z → 0 (3t), left arm and body → 0 (4t). Breathing restarts 5t later. |
| Holding | right_arm x −15 (≈−17.1°), instant. Sets `holding_item=1`. |
| Head tilt | head z −15 (≈−17.1°) over 5t; stop → 0. |
| Head look | Each tick head `Rotation[0]` = AI yaw, `left_rotation[0] = pitch × 0.008` (pitch 15 → ≈13.7°), 5t interpolation. |
| Body follow (`rotation_math`) | Every non-head part keeps its own yaw. If the head−body difference exceeds 45°, the body turns 5° per tick toward the head. With `clamp_rotation=false` it also turns 4 and 2 degrees per tick until within ±2, then `clamped`. Slip-behind pushes the body sideways. |
| Scripted yaw (`rotate/start`) | The AI yaw rotates `increment` per tick up to `rotation` total, then runs a command. Used by the haunted herd: 180° at 5° per tick. |

### 3.4 Skin library (95 definitions)

Ids are listed with author; all use namespace `lunareclipse.watching`. Config pages:

- **p1:** default (Mojang), default_shaved (Zwaluw), classic (Mojang), classic_shaved (Mojang), lunar_eclipse_version (Zwaluw & Bret06), millerbrine, millerbrine_classic (Millerbrine), him (Bret06), glitch (Nox; animated), lunar_eclipse (Bret06), apparition (Bret06; no legs), zombie (Bret06; no arms), nullified (Nox), fallen_kingdom (Zwaluw), sculk_infested, crimson_infested, smile (Bret06), shattered (KJFox12), birthday (Zwaluw; no arms), jack_black (Zwaluw; has bbmodel).
- **p2:** tennis_, tuxedo_, athlete_, scottish_, prisoner_, cyclist_, boxer_ and developer_herobrine (4J Studios); lonely, roseate, trench_diver and frozen_wanderer (PJ The Sqweepsss).
- **p3:** entity_303 (LexLim), entity_303_classic (Thespeed179), null (Unknown), black_eyed and black_eyed_classic (Mojang), faceless (Bret06), distorted (LexLim; animated head), distorted_classic (Unknown), alex and alex_classic (Mojang), lick (Bret06), lick_classic (Giovani), white_eyes (CharlieGamer4896 & LexLim; spider-like), hat_man (Bret06).
- **p4:** nowhere (Zwaluw; animated), werewolf (variant group; Bret06, Nox & LexLim), the_man_from_the_fog (Mado, modified by Zwaluw & Bret06), slenderman (Zwaluw), cursed_oak (4J Studios), freddy_fazbear (LexLim).
- **p5:** the_librarian (Bret06), comicalbonzo, tokimi, notvixios, pixelnaut, derpblock, vivilly, mackheaththeknife (each by the same-named creator).
- **p6:** farlander, lumbering_jack, grimreaper (no legs), white_pumpkin (animated emissive) and ghost_costume (all Bret06); the_headless (Zwaluw), mr_boots (Mystery Manor), scarecrow (Zwaluw; head and body only), invisible_man (Bret06 & Zwaluw).
- **p7:** notch, notch_v2 (Zwaluw), notch_classic (Notch), notch_v2_classic (halucid).
- **Not on any page:** hoebrine (SSS), swayle (Swayle), sqannky_duck (Sqannky Duck), white_eyes_classic, the werewolf sub-variants, and the herd skins `{temperate,warm,cold}_{pig,cow,frog}` (Mojang).

**Bedrock mapping:**

- One entity `ftf:herobrine` with an int property `ftf:skin` (0..N). The render controller uses `Array.skins[query.property('ftf:skin')]` and picks geometry by skin class:
  - `geometry.ftf.humanoid` (64×64 classic arms; most skins);
  - `geometry.ftf.humanoid_slim` (alex);
  - partial-part geometries (apparition, zombie, birthday, scarecrow, grimreaper, nowhere, werewolf, freddy), or visibility via part_visibility;
  - `geometry.ftf.tall` (slenderman, the_man_from_the_fog; export from the provided `.bbmodel`);
  - `geometry.ftf.white_eyes`.
- The skin PNGs are standard 64×64 Java skin layouts and can be reused directly.
- Haunted herd: separate entities `ftf:fake_pig`, `ftf:fake_cow` and `ftf:fake_frog` using vanilla textures and geometries, or properties on one entity.
- Emissive: a second render-controller layer with the `skin_emissive.png` texture and an emissive material, the spider-eyes approach (`entity_emissive_alpha` / `entity_alphatest` + emissive). It can be toggled by a molang property for `emissive_layers`. The #FEFEFE glow shader has no equivalent; bake the glow into the emissive texture.
- Animated textures (`.mcmeta` on glitch, nowhere, him eye, distorted head, white_pumpkin emissive) → UV-animated texture strips driven by `query.anim_time` in the render controller (`uv_anim`) or frame textures swapped by molang.
- Animations: a Bedrock `.animation.json` per keyframe set above, in **degrees** (use the real angles), plus an animation controller keyed on properties `ftf:anim` (enum idle, walk, punch), `ftf:holding` (bool) and `ftf:tilt` (bool). Breathing loops at 3.2 s (64t), walking at 0.7 s (14t), punch 0.5 s.
- Head look via `query.target_x_rotation`/`head_y_rotation` (entity rotation set by script `teleport(..., {facingLocation})` / `setRotation`). Body lag via `minecraft:body_rotation_blocked` plus script-driven `ftf:body_yaw` property, or vanilla `body_rotation` with max head rotation 45° (`minecraft:behavior.look_at_player`-like `max_head_rotation`) as an approximation.
- Held items: `minecraft:equippable`, set with `entity.runCommand("replaceitem entity @s slot.weapon.mainhand 0 iron_pickaxe")`, plus `enable_attachables` in the client entity.

---

## 4. Worldgen structures

**Active (`lunareclipse.watching`).** Each structure has a `<s>/base` worldgen structure registered in a `structure_set`, plus a `<s>/structure` jigsaw that is used only by the function placement. Both use jigsaw `size 1`, step `surface_structures`, `start_height absolute 0`, `project_start_to_heightmap WORLD_SURFACE_WG`, `terrain_adaptation beard_thin` and `max_distance_from_center 80`. All are `random_spread` with `frequency_reduction_method legacy_type_2`.

| Structure | Biomes (tag) | spacing / separation (chunks) | frequency | salt | Base .nbt (worldgen) | Visible templates (dynamic) |
|---|---|---|---|---|---|---|
| mossy_pyramid | forest, birch_forest, old_growth_birch_forest, windswept_forest | 5 / 3 | 0.6 | 2025997793 | `mossy_pyramid/base.nbt` 5×4×5: 75 cave_air + 2 markers | `pyramid1.nbt` 3×3×3 (3×3 layer of mossy cobblestone plus a top block, 10 blocks); `pyramid2.nbt` 5×4×5 (5×5, 3×3 and 1 layers, 35 blocks) |
| sand_pyramid | desert, beach | 8 / 5 | 0.6 | 10387313 | `sand_pyramid/base.nbt` (same layout) | `pyramid1/2.nbt` in **sand** (10 / 35 blocks) |
| letter | desert, plains | 10 / 5 | 0.3 | 10083237 | `letter/base.nbt` 1×6×3, cave_air + markers | `letter_{h,e,r,o,b,i,n}.nbt`: glowstone letters, 1 thick × 5 tall (H and R and N are 4 wide, others 3) |
| wooden_cross | same as mossy | 6 / 4 | 0.4 | 1065294793 | `wooden_cross/base.nbt` 5×7×3, markers only | `wooden_cross.nbt` 5×7×3: oak log post 6 high, 5-wide crossbar at y4, 2 lit redstone wall torches on the crossbar (N and S faces) |

**Runtime flow (Java).**

1. Worldgen places only the invisible **base**. It holds two markers, `watching.structure` and `watching.structure`+`watching.center`, each with `data.structure=<name>`.
2. On the first tick a marker is loaded, `remove_base/<s>` clears the footprint to air around the centre marker: 5×3×5 for the pyramids, 5×6×5 for the letter and cross.
3. `save_location` stores {x,y,z} (non-centre marker) and {x,y,z}_center in `structures.<s>.positions`.
4. The forceload is removed and both markers are killed.
5. Later, §2.21 places a random visible template at one saved spot, so structures **appear** while the player is in the world.

**Legacy stubs (`data/watching/worldgen`).** These are `leafless_grove`, `mossy_pyramid`, `ocean_sand_pyramid`, `redstone_torch_trail`, `sand_pyramid`, `torch_cave`, `logless_tree/{acacia,birch,oak,spruce}` and `torch_tunnel/{2x2_tunnel, 1x2/tunnel, 1x2/torchless_tunnel}`. All are jigsaws with **empty biomes**, an empty pool `watching:draft` and **no structure_set**, so they never generate. They are kept only so old worlds referencing them load. "Logless trees" and "torch tunnels" (and the README's "2x2 tunnel", "ocean sand pyramid") have **no data in this repo**; recreate them from scratch if wanted.

**Development structures** (`structure/development_structures/*.nbt`): `haunted_manor` 25×22×24 (DataVersion 3117, dark oak/spruce, 102 barrels, cobwebs, ~30 bats, candles), `original_test_house` 7×5×7, `puredominace_house` 9×16×13, `simple_house` 16×10×15 (chest contains a "Help." redstone torch), `third_encounter` 18×15×15. None are referenced by any function or worldgen. Test builds; skip.

**Bedrock plan:**

- Convert the templates to `.mcstructure`: `ftf:mossy_pyramid_1`/`_2`, `ftf:sand_pyramid_1`/`_2`, `ftf:letter_h` … `_n`, and `ftf:wooden_cross`. Rebuild by hand in a Bedrock world and `/structure save`; they are tiny. Alternatively write a converter: block names are 1:1 except `redstone_wall_torch` → `redstone_torch` with `torch_facing_direction`.
- Candidate positions: use **feature rules** (`minecraft:feature_rules` with `minecraft:structure_template_feature` or `single_block_feature`) to place an invisible custom block `ftf:structure_anchor`. Filter per biome with `has_biome_tag` (forest, birch, desert, beach, plains, extreme_hills/windswept). Scatter chance should approximate spacing: 1 per about 5×5, 8×8, 10×10 and 6×6 chunks at the given frequency.
- The anchor block's custom component `onTick` (or `onPlace`) records its position in a dynamic property list, clears the 5×h×5 footprint, then sets itself to air.
- The 5-minute script loop applies the chance and the all-air check, and calls `world.structureManager.place(id, dim, loc)` with a random template id.
- Experimental data-driven jigsaw structures (Bedrock "Jigsaw Structures" experiment) are an alternative, but are not needed.

---

## 5. Resource pack contents (`assets/`)

**Counts:** 2736 files: 2286 json, 396 png, 31 ogg, 16 mcmeta, 5 bbmodel, 1 fsh.

- **Skins:**
  - `items/skins/<skin>/{head,body,left_arm,right_arm,left_leg,right_leg}.json` plus `emissive/…`: Java 1.21.4+ item-model definitions, `oversized_in_gui`;
  - `models/item/skins/<skin>/…` with parents in `models/item/skins/template/{head,body,left_arm,right_arm,left_arm_slim,right_arm_slim,left_leg,right_leg,template}.json` and `template_frog/{base,emissive}.json`;
  - `textures/item/skins/<skin>/skin.png` and `skin_emissive.png` (64×64; 81 skin dirs plus `shadow.png`). Extras: werewolf `claws/teeth/vest`; glitch `animated_eyes`, `glitch`, `watching_anim` (animated).
  - Herd skins reuse vanilla entity textures, made available to item models via `assets/minecraft/atlases/blocks.json`, which adds `entity/pig`, `entity/cow` and `entity/frog` to the blocks atlas. `pig_ftf.png`, `cow_ftf.png` and `frog_ftf.png` are added there.
  - Blockbench sources: `skins/the_man_from_the_fog/full.bbmodel`, `jack_black/{base,running_base}.bbmodel`, `lonely/lonely.bbmodel`, `ui/config/wardrobe.bbmodel`.
- **Config UI models and textures** (`items|models/item/ui/config/...`):
  - `main_menu`, `main_menu_glitched`, `option_page_info`, `option_page_settings`, `next_page`, `wardrobe`;
  - `settings/{switch_on,switch_off,slot_on,slot_off,skin_on,skin_off,int_display,int_increase,int_decrease,spinning_true,spinning_false,give_him_control_switch}`;
  - `icons/<page>` for each of the 16+ pages plus `super_secret_settings`, `lights_out`, `vanishing_delay_old`;
  - logo `ui/text` and `ui/text_red`; panel textures (border, corner, inner, isolated, `panel_front_glitched` animated).
  
  Bedrock: not needed with forms; reuse the icon PNGs as `ActionFormData` button icons (`textures/ftf/ui/icons/<page>`).
- **Advancement icons:** `items|models|textures/item/ui/advancements/...` (layered 3D icons, some animated); `textures/ui/advancement_background.png`.
- **Music discs:** `items/{deathbed,herobrine_lullaby,hes_from_the_fog}.json`, models and textures (16×16 disc icons).
- **Paintings** (`textures/painting/`):

  | Painting | Size (px) | Blocks |
  |---|---|---|
  | `a_distant_memory` | 64×64 | 2×2 |
  | `redstone_halls` | 96×64 | 3×2 |
  | `watching` | 64×64 | 2×2 |
  | `twisted_tapestries/backyard` | 48×64 | |
  | `twisted_tapestries/burning_skull` | 64×64 | |
  | `twisted_tapestries/creebet` | 32×16 | |
  | `twisted_tapestries/fighters` | 64×32 | |
  | `twisted_tapestries/humble` | 32×32 | |
  | `twisted_tapestries/meditative` | 16×16 | |
  | `twisted_tapestries/pigscene` | 64×64 | |
  | `twisted_tapestries/skeleton` | 64×48 | |
  | `twisted_tapestries/unpacked` | 64×64 | |

  Datapack `painting_variant/*.json` defines these: the 3 heirlooms plus the 9 twisted variants, with titles and sizes matching vanilla.
- **Fonts:** `font/config.json` (ascii bitmap) and `font/ui.json` (adds `ꨀ` → `textures/font/ui/failed.png`, `ꨁ` → `passed.png`).
- **Shader:** `minecraft/shaders/core/item.fsh` (#FEFEFE glow).
- **Vanilla overrides:** `minecraft/sounds/entity/armorstand/break1-4.ogg`, silent (6445 B, identical to `caution_caption.ogg`), so killing the Herobrine armor stand makes no sound.
- **Lang:** 32 files: af_za, bg_bg, cs_cz, da_dk, en_au, en_ca, en_gb, en_nz, en_ud, en_us, enws, fa_ir, fi_fi, fr_fr, he_il, hu_hu, id_id, it_it, ja_jp, lol_us, lt_lt, nl_be, nl_nl, no_no, pl_pl, ro_ro, rpr, ru_ru, sr_cs, sr_sp, th_th, tr_tr. `en_us` has 627 keys: `config.*`, `entity.nametag.<skin>.value`, `sinister_signs.*`, `caution_caption.*`, `dreadful_donation.message.1-3`, `eerie_entrance.*`, `jukebox_song.*`, `painting.*`, `advancements.from_the_fog.*`. Bedrock: convert to `texts/<locale>.lang` (`key=value`) in both BP and RP; use `{rawtext:[{translate}]}`.
- **sounds.json** (namespace `lunareclipse.watching`):

  | Event id | Files |
  |---|---|
  | `event.caution_caption.{local,swedish}.message.{1-4}` | `events/caution_caption` (silent), with `subtitle` = the caption text key |
  | `events.sudden_scare.jumpscare` | `events/sudden_scare/jumpscare1..5` (random) |
  | `events.sudden_scare.crash` | `events/sudden_scare/crash` |
  | `events.suspenseful_sighting.{default,slenderman,werewolf}` | `events/suspenseful_sighting/*` |
  | `config.setting.<id>.<true/false>` (about 70 ids) | `config/<id>_<true/false>.ogg` for burning_base, disappearing_torches, dreadful_donation, sudden_scare (both values); `ghost_doors_true`, `hoebrine_returns_true`, `malicious_malfunction_true`, `shrine_strike_true`; `sightings_selected` for the 5 sighting toggles; `default.ogg` for all others |
  | `music_disc.{herobrine_lullaby,deathbed,hes_from_the_fog}` | `music_disc/*.ogg` (6.36 MB / 1.72 MB / 5.96 MB) |

  Bedrock: `sound_definitions.json` with `ftf.*` ids, category `hostile`/`ui`/`record`, and `"stream": true` for the discs.

---

## 6. Java-only techniques → Bedrock equivalents

Assume @minecraft/server 2.x and @minecraft/server-ui 2.x on Bedrock 1.26. Items marked *(verify)* need checking against the exact 2.x minor version you target.

| Java feature used | Where | Bedrock equivalent |
|---|---|---|
| `#minecraft:load` / `tick` function tags | main | `world.afterEvents.worldLoad` (init) + `system.runInterval(tick, 1)`; 1 s loop `runInterval(…,20)`; the 5-min loop as a countdown variable, so the activity multiplier can shorten it |
| `schedule function … Ns` / `schedule clear` | everywhere | `system.runTimeout(fn, ticks)`; keep handles to `system.clearRun()` |
| Scoreboards as variables | everywhere | Plain JS module state. Persist across reloads with `world.setDynamicProperty` (JSON). Per-player state via `player.setDynamicProperty`. |
| NBT storage `data modify storage` | config, positions, skin library | JSON in world dynamic properties (32 767 chars per string, so shard lists like `ftf:pos:<tag>:<n>`); skin library as a static JS table |
| Function macros `$(...)` + `lunareclipse.utils` loops and random | everywhere | Native JS functions, arrays, `Math.random()` |
| `random value a..b` | everywhere | `Math.floor(Math.random()*(b-a+1))+a` |
| Predicates `random_chance`, `time_check`, `weather_check`, `can_see_sky`, light, position | checks | `Math.random()`, `world.getTimeOfDay()` (13000–24000 night; 13000–22000 dark), weather via `world.afterEvents.weatherChange` cache or `dimension.getWeather()` *(verify)*. Sky: `dimension.getTopmostBlock({x,z}).y <= player.y`. Light: `dimension.getLightLevel(loc)` / `getSkyLightLevel` *(verify; fallback: time of day + sky exposure + nearby light-block scan)*. |
| `looking_at` (fixated, tooltips) | GHC jumpscare | `player.getEntitiesFromViewDirection({maxDistance:128})[0]?.entity` is Herobrine |
| `facing`-vector + `distance=..0.xx` FOV cone | spot, suspense, tapestries | Compute `acos(dot(normalize(player.getViewDirection()), normalize(target − player.getHeadLocation())))` against the angle table in §2.2 |
| Recursive 0.5-block ray through block tags | spot, tapestries, placed-block finder | DDA or 0.5-step march with `dimension.getBlock(v)`, testing `typeId` against JS sets (sight_blocks, glass), max 256 steps. Alternative: `dimension.getBlockFromRay(eye, dir, {maxDistance, includePassableBlocks:false, includeLiquidBlocks:false})`, then check whether the hit is glass (window watcher) and continue past it. |
| Advancements as event triggers (`placed_block`, `location` biome, `slept_in_bed`, `player_hurt_entity`, `player_interacted_with_entity`, `inventory_changed`, `player_killed_entity`, `tick`) | detection, config, nightmare, punch | - `world.afterEvents.playerPlaceBlock`<br>- periodic biome check via `dimension.getBiome(loc)` *(verify)*, else a block heuristic (logs + leaves density)<br>- poll `player.isSleeping` *(verify)* or `playerInteractWithBlock` on a bed at night + `player.isSleeping`<br>- `world.afterEvents.entityHitEntity`<br>- forms replace the config clicks<br>- `playerInteractWithBlock` on chests (scan the container for tagged items)<br>- `world.afterEvents.entityDie` (the dead entity has tag `ftf:previously_haunted_herd`, the damager is a player) |
| Custom advancements and toasts | §2.22 | No equivalent. Script achievement journal plus title or action-bar toast. |
| `item_display` / `text_display` / `interaction` / `marker` multi-entity rigs | Herobrine model, config UI, markers | One **custom entity** `ftf:herobrine` (RP geometry, textures, animations, render controllers; BP entity with no AI, `minecraft:physics` (has_gravity false or true), `minecraft:collision_box` 0.6×1.8, `minecraft:pushable` false, `minecraft:damage_sensor` to cancel damage and forward to script, `minecraft:nameable`, `minecraft:persistent` false, properties `ftf:skin`, `ftf:anim`, `ftf:holding`, `ftf:tilt`, `ftf:emissive`). Markers become stored coordinates. The config UI uses `@minecraft/server-ui` forms. |
| Display-entity `transformation` + interpolation | animations | Bedrock keyframe animations and animation controllers, triggered by properties (`entity.setProperty`) |
| Armor-stand passengers and `rotate` / `tp facing` | AI root | `entity.teleport(loc, {facingLocation})` / `entity.setRotation({x,y})`; client head via `query.head_x_rotation` |
| Riding invisible husk (block sleep) | no_sleep | Give `ftf:herobrine` `minecraft:type_family` `["monster","herobrine"]` (Bedrock blocks sleeping when monsters are near) *(verify)*. Fallback: `world.beforeEvents.playerInteractWithBlock` on beds cancels while a sighting is within 8 horizontal / 5 vertical blocks, with action-bar "You may not rest now; there are monsters nearby". |
| `mannequin` with profile | vanilla_sightings | None needed: the custom entity already uses player geometry. Skip the option. |
| `summon villager` with `attributes scale 0.1`, nitwit, invisibility, team no-collision | ghost doors | Custom entity `ftf:door_ghost`: invisible geometry, collision 0.2×0.4, `minecraft:navigation.walk` with `can_open_doors:true`, `can_pass_doors:true`, `minecraft:behavior.open_door` (`close_door_after:true`), `random_stroll` / `move_indoors`, no sounds, `pushable` false. Script despawns it in a door or bed block or after 30 s (`entity.remove()`). |
| Wolf `angry_at` / `anger_end_time` NBT | sighting sense | No API to set wolf anger. Use `wolf.setRotation` toward Herobrine when sitting (`minecraft:is_sitting` via `entity.getComponent` / property *(verify)*) plus periodic `dimension.playSound("mob.wolf.growl", wolf.location)`. |
| `data modify entity @s variant` (paintings) / custom `painting_variant` | twisted tapestries, heirlooms | **No custom painting variants in Bedrock**, and the Script API cannot read a painting's motif. Heirloom paintings: custom placeable entities `ftf:painting_<name>` (flat quad geometry, size 2×2, 3×2, 2×2) spawned by an item's `itemUseOn`. Twisted tapestries: partial; spawn a thin overlay entity `ftf:tapestry_<motif>` in front of a painting entity (`dimension.getEntities({type:"minecraft:painting"})`), sized by a script-measured bounding guess, removed when looked at. Or drop the feature (default off). |
| `jukebox_song` + `jukebox_playable` component | heirloom discs | Custom items with `minecraft:record` (`sound_event`, `duration`, `comparator_signal`: 12). Custom `sound_event` support is limited *(verify)*. Fallback: on `playerInteractWithBlock` with jukebox + ftf disc, cancel and play `ftf.music_disc.<id>` via `dimension.playSound`; on jukebox break or eject, `/stopsound`. |
| `loot insert` from loot tables | dreadful donation | `block.getComponent("minecraft:inventory").container.addItem(new ItemStack(...))` with `nameTag` and `setLore(["§8ftf:donation"])` as a marker. Optionally a BP `loot_tables/ftf/donation.json` + `/loot insert` via `dimension.runCommand` (Bedrock supports `/loot insert x y z loot <table>`). |
| `set_components custom_data` / `item_model` | donations, discs, config | Custom items `ftf:disc_*`, `ftf:painting_*`; item lore or dynamic properties (non-stackable) as markers |
| `fill … replace <state>` over 51³ | candles, torches, leaves | `dimension.fillBlocks(volume, perm, {blockFilter:{includeTypes:[...]}})` for type swaps. Per-block state changes (candle `lit`, torch facing) use `dimension.getBlocks(volume, filter)` and iterate with `block.setPermutation(perm.withState(...))` inside `system.runJob` generators. Bedrock state names differ: candles are 0-based `candles` 0–3, `lit`; torches use `torch_facing_direction`; there is no separate wall_torch id. Respect the per-tick budget. |
| `forceload add/remove` | block events, structures, sightings | No Script API forceload. Use `dimension.runCommand("tickingarea add …")` (max 10 areas), or only run an event when its chunk is loaded (`dimension.isChunkLoaded(loc)` / try-catch `LocationInUnloadedChunkError`). Choose stored positions that are loaded but more than 50 blocks from players (inside simulation distance). |
| `execute if blocks … masked` (collision) | structures | Loop over the box, all `block.isAir` |
| `place structure` (jigsaw) | structures | `world.structureManager.place("ftf:<template>", dimension, loc)` with a random template chosen in script |
| Worldgen `structure` / `structure_set` / `template_pool` / biome tags | §4 | Feature rules + anchor block + script (§4), or experimental data-driven jigsaws |
| `place`d markers in `.nbt` | §4 | Custom anchor block with a custom component `onTick` (registered in `system.beforeEvents.startup` via `blockComponentRegistry`) |
| Heightmap `positioned over motion_blocking_no_leaves` | stalking, lurking | `dimension.getTopmostBlock({x,z})`, then step down while the block is leaves |
| Block tags (`#minecraft:beds`, `#doors`, `#candles`, `#leaves`, `#logs`, `#flowers`, custom tags) | everywhere | JS sets of Bedrock ids, or `block.hasTag()` where Bedrock tags exist (`log`, `wood`, `stone`…) *(verify ids per tag)* |
| Biome tags (`#is_forest`, `#is_taiga`, `#spawns_warm_variant_*`) | grove, signs, herd | `dimension.getBiome(loc).id` mapped to JS sets *(verify API)*. Herd variants: spawn `minecraft:pig` / `cow` / `frog` normally (Bedrock assigns climate variants by biome at spawn) *(verify spawn event)*. |
| `summon … {variant:"minecraft:cold"}` | herd replace | `dimension.spawnEntity("minecraft:cow", loc)` (variant auto by biome), then `setRotation` and `addTag("ftf:previously_haunted_herd")` |
| `damage @s N type by` | punch, hit | `player.applyDamage(1, {cause: EntityDamageCause.entityAttack, damagingEntity: herobrine})`. The hit penalty uses `applyDamage(2, {cause: EntityDamageCause.void})` or `selfDestruct`. |
| Arrow-proximity check, `Motion` reset | shot | `world.afterEvents.projectileHitEntity` (`hitEntity` is Herobrine), then `projectile.clearVelocity()` or remove |
| `summon lightning_bolt` | shrine strike | `dimension.spawnEntity("minecraft:lightning_bolt", loc)` |
| `particle … force @a` | specter, close | `dimension.spawnParticle("ftf:poof"/"ftf:smoke"/"ftf:portal", loc)`: custom RP particles (box emitter 0.5×1.6×0.5, 50-particle burst) |
| `playsound` vanilla ids | many | Bedrock ids *(verify)*: `ambient.cave`; `mob.ghast.scream` (hurt); `mob.elderguardian.curse`; `hit.stone` / `dig.stone`; `step.grass` / `stone` / `sand` / `wood` / `deepslate`; `note.snare`, `note.pling`; `item.book.page_turn`; `random.click`; `armor.equip_leather`; `tile.piston.in` / `out` |
| Sound **subtitles** as text (caution caption) | shrine | No custom subtitles. Show `player.onScreenDisplay.setTitle("", {subtitle:{translate:"ftf.caution_caption.local.1"}, fadeInDuration:10, stayDuration:40, fadeOutDuration:20})` or the action bar in gray italic, plus a faint sound. |
| `tellraw` translate / obfuscated | debug, eerie entrance | `world.sendMessage({rawtext:[{text:"§e"},{translate:"…"}]})`; `§k` for cryptic |
| Sign NBT `front_text` with translate + `selector` | sinister signs | `dimension.setBlockPermutation(loc, BlockPermutation.resolve("minecraft:<wood>_standing_sign", {ground_sign_direction: r}))`, then `block.getComponent("minecraft:sign").setText(text, SignSide.Front)`. Resolve the translation to the player's chosen language string in script (rawtext is supported by `setText` *(verify)*). Insert the player name for message 4. |
| `attribute entity_interaction_range` | config | Not needed (forms) |
| `team collisionRule never` | ghost | Entity `minecraft:pushable {is_pushable:false}` + tiny collision box |
| `gamerule max_block_modifications` | fills | Not needed in script (`fillBlocks` has its own limit; chunk the job) |
| Core shader (glow pixels) | skins | No custom shaders in Bedrock add-ons → emissive texture layer |
| Item model definitions (`items/*.json`), atlases | skins, UI | Entity geometry and textures; UI icons as plain PNGs |
| Custom fonts (bitmap glyph `ꨀ`) | debug | `font/glyph_AA.png` glyph sheet (private-use glyphs supported in Bedrock), or plain colour codes |
| Function tag `#from_the_fog:config` callable command | config | Custom command `ftf:config` via `CustomCommandRegistry` *(verify stable)*, plus `/scriptevent ftf:config` and an item |
| `particle rain 666666666` client crash | malicious malfunction | **No equivalent and should not be reproduced.** Fake it with `camera fade` + title + sound. |
| `time of minecraft:overworld query time` / day count | haunting delay, GHC | `world.getDay()` / `Math.floor(world.getAbsoluteTime()/24000)` |
| `minecraft.custom:play_time` objective | time-played achievements | Per-player counter incremented every second (dynamic property) |
| Developer-UUID advancements (found_footage) | easter eggs | Skip |
| `mannequin`, `rotate` command, `return run`, `execute if function` | various | Plain JS control flow |

**No Bedrock equivalent, with the workaround chosen:**

1. Custom painting variants and reading painting motifs → custom entities.
2. Custom advancements → script journal.
3. Shaders → emissive layer.
4. Subtitles → titles or action bar.
5. Forceload → tickingarea or loaded-only.
6. Wolf anger target → rotation plus growl sounds.
7. Client crash → omit.
8. Mannequin → custom entity.
9. Jigsaw from datapack → feature rules + anchors.
10. Item-model UI → forms.

**Recommended Bedrock architecture:**

- **BP:**
  - `manifest` (script module `@minecraft/server` 2.x and `@minecraft/server-ui` 2.x);
  - `entities/herobrine.json` (and `fake_pig`/`cow`/`frog`, or skin-property variants), `door_ghost.json`, `painting_*.json`, `tapestry_*.json`;
  - `blocks/structure_anchor.json`;
  - `features/*` + `feature_rules/*`;
  - `structures/ftf/*.mcstructure`;
  - `items/disc_*.json`, `painting_*.json`, `journal.json`;
  - `loot_tables/ftf/*`;
  - `texts/*.lang`;
  - `scripts/main.js` with modules: config, scheduler, sightings, spot, herobrine, events/blocks, events/world, structures, achievements, debug.
- **RP:**
  - `entity/*.entity.json`, `models/entity/*.geo.json`, `animations/herobrine.animation.json`, `animation_controllers/*`, `render_controllers/*`;
  - `textures/entity/ftf/skins/<skin>/{skin,skin_emissive}.png`, `textures/ftf/paintings/*`, `textures/ftf/ui/icons/*`, `textures/items/*`;
  - `particles/ftf_*.json`;
  - `sounds/sound_definitions.json` + `sounds/ftf/*.ogg`;
  - `texts/*.lang`;
  - `font/glyph_AA.png` (optional).

---

## 7. Function inventory (328 files)

Paths are relative to `data/<namespace>/function/`. Abbreviations: **lw** = `lunareclipse.watching`, **lu** = `lunareclipse.utils`.

| Function | Purpose |
|---|---|
| lu: forloop/start | Init a macro for-each loop over a storage array (`target`, `path`, `command`). |
| lu: forloop/loop | Recursive step: copy `[index]` to `value`, call `command`, increment. |
| lu: forloop/cleanup | Remove the loop's temp storage and score. |
| lu: forloop/test | Debug `say` for forloop. |
| lu: forloop_int/start | Init an integer loop from `starting_int` to `int`. |
| lu: forloop_int/loop | Recursive integer loop step. |
| lu: forloop_int/cleanup | Clear integer loop temps. |
| lu: forloop_int/test | Debug `say`. |
| lu: forloop_object/start | For-each that merges each object's fields into macro args. |
| lu: forloop_object/loop | Merge the element into temp and run the command. |
| lu: forloop_object/run_command | Call the command with the merged object. |
| lu: forloop_object/test | Debug `say`. |
| lu: main/reload | Create objective `lunareclipse_utils.global_values`. |
| lu: random_range/start | Roll a random int in [start,end] and call the command with `value`. |
| lu: random_range/test | Debug `say`. |
| lu: random_value/start | Pick a random element from an inline array and call the command. |
| lu: random_value/increment_array_length | Count array length (loop body). |
| lu: random_value/roll_number | Roll an index 0..length. |
| lu: random_value/select_value | Copy `array[index]` to `value`. |
| lu: random_value/test | Debug `say`. |
| lu: random_value_storage/start | Random element from a storage array (copies the array then delegates). |
| lu: random_value_storage/execute | Call random_value with the copied array. |
| lu: random_value_storage/test | Debug `say`. |
| lu: value_check/start | String-compare `base`/`dynamic` and run a raw command if equal. |
| lw: THIS IS NOT A BUG IT IS INTENTIONAL - $FTF_DATAPACK | Easter-egg file containing "wienercat"; no function. |
| lw: advancements/found_structure | Grant `find_any_structure` and `find_<structure>` to players within 16. |
| lw: advancements/get_day_count | Compute days played (play_time/24000) and grant 3/10/50/100/200-day advancements. |
| lw: advancements/grant_dreadful_donation | Grant `get_donation`. |
| lw: block_events/detection/placed/bed | Placed-bed trigger → raycast for `#beds`. |
| lw: block_events/detection/placed/candle | Placed candle → raycast `#candles`. |
| lw: block_events/detection/placed/chest | Placed chest → raycast `chest`. |
| lw: block_events/detection/placed/door | Placed door → raycast `#doors`. |
| lw: block_events/detection/placed/fire | Placed fire → raycast `fire` (shrine). |
| lw: block_events/detection/placed/forest | Entered a forest biome → log the player position under `#is_forest`. |
| lw: block_events/detection/placed/lantern | Placed lantern → raycast `lantern`. |
| lw: block_events/detection/placed/torch | Placed torch → raycast `#torches`. |
| lw: block_events/detection/raycasting/start | Revoke detection advancements; start the 0.5-step eye raycast. |
| lw: block_events/detection/raycasting/increment | Ray step (max 10); found block → log or shrine check. |
| lw: block_events/detection/raycasting/found_block | Summon a marker to read int coords, then store the position. |
| lw: block_events/detection/raycasting/store_marker_location | Write marker xyz and player yaw/pitch to temp; kill the marker. |
| lw: block_events/detection/raycasting/store_location | Prepend {x,y,z,yaw,pitch} to `block_positions.<tag>` unless duplicate. |
| lw: block_events/detection/remove/start | Loop all stored positions of a tag for pruning. |
| lw: block_events/detection/remove/check_block | Per entry: prune normally, or the shrine path for fire. |
| lw: block_events/detection/remove/check_block_status | Forceload; remove the entry if the block no longer matches. |
| lw: block_events/events/executables/burning_base | Bed + flammable below + nobody within 50 + roll → 3×2×3 fire. |
| lw: block_events/events/executables/chilled_candles | Candle (retry if unlit, max 4) + nobody within 50 + roll → extinguish all candles within ±25. |
| lw: block_events/events/executables/chilled_candles/extinguish | Per candle colour: fill `lit=false` for counts 1–4. |
| lw: block_events/events/executables/crimson_curse | Torch/lantern + nobody within 50 + roll → torches to redstone torches (soul lanterns optional). |
| lw: block_events/events/executables/disappearing_torches | Torch + nobody within 50 + roll → remove torches within ±25. |
| lw: block_events/events/executables/dreadful_donation | Chest + nobody within 25 + roll → donate. |
| lw: block_events/events/executables/dreadful_donation/donate | `loot insert` from `<lang>_<heirlooms>` table. |
| lw: block_events/events/executables/ghost_doors | Door + roll → remove Herobrine; spawn an invisible tiny nitwit "door ghost" for 30 s. |
| lw: block_events/events/executables/leafless_grove | Forest entry + nobody within 78 + roll → strip leaves and border logs; chain 3× along facing. |
| lw: block_events/events/random/init | Prune, measure, pick a random stored block, execute the event, debug log. |
| lw: block_events/events/random/increment_array_length | Count positions. |
| lw: block_events/events/random/roll_block | Random index via random_range. |
| lw: block_events/events/random/select_block | Copy the chosen entry to `.selected`. |
| lw: block_events/events/random/execute | Forceload; run the executable at the stored pos and rotation. |
| lw: block_events/presets/burning_base | Random event on `#beds`. |
| lw: block_events/presets/chilled_candles | Random event on `#candles`. |
| lw: block_events/presets/crimson_curse | 50/50 torches or lanterns list → random event. |
| lw: block_events/presets/disappearing_torches | Random event on torches. |
| lw: block_events/presets/dreadful_donation | Random event on chests. |
| lw: block_events/presets/ghost_doors | Random event on doors. |
| lw: block_events/presets/leafless_grove | Random event on forest positions. |
| lw: config/add_library_option | Register an option (list/page/type/options/offset/experimental) in the config layout. |
| lw: config/button/check_button | Find the clicked interaction → nearest button → run its function. |
| lw: config/button/clicked_button | Advancement reward: revoke; schedule check_button in 2t. |
| lw: config/button/run_function | Run a function with the button's custom_data as macro args. |
| lw: config/close_config | Kill the UI, remove the reach modifier and tag, flash particles. |
| lw: config/config_layout | Define pages, page counts and all options (§1.8). |
| lw: config/defaults | Default values for every option (only if unset); legacy value migration. |
| lw: config/initialize_defaults | Run `defaults_edited` (missing) then `defaults`. |
| lw: config/set_default | Set `options.<id>` if absent. |
| lw: config/main_menu/check_option | Clicked icon → open that option page. |
| lw: config/main_menu/clicked_option | Advancement reward: revoke, schedule check, snare sound. |
| lw: config/main_menu/create_option | Spawn the icon item_display + interaction for a menu entry. |
| lw: config/main_menu/create_sss | Spawn the hidden Super Secret Settings hotspot. |
| lw: config/main_menu/open_page | Rotate the pivot to the player; build the main page. |
| lw: config/main_menu/page | Build the main menu page (logo, background, icons, arrow, GHC theme). |
| lw: config/main_menu/page_contents | Lay out icons 4 per row; GHC page shows its switch. |
| lw: config/main_menu/theme/give_him_control | Apply the red or glitched GHC theme; 4 caution captions. |
| lw: config/next_page/main_menu | Go to the next main page or wrap to 1. |
| lw: config/next_page/settings | Go to the next settings page or wrap; skin-page offsets. |
| lw: config/next_page/switch_page | Increment the page and dispatch to the main or settings pager; page sound. |
| lw: config/open_config | Spawn pivots (3 ahead), ground-correct, extend reach, open page 1. |
| lw: config/option_page/clicked/back | Back to the main menu. |
| lw: config/option_page/clicked/boolean | Toggle a boolean (respect `locked`), update the switch, play the custom sound; GHC on → theme, sounds, activity, creeping sighting. |
| lw: config/option_page/clicked/increment | ± an int option (min 0), update the display, run the hook. |
| lw: config/option_page/clicked/increment/haunting_delay | Reset `started_events` when the delay changes. |
| lw: config/option_page/clicked/option | Select an enum value; update radio slots. |
| lw: config/option_page/clicked/pause_spinning | Toggle the preview spin. |
| lw: config/option_page/clicked/skin | Add or remove a skin from the list; update the toggle and preview. |
| lw: config/option_page/clicked/skin/add | Append the skin to `herobrine_skin.list`. |
| lw: config/option_page/clicked/skin/remove | Remove the skin from the list (keep at least one). |
| lw: config/option_page/clicked/skin/find_skin | Loop body: delete the matching list entry. |
| lw: config/option_page/clicked/skin/update_display | Rebuild the preview model of a skin on the stage; nametag. |
| lw: config/option_page/clicked/skin/create_model | Summon the preview armor stand with assembled passengers. |
| lw: config/option_page/clicked/skin/build_model/add_part | Gather the pivot/namespace for one preview part. |
| lw: config/option_page/clicked/skin/build_model/define_part | Append base and emissive item_display passengers for the preview. |
| lw: config/option_page/clicked/skin/random_variant_display/start | Pick a random variant of a group skin for preview. |
| lw: config/option_page/clicked/skin/random_variant_display/display | Show the chosen variant. |
| lw: config/option_page/create_option | Dispatch to the option type builder. |
| lw: config/option_page/create_settings_menu | Build the settings panel page, its contents and the next arrow. |
| lw: config/option_page/info_page_displays | Build the info panel (back, title, description, icon). |
| lw: config/option_page/open_page | Rotate the pivot; build the option page. |
| lw: config/option_page/page | Build the full option page (info + settings + skin stage). |
| lw: config/option_page/settings_content | Place each setting on its row. |
| lw: config/option_page/skin_library_stage | Spawn the wardrobe stage and spin button. |
| lw: config/option_page/type/boolean | Build a boolean row (texts + switch). |
| lw: config/option_page/type/increment | Build an increment row (value display, ±). |
| lw: config/option_page/type/option | Build an enum row (texts + loop of slots). |
| lw: config/option_page/type/option/create_options | Position each enum slot. |
| lw: config/option_page/type/option/create_option | Spawn one slot button + label. |
| lw: config/option_page/type/skin | Build a skin-page grid (title + toggles). |
| lw: config/option_page/type/skin/create_options | Compute spacing; place skin toggles. |
| lw: config/option_page/type/skin/create_options2 | Lay out skin toggles in rows of 5. |
| lw: config/option_page/type/skin/create_option | Fetch skin info → full builder. |
| lw: config/option_page/type/skin/create_option_full_info | Spawn the skin toggle + head preview. |
| lw: config/option_page/type/skin/create_display | Head preview (base + emissive). |
| lw: config/option_page/type/skin/create_display_default | Head preview for a variant group's default. |
| lw: config/option_page/type/skin/create_skin_pack_title | Title text for custom skin-pack pages. |
| lw: config/option_page/type/skin/store_spacing | Store spacing = index × 0.75. |
| lw: config/option_tip/raycast | Ray from the eyes (0.5 steps, 7 blocks) for a tip-activated element. |
| lw: config/option_tip/found_icon | Replace the tooltip text_display. |
| lw: config/option_tip/create_tip_main_menu | Tooltip with the page name. |
| lw: config/option_tip/create_tip_skin | Tooltip with the skin name. |
| lw: config/option_tip/create_tip_skin2 | Tooltip line with the skin credit. |
| lw: debug/message | Tellraw a debug line plus key/values to debug-mode players. |
| lw: debug/message/add_info_value | Loop body for info pairs. |
| lw: debug/message/create_string | Tellraw one key/value line. |
| lw: debug/toggle | Toggle the `watching.debug_mode` tag with piston sounds. |
| lw: fearful_footsteps/event | Roll (common) → attempt; debug. |
| lw: fearful_footsteps/presets/attempt | Remove Herobrine; pick one of 5 start offsets; init. |
| lw: fearful_footsteps/init | Choose the step sound by the block under the start. |
| lw: fearful_footsteps/start | Step counter (4 max); store the position; play; schedule a loop in 0.3 s. |
| lw: fearful_footsteps/loop | Re-enter start with stored data. |
| lw: fearful_footsteps/play | Advance 1 block along the stored yaw; play the step sound (vol 0.2). |
| lw: fearful_footsteps/store_marker_location | Marker → store xyz (+ yaw on the first step). |
| lw: fearful_footsteps/cleanup | Clear footsteps temp state. |
| lw: ghost_doors/remove_ghost | Clear the schedule; tp door ghosts to y −512. |
| lw: ghost_miner/start | Underground + strip-mine + roll → choose a side ±5, dig the tunnel, spawn the miner. |
| lw: ghost_miner/check_strip_mine | Check left/right blocks are stone/dirt/ores. |
| lw: ghost_miner/dig_tunnel/init | Dig away from the player (20×5), spawn the miner sighting, schedule mining sounds. |
| lw: ghost_miner/dig_tunnel/start_digging | Reset the counter; start dig at block centre. |
| lw: ghost_miner/dig_tunnel/dig | Recursive: clear 1×2×5, place a redstone wall torch, plug liquids and holes with cobblestone. |
| lw: ghost_miner/test | Dev test (`as Bret06`). |
| lw: give_him_control/init | Back up config; force settings; define the 41-option randomisation table. |
| lw: give_him_control/check_day | New day → randomise options. |
| lw: give_him_control/random_option/start | Loop all GHC options. |
| lw: give_him_control/random_option/change_setting | Pick a random value for one option. |
| lw: give_him_control/random_option/set_value | Write the option value; debug log. |
| lw: give_him_control/cleanup | Restore the backup config; re-init defaults. |
| lw: main/reload | Load: schedules, team, gamerule, objectives, defaults, layout, skins. |
| lw: main/tick | Per-tick dispatch (§1.1). |
| lw: main/timed_ticks/1_second | 1 s loop (§1.1). |
| lw: main/timed_ticks/5_minutes | 5 min event loop (§1.1). |
| lw: main/entity_ticks/herobrine | Per-Herobrine-entity logic (§2.4). |
| lw: main/entity_ticks/herobrine_model | Apply part animations, head rotation, body math. |
| lw: main/entity_ticks/player | Spot checks, tapestry checks, nightmare end, config tooltip, proximity jumpscare. |
| lw: main/entity_ticks/wolf | Sighting sense: sitting wolves stare and are angered at Herobrine. |
| lw: main/entity_ticks/door_ghost | Despawn the ghost when in a door or bed block. |
| lw: shrine/check | Is the fire on a classic or mod shrine? |
| lw: shrine/types/classic | Netherrack + 8 gold ring layout check. |
| lw: shrine/types/mod | Netherrack ×2 + gold ×2 column check. |
| lw: shrine/found_shrine | Log, start events, lightning, advancement, activity ×6, caption, shrine surprise. |
| lw: shrine/remove_relight_shrine | Relight a valid shrine if nobody within 80; prune invalid ones. |
| lw: shrine/haunting_delay/check_day | Start events once day ≥ haunting_delay. |
| lw: shrine/caution_caption/init | Roll message 1–4 and play it. |
| lw: shrine/caution_caption/roll_message | Store the rolled message number. |
| lw: shrine/caution_caption/play | Play the subtitle-only sound in the chosen language (radius 15). |
| lw: start_events | Set `started_events=1`; optional fake "Herobrine joined the game". |
| lw: sightings/sighting | Core sighting spawner (§2.2). |
| lw: sightings/retry_sighting | Increment the retry counter; re-run the previous preset. |
| lw: sightings/distance_check | Remove if no player within max_distance. |
| lw: sightings/cleanup | Reset all per-sighting state and animations. |
| lw: sightings/end_nightmare | Remove Herobrine; untag the player. |
| lw: sightings/player_sleep | Slept trigger → nightmare roll by frequency. |
| lw: sightings/punch | Punch the nearest player within 6 (animation + 1 damage). |
| lw: sightings/hit_herobrine | Player hit the hitbox → 2 void damage, remove, activity ×4, advancement. |
| lw: sightings/shot_herobrine | Arrow near → stop the arrow, remove, activity ×4. |
| lw: sightings/get_direction | Dev: say the compass direction of the head yaw. |
| lw: sightings/presets/creeping | Creeping parameters. |
| lw: sightings/presets/stalking | Stalking with dimension and underground routing. |
| lw: sightings/presets/lurking | Lurking with routing. |
| lw: sightings/presets/dwelling | Dwelling with routing. |
| lw: sightings/presets/nightmare | Nightmare at the bed (+ advancement). |
| lw: sightings/presets/ghost_miner | Still miner sighting + pickaxe after 1 s. |
| lw: sightings/presets/haunted_herd | Herd sighting with routing. |
| lw: sightings/presets/shrine_surprise | 2 s unspottable Herobrine on the shrine. |
| lw: sightings/presets/fake | Debug permanent still Herobrine. |
| lw: sightings/presets/test | Dev test at fixed coordinates. |
| lw: sightings/custom/summon | Public API for custom sightings. |
| lw: sightings/custom/rotate | Make Herobrine face a location. |
| lw: sightings/custom/toggle_facing_player | Toggle face_player. |
| lw: sightings/spawning_rules/roll_spawn_timer | Choose a type (1..7) and countdown; schedule. |
| lw: sightings/spawning_rules/set_spawn_timer | Roll day/night countdown ÷ multiplier. |
| lw: sightings/spawning_rules/schedule_sighting | Schedule summon_sighting in N s; debug. |
| lw: sightings/spawning_rules/summon_sighting | Window/started checks → select_sighting. |
| lw: sightings/spawning_rules/select_sighting | No door ghost → preset on a random non-spectator. |
| lw: sightings/spread_position/random_spread/start | Roll signed distance and lateral offset. |
| lw: sightings/spread_position/random_spread/spread | `tp ^lateral ^ ^distance`. |
| lw: sightings/spread_position/creeping/init | Run set_position as the spread entity. |
| lw: sightings/spread_position/creeping/set_position | 3–5 behind, ±2–3; +7 y; centre. |
| lw: sightings/spread_position/dwelling/init | Run set_position. |
| lw: sightings/spread_position/dwelling/set_position | 6–12, ±0–18; +7; centre; dwelling-shadow flag. |
| lw: sightings/spread_position/stalking/init | Run set_position. |
| lw: sightings/spread_position/stalking/set_position | 25–46, ±0–35; surface heightmap. |
| lw: sightings/spread_position/lurking/init | Run set_position. |
| lw: sightings/spread_position/lurking/set_position | 50–100, ±0–50; surface heightmap. |
| lw: sightings/spread_position/nightmare/init | Tag the player; position by bed. |
| lw: sightings/spread_position/nightmare/set_position | 2 blocks off the bed foot by facing. |
| lw: sightings/spread_position/still/init | Exact position, facing the executor. |
| lw: sightings/ground_correction/init | Reset; start the descent loop. |
| lw: sightings/ground_correction/loop | Descend 0.1 per step (≤ 320) to a standable cell; remove in liquid. |
| lw: sightings/models/schedule_create | Herd mob and variant choice; validate skins; pick a skin; create the model. |
| lw: sightings/models/create | Build the assembled passenger list, summon, nametag, no-sleep husk, breathing. |
| lw: sightings/models/create_model | Summon the AI armor stand with passengers; hide the shadow at night. |
| lw: sightings/models/technical/check_skins | Drop unknown skins (fallback default). |
| lw: sightings/models/technical/select_skin | Store the selected skin; resolve variant groups. |
| lw: sightings/models/mannequin/create | Vanilla-sightings: get the AI rotation; summon the mannequin. |
| lw: sightings/models/mannequin/summon | Summon a `mannequin` with the Herobrine profile texture. |
| lw: sightings/models/rotation_math | Body-follows-head yaw math, clamp, slip-behind bias. |
| lw: sightings/models/remove | Teleport-jumpscare chance, sign placement, specter particles, cleanup, kill all. |
| lw: sightings/models/despawn | Timer expiry: missed advancement; remove. |
| lw: sightings/skin_library/define_skins | Define all 95 skins and pivots; load custom packs; build pages. |
| lw: sightings/skin_library/define_skin_info | Store one skin's data; register a custom pack. |
| lw: sightings/skin_library/define_variant | Store a variant-group skin. |
| lw: sightings/skin_library/build_model/add_part | Gather pivot/namespace for a part. |
| lw: sightings/skin_library/build_model/define_part | Append base and emissive item_display (or item slot) to the assembly. |
| lw: sightings/skin_library/append_pages/create_page | Segment a custom pack into 20-skin pages. |
| lw: sightings/skin_library/append_pages/segment_skin_list | Assign skins to segments. |
| lw: sightings/skin_library/append_pages/add_skin_to_segment | Append a skin to a segment. |
| lw: sightings/skin_library/append_pages/new_page | Add a page number to the skin_library page list. |
| lw: sightings/skin_library/append_pages/add_skins | Register the page's skin option with the pack title. |
| lw: sightings/player_spotted/init | Per-player FOV cone checks (spot + suspense). |
| lw: sightings/player_spotted/search | 0.5-step ray: glass flag, retry, advancements, light-reactant, spotted. |
| lw: sightings/player_spotted/spotted/spotted | Handle a spot (herd, window watcher, signs, vanish). |
| lw: sightings/player_spotted/spotted/player_event | Spot advancements. |
| lw: sightings/player_spotted/spotted/vanish | Noise, GHC, slip-behind, tilt, schedule removal. |
| lw: sightings/player_spotted/spotted/vanish_end | Schedule remove after the delay. |
| lw: sightings/player_spotted/spotted/tilt_head | GHC 1% head tilt with 3 s delay. |
| lw: sightings/player_spotted/spotted/slip_behind/check | Wall-edge test left or right. |
| lw: sightings/player_spotted/spotted/slip_behind/left | Walk left; remove after 14t. |
| lw: sightings/player_spotted/spotted/slip_behind/right | Walk right; remove after 14t. |
| lw: sightings/player_spotted/spotted/suspenseful_strike | Play the suspense sting once after spotting. |
| lw: sightings/player_spotted/spotted/teleport_jumpscare | Tp Herobrine in front of the player; jumpscare. |
| lw: sightings/player_spotted/shrouded_specter/start | Dispatch the particle preset. |
| lw: sightings/player_spotted/shrouded_specter/presets/poof | 50 poof particles. |
| lw: sightings/player_spotted/shrouded_specter/presets/smoke | 50 smoke particles. |
| lw: sightings/player_spotted/shrouded_specter/presets/portal | 50 portal particles. |
| lw: sightings/sudden_scare/init | Jumpscare entry (malfunction / scare / remove). |
| lw: sightings/sudden_scare/jumpscare | Face Herobrine, sound, GHC punch, remove after 10t, activity ×2. |
| lw: sightings/sudden_scare/crash | Jumpscare + crash sound + schedule crash_game. |
| lw: sightings/sudden_scare/crash_game | Spawn 666,666,666 rain particles to crash the client (do not port). |
| lw: sightings/sneaky_strike/schedule_attempt | Schedule a strike at despawn − 3 s. |
| lw: sightings/sneaky_strike/strike | Roll → punch. |
| lw: sightings/haunted_herd/attempt | Roll → herd preset on a random player. |
| lw: sightings/haunted_herd/spotted | Start the 180° head-spin rotation. |
| lw: sightings/haunted_herd/replace/start | Store yaw/pitch; summon the real mob behind. |
| lw: sightings/haunted_herd/replace/summon | Summon the real mob with variant; remove the fake. |
| lw: sightings/haunted_herd/variant/init | Store the mob; pick variant; set the skin list. |
| lw: sightings/haunted_herd/variant/select | Warm, cold or temperate by biome tag. |
| lw: sightings/haunted_herd/variant/set_skin | Skin list = `<variant>_<mob>`. |
| lw: sightings/item/set | Holding pose; set the item next tick. |
| lw: sightings/item/remove | Clear the item and pose. |
| lw: sightings/item/technical/update | Apply the stored item. |
| lw: sightings/item/technical/update2 | Write the item to the right_arm_item display. |
| lw: sightings/item/presets/iron_pickaxe | Hold an iron pickaxe. |
| lw: sightings/item/presets/sign | Biome sign type → hold. |
| lw: sightings/item/presets/sign2 | Hold `<type>_sign`. |
| lw: sightings/animations/update | Set the target rotation, interpolation and update flag for a part axis. |
| lw: sightings/animations/rotate | Apply the part quaternion from scores (×0.01). |
| lw: sightings/animations/rotate/start | Begin a scripted AI yaw rotation. |
| lw: sightings/animations/rotate/increment | Per-tick yaw step; run the command at the end. |
| lw: sightings/animations/clamped | Body clamp reached; herd → replace in 1 s. |
| lw: sightings/animations/breathing/start | Start the breathing loop. |
| lw: sightings/animations/breathing/out | Arms out (±5); schedule in. |
| lw: sightings/animations/breathing/in | Arms in (0); schedule out. |
| lw: sightings/animations/breathing/stop | Cancel the loop; arms to 0. |
| lw: sightings/animations/walking/start | Start the walk loop; stop breathing. |
| lw: sightings/animations/walking/cycle1 | Legs and arms ±50; schedule cycle2 in 7t. |
| lw: sightings/animations/walking/cycle2 | Mirror; schedule cycle1 in 7t. |
| lw: sightings/animations/walking/stop | Cancel; limbs to 0; breathing. |
| lw: sightings/animations/punch/start | Wind-up keyframe; schedule next in 2t. |
| lw: sightings/animations/punch/next | Thrust keyframe; schedule end in 3t. |
| lw: sightings/animations/punch/end | Return to neutral; breathing in 5t. |
| lw: sightings/animations/holding/start | Right arm −15 x; holding flag. |
| lw: sightings/animations/holding/stop | Arm to 0; clear the flag. |
| lw: sightings/animations/tilting_head/start | Head z −15. |
| lw: sightings/animations/tilting_head/stop | Head z 0. |
| lw: signs/init_place | Compute rotation, message and type; place the sign. |
| lw: signs/get_rotation | Map head yaw to sign rotation 0–14. |
| lw: signs/get_type | Biome → wood type (or oak). |
| lw: signs/roll_message | Store message 1–12. |
| lw: signs/place | Place the translated sign; message 4 gets the player name. |
| lw: sounds/disappear | Cave or ghast vanish sound at +8 y. |
| lw: sounds/suspenseful_sighting | Default, slenderman or werewolf sting. |
| lw: sounds/ghost_miner/mining_stone | `block.stone.hit` at Herobrine (0.1, pitch 0.5). |
| lw: sounds/ghost_miner/break_stone | `block.stone.break` at Herobrine (0.1, 1). |
| lw: structures/place/start | Roll the structure chance (bug A2) → init. |
| lw: structures/place/init | Measure the list; pick and remove a random saved position; execute. |
| lw: structures/place/increment_array_length | Count positions. |
| lw: structures/place/roll_position | Random index. |
| lw: structures/place/select_position | Copy the entry; remove it from the list. |
| lw: structures/place/execute | Forceload; collision check; place; debug. |
| lw: structures/place/place | `place structure <s>/structure`; summon the advancement marker. |
| lw: structures/check_collision/letter | 5×3×5 clear check. |
| lw: structures/check_collision/mossy_pyramid | 5×6×5 clear check. |
| lw: structures/check_collision/sand_pyramid | 5×6×5 clear check. |
| lw: structures/check_collision/wooden_cross | 5×6×5 clear check. |
| lw: structures/remove_base/start | Dispatch by marker data.structure. |
| lw: structures/remove_base/letter | Clear 5×6×5; save location; kill markers. |
| lw: structures/remove_base/mossy_pyramid | Clear 5×3×5; save; kill markers. |
| lw: structures/remove_base/sand_pyramid | Clear 5×3×5; save; kill markers. |
| lw: structures/remove_base/wooden_cross | Clear 5×6×5; save; kill markers. |
| lw: structures/save_location | Prepend {x,y,z,*_center} to `structures.<s>.positions`. |
| lw: twisted_tapestries/attempt | Roll (common) → twist the nearest painting within 30. |
| lw: twisted_tapestries/change_painting/add | Swap a vanilla variant to its twisted variant; tag. |
| lw: twisted_tapestries/change_painting/remove | Swap back; untag. |
| lw: twisted_tapestries/player_spotted/init | 41° cone check toward twisted paintings within 60. |
| lw: twisted_tapestries/player_spotted/search | 0.5-step ray; revert the painting when hit. |
| lw: twisted_tapestries/player_spotted/cleanup | Reset the ray counter. |
| lw: utils/activity_multiplier/set | Set the multiplier; schedule reset; pull the 5-min tick earlier. |
| lw: utils/activity_multiplier/reset | Multiplier back to 1. |
| lw: utils/determine_chance/default | Default chance table roll (§1.5). |
| lw: utils/determine_chance/common | Common chance table (roll call misspelled, bug A1). |
| lw: utils/determine_chance/roll | `random 1..max` → `<mech>_roll`. |
