// From The Fog (Bedrock) - configuration (spec §1.8) and Give Him Control (§1.9).
import { world } from "@minecraft/server";
import { loadJSON, saveJSON, pick } from "./util.js";

const CHANCE = ["common", "default", "rare", "scarce"];
const DELAY = ["0.1", "0.5", "1", "2"];
const STRUCT = ["off", "common", "default", "rare"];
const LANG = ["local", "swedish"];

/**
 * Option definitions grouped by menu page.
 * type: bool | enum | int | skins
 * @type {{id:string, title:string, options:any[][]}[]}
 */
export const CATEGORIES = [
  { id: "sightings", title: "Sightings", options: [
    ["creeping_sighting", "bool", false, "Creeping sighting"],
    ["nightmare_sighting", "bool", true, "Nightmare sighting"],
    ["stalking_sighting", "bool", true, "Stalking sighting"],
    ["lurking_sighting", "bool", true, "Lurking sighting"],
    ["dwelling_sighting", "bool", true, "Dwelling sighting"],
    ["dwelling_shadow", "bool", true, "Dwelling Shadow"],
    ["sighting_frequency", "enum", "default", "Sighting frequency", CHANCE],
    ["sighting_window", "enum", "always", "Sighting window", ["always", "day", "night", "rain"]],
    ["sudden_scare", "bool", true, "Sudden Scare"],
    ["malicious_malfunction", "bool", false, "Malicious Malfunction (harmless fake glitch)"],
    ["suspenseful_sighting", "enum", "off", "Suspenseful Sighting", ["off", "default", "dynamic"]],
    ["shrouded_specter", "enum", "off", "Shrouded Specter", ["off", "poof", "smoke", "portal"]],
    ["no_sleep", "bool", true, "No Sleep"],
    ["window_watcher", "bool", true, "Window Watcher"],
    ["sneaky_strike", "bool", true, "Sneaky Strike"],
    ["sneaky_strike_chance", "enum", "default", "Sneaky Strike chance", CHANCE],
    ["sighting_sense", "bool", true, "Sighting Sense (dogs)"],
    ["sighting_sense_chance", "enum", "default", "Sighting Sense chance", CHANCE],
    ["nametag", "enum", "off", "Nametag", ["off", "dynamic", "cryptic"]],
    ["sighting_noise", "enum", "cave", "Sighting noise", ["off", "cave", "ghast"]],
    ["emissive_layers", "bool", true, "Emissive layers (glowing eyes)"],
  ] },
  { id: "vanishing_delay", title: "Vanishing Delay", options: [
    ["creeping_vanishing_delay", "enum", "0.1", "Creeping (s)", DELAY],
    ["dwelling_vanishing_delay", "enum", "0.5", "Dwelling (s)", DELAY],
    ["stalking_vanishing_delay", "enum", "0.5", "Stalking (s)", DELAY],
    ["lurking_vanishing_delay", "enum", "1", "Lurking (s)", DELAY],
  ] },
  { id: "skin_library", title: "Skin Library", options: [] },
  { id: "burning_base", title: "Burning Base", options: [
    ["burning_base", "bool", false, "Burning Base"], ["burning_base_chance", "enum", "default", "Chance", CHANCE]] },
  { id: "disappearing_torches", title: "Disappearing Torches", options: [
    ["disappearing_torches", "bool", true, "Disappearing Torches"], ["disappearing_torches_chance", "enum", "default", "Chance", CHANCE]] },
  { id: "malevolent_shrine", title: "Malevolent Shrine", options: [
    ["haunting_delay", "int", 3, "Haunting delay (days)"],
    ["rekindling_shrine", "bool", true, "Rekindling Shrine"],
    ["shrine_surprise", "bool", false, "Shrine Surprise"],
    ["shrine_strike", "bool", false, "Shrine Strike"],
    ["eerie_entrance", "bool", false, "Eerie Entrance"],
    ["caution_caption", "bool", true, "Caution Caption"],
    ["lurking_language", "enum", "local", "Lurking language", LANG],
  ] },
  { id: "dreadful_donation", title: "Dreadful Donation", options: [
    ["dreadful_donation", "bool", true, "Dreadful Donation"], ["dreadful_donation_chance", "enum", "default", "Chance", CHANCE],
    ["haunted_heirlooms", "bool", false, "Haunted Heirlooms"], ["lurking_language", "enum", "local", "Lurking language", LANG]] },
  { id: "leafless_grove", title: "Leafless Grove", options: [
    ["leafless_grove", "bool", true, "Leafless Grove"], ["leafless_grove_chance", "enum", "default", "Chance", CHANCE]] },
  { id: "structures", title: "Structures", options: [
    ["mossy_pyramid", "enum", "default", "Mossy pyramid", STRUCT], ["sand_pyramid", "enum", "default", "Sand pyramid", STRUCT],
    ["letter", "enum", "off", "Glowstone letters", STRUCT], ["wooden_cross", "enum", "off", "Wooden cross", STRUCT]] },
  { id: "chilled_candles", title: "Chilled Candles", options: [
    ["chilled_candles", "bool", true, "Chilled Candles"], ["chilled_candles_chance", "enum", "default", "Chance", CHANCE]] },
  { id: "crimson_curse", title: "Crimson Curse", options: [
    ["crimson_curse", "bool", true, "Crimson Curse"], ["crimson_curse_chance", "enum", "default", "Chance", CHANCE],
    ["soul_shift", "bool", false, "Soul Shift (lanterns)"]] },
  { id: "sinister_signs", title: "Sinister Signs", options: [
    ["sinister_signs", "bool", true, "Sinister Signs"], ["sinister_signs_chance", "enum", "default", "Chance", CHANCE],
    ["lurking_language", "enum", "local", "Lurking language", LANG], ["biome_boards", "bool", true, "Biome boards"]] },
  { id: "ghost_miner", title: "Ghost Miner", options: [
    ["ghost_miner", "bool", true, "Ghost Miner"], ["ghost_miner_chance", "enum", "default", "Chance", CHANCE]] },
  { id: "advancements", title: "Advancements", options: [["advancements", "bool", false, "Achievements (journal + toasts)"]] },
  { id: "fearful_footsteps", title: "Fearful Footsteps", options: [
    ["fearful_footsteps", "bool", true, "Fearful Footsteps"], ["fearful_footsteps_chance", "enum", "default", "Chance", CHANCE]] },
  { id: "haunted_herd", title: "Haunted Herd", options: [
    ["haunted_herd", "bool", true, "Haunted Herd"], ["haunted_herd_chance", "enum", "default", "Chance", CHANCE]] },
  { id: "twisted_tapestries", title: "Twisted Tapestries", options: [
    ["twisted_tapestries", "bool", false, "Twisted Tapestries (not available on Bedrock)"], ["twisted_tapestries_chance", "enum", "default", "Chance", CHANCE]] },
  { id: "ghost_doors", title: "Ghost Doors", options: [
    ["ghost_doors", "bool", true, "Ghost Doors"], ["ghost_doors_chance", "enum", "default", "Chance", CHANCE]] },
];

/** @type {any[][]} */
export const SECRET_OPTIONS = [
  ["hoebrine_returns", "bool", false, "Hoebrine returns (no Bedrock model, uses default)"],
  ["from_the_pants", "bool", false, "From the pants (legs only)"],
];

const HIDDEN_DEFAULTS = {
  nightmare_vanishing_delay: "0.1",
  ghost_miner_vanishing_delay: "0.5",
  activity_multiplier: 1,
  give_him_control: false,
  herobrine_skin: ["default"],
};

export const DEFAULTS = (() => {
  /** @type {Record<string, any>} */
  const d = { ...HIDDEN_DEFAULTS };
  for (const c of CATEGORIES) for (const [id, , def] of c.options) d[id] = def;
  for (const [id, , def] of SECRET_OPTIONS) d[id] = def;
  return d;
})();

const KEY = "ftf:config";
/** @type {Record<string, any>} */
let cfg = { ...DEFAULTS };

export function loadConfig() {
  const saved = loadJSON(KEY, {});
  cfg = { ...DEFAULTS, ...saved };
  if (!Array.isArray(cfg.herobrine_skin) || cfg.herobrine_skin.length === 0) cfg.herobrine_skin = ["default"];
}
export function saveConfig() { saveJSON(KEY, cfg); }
/** @param {string} id */
export function opt(id) { return cfg[id]; }
/** @param {string} id */
export const on = (id) => cfg[id] === true;
export function setOpt(id, value) { cfg[id] = value; saveConfig(); }
export function allOptions() { return cfg; }
/** Server admins may set world property ftf:locked to a JSON array of option ids. */
export function lockedOptions() { return loadJSON("ftf:locked", []); }

// ---------------------------------------------------------------- Give Him Control (spec §1.9)
const GHC_KEY = "ftf:ghc_backup";
const GHC_BOOLS = ["burning_base", "dwelling_shadow", "no_sleep", "window_watcher", "sneaky_strike", "sighting_sense", "disappearing_torches",
  "shrine_strike", "shrine_surprise", "rekindling_shrine", "dreadful_donation", "haunted_heirlooms", "leafless_grove", "chilled_candles",
  "crimson_curse", "soul_shift", "sinister_signs", "biome_boards", "ghost_miner", "fearful_footsteps", "haunted_herd", "twisted_tapestries", "ghost_doors"];
const GHC_CHANCES = ["burning_base_chance", "sighting_frequency", "sneaky_strike_chance", "sighting_sense_chance", "disappearing_torches_chance",
  "dreadful_donation_chance", "leafless_grove_chance", "chilled_candles_chance", "crimson_curse_chance", "sinister_signs_chance", "ghost_miner_chance",
  "haunted_herd_chance", "twisted_tapestries_chance", "ghost_doors_chance"];
const GHC_STRUCTS = ["mossy_pyramid", "sand_pyramid", "letter", "wooden_cross"];

export function ghcHasBackup() { return typeof world.getDynamicProperty(GHC_KEY) === "string"; }

export function ghcInit() {
  const backup = { ...cfg, give_him_control: false };
  saveJSON(GHC_KEY, backup);
  for (const s of ["creeping_sighting", "nightmare_sighting", "stalking_sighting", "lurking_sighting", "dwelling_sighting", "sudden_scare"]) cfg[s] = true;
  cfg.sighting_window = "always";
  cfg.eerie_entrance = false;
  cfg.haunting_delay = 0;
  ghcRandomize();
}

export function ghcRandomize() {
  for (const id of GHC_BOOLS) cfg[id] = Math.random() < 0.5;
  for (const id of GHC_CHANCES) cfg[id] = pick(["insane", "common", "default"]);
  for (const id of GHC_STRUCTS) cfg[id] = pick(["common", "default"]);
  saveConfig();
}

export function ghcCleanup() {
  const backup = loadJSON(GHC_KEY, null);
  world.setDynamicProperty(GHC_KEY, undefined);
  cfg = { ...DEFAULTS, ...(backup || {}), give_him_control: false };
  saveConfig();
}
