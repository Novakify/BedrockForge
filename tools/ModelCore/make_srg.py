#!/usr/bin/env python3
"""Build the local SRG -> Mojang name table used by ModelCore.srg (never commit the output).

    python make_srg.py client_mappings_1.20.1.txt joined_1.20.1.tsrg ~/mappings/srg_1.20.1.json

Inputs: Mojang's official 1.20.1 client mappings (obf -> Mojang; development use only, no redistribution) and
MinecraftForge/MCPConfig versions/release/1.20.1/joined.tsrg (obf -> SRG). Output: {srg: {"name", "owner"}}.
"""
import json, re, sys

PRIM = {"int": "I", "long": "J", "float": "F", "double": "D", "boolean": "Z", "byte": "B", "char": "C", "short": "S", "void": "V"}


def main(cm, tsrg, out):
    lines = open(cm, encoding="utf-8").read().splitlines()
    m2o = {}
    for l in lines:
        if not l.startswith((" ", "#")):
            m = re.match(r"(\S+) -> (\S+):", l); m2o[m.group(1)] = m.group(2)
    o2m = {v: k for k, v in m2o.items()}

    def td(t):
        arr = 0
        while t.endswith("[]"): arr += 1; t = t[:-2]
        return "[" * arr + (PRIM.get(t) or "L" + m2o.get(t, t).replace(".", "/") + ";")
    fld, mth, cur = {}, {}, None
    for l in lines:
        if l.startswith("#"): continue
        if not l.startswith(" "): cur = re.match(r"(\S+) -> (\S+):", l).group(2); continue
        l = l.strip()
        m = re.match(r"(?:\d+:\d+:)?(\S+) (\S+)\((.*)\)(?::\d+:\d+)? -> (\S+)", l)
        if m:
            ret, name, args, obf = m.groups()
            mth[(cur, obf, "(" + "".join(td(a) for a in args.split(",") if a) + ")" + td(ret))] = name; continue
        m = re.match(r"(\S+) (\S+) -> (\S+)", l)
        if m: fld[(cur, m.group(3))] = m.group(2)
    srg, cur = {}, None
    for l in open(tsrg, encoding="utf-8").read().splitlines()[1:]:
        if not l.startswith("\t"): cur = l.split()[0]; continue
        if l.startswith("\t\t"): continue
        p = l.strip().split()
        if len(p) == 3 and p[1].startswith("f_") and (cur, p[0]) in fld: srg[p[1]] = {"name": fld[(cur, p[0])], "owner": o2m.get(cur, cur)}
        if len(p) == 4 and p[2].startswith("m_") and (cur, p[0], p[1]) in mth: srg[p[2]] = {"name": mth[(cur, p[0], p[1])], "owner": o2m.get(cur, cur)}
    json.dump(srg, open(out, "w")); print(len(srg), "names")


if __name__ == "__main__":
    main(*sys.argv[1:4])
