"""SRG (Forge 1.20.1 runtime names: m_20185_, f_50016_) -> Mojang names (getX, AIR).

The table is built locally from Mojang's 1.20.1 client mappings + MCPConfig joined.tsrg (see tools/ModelCore/README:
"SRG table"). Mojang's mappings may be used for development but not redistributed, so the table is NEVER committed:
set GB_SRG_MAP or keep it at ~/mappings/srg_1.20.1.json. Without it every lookup returns the SRG name unchanged.
"""
import json, os
from pathlib import Path

_T = None


def table():
    global _T
    if _T is None:
        cands = [os.environ.get("GB_SRG_MAP"), Path.home() / "mappings" / "srg_1.20.1.json", "/home/claude/mappings/srg_1.20.1.json"]
        p = next((Path(c) for c in cands if c and Path(c).exists()), None)
        _T = json.loads(p.read_text()) if p else {}
    return _T


def srg_name(n):
    e = table().get(n)
    return e["name"] if e else n


def srg_owner(n):
    e = table().get(n)
    return e["owner"] if e else None
