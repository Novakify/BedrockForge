// Renderer Foundation - World FX: camera motion blur, drawn on the world's own targets right after the World FX pass
// (before the hand and the HUD are drawn: they stay sharp). Original code for Renderer Foundation.
//
// Why here and not at Present (shaders/postfx.hlsl has the old one): at Present there is no depth, so the blur could
// only follow turning, and it smeared the HUD, the crosshair and the held item. Here the depth copy of the pass is at
// hand: every pixel gets its own velocity from where its world point was a shutter-time ago - turning, walking,
// strafing, falling. Near things streak more than far ones, the sky only when you turn.
//
// Butter, not smear:
//   * the shutter is a TIME (ms), not "one frame": the blur has the same length at 60 and at 240 FPS, and SkyPass
//     smooths the camera's velocity, so uneven frame times do not make the blur length flicker
//   * centred on the pixel (half back, half ahead): the picture does not lag behind the camera
//   * under ~1 px of motion nothing is touched (discard): slow pans and standing still stay pin sharp and cost nothing
//   * averaged in linear light: bright things streak the way a camera sees them, dark ones do not go muddy
//   * a different dither every frame: the gaps between taps dissolve into the next frames instead of banding
cbuffer C : register(b0) {
    float4 c0; // xyz = camera forward, w = tan(fovx/2)
    float4 c1; // xyz = camera right,   w = tan(fovy/2)
    float4 c2; // xyz = camera up,      w = frame seed
    float4 c3; // x = near plane, y = longest blur (px), z = dead zone (px), w = flags (1 = scene is BGRA)
    float4 c4; // xy = target size, z = scene row pitch (bytes), w = depth row pitch (bytes)
    float4 c5; // xyz = the camera's turn over the shutter (world axis * radians), w = most taps
    float4 c6; // xyz = the camera's travel over the shutter (world, blocks), w = debug (1 = show the velocity)
    float4 c7, c8, c9, c10;
};
ByteAddressBuffer gScene : register(t0);
ByteAddressBuffer gDepth : register(t1);

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

// where this pixel's world point was on screen a shutter-time ago -> its velocity in pixels over the shutter
float2 velocityAt(float2 px, float d) {
    float2 ndc = float2(px.x / c4.x * 2.0 - 1.0, 1.0 - px.y / c4.y * 2.0);
    float  z   = d >= 0.99999 ? 1e6 : c3.x / max(1.0 - d, 1e-7);
    float3 q   = (c0.xyz + c1.xyz * (ndc.x * c0.w) + c2.xyz * (ndc.y * c1.w)) * z;   // camera-relative, now
    q += c6.xyz;                           // the camera was that far back: the point was that much further along
    q += cross(c5.xyz, q);                 // and it looked the other way by this turn (small angle)
    float  zf = dot(q, c0.xyz);
    if (zf < 1e-3) return 0.0;             // it was behind the camera
    float2 pn = float2(dot(q, c1.xyz) / (zf * c0.w), dot(q, c2.xyz) / (zf * c1.w));
    float2 pp = float2((pn.x * 0.5 + 0.5) * c4.x, (0.5 - pn.y * 0.5) * c4.y);
    return px - pp;
}

float4 PSWorldFxBlur(VSOut i) : SV_Target {
    float2 px = i.pos.xy;
    float2 v  = velocityAt(px, rawDepth(int2(px)));
    float  len = length(v);
    if (len <= c3.z) discard;                                   // (nearly) still: the pixel stays exactly as it is
    float  use = min(len - c3.z * 0.5, c3.y);                   // a soft start past the dead zone, a longest streak
    float2 dir = v / len;
    [branch] if (c6.w > 0.5) return float4(saturate(abs(dir) * use / c3.y), saturate(use / c3.y) * 0.5, 1.0);
    int    n   = clamp(int(ceil(use * 0.75)) | 1, 3, int(c5.w));
    float  j   = ign(px + c2.w * 5.588238) - 0.5;
    float2 st  = dir * (use / float(n));
    float2 p   = px - dir * (use * 0.5) + st * (0.5 + j);
    float2 da  = abs(dir);
    float3 acc = 0.0;
    bool   fine = use < 6.0;                                    // short streaks: blend between pixels (see sceneLin)
    [loop] for (int k = 0; k < n; ++k, p += st) {
        if (fine) acc += sceneLin(p, da);
        else { float3 s = sceneAt(int2(p)); acc += s * s; }
    }
    return float4(sqrt(acc / float(n)), 1.0);
}
