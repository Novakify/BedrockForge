// Renderer Foundation - Glass Items pixel shader (Purxiy "Glass": Items). Runs instead of the game's ItemInHandColor
// pixel shader on held items (first + third person), dropped items and item frames, replayed after the actor pass with a
// copy of the colour + depth targets like Glass Armor / Glass Body.
// The item stays itself, made of glass (Fernando 2026-11-17: the neon is the vanilla item's own colour, bright):
//   * the world behind bends through it (a lens toward the rim, a fixed twist - nothing swims)
//   * its own colours fill the glass, so a diamond sword is aqua glass with a warm handle, a golden apple amber
//   * a neon line round the sprite's shape in a vivid version of the colour right there
//   * Frosted / Glow / Shimmer / Colour from the client's Glass module, LyricsForge beat, Novak night glow
// The item comes from the item ATLAS the draw itself samples (the game's t1); the draw's UVs point into it. (The draw's first texture, 16x16, is the lightmap - the
// first version read that and every item became a solid glass box.)

ByteAddressBuffer gScene : register(t0, space9);
ByteAddressBuffer gDepth : register(t1, space9);
ByteAddressBuffer gPrev  : register(t2, space9);
// the item ATLAS straight from the game's own texture table (root parameter 1: t0 = the 16x16 lightmap, t1 = the atlas),
// which the replay binds exactly as the game did. (2026-11-18: the first version copied the atlas into our own buffer
// through a texture pointer RF tracked from descriptor writes - after a teleport that pointer could name a texture the
// game had already freed, and the copy removed the device: the game died in the driver. No copy, no pointer now.)
Texture2D<float4> gAtlas : register(t1);
// must match GaParams (src/GlassArmor.cpp) and cbuffer GA in glass_armor.hlsl
cbuffer GA : register(b0, space9) {
    float2 res;
    float  time;
    float  glint;
    uint   scenePitch;
    uint   depthPitch;
    float  swirl;
    float  ripple;
    float  lens;
    float  uiScale;
    uint   sceneBgra;
    uint   pad;
    float  beat, bass, impact, flash;
    float3 flashCol;
    float  active;
    float3 pal;
    float  palAmount;
    float  exposure;
    uint   prevPitch;
    float  prevW, prevH;
    uint   prevBgra, havePrev;
    float  reflectAmt, react;
    float3 cover;  float coverK;
    float3 skyC;   float skyK;
    float3 wordCol; float wordColK;
    float  awaken, drop, finale, wordT;
    float  idleEdge, lf, beatK, pad2;
    int    dyeRow, dyeRowGlint;
    uint   dbgRow;
    float  enchFlow;
    float  fusedK, filmK, pad3, pad4;
    float  frost, glowK, shimmer;
    uint   tintPk;
};

// the ItemInHandColor vertex shader's output signature (probe 2026-11-17): COLOR3 and COLOR1 swapped vs Actor
struct PSIn {
    float4 pos : SV_Position;
    float4 c3  : COLOR3;
    float4 c0  : COLOR0;
    float4 c2  : COLOR2;
    float4 c1  : COLOR1;
    float2 uv  : TEXCOORD0;
    float3 t3  : TEXCOORD3;
};

uint2 itemSize() { uint w, h; gAtlas.GetDimensions(w, h); return uint2(w, h); }
float4 itemAt(int2 t, int2 sz) {
    if (t.x < 0 || t.y < 0 || t.x >= sz.x || t.y >= sz.y) return float4(0, 0, 0, 0);
    return gAtlas.Load(int3(t, 0));   // the view's own format: RGBA / BGRA come out in order
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
float3 prevAt(float2 px) {
    int2 p = clamp(int2(px * float2(prevW, prevH) / res), int2(0, 0), int2(prevW, prevH) - 1);
    uint v = gPrev.Load(uint(p.y) * prevPitch + uint(p.x) * 4);
    float3 c = float3(v & 255u, (v >> 8) & 255u, (v >> 16) & 255u) / 255.0;
    return prevBgra ? c.bgr : c;
}
float gaHash(float2 q) { return frac(sin(dot(q, float2(127.1, 311.7))) * 43758.5453); }
float3 vivid(float3 c) {   // the colour, bright: brightest channel 1, a little more saturated; no hue -> a cool white
    float m = max(max(c.r, c.g), c.b);
    float n = min(min(c.r, c.g), c.b);
    float3 v = c / max(m, 1e-3);
    v = saturate(lerp(dot(v, float3(0.299, 0.587, 0.114)).xxx, v, 1.4));
    float grey = 1.0 - smoothstep(0.06, 0.20, m - n) * smoothstep(0.03, 0.10, m);
    return lerp(v, float3(0.86, 0.93, 1.00), grey);
}

float4 PSGlassItem(PSIn i) : SV_Target {
    int2   sz = int2(itemSize());
    if (pad == 1u) return float4(1.0, 0.0, 1.0, 1.0);   // debug: does the replayed draw land?
    if (sz.x < 1 || sz.y < 1) discard;
    float2 t  = saturate(i.uv) * float2(sz);              // atlas texel space (the draw's UVs point into the atlas)
    int2   b  = int2(floor(t));
    float4 it = itemAt(b, sz);
    if (it.a <= 0.1) discard;                            // the item's own shape

    // distance (texels) to the sprite's outline
    float dIn = 1.0;
    [unroll] for (int dy = -1; dy <= 1; ++dy)
    [unroll] for (int dx = -1; dx <= 1; ++dx) {
        if (dx == 0 && dy == 0) continue;
        int2 n = b + int2(dx, dy);
        if (itemAt(n, sz).a > 0.1) continue;
        float2 q = abs(t - (float2(n) + 0.5)) - 0.5;
        dIn = min(dIn, length(max(q, 0.0)));
    }
    float2 tx = ddx(t), ty = ddy(t);
    float  dj = tx.x * ty.y - ty.x * tx.y;
    float  pxPerTexel = rsqrt(max(abs(dj), 1e-8));
    float  S  = uiScale;
    float  d  = -dIn * pxPerTexel;
    float2 gr = float2(ddx(-dIn), ddy(-dIn));
    float2 n  = gr / max(length(gr), 1e-6);
    // the item's own cell in the atlas (16 texels in a 1024 atlas, scaled with packs): the twist turns about its centre
    float  cell  = max(16.0, 16.0 * float(sz.x) / 1024.0);
    float2 cc    = (floor(t / cell) + 0.5) * cell;
    float  bevel = min(10.0 * S, cell * 0.25 * pxPerTexel);
    float  e     = pow(1.0 - saturate(-d / max(bevel, 1.0)), 2.2);

    // the bend: a lens toward the outline + a fixed twist about the sprite's centre (static)
    float2 rel = t - cc;
    float  rr  = length(rel) / (cell * 0.5);
    float  ang = swirl * 0.6 * smoothstep(1.2, 0.0, rr);
    float  cs = cos(ang), sn = sin(ang);
    float2 fo = float2(cs * rel.x - sn * rel.y, sn * rel.x + cs * rel.y) - rel;
    float2 off = abs(dj) > 1e-8 ? float2(ty.y * fo.x - ty.x * fo.y, -tx.y * fo.x + tx.x * fo.y) / dj : float2(0, 0);
    off  = clamp(off, -28.0 * S, 28.0 * S);
    off += -n * 10.0 * lens * S * e;
    float frz = saturate(frost);
    off *= lerp(1.0, 0.45, frz);
    float2 px = i.pos.xy;

    float3 c;
    c.r = sceneAt(px + off).r;
    c.g = sceneAt(px + off * 1.04).g;
    c.b = sceneAt(px + off * 1.08).b;
    // the item's own colour in the glass: enough that it is unmistakably that item, still see-through
    float3 ic = it.rgb;
    float  iL = dot(ic, float3(0.299, 0.587, 0.114));
    float  soak = 0.55 * lerp(1.0, 0.7, iL), own = 0.36 * lerp(1.0, 0.55, iL);
    c = lerp(c, c * ic * 1.3 + ic * 0.08, soak);
    c = lerp(c, ic, own);
    float3 tintF = float3(tintPk & 255u, (tintPk >> 8) & 255u, (tintPk >> 16) & 255u) / 255.0;
    if (frz > 0.001) {
        float  R  = (2.0 + 22.0 * frz) * S;
        float  a0 = gaHash(px * 0.37 + 3.1) * 6.2831853;
        float3 bd = 0;
        [unroll] for (int k = 0; k < 12; ++k) {
            float an = a0 + float(k) * 2.3999632;
            float rk = sqrt((float(k) + 0.5) / 12.0) * R;
            bd += texel(int2(px + off + float2(cos(an), sin(an)) * rk));
        }
        bd /= 12.0;
        bd = lerp(bd, dot(bd, float3(0.2126, 0.7152, 0.0722)).xxx, 0.30 * frz);
        float milk = 0.06 + 0.46 * frz * frz;
        bd = bd * (1.0 - milk) + lerp(tintF, tintF * ic, 0.35) * milk * 0.92;
        bd = lerp(lerp(bd, bd * ic * 1.3 + ic * 0.08, soak), ic, own);
        c = lerp(c, bd, smoothstep(0.0, 0.30, frz));
    }
    if (havePrev) {   // the last finished frame glints at the rims
        float3 refl = prevAt(px - off * 1.6 + n * (16.0 * S));
        float  w    = reflectAmt * (0.08 + 0.45 * e) * smoothstep(0.25, 0.85, dot(refl, float3(0.299, 0.587, 0.114)) + 0.25);
        c = 1.0 - (1.0 - c) * (1.0 - refl * w);
    }
    // a polished sheen across the item, fixed to it (turns with it, doesn't crawl)
    float2 tc    = (t - cc) / cell + 0.5;                // 0..1 across the item's cell
    float sheen = exp(-pow((frac((tc.x - tc.y) * 0.5 + 0.35) - 0.5) / 0.08, 2.0));
    c += sheen * 0.10 * (1.0 - 0.6 * frz) * lerp(float3(1, 1, 1), vivid(ic), 0.3);
    c += lerp(tintF, tintF * ic, 0.25) * glowK * (0.10 + 0.20 * e);
    if (shimmer > 0.5) {
        float ph2 = frac(time / 6.0 + 0.37);
        float sp2 = tc.x * 0.7 + tc.y * 0.3;
        float k2  = (sp2 - (ph2 * 3.2 - 1.1)) / 0.09;
        c += exp(-k2 * k2) * (0.10 + 0.10 * frz) * lerp(float3(1, 1, 1), tintF, 0.3);
    }
    float mus   = active * react;
    float kick  = mus * beat * beatK;
    float flare = drop * react;
    float ngK   = pad4 > 0.5 && pad4 < 1.5 ? smoothstep(0.30, 0.95, pad3) : 0.0;
    float3 edgeC = vivid(ic) * (1.0 + kick * 0.9 + flare * 1.4 + 0.45 * ngK);
    float  rim = 1.0 - smoothstep(0.0, 1.6, abs(d + 0.9));
    float  lit = 0.45 + 0.55 * max(dot(n, normalize(float2(-0.45, -0.89))), 0.0);
    c += 0.6 * (rim * lit * 0.6 + e * 0.05);
    c += flashCol * flash * mus * (0.5 + 1.0 * e);
    c += edgeC * (0.22 + 0.25 * kick + 0.35 * flare + 0.20 * ngK) * pow(saturate(1.0 + d / max(bevel, 1.0)), 3.0);
    c = lerp(c, c * lerp(float3(1, 1, 1), vivid(ic), 0.3), 0.5 * ngK) + vivid(ic) * 0.06 * ngK;
    float aa = max(fwidth(dIn), 1e-4) * 1.1;
    float lw = 0.18 + 0.06 * kick + 0.08 * min(flare, 1.0);
    lw = clamp(lw, 0.9 * S / max(pxPerTexel, 1e-3), 2.2 * S / max(pxPerTexel, 1e-3));
    float ln = 1.0 - smoothstep(lw - aa, lw + aa, dIn);
    c = lerp(c, edgeC * 1.2, ln);
    return float4(saturate(c), 1.0);
}
