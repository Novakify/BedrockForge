# Original Bedrock Forge impact-burst frames (procedural; not derived from any other mod's art).
import math, random, sys
from PIL import Image, ImageDraw
S, SS = 512, 4            # output size, supersampling
N = S * SS
C = N / 2
def spiky(r_in, r_out, spikes, jitter, seed, rot=0.0):
    rnd = random.Random(seed); pts = []
    for i in range(spikes * 2):
        a = rot + math.pi * i / spikes + rnd.uniform(-jitter, jitter) * math.pi / spikes
        r = (r_out * rnd.uniform(0.72, 1.0)) if i % 2 == 0 else (r_in * rnd.uniform(0.85, 1.1))
        pts.append((C + r * math.cos(a), C + r * math.sin(a)))
    return pts
def frame(i):
    img = Image.new("L", (N, N), 0); d = ImageDraw.Draw(img)
    R = N * 0.5
    if i == 0:   # tight core flash
        d.polygon(spiky(R*.05, R*.30, 14, .5, 11), fill=255)
    elif i == 1: # full burst
        d.polygon(spiky(R*.16, R*.95, 18, .6, 22), fill=255)
    elif i == 2: # burst with hollow core (shockwave forming)
        d.polygon(spiky(R*.30, R*1.25, 20, .6, 33), fill=255)
        d.polygon(spiky(R*.10, R*.42, 16, .5, 34, .1), fill=0)
    elif i == 3: # jagged ring
        d.polygon(spiky(R*.70, R*1.45, 22, .7, 44), fill=255)
        d.polygon(spiky(R*.45, R*.92, 22, .6, 45, .07), fill=0)
    else:        # ring breaking into shards at the edges
        rnd = random.Random(55)
        for k in range(26):
            a = 2*math.pi*k/26 + rnd.uniform(-.08,.08); r0 = R*rnd.uniform(.95,1.15); r1 = r0 + R*rnd.uniform(.25,.5)
            w = rnd.uniform(.03,.07)
            d.polygon([(C+r0*math.cos(a-w),C+r0*math.sin(a-w)),(C+r1*math.cos(a),C+r1*math.sin(a)),(C+r0*math.cos(a+w),C+r0*math.sin(a+w))], fill=255)
    a = img.resize((S, S), Image.LANCZOS)
    out = Image.new("RGBA", (S, S), (255, 255, 255, 0)); out.putalpha(a)  # white shape, alpha mask -> tintable
    return out
dst = sys.argv[1]
for i in range(5): frame(i).save(f"{dst}/frame_{i}.png")
