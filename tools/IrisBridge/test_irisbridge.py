#!/usr/bin/env python3
"""IrisBridge unit tests: python3 test_irisbridge.py"""
import sys; sys.path.insert(0, __import__("os").path.dirname(__file__))
from expr import build, VM
from pack import conditionals, targets

bad = n = 0
def check(name, got, want):
    global bad, n; n += 1
    if (abs(got - want) > 1e-6) if isinstance(want, (int, float)) and isinstance(got, (int, float)) else got != want:
        bad += 1; print("FAIL", name, got, "want", want)

def ev(e, inp={}, uni=None, steps=1, dt=0.05):
    vm = VM(build(uni or [("float", "r", e, False)]))
    for _ in range(steps): r = vm.run(inp, dt)
    return r

for e, i, w in [("1+2*3", {}, 7), ("-(2+3)*2", {}, -10), ("if(x>1, 5, x<0, -5, 0)", {"x": 3}, 5), ("if(x>1, 5, x<0, -5, 0)", {"x": -1}, -5),
                ("if(x>1, 5, x<0, -5, 0)", {"x": 0.5}, 0), ("in(b, 1, 3, 7)", {"b": 3}, 1), ("in(b, 1, 3, 7)", {"b": 4}, 0),
                ("clamp(x,0,1)", {"x": 4}, 1), ("frac(-0.25)", {}, 0.75), ("max(1,5,3)", {}, 5), ("frameCounter % 8", {"frameCounter": 13}, 5),
                ("equals(a,b,0.01)", {"a": 1, "b": 1.005}, 1), ("!(1 && 0) || 0", {}, 1), ("cameraPosition.y - 64", {"cameraPosition.y": 70}, 6),
                ("pow(2,10)", {}, 1024), ("abs(-3)+floor(2.7)+ceil(2.1)", {}, 8), ("1 < 2 && 3 >= 3", {}, 1), ("2 == 2 != 0", {}, 1)]:
    check(e, ev(e, i)["r"], w)
vm = VM(build([("float", "r", "smooth(7, x, 2, 2)", False)])); vm.run({"x": 0}, 0.05)
for _ in range(40): r = vm.run({"x": 1}, 0.05)
check("smooth half-life T/10", r["r"], 1 - 2 ** -10)
check("vec var + swizzle", ev(None, uni=[("vec3", "v", "vec3(1,2,3) * 2", True), ("float", "r", "v.y + v.z", False)])["r"], 10)
r = ev(None, uni=[("vec2", "p", "eyeBrightness / 240.0", False)], inp={"eyeBrightness.x": 120, "eyeBrightness.y": 240})
check("vec input", (r["p.x"], r["p.y"]), (0.5, 1.0))
check("variables not exported", "v" in ev(None, uni=[("float", "v", "1", True), ("float", "r", "v", False)]), False)
# properties preprocessor: conditionals only, no macro expansion of bodies
src = "#if A == 2\nprofile.X = A=4 !B\n#elif defined C\nc=1\n#else\nd=1\n#endif\n#ifdef D\ne=1\n#endif"
check("cond if", conditionals(src, {"A": "2"}).split(), ["profile.X", "=", "A=4", "!B"])
check("cond elif", conditionals(src, {"A": "1", "C": ""}).split(), ["c=1"])
check("cond else", conditionals(src, {"A": "1"}).split(), ["d=1"])
check("cond nested define", conditionals("#define Q 3\n#if Q > 2 && !defined Z\nok\n#endif", {}).split(), ["ok"])
check("targets RT", targets("/* RENDERTARGETS: 0,3,6 */"), [0, 3, 6])
check("targets DB", targets("/* DRAWBUFFERS:03A */"), [0, 3, 10])
print(f"{n - bad}/{n} passed"); sys.exit(1 if bad else 0)
