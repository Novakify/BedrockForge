"""Knife animations, effects and sounds for BF Cosmetics (by Novak).

Every knife has its own: draw flourish, two inspect tricks (the inspect key / idle tricks alternate them), two attack
animations (they alternate on every click), slash arcs off the blade tip, sparks + a cut on confirmed hits, and our
own synthesised sounds (sounds.py). Timing comes from Bedrock Forge (src/client/Cosmetics.cpp: public variables
v.bfkdraw / bfkinsp / bfkinsn / bfkatk / bfkatn / bfkhit / bfkhitn / bfkspd / bfkopt), so they follow your real
clicks and hits; the Knives settings mirror Purxiy's (idle tricks + interval, speed, draw, slash arcs, hit effects,
sounds).

Bone frame (tools/Cosmetics/knives.py): item along +y, flat facing +-z. bfk_pose turns the whole knife about the grip;
bfk_spin spins it (the karambit about its finger ring); the balisong's handles bfb_safe / bfb_bite swing on their
pivot pins (180 = folded over the blade). Rotations in degrees. Poses are first guesses until tuned on real frames.
"""
from __future__ import annotations

KNIVES = ("karambit", "balisong", "dagger")
FP_SCALE = {"karambit": 0.98, "balisong": 0.72, "dagger": 0.78}   # first-person size (bfk_fix)
# third person: the knives were ~70% of a vanilla sword in the hand (Fernando: "karambit way too small"); the karambit
# is short and curled (14 px tip to ring vs 17-18 for the others), so it gets the biggest boost to read the same size
TP_SCALE = {"karambit": 1.30, "balisong": 1.10, "dagger": 1.10}
# the effect colour Bedrock Forge picked (random per swing, or the fixed one) handed to the particle emitter
FX_SCRIPT = "v.cr = v.bfk_cr; v.cg = v.bfk_cg; v.cb = v.bfk_cb;"
# slash styles (Cosmetics.cpp kMatchedSlash: karambit crescent, balisong trail, dagger thrust streak)
SLASH_STYLES = ("trail", "crescent", "streak")
SPEED = "q.anim_time + q.delta_time * v.bfk_spd"


def keys(points):
    """[(t, [x, y, z]), ...] -> smooth (Catmull-Rom) keyframes"""
    out = {}
    for t, v in points:
        out[f"{t:.3f}"] = {"post": v if isinstance(v, list) else [v, v, v], "lerp_mode": "catmullrom"}
    return out


def anim(length, bones, sounds=None, particles=None):
    # NB: no empty "bones": {} - in game one such clip made Bedrock drop its whole animation file (2026-11-18)
    a = {"animation_length": length, "anim_time_update": SPEED}
    if bones:
        a["bones"] = {bone: {k: keys(v) for k, v in ch.items()} for bone, ch in bones.items()}
    def timeline(events, make):
        out = {}
        for t, e in events:
            out.setdefault(f"{t:.3f}", []).append(make(e))
        return {t: v[0] if len(v) == 1 else v for t, v in out.items()}
    if sounds:
        a["sound_effects"] = timeline(sounds, lambda e: {"effect": e})
    if particles:
        a["particle_effects"] = timeline(particles, lambda e: {"effect": e, "locator": "tip", "pre_effect_script": FX_SCRIPT})
    return a


Z = [0, 0, 0]


def arc_trail(t0, t1, n=6):
    return [(t0 + (t1 - t0) * i / (n - 1), "arc") for i in range(n)]


# ------------------------------------------------------------------ per knife: (bones anim, sounds, particles, length)
def karambit():
    A = {}
    # draw: the knife comes up spinning round the index finger, one and a half turns, snaps into the grip
    A["draw"] = (0.75, {
        "bfk_spin": {"rotation": [(0, [0, 0, -540]), (0.30, [0, 0, -150]), (0.52, [0, 0, 12]), (0.64, [0, 0, -3]), (0.75, Z)]},
        "bfk_pose": {"position": [(0, [0, -5, 0]), (0.22, [0, 0.6, 0]), (0.40, Z), (0.75, Z)],
                     "rotation": [(0, [25, 0, 0]), (0.3, [5, 0, 0]), (0.75, Z)]}},
        [(0.0, "draw"), (0.06, "ring_spin"), (0.50, "click")], [])
    # inspect 1 - ring roll: lift and turn the Damascus to you, two turns round the ring, catch, settle
    A["insp1"] = (2.2, {
        "bfk_pose": {"rotation": [(0, Z), (0.3, [-22, 38, 18]), (1.7, [-22, 38, 18]), (2.0, [2, -3, -2]), (2.2, Z)],
                     "position": [(0, Z), (0.3, [0.8, 1.6, -0.8]), (1.7, [0.8, 1.6, -0.8]), (2.2, Z)]},
        "bfk_spin": {"rotation": [(0, Z), (0.38, Z), (0.80, [0, 0, -300]), (1.15, [0, 0, -700]), (1.28, [0, 0, -728]), (1.4, [0, 0, -720]), (2.2, [0, 0, -720])]}},
        [(0.42, "ring_spin"), (0.82, "ring_spin"), (1.27, "click")], [])
    # inspect 2 - reverse-grip flip: half a turn into reverse grip, show both flats, half a turn back
    A["insp2"] = (2.0, {
        "bfk_spin": {"rotation": [(0, Z), (0.25, [0, 0, -190]), (0.34, [0, 0, -178]), (1.35, [0, 0, -180]), (1.65, [0, 0, -365]), (1.76, [0, 0, -358]), (2.0, [0, 0, -360])]},
        "bfk_pose": {"rotation": [(0, Z), (0.45, [0, 75, 12]), (0.95, [0, -60, 12]), (1.3, [0, 0, 6]), (2.0, Z)]}},
        [(0.3, "click"), (1.7, "click")], [])
    # attacks: hook slash (the claw leads, the ring rolls into it) / backhand rip
    A["swing1"] = (0.36, {
        "bfk_pose": {"rotation": [(0, Z), (0.035, [8, 0, -16]), (0.09, [-30, 0, 78]), (0.19, [12, 0, -24]), (0.27, [-3, 0, 6]), (0.36, Z)],
                     "position": [(0, Z), (0.09, [-1.2, 1.0, -1.0]), (0.36, Z)]},
        "bfk_spin": {"rotation": [(0, Z), (0.05, [0, 0, -6]), (0.11, [0, 0, 24]), (0.22, [0, 0, -5]), (0.36, Z)]}},
        [(0.0, "swish")], arc_trail(0.05, 0.17))
    A["swish2_"] = None
    A["swing2"] = (0.36, {
        "bfk_pose": {"rotation": [(0, Z), (0.035, [-8, 0, 14]), (0.1, [36, 0, -70]), (0.2, [-10, 0, 20]), (0.28, [2, 0, -4]), (0.36, Z)],
                     "position": [(0, Z), (0.1, [1.6, 1.0, -0.8]), (0.36, Z)]},
        "bfk_spin": {"rotation": [(0, Z), (0.11, [0, 0, -22]), (0.22, [0, 0, 4]), (0.36, Z)]}},
        [(0.0, "swish")], arc_trail(0.05, 0.18))
    del A["swish2_"]
    # inspect 3 - ring spinner: dropped round the finger ring, three fast turns, caught back into the grip
    A["insp3"] = (1.9, {
        "bfk_pose": {"position": [(0, Z), (0.2, [0, -1.2, 0]), (1.25, [0, -1.2, 0]), (1.5, [0, 0.4, 0]), (1.9, Z)],
                     "rotation": [(0, Z), (0.2, [10, 0, -8]), (1.3, [10, 0, -8]), (1.9, Z)]},
        "bfk_spin": {"rotation": [(0, Z), (0.22, Z), (0.55, [0, 0, -400]), (0.9, [0, 0, -820]), (1.2, [0, 0, -1080]), (1.3, [0, 0, -1088]), (1.42, [0, 0, -1080]), (1.9, [0, 0, -1080])]}},
        [(0.25, "ring_spin"), (0.6, "ring_spin"), (0.95, "ring_spin"), (1.28, "click")], [])
    # inspect 4 - look at it: brought close and turned slowly under the light so the Damascus catches it, both flats
    A["insp4"] = (2.6, {
        "bfk_pose": {"rotation": [(0, Z), (0.45, [-28, 50, 14]), (1.25, [-30, -20, 10]), (1.95, [-24, 55, 16]), (2.35, [3, -4, -2]), (2.6, Z)],
                     "position": [(0, Z), (0.45, [1.6, 2.2, -2.4]), (1.95, [1.4, 2.3, -2.4]), (2.6, Z)]}},
        [(0.1, "swish"), (2.3, "click")], [])
    # swing 3 (combo) - rising hook: low, then up through the target, the ring rolling with it
    A["swing3"] = (0.36, {
        "bfk_pose": {"rotation": [(0, Z), (0.04, [22, 0, 10]), (0.11, [-58, 0, -18]), (0.21, [10, 0, 6]), (0.29, [-2, 0, -1]), (0.36, Z)],
                     "position": [(0, Z), (0.04, [0, -1.4, 0.6]), (0.11, [0.4, 2.4, -1.4]), (0.36, Z)]},
        "bfk_spin": {"rotation": [(0, Z), (0.11, [0, 0, 30]), (0.24, [0, 0, -4]), (0.36, Z)]}},
        [(0.0, "swish")], arc_trail(0.05, 0.16))
    # swing 4 (combo finisher) - reverse-hook rip: a full turn round the ring into reverse grip and a wide backhand
    A["swing4"] = (0.5, {
        "bfk_spin": {"rotation": [(0, Z), (0.1, [0, 0, -360]), (0.4, [0, 0, -360]), (0.5, [0, 0, -360])]},
        "bfk_pose": {"rotation": [(0, Z), (0.1, [-6, 0, 30]), (0.22, [34, 0, -96]), (0.34, [-8, 0, 16]), (0.42, [2, 0, -3]), (0.5, Z)],
                     "position": [(0, Z), (0.1, [-1.4, 0.6, 0]), (0.22, [2.2, 1.4, -1.6]), (0.5, Z)]}},
        [(0.0, "ring_spin"), (0.14, "swish")], arc_trail(0.13, 0.27, 8))
    return A


def balisong():
    A = {}
    # draw: a real basic opening - closed, safe handle swings out, the knife twirls, bite handle follows, latch
    A["draw"] = (0.95, {
        "bfb_safe": {"rotation": [(0, [0, 0, 180]), (0.08, [0, 0, 175]), (0.26, [0, 0, -6]), (0.32, Z), (0.95, Z)]},
        "bfb_bite": {"rotation": [(0, [0, 0, -180]), (0.5, [0, 0, -180]), (0.72, [0, 0, 5]), (0.78, Z), (0.95, Z)]},
        "bfk_spin": {"rotation": [(0, Z), (0.28, Z), (0.62, [0, 0, 360]), (0.95, [0, 0, 360])]},
        "bfk_pose": {"position": [(0, [0, -4, 0]), (0.25, [0, 0.4, 0]), (0.45, Z), (0.95, Z)]}},
        [(0.27, "clack"), (0.74, "clack"), (0.8, "latch")], [])
    # inspect 1 - rollovers: the safe handle swings over and back twice round the blade, clack on every stop
    A["insp1"] = (2.2, {
        "bfb_safe": {"rotation": [(0, Z), (0.3, [0, 0, 180]), (0.6, Z), (1.1, [0, 0, 180]), (1.4, Z), (2.2, Z)]},
        "bfb_bite": {"rotation": [(0, Z), (0.62, Z), (0.85, [0, 0, -180]), (1.05, Z), (2.2, Z)]},
        "bfk_pose": {"rotation": [(0, Z), (0.25, [-15, 25, 10]), (1.6, [-15, 25, 10]), (2.2, Z)],
                     "position": [(0, Z), (0.25, [0.5, 1.4, -0.6]), (1.6, [0.5, 1.4, -0.6]), (2.2, Z)]}},
        [(0.3, "clack"), (0.6, "clack"), (0.86, "clack"), (1.05, "clack"), (1.1, "clack"), (1.4, "latch")], [])
    # inspect 2 - aerial: tossed up, one turn in the air, caught, a quick fan of the bite handle
    A["insp2"] = (2.0, {
        "bfk_pose": {"position": [(0, Z), (0.25, [0, 5.5, 0]), (0.5, [0, 6.2, 0]), (0.78, Z), (0.86, [0, -0.4, 0]), (1.0, Z), (2.0, Z)]},
        "bfk_spin": {"rotation": [(0, Z), (0.78, [0, 0, -360]), (2.0, [0, 0, -360])]},
        "bfb_bite": {"rotation": [(0, Z), (1.1, Z), (1.35, [0, 0, -180]), (1.6, Z), (2.0, Z)]}},
        [(0.8, "clack"), (1.36, "clack"), (1.62, "clack")], [])
    # attacks: flick slash (handles rattle) / straight thrust
    A["swing1"] = (0.32, {
        "bfk_pose": {"rotation": [(0, Z), (0.035, [4, 0, 14]), (0.09, [-14, 0, -78]), (0.18, [5, 0, 18]), (0.25, [0, 0, -4]), (0.32, Z)],
                     "position": [(0, Z), (0.09, [1.4, 0.8, -0.8]), (0.32, Z)]},
        "bfb_safe": {"rotation": [(0, Z), (0.08, [0, 0, 7]), (0.16, [0, 0, -4]), (0.3, Z)]},
        "bfb_bite": {"rotation": [(0, Z), (0.08, [0, 0, -7]), (0.16, [0, 0, 4]), (0.3, Z)]}},
        [(0.0, "swish")], arc_trail(0.04, 0.16))
    A["swing2"] = (0.3, {
        "bfk_pose": {"position": [(0, Z), (0.04, [0, -1.6, 0.4]), (0.1, [0, 7.5, -1.2]), (0.18, [0, -0.6, 0]), (0.3, Z)],
                     "rotation": [(0, Z), (0.04, [6, 0, -4]), (0.1, [-18, 0, 8]), (0.2, [3, 0, -2]), (0.3, Z)]}},
        [(0.0, "swish")], arc_trail(0.05, 0.12, 4))
    # inspect 3 - chaplin: the knife spins on the safe handle's pin, the blade and bite handle swinging round together
    A["insp3"] = (2.2, {
        "bfb_safe": {"rotation": [(0, Z), (0.25, [0, 0, 180]), (0.5, [0, 0, 180]), (1.5, [0, 0, 180]), (1.75, Z), (2.2, Z)]},
        "bfk_spin": {"rotation": [(0, Z), (0.5, Z), (0.95, [0, 0, 360]), (1.4, [0, 0, 720]), (2.2, [0, 0, 720])]},
        "bfk_pose": {"rotation": [(0, Z), (0.3, [-12, 20, 8]), (1.8, [-12, 20, 8]), (2.2, Z)],
                     "position": [(0, Z), (0.3, [0.4, 1.2, -0.4]), (1.8, [0.4, 1.2, -0.4]), (2.2, Z)]}},
        [(0.26, "clack"), (0.96, "clack"), (1.41, "clack"), (1.76, "clack"), (1.82, "latch")], [])
    # inspect 4 - zen rollover: both handles roll over the hand one after the other, twice, with a slow look at the bite
    A["insp4"] = (2.4, {
        "bfb_bite": {"rotation": [(0, Z), (0.3, [0, 0, -180]), (0.55, Z), (1.1, [0, 0, -180]), (1.35, Z), (2.4, Z)]},
        "bfb_safe": {"rotation": [(0, Z), (0.55, Z), (0.8, [0, 0, 180]), (1.05, Z), (1.35, Z), (1.6, [0, 0, 180]), (1.85, Z), (2.4, Z)]},
        "bfk_pose": {"rotation": [(0, Z), (0.25, [-18, 30, 12]), (1.95, [-22, -25, 10]), (2.4, Z)],
                     "position": [(0, Z), (0.25, [0.6, 1.6, -1.0]), (1.95, [0.6, 1.6, -1.0]), (2.4, Z)]}},
        [(0.3, "clack"), (0.56, "clack"), (0.8, "clack"), (1.05, "clack"), (1.1, "clack"), (1.35, "clack"), (1.6, "clack"), (1.86, "latch")], [])
    # swing 3 (combo) - backhand flick
    A["swing3"] = (0.32, {
        "bfk_pose": {"rotation": [(0, Z), (0.035, [-4, 0, -14]), (0.09, [16, 0, 74]), (0.18, [-5, 0, -16]), (0.25, [0, 0, 3]), (0.32, Z)],
                     "position": [(0, Z), (0.09, [-1.6, 0.8, -0.8]), (0.32, Z)]},
        "bfb_safe": {"rotation": [(0, Z), (0.08, [0, 0, -7]), (0.16, [0, 0, 4]), (0.3, Z)]},
        "bfb_bite": {"rotation": [(0, Z), (0.08, [0, 0, 7]), (0.16, [0, 0, -4]), (0.3, Z)]}},
        [(0.0, "swish")], arc_trail(0.04, 0.16))
    # swing 4 (combo finisher) - flip-stab: the handles fan open, a full twirl, and a hard thrust that latches shut
    A["swing4"] = (0.5, {
        "bfb_safe": {"rotation": [(0, Z), (0.06, [0, 0, 120]), (0.16, Z), (0.5, Z)]},
        "bfb_bite": {"rotation": [(0, Z), (0.06, [0, 0, -120]), (0.16, Z), (0.5, Z)]},
        "bfk_spin": {"rotation": [(0, Z), (0.16, [0, 0, 360]), (0.5, [0, 0, 360])]},
        "bfk_pose": {"position": [(0, Z), (0.16, [0, -1.8, 0.6]), (0.25, [0, 9.0, -1.6]), (0.34, [0, -0.6, 0]), (0.5, Z)],
                     "rotation": [(0, Z), (0.16, [8, 0, -6]), (0.25, [-22, 0, 10]), (0.36, [3, 0, -2]), (0.5, Z)]}},
        [(0.02, "clack"), (0.15, "latch"), (0.18, "swish")], arc_trail(0.18, 0.26, 5))
    return A


def dagger():
    A = {}
    # draw: tossed, one turn end over end, caught point-up with a low ring
    A["draw"] = (0.8, {
        "bfk_pose": {"position": [(0, [0, -4, 0]), (0.3, [0, 4.0, 0]), (0.6, Z), (0.66, [0, -0.3, 0]), (0.8, Z)]},
        "bfk_spin": {"rotation": [(0, Z), (0.6, [-360, 0, 0]), (0.8, [-360, 0, 0])]}},
        [(0.0, "swish"), (0.6, "dagger_catch")], [])
    # inspect 1 - palm twirl: spun twice round its own axis, then held up so the mirror blade flashes
    A["insp1"] = (2.1, {
        "bfk_spin": {"rotation": [(0, Z), (0.2, Z), (1.0, [0, 720, 0]), (2.1, [0, 720, 0])]},
        "bfk_pose": {"rotation": [(0, Z), (0.3, [-10, 0, 12]), (1.1, [-10, 0, 12]), (1.45, [-28, 35, 8]), (1.85, [-28, 35, 8]), (2.1, Z)],
                     "position": [(0, Z), (0.3, [0, 1.0, 0]), (1.85, [0, 1.0, 0]), (2.1, Z)]}},
        [(0.25, "swish"), (1.42, "click")], [])
    # inspect 2 - point-down flip: half a turn into an ice-pick grip, a look at the ridge, half a turn back
    A["insp2"] = (1.9, {
        "bfk_spin": {"rotation": [(0, Z), (0.3, [0, 0, 180]), (1.3, [0, 0, 180]), (1.6, [0, 0, 360]), (1.9, [0, 0, 360])]},
        "bfk_pose": {"rotation": [(0, Z), (0.6, [0, 60, 0]), (1.1, [0, -30, 0]), (1.9, Z)]}},
        [(0.32, "dagger_catch"), (1.62, "click")], [])
    # attacks: stab / slash
    A["swing1"] = (0.3, {
        "bfk_pose": {"position": [(0, Z), (0.04, [0, -2.0, 0.6]), (0.11, [0, 9.0, -1.5]), (0.19, [0, -0.8, 0]), (0.3, Z)],
                     "rotation": [(0, Z), (0.04, [8, 0, 0]), (0.11, [-20, 0, 0]), (0.21, [4, 0, 0]), (0.3, Z)]}},
        [(0.0, "swish")], arc_trail(0.06, 0.13, 4))
    A["swing2"] = (0.34, {
        "bfk_pose": {"rotation": [(0, Z), (0.035, [4, 0, -14]), (0.1, [-14, 0, 76]), (0.2, [4, 0, -18]), (0.27, [0, 0, 4]), (0.34, Z)],
                     "position": [(0, Z), (0.1, [-1.2, 0.8, -0.8]), (0.34, Z)]}},
        [(0.0, "swish")], arc_trail(0.05, 0.17))
    # inspect 3 - flip-and-catch by the blade: tossed, a turn and a half, caught by the blade, flipped back to the grip
    A["insp3"] = (2.2, {
        "bfk_pose": {"position": [(0, Z), (0.3, [0, 4.6, 0]), (0.62, Z), (0.7, [0, -0.3, 0]), (0.8, Z), (1.5, Z), (1.7, [0, 2.4, 0]), (1.92, Z), (2.2, Z)]},
        "bfk_spin": {"rotation": [(0, Z), (0.62, [-540, 0, 0]), (1.5, [-540, 0, 0]), (1.92, [-720, 0, 0]), (2.2, [-720, 0, 0])]}},
        [(0.0, "swish"), (0.62, "dagger_catch"), (1.52, "swish"), (1.92, "dagger_catch")], [])
    # inspect 4 - look at it: held up close and turned slowly so the mirror blade sweeps the light across
    A["insp4"] = (2.5, {
        "bfk_pose": {"rotation": [(0, Z), (0.45, [-30, 55, 10]), (1.3, [-32, -35, 8]), (2.0, [-28, 50, 12]), (2.5, Z)],
                     "position": [(0, Z), (0.45, [1.2, 2.0, -2.2]), (2.0, [1.2, 2.0, -2.2]), (2.5, Z)]}},
        [(0.1, "swish"), (2.2, "click")], [])
    # swing 3 (combo) - reverse slash back across
    A["swing3"] = (0.32, {
        "bfk_pose": {"rotation": [(0, Z), (0.035, [-4, 0, 14]), (0.1, [14, 0, -74]), (0.2, [-4, 0, 16]), (0.27, [0, 0, -3]), (0.32, Z)],
                     "position": [(0, Z), (0.1, [1.4, 0.8, -0.8]), (0.32, Z)]}},
        [(0.0, "swish")], arc_trail(0.05, 0.17))
    # swing 4 (combo finisher) - double stab: two fast thrusts, the second deeper
    A["swing4"] = (0.48, {
        "bfk_pose": {"position": [(0, Z), (0.04, [0, -1.6, 0.5]), (0.1, [0, 7.0, -1.2]), (0.17, [0, -1.2, 0.4]), (0.25, [0, 10.0, -1.8]), (0.34, [0, -0.6, 0]), (0.48, Z)],
                     "rotation": [(0, Z), (0.1, [-16, 0, 0]), (0.17, [8, 0, 0]), (0.25, [-24, 0, 0]), (0.36, [4, 0, 0]), (0.48, Z)]}},
        [(0.0, "swish"), (0.16, "swish")], [(0.1, "streak"), (0.25, "streak"), (0.26, "spark")])
    return A


DEFS = {"karambit": karambit, "balisong": balisong, "dagger": dagger}
DURATION = {}


def slash_style(trail, style):
    """the knife's tip-trail timing (arc_trail) -> the same slash in another style. Trail: the light dots along the
    tip's path. Crescent: one curved blade of light at the middle of the cut. Streak: a thrust burst where the
    stab reaches furthest (the end of the trail window)."""
    if style == "trail":
        return trail
    ts = [t for t, _ in trail]
    if style == "crescent":
        return [(ts[len(ts) // 2], "crescent")]
    return [(ts[-1], "streak")]


GLINT_AT = {"draw": 0.8, "insp1": 0.6, "insp2": 0.6, "insp3": 0.66, "insp4": 0.5}   # share of the clip where the edge catches the light


GHOSTS = 8            # afterimage copies per knife
GHOST_DT = 0.02       # seconds between copies (8 x 20 ms: the trail covers the last 160 ms of motion)


def ghost_bone(bone, g):
    """bfk_pose -> bfg3_pose, bfb_safe -> bfg3_safe (the ghost tree's names)"""
    return f"bfg{g}_" + bone.split("_", 1)[1]


def _sample(track, t):
    ks = sorted((float(kk), v["post"] if isinstance(v, dict) else v) for kk, v in track.items())
    ts = [a for a, _ in ks]; vs = [list(map(float, b if isinstance(b, list) else [b] * 3)) for _, b in ks]
    if t <= ts[0]: return vs[0]
    if t >= ts[-1]: return vs[-1]
    j = max(x for x in range(len(ts)) if ts[x] <= t)
    u = (t - ts[j]) / (ts[j + 1] - ts[j])
    p0, p1, p2, p3 = vs[max(j - 1, 0)], vs[j], vs[j + 1], vs[min(j + 2, len(vs) - 1)]
    return [0.5 * ((2 * b) + (-a + c) * u + (2 * a - 5 * b + 4 * c - d) * u * u + (-a + 3 * b - 3 * c + d) * u ** 3) for a, b, c, d in zip(p0, p1, p2, p3)]


def ghost_visibility(bones, L, lag):
    """position keys for the ghost root over the ghost's own clip time: +1000 (shown) where the knife has moved
    enough since this ghost's moment (rotation > 6 deg or position > 0.5 px on any bone), 0 (hidden) elsewhere"""
    step = 1 / 60
    ts, ons = [], []
    t = 0.0
    while t <= L + 1e-6:
        moved = 0.0
        for ch in bones.values():
            for kind, tr in ch.items():
                a, b = _sample(tr, t), _sample(tr, min(t + lag, L))
                d = max(abs(x - y) for x, y in zip(a, b))
                moved = max(moved, d / 6.0 if kind == "rotation" else d / 0.5 if kind == "position" else 0.0)
        ts.append(t); ons.append(moved > 1.0)
        t += step
    # no flicker: close gaps under 80 ms, drop blips under 40 ms
    def runs(v):
        out, i = [], 0
        while i < len(v):
            j = i
            while j < len(v) and v[j] == v[i]: j += 1
            out.append((v[i], i, j)); i = j
        return out
    for val, i, j in runs(ons):
        if not val and 0 < i and j < len(ons) and (j - i) * step < 0.08: ons[i:j] = [True] * (j - i)
    for val, i, j in runs(ons):
        if val and (j - i) * step < 0.04: ons[i:j] = [False] * (j - i)
    keys, last = {}, None
    for t, on in zip(ts, ons):
        if on != last:
            v = [0, 1000, 0] if on else [0, 0, 0]
            prev = [0, 0, 0] if on else [0, 1000, 0]
            keys[f"{t:.3f}"] = {"pre": prev if last is not None else v, "post": v}
            last = on
    if not any(k["post"][1] for k in keys.values()):
        return None
    return keys


def build():
    """-> (animations json, controllers json, animation map, animate list, sounds, particles)

    No animation controllers: on the PC they never ran. Every clip is a plain animation whose time IS Bedrock Forge's
    clock for its event (seconds since the draw / inspect / click / hit x speed), switched on by a condition. The click
    parity picks swing1 or swing2 (and insp1 / insp2, hit a / b), so a new click restarts the other clip at 0."""
    anims = {}
    for k in KNIVES:
        for name, (length, bones, snd, prt) in DEFS[k]().items():
            DURATION[(k, name)] = length
            anims[f"animation.bfk.{k}.{name}"] = anim(length, bones)
            if snd:
                anims[f"animation.bfk.{k}.{name}.snd"] = anim(length, {}, sounds=snd)
                anims[f"animation.bfk.{k}.{name}.sndq"] = anim(length, {}, sounds=[(t, e + "_q") for t, e in snd])
            if prt:
                for si, style in enumerate(SLASH_STYLES):
                    anims[f"animation.bfk.{k}.{name}.fx{si}"] = anim(length, {}, particles=slash_style(prt, style))
            if name in GLINT_AT:   # edge glint: a star of light on the tip as the knife settles / is shown off
                anims[f"animation.bfk.{k}.{name}.gl"] = anim(length, {}, particles=[(round(length * GLINT_AT[name], 3), "glint")])
    # first person: bfk_fix (between the World FX hold bone and the pose) sizes and places the knife in view;
    # tuned on real frames (Knives Test world, 2026-11-17)
    # (karambit verified; the balisong read ~10% long on screen, so it is held a little smaller)
    for k in KNIVES:
        anims[f"animation.bfk.{k}.fp"] = {"loop": True, "bones": {"bfk_fix": {
            "scale": f"{FP_SCALE[k]} * v.bfk_scl", "position": ["v.bfk_ox", "7 + v.bfk_oy", 0]}}}
    # confirmed hits: sparks + cut flash off the tip, and the cut sound
    for ab in ("a", "b"):
        anims[f"animation.bfk.hit_{ab}.fx"] = anim(0.3, {}, particles=[(0.0, "spark"), (0.0, "cutflash")])
        # a critical hit: a second burst of sparks a beat later and a star on the edge (heavier, still under 0.3 s)
        anims[f"animation.bfk.hit_{ab}.fxc"] = anim(0.3, {}, particles=[(0.0, "glint"), (0.04, "spark")])
        anims[f"animation.bfk.hit_{ab}.snd"] = anim(0.3, {}, sounds=[(0.0, "cut")])
        anims[f"animation.bfk.hit_{ab}.sndq"] = anim(0.3, {}, sounds=[(0.0, "cut_q")])

    # bfkinsn = trick 0..3, bfkatn = combo step 0..3 (Cosmetics.cpp); 1 / 0 kept for the original two of each
    clock = {"draw": ("v.bfk_d", None), "insp1": ("v.bfk_i", "v.bfk_in == 1"), "insp2": ("v.bfk_i", "v.bfk_in == 0"),
             "insp3": ("v.bfk_i", "v.bfk_in == 2"), "insp4": ("v.bfk_i", "v.bfk_in == 3"),
             "swing1": ("v.bfk_a", "v.bfk_an == 1"), "swing2": ("v.bfk_a", "v.bfk_an == 0"),
             "swing3": ("v.bfk_a", "v.bfk_an == 2"), "swing4": ("v.bfk_a", "v.bfk_an == 3")}
    # sprint carry (first person): the knife turns round and lies back along the forearm, eased by Bedrock Forge
    # (v.bfk_spr 0..1, smoothstepped here so the flip starts and lands soft)
    for k in KNIVES:
        anims[f"animation.bfk.{k}.carry"] = {"loop": True, "bones": {"bfk_pose": {
            "rotation": ["v.bfk_spe * -28", "v.bfk_spe * 12", "v.bfk_spe * 168"],
            "position": ["v.bfk_spe * 0.6", "v.bfk_spe * -3.2", "v.bfk_spe * 1.2"]}}}
    for k in KNIVES:   # third person: same bone, own size
        anims[f"animation.bfk.{k}.tp"] = {"loop": True, "bones": {"bfk_fix": {"scale": f"{TP_SCALE[k]} * v.bfk_scl"}}}
    amap = {f"bfk_fp_{k}": f"animation.bfk.{k}.fp" for k in KNIVES}
    amap.update({f"bfk_tp_{k}": f"animation.bfk.{k}.tp" for k in KNIVES})
    animate = [{f"bfk_fp_{k}": f"v.bfk == {i + 1} && c.is_first_person"} for i, k in enumerate(KNIVES)]
    animate += [{f"bfk_tp_{k}": f"v.bfk == {i + 1} && !c.is_first_person"} for i, k in enumerate(KNIVES)]
    amap.update({f"bfk_carry_{k}": f"animation.bfk.{k}.carry" for k in KNIVES})
    animate += [{f"bfk_carry_{k}": f"v.bfk == {i + 1} && c.is_first_person && v.bfk_spr > 0.002"} for i, k in enumerate(KNIVES)]
    for i, k in enumerate(KNIVES):
        n = i + 1
        for kind, (var, parity) in clock.items():
            if (k, kind) not in DURATION:
                continue
            L = DURATION[(k, kind)]
            on = f"v.bfk == {n} && {var} * v.bfk_spd < {L:.3f}"
            if parity:
                on += f" && {parity}"
            if kind in ("draw", "insp1", "insp2", "insp3", "insp4"):
                on += " && !v.bfk_swinging"       # a click cuts a trick short instead of stacking on it
            variants = [("", ""), (".snd", " && v.bfk_snd && !v.bfk_quiet"), (".sndq", " && v.bfk_snd && v.bfk_quiet")]
            variants += [(f".fx{si}", f" && v.bfk_arcs && v.bfk_sl == {si}") for si in range(len(SLASH_STYLES))]
            variants += [(".gl", " && v.bfk_arcs")]
            for suffix, extra in variants:
                full = f"animation.bfk.{k}.{kind}{suffix}"
                if full not in anims:
                    continue
                short = f"{k}_{kind}{suffix.replace('.', '_')}"
                anims[full]["anim_time_update"] = f"{var} * v.bfk_spd"
                amap[short] = full
                animate.append({short: on + extra})
    # ---- afterimage (Purxiy reference, video 2 at 161 s / video 4 at 33 s): GHOST copies of the whole knife fan out
    # along every swing / flip - flat see-through lavender, the newest strongest. Ghost k is its own copy of the bone
    # tree (bfg{k}_*, in the ghost geometry) playing the SAME clip k * GHOST_DT behind the knife. It only shows where
    # the knife has really moved since (a still knife has no trail): its root bone sits 1000 px away (hidden, the
    # ghost_hide clip) and each clip lifts it back (+1000) only over the moving stretches, worked out here.
    anims["animation.bfk.ghost_hide"] = {"loop": True, "bones": {f"bfg{g}": {"position": [0, -1000, 0]} for g in range(1, GHOSTS + 1)}}
    amap["bfk_ghost_hide"] = "animation.bfk.ghost_hide"
    animate.append({"bfk_ghost_hide": "v.bfk > 0"})
    for i, k in enumerate(KNIVES):
        n = i + 1
        for kind, (var, parity) in clock.items():
            if (k, kind) not in DURATION:
                continue
            L = DURATION[(k, kind)]
            base = anims[f"animation.bfk.{k}.{kind}"]
            for g in range(1, GHOSTS + 1):
                lag = g * GHOST_DT
                bones = {}
                for bone, ch in base.get("bones", {}).items():
                    bones[ghost_bone(bone, g)] = ch
                show = ghost_visibility(base.get("bones", {}), L, lag)
                if not show:
                    continue
                bones[f"bfg{g}"] = {"position": show}
                full = f"animation.bfk.{k}.{kind}.g{g}"
                anims[full] = {"animation_length": L, "anim_time_update": f"math.max(0, {var} * v.bfk_spd - {lag:.3f})", "bones": bones}
                short = f"{k}_{kind}_g{g}"
                amap[short] = full
                on = f"v.bfk == {n} && v.bfk_ghost && {var} * v.bfk_spd < {L + lag:.3f} && {var} * v.bfk_spd > {lag:.3f}"
                if parity:
                    on += f" && {parity}"
                if kind in ("draw", "insp1", "insp2", "insp3", "insp4"):
                    on += " && !v.bfk_swinging"
                animate.append({short: on})
    for ab, par in (("a", 1), ("b", 0)):
        on = f"v.bfk > 0 && v.bfk_h < 0.3 && v.bfk_hn == {par}"
        for suffix, extra in (("fx", " && v.bfk_hfx"), ("fxc", " && v.bfk_hfx && v.bfk_crit"), ("snd", " && v.bfk_snd && !v.bfk_quiet"), ("sndq", " && v.bfk_snd && v.bfk_quiet")):
            full = f"animation.bfk.hit_{ab}.{suffix}"
            anims[full]["anim_time_update"] = "v.bfk_h"
            amap[f"hit_{ab}_{suffix}"] = full
            animate.append({f"hit_{ab}_{suffix}": on + extra})
    sounds = {}
    for e in ("swish", "cut", "click", "ring_spin", "clack", "latch", "draw", "dagger_catch"):
        sounds[e] = f"bfk.{e}"
        sounds[e + "_q"] = f"bfk.{e}.q"
    particles = {n: f"bfk:{n}" for n in ("arc", "crescent", "streak", "spark", "cutflash", "glint")}
    return {"format_version": "1.10.0", "animations": anims}, None, amap, animate, sounds, particles


def swinging_expr():
    """v.bfk_swinging for PRE: a swing clip of the selected knife is still running"""
    parts = []
    for i, k in enumerate(KNIVES):
        L = max(DURATION.get((k, f"swing{j}"), 0) for j in (1, 2, 3, 4))
        parts.append(f"(v.bfk == {i + 1} && v.bfk_a * v.bfk_spd < {L:.3f})")
    return "v.bfk_swinging = " + " || ".join(parts) + ";"


PRE = ("v.bfk = q.owner_identifier == 'minecraft:player' ? c.owning_entity->v.bfknife : 0;"
       "v.bfk_d = v.bfk > 0 ? c.owning_entity->v.bfkdraw : 99;"
       "v.bfk_i = v.bfk > 0 ? c.owning_entity->v.bfkinsp : 99;"
       "v.bfk_in = v.bfk > 0 ? c.owning_entity->v.bfkinsn : 0;"
       "v.bfk_a = v.bfk > 0 ? c.owning_entity->v.bfkatk : 99;"
       "v.bfk_an = v.bfk > 0 ? c.owning_entity->v.bfkatn : 0;"
       "v.bfk_h = v.bfk > 0 ? c.owning_entity->v.bfkhit : 99;"
       "v.bfk_hn = v.bfk > 0 ? c.owning_entity->v.bfkhitn : 0;"
       "v.bfk_spd = v.bfk > 0 ? math.clamp(c.owning_entity->v.bfkspd, 0.5, 2.0) : 1;"
       "t.o = v.bfk > 0 ? c.owning_entity->v.bfkopt : 0;"
       "v.bfk_arcs = math.mod(t.o, 2) >= 1;"
       "v.bfk_hfx = math.mod(math.floor(t.o / 2), 2) >= 1;"
       "v.bfk_snd = math.mod(math.floor(t.o / 4), 2) >= 1;"
       "v.bfk_quiet = math.mod(math.floor(t.o / 8), 2) >= 1;"
       "v.bfk_glint = math.mod(math.floor(t.o / 16), 2) < 1;"
       "v.bfk_crit = math.mod(math.floor(t.o / 64), 2) >= 1;"
       "v.bfk_tp = math.mod(math.floor(t.o / 32), 2) < 1;"
       "v.bfk_fin = v.bfk > 0 ? math.clamp(math.floor(c.owning_entity->v.bfkfin), 0, 3) : 0;"
       "v.bfk_sl = v.bfk > 0 ? c.owning_entity->v.bfkslash : 0;"
       "v.bfk_scl = v.bfk > 0 ? math.clamp(c.owning_entity->v.bfkscl, 0.4, 2.0) : 1;"
       "v.bfk_ox = v.bfk > 0 ? c.owning_entity->v.bfkox : 0;"
       "v.bfk_oy = v.bfk > 0 ? c.owning_entity->v.bfkoy : 0;"
       "v.bfk_spr = v.bfk > 0 ? math.clamp(c.owning_entity->v.bfkspr, 0, 1) : 0;"
       "v.bfk_spe = v.bfk_spr * v.bfk_spr * (3 - 2 * v.bfk_spr);"
       "v.bfk_ghost = math.mod(math.floor(t.o / 128), 2) < 1;"
       "v.bfk_cr = v.bfk > 0 ? c.owning_entity->v.bfkcr : 0.8;"
       "v.bfk_cg = v.bfk > 0 ? c.owning_entity->v.bfkcg : 0.95;"
       "v.bfk_cb = v.bfk > 0 ? c.owning_entity->v.bfkcb : 0.97;"
       # the knife only shows in first person when "third person" is off (other views keep the vanilla sword)
       "v.bfk = (v.bfk > 0 && !v.bfk_tp && !c.is_first_person) ? 0 : v.bfk;")


# ------------------------------------------------------------------ particles (our own textures: particles_fx.png)
def particle_files():
    """our own particles (fx.png); every one takes its colour from the emitter (v.cr/cg/cb, set by FX_SCRIPT from
    Bedrock Forge's pick) and falls back to steel white if the game doesn't pass it. Additive, small, short-lived:
    a whole swing is at most 6 particles (trail), 1 (crescent) or 9 (streak); a hit is ~10 sparks + 1 cut."""
    tex = "textures/bf_cosmetics/fx"
    def base(ident, comps):
        return {"format_version": "1.10.0", "particle_effect": {
            "description": {"identifier": ident, "basic_render_parameters": {"material": "particles_add", "texture": tex}},
            "components": comps}}
    uv = lambda x, y: {"texture_width": 64, "texture_height": 64, "uv": [x, y], "uv_size": [16, 16]}
    age = "v.particle_age / v.particle_lifetime"
    def tint(hot=2.5, white=1.0, fade="1 - {a}"):
        """starts white-hot, settles into the picked colour, fades out"""
        k = f"math.min(1, {age} * {hot})"
        ch = lambda c, d: f"math.lerp({white}, (v.{c} ?? {d}), {k})"
        return {"color": [ch("cr", 0.8), ch("cg", 0.95), ch("cb", 0.97), fade.format(a=age)]}
    once = {"minecraft:emitter_lifetime_once": {"active_time": 0.01}}
    arc = base("bfk:arc", dict(once, **{
        # trail: one soft streak of light where the tip passed; a run of them draws the slash
        "minecraft:emitter_rate_instant": {"num_particles": 1},
        "minecraft:emitter_shape_point": {},
        "minecraft:particle_lifetime_expression": {"max_lifetime": 0.16},
        "minecraft:particle_initial_speed": 0,
        "minecraft:particle_motion_dynamic": {},
        "minecraft:particle_appearance_billboard": {"size": [f"0.09 * (1 - {age} * 0.5)", f"0.09 * (1 - {age} * 0.5)"],
                                                    "facing_camera_mode": "rotate_xyz", "uv": uv(0, 0)},
        "minecraft:particle_appearance_tinting": tint()}))
    crescent = base("bfk:crescent", dict(once, **{
        # crescent: one curved blade of light that opens along the cut and thins out (the karambit's hook)
        "minecraft:emitter_rate_instant": {"num_particles": 1},
        "minecraft:emitter_shape_point": {},
        "minecraft:particle_lifetime_expression": {"max_lifetime": 0.2},
        "minecraft:particle_initial_speed": 0,
        "minecraft:particle_initial_spin": {"rotation": "math.random(-25, 25)"},
        "minecraft:particle_motion_dynamic": {},
        "minecraft:particle_appearance_billboard": {"size": [f"0.42 + 0.18 * {age}", f"0.42 + 0.18 * {age}"],
                                                    "facing_camera_mode": "rotate_xyz",
                                                    "uv": {"texture_width": 64, "texture_height": 64, "uv": [0, 16], "uv_size": [32, 32]}},
        "minecraft:particle_appearance_tinting": tint(4, 1.0, "math.pow(1 - {a}, 1.5)")}))
    streak = base("bfk:streak", dict(once, **{
        # thrust streak: a hard point of light at full reach with fine lines kicked out round it (the dagger's stab)
        "minecraft:emitter_rate_instant": {"num_particles": 9},
        "minecraft:emitter_shape_sphere": {"radius": 0.01, "direction": "outwards"},
        "minecraft:particle_lifetime_expression": {"max_lifetime": "math.random(0.08, 0.14)"},
        "minecraft:particle_initial_speed": "math.random(3, 6)",
        "minecraft:particle_motion_dynamic": {"linear_drag_coefficient": 9.0},
        "minecraft:particle_appearance_billboard": {"size": [0.008, f"0.07 * (1 - {age})"],
                                                    "facing_camera_mode": "lookat_direction",
                                                    "direction": {"mode": "derive_from_velocity"}, "uv": uv(16, 0)},
        "minecraft:particle_appearance_tinting": tint(5)}))
    spark = base("bfk:spark", {
        # steel sparks off the edge on a hit: hot streaks that arc down, fading from white through the colour
        "minecraft:emitter_rate_instant": {"num_particles": "math.random_integer(7, 11)"},
        "minecraft:emitter_lifetime_once": {"active_time": 0.02},
        "minecraft:emitter_shape_sphere": {"radius": 0.03, "direction": "outwards"},
        "minecraft:particle_lifetime_expression": {"max_lifetime": "math.random(0.12, 0.3)"},
        "minecraft:particle_initial_speed": "math.random(2.5, 5.5)",
        "minecraft:particle_motion_dynamic": {"linear_acceleration": [0, -12, 0], "linear_drag_coefficient": 3.0},
        "minecraft:particle_appearance_billboard": {"size": [0.012, f"0.06 * (1 - {age})"],
                                                    "facing_camera_mode": "lookat_direction", "direction": {"mode": "derive_from_velocity"}, "uv": uv(16, 0)},
        "minecraft:particle_appearance_tinting": tint(3)})
    cut = base("bfk:cutflash", dict(once, **{
        # the cut itself: a thin bright line that opens and fades in a tenth of a second
        "minecraft:emitter_rate_instant": {"num_particles": 1},
        "minecraft:emitter_shape_point": {},
        "minecraft:particle_lifetime_expression": {"max_lifetime": 0.11},
        "minecraft:particle_initial_speed": 0,
        "minecraft:particle_motion_dynamic": {},
        "minecraft:particle_appearance_billboard": {"size": [f"0.5 * (0.4 + {age})", 0.035],
                                                    "facing_camera_mode": "rotate_xyz", "uv": uv(32, 0)},
        "minecraft:particle_appearance_tinting": tint(6, 1.0)}))
    glint = base("bfk:glint", dict(once, **{
        # edge glint: a four-point star that blooms on the tip and closes again in a quarter second, turning a little
        "minecraft:emitter_rate_instant": {"num_particles": 1},
        "minecraft:emitter_shape_point": {},
        "minecraft:particle_lifetime_expression": {"max_lifetime": 0.26},
        "minecraft:particle_initial_speed": 0,
        "minecraft:particle_initial_spin": {"rotation": "math.random(0, 45)", "rotation_rate": 140},
        "minecraft:particle_motion_dynamic": {},
        "minecraft:particle_appearance_billboard": {"size": [f"0.16 * math.sin({age} * 180)", f"0.16 * math.sin({age} * 180)"],
                                                    "facing_camera_mode": "rotate_xyz", "uv": uv(48, 0)},
        "minecraft:particle_appearance_tinting": tint(1.2, 1.0, "1")}))
    return {f"bfk_{n}.particle.json": js for n, js in
            (("arc", arc), ("crescent", crescent), ("streak", streak), ("spark", spark), ("cutflash", cut), ("glint", glint))}


def fx_texture():
    """64x64: soft round light (arc), hot streak (spark), thin blade line (cut); white, tinted in the particles"""
    import numpy as np
    img = np.zeros((64, 64, 4), np.uint8)
    y, x = np.mgrid[0:16, 0:16] + 0.5
    r = np.hypot(x - 8, y - 8) / 8
    soft = np.clip(1 - r, 0, 1) ** 2.2
    streak = np.clip(1 - np.abs(x - 8) / 2.2, 0, 1) * np.clip(1 - np.abs(y - 8) / 8, 0, 1) ** 0.6
    line = np.clip(1 - np.abs(y - 8) / 1.6, 0, 1) * np.clip(1 - np.abs(x - 8) / 8, 0, 1) ** 0.5
    # crescent (32x32 at 0,16, drawn 4x supersampled): a moon-shaped blade of light - the outer circle minus an
    # offset inner one gives the real crescent taper (sharp horns, full belly) - plus a faint inner glow
    S = 128
    yy, xx = (np.mgrid[0:S, 0:S] + 0.5) / S
    outer = np.hypot(xx - 0.5, yy - 0.62) - 0.40
    inner = np.hypot(xx - 0.5, yy - 0.74) - 0.40
    aa = 1.5 / S
    body = np.clip(0.5 - outer / aa, 0, 1) * np.clip(0.5 + inner / aa, 0, 1)
    glow = np.exp(-np.maximum(outer, 0) ** 2 / 0.0025) * np.clip(0.5 + inner / 0.05, 0, 1) * 0.25
    big = np.clip(body + glow, 0, 1)
    crescent = big.reshape(32, 4, 32, 4).mean(axis=(1, 3))
    img[16:48, 0:32, :3] = 255
    img[16:48, 0:32, 3] = (crescent * 255).astype(np.uint8)
    # glint (48,0): a four-point star - two thin crossed spikes over a small hot core, drawn 8x supersampled
    G = 128
    gy, gx = (np.mgrid[0:G, 0:G] + 0.5) / G * 2 - 1
    ax, ay = np.abs(gx), np.abs(gy)
    spikes = np.clip(1 - ax / (0.16 * (1 - ay) + 1e-3), 0, 1) * (1 - ay) ** 1.1 + np.clip(1 - ay / (0.16 * (1 - ax) + 1e-3), 0, 1) * (1 - ax) ** 1.1
    core = np.exp(-(gx ** 2 + gy ** 2) / 0.03)
    star = np.clip(spikes + core, 0, 1).reshape(16, 8, 16, 8).mean(axis=(1, 3)) ** 0.6   # lifted: thin spikes average out at 16 px
    img[0:16, 48:64, :3] = 255
    img[0:16, 48:64, 3] = (star * 255).astype(np.uint8)
    for i, a in enumerate((soft, streak, line)):
        img[0:16, i * 16:(i + 1) * 16, :3] = 255
        img[0:16, i * 16:(i + 1) * 16, 3] = (a * 255).astype(np.uint8)
    return img
