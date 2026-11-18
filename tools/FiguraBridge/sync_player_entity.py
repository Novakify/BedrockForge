#!/usr/bin/env python3
"""Bring an avatar's player.entity.json up to the game's own vanilla player scripts (idempotent).

FiguraBridge's player.entity.json is a copy of the vanilla one from the bedrock-samples version it was compiled
against. The game keeps using its *current* vanilla animations (e.g. vanilla_1.21.130 player_firstperson), which
read variables newer vanilla entities set in pre_animation - first_person_item_rotation_factor, the melee spear
variables... With the old entity those reads fail: in game ~100k-130k Molang errors per session in the content log,
6 per frame while the arm swings (placing blocks, attacking), plus attachables reading missing public variables.

This replaces the entity's vanilla initialize / pre_animation lines with the game's, keeps every line the
avatar added (FiguraBridge v.fb* lines, `?? 0` defaults, older names), and merges the public variable list.

usage: sync_player_entity.py VANILLA_PLAYER_ENTITY_JSON PLAYER_ENTITY_JSON [OUT]
  VANILLA_PLAYER_ENTITY_JSON: the game's minecraft:player client entity (e.g. extracted from
  data/resource_packs/vanilla_<ver>/__brarchive/entity.brarchive)
"""
import json
import re
import sys
from pathlib import Path

ASSIGN = re.compile(r"\s*(?:v|variable)\.([a-z0-9_]+)\s*=(?!=)", re.I)


def assigned(lines):
    return set(re.findall(r"(?:v|variable)\.([a-z0-9_]+)\s*=(?!=)", " ".join(lines), re.I))


def merge(vanilla_lines, own_lines):
    vas = assigned(vanilla_lines)
    same = set(vanilla_lines)
    defaults, extra = [], []
    for l in own_lines:
        if l in same:
            continue                     # already a vanilla line (multi-line blocks, temps): no duplicate
        m = ASSIGN.match(l)
        if m and m.group(1) in vas and "??" not in l:
            continue                     # an (older) copy of a vanilla line: the game's version replaces it
        (defaults if m and "??" in l and l.split("=", 1)[1].strip().startswith(f"variable.{m.group(1)}") else extra).append(l)
    # `x = x ?? 0` defaults first (vanilla lines may read them), then vanilla, then the avatar's own lines
    return defaults + list(vanilla_lines) + extra


def sync(vanilla: dict, ent: dict) -> dict:
    v = vanilla["minecraft:client_entity"]["description"]["scripts"]
    d = ent["minecraft:client_entity"]["description"]["scripts"]
    for k in ("initialize", "pre_animation"):
        d[k] = merge(v.get(k, []), d.get(k, []))
    d.setdefault("variables", {}).update({k: x for k, x in v.get("variables", {}).items() if k not in d["variables"]})
    return ent


if __name__ == "__main__":
    van = json.loads(Path(sys.argv[1]).read_text())
    ent = json.loads(Path(sys.argv[2]).read_text())
    out = Path(sys.argv[3] if len(sys.argv) > 3 else sys.argv[2])
    out.write_text(json.dumps(sync(van, ent), indent=1))
    s = ent["minecraft:client_entity"]["description"]["scripts"]
    print(f"pre_animation {len(s['pre_animation'])} lines, initialize {len(s['initialize'])}, variables {len(s['variables'])}")
