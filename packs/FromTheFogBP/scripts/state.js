// From The Fog (Bedrock) - persistent world state (replaces Java scoreboards/storage), activity multiplier, achievements journal.
import { world, system } from "@minecraft/server";
import { loadJSON, saveJSON, debug } from "./util.js";
import { opt, setOpt } from "./config.js";

const KEY = "ftf:state";
export const state = {
  started_events: false,
  logged_day: -1,
  /** seconds until the next "5 minute" event tick */
  five_min_left: 300,
};

export function loadState() { Object.assign(state, loadJSON(KEY, {})); }
export function saveState() { saveJSON(KEY, state); }

// ---------------------------------------------------------------- activity multiplier (spec §1.6)
let activityResetHandle;
export function activity() { return Math.max(1, Number(opt("activity_multiplier")) || 1); }
/**
 * @param {number} value multiplier
 * @param {number} delay seconds until it resets to 1
 * @param {number} eventTimer seconds until the next 5-minute event tick
 */
export function setActivity(value, delay, eventTimer) {
  setOpt("activity_multiplier", value);
  if (activityResetHandle !== undefined) system.clearRun(activityResetHandle);
  activityResetHandle = system.runTimeout(() => { setOpt("activity_multiplier", 1); activityResetHandle = undefined; }, delay * 20);
  state.five_min_left = eventTimer;
  saveState();
  debug(`activity x${value} for ${delay}s, events in ${eventTimer}s`);
}

// ---------------------------------------------------------------- start of the haunting (spec §1.7)
export function startEvents() {
  if (state.started_events) return;
  state.started_events = true;
  saveState();
  if (opt("eerie_entrance") === true) world.sendMessage({ rawtext: [{ text: "§e" }, { translate: "ftf.eerie_entrance.message.prefix" }, { text: "Herobrine" }, { translate: "ftf.eerie_entrance.message.suffix" }] });
  debug("hauntings started");
}

// ---------------------------------------------------------------- achievements journal (spec §2.22, Bedrock replacement for advancements)
export const ACHIEVEMENTS = {
  build_shrine: ["Malevolent Shrine", "Build and light a shrine"],
  first_encounter: ["First Encounter", "Spot Herobrine"],
  encounter_dwelling_shadow: ["Dwelling Shadow", "Get close to a Herobrine hiding in the dark"],
  encounter_dog_sense: ["Sighting Sense", "Your dog senses something"],
  encounter_ghost_miner: ["Ghost Miner", "Hear someone mining nearby"],
  encounter_pants: ["???", "From the pants"],
  experience_nightmare: ["Nightmare", "Wake up"],
  get_jumpscared: ["Sudden Scare", "Get too close"],
  missed_herobrine: ["Missed Him", "He was there, you never saw him"],
  punch_herobrine: ["Bad Idea", "Punch Herobrine"],
  kill_a_haunted_herd_mob: ["Haunted Herd", "Kill an animal that was not what it seemed"],
  find_any_structure: ["Strange Build", "Find a structure he left behind"],
  find_letter: ["Glowstone Letter", "Find a glowstone letter"],
  find_sand_pyramid: ["Sand Pyramid", "Find a sand pyramid"],
  find_mossy_pyramid: ["Mossy Pyramid", "Find a mossy pyramid"],
  find_wooden_cross: ["Wooden Cross", "Find a wooden cross"],
  get_donation: ["Dreadful Donation", "Find something you never put in a chest"],
  get_named_donation: ["Named Donation", "Find a named gift"],
  get_haunted_heirloom: ["Haunted Heirloom", "Find a haunted heirloom"],
  "3_days": ["3 Days", "Play for 3 days"],
  "10_days": ["10 Days", "Play for 10 days"],
  "50_days": ["50 Days", "Play for 50 days"],
  "100_days": ["100 Days", "Play for 100 days"],
  "200_days": ["200 Days", "Play for 200 days"],
};

/** @param {import("@minecraft/server").Player} player */
export function getAchievements(player) {
  try { const r = player.getDynamicProperty("ftf:achievements"); return typeof r === "string" ? JSON.parse(r) : []; } catch { return []; }
}
/** @param {import("@minecraft/server").Player} player @param {string} id */
export function grant(player, id) {
  if (opt("advancements") !== true || !player?.isValid) return;
  const list = getAchievements(player);
  if (list.includes(id)) return;
  list.push(id);
  player.setDynamicProperty("ftf:achievements", JSON.stringify(list));
  const a = ACHIEVEMENTS[id];
  if (!a) return;
  player.onScreenDisplay.setActionBar(`§eAchievement: §f${a[0]}`);
  player.playSound("random.levelup", { volume: 0.5, pitch: 1.5 });
}
