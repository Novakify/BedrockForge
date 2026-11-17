# Bedrock Forge icon set ("Forge facet"): our own icons, drawn from scratch on a 24-unit grid.
# House style: 2.1-unit rounded strokes + one solid "facet" (a lighter cut face, ~45% alpha, lit from the top-left),
# Forge metaphors (anvil home, cut gem for visuals, ringed planet for world, aperture camera, bracket reticle).
# Writes resources/client/icons.png (1024x512, 128px cells) + icons.json (same keys as before).
import json, math, sys
from PIL import Image, ImageDraw

W, H, C, SS = 1024, 512, 128, 4
S = (112 / 24) * SS          # units -> supersampled px
O = 8 * SS
LW = int(2.1 * S)
WHITE = (255, 255, 255, 255)
FACET = 118                  # facet alpha


def P(x, y): return (O + x * S, O + y * S)


class Icon:
    def __init__(self):
        self.im = Image.new('RGBA', (C * SS, C * SS), (0, 0, 0, 0))
        self.fa = Image.new('L', (C * SS, C * SS), 0)          # facet mask (drawn under the strokes)
        self.d = ImageDraw.Draw(self.im)
        self.df = ImageDraw.Draw(self.fa)

    def cap(self, p, w=LW):
        x, y = P(*p); self.d.ellipse([x - w / 2, y - w / 2, x + w / 2, y + w / 2], fill=WHITE)

    def line(self, pts, w=None):
        w = int(w * S) if w else LW
        self.d.line([P(*p) for p in pts], fill=WHITE, width=w, joint='curve')
        for p in pts: self.cap(p, w)

    def poly(self, pts): self.line(list(pts) + [pts[0]])

    def facet(self, pts): self.df.polygon([P(*p) for p in pts], fill=255)

    def facet_ell(self, cx, cy, rx, ry): self.df.ellipse([*P(cx - rx, cy - ry), *P(cx + rx, cy + ry)], fill=255)

    def arc(self, cx, cy, rx, ry, a0=0, a1=360, w=None):
        w = int(w * S) if w else LW
        x0, y0 = P(cx - rx, cy - ry); x1, y1 = P(cx + rx, cy + ry); h = w / 2
        self.d.arc([x0 - h, y0 - h, x1 + h, y1 + h], a0, a1, fill=WHITE, width=w)
        if a1 - a0 < 360:
            for a in (a0, a1): self.cap((cx + rx * math.cos(math.radians(a)), cy + ry * math.sin(math.radians(a))), w)

    def dot(self, cx, cy, r):
        self.d.ellipse([*P(cx - r, cy - r), *P(cx + r, cy + r)], fill=WHITE)

    def rrect(self, x0, y0, x1, y1, r, facet=False):
        if facet: self.df.rounded_rectangle([*P(x0, y0), *P(x1, y1)], radius=r * S, fill=255)
        h = LW / 2
        a, b = P(x0, y0); c, d = P(x1, y1)
        self.d.rounded_rectangle([a - h, b - h, c + h, d + h], radius=r * S + h, outline=WHITE, width=LW)

    def done(self):
        # facet: a soft top-left -> bottom-right fade (our "lit cut face"), under the white strokes
        g = Image.linear_gradient('L').rotate(45, expand=False).resize(self.fa.size)
        g = g.point(lambda v: int(FACET * (1.0 - 0.45 * v / 255)))
        a = Image.composite(g, Image.new('L', self.fa.size, 0), self.fa)
        base = Image.new('RGBA', self.im.size, (255, 255, 255, 0)); base.putalpha(a)
        base.alpha_composite(self.im)
        return base.resize((C, C), Image.LANCZOS)


def star(cx, cy, r, ri, n=4, rot=-90):
    pts = []
    for i in range(n * 2):
        rr = r if i % 2 == 0 else ri
        a = math.radians(rot + i * 180 / n)
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    return pts


I = {}
# home -> anvil (Bedrock Forge)
i = Icon()
top = [(3, 6), (21, 6), (21, 8.5), (16, 11), (16, 14), (18, 18), (6, 18), (8, 14), (8, 11), (5, 9.5), (3, 9.5)]
i.facet([(3, 6), (21, 6), (21, 8.5), (16, 11), (8, 11), (5, 9.5), (3, 9.5)]); i.poly(top); i.line([(4.5, 21), (19.5, 21)]); I['home'] = i
# pencil -> pen nib
i = Icon()
nib = [(12, 2.5), (19, 10), (16, 19), (8, 19), (5, 10)]
i.facet([(12, 2.5), (5, 10), (8, 19), (12, 19), (12, 2.5)]); i.poly(nib); i.line([(12, 9), (12, 13.5)]); i.dot(12, 13.5, 1.6); i.line([(8, 22), (16, 22)]); I['pencil'] = i
# sparkle -> a big 4-point star with an orbiting small one
i = Icon()
st = star(10, 13, 8.5, 2.6); i.facet([st[7], st[0], st[1], (10, 13)]); i.poly(st)
i.facet(star(19.5, 5, 3.2, 1.0)); i.poly(star(19.5, 5, 3.2, 1.0)); I['sparkle'] = i
# gear -> hex nut
i = Icon()
hx = [(12 + 10 * math.cos(math.radians(30 + 60 * k)), 12 + 10 * math.sin(math.radians(30 + 60 * k))) for k in range(6)]
i.facet([hx[3], hx[4], hx[5], (12, 12)]); i.poly(hx); i.arc(12, 12, 3.6, 3.6); I['gear'] = i
# pin -> bookmark ribbon with a star cut
i = Icon()
bm = [(5.5, 2.5), (18.5, 2.5), (18.5, 21.5), (12, 16.5), (5.5, 21.5)]
i.facet([(5.5, 2.5), (18.5, 2.5), (18.5, 7), (5.5, 7)]); i.poly(bm); i.poly(star(12, 9.5, 3.4, 1.2)); I['pin'] = i
# eye (Visual) -> cut gem
i = Icon()
gem = [(7, 3.5), (17, 3.5), (22, 9), (12, 21), (2, 9)]
i.facet([(7, 3.5), (17, 3.5), (15, 9), (9, 9)]); i.poly(gem); i.line([(2, 9), (22, 9)]); i.line([(9, 9), (12, 21), (15, 9)]); I['eye'] = i
# monitor (HUD) -> stacked glass panes
i = Icon()
i.rrect(7, 3, 21, 14, 2.2, facet=True); i.rrect(3, 9, 17, 20.5, 2.2); i.line([(6.5, 16.5), (11, 16.5)]); I['monitor'] = i
# sword (Combat) -> crossed blades (guards low, pommels as dots)
i = Icon()
for sgn in (1, -1):
    tx = 12 - sgn * 8.5
    i.facet([(tx, 3.5), (tx + sgn * 2.6, 3.5), (12 + sgn * 4.0, 15.0), (12 + sgn * 3.0, 16.0)])
    i.line([(tx, 3.5), (tx + sgn * 2.6, 3.5), (12 + sgn * 4.0, 15.0)]); i.line([(tx, 3.5), (12 + sgn * 3.0, 16.0)])
    i.line([(12 + sgn * 1.6, 17.2), (12 + sgn * 6.0, 12.8)])
    i.line([(12 + sgn * 5.2, 17.6), (12 + sgn * 7.4, 19.8)]); i.dot(12 + sgn * 8.4, 20.8, 1.5)
I['sword'] = i
# camera -> aperture
i = Icon()
i.arc(12, 12, 10, 10)
for k in range(6):
    a = math.radians(60 * k); b = math.radians(60 * k + 75)
    i.line([(12 + 4.2 * math.cos(a), 12 + 4.2 * math.sin(a)), (12 + 9.6 * math.cos(b), 12 + 9.6 * math.sin(b))], w=1.7)
i.facet([(12 + 4.2 * math.cos(math.radians(60 * k)), 12 + 4.2 * math.sin(math.radians(60 * k))) for k in range(6)]); I['camera'] = i
# puzzle (Misc) -> 2x2 tiles, one round
i = Icon()
i.rrect(3, 3, 10.5, 10.5, 2, facet=True); i.rrect(13.5, 13.5, 21, 21, 2); i.rrect(3, 13.5, 10.5, 21, 2); i.arc(17.25, 6.75, 3.75, 3.75); I['puzzle'] = i
# music -> waveform bars
i = Icon()
for x, h in ((3.5, 3), (7.75, 7), (12, 10), (16.25, 6), (20.5, 3.5)):
    i.line([(x, 12 - h), (x, 12 + h)])
I['music'] = i
# back -> chevron in a rounded tile
i = Icon()
i.rrect(3, 3, 21, 21, 6, facet=False); i.line([(14, 7.5), (9.5, 12), (14, 16.5)]); I['back'] = i
# close -> notched X (gap in the middle: our signature)
i = Icon()
for a, b in (((5, 5), (9.6, 9.6)), ((14.4, 14.4), (19, 19)), ((19, 5), (14.4, 9.6)), ((9.6, 14.4), (5, 19))): i.line([a, b])
i.dot(12, 12, 1.5); I['close'] = i
# search -> rounded-square lens + handle
i = Icon()
i.rrect(3, 3, 15, 15, 4.5, facet=True); i.line([(15.5, 15.5), (21, 21)], w=2.6); I['search'] = i
# play -> rounded triangle with facet
i = Icon()
tri = [(7, 4), (20, 12), (7, 20)]; i.facet([(7, 4), (20, 12), (7, 12)]); i.poly(tri); I['play'] = i
# check -> check in a diamond
i = Icon()
dm = [(12, 1.8), (22.2, 12), (12, 22.2), (1.8, 12)]; i.facet([(12, 1.8), (1.8, 12), (12, 12)]); i.poly(dm); i.line([(7.5, 12.3), (10.8, 15.4), (16.5, 8.8)]); I['check'] = i
# globe (World) -> ringed planet
i = Icon()
i.facet_ell(12, 12, 7, 7); i.arc(12, 12, 7, 7)
i.arc(12, 12, 11, 3.6, -18, 198); I['globe'] = i
# wand -> stick with a star head
i = Icon()
i.line([(3, 21), (12.5, 11.5)]); s4 = star(16.5, 7.5, 5.5, 1.8); i.facet(s4); i.poly(s4); i.dot(5.5, 6, 1.1); i.dot(20.5, 17, 1.1); I['wand'] = i
# volume -> speaker + sound dots
i = Icon()
sp = [(3, 9), (7, 9), (12, 4.5), (12, 19.5), (7, 15), (3, 15)]; i.facet(sp); i.poly(sp)
for k, r in ((-1, 1.25), (0, 1.5), (1, 1.25)): i.dot(16.5, 12 + 4 * k, r * 0.8)
for k in (-1, 0, 1): i.dot(20.5, 12 + 6 * k, 1.1)
I['volume'] = i
# crosshair -> corner brackets + centre dot
i = Icon()
for sx, sy in ((1, 1), (-1, 1), (1, -1), (-1, -1)):
    cx, cy = 12 + 9 * sx, 12 + 9 * sy
    i.line([(cx, cy - 4.5 * sy), (cx, cy), (cx - 4.5 * sx, cy)])
i.facet_ell(12, 12, 3.2, 3.2); i.dot(12, 12, 1.4); I['crosshair'] = i

order = ['home', 'pencil', 'sparkle', 'gear', 'pin', 'eye', 'monitor', 'sword', 'camera', 'puzzle', 'music', 'back', 'close',
         'search', 'play', 'check', 'globe', 'wand', 'volume', 'crosshair']
atlas = Image.new('RGBA', (W, H), (0, 0, 0, 0))
rects = {}
for idx, k in enumerate(order):
    col, row = idx % 8, idx // 8
    atlas.paste(I[k].done(), (col * C, row * C))
    rects[k] = [col * C / W, row * C / H, (col + 1) * C / W, (row + 1) * C / H]
root = sys.argv[1] if len(sys.argv) > 1 else '.'
atlas.save(f'{root}/resources/client/icons.png')
json.dump({'rects': rects}, open(f'{root}/resources/client/icons.json', 'w'), indent=1)
bg = Image.new('RGBA', (W, 3 * C), (40, 20, 60, 255)); bg.alpha_composite(atlas.crop((0, 0, W, 3 * C))); bg.convert('RGB').save('/tmp/icons_forge.png')
print('ok')
