# JavaBridge — a Forge mod's own Java code on the Bedrock Script API

GeckoBridge converts a mod's models/animations, ModPorter its content and worlds; **JavaBridge runs its code**:
MCreator procedures, entity/block/item/effect overrides and `@SubscribeEvent` handlers.

```
jar --javap -c -p -s -v--> jvm2js.py ----> scripts/javabridge/mod.js     (one JS function per reachable Java method)
                     ModelCore/srg.py (SRG -> Mojang names)   runtime.js (Minecraft/Forge/JDK surface on Script API)
                                                              hooks.js   (Bedrock events -> the Java hook methods)
```

* **Translation, not decompilation.** Every method becomes a pc-switch over an operand stack, instruction by
  instruction (`jvm2js.py`). Calls into the mod stay JS→JS through a direct function table (`FX[i]`);
  everything else goes to `R` (runtime) by **Mojang name** (`m_20185_` → `getX`) via the SRG table.
* **Reachability.** Only methods reachable from a hook are emitted (Wonderland: 6,877 of 15,135).
* **Runtime surface** (`runtime.js`): entities/levels/blocks/items/effects/NBT/synced data/commands/sounds/
  particles/structures/streams/optionals/configs. Unknown calls log once (`[JavaBridge] unsupported …`) and return
  a neutral value, so a missing API never stops a procedure.
* **Hooks** (`hooks.js`): entity `baseTick/aiStep` (round-robin, 12 ms/tick budget), `finalizeSpawn`, `hurt`
  (Java `false` = damage undone), `die`, `mobInteract`, `doHurtTarget`; block `onPlace/use/entityInside/stepOn/
  playerWillDestroy`; item `use/finishUsingItem/hurtEnemy/inventoryTick/useOn`; the mod's effects' `applyEffectTick`
  (timed-effect table, Bedrock has no custom effects); events PlayerTick, LivingTick, LivingDeath, LivingAttack,
  ServerChat, RightClickBlock, Block Break/Place, Sleep/Wake.
* **MCreator specifics**: `setAnimation` → `gbTrigger` (GeckoBridge trigger states); `MapVariables`/`WorldVariables`
  → world dynamic properties `mv:*` (defaults read from the class's `<init>`); `PlayerVariables` capability →
  player dynamic properties `pv:*`; `performPrefixedCommand` → Bedrock command (NBT stripped, `effect give`,
  `playsound` ids rewritten).

## Known gaps (honest list)
* Client-only code (screens, overlays, renderers) is excluded; GUIs need Bedrock forms (not done).
* Bedrock can't `setTarget` a mob: emulated with a zero-damage hit from the target (hurt_by_target picks it up).
* `Mob.getNavigation().moveTo` is an impulse toward the point, not real pathfinding.
* Forge networking / packets are no-ops (they are client sync in MCreator mods).
* The SRG table is built locally from Mojang's 1.20.1 mappings + MCPConfig and is **never committed** (Mojang's
  mappings may not be redistributed); set `GB_SRG_MAP` or keep it at `~/mappings/srg_1.20.1.json`.

Private personal use only: the translated code is the mod author's work.
