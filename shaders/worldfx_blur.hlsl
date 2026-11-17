// Renderer Foundation - World FX: camera motion blur, drawn on the world's own targets right after the World FX pass
// (before the hand and the HUD are drawn: they stay sharp). Original code for Renderer Foundation.
//
// Why here and not at Present (shaders/postfx.hlsl has the old one): at Present there is no depth, so the blur could
// only follow turning, and it smeared the HUD, the crosshair and the held item. Here the depth copy of the pass is at
// hand: every pixel gets its own velocity from where its world point was a shutter-time ago - turning, walking,
// strafing, falling. Near things streak more than far ones, the sky only when you turn.
//
// Butter, not smear (audit 2, Nov 16 - the first try looked "like a 60 Hz monitor"):
//   * the streak is a SHARE of what the camera really did since the last frame (a pose-to-pose reprojection, no clock):
//     it fills part of the gap between two frames and can never be longer than that gap or out of step with it. The
//     first version measured a velocity against the wall clock and held the shutter open for a whole frame at high FPS:
//     the longest smear there is, pulsing with every uneven frame time.
//   * things that move by themselves are left sharp: players, your own character in third person, mobs, dropped items
//     (Bedrock Forge sends their boxes). The camera's motion says nothing about how THEY move on screen - blurring the
//     player you are tracking along your own turn smears exactly what your eyes are on.
//   * what stands in front of a pixel is not gathered into it: no ghost of a player or a near block in the world behind
//   * centred on the pixel (half back, half ahead): the picture does not lag behind the camera
//   * under ~1 px of motion nothing is touched (discard): slow pans and standing still stay pin sharp and cost nothing
//   * averaged in linear light, a different dither every frame
cbuffer C : register(b0) {
    float4 c0; // xyz = camera forward, w = tan(fovx/2)
    float4 c1; // xyz = camera right,   w = tan(fovy/2)
    float4 c2; // xyz = camera up,      w = frame seed
    float4 c3; // x = shutter (share of the last frame's motion), y = longest blur (px), z = dead zone (px), w = flags: 1 BGRA, 4 show the velocity
    float4 c4; // xy = target size, z = scene row pitch (bytes), w = depth row pitch (bytes)
    float4 c5, c6, c7;   // the camera one frame ago: forward / right / up, w = how far it has come since (x, y, z; the "moving" share is in it)
    float4 c8, c9, c10;
};
ByteAddressBuffer gScene : register(t0);
ByteAddressBuffer gDepth : register(t1);
// the things that move by themselves (from Bedrock Forge, through SkyPass): upright cylinders, camera-relative
cbuffer A : register(b1) {
    float4 aInfo;      // x = how many
    float4 aPos[24];   // xyz = the middle of its feet, w = radius
    float4 aDim[24];   // x = height
};

int2   clampPx(int2 p) { return clamp(p, int2(0, 0), int2(c4.xy) - 1); }
float  rawDepth(int2 p) { p = clampPx(p); return float(gDepth.Load(uint(p.y) * uint(c4.w) + uint(p.x) * 4u) & 0xFFFFFFu) / 16777215.0; }
float3 sceneAt(int2 p) {
    p = clampPx(p);
    uint  v = gScene.Load(uint(p.y) * uint(c4.z) + uint(p.x) * 4u);
    float3 c = float3(v & 255u, (v >> 8) & 255u, (v >> 16) & 255u) / 255.0;
    return c3.w > 0.5 ? c.bgr : c;
}
// two pixels blended along x or y, whichever the blur runs along least: taps land between pixel rows / columns, and
// without this a slow diagonal blur shows the pixel grid as crawling stair steps
float3 sceneLin(float2 p, float2 dirAbs) {
    float2 f = p - 0.5; int2 b = int2(floor(f)); float2 w = f - floor(f);
    float3 a, q;
    if (dirAbs.x > dirAbs.y) { a = sceneAt(int2(round(f.x), b.y)); q = sceneAt(int2(round(f.x), b.y + 1)); a *= a; q *= q; return lerp(a, q, w.y); }
    a = sceneAt(int2(b.x, round(f.y))); q = sceneAt(int2(b.x + 1, round(f.y))); a *= a; q *= q; return lerp(a, q, w.x);
}
float ign(float2 p) { return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715)))); }

struct VSOut { float4 pos : SV_Position; };
VSOut VSWorldFxBlur(uint id : SV_VertexID) {
    VSOut o;
    o.pos = float4(id == 2 ? 3.0 : -1.0, id == 1 ? 3.0 : -1.0, 0.0, 1.0);
    return o;
}

float viewZ(float d) { return d >= 0.99999 ? 1e6 : 0.025 / max(1.0 - d, 1e-7); }
// camera-relative point -> where an earlier camera (its axes f, r, u; it stood t behind) saw it: pixel xy + distance z
float3 seenBy(float3 q, float4 f, float4 r, float4 u) {
    q += float3(f.w, r.w, u.w);
    float zf = dot(q, f.xyz);
    if (zf < 1e-3) return float3(0.0, 0.0, -1.0);
    float2 pn = float2(dot(q, r.xyz) / (zf * c0.w), dot(q, u.xyz) / (zf * c1.w));
    return float3((pn.x * 0.5 + 0.5) * c4.x, (0.5 - pn.y * 0.5) * c4.y, zf);
}

// 1 = part of the still world (blur), 0 = inside something that moves by itself (keep sharp)
float stillWorld(float3 q) {
    float k = 1.0;
    int   n = min(int(aInfo.x), 24);
    [loop] for (int i = 0; i < n; ++i) {
        float3 d = q - aPos[i].xyz;
        if (d.y < -0.2 || d.y > aDim[i].x) continue;
        float r = length(d.xz);
        k = min(k, smoothstep(aPos[i].w - 0.12, aPos[i].w, r));
    }
    return k;
}

float4 PSWorldFxBlur(VSOut i) : SV_Target {
    float2 px  = i.pos.xy;
    float  zc  = viewZ(rawDepth(int2(px)));
    float2 ndc = float2(px.x / c4.x * 2.0 - 1.0, 1.0 - px.y / c4.y * 2.0);
    float3 q   = (c0.xyz + c1.xyz * (ndc.x * c0.w) + c2.xyz * (ndc.y * c1.w)) * zc;   // camera-relative, now
    float3 pa  = seenBy(q, c5, c6, c7);
    if (pa.z <= 0.0) discard;
    float2 v   = (px - pa.xy) * c3.x;                            // its path over the shutter
    float  len = length(v);
    if (len <= c3.z) discard;                                   // (nearly) still: the pixel stays exactly as it is
    float  use = min(len - c3.z * 0.5, c3.y);                   // a soft start past the dead zone, a longest streak
    use *= stillWorld(q);
    if (use <= c3.z * 0.5) discard;                             // it moves by itself: sharp
    float2 dir = v / len;
    [branch] if ((uint(c3.w) & 4u) != 0u) return float4(saturate(abs(dir) * use / c3.y), saturate(use / c3.y) * 0.5, 1.0);
    int    n   = clamp(int(ceil(use * 0.75)) | 1, 3, 15);
    float  j   = ign(px + c2.w * 5.588238) - 0.5;
    float2 st  = dir * (use / float(n));
    float2 p   = px - dir * (use * 0.5) + st * (0.5 + j);
    float2 da  = abs(dir);
    float3 own = sceneAt(int2(px)); own *= own;
    float3 acc = own; float wsum = 1.0;                          // the pixel itself always counts
    bool   fine = use < 6.0;                                    // short streaks: blend between pixels (see sceneLin)
    float  zNear = zc * 0.88 - 0.12;
    [loop] for (int k = 0; k < n; ++k, p += st) {
        // what stands clearly in FRONT of this pixel is not gathered: a player, your own character, a near block keep a
        // clean outline against the blurred world behind them instead of bleeding a ghost into it
        float w = viewZ(rawDepth(int2(p))) < zNear ? 0.0 : 1.0;
        float3 s2;
        if (fine) s2 = sceneLin(p, da);
        else { s2 = sceneAt(int2(p)); s2 *= s2; }
        acc += s2 * w; wsum += w;
    }
    return float4(sqrt(acc / wsum), 1.0);
}
