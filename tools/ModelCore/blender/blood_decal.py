import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

OUT = sys.argv[1]
W = H = 1024
rng = np.random.default_rng(7)

# ---- alpha mask: mouth smear + drips + splatter (image top = nose, bottom = chin) ----
mask = Image.new('L', (W, H), 0)
d = ImageDraw.Draw(mask)
cx, cy = int(0.50 * W), int(0.50 * H)  # mouth line

# main irregular smear band across the mouth
for _ in range(95):
    ex = cx + int(rng.normal(0, 0.17 * W)); ey = cy + int(rng.normal(0, 0.075 * H))
    rw = int(abs(rng.normal(0.055 * W, 0.03 * W)) + 6); rh = int(abs(rng.normal(0.035 * H, 0.02 * H)) + 5)
    d.ellipse([ex - rw, ey - rh, ex + rw, ey + rh], fill=int(rng.integers(160, 255)))

# dense opaque core right on the mouth/teeth
for _ in range(16):
    ex = cx + int(rng.normal(0, 0.10 * W)); ey = cy + int(rng.normal(0.015 * H, 0.05 * H))
    rw = int(abs(rng.normal(0.075 * W, 0.02 * W)) + 10); rh = int(abs(rng.normal(0.045 * H, 0.015 * H)) + 8)
    d.ellipse([ex - rw, ey - rh, ex + rw, ey + rh], fill=int(rng.integers(215, 255)))

# lighter smeared stains UP onto the snout/muzzle (image top half = higher on the face)
for _ in range(34):
    ex = cx + int(rng.normal(0, 0.14 * W)); ey = int(0.33 * H) + int(rng.normal(0, 0.06 * H))
    rw = int(abs(rng.normal(0.05 * W, 0.025 * W)) + 5); rh = int(abs(rng.normal(0.03 * H, 0.015 * H)) + 4)
    d.ellipse([ex - rw, ey - rh, ex + rw, ey + rh], fill=int(rng.integers(110, 205)))

# drips running DOWN (toward chin / image bottom)
for _ in range(20):
    dx = cx + int(rng.normal(0, 0.19 * W)); dy = cy + int(rng.integers(-8, 45))
    length = int(abs(rng.normal(0.20 * H, 0.13 * H)) + 20); wdt = int(abs(rng.normal(5.0, 3.0)) + 2)
    y = dy
    while y < dy + length and y < H:
        rr = max(1, int(wdt * (1 - (y - dy) / max(1, length))))
        d.ellipse([dx - rr, y - rr, dx + rr, y + rr], fill=int(rng.integers(150, 240)))
        dx += int(rng.normal(0, 1.3)); y += max(1, rr)
    d.ellipse([dx - wdt, y - wdt, dx + wdt, y + wdt], fill=int(rng.integers(160, 230)))  # bulb tip

# fine splatter droplets
for _ in range(240):
    sx = cx + int(rng.normal(0, 0.27 * W)); sy = cy + int(rng.normal(0.05 * H, 0.23 * H))
    rr = int(abs(rng.normal(2.2, 1.7)) + 0.5)
    if 0 <= sx < W and 0 <= sy < H:
        d.ellipse([sx - rr, sy - rr, sx + rr, sy + rr], fill=int(rng.integers(110, 220)))

mask = mask.filter(ImageFilter.GaussianBlur(2.3))
A = np.asarray(mask).astype(np.float32) / 255.0

# ---- color: thin stains -> brown, thick -> dark maroon, clots -> near black ----
small = (rng.random((H // 8, W // 8)).astype(np.float32))
nf = np.asarray(Image.fromarray((small * 255).astype(np.uint8)).resize((W, H))).astype(np.float32) / 255.0
dark  = np.array([42, 12, 8], np.float32)   # thick dried
clot  = np.array([15, 5, 4], np.float32)    # pooled / black
brown = np.array([88, 42, 25], np.float32)  # thin oxidized stain

t = np.clip(A, 0, 1)[..., None]
col = brown[None, None] * (1 - t) + dark[None, None] * t
clotmask = (nf < 0.34)[..., None].astype(np.float32)
col = col * (1 - clotmask * 0.6) + clot[None, None] * (clotmask * 0.6)
grain = (rng.random((H, W)).astype(np.float32) - 0.5) * 24.0
col = np.clip(col + grain[..., None], 0, 255)

alpha = np.clip(A * 1.45, 0, 1) * 255.0
rgba = np.dstack([col, alpha]).astype(np.uint8)
Image.fromarray(rgba, 'RGBA').save(OUT)
print("SAVED", OUT, "coverage %.1f%%" % (100.0 * (A > 0.05).mean()))
