#!/usr/bin/env python3
"""Generate the hand-authored parts of the Sculk Horde overlay that are tedious to write by hand.

  python build_extras.py <Sculk-Horde repo>/src/main/resources/assets/sculkhorde

Writes into this overlay folder (rp/ + bp/ + scripts/infested_map.js):
  * infested block variants (infested stone, deepslate, logs, terracotta... with the mod's textures, Apache-2.0)
    and the vanilla -> infested map the infestation cursors use
  * projectile / attack entities: acid blob (spitter), soul fire / poison / ice / breeze bolts (Angel of Reaping),
    sculk spine spike (Sculk Enderman), floor soul spears
"""
import json, shutil, sys
from pathlib import Path

HERE = Path(__file__).parent
SRC = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("/home/claude/sculk/src/src/main/resources/assets/sculkhorde")
NS = "sculkhorde"
RP, BP = HERE / "rp", HERE / "bp"

# vanilla block -> (infested id, {face: texture} | texture for all faces)
COLORS = ["white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray", "light_gray", "cyan", "purple",
          "blue", "brown", "green", "red", "black"]
BLOCKS = {
    "stone": "infested_stone", "cobblestone": "infested_cobblestone", "mossy_cobblestone": "infested_cobblestone",
    "andesite": "infested_andesite", "diorite": "infested_diorite", "granite": "infested_granite", "tuff": "infested_tuff",
    "calcite": "infested_calcite", "gravel": "infested_gravel", "clay": "infested_clay", "mud": "infested_mud",
    "packed_mud": "infested_packed_mud", "mud_bricks": "infested_mud_bricks", "moss_block": "infested_moss",
    "netherrack": "infested_netherrack", "end_stone": "infested_endstone", "smooth_basalt": "infested_smooth_basalt",
    "stone_bricks": "infested_stone_bricks", "cobbled_deepslate": "infested_cobbled_deepslate", "sand": "infested_sand",
    "red_sand": "infested_sand", "snow": "infested_snow", "terracotta": "infested_terracotta",
    "crying_obsidian": "infested_crying_obsidian",
    "deepslate": {"*": "infested_deepslate", "up": "infested_deepslate_top", "down": "infested_deepslate_top"},
    "sandstone": {"*": "infested_sandstone", "up": "infested_sandstone_top", "down": "infested_sandstone_bottom"},
    "basalt": {"*": "infested_basalt_side", "up": "infested_basalt_top", "down": "infested_basalt_top"},
    "blackstone": {"*": "infested_blackstone_side", "up": "infested_blackstone_top", "down": "infested_blackstone_top"},
    "polished_blackstone_bricks": "infested_blackstone_bricks",
    **{f"{c}_terracotta": f"infested_{c}_terracotta" for c in COLORS},
}
LOGS = ["oak", "spruce", "birch", "jungle", "acacia", "dark_oak", "mangrove", "cherry", "pale_oak"]
for w in LOGS:
    BLOCKS[f"{w}_log"] = {"*": "infested_log_side", "up": "infested_log", "down": "infested_log"}
    BLOCKS[f"{w}_wood"] = "infested_log_side"


def main():
    tex_dir = RP / "textures/blocks" / NS
    tex_dir.mkdir(parents=True, exist_ok=True)
    (BP / "blocks").mkdir(parents=True, exist_ok=True)
    tt_path = RP / "textures/terrain_texture.json"
    tt = json.loads(tt_path.read_text())
    blocks_json = json.loads((RP / "blocks.json").read_text())
    lang, mapping, made = {}, {}, set()
    for vanilla, spec in BLOCKS.items():
        faces = spec if isinstance(spec, dict) else {"*": spec}
        if not all((SRC / "textures/block" / f"{t}.png").exists() for t in faces.values()):
            print("skip (no texture):", vanilla); continue
        bid = faces["*"].replace("_side", "") if vanilla.endswith(("_log", "_wood")) else faces["*"]
        bid = {"infested_log": "infested_log", "infested_basalt": "infested_basalt", "infested_blackstone": "infested_blackstone"}.get(bid, bid)
        if vanilla.endswith("_wood"): bid = "infested_wood"
        mapping[f"minecraft:{vanilla}"] = f"{NS}:{bid}"
        if bid in made: continue
        made.add(bid)
        mats = {}
        for face, t in faces.items():
            shutil.copy(SRC / "textures/block" / f"{t}.png", tex_dir / f"{t}.png")
            tt["texture_data"][f"{NS}_{t}"] = {"textures": f"textures/blocks/{NS}/{t}"}
            mats[face] = {"texture": f"{NS}_{t}", "render_method": "opaque"}
        (BP / "blocks" / f"{bid}.json").write_text(json.dumps({"format_version": "1.21.40", "minecraft:block": {
            "description": {"identifier": f"{NS}:{bid}", "menu_category": {"category": "nature"}},
            "components": {"minecraft:destructible_by_mining": {"seconds_to_destroy": 1.2},
                           "minecraft:destructible_by_explosion": {"explosion_resistance": 6},
                           "minecraft:map_color": "#0b3a40", "minecraft:material_instances": mats,
                           "minecraft:loot": "loot_tables/empty.json"}}}, indent=1))
        blocks_json[f"{NS}:{bid}"] = {"sound": "sculk"}
        lang[f"tile.{NS}:{bid}.name"] = bid.replace("_", " ").title()
    tt_path.write_text(json.dumps(tt, indent=1))
    (RP / "blocks.json").write_text(json.dumps(blocks_json, indent=1))
    (BP / "loot_tables").mkdir(exist_ok=True)
    (BP / "loot_tables/empty.json").write_text('{"pools": []}')
    (HERE / "scripts/infested_map.js").write_text(
        "// generated by build_extras.py: vanilla block -> Sculk Horde infested variant\nexport const INFESTED = "
        + json.dumps(mapping, indent=1) + ";\n")

    # ---------------------------------------------------------------- projectiles / attacks
    projectiles = {
        "acid_blob": ("acid_blob_projectile", {"impact_damage": {"damage": 4, "knockback": True},
                      "mob_effect": {"effect": "poison", "durationeasy": 40, "durationnormal": 60, "durationhard": 100, "amplifier": 0}}, 0.05),
        "soul_fire_bolt": ("soul_fire_projectile", {"impact_damage": {"damage": 6, "knockback": True, "catch_fire": True}}, 0.0),
        "soul_poison_bolt": ("soul_poison_projectile", {"impact_damage": {"damage": 5, "knockback": True},
                      "mob_effect": {"effect": "poison", "durationeasy": 60, "durationnormal": 100, "durationhard": 140, "amplifier": 1}}, 0.0),
        "soul_ice_bolt": ("soul_ice_projectile", {"impact_damage": {"damage": 5, "knockback": True},
                      "mob_effect": {"effect": "slowness", "durationeasy": 60, "durationnormal": 100, "durationhard": 140, "amplifier": 2}}, 0.0),
        "soul_breeze_bolt": ("soul_breeze_projectile", {"impact_damage": {"damage": 4, "knockback": True},
                      "mob_effect": {"effect": "levitation", "durationeasy": 20, "durationnormal": 30, "durationhard": 40, "amplifier": 0}}, 0.0),
    }
    (BP / "entities").mkdir(exist_ok=True); (RP / "entity").mkdir(exist_ok=True)
    for pid, (stem, hit, gravity) in projectiles.items():
        on_hit = {**hit, "remove_on_hit": {}, "particle_on_hit": {"particle_type": "iconcrack", "num_particles": 6, "on_entity_hit": True, "on_other_hit": True}}
        (BP / "entities" / f"{pid}.json").write_text(json.dumps({"format_version": "1.21.0", "minecraft:entity": {
            "description": {"identifier": f"{NS}:{pid}", "is_spawnable": False, "is_summonable": True},
            "components": {"minecraft:collision_box": {"width": 0.4, "height": 0.4},
                           "minecraft:projectile": {"on_hit": on_hit, "power": 1.4, "gravity": gravity, "inertia": 1.0,
                                                    "liquid_inertia": 0.9, "anchor": 1, "offset": [0, 0, 0], "hit_sound": "bow.hit"},
                           "minecraft:physics": {}, "minecraft:pushable": {"is_pushable": False, "is_pushable_by_piston": True},
                           "minecraft:conditional_bandwidth_optimization": {"default_values": {"max_optimized_distance": 80.0, "max_dropped_ticks": 7, "use_motion_prediction_hints": True}},
                           "minecraft:despawn": {"despawn_from_distance": {"min_distance": 48, "max_distance": 64}}}}}, indent=1))
        client_entity(pid, stem)
    # ground attacks: harmless visual entities; the script deals the damage when they "emerge"
    for pid, stem in {"spine_spike": "sculk_spine_spike_attack", "floor_soul_spears": "floor_soul_spears"}.items():
        (BP / "entities" / f"{pid}.json").write_text(json.dumps({"format_version": "1.21.0", "minecraft:entity": {
            "description": {"identifier": f"{NS}:{pid}", "is_spawnable": False, "is_summonable": True},
            "components": {"minecraft:collision_box": {"width": 1.0, "height": 1.5}, "minecraft:physics": {"has_gravity": False, "has_collision": False},
                           "minecraft:damage_sensor": {"triggers": [{"cause": "all", "deals_damage": "no"}]},
                           "minecraft:pushable": {"is_pushable": False, "is_pushable_by_piston": False},
                           "minecraft:type_family": {"family": ["inanimate", "sculkhorde_attack"]}},
            }}, indent=1))
        client_entity(pid, stem, emerge=True)
    print(f"{len(made)} infested blocks ({len(mapping)} vanilla blocks mapped), {len(projectiles) + 2} attack entities")


def client_entity(pid, stem, emerge=False):
    anim_file = SRC / "animations/entity" / f"{stem}.animation.json"
    anims = {}
    if anim_file.exists():
        for k in json.loads(anim_file.read_text()).get("animations", {}):
            short = k.split(".")[-1].lower()
            anims[short] = f"animation.{NS}.{stem}.{short}"
    play = [a for a in (["emerge"] if emerge and "emerge" in anims else []) + (["living"] if "living" in anims else []) if a in anims]
    tex = f"textures/entity/{NS}/{stem}"
    glow = (SRC / "textures/entity" / f"{stem}_glowmask.png").exists()
    desc = {"identifier": f"{NS}:{pid}", "materials": {"default": "entity_alphatest"}, "textures": {"default": tex},
            "geometry": {"default": f"geometry.{NS}.{stem}"}, "render_controllers": ["controller.render.default"]}
    if glow:
        desc["materials"]["glow"] = "entity_emissive_alpha"; desc["textures"]["glow"] = tex + "_glowmask"
        desc["render_controllers"].append(f"controller.render.{NS}.glow")
    if play:
        desc["animations"] = {a: anims[a] for a in play}
        desc["scripts"] = {"animate": play}
    (RP / "entity" / f"{pid}.entity.json").write_text(json.dumps({"format_version": "1.10.0", "minecraft:client_entity": {"description": desc}}, indent=1))


if __name__ == "__main__":
    main()
