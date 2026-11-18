// MobAI / JavaBridge smoke test with a fake Script API (node_modules/@minecraft/server here).
// Usage: copy a built pack's scripts/javabridge to ./jb, then `node smoke.mjs`. Ticks one of every mod mob for
// 200 ticks and prints script errors, plus the GoalRunner events each gated mob fired.
import { step, FakeEntity } from "@minecraft/server";
const warns = []; console.warn = (m) => warns.push(String(m));
await import("./jb/main.js");
const CFG = globalThis.__JB_CFG;
new FakeEntity("minecraft:player", { x: 0, y: 64, z: 0 }, { health: { currentValue: 20, effectiveMax: 20 } });
const mobs = Object.keys(CFG.typeToClass).map((t, i) => new FakeEntity(t, { x: 3 + (i % 20), y: 64, z: Math.floor(i / 20) * 3 },
  { health: { currentValue: 20, effectiveMax: 20 }, collision_box: { width: 0.6, height: 1.8 }, movement: { currentValue: 0.3 } }));
step(200);
const errs = warns.filter((w) => /JavaBridge/.test(w) && !/unsupported|mobs, \d+ blocks/.test(w));
console.log(`${mobs.length} mob types, ${errs.length} errors`); for (const e of errs.slice(0, 20)) console.log("  " + e);
for (const t of Object.keys(CFG.hooks.goals ?? {})) { const m = mobs.find((x) => x.typeId === t); console.log(t, [...new Set(m.events)].join(",")); }
