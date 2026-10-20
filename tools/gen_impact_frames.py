"""Original Bedrock Forge impact frame sets (procedural, not derived from any other mod's art).
Styles:  star  - smooth 8-point concave star that blooms into a star-shaped shockwave
         ring  - crisp expanding shockwave ring with thin radial cracks
         ink   - smooth ink-blot burst (organic, manga splash)
Each frame is a white shape on a transparent background (tinted in game).
usage: python gen_impact_frames.py OUT_DIR [style]"""
import math, random, sys
from PIL import Image, ImageDraw, ImageFilter
S, SS = 512, 4
N = S * SS; C = N / 2; R = N / 2

def star(scale, points=8, depth=0.80, rot=0.0):
    pts = []
    for i in range(720):
        a = 2 * math.pi * i / 720
        # smooth concave star: radius dips between tips with a sharpened cosine
        k = abs(math.cos(points * (a + rot) / 2))
        r = scale * R * (1 - depth * (1 - k ** 14))
        pts.append((C + r * math.cos(a), C + r * math.sin(a)))
    return pts

def blob(scale, seed, lobes=9, wob=0.22):
    rnd = random.Random(seed); ph = [rnd.uniform(0, 6.3) for _ in range(4)]; am = [rnd.uniform(.4, 1) for _ in range(4)]
    pts = []
    for i in range(720):
        a = 2 * math.pi * i / 720
        w = sum(am[j] * math.sin((lobes + j * 3) * a + ph[j]) for j in range(4)) / 4
        r = scale * R * (1 + wob * w)
        pts.append((C + r * math.cos(a), C + r * math.sin(a)))
    return pts

def frame(style, i):
    img = Image.new("L", (N, N), 0); d = ImageDraw.Draw(img)
    if style == "star":
        sc = [0.25, 0.6, 0.95, 1.25, 1.45][i]
        d.polygon(star(sc), fill=255)
        if i >= 2: d.polygon(star(sc * [0, 0, .35, .62, .8][i], points=8, depth=0.80), fill=0)   # hollow out -> shockwave
    elif style == "ring":
        r_out = [0.16, 0.38, 0.62, 0.84, 0.98][i] * R; th = [0.16, 0.12, 0.08, 0.05, 0.03][i] * R
        d.ellipse([C - r_out, C - r_out, C + r_out, C + r_out], fill=255)
        if i > 0: d.ellipse([C - r_out + th, C - r_out + th, C + r_out - th, C + r_out - th], fill=0)
        rnd = random.Random(7)
        for k in range(16):                                   # thin radial cracks outside the ring
            a = 2 * math.pi * k / 16 + rnd.uniform(-.1, .1); r0 = r_out * 1.02; r1 = r0 + R * rnd.uniform(.15, .45) * (1 if i < 4 else .5)
            w = 0.012
            d.polygon([(C + r0 * math.cos(a - w), C + r0 * math.sin(a - w)), (C + r1 * math.cos(a), C + r1 * math.sin(a)),
                       (C + r0 * math.cos(a + w), C + r0 * math.sin(a + w))], fill=255)
    else:  # ink
        sc = [0.22, 0.5, 0.8, 1.0, 1.15][i]
        d.polygon(blob(sc, 3), fill=255)
        if i >= 2: d.polygon(blob(sc * [0, 0, .35, .7, .9][i], 4, wob=.15), fill=0)
    img = img.filter(ImageFilter.GaussianBlur(SS * 0.6))
    a = img.resize((S, S), Image.LANCZOS)
    out = Image.new("RGBA", (S, S), (255, 255, 255, 0)); out.putalpha(a)
    return out

if __name__ == "__main__":
    dst = sys.argv[1]; styles = [sys.argv[2]] if len(sys.argv) > 2 else ["star", "ring", "ink"]
    import os
    for st in styles:
        os.makedirs(f"{dst}/{st}", exist_ok=True)
        for i in range(5): frame(st, i).save(f"{dst}/{st}/frame_{i}.png")
