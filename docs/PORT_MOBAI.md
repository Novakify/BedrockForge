# MobAI Bridge: make ported mobs act exactly like they do in Java (research, branch `research/mobai`)

Research plan written 2026-10-31. No code yet: agree the plan first (same flow as GeckoBridge).
Scope: **mob behaviour only**. "Mimic AI" (voice mimic / LLM chat / mic input) is a separate, last-in-line
project, see the end of this file.
Evidence labels: **[Source]** seen in our repo / a mod's code · **[Docs]** documented, not tested by us · **[Theory]** inference.

## 0. Test mod: The Wonderland 3.0.3 (decided 2026-10-31)
Wonderland is already fully ported on `research/geckobridge` (129 mobs, procedures via JavaBridge, dimensions,
jumpscares; see docs/PORT_WONDERLAND.md). Its one "approximate" row is **Mob AI**. Doing MobAI on it finishes the
pack and builds the system at the same time.

What the Wonderland build already does (`tools/GeckoBridge/build.py` GOALS / `goals_of`, ~line 1181) [Source]:
- reads each mob's `registerGoals` and maps ~20 vanilla goals to Bedrock behaviours (= Layer A below, mostly done);
- for MCreator anonymous goals (`new MeleeAttackGoal(...) { canUse() {...} }`) it uses only the superclass and
  **drops the overridden methods**;
- for the mod's own goal classes it **guesses by class name** ("stalk" / "hide" / "attack"...), else drops them.
That dropped code is exactly the "intelligence" that's missing.

Big shortcut: **JavaBridge already translates MCreator bytecode to Script API JS** (72 event hooks work). The same
translator can turn the dropped `canUse / canContinueToUse / start / tick / stop` methods into JS for the GoalRunner
(Layer B), so most custom goals convert automatically instead of being hand-written.

Wonderland MobAI milestones (replace generic M1-M5 below for this mod):
- **W1** Inventory: list every mob's goals -> mapped / anonymous-with-overrides / custom class / tick() logic, with
  counts. Report in this file.
- **W2** GoalRunner (JS, Java GoalSelector rules: priority + MOVE/LOOK/JUMP/TARGET flags) + JavaBridge emits the
  goal methods. Vanilla components get switched by component groups while a custom goal holds their slot.
- **W3** Convert all mobs; native BF `moveTo` only if W2 shows the waypoint trick isn't good enough.
- **W4** Parity: Java AI trace vs Bedrock trace on the 5 mobs you care about most; fix until they match.
- **W5** Rebuild + install in "Wonderland Test", in-game check with you.

Needs from the PC: the Wonderland jar (C:\BedrockModding\TestMods) staged into the cloud session, plus the
build inputs that aren't in git (srg_1.20.1 mappings, packconv textures.json, tools/vanilla).

## 0b. Wonderland MobAI status (done 2026-10-31, deployed to "Wonderland Test")
Inventory (W1, 129 mob classes): 96 have registerGoals. Goals: AvoidEntityGoal 219, HurtByTarget 90, Float 86,
RandomStroll 73, RandomLookAround 68, NearestAttackableTarget 18, OpenDoor 18; **94 anonymous MeleeAttackGoals**
(override getAttackReachSqr), 7 conditional goals (Lithic x4, Sanguine, The Sorrow x2: canUse/canContinueToUse ->
a *CanTarget procedure), 1 goal written from scratch (Remember Me: flying charge via MoveControl). Procedures (the
real horror logic, already JavaBridge): setTarget 96, lookAt 99, getTarget 45, setDeltaMovement 64, moveTo 9.

Bugs found and fixed (these are why Wonderland mobs "had no brain"):
1. **No mob could melee.** extract.parse_goals skipped anonymous goal classes (`Entity$1`), so all 94
   MeleeAttackGoals (and every conditional goal) were silently dropped.
2. **Wrong target/avoid classes.** `ldc_w?` regex never matched plain `ldc`, so the class argument was lost:
   all 219 AvoidEntityGoals became "avoid the player at 128 blocks" (The Sorrow etc. fled from you instead of from
   the chasers). Now each mod mob has its own family `the_wonderland_<id>` and goals target/avoid the exact type.
3. **setTarget hurt-fakes.** The old runtime made the target hit the mob for 0 damage, which fired the mod's own
   hurt procedures. Now: the target gets tag `gbt_<mob family>`; every mob has a nearest_attackable_target entry for it.
4. **lookAt froze mobs.** It teleported the mob (facingLocation) every tick, cancelling movement. Now setRotation.
5. **navigation.moveTo was one 0.15 impulse.** Now a MoveControl wanted position, steered every tick (MoveCtl).
6. **The whole script pack was dead in 1.26.51.** paintings.js used `beforeEvents.itemUseOn` (removed in
   @minecraft/server 2.x); the throw aborted gb_runtime -> worldgen -> JavaBridge. Fixed (playerInteractWithBlock).
7. Smaller: Vec3 field reads (`vec.x`) returned a function; inherited vanilla fields through mod classes
   (`this.moveControl`) kept SRG names; getBbWidth was fixed 0.6; AABB.intersects / doHurtTarget missing.

Built (W2/W3): GoalRunner in JavaBridge hooks.js (gate + custom modes, Java priority/flag order, MoveControl steering);
gated goals = on/off component-group pairs fired by the translated Java canUse; melee reach from
getAttackReachSqr (`reach_multiplier`). Report: 673 goals mapped, 7 gated, 1 custom, 0 unmapped.
Checks: node harness with a fake Script API (all 129 types ticked 200 ticks, 0 errors; Remember Me charges the
target; Lithic gates switch from LithicCanTarget); validate.py 0 errors; in game ContentLog: no entity/behaviour
errors, JavaBridge loads, procedures call setTarget (player got gbt_* tags from Skinless and Blue Eye).
Update (2026-11-01):
- navigation.moveTo now uses Bedrock's pathfinder: an invisible `gb_nav_point` entity the mob is told to target
  (mobs with a melee behaviour); others still steer (MoveCtl).
- Mobs with no goals in Java (34 in Wonderland) get no Bedrock behaviours (the generic chase brain is gone).
- getTarget rebuilt (Script API reports no target for these mobs): forced target -> last attacker -> nearest
  survival player in FOLLOW_RANGE while the Java targeting goal (or its gate) is active. NAT ranges = follow_range.
- W4 tooling: tools/MobAITrace (Java recorder mod) + Bedrock trace (`/tag @s add gb_trace`). First Bedrock run
  (5 mobs, 30 s): Skinless walked in and killed the player; Sanguine/The Sorrow held position (their gates off);
  Remember Me never charged -> found the getTarget gap above (fixed, deployed). Java run still to do.
Pre-existing, not MobAI: GeckoBridgeProofRP fog files use the "minecraft:" namespace (Bedrock rejects them).

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
