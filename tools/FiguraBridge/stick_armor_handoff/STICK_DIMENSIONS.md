# Stick figure — exact dimensions (rest pose)

Source: the installed FiguraBridge model `geometry.figura.stick` (figura_stick.geo.json), full resolution. Measured by script, nothing typed by hand.

## Units and axes

- Units: Bedrock **model pixels** (16 = 1 block; the player hitbox is 1.8 blocks = 28.8 px tall). The model is then drawn at entity scale **0.9375**, so in-world size = value × 0.9375 / 16 blocks.
- **+y up, front = −z** (his face is on the −z side), **+x = his left side**.
- **Rest pose** = the coordinates below. In game, Figura's animation moves each bone (the arms swing out from behind the torso), and anything attached to a bone moves with it. Design against these rest-pose numbers.

## Overall (all visible parts)

| | min | max | size |
|---|---|---|---|
| x | -4.000 | 4.000 | 8.000 |
| y | -0.002 | 35.197 | 35.199 |
| z | -3.465 | 2.733 | 6.198 |

Total height 35.199 px = 2.200 blocks (× 0.9375 in world = 2.062 blocks).

## Parts

Length / thickness are measured along each part's own main axis (principal-component fit of the mesh), so tilted limbs give their true length. `top`/`bottom` are the two ends of that axis (centre of the rod's end faces). Tilt = angle of the axis from straight up.

| bone | part | length | thickness (a × b) | tilt from vertical | axis top end | axis bottom end | bounds min | bounds max | centre | verts |
|---|---|---|---|---|---|---|---|---|---|---|
| `p17` | head ball (the visible head) | 6.599 | 6.601 × 3.533 | 1.1° | (0.012, 32.976, -1.508) | (-0.019, 26.378, -1.389) | (-3.300, 26.400, -3.465) | (3.300, 33.000, 0.120) | (0.000, 29.700, -1.673) | 2392 |
| `p6` | head outline disc (flat, faces camera) | 8.200 | 8.000 × 0.000 | 0.1° | (-0.006, 35.197, 0.850) | (0.004, 26.998, 0.850) | (-4.000, 26.997, 0.850) | (4.000, 35.197, 0.850) | (0.000, 31.097, 0.850) | 510 |
| `p22` | torso rod | 10.750 | 1.750 × 1.500 | 0.0° | (-0.000, 27.238, 0.040) | (-0.000, 16.487, 0.040) | (-0.750, 16.488, -0.750) | (0.750, 27.238, 1.000) | (0.000, 21.863, 0.125) | 248 |
| `p30` | left upper arm | 6.938 | 2.044 × 1.571 | 17.4° | (-0.004, 26.801, 0.203) | (0.207, 20.182, 2.270) | (-0.750, 20.075, -0.500) | (0.931, 26.738, 2.733) | (0.091, 23.406, 1.116) | 248 |
| `p32` | left forearm | 6.422 | 1.519 × 1.518 | 9.6° | (-0.091, 21.295, 2.020) | (-0.648, 14.963, 1.102) | (-1.351, 14.987, 0.407) | (0.620, 21.325, 2.701) | (-0.366, 18.156, 1.554) | 328 |
| `p37` | right upper arm | 6.938 | 2.044 × 1.571 | 17.4° | (0.004, 26.801, 0.203) | (-0.207, 20.182, 2.270) | (-0.931, 20.075, -0.500) | (0.750, 26.738, 2.733) | (-0.091, 23.406, 1.116) | 248 |
| `p39` | right forearm | 6.422 | 1.519 × 1.518 | 9.6° | (0.091, 21.295, 2.020) | (0.648, 14.963, 1.102) | (-0.620, 14.987, 0.407) | (1.351, 21.325, 2.701) | (0.366, 18.156, 1.554) | 328 |
| `p44` | left thigh | 9.591 | 1.680 × 1.810 | 7.0° | (0.542, 17.752, -0.012) | (1.568, 8.232, -0.562) | (-0.238, 8.230, -1.250) | (2.197, 17.734, 0.750) | (0.980, 12.982, -0.250) | 328 |
| `p46` | left shin | 9.462 | 1.647 × 1.580 | 3.4° | (1.684, 9.428, -0.564) | (1.646, -0.017, -0.007) | (0.878, -0.002, -1.250) | (2.463, 9.417, 0.750) | (1.671, 4.708, -0.250) | 328 |
| `p48` | left foot | 2.498 | 1.517 × 1.491 | 14.9° | (2.055, 2.930, 0.823) | (2.257, 0.516, 0.212) | (1.403, 0.583, -0.406) | (2.948, 2.896, 1.399) | (2.175, 1.739, 0.496) | 248 |
| `p52` | right thigh | 9.591 | 1.680 × 1.810 | 7.0° | (-0.542, 17.752, -0.012) | (-1.568, 8.232, -0.562) | (-2.197, 8.230, -1.250) | (0.238, 17.734, 0.750) | (-0.980, 12.982, -0.250) | 328 |
| `p54` | right shin | 9.462 | 1.647 × 1.580 | 3.4° | (-1.684, 9.428, -0.564) | (-1.646, -0.017, -0.007) | (-2.463, -0.002, -1.250) | (-0.878, 9.417, 0.750) | (-1.671, 4.708, -0.250) | 328 |
| `p56` | right foot | 2.498 | 1.517 × 1.491 | 14.9° | (-2.055, 2.930, 0.823) | (-2.257, 0.516, 0.212) | (-2.948, 0.583, -0.406) | (-1.403, 2.896, 1.399) | (-2.175, 1.739, 0.496) | 248 |

Notes: the limb rods are bevelled cuboids, **1.5 × 1.5** in cross-section. The head ball is a squashed sphere (6.6 wide × 6.6 tall × ~3.6 deep); its face is the −z side. The outline disc is flat (zero depth) with radius 4. Both head parts are Figura **Camera** parts: they always turn to face the viewer, so a helmet must go on `p17`.

## Joints (bone pivots = where each part rotates)

| bone | joint | pivot (x, y, z) | parent |
|---|---|---|---|
| `p2` | root / lower body group | (0.000, 0.000, 0.000) | `p1` |
| `p3` | body (waist) | (0.000, 16.500, 0.000) | `p2` |
| `p4` | neck / head group | (0.000, 26.648, 0.000) | `p3` |
| `p16` | head ball pivot (billboard) | (0.000, 29.700, -0.165) | `p4` |
| `p5` | head disc pivot (billboard) | (0.000, 31.398, 0.100) | `p4` |
| `p29` | left shoulder | (0.825, 25.988, 0.250) | `p3` |
| `p31` | left elbow | (2.100, 21.387, 1.750) | `p29` |
| `p36` | right shoulder | (-0.825, 25.988, 0.000) | `p3` |
| `p38` | right elbow | (-2.100, 21.387, 1.750) | `p36` |
| `p43` | left hip | (0.000, 17.738, 0.000) | `p2` |
| `p45` | left knee | (1.650, 8.912, -1.200) | `p43` |
| `p47` | left ankle | (2.000, 0.750, 0.300) | `p45` |
| `p51` | right hip | (0.000, 17.738, 0.000) | `p2` |
| `p53` | right knee | (-1.650, 8.912, -1.200) | `p51` |
| `p55` | right ankle | (-2.000, 0.750, 0.300) | `p53` |

Some pivots sit away from their mesh in the rest pose (e.g. the elbows). That's how the avatar was built in Blockbench. The animation still rotates each part around its own pivot, and attached armor follows the same transform.

## Part hierarchy
```
head ball (the visible head)               p0 → p1 → p2 → p3 → p4 → p16 → p17
head outline disc (flat, faces camera)     p0 → p1 → p2 → p3 → p4 → p5 → p6
torso rod                                  p0 → p1 → p2 → p3 → p22
left upper arm                             p0 → p1 → p2 → p3 → p29 → p30
left forearm                               p0 → p1 → p2 → p3 → p29 → p31 → p32
right upper arm                            p0 → p1 → p2 → p3 → p36 → p37
right forearm                              p0 → p1 → p2 → p3 → p36 → p38 → p39
left thigh                                 p0 → p1 → p2 → p43 → p44
left shin                                  p0 → p1 → p2 → p43 → p45 → p46
left foot                                  p0 → p1 → p2 → p43 → p45 → p47 → p48
right thigh                                p0 → p1 → p2 → p51 → p52
right shin                                 p0 → p1 → p2 → p51 → p53 → p54
right foot                                 p0 → p1 → p2 → p51 → p53 → p55 → p56
```

## Files that go with this

- `stick_figure_rest.obj`: the exact model (rest pose) as a mesh, one group per part (`p17_head_ball`, `p22_torso` …). Opens in Blender, Blockbench, any 3D viewer.
- `stick_figure_with_armor_v1.obj`: the same + the current v1 armor cubes (one group per slot), to see what's there now.
- `stick_dimensions.json`: everything in this file as data.
