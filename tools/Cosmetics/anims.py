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
SPEED = "q.anim_time + q.delta_time * v.bfk_spd"


def keys(points):
    """[(t, [x, y, z]), ...] -> smooth (Catmull-Rom) keyframes"""
    out = {}
    for t, v in points:
        out[f"{t:.3f}"] = {"post": v if isinstance(v, list) else [v, v, v], "lerp_mode": "catmullrom"}
    return out


def anim(length, bones, sounds=None, particles=None):
    a = {"animation_length": length, "anim_time_update": SPEED, "bones": {}}
    for bone, ch in bones.items():
        a["bones"][bone] = {k: keys(v) for k, v in ch.items()}
    if sounds:
        a["sound_effects"] = {f"{t:.3f}": {"effect": e} for t, e in sounds}
    if particles:
        a["particle_effects"] = {f"{t:.3f}": {"effect": e, "locator": "tip"} for t, e in particles}
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
        "bfk_pose": {"rotation": [(0, Z), (0.035, [6, 0, -10]), (0.09, [-22, 0, 56]), (0.19, [10, 0, -18]), (0.27, [-2, 0, 4]), (0.36, Z)]},
        "bfk_spin": {"rotation": [(0, Z), (0.05, [0, 0, 8]), (0.11, [0, 0, -42]), (0.22, [0, 0, 7]), (0.36, Z)]}},
        [(0.0, "swish")], arc_trail(0.05, 0.17))
    A["swish2_"] = None
    A["swing2"] = (0.36, {
        "bfk_pose": {"rotation": [(0, Z), (0.035, [-6, 0, 10]), (0.1, [28, 0, -50]), (0.2, [-8, 0, 16]), (0.28, [2, 0, -3]), (0.36, Z)],
                     "position": [(0, Z), (0.1, [0.8, 0.6, 0]), (0.36, Z)]},
        "bfk_spin": {"rotation": [(0, Z), (0.11, [0, 0, 34]), (0.22, [0, 0, -5]), (0.36, Z)]}},
        [(0.0, "swish")], arc_trail(0.05, 0.18))
    del A["swish2_"]
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
        "bfk_pose": {"rotation": [(0, Z), (0.035, [0, 0, 10]), (0.09, [-6, 0, -58]), (0.18, [3, 0, 14]), (0.25, [0, 0, -3]), (0.32, Z)]},
        "bfb_safe": {"rotation": [(0, Z), (0.08, [0, 0, 7]), (0.16, [0, 0, -4]), (0.3, Z)]},
        "bfb_bite": {"rotation": [(0, Z), (0.08, [0, 0, -7]), (0.16, [0, 0, 4]), (0.3, Z)]}},
        [(0.0, "swish")], arc_trail(0.04, 0.16))
    A["swing2"] = (0.3, {
        "bfk_pose": {"position": [(0, Z), (0.04, [0, -0.8, 0]), (0.1, [0, 3.6, 0]), (0.18, [0, -0.3, 0]), (0.3, Z)],
                     "rotation": [(0, Z), (0.1, [-10, 0, 5]), (0.3, Z)]}},
        [(0.0, "swish")], arc_trail(0.05, 0.12, 4))
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
        "bfk_pose": {"position": [(0, Z), (0.04, [0, -1.0, 0]), (0.11, [0, 4.6, 0]), (0.19, [0, -0.4, 0]), (0.3, Z)],
                     "rotation": [(0, Z), (0.11, [-12, 0, 0]), (0.3, Z)]}},
        [(0.0, "swish")], arc_trail(0.06, 0.13, 4))
    A["swing2"] = (0.34, {
        "bfk_pose": {"rotation": [(0, Z), (0.035, [0, 0, -10]), (0.1, [-6, 0, 58]), (0.2, [2, 0, -15]), (0.27, [0, 0, 3]), (0.34, Z)]}},
        [(0.0, "swish")], arc_trail(0.05, 0.17))
    return A


DEFS = {"karambit": karambit, "balisong": balisong, "dagger": dagger}
DURATION = {}


def build():
    """-> (animations json, controllers json, description fields for every sword attachable)"""
    anims = {}
    for i, k in enumerate(KNIVES):
        for name, (length, bones, snd, prt) in DEFS[k]().items():
            DURATION[(k, name)] = length
            anims[f"animation.bfk.{k}.{name}"] = anim(length, bones)
            if snd:
                anims[f"animation.bfk.{k}.{name}.snd"] = anim(length, {}, sounds=snd)
            if prt:
                anims[f"animation.bfk.{k}.{name}.fx"] = anim(length, {}, particles=prt)
    # first person: a knife is held a little bigger than the sword's 0.55 (it is shorter)
    anims["animation.bfk.knife_fp"] = {"loop": True, "bones": {"bfk_pose": {"scale": 1.25}}}

    def state(kind, longest):
        """animations of one state for every knife, each gated on the selected knife (+ its sound / fx tracks)"""
        lst = []
        for i, k in enumerate(KNIVES):
            n = i + 1
            lst.append({f"{k}_{kind}": f"v.bfk == {n}"})
            if (k, kind) in DURATION and f"animation.bfk.{k}.{kind}.snd" in anims:
                lst.append({f"{k}_{kind}_snd": f"v.bfk == {n} && v.bfk_snd"})
            if f"animation.bfk.{k}.{kind}.fx" in anims:
                lst.append({f"{k}_{kind}_fx": f"v.bfk == {n} && v.bfk_arcs"})
        return lst

    dmax = {kind: max(DURATION[(k, kind)] for k in KNIVES) for kind in ("draw", "insp1", "insp2", "swing1", "swing2")}
    go_attack = [{"swing1": "v.bfk_a < 0.1 && v.bfk_an == 1"}, {"swing2": "v.bfk_a < 0.1 && v.bfk_an == 0"}]
    go_inspect = [{"insp1": "v.bfk_i < 0.1 && v.bfk_in == 1"}, {"insp2": "v.bfk_i < 0.1 && v.bfk_in == 0"}]
    # the state ends by Bedrock Forge's clock (seconds since the event x speed), the same for every knife
    end = lambda var, kind: {"idle": f"{var} * v.bfk_spd > {dmax[kind]:.2f}"}
    ctrl = {"initial_state": "idle", "states": {
        "idle": {"transitions": [{"draw": "v.bfk_d < 0.1"}] + go_attack + go_inspect},
        "draw": {"animations": state("draw", dmax["draw"]), "transitions": go_attack + [end("v.bfk_d", "draw")], "blend_transition": 0.08},
        "insp1": {"animations": state("insp1", 0), "transitions": go_attack + [{"insp2": "v.bfk_i < 0.1 && v.bfk_in == 0"}, end("v.bfk_i", "insp1")], "blend_transition": 0.12},
        "insp2": {"animations": state("insp2", 0), "transitions": go_attack + [{"insp1": "v.bfk_i < 0.1 && v.bfk_in == 1"}, end("v.bfk_i", "insp2")], "blend_transition": 0.12},
        "swing1": {"animations": state("swing1", 0), "transitions": [{"swing2": "v.bfk_a < 0.1 && v.bfk_an == 0"}, end("v.bfk_a", "swing1")], "blend_transition": 0.05},
        "swing2": {"animations": state("swing2", 0), "transitions": [{"swing1": "v.bfk_a < 0.1 && v.bfk_an == 1"}, end("v.bfk_a", "swing2")], "blend_transition": 0.05},
    }}
    # confirmed hits: sparks off the tip + the cut (each its own controller so the setting can switch it off)
    hit_t = [{"hit_a": "v.bfk_h < 0.1 && v.bfk_hn == 1"}, {"hit_b": "v.bfk_h < 0.1 && v.bfk_hn == 0"}]
    def hit_ctrl(entry):
        return {"initial_state": "idle", "states": {
            "idle": {"transitions": hit_t},
            "hit_a": dict(entry, transitions=[{"hit_b": "v.bfk_h < 0.1 && v.bfk_hn == 0"}, {"idle": "v.bfk_h > 0.25"}]),
            "hit_b": dict(entry, transitions=[{"hit_a": "v.bfk_h < 0.1 && v.bfk_hn == 1"}, {"idle": "v.bfk_h > 0.25"}])}}
    ctrls = {"format_version": "1.10.0", "animation_controllers": {
        "controller.animation.bfk.tricks": ctrl,
        "controller.animation.bfk.hit_fx": hit_ctrl({"particle_effects": [{"effect": "spark", "locator": "tip"}, {"effect": "cutflash", "locator": "tip"}]}),
        "controller.animation.bfk.hit_snd": hit_ctrl({"sound_effects": [{"effect": "cut"}]}),
    }}
    amap = {"bfk_tricks": "controller.animation.bfk.tricks", "bfk_hit_fx": "controller.animation.bfk.hit_fx",
            "bfk_hit_snd": "controller.animation.bfk.hit_snd", "bfk_fp": "animation.bfk.knife_fp"}
    for name in anims:
        if name.startswith("animation.bfk.") and name.count(".") >= 3:
            short = name[len("animation.bfk."):].replace(".", "_")
            amap[short] = name
    animate = [{"bfk_tricks": "v.bfk > 0"}, {"bfk_hit_fx": "v.bfk > 0 && v.bfk_hfx"}, {"bfk_hit_snd": "v.bfk > 0 && v.bfk_snd"},
               {"bfk_fp": "v.bfk > 0 && c.is_first_person"}]
    sounds = {e: f"bfk.{e}" for e in ("swish", "cut", "click", "ring_spin", "clack", "latch", "draw", "dagger_catch")}
    particles = {"arc": "bfk:arc", "spark": "bfk:spark", "cutflash": "bfk:cutflash"}
    return {"format_version": "1.10.0", "animations": anims}, ctrls, amap, animate, sounds, particles


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
       "v.bfk_snd = math.floor(t.o / 4) >= 1;")


# ------------------------------------------------------------------ particles (our own textures: particles_fx.png)
def particle_files():
    tex = "textures/bf_cosmetics/fx"
    def base(ident, comps):
        return {"format_version": "1.10.0", "particle_effect": {
            "description": {"identifier": ident, "basic_render_parameters": {"material": "particles_add", "texture": tex}},
            "components": comps}}
    uv = lambda x, y: {"texture_width": 64, "texture_height": 64, "uv": [x, y], "uv_size": [16, 16]}
    arc = base("bfk:arc", {
        # one soft streak of light left in the air where the tip passed; a run of them draws the slash arc
        "minecraft:emitter_rate_instant": {"num_particles": 1},
        "minecraft:emitter_lifetime_once": {"active_time": 0.01},
        "minecraft:emitter_shape_point": {},
        "minecraft:particle_lifetime_expression": {"max_lifetime": 0.16},
        "minecraft:particle_initial_speed": 0,
        "minecraft:particle_motion_dynamic": {},
        "minecraft:particle_appearance_billboard": {"size": ["0.09 * (1 - v.particle_age / v.particle_lifetime * 0.5)", "0.09 * (1 - v.particle_age / v.particle_lifetime * 0.5)"],
                                                    "facing_camera_mode": "rotate_xyz", "uv": uv(0, 0)},
        "minecraft:particle_appearance_tinting": {"color": {"gradient": {"0.0": "#CCF4F6FF", "0.4": "#88DCE4FF", "1.0": "#00B8C4FF"},
                                                            "interpolant": "v.particle_age / v.particle_lifetime"}}})
    spark = base("bfk:spark", {
        # steel sparks off the edge: a quick spray of hot streaks that arc down and fade amber
        "minecraft:emitter_rate_instant": {"num_particles": "math.random_integer(7, 11)"},
        "minecraft:emitter_lifetime_once": {"active_time": 0.02},
        "minecraft:emitter_shape_sphere": {"radius": 0.03, "direction": "outwards"},
        "minecraft:particle_lifetime_expression": {"max_lifetime": "math.random(0.12, 0.3)"},
        "minecraft:particle_initial_speed": "math.random(2.5, 5.5)",
        "minecraft:particle_motion_dynamic": {"linear_acceleration": [0, -12, 0], "linear_drag_coefficient": 3.0},
        "minecraft:particle_appearance_billboard": {"size": [0.012, "0.06 * (1 - v.particle_age / v.particle_lifetime)"],
                                                    "facing_camera_mode": "lookat_direction", "direction": {"mode": "derive_from_velocity"}, "uv": uv(16, 0)},
        "minecraft:particle_appearance_tinting": {"color": {"gradient": {"0.0": "#FFFFF6DE", "0.35": "#FFFFC46A", "1.0": "#00FF7A28"},
                                                            "interpolant": "v.particle_age / v.particle_lifetime"}}})
    cut = base("bfk:cutflash", {
        # the cut itself: a thin bright line that opens and fades in a tenth of a second
        "minecraft:emitter_rate_instant": {"num_particles": 1},
        "minecraft:emitter_lifetime_once": {"active_time": 0.01},
        "minecraft:emitter_shape_point": {},
        "minecraft:particle_lifetime_expression": {"max_lifetime": 0.11},
        "minecraft:particle_initial_speed": 0,
        "minecraft:particle_motion_dynamic": {},
        "minecraft:particle_appearance_billboard": {"size": ["0.5 * (0.4 + v.particle_age / v.particle_lifetime)", 0.035],
                                                    "facing_camera_mode": "rotate_xyz", "uv": uv(32, 0)},
        "minecraft:particle_appearance_tinting": {"color": {"gradient": {"0.0": "#FFFFFFFF", "1.0": "#00FFFFFF"},
                                                            "interpolant": "v.particle_age / v.particle_lifetime"}}})
    return {"bfk_arc.particle.json": arc, "bfk_spark.particle.json": spark, "bfk_cutflash.particle.json": cut}


def fx_texture():
    """64x64: soft round light (arc), hot streak (spark), thin blade line (cut); white, tinted in the particles"""
    import numpy as np
    img = np.zeros((64, 64, 4), np.uint8)
    y, x = np.mgrid[0:16, 0:16] + 0.5
    r = np.hypot(x - 8, y - 8) / 8
    soft = np.clip(1 - r, 0, 1) ** 2.2
    streak = np.clip(1 - np.abs(x - 8) / 2.2, 0, 1) * np.clip(1 - np.abs(y - 8) / 8, 0, 1) ** 0.6
    line = np.clip(1 - np.abs(y - 8) / 1.6, 0, 1) * np.clip(1 - np.abs(x - 8) / 8, 0, 1) ** 0.5
    for i, a in enumerate((soft, streak, line)):
        img[0:16, i * 16:(i + 1) * 16, :3] = 255
        img[0:16, i * 16:(i + 1) * 16, 3] = (a * 255).astype(np.uint8)
    return img
