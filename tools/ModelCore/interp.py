"""ModelCore interpolation maths - pure functions, no file formats, no Bedrock/Java types.

Shared by GeckoBridge (offline bake of GeckoLib easings) and later FiguraBridge / the Bedrock Forge native
evaluator (C++). Every function here has test vectors in golden.json (`python -I interp.py --golden`); the
native port must reproduce them within 1e-9.

GeckoLib easings: ported from com.geckolib.animation.object.EasingType (GeckoLib 5.5.7, MIT, (c) GeckoLib / Tslat).
Convention (both GeckoLib and Figura): a curve maps segment progress d in [0,1] to weight w; value = a + (b-a)*w,
and d >= 1 returns b exactly.
"""
import math

# ---------------------------------------------------------------- GeckoLib easings ------------------
def _ease_out(f): return lambda t: 1 - f(1 - t)
def _ease_in_out(f): return lambda t: f(t * 2) / 2 if t < 0.5 else 1 - f((1 - t) * 2) / 2
def _sine(n): return 1 - math.cos(n * math.pi / 2)
def _circle(n): return 1 - math.sqrt(max(0.0, 1 - n * n))
def _exp(n): return math.pow(2, 10 * (n - 1))
def _pow(p): return lambda t: math.pow(t, p)
def _elastic(a):
    n2 = 1 if a is None else a
    return lambda t: 1 - math.pow(math.cos(t * math.pi / 2), 3) * math.cos(t * n2 * math.pi)
def _bounce(a):
    n2 = 0.5 if a is None else a
    one = lambda x: 121 / 16 * x * x
    two = lambda x: 121 / 4 * n2 * (x - 6 / 11) ** 2 + 1 - n2
    three = lambda x: 121 * n2 * n2 * (x - 9 / 11) ** 2 + 1 - n2 * n2
    four = lambda x: 484 * n2 ** 3 * (x - 10.5 / 11) ** 2 + 1 - n2 ** 3
    return lambda t: min(one(t), two(t), three(t), four(t))
def _back(a):
    n2 = 1.70158 if a is None else a * 1.70158
    return lambda t: t * t * ((n2 + 1) * t - n2)
def _step(a):
    steps = int(2 if a is None else a)
    if steps < 2: raise ValueError("step easing needs >= 2 steps")
    L = 1 / steps
    def f(t):
        if t < 0: return 0.0
        last = (steps - 1) * L
        if t > last: return last
        return math.floor(t / L + 1e-12) * L
    return f

_SIMPLE = {
    "linear": lambda t: t, "none": lambda t: t,
    "easeinsine": _sine, "easeoutsine": _ease_out(_sine), "easeinoutsine": _ease_in_out(_sine),
    "easeinquad": _pow(2), "easeoutquad": _ease_out(_pow(2)), "easeinoutquad": _ease_in_out(_pow(2)),
    "easeincubic": _pow(3), "easeoutcubic": _ease_out(_pow(3)), "easeinoutcubic": _ease_in_out(_pow(3)),
    "easeinquart": _pow(4), "easeoutquart": _ease_out(_pow(4)), "easeinoutquart": _ease_in_out(_pow(4)),
    "easeinquint": _pow(5), "easeoutquint": _ease_out(_pow(5)), "easeinoutquint": _ease_in_out(_pow(5)),
    "easeinexpo": _exp, "easeoutexpo": _ease_out(_exp), "easeinoutexpo": _ease_in_out(_exp),
    "easeincirc": _circle, "easeoutcirc": _ease_out(_circle), "easeinoutcirc": _ease_in_out(_circle),
}
_ARG = {
    "easeinback": _back, "easeoutback": lambda a: _ease_out(_back(a)), "easeinoutback": lambda a: _ease_in_out(_back(a)),
    "easeinelastic": _elastic, "easeoutelastic": lambda a: _ease_out(_elastic(a)),
    "easeinoutelastic": lambda a: _ease_in_out(_elastic(a)),
    "easeinbounce": _bounce, "easeoutbounce": lambda a: _ease_out(_bounce(a)),
    "easeinoutbounce": lambda a: _ease_in_out(_bounce(a)),
    "step": _step,
}

def easing_fn(name, arg=None):
    n = (name or "linear").lower()
    if n in _SIMPLE: return _SIMPLE[n]
    if n in _ARG: return _ARG[n](arg)
    return _SIMPLE["linear"]  # GeckoLib: unknown name -> LINEAR (EasingType.fromString)

def catmull(d, p0, p1, p2, p3):  # EasingType.CatmullRomEasing.getPointOnSpline
    return 0.5 * (2 * p1 + (p2 - p0) * d + (2 * p0 - 5 * p1 + 4 * p2 - p3) * d * d + (3 * p1 - p0 - 3 * p2 + p3) * d ** 3)


def ease(name, d, arg=None):
    """Weight for progress d with a GeckoLib easing name (case-insensitive)."""
    return 1.0 if d >= 1 else easing_fn(name, arg)(d)

EASINGS = sorted(set(_SIMPLE) | set(_ARG))

# ---------------------------------------------------------------- Figura / Blockbench (F2) ----------
# Figura 0.1.6 Animation interpolations: LINEAR, CATMULLROM, BEZIER, STEP. LINEAR = ease("linear"),
# CATMULLROM = catmull(...). STEP and BEZIER semantics follow Blockbench and are filled in with
# FiguraBridge F2 against Figura's own source (credited there); kept here so there is ONE evaluator.
def step_hold(d):
    """Blockbench/Figura STEP: hold the start value until the next keyframe."""
    return 1.0 if d >= 1 else 0.0

def bezier(d, p1, p2, h1, h2):
    raise NotImplementedError("Figura/Blockbench bezier handles: FiguraBridge F2")


if __name__ == "__main__":
    import json, sys
    if "--golden" in sys.argv:
        ds = [i / 20 for i in range(21)] + [0.001, 0.333, 0.499, 0.501, 0.999]
        out = {"note": "weights w(d); value = a + (b-a)*w. catmull: (d,p0,p1,p2,p3)->value", "easings": {}, "catmull": []}
        for n in EASINGS:
            for arg in ([None] + ([0.5, 1.5] if n in _ARG and n != "step" else []) + ([3, 5] if n == "step" else [])):
                out["easings"][f"{n}|{arg}"] = [[d, ease(n, d, arg)] for d in ds]
        for P in ([0, 0, 1, 1], [0, 1, 2, 3], [5, -3, 10, 2]):
            out["catmull"] += [[d, *P, catmull(d, *P)] for d in ds]
        json.dump(out, open(sys.argv[sys.argv.index("--golden") + 1], "w"), separators=(",", ":"))
        print("wrote", len(out["easings"]), "curves")
