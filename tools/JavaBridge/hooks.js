// JavaBridge hooks (original Bedrock Forge code): call the translated Java methods from Bedrock events.
import { world, system } from "@minecraft/server";
import { M, R, JEntity, JLevel, V, bp, Stack, BLOCK, MODFX } from "./runtime.js";

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
world.afterEvents.playerInteractWithBlock.subscribe(({ block, player }) => {
  const cls = blockClass.get(block.typeId); const lv = JLevel.of(block.dimension), pos = V(block.location.x, block.location.y, block.location.z);
  if (cls && H.block[cls].use) run(H.block[cls].use, [{ __cls: cls }, BLOCK(block.typeId), lv, pos, JEntity.of(player), HAND, { getBlockPos: () => pos, getLocation: () => pos, getDirection: () => ({ n: "up" }) }]);
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
console.warn(`[JavaBridge] ${NS}: ${Object.keys(H.entity).length} mobs, ${blockClass.size} blocks, ${itemClass.size} items, ${effectClass.size} effects, ${H.events.length} event hooks`);
