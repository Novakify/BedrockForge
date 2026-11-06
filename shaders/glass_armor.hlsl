// Renderer Foundation - Glass Armor pixel shader, M1a (see docs/GLASS_ARMOR.md on research/glass-armor).
// Runs instead of the game's Actor pixel shader on the game's own armor draws (same vertex shader, same geometry,
// same depth test). M1a: see-through tinted glass + neon outline tracing the vanilla texture's painted pixels,
// alpha-blended over the real world behind. The twist / refraction (scene copy) is M1b.
//
// Compiled once per armor texture variant by tools/gen_glassarmor.py with:
//   GA_VARIANT  index into kMasks (material x layer)   GA_LAYER  1 = helmet/chest/boots texture, 2 = leggings
//   GA_TINT     float3 material tint (linear)           GA_EDGE   float3 neon outline colour (linear)
#include "glass_armor_data.hlsli" // generated: kMasks[][64], kFaces1[30], kFaces2[12]

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

int faceOf(int2 t) {
#if GA_LAYER == 1
    [unroll] for (int i = 0; i < 30; ++i) {
        int4 r = kFaces1[i];
#else
    [unroll] for (int i = 0; i < 12; ++i) {
        int4 r = kFaces2[i];
#endif
        if (t.x >= r.x && t.y >= r.y && t.x < r.x + r.z && t.y < r.y + r.w) return i;
    }
    return -1;
}

float4 PSGlassArmor(PSIn i) : SV_Target {
    float2 p = i.uv * float2(64.0, 32.0); // armor texel space
    int2   b = int2(floor(p));
    if (!painted(b)) discard;             // vanilla shape (alpha test)
    int f = faceOf(b);

    // distance (texels) to the nearest edge of the painted area / of this cube face
    float d = 1.0;
    [unroll] for (int dy = -1; dy <= 1; ++dy)
    [unroll] for (int dx = -1; dx <= 1; ++dx) {
        if (dx == 0 && dy == 0) continue;
        int2 n = b + int2(dx, dy);
        if (painted(n) && faceOf(n) == f) continue;
        float2 q = abs(p - (float2(n) + 0.5)) - 0.5;
        d = min(d, length(max(q, 0.0)));
    }

    float aa   = max(fwidth(d), 1e-4) * 1.2;
    float ln   = 1.0 - smoothstep(0.17 - aa, 0.17 + aa, d);          // neon outline
    float glow = exp(-d / 0.42);                                       // outline light spilling into the glass
    float3 edge = GA_EDGE, tint = GA_TINT;

    float3 col = tint * 0.55 + edge * glow * 0.85 + 0.12 * glow;
    float  a   = saturate(0.09 + 0.38 * glow);
    col = lerp(col, edge * 1.15, ln);
    a   = lerp(a, 0.96, ln);
    return float4(col, a);
}
