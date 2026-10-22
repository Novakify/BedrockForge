// From The Fog (Bedrock) - config menu (spec §1.8) with server-ui forms, replacing the Java 3D in-world menu.
import { system } from "@minecraft/server";
import { ActionFormData, ModalFormData, FormCancelationReason } from "@minecraft/server-ui";
import { CATEGORIES, SECRET_OPTIONS, opt, setOpt, lockedOptions } from "./config.js";
import { state, saveState, setActivity, ACHIEVEMENTS, getAchievements } from "./state.js";
import { SKINS } from "./skins.js";
import { playersNear, nearestPlayer } from "./util.js";

const ICON = (id) => `textures/ftf/ui/icons/${id}`;
const fmt = (s) => s.replace(/_/g, " ");

/** Listeners other modules register (avoids import cycles). */
export const hooks = { onGhcToggled: (/** @type {any} */ _p) => {} };

/** @param {import("@minecraft/server").Player} player */
export async function openConfig(player, attempt = 0) {
  const ghc = opt("give_him_control") === true;
  const form = new ActionFormData()
    .title(ghc ? "§4I Have Control." : "From The Fog")
    .body(ghc ? "§k||||||||||||||||||||§r\n§7He is in control now." : "§7From The Fog by LunarEclipseStudios\n§8Bedrock adaptation - CC BY-NC-SA 4.0");
  const entries = [];
  for (const c of CATEGORIES) { form.button(c.title, ICON(c.id)); entries.push(c.id); }
  form.button(ghc ? "§4Give Him Control: ON" : "Give Him Control", ICON("sightings")); entries.push("ghc");
  form.button("Journal", ICON("advancements")); entries.push("journal");
  if (!ghc) { form.button("§8 ", ICON("super_secret_settings")); entries.push("sss"); }
  player.playSound("note.pling", { pitch: 2 });
  const res = await form.show(player);
  if (res.canceled) {
    if (res.cancelationReason === FormCancelationReason.UserBusy && attempt < 20) system.runTimeout(() => openConfig(player, attempt + 1), 5);
    return;
  }
  player.playSound("note.snare");
  const sel = entries[res.selection ?? -1];
  if (!sel) return;
  if (sel === "skin_library") return skinPage(player, 0);
  if (sel === "ghc") return ghcPage(player);
  if (sel === "journal") return journal(player);
  if (sel === "sss") return optionPage(player, "Super Secret Settings", SECRET_OPTIONS);
  const cat = CATEGORIES.find((c) => c.id === sel);
  if (cat) return optionPage(player, cat.title, cat.options);
}

async function optionPage(player, title, options) {
  const locked = lockedOptions();
  const form = new ModalFormData().title(title);
  for (const [id, type, , label, choices] of options) {
    const lk = locked.includes(id) ? " §c(locked)" : "";
    const v = opt(id);
    if (type === "bool") form.toggle(label + lk, { defaultValue: v === true });
    else if (type === "enum") form.dropdown(label + lk, choices.map(fmt), { defaultValueIndex: Math.max(0, choices.indexOf(String(v))) });
    else if (type === "int") form.slider(label + lk, 0, 30, { defaultValue: Math.min(30, Number(v) || 0), valueStep: 1 });
  }
  form.submitButton("Save");
  const res = await form.show(player);
  if (res.canceled || !res.formValues) return openConfig(player);
  options.forEach(([id, type, , , choices], i) => {
    if (locked.includes(id)) { if (res.formValues?.[i] !== undefined) player.playSound("note.pling", { pitch: 0.5 }); return; }
    const raw = res.formValues?.[i];
    let nv;
    if (type === "bool") nv = raw === true;
    else if (type === "enum") nv = choices[Number(raw)];
    else nv = Number(raw);
    if (nv === opt(id)) return;
    setOpt(id, nv);
    player.playSound("random.click");
    if (type === "bool") player.playSound(`ftf.config.setting.${id}.${nv}`);
    if (id === "haunting_delay") { state.started_events = false; saveState(); }
  });
  return openConfig(player);
}

async function skinPage(player, page) {
  const visible = SKINS.filter((s) => !s.hidden);
  const per = 20;
  const pages = Math.ceil(visible.length / per);
  const slice = visible.slice(page * per, page * per + per);
  const list = new Set(opt("herobrine_skin") || ["default"]);
  const form = new ModalFormData().title(`Skin Library ${page + 1}/${pages}`);
  for (const s of slice) form.toggle(`${s.id.replace(/_/g, " ")} §8- ${s.credit}`, { defaultValue: list.has(s.id) });
  form.toggle("§7Next page", { defaultValue: false });
  form.submitButton("Save");
  const res = await form.show(player);
  if (res.canceled || !res.formValues) return openConfig(player);
  slice.forEach((s, i) => {
    const want = res.formValues?.[i] === true;
    if (want !== list.has(s.id)) player.playSound("armor.equip_leather");
    if (want) list.add(s.id); else list.delete(s.id);
  });
  if (list.size === 0) { list.add("default"); player.sendMessage("§7At least one skin must stay selected."); }
  setOpt("herobrine_skin", [...list]);
  if (res.formValues[slice.length] === true) return skinPage(player, (page + 1) % pages);
  return openConfig(player);
}

async function ghcPage(player) {
  const cur = opt("give_him_control") === true;
  const form = new ModalFormData().title("§4Give Him Control")
    .label("§7Every in-game day he rewrites your settings.\n§cHe will not ask again.")
    .toggle("§4I give him control", { defaultValue: cur })
    .submitButton("§4Confirm");
  const res = await form.show(player);
  if (res.canceled || !res.formValues) return openConfig(player);
  const nv = res.formValues[1] === true;
  if (nv === cur) return openConfig(player);
  setOpt("give_him_control", nv);
  if (nv) {
    for (const p of playersNear(player.dimension, player.location, 15)) {
      p.playSound("mob.elderguardian.curse", { pitch: 0.1 });
      p.playSound("ambient.cave", { pitch: 0.25 });
      p.playSound("ftf.events.sudden_scare.crash", { volume: 0.25, pitch: 0.25 });
    }
    setActivity(6, 360, 150);
    hooks.onGhcToggled(nearestPlayer(player.dimension, player.location) ?? player);
  } else player.playSound(`ftf.config.setting.give_him_control.false`);
}

async function journal(player) {
  const got = getAchievements(player);
  const lines = Object.entries(ACHIEVEMENTS).map(([id, [t, d]]) => (got.includes(id) ? `§a■ §f${t} §7- ${d}` : `§8■ ???`));
  const form = new ActionFormData().title("Journal")
    .body((opt("advancements") === true ? "" : "§cAchievements are off (Advancements page).\n\n") + lines.join("\n"))
    .button("Back");
  await form.show(player);
  return openConfig(player);
}
