// JavaBridge hooks (original Bedrock Forge code): call the translated Java methods from Bedrock events.
import { world, system, CommandPermissionLevel, CustomCommandParamType, CustomCommandStatus } from "@minecraft/server";
import { M, R, JEntity, JLevel, V, bp, Stack, BLOCK, MODFX, DIR, MOVES, LASTHURT } from "./runtime.js";

const CFG = globalThis.__JB_CFG, H = CFG.hooks, NS = CFG.ns;
const errs = new Map();
function run(id, args) {
  try { return M.call(id, args); }
  catch (e) { const n = (errs.get(id) ?? 0) + 1; errs.set(id, n); if (n <= 2) console.warn(`[JavaBridge] ${id.split("/").pop()}: ${e}${e?.stack ? " " + String(e.stack).split("\n")[1] : ""}`); return null; }
}
const DS = (cause, attacker) => ({ cause: cause ?? "entityAttack", attacker: attacker ? JEntity.of(attacker) : null, key: cause,
  getEntity() { return this.attacker; }, getDirectEntity() { return this.attacker; }, is() { return false; }, getMsgId() { return cause; } });
const HAND = { key: "MAIN_HAND", hand: "mainhand" };
const blockClass = new Map(), itemClass = new Map(), effectClass = new Map();   // bedrock id -> java class with hooks
for (const [id, cls] of Object.entries(CFG.blockClasses ?? {})) if (H.block[cls]) blockClass.set(id, cls);
for (const [id, cls] of Object.entries(CFG.itemClasses ?? {})) if (H.item[cls]) itemClass.set(id, cls);
for (const [id, cls] of Object.entries(CFG.effectClasses ?? {})) if (H.effect[cls]) effectClass.set(id, H.effect[cls]);

// ---- clinit of the mod's config/static data, once the world is up
let ready = false;
world.afterEvents.worldLoad?.subscribe?.(() => { ready = true; });
system.runTimeout(() => { ready = true; }, 40);

// ---- entities: Java ticks every mod mob 20x a second; here each loaded mod mob is ticked round-robin within a
// per-tick time budget (BUDGET ms), so a crowd of mobs slows their logic down instead of the game.
const ETYPES = Object.keys(H.entity).filter((t) => H.entity[t].tick);
const BUDGET = 12;
let queue = [], tickN = 0;
system.runInterval(() => {
  if (!ready) return; tickN++;
  if (!queue.length) {
    for (const dimId of dims()) {
      let d; try { d = world.getDimension(dimId); } catch { continue; }
      for (const t of ETYPES) { let list; try { list = d.getEntities({ type: t }); } catch { continue; } for (const e of list) queue.push(e); }
    }
  }
  const t0 = Date.now();
  while (queue.length && Date.now() - t0 < BUDGET) {
    const e = queue.shift(); let ok = false; try { ok = e.isValid; } catch {} if (!ok) continue;
    const w = JEntity.of(e); for (const id of H.entity[e.typeId].tick) run(id, [w]);
  }
}, 1);
// ---- MobAI GoalRunner (docs/PORT_MOBAI.md). Java's GoalSelector runs the mod's goal objects every tick; here:
//  * "gate" goals (MCreator anonymous goals overriding canUse/canContinueToUse): the Bedrock behaviour does the
//    vanilla part, the translated Java condition switches it on/off (entity events gb:g<i>_on / _off).
//  * "custom" goals (written from scratch): canUse -> start -> tick ... canContinueToUse false -> stop, as in Java.
//  * MoveControl wanted positions (setWantedPosition, navigation.moveTo) are steered one step per tick.
const GOALS = H.goals ?? {}, GTYPES = Object.keys(GOALS), GSTATE = new Map();
// gate state for the runtime's getTarget (gated NearestAttackableTargetGoal): gate group index -> current Java canUse
globalThis.__GB_GATE = (id, gi) => { const st = GSTATE.get(id); if (!st) return false; const t = st.type; const specs = GOALS[t] ?? []; const i = specs.findIndex((g) => g.index === gi); return i >= 0 && !!st.on[i]; };
world.afterEvents.entityHurt.subscribe(({ hurtEntity, damageSource }) => { try { const by = damageSource.damagingEntity; if (by && hurtEntity.typeId.startsWith(NS + ":")) LASTHURT.set(hurtEntity.id, { by, t: system.currentTick }); } catch {} });
let gtick = 0;
function goalObj(cls, w) { const o = R.newObj(cls); o.__goal = true; o["this$0"] = w; return o; }
function runGoals(e, specs) {
  const w = JEntity.of(e); let st = GSTATE.get(e.id);
  if (!st) { st = { type: e.typeId, objs: specs.map((g) => goalObj(g.cls, w)), on: specs.map(() => null), active: specs.map(() => false) }; GSTATE.set(e.id, st); }
  for (let i = 0; i < specs.length; i++) {
    const g = specs[i], o = st.objs[i], fn = g.fn;
    if (g.mode === "gate") {
      if ((gtick + i) % 2) continue;
      const f = st.on[i] ? (fn.canContinueToUse ?? fn.canUse) : (fn.canUse ?? fn.canContinueToUse);
      const v = !!run(f, [o]);
      if (v !== st.on[i]) { st.on[i] = v; try { e.triggerEvent(`gb:g${g.index}_${v ? "on" : "off"}`); } catch {} }
    } else if (!st.active[i]) {
      if (fn.canUse && run(fn.canUse, [o])) { st.active[i] = true; if (fn.start) run(fn.start, [o]); }
    } else if (!run(fn.canContinueToUse ?? fn.canUse, [o])) { st.active[i] = false; if (fn.stop) run(fn.stop, [o]); }
    else if (fn.tick) run(fn.tick, [o]);
  }
}
system.runInterval(() => {
  if (!ready) return; gtick++;
  if (GTYPES.length) for (const dimId of dims()) {
    let d; try { d = world.getDimension(dimId); } catch { continue; }
    for (const t of GTYPES) { let list; try { list = d.getEntities({ type: t }); } catch { continue; } for (const e of list) runGoals(e, GOALS[t]); }
  }
  R.navTick();
  for (const [id, c] of MOVES) { let ok = false; try { ok = c.w.e.isValid; } catch {} if (!ok) { MOVES.delete(id); continue; } c.step(); }
}, 1);
// ---- MobAI parity trace (docs/PORT_MOBAI.md W4): `/tag @s add gb_trace` (or `/scriptevent gb:trace on|off`). One JSON line per tick per mod mob
// (and player) within 48 blocks, to the content log as "[MobAITrace] {...}"; same fields as the Java recorder mod.
let TRACE = false;
system.afterEvents.scriptEventReceive?.subscribe?.((ev) => { if (ev.id === "gb:trace") { TRACE = ev.message.trim() !== "off"; console.warn(`[MobAITrace] ${TRACE ? "on" : "off"}`); } });
const r2 = (v) => Math.round(v * 100) / 100;
system.runInterval(() => {
  const players = world.getAllPlayers().filter((p) => TRACE || p.hasTag("gb_trace"));
  if (!players.length) return;
  for (const p of players) {
    let list = []; try { list = p.dimension.getEntities({ location: p.location, maxDistance: 48 }); } catch {}
    for (const e of list) {
      if (e.typeId !== "minecraft:player" && !e.typeId.startsWith(NS + ":")) continue;
      if (e.typeId === NS + ":gb_nav_point") continue;
      let v = { x: 0, z: 0 }, hp = 0, tg = null; try { v = e.getVelocity(); } catch {} try { hp = e.getComponent("health")?.currentValue ?? 0; } catch {}
      try { const j = JEntity.of(e).getTarget(); tg = j ? `${j.e.typeId}#${j.e.id}` : null; } catch {}
      const st = GSTATE.get(e.id), specs = GOALS[e.typeId] ?? [];
      const goals = specs.map((g, i) => (g.mode === "gate" ? st?.on?.[i] : st?.active?.[i]) ? `${g.priority}:${g.cls.split("/").pop()}` : null).filter(Boolean);
      const l = e.location;
      console.warn("[MobAITrace] " + JSON.stringify({ t: system.currentTick, id: e.id, type: e.typeId, x: r2(l.x), y: r2(l.y), z: r2(l.z), vx: r2(v.x), vz: r2(v.z), hp: r2(hp), target: tg, goals, nav: R.navBusy({ e }) || !!MOVES.get(e.id)?.want }));
    }
  }
}, 1);
world.beforeEvents.entityRemove?.subscribe?.(({ removedEntity }) => { try { GSTATE.delete(removedEntity.id); MOVES.delete(removedEntity.id); } catch {} });
function dims() { const out = ["minecraft:overworld", "minecraft:nether", "minecraft:the_end"]; for (const k of Object.keys(globalThis.__WG_DIMS ?? {})) out.push(k); return out; }
world.afterEvents.entitySpawn.subscribe(({ entity, cause }) => {
  if (!ready || cause === "Loaded") return;
  const hk = H.entity[entity?.typeId]; if (!hk?.spawn) return;
  const w = JEntity.of(entity);
  for (const id of hk.spawn) run(id, [w, w.level(), { key: "NORMAL" }, { key: "MOB_SUMMONED" }, null, null]);
});
world.afterEvents.entityHurt.subscribe(({ hurtEntity, damage, damageSource }) => {
  const hk = H.entity[hurtEntity?.typeId]; if (!hk?.hurt) return;
  const w = JEntity.of(hurtEntity);
  for (const id of hk.hurt) {
    const ok = run(id, [w, DS(damageSource.cause, damageSource.damagingEntity), damage]);
    if (ok === false || ok === 0) { try { const h = hurtEntity.getComponent("health"); h.setCurrentValue(Math.min(h.effectiveMax, h.currentValue + damage)); } catch {} }
  }
});
world.afterEvents.entityDie.subscribe(({ deadEntity, damageSource }) => {
  const hk = H.entity[deadEntity?.typeId];
  if (hk?.die) for (const id of hk.die) run(id, [JEntity.of(deadEntity), DS(damageSource.cause, damageSource.damagingEntity)]);
  for (const ev of H.events) if (ev.kind === "death") run(ev.fn, [{ __ev: true, entity: JEntity.of(deadEntity), source: DS(damageSource.cause, damageSource.damagingEntity) }]);
});
world.afterEvents.playerInteractWithEntity.subscribe(({ player, target }) => {
  const hk = H.entity[target?.typeId]; if (!hk?.interact) return;
  for (const id of hk.interact) run(id, [JEntity.of(target), JEntity.of(player), HAND]);
});
world.afterEvents.entityHitEntity.subscribe(({ damagingEntity, hitEntity }) => {
  const hk = H.entity[damagingEntity?.typeId];
  if (hk?.attack) for (const id of hk.attack) run(id, [JEntity.of(damagingEntity), JEntity.of(hitEntity)]);
  if (damagingEntity?.typeId === "minecraft:player") {   // item hurtEnemy
    let it; try { it = damagingEntity.getComponent("equippable").getEquipment("Mainhand"); } catch {}
    const cls = it && itemClass.get(it.typeId);
    if (cls && H.item[cls].hurtEnemy) run(H.item[cls].hurtEnemy, [M.inst?.(cls) ?? { __cls: cls }, R.handItem(JEntity.of(damagingEntity), "mainhand"), JEntity.of(hitEntity), JEntity.of(damagingEntity)]);
  }
});
for (const ev of H.events) if (ev.kind === "attack") {
  world.afterEvents.entityHurt.subscribe(({ hurtEntity, damage, damageSource }) => {
    run(ev.fn, [{ __ev: true, entity: JEntity.of(hurtEntity), source: DS(damageSource.cause, damageSource.damagingEntity), amount: damage }]);
  });
}

// ---- the mod's Brigadier commands (RegisterCommandsEvent) -> one Bedrock custom command per root literal:
//      /<ns>:<root> [word] [word] [word] [word]  walks the same literal/argument path and runs the Command lambda
const CMDS = H.commands ?? [];
if (CMDS.length) system.beforeEvents.startup.subscribe((ev) => {
  const roots = [...new Set(CMDS.map((c) => c.path[0]))];
  for (const root of roots) {
    const mine = CMDS.filter((c) => c.path[0] === root);
    const depth = Math.max(...mine.map((c) => c.path.length)) - 1;
    const params = Array.from({ length: Math.min(depth, 6) }, (_, i) => ({ name: `arg${i + 1}`, type: CustomCommandParamType.String }));
    const usage = mine.map((c) => c.path.slice(1).join(" ")).slice(0, 12).join(" | ");
    try {
      ev.customCommandRegistry.registerCommand({ name: `${NS}:${root}`, description: `${root} (mod command): ${usage}`.slice(0, 250),
        permissionLevel: CommandPermissionLevel.GameDirectors, optionalParameters: params }, (origin, ...words) => {
        words = words.filter((w) => w !== undefined && w !== "");
        const hit = mine.find((c) => c.path.length - 1 === words.length && c.path.slice(1).every((seg, i) => seg.startsWith("<") || seg === words[i]));
        if (!hit) return { status: CustomCommandStatus.Failure, message: `usage: /${NS}:${root} ${usage}` };
        const args = {}; hit.path.slice(1).forEach((seg, i) => { const m = /^<(\w+):(\w+)>$/.exec(seg); if (m) args[m[1]] = m[2] === "int" ? parseInt(words[i]) : m[2] === "float" ? parseFloat(words[i]) : m[2] === "bool" ? words[i] === "true" : words[i]; });
        const src = origin.sourceEntity;
        system.run(() => {
          const w = src ? JEntity.of(src) : null, lv = JLevel.of(src?.dimension ?? world.getDimension("minecraft:overworld"));
          const l = src?.location ?? { x: 0, y: 64, z: 0 };
          const source = { __src: true, pos: V(l.x, l.y, l.z), level: lv, entity: w, getEntity: () => w, getPlayer: () => w, getLevel: () => lv, getUnsidedLevel: () => lv,
            getPosition: () => V(l.x, l.y, l.z), getServer: () => null, sendSuccess() {}, sendFailure() {}, hasPermission: () => true };
          run(hit.fn, [{ __ctx: true, getSource: () => source, getArgument: (n) => args[n] ?? null, getInput: () => [root, ...words].join(" ") }]);
        });
        return { status: CustomCommandStatus.Success };
      });
    } catch (e) { console.warn(`[JavaBridge] command ${root}: ${e}`); }
  }
});

// ---- MCreator delayed work: queueServerWork(ticks, runnable) -> system.runTimeout (the mod's ServerTickEvent queue
// would also work, but the scheduler costs nothing while idle)
for (const id of CFG.workQueue ?? []) M.def(id, (ticks, r) => {
  system.runTimeout(() => { try { R.lam(r); } catch (e) { console.warn(`[JavaBridge] queued work: ${e}`); } }, Math.max(1, ticks | 0));
});
if (H.events.some((ev) => ev.kind === "serverTick") && !(CFG.workQueue ?? []).length) system.runInterval(() => {
  if (!ready) return;
  for (const ev of H.events) if (ev.kind === "serverTick") run(ev.fn, [{ __ev: true, phase: { key: "END" }, side: { key: "SERVER" }, getServer: () => R.server?.() ?? null }]);
}, 1);

// ---- players: tick events, living tick, chat, sleep
system.runInterval(() => {
  if (!ready) return;
  for (const p of world.getAllPlayers()) {
    const w = JEntity.of(p);
    for (const ev of H.events) {
      if (ev.kind === "playerTick") run(ev.fn, [{ __ev: true, player: w, phase: { key: "END" }, side: { key: "SERVER" } }]);
      else if (ev.kind === "livingTick") run(ev.fn, [{ __ev: true, entity: w }]);
    }
    // held items / armour: inventoryTick
    for (const slot of ["Mainhand", "Offhand", "Head", "Chest", "Legs", "Feet"]) {
      let it; try { it = p.getComponent("equippable").getEquipment(slot); } catch {}
      const cls = it && itemClass.get(it.typeId);
      if (cls && H.item[cls].invTick) run(H.item[cls].invTick, [{ __cls: cls }, R.handItem(w, slot.toLowerCase()), w.level(), w, 0, slot === "Mainhand"]);
    }
    // the mod's own effects: tick procedures
    for (const [id, st] of R.modEffectsOf(w)) { const fn = effectClass.get(id); if (fn) run(fn, [{ __cls: "effect" }, w, st.amp]); }
    // sleeping
    let sl = false; try { sl = p.isSleeping; } catch {}
    if (sl !== (w.__wasSleeping ?? false)) {
      w.__wasSleeping = sl;
      for (const ev of H.events) if ((sl && ev.kind === "sleep") || (!sl && ev.kind === "wake")) run(ev.fn, [{ __ev: true, entity: w, player: w, pos: w.blockPosition(), level: w.level() }]);
    }
  }
}, 1);
// the mod's effects on mobs too (their tick procedures), within a budget
system.runInterval(() => {
  const t0 = Date.now();
  for (const [eid, m] of MODFX) {
    if (Date.now() - t0 > 4) break;
    let e; try { e = world.getEntity(eid); } catch {} if (!e || e.typeId === "minecraft:player") continue;
    const w = JEntity.of(e);
    for (const [id, st] of Object.entries(m)) if (st.until > system.currentTick) { const fn = effectClass.get(id); if (fn) run(fn, [{ __cls: "effect" }, w, st.amp]); }
  }
}, 1);
// dimension changes, login / respawn
const lastDim = new Map();
system.runInterval(() => {
  for (const p of world.getAllPlayers()) {
    const d = p.dimension.id, prev = lastDim.get(p.id); lastDim.set(p.id, d);
    if (prev && prev !== d) for (const ev of H.events) if (ev.kind === "changedDim") run(ev.fn, [{ __ev: true, entity: JEntity.of(p), player: JEntity.of(p), from: prev, to: d }]);
  }
}, 5);
world.afterEvents.playerSpawn.subscribe(({ player, initialSpawn }) => {
  for (const ev of H.events) if ((initialSpawn && ev.kind === "login") || (!initialSpawn && ev.kind === "respawn")) run(ev.fn, [{ __ev: true, entity: JEntity.of(player), player: JEntity.of(player) }]);
});
world.beforeEvents.chatSend?.subscribe?.((ev) => {
  const evs = H.events.filter((x) => x.kind === "chat"); if (!evs.length) return;
  const p = ev.sender, msg = ev.message;
  system.run(() => { for (const e of evs) run(e.fn, [{ __ev: true, player: JEntity.of(p), text: msg, getRawText: () => msg, getMessage: () => ({ __text: msg }) }]); });
});

// ---- blocks
world.afterEvents.playerPlaceBlock.subscribe(({ block, player }) => {
  const cls = blockClass.get(block.typeId); const lv = JLevel.of(block.dimension), pos = V(block.location.x, block.location.y, block.location.z);
  if (cls && H.block[cls].place) run(H.block[cls].place, [{ __cls: cls }, BLOCK(block.typeId), lv, pos, BLOCK("minecraft:air"), false]);
  for (const ev of H.events) if (ev.kind === "placeBlock") run(ev.fn, [{ __ev: true, entity: JEntity.of(player), pos, level: lv, state: BLOCK(block.typeId) }]);
});
world.afterEvents.playerBreakBlock.subscribe(({ block, player, brokenBlockPermutation }) => {
  const id = brokenBlockPermutation.type.id; const cls = blockClass.get(id); const lv = JLevel.of(block.dimension), pos = V(block.location.x, block.location.y, block.location.z);
  if (cls && H.block[cls].destroy) run(H.block[cls].destroy, [{ __cls: cls }, lv, pos, BLOCK(id), JEntity.of(player)]);
  for (const ev of H.events) if (ev.kind === "breakBlock") run(ev.fn, [{ __ev: true, player: JEntity.of(player), entity: JEntity.of(player), pos, level: lv, state: BLOCK(id) }]);
});
world.afterEvents.playerInteractWithBlock.subscribe(({ block, player, blockFace, faceLocation }) => {
  const cls = blockClass.get(block.typeId); const lv = JLevel.of(block.dimension), pos = V(block.location.x, block.location.y, block.location.z);
  if (cls && H.block[cls].use) run(H.block[cls].use, [{ __cls: cls }, BLOCK(block.typeId), lv, pos, JEntity.of(player), HAND, { getBlockPos: () => pos, getLocation: () => (faceLocation ? V(pos.x_ + faceLocation.x, pos.y_ + faceLocation.y, pos.z_ + faceLocation.z) : V(pos.x_ + 0.5, pos.y_ + 0.5, pos.z_ + 0.5)), getDirection: () => DIR[String(blockFace ?? "Up").toUpperCase()] ?? DIR.UP, isInside: () => false }]);
  for (const ev of H.events) if (ev.kind === "rightClickBlock") run(ev.fn, [{ __ev: true, entity: JEntity.of(player), player: JEntity.of(player), pos, level: lv, getPos: () => pos }]);
});
// standing in / on mod blocks
system.runInterval(() => {
  for (const p of world.getAllPlayers()) {
    for (const dy of [0, -1]) {
      let b; try { b = p.dimension.getBlock({ x: Math.floor(p.location.x), y: Math.floor(p.location.y + 0.01) + dy, z: Math.floor(p.location.z) }); } catch { continue; }
      const cls = b && blockClass.get(b.typeId); if (!cls) continue;
      const lv = JLevel.of(b.dimension), pos = V(b.location.x, b.location.y, b.location.z), w = JEntity.of(p);
      if (dy === 0 && H.block[cls].inside) run(H.block[cls].inside, [{ __cls: cls }, BLOCK(b.typeId), lv, pos, w]);
      if (dy === -1 && H.block[cls].stepOn) run(H.block[cls].stepOn, [{ __cls: cls }, lv, pos, BLOCK(b.typeId), w]);
    }
  }
}, 4);

// ---- items
world.afterEvents.itemUse.subscribe(({ source, itemStack }) => {
  const cls = itemClass.get(itemStack?.typeId); if (!cls || !H.item[cls].use) return;
  const w = JEntity.of(source); run(H.item[cls].use, [{ __cls: cls }, w.level(), w, HAND]);
});
world.afterEvents.itemCompleteUse.subscribe(({ source, itemStack }) => {
  const cls = itemClass.get(itemStack?.typeId); if (!cls || !H.item[cls].finish) return;
  const w = JEntity.of(source); run(H.item[cls].finish, [{ __cls: cls }, new Stack(itemStack.typeId, 1), w.level(), w]);
});
world.afterEvents.itemUseOn?.subscribe?.(({ source, itemStack, block }) => {
  const cls = itemClass.get(itemStack?.typeId); if (!cls || !H.item[cls].useOn) return;
  const w = JEntity.of(source), pos = V(block.location.x, block.location.y, block.location.z);
  run(H.item[cls].useOn, [{ __cls: cls }, { getLevel: () => w.level(), getClickedPos: () => pos, getPlayer: () => w, getItemInHand: () => R.handItem(w, "mainhand"), getHand: () => HAND }]);
});
// ---- client overlays (jumpscares, eye overlays): the mod's display procedure decides, the HUD shows the image
// (resource pack ui/hud_screen.json binds each image to a title made only of formatting codes, so no text shows)
const OVS = H.overlays ?? [];
const shown = new Map();
if (OVS.length) system.runInterval(() => {
  for (const p of world.getAllPlayers()) {
    const w = JEntity.of(p), lv = w.level(), l = p.location; let on = null;
    for (const o of OVS) {
      const desc = o.proc.slice(o.proc.indexOf("("));
      const args = []; const types = desc.slice(1, desc.indexOf(")")).match(/\[*(L[^;]+;|[A-Z])/g) ?? [];
      let d = 0;
      for (const t of types) {
        if (t === "D") args.push([l.x, l.y, l.z][d++] ?? 0);
        else if (t.includes("LevelAccessor") || t.includes("Level;")) args.push(lv);
        else if (t.includes("Entity") || t.includes("Player")) args.push(w);
        else args.push(null);
      }
      const r = run(o.proc, args);
      if (r === true || r === 1) { on = o; break; }
    }
    const prev = shown.get(p.id) ?? null;
    if (on) { try { p.onScreenDisplay.setTitle(on.key, { fadeInDuration: 0, stayDuration: 6, fadeOutDuration: 0 }); } catch {} }
    else if (prev) { try { p.onScreenDisplay.setTitle("§r", { fadeInDuration: 0, stayDuration: 1, fadeOutDuration: 0 }); } catch {} }
    shown.set(p.id, on);
  }
}, 3);
// ---- dev diagnostics: /scriptevent jb:fx (list mod effects + active overlay), /scriptevent jb:fxclear (Java's /effect clear for mod effects)
system.afterEvents.scriptEventReceive.subscribe(({ id, sourceEntity: p }) => {
  if (!p || (id !== "jb:fx" && id !== "jb:fxclear")) return;
  if (id === "jb:fxclear") {
    MODFX.delete(p.id);
    for (const k of p.getDynamicPropertyIds()) if (k.startsWith("fx:")) p.setDynamicProperty(k, undefined);
    try { p.runCommand("effect @s clear"); } catch {}
    p.sendMessage("[JavaBridge] mod effects cleared"); return;
  }
  const fx = p.getDynamicPropertyIds().filter(k => k.startsWith("fx:")).map(k => `${k.slice(3)}=${(p.getDynamicProperty(k) ?? 0) - system.currentTick}t`);
  const on = shown.get(p.id);
  const msg = `[JavaBridge] effects: ${fx.join(", ") || "none"} | overlay: ${on ? on.tex : "none"} | title key: ${JSON.stringify(on?.key ?? null)}`;
  p.sendMessage(msg); console.warn(msg);
});
console.warn(`[JavaBridge] ${NS}: ${Object.keys(H.entity).length} mobs, ${blockClass.size} blocks, ${itemClass.size} items, ${effectClass.size} effects, ${H.events.length} event hooks`);
