// Renderer Foundation - Glass Armor pixel shader (approved look: docs/glass_armor/preview_v6.html on research/glass-armor).
// Runs instead of the game's Actor pixel shader on the game's own armor draws (same vertex shader, geometry, pose,
// depth test), replayed after the actor pass with a copy of the colour + depth targets taken right then:
//   * clear glass with a FIXED twist per face (no animation) - only what is behind your body bends,
//     your own limbs under the glass stay clean (depth check)
//   * client-menu lens at the rim + small chromatic split, white rim that brightens towards the light
//   * neon outline tracing the vanilla texture's painted pixels, glowing into the glass
//   * enchanted: edge flow (light travelling round the outline)
//
// Compiled once per armor texture variant by tools/gen_glassarmor.py with:
//   GA_VARIANT  index into kMasks (material x layer)   GA_LAYER  1 = helmet/chest/boots texture, 2 = leggings
//   GA_TINT     float3 material tint (linear)           GA_EDGE   float3 neon outline colour (linear)
#include "glass_armor_data.hlsli" // generated: kMasks[][64], kFaces1[30], kFaces2[12]

ByteAddressBuffer gScene : register(t0, space9); // colour target copy (RGBA8 rows of scenePitch bytes)
ByteAddressBuffer gDepth : register(t1, space9); // depth copy (D24 in the low 24 bits, rows of depthPitch bytes)
cbuffer GA : register(b0, space9) {
    float2 res;
    float  time;
    float  glint;      // 0 off, 1 edge pulse, 2 edge flow
    uint   scenePitch;
    uint   depthPitch;
    float  swirl;      // fixed twist (rad)
    float  ripple;
    float  lens;
    float  uiScale;    // res.y / 760 (menu design px -> px)
    uint   sceneBgra;
    uint   pad;
};

// Must match the Actor vertex shader's output signature exactly (probe M0):
// SV_Position o0, COLOR1 o1, COLOR0 o2, COLOR2 o3, COLOR3 o4, TEXCOORD0.xy o5, TEXCOORD3.xyz o6
struct PSIn {
    float4 pos : SV_Position;
    float4 c1  : COLOR1;
    float4 c0  : COLOR0;
    float4 c2  : COLOR2;
    float4 c3  : COLOR3;
    float2 uv  : TEXCOORD0;
    float3 t3  : TEXCOORD3;
};

bool painted(int2 t) {
    if (t.x < 0 || t.y < 0 || t.x > 63 || t.y > 31) return false;
    uint w = kMasks[GA_VARIANT][t.y * 2 + (t.x >> 5)];
    return ((w >> (31 - (t.x & 31))) & 1u) != 0;
}

int4 faceRect(int i) {
#if GA_LAYER == 1
    return kFaces1[i];
#else
    return kFaces2[i];
#endif
}

int faceOf(int2 t) {
#if GA_LAYER == 1
    [unroll] for (int i = 0; i < 30; ++i) {
#else
    [unroll] for (int i = 0; i < 12; ++i) {
#endif
        int4 r = faceRect(i);
        if (t.x >= r.x && t.y >= r.y && t.x < r.x + r.z && t.y < r.y + r.w) return i;
    }
    return -1;
}

float3 texel(int2 p) {
    p      = clamp(p, int2(0, 0), int2(res) - 1);
    uint v = gScene.Load(uint(p.y) * scenePitch + uint(p.x) * 4);
    float3 c = float3(v & 255u, (v >> 8) & 255u, (v >> 16) & 255u) / 255.0;
    return sceneBgra ? c.bgr : c;
}
float3 sceneAt(float2 px) {
    float2 q = px - 0.5;
    int2   i = int2(floor(q));
    float2 f = q - float2(i);
    return lerp(lerp(texel(i), texel(i + int2(1, 0)), f.x), lerp(texel(i + int2(0, 1)), texel(i + int2(1, 1)), f.x), f.y);
}
float depthAt(float2 px) {
    int2 p = clamp(int2(px), int2(0, 0), int2(res) - 1);
    return float(gDepth.Load(uint(p.y) * depthPitch + uint(p.x) * 4) & 0xFFFFFFu) / 16777215.0;
}

float4 PSGlassArmor(PSIn i) : SV_Target {
    float2 t = i.uv * float2(64.0, 32.0); // armor texel space
    int2   b = int2(floor(t));
    if (!painted(b)) discard;             // vanilla shape
    int  fi = faceOf(b);
    int4 fr = faceRect(max(fi, 0));
    float2 fs = float2(fr.zw), p = t - float2(fr.xy); // face size + face-local position (texels)

    // distance (texels) to the nearest edge of the painted area / of this cube face
    float dIn = 1.0;
    [unroll] for (int dy = -1; dy <= 1; ++dy)
    [unroll] for (int dx = -1; dx <= 1; ++dx) {
        if (dx == 0 && dy == 0) continue;
        int2 n = b + int2(dx, dy);
        if (painted(n) && faceOf(n) == fi) continue;
        float2 q = abs(t - (float2(n) + 0.5)) - 0.5;
        dIn = min(dIn, length(max(q, 0.0)));
    }

    // texel -> screen px (the face is planar: J maps screen px to texels)
    float2   tx = ddx(t), ty = ddy(t);
    float    dj = tx.x * ty.y - ty.x * tx.y;
    float    pxPerTexel = rsqrt(max(abs(dj), 1e-8));
    float    S  = uiScale;
    float    d  = -dIn * pxPerTexel;               // signed edge distance in screen px (- inside)
    float2   gr = float2(ddx(-dIn), ddy(-dIn));
    float2   n  = gr / max(length(gr), 1e-6);      // outward, screen space (y down)
    float    halfPx = min(fs.x, fs.y) * 0.5 * pxPerTexel;

    // enchant: edge flow (light travelling round the outline)
    float ench = 0;
    if (glint > 0.5 && glint < 1.5) ench = 0.5 + 0.5 * sin(time * 2.4);
    if (glint > 1.5) { float per = (p.x + p.y) / (fs.x + fs.y); ench = pow(0.5 + 0.5 * sin((per * 2.0 - time * 0.55) * 6.2831), 6.0); }
    float3 edgeC = GA_EDGE * (1.0 + ench * 1.2);

    // fixed twist around the face centre + gentle fixed wave
    float  bevel = min(22.0 * S, halfPx * 0.85);
    float  e     = pow(1.0 - saturate(-d / max(bevel, 1.0)), 2.2);
    float2 rel   = p - fs * 0.5;
    float  rr    = length(rel) / max(min(fs.x, fs.y) * 0.5, 1e-3);
    float  ph    = frac(sin(dot(float2(fr.xy), float2(12.9898, 78.233))) * 43758.5);
    float  ang   = swirl * smoothstep(1.15, 0.0, rr) * (ph > 0.5 ? 1.0 : -1.0);
    float  cs = cos(ang), sn = sin(ang);
    float2 fo = float2(cs * rel.x - sn * rel.y, sn * rel.x + cs * rel.y) - rel;
    fo += ripple * float2(sin(p.y * 0.9 + ph * 6.0), cos(p.x * 0.9 + ph * 4.0));
    float2 off = abs(dj) > 1e-8 ? float2(ty.y * fo.x - ty.x * fo.y, -tx.y * fo.x + tx.x * fo.y) / dj : float2(0, 0);
    off  = clamp(off, -42.0 * S, 42.0 * S);
    off += -n * 16.0 * lens * S * e;

    // your own body right under the glass stays clean: only bend what is further behind (standard Z: 1-z ~ 1/dist)
    float2 px    = i.pos.xy;
    float  self  = 1.0 - i.pos.z;
    float  ratio = min(self / max(1.0 - depthAt(px), 1e-6), self / max(1.0 - depthAt(px + off), 1e-6));
    off *= smoothstep(1.03, 1.18, ratio);

    float3 c;
    c.r = sceneAt(px + off).r;
    c.g = sceneAt(px + off * 1.04).g;
    c.b = sceneAt(px + off * 1.08).b;
    c   = lerp(c, lerp(float3(0.0065, 0.0032, 0.018), GA_TINT, 0.35), 0.10);

    // menu rim: white, brighter on the side facing the light (top-left, like the client menu)
    float  rim = 1.0 - smoothstep(0.0, 1.6, abs(d + 0.9));
    float2 Ls  = normalize(float2(-0.45, -0.89));
    float  lit = 0.45 + 0.55 * max(dot(n, Ls), 0.0);
    c += 0.9 * (rim * lit * 0.75 + e * 0.08);
    // neon outline + its light glowing into the glass
    float3 E   = GA_EDGE;
    float  sat = max(max(E.r, E.g), E.b) - min(min(E.r, E.g), E.b);
    c += edgeC * (0.24 + 0.30 * ench) * lerp(0.45, 1.0, saturate(sat * 1.5)) * pow(saturate(1.0 + d / max(bevel, 1.0)), 3.0);
    c += edgeC * 0.015;
    float aa = max(fwidth(dIn), 1e-4) * 1.1;
    float ln = 1.0 - smoothstep(0.19 - aa, 0.19 + aa, dIn);
    c = lerp(c, edgeC * 1.25, ln);
    return float4(saturate(c), 1.0);
}
