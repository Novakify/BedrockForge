"""GeckoBridge animation core: GeckoLib .animation.json -> Bedrock actor animation.

Both formats share the Bedrock 1.8.0 schema, so geometry, bone names, rotation signs, pixel units and
loop modes carry over as they are. What does NOT carry over is how a keyframe is interpolated:

  GeckoLib: every keyframe may carry "easing" (30+ curves) and "easingArgs"; the easing of keyframe k
            shapes the segment that ENDS at k (GeckoLib 5 ActorBoneAnimationKeyframe.bakeTriplet,
            GeckoLib 4 BakedAnimationsAdapter.buildKeyframeStack). Values are {"vector": [...]} objects.
  Bedrock:  keyframes are plain [x,y,z] (or {"pre","post","lerp_mode"}); only linear and catmullrom.

So the converter BAKES every non-linear segment into short linear segments (sampled with GeckoLib's own
easing maths, ported below from com.geckolib.animation.object.EasingType, MIT, (c) GeckoLib / Tslat).
`gl_eval` and `bedrock_eval` evaluate both sides so the error of the bake can be measured, not guessed.
"""
import math, re

TOL = {"rotation": 0.05, "position": 0.005, "scale": 0.001}  # max bake error: degrees / pixels / factor
EPS = 1e-5  # GeckoLib uses Mth.EPSILON for pre/post; anything tiny works for evaluation

# ---------------------------------------------------------------- GeckoLib easing maths (ported) ----
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

# ---------------------------------------------------------------- parsing -----------------------------
class KF:
    """One baked GeckoLib keyframe (after pre/post expansion), per axis value may be float or molang str."""
    __slots__ = ("t", "v", "easing", "args")
    def __init__(self, t, v, easing, args): self.t, self.v, self.easing, self.args = t, v, easing, args

def _vec(x):
    if isinstance(x, dict): x = x.get("vector")
    if x is None: return None
    if not isinstance(x, list): x = [x, x, x]
    if len(x) == 1: x = [x[0]] * 3
    return [float(a) if isinstance(a, (int, float)) else a for a in x[:3]]

def _kf_list(entry):
    """GeckoLib channel value -> (list[KF], single). Mirrors ActorBoneAnimationEntry/Keyframe bake order."""
    if not isinstance(entry, dict) or "vector" in entry:
        return [KF(0.0, _vec(entry), "linear", None)], True
    items = sorted(((float(t), kf) for t, kf in entry.items()), key=lambda p: p[0])
    out = []
    force_linear = len(items) == 1
    for t, kf in items:
        if isinstance(kf, dict):
            ease = kf.get("lerp_mode", kf.get("easing")) or "linear"
            args = kf.get("easingArgs")
            arg0 = args[0] if args else None
            if force_linear: ease = "linear"
            if "pre" in kf and _vec(kf["pre"]) is not None:
                p = kf["pre"]; pe = (p.get("easing") if isinstance(p, dict) else None) or ease
                out.append(KF(t - EPS, _vec(p), pe, arg0))
            if "vector" in kf:
                out.append(KF(t, _vec(kf["vector"]), ease, arg0))
            if "post" in kf and _vec(kf["post"]) is not None:
                p = kf["post"]; pe = (p.get("easing") if isinstance(p, dict) else None) or ease
                out.append(KF(t + EPS, _vec(p), pe, arg0))
        else:
            out.append(KF(t, _vec(kf), "linear", None))
    return out, False

def parse_animation(anim):
    """-> {bone: {channel: (list[KF], single)}}"""
    res = {}
    for bone, b in (anim.get("bones") or {}).items():
        for ch in ("rotation", "position", "scale"):
            if ch in b: res.setdefault(bone, {})[ch] = _kf_list(b[ch])
    return res

# ---------------------------------------------------------------- evaluation (constants only) -------
def gl_eval(kfs, t, axis):
    """GeckoLib's value at time t for one axis (numbers only)."""
    if t <= kfs[0].t: return kfs[0].v[axis]
    for i in range(1, len(kfs)):
        k = kfs[i]
        if t <= k.t:
            a, b = kfs[i - 1].v[axis], k.v[axis]
            d = (t - kfs[i - 1].t) / max(k.t - kfs[i - 1].t, 1e-12)
            if d >= 1: return b  # EasingType.apply: delta >= 1 -> toValue
            if k.easing.lower() == "catmullrom":
                p0 = kfs[i - 2].v[axis] if i >= 2 else a
                p3 = kfs[i + 1].v[axis] if i + 1 < len(kfs) else b
                return catmull(d, p0, a, b, p3)
            return a + (b - a) * easing_fn(k.easing, k.args)(d)
    return kfs[-1].v[axis]

def bedrock_eval(chan, t, axis):
    """Bedrock-side value at t for a channel written by `bake_channel` (linear + pre/post only)."""
    if isinstance(chan, list): return chan[axis]
    pts = []
    for ts, kf in sorted(chan.items(), key=lambda p: float(p[0])):
        tt = float(ts)
        if isinstance(kf, list): pts.append((tt, kf, kf))
        else: pts.append((tt, kf.get("pre", kf.get("post")), kf.get("post", kf.get("pre"))))
    if t <= pts[0][0]: return pts[0][1][axis]
    for i in range(1, len(pts)):
        if t <= pts[i][0]:
            a, b = pts[i - 1][2][axis], pts[i][1][axis]
            d = (t - pts[i - 1][0]) / max(pts[i][0] - pts[i - 1][0], 1e-12)
            return a + (b - a) * d
    return pts[-1][2][axis]

# ---------------------------------------------------------------- baking ------------------------------
def _fmt(x):
    if isinstance(x, str): return x
    r = round(x, 5)
    return 0.0 if r == 0 else r

def _lerp_expr(a, b, w):
    if not isinstance(a, str) and not isinstance(b, str): return a + (b - a) * w
    if w <= 0: return a
    if w >= 1: return b
    return f"math.lerp({a}, {b}, {round(w, 6)})"

def bake_channel(kfs, single, tol=0.05, min_samples=2, stats=None):
    """list[KF] -> Bedrock channel value. Non-linear segments are sampled with GeckoLib's curve."""
    if single:
        return [_fmt(a) for a in kfs[0].v]
    out = {}  # time(str) -> [x,y,z] or {"pre","post"}

    def put(t, v):
        key = f"{max(t, 0.0):.5f}".rstrip("0").rstrip(".") or "0"
        if "." not in key: key += ".0"
        v = [_fmt(a) for a in v]
        if key in out:  # same instant twice -> discontinuity (pre/post or step jump)
            prev = out[key]
            pre = prev["pre"] if isinstance(prev, dict) else prev
            out[key] = {"pre": pre, "post": v} if pre != v else pre
        else:
            out[key] = v

    put(round(max(kfs[0].t, 0.0), 4), kfs[0].v)
    for i in range(1, len(kfs)):
        A, B = kfs[i - 1], kfs[i]
        ease = B.easing.lower()
        t0, t1 = round(A.t, 4), round(B.t, 4)
        dur = t1 - t0
        if dur <= 0:  # pre/post pair -> jump at the same time
            put(t1, B.v); continue
        if ease in ("linear", "none") or all(A.v[k] == B.v[k] and not isinstance(A.v[k], str) for k in range(3)):
            put(t1, B.v); continue
        if stats is not None: stats[ease] = stats.get(ease, 0) + 1
        if ease == "step":
            steps = int(B.args or 2); f = easing_fn("step", steps)
            for s in range(1, steps):
                ts = t0 + dur * s / steps
                w_prev, w = f(s / steps - 1e-9), f(s / steps + 1e-9)
                put(ts, [_lerp_expr(A.v[k], B.v[k], w_prev) for k in range(3)])
                put(ts, [_lerp_expr(A.v[k], B.v[k], w) for k in range(3)])
            put(t1 - 1e-4, [_lerp_expr(A.v[k], B.v[k], f(1 - 1e-9)) for k in range(3)])
            put(t1, B.v); continue
        if ease == "catmullrom":
            P0 = kfs[i - 2].v if i >= 2 else A.v
            P3 = kfs[i + 1].v if i + 1 < len(kfs) else B.v
            def val(d, k, P0=P0, A=A, B=B, P3=P3):
                if all(not isinstance(x[k], str) for x in (P0, A.v, B.v, P3)):
                    return catmull(d, P0[k], A.v[k], B.v[k], P3[k])
                return None
            wf = lambda d: d
        else:
            f = easing_fn(ease, B.args)
            def val(d, k, A=A, B=B, f=f):
                if isinstance(A.v[k], str) or isinstance(B.v[k], str): return None
                return A.v[k] + (B.v[k] - A.v[k]) * f(d)
            wf = f
        # adaptive: split [0,1] until straight lines between samples stay within tol of the curve
        def curve(d):
            return [val(d, k) for k in range(3)]
        def err(d0, d1):
            worst = 0.0
            for q in (0.25, 0.5, 0.75):
                dm = d0 + (d1 - d0) * q
                c0, c1, cm = curve(d0), curve(d1), curve(dm)
                for k in range(3):
                    if cm[k] is None:  # molang channel: judge the weight curve instead
                        w0, w1, wm = wf(d0), wf(d1), wf(dm); worst = max(worst, abs(w0 + (w1 - w0) * q - wm) * 50)
                    else:
                        worst = max(worst, abs(c0[k] + (c1[k] - c0[k]) * q - cm[k]))
            return worst
        pts = [0.0, 1.0]; depth = 0
        while depth < 12:
            nxt = [pts[0]]; split = False
            for d0, d1 in zip(pts, pts[1:]):
                if err(d0, d1) > tol: nxt.append((d0 + d1) / 2); split = True
                nxt.append(d1)
            pts = nxt; depth += 1
            if not split: break
        if len(pts) - 1 < min_samples:
            pts = [j / min_samples for j in range(min_samples + 1)]
        for d in pts[1:]:
            ts = t0 + dur * d
            c = curve(d); w = wf(d)
            put(ts, [c[k] if c[k] is not None else _lerp_expr(A.v[k], B.v[k], w) for k in range(3)])
    return out

# ---------------------------------------------------------------- molang -----------------------------
# GeckoLib-only names -> nearest Bedrock query (see docs/PORT_GECKOBRIDGE.md "Molang")
MOLANG_MAP = {
    "query.limb_swing": "query.modified_distance_moved", "q.limb_swing": "query.modified_distance_moved",
    "query.limb_swing_amount": "query.modified_move_speed", "q.limb_swing_amount": "query.modified_move_speed",
    "query.controller_speed": "1.0", "q.controller_speed": "1.0",
}
_ident = re.compile(r"\b(?:query|q)\.[a-z_][a-z0-9_]*", re.I)

def fix_molang(expr, report):
    if not isinstance(expr, str): return expr
    def sub(m):
        k = m.group(0).lower()
        if k in MOLANG_MAP:
            report.setdefault("molang_mapped", set()).add(k); return MOLANG_MAP[k]
        return m.group(0)
    e = _ident.sub(sub, expr)
    # ModPorter E-023: a lost operator ("q.anim_time * 10 5") makes Bedrock drop the whole animation
    if re.search(r"[0-9a-z_)]\s+[0-9(]", e, re.I) and not re.search(r"\b(return|loop|for_each)\b", e):
        report.setdefault("molang_suspect", []).append(expr)
    return e

def convert_animation(name, anim, new_name, rate=30.0, report=None):
    report = report if report is not None else {}
    stats = report.setdefault("baked_segments", {})
    out = {}
    if "loop" in anim:
        lp = anim["loop"]
        out["loop"] = lp if lp in (True, False, "hold_on_last_frame") else (lp == "loop")
    for k in ("animation_length", "anim_time_update", "blend_weight", "start_delay", "loop_delay",
              "override_previous_animation"):
        if k in anim: out[k] = anim[k]
    bones = {}
    for bone, chans in parse_animation(anim).items():
        for ch, (kfs, single) in chans.items():
            for kf in kfs: kf.v = [fix_molang(x, report) for x in kf.v]
            bones.setdefault(bone, {})[ch] = bake_channel(kfs, single, TOL[ch], stats=stats)
        if "relative_to" in (anim["bones"][bone] or {}):
            bones[bone]["relative_to"] = anim["bones"][bone]["relative_to"]
    out["bones"] = bones
    for k in ("sound_effects", "particle_effects", "timeline"):
        if anim.get(k): report.setdefault("dropped_markers", {}).setdefault(k, 0); report["dropped_markers"][k] += len(anim[k])
    return {new_name: out}

def measure_error(anim, converted, hz=240.0):
    """Max |GeckoLib - Bedrock| per channel type over the animation (constant keyframes only)."""
    length = anim.get("animation_length")
    errs = {"rotation": 0.0, "position": 0.0, "scale": 0.0}; skipped = 0
    for bone, chans in parse_animation(anim).items():
        for ch, (kfs, single) in chans.items():
            if any(isinstance(a, str) for k in kfs for a in k.v): skipped += 1; continue
            L = length or kfs[-1].t
            bc = converted["bones"][bone][ch]
            jumps = [k.t for k in kfs] + ([float(ts) for ts, v in bc.items() if isinstance(v, dict)] if isinstance(bc, dict) else [])
            for s in range(int(L * hz) + 1):
                t = s / hz
                if any(abs(t - j) < 2e-4 for j in jumps): continue  # value is two-sided at a pre/post jump
                for ax in range(3):
                    e = abs(gl_eval(kfs, t, ax) - bedrock_eval(bc, t, ax))
                    if e > errs[ch]: errs[ch] = e
    return {k: round(v, 4) for k, v in errs.items()}, skipped
