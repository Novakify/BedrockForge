"""Bedrock Forge impact frames v2 (original, procedural). Manga motifs:
  lines  - dense radial ink speed lines, tapered + rough edges, clear centre (focus lines / 集中線)
  flare  - thin rough cross flare with a spiky core
  spark  - sharp 4-point sparkle that blooms into a diamond shockwave (generic manga sparkle)
White shapes on transparent; the mod tints/inverts them."""
import math, random, sys, os
from PIL import Image, ImageDraw, ImageFilter
W, H, SS = 960, 540, 3
def canvas(): return Image.new("L", (W*SS, H*SS), 0)
def finish(img, blur=0.7):
    img = img.filter(ImageFilter.GaussianBlur(SS*blur)).resize((W, H), Image.LANCZOS)
    out = Image.new("RGBA", (W, H), (255,255,255,0)); out.putalpha(img); return out

def rough_wedge(d, cx, cy, a, r0, r1, w0, rnd):
    # tapered stroke from r1 (thick, outside) to r0 (needle, inside) with jittered edges
    steps = 14; L=[]; R=[]
    for s in range(steps+1):
        t = s/steps; r = r1 + (r0-r1)*t; w = w0*(1-t)**1.3 * (1+rnd.uniform(-.25,.25))
        jx = rnd.uniform(-.004,.004)
        L.append((cx+r*math.cos(a+w+jx), cy+r*math.sin(a+w+jx))); R.append((cx+r*math.cos(a-w+jx), cy+r*math.sin(a-w+jx)))
    d.polygon(L+R[::-1], fill=255)

def lines(seed, density=1.0, clear=0.18):
    img = canvas(); d = ImageDraw.Draw(img); rnd = random.Random(seed)
    cx, cy = W*SS/2 + rnd.uniform(-20,20)*SS, H*SS/2 + rnd.uniform(-12,12)*SS; far = math.hypot(W, H)*SS*0.62
    n = int(260*density)
    for i in range(n):
        a = rnd.uniform(0, 2*math.pi)
        r0 = far*rnd.uniform(clear, clear+0.35)       # where the needle tip stops (clear centre)
        w0 = rnd.uniform(0.004, 0.028)                # angular half-width at the outside
        rough_wedge(d, cx, cy, a, r0, far, w0, rnd)
    return finish(img, 0.5)

def flare(scale, seed):
    img = canvas(); d = ImageDraw.Draw(img); rnd = random.Random(seed); cx, cy = W*SS/2, H*SS/2
    for a, ln in ((0, W*SS*0.55*scale), (math.pi, W*SS*0.55*scale), (math.pi/2, H*SS*0.6*scale), (-math.pi/2, H*SS*0.6*scale)):
        rough_wedge(d, cx, cy, a, 0, ln, 0.012/scale**.3, rnd)
    for i in range(70):  # spiky core
        a = rnd.uniform(0, 2*math.pi); rough_wedge(d, cx, cy, a, 0, rnd.uniform(.03,.13)*H*SS*scale*1.6, rnd.uniform(.03,.12), rnd)
    rc = 0.045*H*SS*scale*1.6; d.ellipse([cx-rc, cy-rc, cx+rc, cy+rc], fill=255)
    return finish(img, 0.6)

def spark(scale, hollow=0.0):
    img = canvas(); d = ImageDraw.Draw(img); cx, cy = W*SS/2, H*SS/2
    def poly(sc):
        pts=[]
        for i in range(1440):
            a = 2*math.pi*i/1440; c, s = math.cos(a), math.sin(a)
            # astroid-like 4-point star: |x|^p + |y|^p = 1 with p<1 gives concave sides
            p = 0.42; r = (abs(c)**p + abs(s)**p) ** (-1/p)
            pts.append((cx + r*c*sc*H*SS*0.5*1.0, cy + r*s*sc*H*SS*0.5))
        return pts
    d.polygon(poly(scale), fill=255)
    if hollow: d.polygon(poly(scale*hollow), fill=0)
    return finish(img, 0.5)

def cross(scale, ring=0.0):
    """Thin needle cross with a small ring at the centre (opening beat of the sparkle sequence)."""
    img = canvas(); d = ImageDraw.Draw(img); rnd = random.Random(9); cx, cy = W*SS/2, H*SS/2
    for a, ln in ((0, W*SS*0.5*scale), (math.pi, W*SS*0.5*scale), (math.pi/2, H*SS*0.62*scale), (-math.pi/2, H*SS*0.62*scale)):
        rough_wedge(d, cx, cy, a, 0, ln, 0.006, rnd)
    if ring:
        r = ring*H*SS; t = r*0.35
        d.ellipse([cx-r, cy-r*0.8, cx+r, cy+r*0.8], fill=255); d.ellipse([cx-r+t, cy-(r-t)*0.8, cx+r-t, cy+(r-t)*0.8], fill=0)
    return finish(img, 0.4)

# ---------------------------------------------------------------- v3: hand-drawn anime sparkle
def _noise1d(seed, octaves=((3, .55), (7, .3), (19, .15))):
    rnd = random.Random(seed); ph = [(f, a, rnd.uniform(0, 6.283)) for f, a in octaves]
    return lambda a: sum(amp * math.sin(f * a + p) for f, amp, p in ph)

def sparkle_poly(cx, cy, rx, ry, p=0.34, rot=0.0, wobble=0.03, seed=1, tip_bias=(1.0, 1.12)):
    """4-point concave sparkle with needle tips. wobble = hand-drawn edge waviness."""
    n = _noise1d(seed); pts = []
    for i in range(1800):
        a = 2*math.pi*i/1800; c, s_ = math.cos(a), math.sin(a)
        r = (abs(c)**p + abs(s_)**p) ** (-1/p)
        r *= 1 + wobble * n(a)
        x, y = r*c*rx*tip_bias[0], r*s_*ry*tip_bias[1]
        xr, yr = x*math.cos(rot) - y*math.sin(rot), x*math.sin(rot) + y*math.cos(rot)
        pts.append((cx + xr, cy + yr))
    return pts

def ink_breaks(d, cx, cy, rmin, rmax, count, seed, width=0.012):
    """Dry-brush gaps: thin dark wedges cut through an outline so it reads hand-inked."""
    rnd = random.Random(seed)
    for _ in range(count):
        a = rnd.uniform(0, 2*math.pi); w = rnd.uniform(.3, 1) * width
        d.polygon([(cx + rmin*math.cos(a-w), cy + rmin*math.sin(a-w)), (cx + rmax*math.cos(a), cy + rmax*math.sin(a)),
                   (cx + rmin*math.cos(a+w), cy + rmin*math.sin(a+w))], fill=0)

def sparkle_v3(i):
    img = canvas(); d = ImageDraw.Draw(img); cx, cy = W*SS/2, H*SS/2; U = H*SS/2; rnd = random.Random(40 + i)
    rot = math.radians([0, 1.5, 3, 4, 5, 6][i])
    if i == 0:   # needle cross + tiny ring: the "tink" before the hit lands
        for a, ln in ((0, W*SS*.48), (math.pi, W*SS*.48), (math.pi/2, H*SS*.58), (-math.pi/2, H*SS*.58)):
            rough_wedge(d, cx, cy, a + rot, 0, ln, 0.005, rnd)
        r = .028*U*2; t = r*.38
        d.ellipse([cx-r, cy-r*.78, cx+r, cy+r*.78], fill=255); d.ellipse([cx-r+t, cy-(r-t)*.78, cx+r-t, cy+(r-t)*.78], fill=0)
    elif i == 1: # compact bright sparkle, needle tips reaching out
        d.polygon(sparkle_poly(cx, cy, U*.55, U*.62, p=.30, rot=rot, seed=11), fill=255)
        for a in (0, math.pi, math.pi/2, -math.pi/2):
            rough_wedge(d, cx, cy, a + rot, U*.4, U*1.25 if a in (0, math.pi) else U*1.0, 0.004, rnd)
    elif i == 2: # full sparkle, small hollow core = the impact point
        d.polygon(sparkle_poly(cx, cy, U*1.25, U*1.05, p=.33, rot=rot, seed=12), fill=255)
        d.polygon(sparkle_poly(cx, cy, U*.10, U*.09, p=.45, rot=rot, seed=13, wobble=0.06), fill=0)
    elif i == 3: # sparkle hollowing out: thick inked outline
        d.polygon(sparkle_poly(cx, cy, U*2.0, U*1.6, p=.36, rot=rot, seed=14), fill=255)
        d.polygon(sparkle_poly(cx, cy, U*1.45, U*1.15, p=.36, rot=rot, seed=15, wobble=.05), fill=0)
        ink_breaks(d, cx, cy, U*.9, U*2.4, 7, 16)
    elif i == 4: # diamond shockwave, outline thinning
        d.polygon(sparkle_poly(cx, cy, U*2.9, U*2.3, p=.42, rot=rot, seed=17), fill=255)
        d.polygon(sparkle_poly(cx, cy, U*2.55, U*2.05, p=.42, rot=rot, seed=18, wobble=.04), fill=0)
        ink_breaks(d, cx, cy, U*1.4, U*3.4, 12, 19, .02)
    else:        # breaking apart at the screen edges
        d.polygon(sparkle_poly(cx, cy, U*3.7, U*2.9, p=.48, rot=rot, seed=20), fill=255)
        d.polygon(sparkle_poly(cx, cy, U*3.5, U*2.78, p=.48, rot=rot, seed=21, wobble=.05), fill=0)
        ink_breaks(d, cx, cy, U*1.8, U*4.5, 26, 22, .035)
    return finish(img, 0.45)

if __name__ == "__main__":
    out = sys.argv[1]
    for name, frames in {
        "lines": [lines(1, .6, .30), lines(2, 1.0, .20), lines(3, 1.2, .14)],
        "flare": [flare(.35, 5), flare(.7, 6), flare(1.0, 7)],
        "spark": [sparkle_v3(i) for i in range(6)],
    }.items():
        os.makedirs(f"{out}/{name}", exist_ok=True)
        for i, f in enumerate(frames): f.save(f"{out}/{name}/frame_{i}.png")
