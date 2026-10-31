"""GeckoBridge animation core: GeckoLib .animation.json -> Bedrock actor animation.

Both formats share the Bedrock 1.8.0 schema, so geometry, bone names, rotation signs, pixel units and
loop modes carry over as they are. What does NOT carry over is how a keyframe is interpolated:

  GeckoLib: every keyframe may carry "easing" (30+ curves) and "easingArgs"; the easing of keyframe k
            shapes the segment that ENDS at k (GeckoLib 5 ActorBoneAnimationKeyframe.bakeTriplet,
            GeckoLib 4 BakedAnimationsAdapter.buildKeyframeStack). Values are {"vector": [...]} objects.
  Bedrock:  keyframes are plain [x,y,z] (or {"pre","post","lerp_mode"}); only linear and catmullrom.

So the converter BAKES every non-linear segment into short linear segments, sampled with GeckoLib's own
easing maths (tools/ModelCore/interp.py, ported from GeckoLib's EasingType, MIT, (c) GeckoLib / Tslat).
`gl_eval` and `bedrock_eval` evaluate both sides so the error of the bake can be measured, not guessed.
"""
import math, re, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

TOL = {"rotation": 0.05, "position": 0.005, "scale": 0.001}  # max bake error: degrees / pixels / factor
EPS = 1e-5  # GeckoLib uses Mth.EPSILON for pre/post; anything tiny works for evaluation

from ModelCore.interp import easing_fn, catmull, EASINGS  # noqa: E402  (shared maths, see tools/ModelCore)

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
    return f"math.lerp({a}, {b}, {w:.6f})"

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

_UNIT_FN = {"math.to_rad": "0.0174532925", "math.to_deg": "57.2957795"}  # GeckoLib-only functions

def _rewrite_unit_fns(e, report):
    low = e.lower()
    for fn, k in _UNIT_FN.items():
        while fn + "(" in low:
            i = low.index(fn + "("); j = i + len(fn) + 1; depth = 1
            while j < len(e) and depth:
                depth += {"(": 1, ")": -1}.get(e[j], 0); j += 1
            inner = e[i + len(fn) + 1:j - 1]
            e = e[:i] + f"(({inner}) * {k})" + e[j:]; low = e.lower()
            report.setdefault("molang_mapped", set()).add(fn)
    return e

def fix_molang(expr, report):
    if not isinstance(expr, str): return expr
    expr = _rewrite_unit_fns(expr, report)
    def sub(m):
        k = m.group(0).lower()
        if k in MOLANG_MAP:
            report.setdefault("molang_mapped", set()).add(k); return MOLANG_MAP[k]
        return m.group(0)
    e = _ident.sub(sub, expr)
    # GeckoLib's MathParser deletes ALL whitespace before parsing, so "q.anim_time * 10 5" means "* 105" in Java.
    # Bedrock instead rejects it and drops the whole file's animations (ModPorter E-023). Do what GeckoLib does.
    joined = re.sub(r"(\d)\s+(?=\d)", r"\1", e)
    if joined != e:
        report.setdefault("molang_whitespace_joined", []).append(expr); e = joined
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
