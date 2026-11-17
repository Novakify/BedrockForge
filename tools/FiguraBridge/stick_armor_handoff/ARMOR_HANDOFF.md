# Stick Figure Armor — design handoff

You're designing armor for a **stick-figure avatar** (Animator vs. Animation style) that Fernando wears in
Minecraft Bedrock through his own mod stack (Bedrock Forge → **FiguraBridge**, a Bedrock port of the Java mod Figura).
Vanilla armor can't work on this avatar: it's built for the normal blocky player body, so it floats in the wrong places.
So he gets **custom armor that hugs his limbs and moves with his bones**. A first version (v1) exists and works in the
pipeline. Your job: **design a better-looking one**. Make it dope, snug and readable in PvP. Then send it back in the
format at the bottom, and the other Claude session (which has his PC) will install, test in-game and polish it.

Everything you need is in this package. You don't need the game.

| File | What it is |
|---|---|
| `ARMOR_HANDOFF.md` | this document |
| `stick_skeleton.json` | the stick's real bones (names, parents, pivots) + the mesh points of every visible part |
| `stick_armor.py` | the generator. **The design lives in two places: `build_cubes()` (shapes) and `MATERIALS` + `panel()` (textures)** |
| `preview.py` | renders the stick wearing the armor from 4 angles (front, 3/4, side, back 3/4) |
| `current_design.png` | v1, rendered (gold helmet, diamond chest, netherite legs, iron boots) |
| `current_materials.png` | v1's 8 material textures |
| `STICK_DIMENSIONS.md` | **exact dimensions** of every part: true length, thickness, tilt, axis end points, bounds, centre, joints (pivots) |
| `stick_dimensions.json` | the same as data |
| `stick_figure_rest.obj` | **the exact model** as a 3D mesh (rest pose), one group per part; opens in Blender / Blockbench / any viewer |
| `stick_figure_with_armor_v1.obj` | the model + the current v1 armor, one group per slot |

Quick start (Python 3 with numpy, pillow, matplotlib):

```
python3 stick_armor.py stick_skeleton.json out
python3 preview.py stick_skeleton.json out/models/entity/fb_armor_stick.geo.json design.png
python3 preview.py stick_skeleton.json out/models/entity/fb_armor_stick.geo.json design2.png head=netherite chest=netherite legs=netherite feet=netherite
```

---

## 1. How it works (why it stays snug)

- In Bedrock, an entity's **animation moves bones by name**. Any geometry the entity draws that has a bone with that
  name gets the same motion. FiguraBridge animates the stick through bones named `p0`…`p68`.
- The armor is its **own geometry with an exact copy of the stick's bone hierarchy** (same names, parents, pivots), plus
  armor cubes on the bones they belong to. The player entity draws it with extra render controllers, so whatever moves
  the stick's thigh moves the thigh armor. It can't drift.
- **One geometry per slot**: `head`, `chest`, `legs`, `feet`. Each slot shows only while that slot has armor on.
- **One texture per material**, picked per slot from what's actually worn: leather, chainmail, iron, golden, diamond,
  netherite, copper, turtle (turtle = helmet only). The *shape* is shared by all materials of a slot; only the texture
  changes. (Per-material shapes are possible too, see §8.)
- Only on Fernando's own stick figure. Other players keep normal vanilla armor, and vanilla armor is hidden on the stick.

You don't touch any of that plumbing. You only design **cubes** and **textures**.

## 2. Hard rules

1. **Cubes go on the bone of the part they cover**, from the list in §4. A cube on the wrong bone won't follow that limb.
   Use the *mesh* bones (the ones with a shape: `p17`, `p22`, `p30`, `p32`, `p37`, `p39`, `p44`, `p46`, `p48`, `p52`, `p54`,
   `p56`), not their parent groups.
2. **Model the pieces in the rest pose** (the coordinates in `stick_skeleton.json`). See §5. Positions are in the same
   space as the mesh points.
3. **Prefer axis-aligned cubes** (no rotation). Tilted limbs get followed by stacking short bands, each re-centred on the
   limb with `fit_line` (that's how v1 does it, and it looks like segmented armor). Rotated cubes are allowed but see §8.
4. **Don't cover the face.** The front of the head ball (`p17`, z ≈ −3.5) is his face. Keep it readable.
5. **Mirror left/right.** Left limbs are `p30 p32 p44 p46 p48` (+x side), right limbs `p37 p39 p52 p54 p56` (−x side). The data
   is already exactly mirrored, so the left design with x negated is the right design.
6. **Budget:** roughly ≤ 40 cubes per slot. More is OK if it really earns it, but it's drawn every frame in PvP.
7. Keep a hair of clearance: plates ~0.3–0.6 units off the surface read as armor, not as a skin colour change.

## 3. Coordinate system

- Units are Bedrock **model pixels** (16 = one block). The stick is ~35 tall (feet at y≈0, top of head ≈35.4).
- **+y up. Front = −z** (his face is on the −z side). **+x = his left side.**
- A cube is `origin` (min corner `[x0,y0,z0]`) + `size` (`[w,h,d]`). In the script: `box(x0, y0, z0, x1, y1, z1)`.

## 4. His skeleton

Mesh bounds are in the rest pose. "Centre line" is a least-squares fit of the rod's centre (x and z as functions of y),
which is what `fit_line()` returns. Every limb rod is **1.5 × 1.5** thick (half thickness `R = 0.75`).

| bone | part | parent | pivot | mesh bounds (rest) | centre line |
|---|---|---|---|---|---|
| `p4` | Head group (parent of the head parts, no shape) | `p3` | [0.0, 26.6475, 0.0] | (group, no mesh) | |
| `p17` | **head ball** (the visible head; put the helmet here) | `p16` | [0.0, 29.7, -0.165] | x[-3.30, 3.30] y[26.40, 33.00] z[-3.46, 0.12] | x=+0.002·y-0.07, z=-0.015·y-1.01 |
| `p6` | head outline disc (flat, faces the camera; don't attach to it) | `p5` | [0.0, 31.3975, 0.85] | x[-4.00, 4.00] y[27.00, 35.20] z[0.85, 0.85] | |
| `p22` | torso rod | `p3` | [0.0, 27.7375, 0.0] | x[-0.75, 0.75] y[16.49, 27.24] z[-0.75, 1.00] | x=+0.000·y-0.00, z=+0.000·y+0.04 |
| `p30` | left upper arm | `p29` | [0.0, 26.7375, 0.25] | x[-0.75, 0.93] y[20.07, 26.74] z[-0.50, 2.73] | x=-0.031·y+0.82, z=-0.301·y+8.30 |
| `p32` | left forearm | `p31` | [0.0, 26.7375, -0.75] | x[-1.35, 0.62] y[14.99, 21.32] z[0.41, 2.70] | x=+0.085·y-1.90, z=+0.139·y-0.95 |
| `p37` | right upper arm | `p36` | [0.0, 26.7375, 0.25] | x[-0.93, 0.75] y[20.07, 26.74] z[-0.50, 2.73] | x=+0.031·y-0.82, z=-0.301·y+8.30 |
| `p39` | right forearm | `p38` | [0.0, 26.7375, -0.75] | x[-0.62, 1.35] y[14.99, 21.32] z[0.41, 2.70] | x=-0.085·y+1.90, z=+0.139·y-0.95 |
| `p44` | left thigh | `p43` | [0.425, 11.2375, 0.0] | x[-0.24, 2.20] y[8.23, 17.73] z[-1.25, 0.75] | x=-0.106·y+2.43, z=+0.057·y-1.02 |
| `p46` | left shin | `p45` | [1.65, 8.9125, -1.2] | x[0.88, 2.46] y[-0.00, 9.42] z[-1.25, 0.75] | x=+0.004·y+1.65, z=-0.058·y-0.01 |
| `p48` | left foot | `p47` | [2.0, 0.75, 0.3] | x[1.40, 2.95] y[0.58, 2.90] z[-0.41, 1.40] | |
| `p52` | right thigh | `p51` | [-0.425, 11.2375, 0.0] | x[-2.20, 0.24] y[8.23, 17.73] z[-1.25, 0.75] | x=+0.106·y-2.43, z=+0.057·y-1.02 |
| `p54` | right shin | `p53` | [-1.65, 8.9125, -1.2] | x[-2.46, -0.88] y[-0.00, 9.42] z[-1.25, 0.75] | x=-0.004·y-1.65, z=-0.058·y-0.01 |
| `p56` | right foot | `p55` | [-2.0, 0.75, 0.3] | x[-2.95, -1.40] y[0.58, 2.90] z[-0.41, 1.40] | |

Chains (each bone moves with all its parents):

```
head ball   p0 → p1 → p2 → p3 → p4 → p16 → p17
torso       p0 → p1 → p2 → p3 → p22
left arm    p0 → p1 → p2 → p3 → p29 → p30 (upper)   and   p29 → p31 → p32 (forearm)
right arm   p0 → p1 → p2 → p3 → p36 → p37 (upper)   and   p36 → p38 → p39 (forearm)
left leg    p0 → p1 → p2 → p43 → p44 (thigh), p43 → p45 → p46 (shin), p45 → p47 → p48 (foot)
right leg   p0 → p1 → p2 → p51 → p52 (thigh), p51 → p53 → p54 (shin), p53 → p55 → p56 (foot)
```

Ignore `p62` `p68` (first-person hands), `p9 p11 p13 p15` (alternate head rings that only show with some of his action
wheel looks) and the `f…` bones (first person).

## 5. Rest pose vs. what you see in game

In the rest pose (the coordinates you have) **the arms hang down behind the torso**, so they overlap it in
`preview.py`. That's normal. At runtime Figura's animation swings them out to the sides, and everything on `p30`,
`p32`, `p37`, `p39` goes with them. When you judge the arms, judge them against their own rods, not against the torso.
The legs are close to how they look standing.

## 6. The head is special

His head is two parts that **always turn to face the viewer** (Figura "Camera" parts, i.e. billboards): the ball `p17`
(6.6 wide, 6.6 tall, only ~3.5 deep, centre ≈ (0, 29.7, −1.7)) and a flat outline disc `p6` (radius 4, centre
(0, 31.4, 0.85), sits behind the ball). Put the helmet on **`p17`** so it turns with the ball. From any angle you then
see the head + helmet from "the front". Design it like a clean 2.5D stick-figure helmet: bands, crowns, crests,
horns, visors that don't hide the face. Stay within ~x ±5, and don't go below y≈27 (the neck/collar is the chest slot's).

## 7. Current design (v1, `current_design.png`)

- **Head** (on `p17`, 11 cubes): elliptical band around the ball at y 30.55–31.45 (front / back / sides + 4 corner
  blocks), a small fin on top with a forward tip, a gem on the front of the band.
- **Chest** (15): torso vest y 21.6–26.4, raised breastplate on the front, collar, two abdomen bands; per arm: pauldron +
  cap on the upper arm top, one upper-arm band, two forearm bracers.
- **Legs** (12): belt + front buckle on the torso bottom; per leg: three thigh bands, a knee guard + knee cap on the
  top of the shin.
- **Feet** (6): per leg: two shin bands (one is the boot cuff) + a boot box around the foot.
- **Textures**: 32×32 bevelled panel per material (vertical gradient, light top/left bevel, dark bottom/right, outline,
  a horizontal neon accent line, 4 rivets). Every cube face shows the whole panel.

What's weak about v1 (fair game to fix): it's very boxy, the vest is a plain block, the boots are blocks, nothing has
a silhouette that says "diamond" or "netherite", and there's no glow.

## 8. How to change it

### Shapes: `build_cubes(bones)` in `stick_armor.py`

Inside it you have:

- `mesh["p22"]` etc.: numpy array of that part's mesh points (N×3).
- `fit_line(mesh[bone])(y)`: `(cx, cz)`, the rod's centre at height `y`.
- `box(x0, y0, z0, x1, y1, z1)`: makes a cube. `add(slot, bone, cube)` attaches it.
- `band(slot, bone, rodmesh, y0, y1, half_w, half_d=None, dz=0)`: a sleeve around the rod from `y0` to `y1`, centred
  on the rod, `half_w` / `half_d` half extents (rod is `R = 0.75`), optional z shift.
- `R = 0.75`: rod half thickness.

Replace the body of `build_cubes` with your design. Keep the slot names and bone rules.

### Textures: `MATERIALS` and `panel()`

- `MATERIALS`: per material `(name, base, dark, highlight, accent)` RGB. Order and names must stay (the game picks by
  index). You can change all the colours.
- `panel()`: draws the 32×32 texture for a material. Change the pattern however you like. A different size is fine
  (power of two; set `TEX`).
- UVs: every face currently maps the **whole** texture (`"uv": [0,0], "uv_size": [TEX, TEX]` per face in `box()`). If you
  want different art per face or per piece, make a bigger texture atlas and give `box()` per-face UV regions. Just say
  so in your notes.

### Allowed, but tell me (I'll verify in game)

- **Rotated cubes** (`"rotation": [rx, ry, rz]` + `"pivot"`): Bedrock's rotation sign convention isn't verified in this
  pipeline yet and `preview.py` ignores rotation. If you use them, keep them few and describe the intended look.
- **Glow** (neon parts that light up in the dark): possible with an emissive material. Mark which parts/pixels should
  glow.
- **Per-material shapes** (e.g. netherite gets spikes, gold gets a crown): possible (one geometry per slot+material).
  Describe what changes per material.
- **Extras**: enchantment shimmer, dyed leather colours, a cape piece. Possible later; list them as wishes.

### Don't

- Don't rename bones, change pivots or add new bones (the animation only knows the existing ones).
- Don't put pieces on `p6`, `p62`, `p68`, the `f…` bones or group bones like `p29`/`p43`.

## 9. What to send back

Best: **your edited `stick_armor.py`** (new `build_cubes()`, and `MATERIALS` / `panel()` if you changed textures) +
the `preview.py` renders of your design + a short note (style intent, anything from §8 you used, what you'd do next).

If you can't run code, send a cube list instead, one entry per cube:

```json
{
  "slots": {
    "head":  [ {"bone": "p17", "from": [-3.95, 30.55, -4.31], "to": [3.95, 31.45, -3.86], "note": "band front"} ],
    "chest": [ ],
    "legs":  [ ],
    "feet":  [ ]
  },
  "materials": {
    "diamond": {"base": [52,214,208], "dark": [14,118,128], "highlight": [168,255,248], "accent": [236,255,255]}
  },
  "texture_pattern": "describe the panel art in words, or attach PNGs (32x32, one per material, names as in MATERIALS)",
  "notes": "style intent, glow wishes, per-material ideas"
}
```

`from`/`to` are opposite corners in the rest-pose coordinates above. Mirror the right limbs yourself (negate x, swap
min/max) or write `"mirror": true` on a left-limb cube and its right twin is added on the matching bone
(`p30↔p37`, `p32↔p39`, `p44↔p52`, `p46↔p54`, `p48↔p56`).

Fernando's taste, for reference: he likes **neon purple + neon white** (his client menu uses them), glass/glow looks,
and things that look "dope" and clean rather than cluttered. It's PvP: armor should still read at a glance.
