import { step, FakeEntity } from "@minecraft/server";
console.warn = () => {};
await import("./jb/main.js");
const C = globalThis.__JB_CFG, H = C.hooks;
const R = await import("./jb/runtime.js");
const p = new FakeEntity("minecraft:player", { x: 0, y: 64, z: 0 }, { health: { currentValue: 20, effectiveMax: 20 } });
step(60);
const DS = (cause, attacker) => ({ cause, attacker: attacker ? R.JEntity.of(attacker) : null, key: cause, getEntity() { return this.attacker; }, getDirectEntity() { return this.attacker; }, is() { return false; }, getMsgId() { return cause; } });
const res = {};
for (const [t, hk] of Object.entries(H.entity)) {
  if (!hk.hurt) continue;
  const m = new FakeEntity(t, { x: 2, y: 64, z: 0 }, { health: { currentValue: 20, effectiveMax: 20 }, collision_box: { width: .6, height: 1.8 } });
  const w = R.JEntity.of(m); let r;
  for (const id of hk.hurt) { try { r = R.M.call(id, [w, DS("entityAttack", p), 5]); } catch (e) { r = "ERR " + e; } }
  const k = String(r); (res[k] ??= []).push(t.split(":")[1]);
}
for (const [k, v] of Object.entries(res)) console.log(k, v.length, v.join(","));
