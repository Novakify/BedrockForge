#!/usr/bin/env python3
"""Death scenes 8-14: bake the tables shaders/death_tables.hlsli is made of.

  python3 tools/gen_death_tables.py <Finishers.jar> <SableRagdolls.jar> [EpicFight.jar]

1. Finishers (by akeresmods, used with the author's permission): its Blockbench / player-animation clips are sampled
   at 20 Hz (the game's tick) into per-bone rotation + offset tables, so the phantom and the victim move exactly as the
   mod's own animations do (its easings included).
2. Ragdolls (Ragdollified by raiiiden, Sable Ragdolls by Leonardoinc22, used with permission): each mod's ragdoll is
   simulated offline with Bullet (pybullet - Ragdollified itself runs JBullet) on a six-box Minecraft body, and the
   result is baked at 20 Hz (part centre + rotation). The shader turns each bake by the kill's random yaw and may
   mirror it, so one bake reads as several different falls.
Everything is packed as halves (f16) in uint arrays to keep the shader's constant data small.
4. Epic Fight (by Yesman and team, used with permission): its signature weapon skills, from its own animation files
   (20-joint armature, keyframed matrices), run through forward kinematics and folded onto the 6-box Minecraft body
   (+ the weapon on bone 7), baked into the same clip tables; the hits are the weapon tip's speed peaks.
3. Sounds: the mods' own .ogg files (Finishers' punches / sword swing / victim cry, Sable Ragdolls' impacts) are
   embedded in src/DeathSoundsData.h with the cue list (scene, time, sound, volume, pitch) that src/DeathSounds.cpp
   plays as each death scene runs - the ragdolls' thuds come from the same bakes as the dust.
"""
import json, math, sys, zipfile, pathlib
import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "shaders/death_tables.hlsli"
HZ = 20.0

def f16(x): return int(np.array([x], np.float16).view(np.uint16)[0])
def pack(vals):   # floats -> uints, two halves each
    vals = list(vals) + ([0.0] if len(vals) & 1 else [])
    return [f16(vals[i]) | (f16(vals[i + 1]) << 16) for i in range(0, len(vals), 2)]

# ------------------------------------------------------------------ 1. Finishers clips
def ease(name, x):
    x = min(max(x, 0.0), 1.0)
    c1 = 1.70158; c3 = c1 + 1; c2 = c1 * 1.525
    if name in (None, "linear"): return x
    if name == "easeInQuad": return x * x
    if name == "easeOutSine": return math.sin(x * math.pi / 2)
    if name == "easeInCirc": return 1 - math.sqrt(1 - x * x)
    if name == "easeOutCirc": return math.sqrt(1 - (x - 1) ** 2)
    if name == "easeInBack": return c3 * x ** 3 - c1 * x * x
    if name == "easeOutBack": return 1 + c3 * (x - 1) ** 3 + c1 * (x - 1) ** 2
    if name == "easeInOutBack":
        return (2 * x) ** 2 * ((c2 + 1) * 2 * x - c2) / 2 if x < 0.5 else ((2 * x - 2) ** 2 * ((c2 + 1) * (x * 2 - 2) + c2) + 2) / 2
    if name == "easeInElastic":
        return 0 if x == 0 else (1 if x == 1 else -(2 ** (10 * x - 10)) * math.sin((x * 10 - 10.75) * (2 * math.pi / 3)))
    raise SystemExit(f"easing {name}?")

def channel(ch, t):
    """Bedrock-style keyframes: the easing on a keyframe shapes the way INTO it (player-animation / emotecraft)."""
    if isinstance(ch, list): return [float(a) for a in ch]
    if "vector" in ch: return [float(a) for a in ch["vector"]]
    ks = sorted(((float(k), v) for k, v in ch.items()), key=lambda kv: kv[0])
    vec = lambda v: [float(a) for a in (v["vector"] if isinstance(v, dict) else v)]
    if t <= ks[0][0]: return vec(ks[0][1])
    for (t0, v0), (t1, v1) in zip(ks, ks[1:]):
        if t <= t1:
            u = ease(v1.get("easing") if isinstance(v1, dict) else None, (t - t0) / max(t1 - t0, 1e-6))
            a, b = vec(v0), vec(v1)
            return [a[i] + (b[i] - a[i]) * u for i in range(3)]
    return vec(ks[-1][1])

BONES = ["head", "body", "right_arm", "left_arm", "right_leg", "left_leg", "root", "finger"]
ALIAS = {"torso": "body", "evrything": "root", "Middle finger": "finger"}

def clip(anims, name, ticks=None):
    a = anims[name]
    n = ticks if ticks else int(math.ceil(float(a["animation_length"]) * HZ)) + 1
    bones = {ALIAS.get(k, k): v for k, v in a["bones"].items()}
    rows = []
    for f in range(n):
        t = f / HZ
        for b in BONES:
            bv = bones.get(b, {})
            r = channel(bv["rotation"], t) if "rotation" in bv else [0, 0, 0]
            p = channel(bv["position"], t) if "position" in bv else [0, 0, 0]
            s = channel(bv["scale"], t)[0] if "scale" in bv else 1.0
            # degrees -> radians. The body here is Bedrock's own geometry (right arm at -x, faces +z), so the clip's
            # Bedrock values go straight in: + pitch bows forward, + roll swings a limb towards +x
            rows += [math.radians(r[0]), math.radians(r[1]), math.radians(r[2]), p[0], p[1], p[2], s, 0.0]
    return n, rows

# ------------------------------------------------------------------ 2. Ragdolls (pybullet)
DPX = 0.05859375
PC = [(0, 28, 0), (0, 18, 0), (-6, 18, 0), (6, 18, 0), (-2, 6, 0), (2, 6, 0)]
PE = [(4, 4, 4), (4, 6, 2), (2, 6, 2), (2, 6, 2), (2, 6, 2), (2, 6, 2)]
PP = [(0, 24, 0), None, (-5, 22, 0), (5, 22, 0), (-2, 12, 0), (2, 12, 0)]
MASS = [0.08, 0.40, 0.055, 0.055, 0.12, 0.12]

def toSim(v): return (v[0], v[2], v[1])     # ours (x, y up, z fwd) -> bullet (x, y fwd, z up)
def fromSim(v): return (v[0], v[2], v[1])

def ragdoll(spec, seconds=4.8):
    import pybullet as p
    cid = p.connect(p.DIRECT)
    p.setGravity(0, 0, -spec.get("g", 15.0))
    p.setTimeStep(1 / 240.0); p.setPhysicsEngineParameter(numSolverIterations=60)
    plane = p.createCollisionShape(p.GEOM_PLANE)
    floor = p.createMultiBody(0, plane)
    p.changeDynamics(floor, -1, lateralFriction=spec.get("mu", 0.75), restitution=0.15)
    ids = []
    for j in range(6):
        e = [x * DPX * 0.97 for x in toSim(PE[j])]
        sh = p.createCollisionShape(p.GEOM_BOX, halfExtents=e)
        b = p.createMultiBody(MASS[j], sh, basePosition=[x * DPX for x in toSim(PC[j])])
        p.changeDynamics(b, -1, lateralFriction=spec.get("mu", 0.75), restitution=spec.get("e", 0.18),
                         linearDamping=0.04, angularDamping=spec.get("angDamp", 0.08), rollingFriction=0.002)
        ids.append(b)
    cons = {}
    for j in (0, 2, 3, 4, 5):   # ball joints at the pivots (Ragdollified: cone/6dof; limits added below as soft torques)
        pv = [x * DPX for x in toSim(PP[j])]
        pa = [pv[k] - PC[1][[0, 2, 1][k]] * DPX for k in range(3)]
        pb = [pv[k] - PC[j][[0, 2, 1][k]] * DPX for k in range(3)]
        cons[j] = p.createConstraint(ids[1], -1, ids[j], -1, p.JOINT_POINT2POINT, [0, 0, 0], pa, pb)
        p.changeConstraint(cons[j], maxForce=400)
        p.setCollisionFilterPair(ids[1], ids[j], -1, -1, 0)
    p.setCollisionFilterPair(ids[0], ids[2], -1, -1, 0); p.setCollisionFilterPair(ids[0], ids[3], -1, -1, 0)
    # the blow: every part gets the launch velocity, plus Ragdollified's limb whip and spin
    for j in range(6):
        v = list(spec["v"]); w = list(spec.get("w", (0, 0, 0)))
        if j in spec.get("whip", {}): v = [v[k] + spec["whip"][j][k] for k in range(3)]
        p.resetBaseVelocity(ids[j], toSim(v), toSim(w))
    LIM = {0: 0.75, 2: 2.6, 3: 2.6, 4: 1.7, 5: 1.7}     # swing limits (rad): neck, shoulders, hips
    steps = int(seconds * 240); frames = []
    severed = set(); sever = dict(spec.get("sever", {}))
    hits = []; lastHit = [-1.0] * 6; prevVz = [0.0] * 6
    for s in range(steps + 1):
        t = s / 240.0
        for j, (tS, kick) in list(sever.items()):   # dismemberment (Ragdollified's severed part) at the first hard hit
            if t >= tS and j not in severed:
                p.removeConstraint(cons[j]); severed.add(j)
                lv, av = p.getBaseVelocity(ids[j])
                p.resetBaseVelocity(ids[j], [lv[k] + toSim(kick)[k] for k in range(3)], [av[0] + 9, av[1] - 4, av[2] + 6])
        for (tp, spin) in spec.get("rolls", []):     # Sable's roll: a turn about the body's own long axis
            if abs(t - tp) < 0.5 / 240.0:
                _, q1 = p.getBasePositionAndOrientation(ids[1])
                ax = np.array(p.getMatrixFromQuaternion(q1)).reshape(3, 3)[:, 2]   # the torso's up (our y = bullet z)
                side = np.cross(ax, [0, 0, 1.0]); side = side / (np.linalg.norm(side) + 1e-6)
                c1 = np.array(p.getBasePositionAndOrientation(ids[1])[0])
                for j in range(6):   # the whole body turns as one: each part also gets w x r
                    lv, av = p.getBaseVelocity(ids[j])
                    r = np.array(p.getBasePositionAndOrientation(ids[j])[0]) - c1
                    v = np.array(lv) + side * 1.2 + [0, 0, 1.4] + np.cross(ax * spin, r)
                    p.resetBaseVelocity(ids[j], v.tolist(), (np.array(av) + ax * spin).tolist())
        for (tp, j, imp) in spec.get("impulses", []):
            if abs(t - tp) < 0.5 / 240.0:
                lv, av = p.getBaseVelocity(ids[j]); p.resetBaseVelocity(ids[j], [lv[k] + toSim(imp)[k] for k in range(3)], av)
        # soft joint limits + joint relax spring (Ragdollified's jointRelax*): torques on the pair
        _, qp = p.getBasePositionAndOrientation(ids[1])
        Rp = np.array(p.getMatrixFromQuaternion(qp)).reshape(3, 3)
        _, wp = p.getBaseVelocity(ids[1])
        for j in (0, 2, 3, 4, 5):
            if j in severed: continue
            _, qc = p.getBasePositionAndOrientation(ids[j])
            Rc = np.array(p.getMatrixFromQuaternion(qc)).reshape(3, 3)
            Rr = Rp.T @ Rc
            ang = math.acos(max(-1.0, min(1.0, (np.trace(Rr) - 1) / 2)))
            if ang > 1e-4:
                ax = np.array([Rr[2, 1] - Rr[1, 2], Rr[0, 2] - Rr[2, 0], Rr[1, 0] - Rr[0, 1]]) / (2 * math.sin(ang))
                axw = Rp @ ax
                k = spec.get("relax", 0.010) + (0.30 * (ang - LIM[j]) if ang > LIM[j] else 0.0)
                _, wc = p.getBaseVelocity(ids[j])
                dw = np.array(wc) - np.array(wp)
                tq = -axw * ang * k - dw * spec.get("jdamp", 0.0012)
                p.applyExternalTorque(ids[j], -1, tq.tolist(), p.WORLD_FRAME)
                p.applyExternalTorque(ids[1], -1, (-tq).tolist(), p.WORLD_FRAME)
        for j in range(6):   # a part landing hard: a thud (dust) where it hits
            pos, _ = p.getBasePositionAndOrientation(ids[j]); lv, _ = p.getBaseVelocity(ids[j])
            if prevVz[j] < -1.6 and lv[2] > prevVz[j] * 0.5 and pos[2] < 0.32 and t - lastHit[j] > 0.15:
                hits.append((-prevVz[j] * MASS[j] ** 0.5, t, pos[0], pos[1])); lastHit[j] = t
            prevVz[j] = lv[2]
        if s % int(240 / HZ) == 0:
            row = []
            for j in range(6):
                pos, q = p.getBasePositionAndOrientation(ids[j])
                pos = fromSim(pos)
                # bullet quat (x, y, z, w) in its axes -> ours: swap y and z and flip the handedness
                qx, qy, qz, qw = q
                row += [pos[0], pos[1], pos[2], -qx, -qz, -qy, qw, 0.0]
            frames.append(row)
        p.stepSimulation()
    p.disconnect(cid)
    hits.sort(reverse=True); hits = sorted(hits[:6], key=lambda h: h[1])
    hits += [(0.0, 99.0, 0.0, 0.0)] * (6 - len(hits))
    return len(frames), [x for r in frames for x in r], [(h[2], h[3], h[1], h[0]) for h in hits]

RAGDOLLS = [
    # Ragdollified: a melee kill from the front - launched back and up, flips, the right arm tears off on the first
    # hard landing, tumbles on
    dict(name="RD_MELEE", v=(0.3, 3.2, -3.6), w=(-6.0, 1.0, 0.5), whip={2: (0.8, 1.6, -2.5), 3: (-0.6, 1.2, -2.0), 0: (0, 1.0, -2.0)},
         sever={2: (0.84, (1.2, 2.6, -0.8))}),
    # Ragdollified: a side hit - spun round, cartwheels, skids
    dict(name="RD_SIDE", v=(5.0, 4.2, 0.6), w=(0.5, 8.0, -9.0), whip={3: (2.0, 1.0, 0.0), 5: (1.6, 0.0, 0.4)}),
    # Sable Ragdolls: the body goes limp where it stood - knees fold, it crumples forward and settles heavy
    dict(name="SB_SLUMP", v=(0.0, 0.0, 0.55), w=(1.2, 0.0, 0.0), whip={0: (0, 0, 0.6), 1: (0, 0, 0.35)}, relax=0.0035, jdamp=0.0025,
         angDamp=0.18, mu=0.95, e=0.05),
    # Sable Ragdolls: knocked onto its back, then rolled over (the ragdoll's WASD roll) and a last reach of the arm
    dict(name="SB_ROLL", v=(-0.8, 1.2, -1.6), w=(-3.0, 0.0, 1.5), relax=0.004, jdamp=0.0022, angDamp=0.10, mu=0.55, e=0.06,
         rolls=[(1.3, 26.0)], impulses=[(2.55, 3, (0.4, 2.6, 1.6))]),
]

def main():
    jar = sys.argv[1] if len(sys.argv) > 1 else None
    out = ["// Generated by tools/gen_death_tables.py - do not edit.", "//", "// Finishers clips (by akeresmods, used with permission),",
           "// ragdoll bakes (Ragdollified by raiiiden / Sable Ragdolls by Leonardoinc22, used with permission; simulated with Bullet).",
           "// All values are f16 pairs in uints; see death_scenes.hlsli for the readers.", ""]
    if not jar or len(sys.argv) < 3: raise SystemExit("usage: gen_death_tables.py <Finishers.jar> <SableRagdolls.jar>")
    sjar = zipfile.ZipFile(sys.argv[2])
    z = zipfile.ZipFile(jar)
    anims = {}
    for n in z.namelist():
        if n.startswith("assets/finishers/player_animation/") and n.endswith(".json"):
            anims.update(json.loads(z.read(n))["animations"])
    CLIPS = [("FN_FINISHER", "misc.finisher"), ("FN_STAGGER", "misc.stagger"), ("FN_TAUNT", "misc.finisher2"),
             ("FN_HEADBUTT", "quickfinisher4.headbutt"), ("FN_STARTLE", "quickfinisher4.victim_startle"),
             ("FN_KICK", "quickfinisher5.kick"), ("FN_PAIN", "victim.pain"), ("FN_ULT", "ultimate.attack")]
    allc = []; offs = []
    for tag, an in CLIPS:
        n, rows = clip(anims, an)
        offs.append((tag, len(allc), n))
        allc += pack(rows)
    efmeta = []
    efc, efmeta, alle, eo = [], [], [], []
    if len(sys.argv) > 3:   # Epic Fight's moves: their own block, after the ragdolls
        efc, efmeta = ef_clips(sys.argv[3])
        EF_META[:] = efmeta
        for name, flat, n in efc:
            eo.append((name, len(alle), n)); alle += pack(flat)
    out.append(f"// clips: per frame (20 Hz) 8 bones x 4 uints (rot xyz rad, offset xyz px, scale, 0); bones {BONES}")
    for tag, o, n in offs: out.append(f"static const uint {tag} = {o}u, {tag}_N = {n}u;")
    if efmeta:
        out.append(f"// Epic Fight moves: (clip offset, frames, weapon, victim distance blocks) and up to 6 hit times (s)")
        out.append("// Epic Fight clips: per frame 13 x 4 uints (centre xyz blocks, quat xyzw): 11 skin segments, Tool_R, Tool_L")
        out.append(f"static const uint4 kEFClip[{len(efmeta)}] = {{ " + ", ".join(f"uint4({o}u, {n}u, {m[1]}u, {int(round(m[3] * 100))}u)" for (nm, o, n), m in zip(eo, efmeta)) + " };")
        out.append(f"static const float kEFHit[{len(efmeta) * 6}] = {{ " + ", ".join(", ".join(f"{(m[2][i] if i < len(m[2]) else 99.0):.3f}" for i in range(6)) for m in efmeta) + " };")
        out.append(f"static const uint kEFHits[{len(efmeta)}] = {{ " + ", ".join(f"{len(m[2])}u" for m in efmeta) + " };")
    out.append("")
    allr = []; ro = []; allh = []
    for spec in RAGDOLLS:
        n, rows, hits = ragdoll(spec)
        ro.append((spec["name"], len(allr), n))
        allr += pack(rows); allh += hits
        print(spec["name"], "thuds", [(round(h[2], 2), round(h[3], 2)) for h in hits if h[2] < 90])
    out.append("// ragdolls: per frame (20 Hz) 6 parts x 4 uints (centre xyz blocks from the feet, rotation quat xyzw)")
    for tag, o, n in ro: out.append(f"static const uint {tag} = {o}u, {tag}_N = {n}u;")
    # the clips and ragdolls themselves are too big for shader constants (drivers keep ~64 KB there; past that they
    # spill and the compile and the frame both suffer): they go in a texture, t5, 256 uints a row - clips, then ragdolls
    data = allc + allr + alle
    rows = (len(data) + 255) // 256
    data += [0] * (rows * 256 - len(data))
    out.append(f"static const uint kRagBase = {len(allc)}u;      // gClips: the clips from 0, the ragdolls from here")
    out.append(f"static const uint kEFBase = {len(allc) + len(allr)}u;    // gClips: the Epic Fight clips from here")
    out.append(f"static const uint kClipRows = {rows}u;")
    hdr = ["// generated by tools/gen_death_tables.py - the death scenes' baked clips and ragdolls (texture t5, R32_UINT, 256 wide)",
           "#pragma once", "#include <cstdint>", f"constexpr unsigned kDeathClipW = 256, kDeathClipH = {rows};",
           f"alignas(16) static const uint32_t kDeathClipData[{len(data)}] = {{"]
    hdr += ["    " + ", ".join(f"0x{v:08x}u" for v in data[i:i + 12]) + "," for i in range(0, len(data), 12)]
    hdr.append("};")
    (ROOT / "src/DeathClipData.h").write_text("\n".join(hdr) + "\n")
    import array
    array.array("I", data).tofile(open(ROOT / "tools/worldfx_lab/death_clips.bin", "wb"))
    out.append("// ragdoll thuds: 6 per bake, (x, y = horizontal place in bake space; z = time s, 99 = none; w = strength)")
    out.append(f"static const float4 kRagHit[{len(allh)}] = {{")
    out += [f"    float4({h[0]:.3f}, {h[1]:.3f}, {h[2]:.3f}, {h[3]:.3f})," for h in allh]
    out.append("};")
    OUT.write_text("\n".join(out) + "\n")
    sounds(z, sjar, [h for h in allh])
    print(f"wrote {OUT.relative_to(ROOT)}: clips {len(allc) * 4} B, ragdolls {len(allr) * 4} B")


# ------------------------------------------------------------------ 4. Epic Fight
EF_MOVES = [   # (name, animation files played back to back, weapon)
    ("guillotine", ["skill/the_guillotine.json"], "axe"),
    ("heartpiercer", ["skill/heartpiercer.json"], "spear"),
    ("eviscerate", ["skill/eviscerate_first.json", "skill/eviscerate_second.json"], "dagger"),
    ("sweeping_edge", ["skill/sweeping_edge.json"], "sword"),
    ("dancing_edge", ["skill/dancing_edge.json"], "dual"),
    ("battojutsu", ["skill/battojutsu_dash.json"], "katana"),
    ("blade_rush", ["skill/blade_rush_try.json", "skill/blade_rush_combo1.json", "skill/blade_rush_execute.json"], "dagger"),
    ("wrathful_lightning", ["skill/wrathful_lighting.json"], "trident"),
    ("tsunami", ["skill/tsunami_reinforced.json"], "trident"),
    ("steel_whirlwind", ["skill/steel_whirlwind_charging.json", "skill/steel_whirlwind.json"], "greatsword"),
    ("demolition_leap", ["skill/demolition_leap_charge.json", "skill/demolition_leap.json"], "greatsword"),
    ("relentless_combo", ["skill/relentless_combo.json"], "fist"),
    ("rushing_tempo", ["skill/rushing_tempo1.json", "skill/rushing_tempo3.json"], "tachi"),
    ("grasping_spire", ["skill/grasping_spire_second.json"], "spear"),
    ("sharp_stab", ["skill/sharp_stab.json"], "longsword"),
    ("revelation", ["skill/revelation_twohand.json"], "longsword"),
    ("greatsword_slam", ["skill/greatsword_slam.json"], "greatsword"),
]
EF_WEAPON = {"sword": 0, "longsword": 1, "greatsword": 2, "katana": 3, "tachi": 3, "dagger": 4, "dual": 5, "spear": 6, "trident": 7, "axe": 8, "fist": 9}
EF_REACH = {0: 1.0, 1: 1.25, 2: 1.6, 3: 1.1, 4: 0.55, 5: 1.0, 6: 2.0, 7: 1.9, 8: 1.0, 9: 0.35}   # blade / head length, blocks

# EF_SEGS: the phantom drawn the way Epic Fight skins its biped - each of our 6 parts split where the mod's mesh is
# weighted (elbows, knees, chest/waist), each half carried rigidly by its joint: (our part, EF joint, clip lo, clip hi px)
EF_SEGS = [(0, "Head", -99, 99), (1, "Chest", -0.5, 99), (1, "Torso", -99, 0.5),
           (2, "Arm_R", -0.25, 99), (2, "Hand_R", -99, 0.25), (3, "Arm_L", -0.25, 99), (3, "Hand_L", -99, 0.25),
           (4, "Thigh_R", -0.25, 99), (4, "Leg_R", -99, 0.25), (5, "Thigh_L", -0.25, 99), (5, "Leg_L", -99, 0.25)]
KPC = [(0, 28, 0), (0, 18, 0), (-6, 18, 0), (6, 18, 0), (-2, 6, 0), (2, 6, 0)]   # our parts' rest centres, px
# each weapon's blade in the Tool joint's frame (ours: faces +z), from Epic Fight's own trail definitions (item_skins/*.json)
EF_BLADE = {0: ((0, 0.1, 0.1), (0, -0.05, 0.95)), 1: ((0, 0, 0.2), (0, 0.2, 1.8)), 2: ((0, 0, 0.2), (0, -0.24, 2.2)),
            3: ((0, 0, 0.2), (0, -0.22, 1.25)), 4: ((0, 0, 0.2), (0, 0.1, 1.0)), 5: ((0, 0.1, 0.1), (0, -0.05, 0.95)),
            6: ((0, 0.2, 0.9), (0, 0.25, 2.0)), 7: ((0, 0, 0.2), (0, -0.22, 1.25)), 8: ((0, -0.15, 0.35), (0, -0.15, 0.7)),
            9: ((0, 0, 0), (0, 0, 0.15))}
# Epic Fight's attack phases, read from the mod's own Animations class (javap): every animation path's (start, antic,
# preDelay, contact, recovery) - its hits land between preDelay and contact. Needs a JDK's javap; without one, {}
def ef_phases(jar):
    import re, subprocess, os, tempfile, shutil
    if not shutil.which("javap"): return {}
    d = tempfile.mkdtemp()
    subprocess.run(["unzip", "-o", "-q", jar, "yesman/epicfight/gameasset/Animations.class", "-d", d], check=True)
    cls = os.path.join(d, "yesman/epicfight/gameasset/Animations.class")
    c = subprocess.run(["javap", "-p", "-c", "-constants", cls], capture_output=True, text=True).stdout
    v = subprocess.run(["javap", "-p", "-v", cls], capture_output=True, text=True).stdout
    bs = {}   # bootstrap index -> lambda name
    bpart = v[v.index("BootstrapMethods:"):]
    for m in re.finditer(r"^\s+(\d+): #\d+ REF_invokeStatic java/lang/invoke/LambdaMetafactory.*?\n(?:.*\n){2}\s+#\d+ REF_invokeStatic yesman/epicfight/gameasset/Animations\.(lambda\$\w+\$\d+)", bpart, re.M):
        bs[int(m.group(1))] = m.group(2)
    path2lam, last = {}, None
    for line in c.splitlines():
        m = re.search(r"// String (biped/[\w/]+)", line)
        if m: last = m.group(1); continue
        m = re.search(r"invokedynamic #\d+,\s+0\s+// InvokeDynamic #(\d+):apply", line)
        if m and last: path2lam[last] = bs.get(int(m.group(1))); last = None
    # lambda bodies
    bodies, cur = {}, None
    for line in c.splitlines():
        m = re.match(r"\s+(?:private |public )?static .*?(lambda\$\w+\$\d+)\(", line)
        if m: cur = m.group(1); bodies[cur] = []; continue
        if cur: bodies[cur].append(line)
    out = {}
    for p, lam in path2lam.items():
        fl, ph, speed = [], [], None
        for line in bodies.get(lam, []):
            m = re.search(r"// float (-?[\d.E-]+)f", line)
            if m: fl.append(float(m.group(1))); continue
            m = re.search(r"fconst_(\d)", line)
            if m: fl.append(float(m.group(1))); continue
            if "AttackAnimation$Phase.\"<init>\":(FFFFF" in line: ph.append(fl[-5:]); fl = []
            elif "AttackAnimation.\"<init>\":(FFFFF" in line: ph.append([0.0] + fl[-4:]); fl = []
            if "PLAY_SPEED_MODIFIER" in line or "BASIS_ATTACK_SPEED" in line: speed = "next"
            elif speed == "next" and fl: speed = fl[-1]
        out[p] = ph
    return out

def ef_clips(jar):
    """Epic Fight's moves, posed exactly as its JsonAssetLoader + Armature do (see the comments in efPose), baked per
    frame (20 Hz) as 13 entries of (centre xyz blocks, rotation quat): the 11 skin segments, then Tool_R and Tool_L.
    Positions are in the attacker's frame (feet at the origin at the start, facing +z), root motion (Coord) included."""
    from scipy.spatial.transform import Rotation as Rot, Slerp
    z = zipfile.ZipFile(jar)
    A = "assets/epicfight/animmodels/"
    C = np.array([[1, 0, 0, 0], [0, 0, 1, 0], [0, -1, 0, 0], [0, 0, 0, 1.]])   # BLENDER_TO_MINECRAFT_COORD: rotX(-90)
    K = np.diag([-1.0, 1, -1, 1])                                            # the mod's model faces -z; ours +z
    arm = json.loads(z.read(A + "entity/biped.json"))["armature"]
    bind, parent, order = {}, {}, []
    def walk(n, p, root):   # the armature: Root's bind gets the coordinate correction (JsonAssetLoader.getJoint)
        M = np.array(n["transform"], float).reshape(4, 4)
        bind[n["name"]] = C @ M if root else M; parent[n["name"]] = p; order.append(n["name"])
        for c in n.get("children", []): walk(c, n["name"], False)
    for r in arm["hierarchy"]: walk(r, None, True)
    def worldOf(j, loc):
        M = loc(j); q = parent[j]
        while q: M = loc(q) @ M; q = parent[q]
        return M
    bindW = {j: K @ worldOf(j, lambda q: bind[q]) @ K for j in order}
    def orth(m):
        u, _, vt = np.linalg.svd(m); r = u @ vt
        return r if np.linalg.det(r) > 0 else u @ np.diag([1, 1, -1]) @ vt
    def load(f):   # the matrices are row-major (load + transpose); the first Coord-or-joint entry gets the correction
        out, first = {}, True
        for e in json.loads(z.read(A + "animations/biped/" + f))["animation"]:
            nm = e["name"]
            if nm != "Coord" and nm not in bind: continue
            ms = [np.array(m, float).reshape(4, 4) for m in e["transform"]]
            if first: ms = [C @ m for m in ms]
            first = False
            out[nm] = (np.array(e["time"], float), Rot.from_matrix(np.array([orth(m[:3, :3]) for m in ms])), np.array([m[:3, 3] for m in ms]))
        return out
    def sample(an, nm, t):
        ts, rs, tr = an[nm]; M = np.eye(4)
        if len(ts) == 1: M[:3, :3] = rs.as_matrix()[0]; M[:3, 3] = tr[0]; return M
        t = min(max(t, ts[0]), ts[-1])
        i = int(np.clip(np.searchsorted(ts, t, side="right") - 1, 0, len(ts) - 2)); u = (t - ts[i]) / max(ts[i + 1] - ts[i], 1e-9)
        M[:3, :3] = Slerp([0, 1], rs[[i, i + 1]])([u]).as_matrix()[0]; M[:3, 3] = tr[i] * (1 - u) + tr[i + 1] * u
        return M
    coordOf = lambda an, t: (K @ sample(an, "Coord", t) @ K)[:3, 3] if "Coord" in an else np.zeros(3)
    clips, meta = [], []
    PH = ef_phases(jar)
    for name, files, weapon in EF_MOVES:
        anims = [load(f) for f in files]
        lens = [max(v[0].max() for v in a.values()) for a in anims]
        T = sum(lens); n = int(math.ceil(T * HZ)) + 1
        absC = any(np.linalg.norm(coordOf(a, 0.0)) > 1.5 for a in anims)
        wi = EF_WEAPON[weapon]; b0, b1 = (np.array(v, float) for v in EF_BLADE[wi])
        frames, tipS, tipW, mot = [], [], [], []
        for f in range(n):
            t = f / HZ; k = 0; tl = t
            while k < len(anims) - 1 and tl > lens[k]: tl -= lens[k]; k += 1
            an = anims[k]
            # root motion: the entity is moved by Coord's change (ActionAnimation), file after file - except the moves that
            # trace their target (blade rush, the slam): their Coord is the path relative to the target, used as is
            if absC: mv = coordOf(an, tl) - coordOf(anims[0], 0.0)
            else: mv = sum((coordOf(anims[q], lens[q]) - coordOf(anims[q], 0.0) for q in range(k)), np.zeros(3)) + coordOf(an, tl) - coordOf(an, 0.0)
            loc = lambda j: sample(an, j, tl) if j in an else bind[j]
            W = {j: K @ worldOf(j, loc) @ K for j in order}
            ent = []
            for (pj, jn, lo, hi) in EF_SEGS:
                D = W[jn] @ np.linalg.inv(bindW[jn])                   # how far this joint has moved from the bind pose
                c = D @ np.array([KPC[pj][0] * DPX, KPC[pj][1] * DPX, KPC[pj][2] * DPX, 1.0])
                ent.append((c[:3], Rot.from_matrix(orth(D[:3, :3])).as_quat()))
            for jn in ("Tool_R", "Tool_L"):
                ent.append((W[jn][:3, 3], Rot.from_matrix(orth(W[jn][:3, :3])).as_quat()))
            frames.append(ent); mot.append(mv)
            Rt = W["Tool_R"][:3, :3]
            tipS.append(W["Tool_R"][:3, 3] + Rt @ b1); tipW.append(mv + tipS[-1])
        mot = np.array(mot); mot[:, 1] -= min(mot[:, 1].min(), 0.0)   # never below the ground it started on
        # keep quaternion signs continuous so the shader's nlerp never takes the long way
        for e in range(13):
            for f in range(1, n):
                if np.dot(frames[f][e][1], frames[f - 1][e][1]) < 0: frames[f][e] = (frames[f][e][0], -frames[f][e][1])
        flat = []
        for f in range(n):
            for e in range(13):
                p, q = frames[f][e]
                p = p + mot[f]
                flat += [p[0], p[1], p[2], q[0], q[1], q[2], q[3], 0.0]
        # the hits: the blade tip's speed peaks (the swing alone, not the body's travel); the victim stands at the last
        tipS = np.array(tipS); tipW = np.array(tipW) + np.array([0, 0, 0])
        tipW[:, 1] -= 0
        sp = np.r_[0, np.linalg.norm(np.diff(tipS, axis=0), axis=1) * HZ]
        sp_s = np.convolve(sp, [0.25, 0.5, 0.25], mode="same")
        cand = [i for i in range(1, n - 1) if sp_s[i] >= sp_s[i - 1] and sp_s[i] >= sp_s[i + 1] and sp_s[i] > 0.55 * sp_s.max()]
        hits = []
        for i in sorted(cand, key=lambda i: -sp_s[i]):
            if all(abs(i - j) >= 4 for j in hits): hits.append(i)
        hits = sorted(hits)[:6]
        # better: the mod's own hit windows, where it has them (a file without phases keeps its speed peaks)
        exact, t0f = [], 0.0
        for q, f in enumerate(files):
            phs = PH.get("biped/" + f[:-5])
            if phs: exact += [int(round((t0f + 0.5 * (ph[2] + ph[3])) * HZ)) for ph in phs]
            else: exact += [i for i in hits if t0f * HZ <= i < (t0f + lens[q]) * HZ]
            t0f += lens[q]
        if exact: hits = sorted(set(min(i, n - 1) for i in exact))[-6:]
        # the victim stands where the blows reach: the blade tip's farthest forward around any hit (a spin's tip can
        # point backwards at the exact frame), never closer than arm's length in front of the attacker's own body
        near = [i for h in hits for i in range(max(h - 2, 0), min(h + 3, n))] or [n - 1]
        bodyZ = max(float(mot[i][2] + frames[i][2][0][2]) for i in near)
        fwd = max(max(float(mot[i][2] + tipS[i][2]) for i in near), bodyZ + 0.75)
        clips.append((name, flat, n))
        meta.append((name, wi, [h / HZ for h in hits], min(max(fwd, 0.6), 14.0), n))
        print(f"EF {name}: {T:.2f}s, hits {[round(h / HZ, 2) for h in hits]}, victim at {fwd:.2f}, travel {np.round(mot[-1], 2).tolist()}")
    return clips, meta

# ------------------------------------------------------------------ 3. sounds
SND = [("victim", "fin", "assets/finishers/sounds/finisher/victim_sound_1.ogg"),
       ("swing", "fin", "assets/finishers/sounds/finisher/swordswing.ogg"),
       ("punchPower", "fin", "assets/finishers/sounds/finisher/quick_punch_power.ogg"),
       ("punch02", "fin", "assets/finishers/sounds/finisher/quick_punch_02.ogg"),
       ("punch03", "fin", "assets/finishers/sounds/finisher/quick_punch_03.ogg"),
       ("punchHard", "fin", "assets/finishers/sounds/finisher/quick_punch_hard.ogg"),
       ("impact", "sab", "assets/sable_player_ragdoll/sounds/impact.ogg"),
       ("smallImpact", "sab", "assets/sable_player_ragdoll/sounds/small_impact.ogg")]
PUNCH = -1   # Finishers' "finisher.quick_punch": one of its four, picked per kill
# (scene, t s, sound, volume, pitch); scene times as in death_scenes_ext.hlsli; a negative volume = stop that sound
def cues(hits):
    c = [(7, 0.00, "victim", 0.9, 1.0), (7, 0.10, "punchPower", 0.55, 0.55),     # the cry; the phantom lands
         (7, 3.30, "swing", 1.0, 1.0), (7, 3.60, "punchHard", 0.85, 0.85), (7, 4.00, "victim", -1, 1),
         # Shield Counter at 1.1x (tick / 22): the mod's sweep (tick 29) and chain hit (51) - ours from its own
         # files - and the body landing after the kill tick
         (8, 0.05, "punchPower", 0.7, 0.8), (8, 29 / 22, "swing", 0.75, 0.9), (8, 51 / 22, "punchPower", 0.9, 0.65),
         (8, 91 / 22, "smallImpact", 0.7, 1.0),
         (9, 0.65, PUNCH, 1.0, 0.92), (10, 0.70, PUNCH, 1.0, 1.0)]
    # Ultimate (Kamehameha): a low whoosh as it charges, the boom as it fires, the beam's roar, the hit, the landings
    tb, vb = 0.4 + 0.75, 12.0
    th = tb + 2.55 / vb; tl = th + 0.15
    c += [(11, 0.45, "swing", 0.6, 0.45), (11, tb - 0.02, "punchHard", 1.0, 0.5), (11, tb + 0.05, "punchPower", 0.8, 0.5),
          (11, tb + 0.35, "punchPower", 0.55, 0.45), (11, tb + 0.75, "punchPower", 0.5, 0.42), (11, th, "punchHard", 0.95, 0.75),
          (11, tb + 8.5 / vb, "punchHard", 1.0, 0.45)]   # the dome at the wave's end
    side = sorted([h for h in hits[6:12] if h[2] < 1.8], key=lambda h: -h[3])[:2]
    for x, z, t, w in side: c.append((11, tl + t, "impact" if w > 1.0 else "smallImpact", min(1.0, 0.45 + 0.4 * w), 1.0))
    for sc in (12, 13): c.append((sc, 0.0, "punchPower", 0.8, 1.0))                 # Ragdollified: the killing blow
    # Finishers 2 (codes 16+): its own sounds are vanilla Minecraft's, so these are scored with the oggs we carry -
    # a whoosh for things that move, a punch for things that hit, the impacts for things that land or break
    F2 = {0: [(1.5, "smallImpact", .5, 1.6)], 1: [(1.0, "impact", .9, .8), (1.05, "smallImpact", .7, .7)],
          2: [(0.35, "swing", .7, .5), (2.2, "punchHard", .9, 1.3)], 3: [(0.2, "swing", .6, .4), (2.0, "punchPower", .6, .5)],
          4: [(0.15, "punchHard", 1, .6), (0.5, "punchHard", .9, .7)], 5: [(1.55, "smallImpact", 1, 1.5), (1.57, "impact", .6, 1.4)],
          6: [(0.1, "punch02", .5, 1.8), (1.2, "punch03", .5, 1.6)], 7: [(2.4, "punchPower", .8, 1.6)], 8: [(0.9, "swing", .5, .6)],
          9: [(1.2, "swing", .6, 1.4)], 10: [(1.75, "swing", .5, 1.2)], 11: [(0.2, "swing", .6, .5), (1.7, "smallImpact", .5, .7)],
          12: [(0.6, "punchHard", .7, .4), (2.0, "smallImpact", .8, 1.3)], 13: [(1.0, "punch02", .6, 1.9)], 14: [(0.15, "swing", .7, .45)],
          15: [(1.3, "smallImpact", .6, .6)], 16: [(0.3, "swing", .7, .4), (1.0, "punchHard", 1, .5), (1.02, "impact", 1, .6)],
          17: [(0.3, "punchHard", .8, 1.4), (0.6, "swing", .6, .5)], 18: [(0.7, "punchHard", 1, .55), (0.72, "impact", 1, .7), (2.2, "smallImpact", .6, 1.0)],
          19: [(0.15, "punchPower", 1, 1.0)], 20: [(0.1, "swing", .7, .4), (1.6, "swing", .6, .6)], 21: [(1.9, "smallImpact", .7, .7)],
          22: [(1.8, "punch03", .7, 1.7)], 23: [(1.7, "punchPower", .8, 1.5)], 24: [(2.2, "punch02", .7, 1.4)], 25: [(0.5, "swing", .5, .4)],
          26: [(2.7, "punch02", .6, 1.9)], 27: [(0.5, "swing", .6, .45)], 28: [(0.5, "swing", .5, .7)],
          29: [(0.5, "punchPower", .6, 1.3), (0.55, "swing", .6, 1.2)], 30: [(1.0, "swing", .5, 1.5)], 31: [(2.4, "punch03", .5, 1.5)],
          32: [(0.8 + i * 0.35, "punch02" if i % 2 else "punch03", .8, 1.0) for i in range(6)],
          33: [(0.1, "punchHard", .7, 1.2), (2.0, "smallImpact", 1, 1.6)], 34: [(0.8, "punchHard", 1, .5), (0.82, "impact", 1, .6), (2.3, "smallImpact", .8, 1.4)],
          35: [(1.0, "smallImpact", .4, 1.8), (2.3, "smallImpact", 1, 1.7)], 36: [(2.2, "punchPower", .7, 1.4)], 37: [(1.3, "swing", .4, 1.6)],
          38: [(0.6, "swing", .6, .5), (2.6, "swing", .7, 1.3)], 39: [(0.55, "punchPower", .6, 1.2), (1.6, "swing", .8, 1.1), (1.68, "swing", .7, 1.2)],
          40: [(1.3, "swing", 1, .5), (1.32, "punchHard", 1, .6)],
          41: [(0.7 + i * 0.15, "smallImpact", .4, 1.5) for i in range(12)] + [(3.0, "impact", .8, 1.4)],
          42: [(1.2, "swing", .6, .4), (2.0, "punchHard", 1, .45), (2.02, "impact", 1, .5)], 43: [(1.4, "swing", .6, .35), (3.0, "punchHard", .8, .4)],
          44: [(0.9, "punchHard", .8, .5), (2.9, "punchHard", 1, .45), (2.92, "impact", 1, .5)], 45: [(2.2, "swing", .7, .4), (2.9, "punchHard", .8, .5)],
          46: [(0.6 + i * 0.3, "swing", .4, 1.6) for i in range(7)] + [(3.0, "punchHard", .9, .9)],
          47: [(x, "punch02" if i % 2 else "punch03", .8, 1.0) for i, x in enumerate((0.5, 0.8, 1.1, 1.4, 1.7, 2.0))] + [(2.4, "punchHard", 1, .8)],
          48: [(0.25, "punchPower", .8, 2.0), (3.35, "punchHard", 1, .4)]}
    for k, lst in F2.items():
        for (tt, snd, v, pt) in lst: c.append((16 + k, tt, snd, v, pt))
    # Epic Fight (codes 65+): a whoosh just before each hit, a punch on it, the heaviest on the last
    for k, m in enumerate(EF_META):
        hits = m[2]
        for i, th in enumerate(hits):
            last = i == len(hits) - 1
            c.append((65 + k, max(th + 0.35 - 0.08, 0.0), "swing", .55, 1.0 + 0.1 * (k % 3)))
            c.append((65 + k, th + 0.35, "punchHard" if last else ("punch02" if i % 2 else "punch03"), .9 if last else .7, .8 if last else 1.05))
        if k in (0, 10, 16) and hits: c.append((65 + k, hits[-1] + 0.37, "impact", 1.0, .6))
    return c
EF_META = []

def sounds(z, sjar, hits):
    out = ["// Generated by tools/gen_death_tables.py - do not edit.", "// The death scenes' sounds: Finishers (by akeresmods) and Sable Ragdolls (by Leonardoinc22), used",
           "// with permission; the .ogg files as the mods ship them, and when each one plays (src/DeathSounds.cpp).",
           "#pragma once", "#include <cstdint>", "", "namespace rf::deathsfx {"]
    names = [n for n, _, _ in SND]
    for n, src, path in SND:
        b = (z if src == "fin" else sjar).read(path)
        rows = [", ".join(f"0x{x:02x}" for x in b[i:i + 24]) for i in range(0, len(b), 24)]
        out.append(f"inline constexpr uint8_t k_{n}[{len(b)}] = {{\n    " + ",\n    ".join(rows) + "\n};")
    out.append(f"struct Sound {{ uint8_t const* ogg; uint32_t size; }};")
    out.append("inline constexpr Sound kSounds[] = { " + ", ".join(f"{{k_{n}, sizeof(k_{n})}}" for n in names) + " };")
    out.append(f"inline constexpr int kPunch = -1;   // one of {names.index('punchPower')}, {names.index('punch02')}, {names.index('punch03')}, {names.index('punchHard')}")
    out.append(f"inline constexpr int kPunchSet[4] = {{ {names.index('punchPower')}, {names.index('punch02')}, {names.index('punch03')}, {names.index('punchHard')} }};")
    out.append("struct Cue { uint8_t scene; float t; int8_t sound; float vol, pitch; };   // vol < 0: stop that sound")
    cl = list(cues(hits))
    # the ragdoll thuds: scene 12/13 = Ragdollified melee / side, 14/15 = Sable slump / roll (the bakes' landings)
    for b, sc in enumerate((12, 13, 14, 15)):
        kept = []   # landings closer than 0.15 s are one thud (the loudest)
        for hh in sorted([h for h in hits[b * 6:(b + 1) * 6] if h[2] < 50], key=lambda h: -h[3]):
            if all(abs(hh[2] - k[2]) > 0.15 for k in kept): kept.append(hh)
        for x, zz, t, w in kept:
            big = w > (1.0 if sc < 14 else 0.75)
            cl.append((sc, t, "impact" if big else "smallImpact", min(1.0, 0.45 + 0.4 * w), 0.9 + 0.2 * ((x * 7.3) % 1.0)))
    cl.sort(key=lambda c: (c[0], c[1]))
    out.append(f"inline constexpr Cue kCues[{len(cl)}] = {{")
    for sc, t, snd, v, pt in cl:
        si = snd if isinstance(snd, int) else names.index(snd)
        out.append(f"    {{{sc}, {t:.3f}f, {si}, {v:.3f}f, {pt:.3f}f}},")
    out.append("};")
    out.append("} // namespace rf::deathsfx")
    (ROOT / "src/DeathSoundsData.h").write_text("\n".join(out) + "\n")
    import json as _j
    (ROOT / "tools/worldfx_lab/death_cues.json").write_text(_j.dumps({"sounds": [p for _, _, p in SND], "cues": [[c[0], c[1], c[2] if isinstance(c[2], int) else names.index(c[2]), c[3], c[4]] for c in cl]}, indent=0))
    print(f"wrote src/DeathSoundsData.h: {len(cl)} cues")

if __name__ == "__main__":
    main()
