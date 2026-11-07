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
    uint   pad;        // 1 = debug solid colour
    // M2: LyricsForge (BF Director), neutral when no song plays
    float  beat, bass, impact, flash;
    float3 flashCol;
    float  active;
    float3 pal;        // the sky's light colour (brightest channel 1)
    float  palAmount;
    float  exposure;
    uint   prevPitch;
    float  prevW, prevH;
    uint   prevBgra, havePrev;
    float  reflectAmt, react;
    // M2.5: LyricsForge glass block (all neutral when lf = 0)
    float3 cover;  float coverK;   // song / chapter cover colour
    float3 skyC;   float skyK;     // LyricsForge sky colour
    float3 wordCol; float wordColK;// colour word flash
    float  awaken, drop, finale, wordT;
    float  idleEdge, lf, beatK, pad2;
};
ByteAddressBuffer gPrev : register(t2, space9); // last finished frame (lyrics, sky, post) for reflections

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
float3 prevTexel(int2 p) {
    p      = clamp(p, int2(0, 0), int2(prevW, prevH) - 1);
    uint v = gPrev.Load(uint(p.y) * prevPitch + uint(p.x) * 4);
    float3 c = float3(v & 255u, (v >> 8) & 255u, (v >> 16) & 255u) / 255.0;
    return prevBgra ? c.bgr : c;
}
float3 prevAt(float2 px) { // the frame is the same size as the world target in practice; scale if it isn't
    px *= float2(prevW, prevH) / res;
    float2 q = px - 0.5;
    int2   i = int2(floor(q));
    float2 f = q - float2(i);
    return lerp(lerp(prevTexel(i), prevTexel(i + int2(1, 0)), f.x), lerp(prevTexel(i + int2(0, 1)), prevTexel(i + int2(1, 1)), f.x), f.y);
}
float depthAt(float2 px) {
    int2 p = clamp(int2(px), int2(0, 0), int2(res) - 1);
    return float(gDepth.Load(uint(p.y) * depthPitch + uint(p.x) * 4) & 0xFFFFFFu) / 16777215.0;
}

float4 PSGlassArmor(PSIn i) : SV_Target {
    float2 t = i.uv * float2(64.0, 32.0); // armor texel space
    int2   b = int2(floor(t));
    if (pad == 1u) return float4(1.0, 0.0, 1.0, 1.0); // debug: does the replayed draw land at all?
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

    // M2.5 Awaken: the neon boots piece by piece over the ~5 s startup (boots, leggings, chest, helmet) and powers
    // down in reverse when the world exhales at the end. No music = plain clear glass with a faint outline.
#if GA_LAYER == 1
    float stage = fi < 12 ? 3.0 : (fi < 24 ? 2.0 : 0.0);
#else
    float stage = 1.0;
#endif
    float th     = stage * 0.19;
    float boot   = lf > 0.5 ? smoothstep(th, th + 0.22, awaken) : 1.0;
    float ignite = lf > 0.5 ? exp(-pow((awaken - th - 0.2) / 0.045, 2.0)) : 0.0; // the moment a piece lights
    float edgeLv = lf > 0.5 ? lerp(idleEdge, 1.0, boot) + ignite * 1.5 : 1.0;

    // enchant: edge flow (light travelling round the outline)
    float ench = 0;
    if (glint > 0.5 && glint < 1.5) ench = 0.5 + 0.5 * sin(time * 2.4);
    if (glint > 1.5) { float per = (p.x + p.y) / (fs.x + fs.y); ench = pow(0.5 + 0.5 * sin((per * 2.0 - time * 0.55) * 6.2831), 6.0); }
    // LyricsForge: the outline light runs round the edges with the music and pulses on the beat
    float mus = active * react * boot;
    if (mus > 0.5) {
        float per  = (p.x + p.y) / (fs.x + fs.y);
        float flow = pow(0.5 + 0.5 * sin((per * 2.0 - time * (0.45 + 1.6 * bass)) * 6.2831), 5.0);
        ench = max(ench, flow * (0.35 + 0.65 * beat) + impact * 0.8);
    }
    // cover-coloured glass: the neon takes the cover's accent (a vivid version of it), the frost takes the cover itself
    float  cMix = saturate(coverK * mus);
    float  cMax = max(max(cover.r, cover.g), max(cover.b, 1e-3));
    float3 acc  = cover / cMax;
    acc = saturate(lerp(dot(acc, float3(0.299, 0.587, 0.114)).xxx, acc, 1.35));
    float3 edgeB = lerp(GA_EDGE, acc, cMix * 0.85);
    edgeB = lerp(edgeB, wordCol * 1.2, saturate(wordColK * boot) * 0.85); // colour words flash the outline
    // beat-pulse: brighter on every kick, a big flare on the drop
    float kick  = mus * beat * beatK;
    float flare = drop * boot * react;
    float3 edgeC = edgeB * (1.0 + ench * 1.2 + kick * 0.9 + flare * 1.4 + ignite * 1.2);
    float3 palC  = lerp(float3(1, 1, 1), pal, saturate(palAmount * mus));
    palC = lerp(palC, skyC, saturate(skyK * boot * react) * 0.75); // the rim reflects the LyricsForge sky above you

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
    float3 frost = lerp(lerp(float3(0.0065, 0.0032, 0.018), GA_TINT, 0.35), cover * 0.85, cMix);
    c   = lerp(c, frost, 0.10 + 0.12 * cMix); // a dark cover gives deep glass, a bright one glows through

    // reflection of the last finished frame: lyrics, sky and LyricsForge effects glint across the glass, strongest at
    // the rims (fresnel-like) - sampled mirrored around the face and pushed outwards along the edge normal
    if (havePrev) {
        float2 rp   = px - off * 1.6 + n * (24.0 * S) - fo * 0.0;
        float3 refl = (prevAt(rp) + prevAt(rp + float2(3, 0) * S) + prevAt(rp + float2(0, 3) * S)) / 3.0;
        float  lumR = dot(refl, float3(0.299, 0.587, 0.114));
        float  w    = reflectAmt * (0.10 + 0.55 * e) * smoothstep(0.25, 0.85, lumR + 0.25); // bright things reflect most
        c = 1.0 - (1.0 - c) * (1.0 - refl * w);                                          // screen blend
    }

    // lyric glint: one light sweep across each plate per sung word, like a reflection passing over it
    if (wordT < 0.6) {
        float sp   = (p.x / fs.x) * 0.65 + (p.y / fs.y) * 0.35;
        float pos  = wordT / 0.34 * 1.5 - 0.25;
        float band = exp(-pow((sp - pos) / 0.07, 2.0)) * (1.0 - saturate(wordT / 0.6)) * mus;
        c += band * 0.32 * lerp(float3(1, 1, 1), edgeB, 0.35);
    }
    // stars in the glass during the constellation outro
    if (finale > 0.01) {
        float2 sc = t * 3.0;
        float2 g  = floor(sc);
        float  h  = frac(sin(dot(g, float2(12.9898, 78.233))) * 43758.5453);
        if (h > 0.9) {
            float tw = 0.5 + 0.5 * sin(time * (2.0 + h * 3.0) + h * 40.0);
            float sd = length(frac(sc) - 0.5);
            float st = smoothstep(0.24, 0.0, sd) * tw * finale;
            c += st * lerp(float3(1.0, 0.97, 0.9), acc, 0.25 * cMix) * 1.3;
        }
        c *= 1.0 - 0.18 * finale * (1.0 - e); // the glass darkens a little, like the night the stars sit in
    }

    // menu rim: white, brighter on the side facing the light (top-left, like the client menu)
    float  rim = 1.0 - smoothstep(0.0, 1.6, abs(d + 0.9));
    float2 Ls  = normalize(float2(-0.45, -0.89));
    float  lit = 0.45 + 0.55 * max(dot(n, Ls), 0.0);
    c += 0.9 * palC * (rim * lit * 0.75 + e * 0.08) * lerp(1.0, exposure, 0.5 * mus);
    c += flashCol * flash * mus * (0.6 + 1.2 * e);   // impact frames light the glass up
    // neon outline + its light glowing into the glass
    float3 E   = GA_EDGE;
    float  sat = max(max(E.r, E.g), E.b) - min(min(E.r, E.g), E.b);
    c += lerp(edgeC, edgeC * palC, 0.5) * (0.24 + 0.30 * ench + 0.25 * kick + 0.4 * flare) * edgeLv * lerp(0.45, 1.0, saturate(sat * 1.5)) * pow(saturate(1.0 + d / max(bevel, 1.0)), 3.0);
    c += edgeC * 0.015 * edgeLv;
    float aa = max(fwidth(dIn), 1e-4) * 1.1;
    float lw = 0.19 + 0.07 * kick + 0.10 * min(flare, 1.0);  // a touch thicker on the kick, thicker still on the drop
    float ln = 1.0 - smoothstep(lw - aa, lw + aa, dIn);
    c = lerp(c, edgeC * 1.25, ln * saturate(edgeLv));
    return float4(saturate(c), 1.0);
}
