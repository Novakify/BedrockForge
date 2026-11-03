#!/usr/bin/env python3
"""Preview the stick figure wearing the armor (rest pose), 4 views, no game needed.

usage: preview.py STICK_SKELETON ARMOR_GEO OUT.png [head=golden chest=diamond legs=netherite feet=iron]
  STICK_SKELETON: stick_skeleton.json      ARMOR_GEO: OUT/models/entity/fb_armor_stick.geo.json from stick_armor.py
  The armor is drawn with each slot's material texture colour (TEXDIR next to ARMOR_GEO, if present) or a flat colour.

Rest pose note: in the rest pose the stick's arms hang behind the torso (they overlap it in these views). The Figura
animation swings them out at runtime and every armor piece on an arm bone goes with it.
"""
import json
import sys
from pathlib import Path

import matplotlib
import numpy as np

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

COL = {"leather": (139, 90, 55), "chainmail": (118, 120, 128), "iron": (196, 200, 206), "golden": (236, 188, 40),
       "diamond": (52, 214, 208), "netherite": (62, 54, 62), "copper": (204, 112, 70), "turtle": (70, 162, 72)}
ACC = {"leather": (236, 196, 140), "chainmail": (225, 228, 240), "iron": (255, 255, 255), "golden": (255, 252, 210),
       "diamond": (236, 255, 255), "netherite": (196, 92, 255), "copper": (255, 214, 170), "turtle": (196, 255, 168)}
HIDE = {"p62", "p68", "p9", "p11", "p13", "p15"}  # first-person hands, alternate head rings (not shown by default)


def corners(o, s):
    x0, y0, z0 = o
    x1, y1, z1 = o[0] + s[0], o[1] + s[1], o[2] + s[2]
    v = np.array([[x0, z0, y0], [x1, z0, y0], [x1, z0, y1], [x0, z0, y1], [x0, z1, y0], [x1, z1, y0], [x1, z1, y1], [x0, z1, y1]])
    return [v[i] for i in ([0, 1, 2, 3], [4, 5, 6, 7], [0, 1, 5, 4], [2, 3, 7, 6], [1, 2, 6, 5], [0, 3, 7, 4])]


def main():
    skel, armor, out = sys.argv[1:4]
    pick = {"head": "golden", "chest": "diamond", "legs": "netherite", "feet": "iron"}
    for a in sys.argv[4:]:
        k, v = a.split("=")
        pick[k] = v
    g = json.loads(Path(skel).read_text())["minecraft:geometry"][0]
    A = json.loads(Path(armor).read_text())["minecraft:geometry"]
    views = [("front", -90, 5), ("3/4 front", -55, 15), ("side", 0, 5), ("back 3/4", 125, 15)]
    fig = plt.figure(figsize=(16, 10), facecolor="#20202a")
    for k, (name, az, el) in enumerate(views):
        ax = fig.add_subplot(1, 4, k + 1, projection="3d")
        ax.set_facecolor("#20202a")
        for b in g["bones"]:
            if "poly_mesh" in b and b["name"] not in HIDE:
                P = np.array(b["poly_mesh"]["positions"])
                ax.scatter(P[:, 0], P[:, 2], P[:, 1], s=0.5, color="#e0e0e0", alpha=.6)
        for geo in A:
            slot = geo["description"]["identifier"].split(".")[-1]
            m = pick.get(slot, "iron")
            for b in geo["bones"]:
                for c in b.get("cubes", []):
                    ax.add_collection3d(Poly3DCollection(corners(c["origin"], c["size"]), facecolor=np.array(COL[m]) / 255,
                                                         edgecolor=np.array(ACC[m]) / 255, linewidths=.5, alpha=.9))
        ax.set_xlim(-6, 6)
        ax.set_ylim(-6, 6)
        ax.set_zlim(-1, 38)
        ax.set_box_aspect((12, 12, 39))
        ax.view_init(elev=el, azim=az)
        ax.set_title(name, color="w")
        ax.set_axis_off()
    plt.tight_layout()
    plt.savefig(out, dpi=80, facecolor="#20202a")


if __name__ == "__main__":
    main()
