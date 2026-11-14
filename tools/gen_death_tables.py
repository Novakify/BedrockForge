#!/usr/bin/env python3
"""Death scenes 8-14: bake the tables shaders/death_tables.hlsli is made of.

  python3 tools/gen_death_tables.py <Finishers.jar>

1. Finishers (by akeresmods, used with the author's permission): its Blockbench / player-animation clips are sampled
   at 20 Hz (the game's tick) into per-bone rotation + offset tables, so the phantom and the victim move exactly as the
   mod's own animations do (its easings included).
2. Ragdolls (Ragdollified by raiiiden, Sable Ragdolls by Leonardoinc22, used with permission): each mod's ragdoll is
   simulated offline with Bullet (pybullet - Ragdollified itself runs JBullet) on a six-box Minecraft body, and the
   result is baked at 20 Hz (part centre + rotation). The shader turns each bake by the kill's random yaw and may
   mirror it, so one bake reads as several different falls.
Everything is packed as halves (f16) in uint arrays to keep the shader's constant data small.
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
        for j, fn in spec.get("pushes", []):         # Sable: the limp body rolled / shoved now and then
            pass
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
    dict(name="SB_ROLL", v=(-0.8, 1.2, -1.6), w=(-3.0, 0.0, 1.5), relax=0.004, jdamp=0.0022, angDamp=0.14, mu=0.9, e=0.06,
         impulses=[(1.55, 1, (4.2, 2.2, 0.0)), (1.55, 2, (3.2, 2.0, 0)), (1.55, 4, (3.0, 1.0, 0)), (2.45, 3, (0.4, 2.6, 1.6))]),
]

def main():
    jar = sys.argv[1] if len(sys.argv) > 1 else None
    out = ["// Generated by tools/gen_death_tables.py - do not edit.", "//", "// Finishers clips (by akeresmods, used with permission),",
           "// ragdoll bakes (Ragdollified by raiiiden / Sable Ragdolls by Leonardoinc22, used with permission; simulated with Bullet).",
           "// All values are f16 pairs in uints; see death_scenes.hlsli for the readers.", ""]
    if not jar: raise SystemExit("usage: gen_death_tables.py <Finishers.jar>")
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
    out.append(f"// clips: per frame (20 Hz) 8 bones x 4 uints (rot xyz rad, offset xyz px, scale, 0); bones {BONES}")
    for tag, o, n in offs: out.append(f"static const uint {tag} = {o}u, {tag}_N = {n}u;")
    out.append(f"static const uint kClip[{len(allc)}] = {{")
    out += ["    " + ", ".join(f"0x{v:08x}u" for v in allc[i:i + 12]) + "," for i in range(0, len(allc), 12)]
    out.append("};"); out.append("")
    allr = []; ro = []; allh = []
    for spec in RAGDOLLS:
        n, rows, hits = ragdoll(spec)
        ro.append((spec["name"], len(allr), n))
        allr += pack(rows); allh += hits
        print(spec["name"], "thuds", [(round(h[2], 2), round(h[3], 2)) for h in hits if h[2] < 90])
    out.append("// ragdolls: per frame (20 Hz) 6 parts x 4 uints (centre xyz blocks from the feet, rotation quat xyzw)")
    for tag, o, n in ro: out.append(f"static const uint {tag} = {o}u, {tag}_N = {n}u;")
    out.append(f"static const uint kRag[{len(allr)}] = {{")
    out += ["    " + ", ".join(f"0x{v:08x}u" for v in allr[i:i + 12]) + "," for i in range(0, len(allr), 12)]
    out.append("};")
    out.append("// ragdoll thuds: 6 per bake, (x, y = horizontal place in bake space; z = time s, 99 = none; w = strength)")
    out.append(f"static const float4 kRagHit[{len(allh)}] = {{")
    out += [f"    float4({h[0]:.3f}, {h[1]:.3f}, {h[2]:.3f}, {h[3]:.3f})," for h in allh]
    out.append("};")
    OUT.write_text("\n".join(out) + "\n")
    print(f"wrote {OUT.relative_to(ROOT)}: clips {len(allc) * 4} B, ragdolls {len(allr) * 4} B")

if __name__ == "__main__":
    main()
