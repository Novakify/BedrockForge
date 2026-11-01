# MobAI Bridge: make ported mobs act exactly like they do in Java (research, branch `research/mobai`)

Research plan written 2026-10-31. No code yet: agree the plan first (same flow as GeckoBridge).
Scope: **mob behaviour only**. "Mimic AI" (voice mimic / LLM chat / mic input) is a separate, last-in-line
project, see the end of this file.
Evidence labels: **[Source]** seen in our repo / a mod's code · **[Docs]** documented, not tested by us · **[Theory]** inference.

## 1. Why ported mobs don't act like Java (the actual cause)
- `tools/ModPorter/extract.py` and `src_details.py` already **read every mob's AI goals** (selector, priority, class,
  args). [Source]
- But `tools/ModPorter/generate.py` (~line 265) then **throws them away** and gives every mob the same
  "generic hostile AI": float + hurt_by_target + nearest_attackable_target + melee_attack + random_stroll +
  look_at_player + random_look_around. [Source] So a Cave Dweller, a Sculk mob and a bear all have the
  same brain. The only mobs that behave right are the ones with a hand-written overlay
  (The Dweller, Sculk Horde v0, From The Fog script pack).
- GeckoLib/GeckoBridge doesn't fix this, and it shouldn't: GeckoLib only covers models and animations. The behaviour
  is plain Java code in the mod's entity and goal classes.

So the job is: **translate the goals instead of discarding them**, and give the custom ones a runtime.

## 2. How Java mob AI works (what we have to reproduce)
| Piece | What it does in Java | Notes for the port |
|---|---|---|
| `goalSelector` / `targetSelector` | Lists of goals with **priorities**. Each tick, the highest-priority goal that `canUse()` takes the slots it needs (flags MOVE / LOOK / JUMP / TARGET) and pushes lower ones out | Bedrock's behaviour components also run by priority and are backed by native goal classes. Close match for vanilla goals [Docs/Theory] |
| Goal lifecycle | `canUse` -> `start` -> `tick` (every tick) -> `canContinueToUse` -> `stop` | Custom goals = these 5 methods. That's what we generate code from |
| `PathNavigation` | A* pathfinding, `moveTo(x,y,z,speed)` | Bedrock has its own navigation (`navigation.walk/climb/fly/...`). Script API can't ask it to "walk to X" directly as far as known [Theory, verify on current @minecraft/server] |
| `MoveControl` / `LookControl` / `JumpControl` | Low-level steering and head turning | Mostly components; head look already handled in GeckoBridge |
| `Brain` (newer mobs: Warden-style) | Memories + sensors + activities instead of goals | Rare in horror mods; treat as "custom" |
| Entity `tick()` / `aiStep()` overrides | Lots of horror logic lives here, not in goals (timers, "is the player looking at me", teleports) | Script API tick loop or native hook |
| Synched data / NBT | State flags (angry, crawling, stage) | Bedrock **entity properties** + component groups |
| 20 ticks/s | Java timing | Bedrock also runs 20 ticks/s, so tick-based timers map 1:1 [Docs] |

## 3. The plan: 3 layers, cheapest first (same hybrid shape as GeckoBridge)

### Layer A: vanilla goals -> Bedrock components (offline, in ModPorter)
Most mods mix vanilla goals with a few custom ones. Map the vanilla ones 1:1 with priorities kept:

| Java goal | Bedrock component | Status |
|---|---|---|
| FloatGoal | `behavior.float` | 1:1 |
| MeleeAttackGoal(speed, followEvenIfNotSeen) | `behavior.melee_attack` (speed_multiplier, track_target) | 1:1 |
| NearestAttackableTargetGoal(Player, mustSee) | `behavior.nearest_attackable_target` (must_see, entity_types) | 1:1 |
| HurtByTargetGoal | `behavior.hurt_by_target` | 1:1 |
| WaterAvoidingRandomStrollGoal / RandomStrollGoal | `behavior.random_stroll` | 1:1 |
| LookAtPlayerGoal / RandomLookAroundGoal | `behavior.look_at_player` / `behavior.random_look_around` | 1:1 |
| AvoidEntityGoal | `behavior.avoid_mob_type` | approx (check distances/speeds) |
| PanicGoal | `behavior.panic` | 1:1 |
| LeapAtTargetGoal | `behavior.leap_at_target` | 1:1 |
| OpenDoorGoal / BreakDoorGoal | `behavior.open_door` / `behavior.break_door` + navigation `can_open_doors` | 1:1 |
| RangedAttackGoal / RangedBowAttackGoal | `behavior.ranged_attack` + `shooter` | approx |
| MoveTowardsTargetGoal | `behavior.move_towards_target` | 1:1 |
| RestrictSunGoal / FleeSunGoal | `behavior.restrict_sun` / `behavior.flee_sun` | 1:1 |
| anything else vanilla | table grows as we meet them; unknown = report, never silently drop | - |

Output: real per-mob BP entity files instead of the generic brain. Cheap, no runtime. [Theory, to test]

### Layer B: custom goals -> generated "GoalRunner" script (runtime, Script API)
The horror stuff is almost always custom goals/tick code. Plan:
1. **Decompile** the mod's goal + entity classes (ModPorter already reads source/bytecode; add a decompiler
   step, e.g. Vineflower) so we see the real `canUse/start/tick/stop` logic.
2. Port each custom goal into a JS class with the same 5 methods, run by a small **GoalRunner** we ship once:
   same priority + flag rules as Java's GoalSelector, ticked every game tick. Vanilla parts stay Layer A
   components; GoalRunner switches them on/off through component groups when a custom goal takes the
   MOVE/LOOK/TARGET slot (so the two never fight).
3. State lives in entity properties (also drives animations through GeckoBridge).
4. Semi-automatic: the common horror patterns below become reusable library functions, so each new mod is mostly
   wiring, not writing from scratch. Odd one-off logic is hand-ported (like the current overlays).

Script API pieces we lean on [Docs, verify versions]: `system.runInterval`, `entity.getViewDirection()`,
`dimension.getEntitiesFromRay` / `getBlockFromRay` (line of sight), `entity.teleport`, `applyImpulse`,
`setProperty` / `triggerEvent`, `dimension.setBlockType`, `playSound`.

### Layer C: native BF hooks (only where A + B can't match Java)
LeviLamina is native code inside the game. In single-player the world (and mob AI) runs in the same process as
the client, so hooks can reach it [Theory: confirm which Mob/Navigation/Goal types exist in LeviLamina's client
headers or need our own signatures]. Short list of what probably needs it:
- **"Walk to exactly X,Y,Z"** with Bedrock's own pathfinder (Java `navigation.moveTo`). Script API workaround is an
  invisible waypoint entity + `move_towards_target`; works but clunky. A native `bf::mob::moveTo()` is the clean fix.
- **Exact speed/steering control** per tick (Java sets `MoveControl` directly).
- **Hitbox changes mid-life** (Cave Dweller crawling into 1-block tunnels): component groups can swap
  `collision_box`, but pathing through 1-high gaps may need navigation tweaks.
- **Performance**: if JS GoalRunner gets heavy with many mobs, move the hot loop native.
Exposed to scripts as a tiny BF API so the generated JS stays the same either way.

## 4. Horror patterns to build into the library (from Dweller / From The Caves / From The Fog / Sculk Horde)
| Pattern | Java usually | Bedrock route |
|---|---|---|
| Is the player looking at me | dot(player look, dir to mob) > threshold + LOS raycast | B (view direction + ray) |
| Freeze / flee / vanish when looked at | goal checks the above | B |
| Stalk at a distance, stay out of sight | custom goal + navigation to hidden spot | B + C (moveTo) |
| Teleport when not observed | tick code | B |
| Crawl / squeeze through tunnels | pose + hitbox change | A/B (component groups) + C if pathing fails |
| Climb walls | climbing navigation | A (`navigation.climb`, `can_climb`) |
| Break/put out torches and lights | goal with block edits | B |
| Open/break doors, sneak through houses | vanilla door goals | A |
| Chase with music / heartbeat | client sound on state change | B + RP sounds |
| Jumpscare (camera snap, scream, flash) | client packet + render | B (`/camera`) + our impact flash (RF) |
| Fake footsteps / ambient stalking sounds | tick code + sounds | B |
| Spawn rules, cooldowns, "stage" progression over days | world saved data | B (dynamic properties) |

## 5. How we prove "exactly like Java" (parity test)
Feel isn't measurable, so we measure:
- **AI trace in Java**: a tiny private debug mod (or the mod in a dev run) logs per tick: active goals, target,
  position, state flags. Same scripted test world/seed, same player route.
- **AI trace in Bedrock**: GoalRunner logs the same fields.
- Compare: which goal runs when, reaction times (ticks), distances (stalk range, flee range), speeds.
  Pass = same goal sequence and timings within a small tolerance. RNG won't be identical, so we test the rules and
  timing, not the exact random rolls.
- Plus side-by-side videos for the "does it feel the same" check you do yourself.

## 6. Milestones
- **M0** (this doc) research plan. Agree before code.
- **M1** ModPorter: vanilla goal table (Layer A) replaces the generic brain; report every unmapped goal. Re-port
  existing mobs and compare.
- **M2** GoalRunner runtime + decompile step; port **one mob fully** as the test case (proposal: Cave Dweller
  Evolved, MIT, already in the GeckoBridge test set, classic stalker AI).
- **M3** Layer C hooks only for what M2 proves Script API can't do (moveTo first).
- **M4** AI trace parity tests for the M2 mob; fix until it passes.
- **M5** Roll out: From The Caves, Sculk Horde, From The Fog, then new horror mods. Library grows each time.
- **M6 (last, separate project)** Mimic AI: local sidecar (voice clips / speech-to-text / local LLM) talking to BF
  over localhost. Not started until M5 is solid.

## 7. Fits the roadmap
Follows the rule "finish current work first": MobAI work starts after GeckoBridge (horror mobs need their models
working first), and before FiguraBridge only if you choose that order. Build GoalRunner so it reads entity
properties GeckoBridge already uses, so behaviour and animation stay in sync.

## 8. Rules carried over
- Credit original authors; check each mod's licence before porting (ARR = private only, never committed).
- All ports private / personal use. Single-player first.
- Research branch only, never push to main.

## Open questions for Fernando
1. OK with Cave Dweller Evolved as the first full-AI test mob? (Or name the horror mod you want most.)
2. Start M1 (the cheap ModPorter fix) right away in parallel with GeckoBridge, or strictly after it?
