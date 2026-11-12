#!/usr/bin/env python3
"""IrisBridge custom uniforms: OptiFine/Iris expression language -> scalar stack bytecode (run per frame by RF).

Each uniform/variable component compiles to a flat program of (op, arg) pairs; vectors are split into components.
smooth() keeps per-call state; inputs are named builtin scalars ("cameraPosition.x", "eyeBrightness.y", "biome"...).
"""
from __future__ import annotations
import math, re
from dataclasses import dataclass, field

OPS = ["CONST", "INPUT", "VAR", "NEG", "NOT", "ADD", "SUB", "MUL", "DIV", "MOD", "LT", "GT", "LE", "GE", "EQ", "NE", "AND", "OR",
       "CALL", "SMOOTH", "SELECT"]
FUNCS = {  # name: (argc or -1 variadic, python impl)
    "sin": (1, math.sin), "cos": (1, math.cos), "tan": (1, math.tan), "asin": (1, lambda x: math.asin(max(-1, min(1, x)))),
    "acos": (1, lambda x: math.acos(max(-1, min(1, x)))), "atan": (1, math.atan), "atan2": (2, math.atan2),
    "torad": (1, math.radians), "todeg": (1, math.degrees), "abs": (1, abs), "floor": (1, math.floor), "ceil": (1, math.ceil),
    "round": (1, lambda x: math.floor(x + 0.5)), "frac": (1, lambda x: x - math.floor(x)), "exp": (1, math.exp),
    "exp2": (1, lambda x: 2.0 ** x), "log": (1, lambda x: math.log(x) if x > 0 else -1e30), "log2": (1, lambda x: math.log2(x) if x > 0 else -1e30),
    "sqrt": (1, lambda x: math.sqrt(max(0.0, x))), "pow": (2, lambda a, b: math.pow(a, b) if a >= 0 or float(b).is_integer() else 0.0),
    "signum": (1, lambda x: (x > 0) - (x < 0)), "fmod": (2, lambda a, b: math.fmod(a, b) if b else 0.0),
    "min": (-1, min), "max": (-1, max), "clamp": (3, lambda x, a, b: max(a, min(b, x))),
    "between": (3, lambda x, a, b: 1.0 if a <= x <= b else 0.0),
    "equals": (3, lambda a, b, e: 1.0 if abs(a - b) <= e else 0.0),
    "in": (-1, lambda x, *vals: 1.0 if x in vals else 0.0),
    "random": (0, None),   # runtime: per-frame random 0..1
}
FN_ID = {n: i for i, n in enumerate(FUNCS)}
CONSTS = {"pi": math.pi, "true": 1.0, "false": 0.0}
VEC = {"vec2": 2, "vec3": 3, "vec4": 4}
SWZ = {"x": 0, "y": 1, "z": 2, "w": 3, "r": 0, "g": 1, "b": 2, "a": 3, "s": 0, "t": 1}

# builtin vector inputs (Iris names) and their sizes - anything else is a scalar input
VEC_INPUTS = {"cameraPosition": 3, "previousCameraPosition": 3, "eyePosition": 3, "relativeEyePosition": 3, "playerLookVector": 3,
              "playerBodyVector": 3, "skyColor": 3, "fogColor": 3, "sunPosition": 3, "moonPosition": 3, "shadowLightPosition": 3,
              "upPosition": 3, "eyeBrightness": 2, "eyeBrightnessSmooth": 2, "atlasSize": 2, "terrainIconSize": 2,
              "lightningBoltPosition": 4, "entityColor": 4, "spriteBounds": 4}
# Iris biome / precipitation constants (BIOME_* resolved by the runtime's biome table; precipitation: none=0 rain=1 snow=2)
NAMED = {"PPT_NONE": 0, "PPT_RAIN": 1, "PPT_SNOW": 2}


class ExprError(Exception): pass


def tokenize(s: str):
    return re.findall(r"\d+\.\d*(?:[eE][-+]?\d+)?|\.\d+(?:[eE][-+]?\d+)?|\d+(?:[eE][-+]?\d+)?|[A-Za-z_]\w*|&&|\|\||==|!=|<=|>=|[-+*/%<>!(),.]", s)


@dataclass
class Compiler:
    known: dict[str, int] = field(default_factory=dict)      # computed uniform/variable component name -> VAR slot
    n_smooth: int = 0
    inputs: dict[str, int] = field(default_factory=dict)
    biome_ids: dict[str, int] = field(default_factory=dict)

    def compile(self, expr: str, ncomp: int = 1) -> list[list[tuple[int, float]]]:
        self.t, self.i = tokenize(expr), 0
        trees = self.expr()
        if self.i != len(self.t): raise ExprError(f"unexpected '{self.t[self.i]}' in {expr!r}")
        if len(trees) == 1 and ncomp > 1: trees = trees * ncomp
        if len(trees) != ncomp: raise ExprError(f"{expr!r}: {len(trees)} components, want {ncomp}")
        return [self.emit(tr) for tr in trees]

    # --- parser: returns list of scalar trees (one per component) ---
    def peek(self): return self.t[self.i] if self.i < len(self.t) else None
    def take(self, tok=None):
        t = self.peek()
        if tok and t != tok: raise ExprError(f"expected {tok}, got {t}")
        self.i += 1; return t

    def binary(self, nxt, ops):
        a = nxt()
        while self.peek() in ops:
            op = self.take(); b = nxt()
            if len(a) != len(b):
                if len(a) == 1: a = a * len(b)
                elif len(b) == 1: b = b * len(a)
                else: raise ExprError("vector size mismatch")
            a = [(op, x, y) for x, y in zip(a, b)]
        return a

    def expr(self): return self.binary(self.and_, ("||",))
    def and_(self): return self.binary(self.eq, ("&&",))
    def eq(self): return self.binary(self.rel, ("==", "!="))
    def rel(self): return self.binary(self.add, ("<", ">", "<=", ">="))
    def add(self): return self.binary(self.mul, ("+", "-"))
    def mul(self): return self.binary(self.unary, ("*", "/", "%"))

    def unary(self):
        if self.peek() == "-": self.take(); return [("neg", x) for x in self.unary()]
        if self.peek() == "+": self.take(); return self.unary()
        if self.peek() == "!": self.take(); return [("not", x) for x in self.unary()]
        return self.postfix()

    def postfix(self):
        v = self.primary()
        while self.peek() == ".":
            self.take(); sw = self.take()
            v = [v[SWZ[c]] for c in sw]
        return v

    def args(self):
        self.take("("); out = []
        if self.peek() != ")":
            out.append(self.expr())
            while self.peek() == ",": self.take(); out.append(self.expr())
        self.take(")"); return out

    def primary(self):
        t = self.take()
        if t is None: raise ExprError("unexpected end")
        if t == "(":
            v = self.expr(); self.take(")"); return v
        if re.match(r"[\d.]", t): return [("c", float(t))]
        if t in CONSTS: return [("c", CONSTS[t])]
        if t in NAMED: return [("c", float(NAMED[t]))]
        if t.startswith("BIOME_"): return [("c", float(self.biome_ids.get(t, -1)))]
        if self.peek() == "(":
            a = self.args()
            if t in VEC:
                comps = [c for x in a for c in x]
                if len(comps) == 1: comps *= VEC[t]
                return comps
            if t == "if":                    # if(c1, v1, c2, v2, ..., else)
                return self._if(a)
            if t == "smooth":                # smooth([id,] x, [up, [down]])
                if len(a) == 4: a = a[1:]
                elif len(a) == 2 and False: pass
                x = a[0]; up = a[1] if len(a) > 1 else [("c", 1.0)]; dn = a[2] if len(a) > 2 else up
                outs = []
                for k, xc in enumerate(x):
                    outs.append(("smooth", self.n_smooth, xc, up[0], dn[0])); self.n_smooth += 1
                return outs
            if t not in FUNCS: raise ExprError(f"unknown function {t}")
            n = max(len(x) for x in a) if a else 1
            a = [x * n if len(x) == 1 else x for x in a]
            return [("call", t, [x[k] for x in a]) for k in range(n)]
        if t in self.known_vec():
            return [("var", self.known[f"{t}.{'xyzw'[k]}"]) for k in range(self.known_vec()[t])]
        if t in self.known: return [("var", self.known[t])]
        if t in VEC_INPUTS: return [("in", self.inp(f"{t}.{'xyzw'[k]}")) for k in range(VEC_INPUTS[t])]
        return [("in", self.inp(t))]

    def _if(self, a):
        if len(a) % 2 == 0: raise ExprError("if() needs an else value")
        out = a[-1]
        for k in range(len(a) - 3, -1, -2):
            c, v = a[k], a[k + 1]
            n = max(len(v), len(out)); v = v * n if len(v) == 1 else v; out = out * n if len(out) == 1 else out
            out = [("sel", c[0], vv, oo) for vv, oo in zip(v, out)]
        return out

    def known_vec(self):
        vs = {}
        for k in self.known:
            if "." in k: b, c = k.split("."); vs[b] = max(vs.get(b, 0), "xyzw".index(c) + 1)
        return vs

    def inp(self, name):
        if name not in self.inputs: self.inputs[name] = len(self.inputs)
        return self.inputs[name]

    # --- codegen: postfix (op, arg) ---
    def emit(self, n, out=None):
        out = [] if out is None else out
        k = n[0]
        if k == "c": out.append((OPS.index("CONST"), n[1]))
        elif k == "in": out.append((OPS.index("INPUT"), n[1]))
        elif k == "var": out.append((OPS.index("VAR"), n[1]))
        elif k == "neg": self.emit(n[1], out); out.append((OPS.index("NEG"), 0))
        elif k == "not": self.emit(n[1], out); out.append((OPS.index("NOT"), 0))
        elif k == "sel": self.emit(n[1], out); self.emit(n[2], out); self.emit(n[3], out); out.append((OPS.index("SELECT"), 0))
        elif k == "smooth":
            self.emit(n[2], out); self.emit(n[3], out); self.emit(n[4], out); out.append((OPS.index("SMOOTH"), n[1]))
        elif k == "call":
            for x in n[2]: self.emit(x, out)
            out.append((OPS.index("CALL"), FN_ID[n[1]] * 256 + len(n[2])))
        else:
            self.emit(n[1], out); self.emit(n[2], out)
            out.append((OPS.index({"+": "ADD", "-": "SUB", "*": "MUL", "/": "DIV", "%": "MOD", "<": "LT", ">": "GT", "<=": "LE",
                                   ">=": "GE", "==": "EQ", "!=": "NE", "&&": "AND", "||": "OR"}[k]), 0))
        return out


TYPES = {"float": 1, "int": 1, "bool": 1, "vec2": 2, "vec3": 3, "vec4": 4, "ivec2": 2, "ivec3": 3}


def build(uniforms, biome_ids=None):
    """uniforms: [(type, name, expr, is_variable)] in file order -> bundle dict for the runtime."""
    c = Compiler(biome_ids=biome_ids or {})
    slots, progs = [], []
    for ty, name, ex, is_var in uniforms:
        n = TYPES.get(ty, 1)
        code = c.compile(ex, n)
        for k in range(n):
            key = name if n == 1 else f"{name}.{'xyzw'[k]}"
            c.known[key] = len(slots)
            slots.append({"name": key, "uniform": name, "comp": k, "type": ty, "export": not is_var})
            progs.append(code[k])
    return {"inputs": sorted(c.inputs, key=c.inputs.get), "slots": slots, "code": progs, "smooth_states": c.n_smooth,
            "ops": OPS, "funcs": list(FUNCS)}


class VM:
    """Reference interpreter (the C++ runtime mirrors this)."""
    def __init__(self, bundle):
        self.b = bundle; self.vars = [0.0] * len(bundle["slots"]); self.sm = [None] * bundle["smooth_states"]

    def run(self, inputs: dict[str, float], dt: float):
        inp = [float(inputs.get(n, 0.0)) for n in self.b["inputs"]]
        fl = list(FUNCS.values())
        for si, code in enumerate(self.b["code"]):
            st = []
            for op, arg in code:
                o = OPS[op]
                if o == "CONST": st.append(arg)
                elif o == "INPUT": st.append(inp[int(arg)])
                elif o == "VAR": st.append(self.vars[int(arg)])
                elif o == "NEG": st.append(-st.pop())
                elif o == "NOT": st.append(0.0 if st.pop() else 1.0)
                elif o == "SELECT": e = st.pop(); v = st.pop(); c = st.pop(); st.append(v if c else e)
                elif o == "SMOOTH":
                    dn = st.pop(); up = st.pop(); x = st.pop(); k = int(arg); prev = self.sm[k]
                    if prev is None: cur = x
                    else:
                        T = up if x > prev else dn
                        cur = x if T <= 0 else prev + (x - prev) * (1.0 - math.pow(2.0, -dt / (0.1 * T)))  # Iris: half-life = T/10
                    self.sm[k] = cur; st.append(cur)
                elif o == "CALL":
                    fid, argc = divmod(int(arg), 256); a = st[len(st) - argc:]; del st[len(st) - argc:]
                    f = fl[fid][1]; st.append(0.5 if f is None else float(f(*a)))
                else:
                    b = st.pop(); a = st.pop()
                    st.append({"ADD": a + b, "SUB": a - b, "MUL": a * b, "DIV": a / b if b else 0.0, "MOD": math.fmod(a, b) if b else 0.0,
                               "LT": float(a < b), "GT": float(a > b), "LE": float(a <= b), "GE": float(a >= b), "EQ": float(a == b),
                               "NE": float(a != b), "AND": float(bool(a) and bool(b)), "OR": float(bool(a) or bool(b))}[o])
            self.vars[si] = st[-1] if st else 0.0
        return {s["name"]: self.vars[i] for i, s in enumerate(self.b["slots"]) if s["export"]}
