// Renderer Foundation - LyricsFlow world text (glyph quads). Original code for Renderer Foundation / Bedrock Forge.
// One instanced draw: 6 vertices per glyph, glyph data from Bedrock Forge (BF_CopyLyricFrame, src/BfLyrics.h).
// Look: SDF fill (+ vertical gradient), outline, soft glow; MSDF (median of rgb) and pixel bitmap (nearest) atlases.
// Output is premultiplied alpha (blend ONE / INV_SRC_ALPHA, same as panel.hlsl).
//
// Root constants (b0, 24 floats):
//   c0..c3  viewProj, column-major, camera-relative (input = glyph.pos + c4.xyz)
//   c4      xyz: BF origin minus camera position (world units), w: time (s)
//   c5      x: screen aspect (width / height), y: global opacity, z: debug (1 = show quad bounds), w: screen height (px)
// t0 = StructuredBuffer<Glyph>, t1..t16 = atlases (index = Glyph.atlas), t17 = S2 UI target, s0 = linear clamp, s1 = point clamp
// t18 = copy of the frame (glass), t19 / t20 = quarter-res blur ping-pong (t19 holds the final blur)
//
// Glass (Bedrock Forge client UI + LyricsFlow panel, 2026-10-31): BF_GLYPH_GLASS surfaces show the frame behind them
// frosted (blur mix) and bent like a lens near the edge (refraction), with a bright rim and a coloured inner edge glow.
// The frame is copied once before the glyphs draw, so glass sees the world and the HUD but not other glyphs.
cbuffer Params : register(b0) {
    float4x4 viewProj;
    float4   originTime;
    float4   misc;
};

struct Glyph {
    float3 pos;   float pad0;
    float3 right; float pad1;
    float3 up;    float pad2;
    float4 uv;            // u0 v0(top) u1 v1(bottom)
    uint   fill, fill2, outline, glow; // RGBA8, R in the low byte
    float  alpha, outlineW, glowW, glowK;
    uint   atlas, flags;
    float  pxRange, pad3;
};

static const uint F_SCREEN   = 1u;
static const uint F_BITMAP   = 2u;
static const uint F_MSDF     = 4u;
static const uint F_GRADIENT = 16u;
static const uint F_SOLID    = 32u; // rounded rect, no atlas (top bar pill, icons, EQ bars)
static const uint F_VIEW     = 64u; // settings panel: camera-locked perspective, pos already / tan(fovY/2)
static const uint F_IMAGE    = 128u; // colour image: rgba * fill tint
static const uint F_GLASS    = 256u;  // with SOLID / PETAL: glass (see BfLyrics.h for the field meanings)
static const uint F_PETAL    = 512u;  // ring sector inside the quad (the client wheel's buttons)
static const uint F_BACKDROP = 1024u; // full-screen frosted copy of the frame with a clear circle in the middle
static const uint F_BLUR     = 2048u; // [LyricsFlow player] with IMAGE: soft-blurred image, radius glowW (fraction), faded edges
static const uint kAtlases   = 16u;

StructuredBuffer<Glyph> gGlyphs  : register(t0);
Texture2D               gAtlas[16] : register(t1);
SamplerState            sLin     : register(s0);
SamplerState            sPoint   : register(s1);
Texture2D               gScene   : register(t18);
Texture2D               gBlur    : register(t19);
Texture2D               gBlurB   : register(t20);

// Atlas access. SM6 (D3D12) indexes the texture array directly; SM5 (D3D11, compiled with fxc /D RF_SM5) cannot index
// resources at run time, so it switches over the 16 slots. The atlas index is per glyph (nointerpolation), so every
// pixel of a quad takes the same case. Gradients are taken before the switch (SampleGrad) so filtering stays correct.
#ifdef RF_SM5
#define RF_ATLAS_CASES(X) X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11) X(12) X(13) X(14) X(15)
float4 atlasSample(uint ai, SamplerState s, float2 uv) {
    float2 dx = ddx(uv), dy = ddy(uv);
    #define RF_CASE(n) case n: return gAtlas[n].SampleGrad(s, uv, dx, dy);
    [forcecase] switch (ai) { RF_ATLAS_CASES(RF_CASE) default: return 0; }
    #undef RF_CASE
}
float4 atlasSampleLevel(uint ai, SamplerState s, float2 uv, float lod) {
    #define RF_CASE(n) case n: return gAtlas[n].SampleLevel(s, uv, lod);
    [forcecase] switch (ai) { RF_ATLAS_CASES(RF_CASE) default: return 0; }
    #undef RF_CASE
}
float2 atlasSize(uint ai) {
    float w = 1, h = 1;
    #define RF_CASE(n) case n: gAtlas[n].GetDimensions(w, h); break;
    [forcecase] switch (ai) { RF_ATLAS_CASES(RF_CASE) default: break; }
    #undef RF_CASE
    return float2(w, h);
}
#else
float4 atlasSample(uint ai, SamplerState s, float2 uv) { return gAtlas[NonUniformResourceIndex(ai)].Sample(s, uv); }
float4 atlasSampleLevel(uint ai, SamplerState s, float2 uv, float lod) { return gAtlas[NonUniformResourceIndex(ai)].SampleLevel(s, uv, lod); }
float2 atlasSize(uint ai) { float w, h; gAtlas[NonUniformResourceIndex(ai)].GetDimensions(w, h); return float2(w, h); }
#endif

struct V2P {
    float4 pos  : SV_Position;
    float2 uv   : TEXCOORD0;
    float  grad : TEXCOORD1;  // 0 at the top of the quad, 1 at the bottom
    nointerpolation uint inst : TEXCOORD2;
    float2 lc   : TEXCOORD3;  // [letter fx] position in the glyph's atlas rect: 0..1, y down (outside = the fx margin)
    nointerpolation float ar : TEXCOORD4; // glyph rect width / height
};

// ---------------------------------------------------------------- [letter fx] animated letters (Bedrock Forge 2026-11-03)
// Glyph.pad2 = effect id (low 5 bits; bit 5 = rainbow) + intensity / 2.5 as the fraction, Glyph.pad3 = letter seed +
// speed / 4 as the fraction. The VS grows the quad by a per-effect margin (flames above, drips below, flakes around);
// everything outside the atlas rect samples as empty so neighbouring glyphs in the atlas never show.
static const uint FX_NEON = 1, FX_FIRE = 2, FX_WATER = 3, FX_ICE = 4, FX_ELECTRIC = 5, FX_LAVA = 6, FX_GLITCH = 7, FX_GALAXY = 8;
uint   fxOf(Glyph g)        { return g.pad2 >= 1.0 ? (uint(floor(g.pad2)) & 31u) : 0u; }
bool   fxRainbow(Glyph g)   { return g.pad2 >= 1.0 && (uint(floor(g.pad2)) & 32u) != 0u; }
float  fxIntensity(Glyph g) { return clamp(frac(g.pad2) * 2.5, 0.2, 2.0); }
float  fxSpeed(Glyph g)     { return clamp(frac(g.pad3) * 4.0, 0.1, 3.0); }
float  fxSeed(Glyph g)      { return floor(g.pad3); }
// margins in glyph heights: left / right, top, bottom
float3 fxMargin(uint fx) {
    if (fx == FX_NEON) return float3(0.30, 0.30, 0.30);
    if (fx == FX_FIRE) return float3(0.22, 0.95, 0.10);
    if (fx == FX_WATER) return float3(0.12, 0.10, 0.95);
    if (fx == FX_ICE) return float3(0.45, 0.45, 0.50);
    if (fx == FX_ELECTRIC) return float3(0.32, 0.32, 0.32);
    if (fx == FX_LAVA) return float3(0.18, 0.18, 0.18);
    if (fx == FX_GLITCH) return float3(0.25, 0.08, 0.08);
    if (fx == FX_GALAXY) return float3(0.22, 0.22, 0.22);
    return float3(0, 0, 0);
}


float4 unpack(uint c) { return float4(c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF, c >> 24) / 255.0; }
float  median3(float3 v) { return max(min(v.r, v.g), min(max(v.r, v.g), v.b)); }

V2P VSGlyph(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
    static const float2 corner[6] = {float2(-1, 1), float2(1, 1), float2(-1, -1), float2(-1, -1), float2(1, 1), float2(1, -1)};
    Glyph  g = gGlyphs[iid];
    float2 c = corner[vid];
    // [letter fx] local rect coords (0..1, y down), pushed out by the effect's margin
    float  ar  = length(g.right) / max(length(g.up), 1e-6);
    float3 m   = fxMargin(fxOf(g));
    float2 lc  = float2(c.x < 0 ? -m.x / max(ar, 1e-3) : 1.0 + m.x / max(ar, 1e-3), c.y > 0 ? -m.y : 1.0 + m.z);
    float3 p   = g.pos + g.right * (lc.x * 2.0 - 1.0) + g.up * (1.0 - lc.y * 2.0);
    V2P    o;
    if (g.flags & F_VIEW) // panel: exact perspective (BF ray-picks the mouse with the same mapping)
        o.pos = float4(p.x / max(misc.x, 1e-3), p.y, 0.0, max(p.z, 1e-3));
    else if (g.flags & F_SCREEN) // x 0..aspect, y 0..1 downwards
        o.pos = float4(p.x / max(misc.x, 1e-3) * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
    else
        o.pos = mul(viewProj, float4(p + originTime.xyz, 1.0));
    o.uv   = lerp(g.uv.xy, g.uv.zw, lc);
    o.grad = lc.y;
    o.inst = iid;
    o.lc   = lc;
    o.ar   = ar;
    return o;
}


// ---------------------------------------------------------------- glass
float2 screenPx() { return float2(max(misc.x, 1e-3) * max(misc.w, 1.0), max(misc.w, 1.0)); }
float3 sceneAt(float2 px, float blur) {
    float2 uv = px / screenPx();
    return lerp(gScene.SampleLevel(sLin, uv, 0).rgb, gBlur.SampleLevel(sLin, uv, 0).rgb, blur);
}
float2 sdfNormal(float d) { // screen-space gradient of the signed distance (outward)
    float2 g = float2(ddx(d), ddy(d));
    float  l = length(g);
    return l > 1e-4 ? g / l : float2(0, -1);
}
// d = signed distance in screen px (+ outside), px = this pixel. Fields: fill = tint (a = amount), glowW = bevel px,
// glowK = refraction px, pxRange = blur mix 0..1, outline = rim light (a = strength), glow = inner edge glow (a = strength)
float4 glassShade(Glyph g, float d, float2 px, float4 tint, float k) {
    float2 n     = sdfNormal(d);
    float  bevel = max(g.glowW, 1.0);
    float  x     = saturate(-d / bevel);
    float  edge  = pow(1.0 - x, 2.2);
    float2 off   = -n * g.glowK * edge;               // lens: the rim pulls the world inward (magnifies + bends)
    float  blur  = saturate(g.pxRange);
    float3 c;
    c.r = sceneAt(px + off, blur).r;
    c.g = sceneAt(px + off * 1.07, blur).g;
    c.b = sceneAt(px + off * 1.14, blur).b;          // tiny chromatic split like real glass
    c   = lerp(c, tint.rgb, saturate(tint.a));
    float4 rimC  = unpack(g.outline);
    float  rim   = 1.0 - smoothstep(0.0, 1.6, abs(d + 0.9));
    float  light = 0.45 + 0.55 * max(dot(n, normalize(float2(-0.55, -0.85))), 0.0);
    c += rimC.rgb * rimC.a * (rim * light * 0.75 + edge * 0.10);
    float4 inC = unpack(g.glow);
    c += inC.rgb * inC.a * pow(saturate(1.0 + d / bevel), 3.0);
    float a = saturate(0.5 - d) * k;
    return float4(c * a, a);
}

// ---------------------------------------------------------------- [letter fx] helpers
float  hash11(float n) { return frac(sin(n * 12.9898) * 43758.5453); }
float  hash21(float2 p) { return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
float2 hash22(float2 p) { return frac(sin(float2(dot(p, float2(127.1, 311.7)), dot(p, float2(269.5, 183.3)))) * 43758.5453); }
float  vnoise(float2 p) {
    float2 i = floor(p), f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(hash21(i), hash21(i + float2(1, 0)), u.x), lerp(hash21(i + float2(0, 1)), hash21(i + float2(1, 1)), u.x), u.y);
}
float fbm(float2 p) { return 0.5 * vnoise(p) + 0.3 * vnoise(p * 2.03 + 17.1) + 0.2 * vnoise(p * 4.01 + 41.7); }
// distance to the nearest cell edge (cracks) and the cell's id
float2 voronoiEdge(float2 p) {
    float2 ip = floor(p), fp = frac(p);
    float  d1 = 8.0, d2 = 8.0, id = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x) {
            float2 o = float2(x, y);
            float2 r = o + hash22(ip + o) - fp;
            float  d = dot(r, r);
            if (d < d1) { d2 = d1; d1 = d; id = hash21(ip + o); }
            else if (d < d2) d2 = d;
        }
    return float2(sqrt(d2) - sqrt(d1), id);
}
float3 hsv2rgb(float h, float s, float v) {
    float3 k = saturate(abs(frac(h + float3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0);
    return v * lerp(float3(1, 1, 1), k, s);
}
float3 fireRamp(float h) { return float3(saturate(h * 2.2), saturate(h * 2.2 - 0.75) * 0.88, saturate(h * 3.2 - 2.3) * 0.85); }

float glyphV(Glyph g, uint ai, float2 lc) { // SDF value at a local rect point (0 outside the rect)
    if (lc.x < 0.0 || lc.y < 0.0 || lc.x > 1.0 || lc.y > 1.0) return 0.0;
    float3 s = atlasSampleLevel(ai, sLin, lerp(g.uv.xy, g.uv.zw, lc), 0).rgb;
    return (g.flags & F_MSDF) ? median3(s) : s.r;
}
float glyphCov(Glyph g, uint ai, float2 lc) { return smoothstep(0.38, 0.62, glyphV(g, ai, lc)); }
// soft blurred coverage around q (glyph-height units): a ring of 8 taps at radius r
float halo(Glyph g, uint ai, float2 lc, float ar, float r) {
    float acc = 0;
    [unroll] for (int k = 0; k < 8; ++k) {
        float an = k * 0.785398 + 0.39;
        acc += glyphCov(g, ai, lc + float2(cos(an) * r / max(ar, 1e-3), sin(an) * r));
    }
    return acc / 8.0;
}

float smin(float a, float b, float k) { float h = saturate(0.5 + 0.5 * (b - a) / k); return lerp(b, a, h) - k * h * (1.0 - h); }
float sdSeg(float2 p, float2 a, float2 b) { float2 pa = p - a, ba = b - a; float h = saturate(dot(pa, ba) / max(dot(ba, ba), 1e-6)); return length(pa - ba * h); }
// sd = signed distance in screen px (+ inside), F = fill coverage, Hpx = glyph height in px, base = the glyph colour
float4 letterFx(Glyph g, uint ai, V2P i, float sd, float F, float4 base) {
    uint   fx   = fxOf(g);
    float  I    = fxIntensity(g);
    float  sdd  = fxSeed(g);
    float  t    = originTime.w * fxSpeed(g) + sdd * 1.37;
    float  ar   = max(i.ar, 1e-3);
    float2 lc   = i.lc;
    float2 q    = float2(lc.x * ar, lc.y);                    // isotropic, glyph heights
    float  Hpx  = 1.0 / max(fwidth(lc.y), 1e-6);
    float  aa   = 1.5 / Hpx;                                  // ~1.5 px in q units
    float  tw   = max(1.2, 0.035 * Hpx);                      // tube / rim half width, px
    float  edge = saturate(1.0 - abs(sd) / tw);
    float2 cq   = float2(0.5 * ar, 0.5);
    float  ang  = atan2(q.y - cq.y, q.x - cq.x);
    float3 C    = base.rgb;
    float3 W    = float3(1, 1, 1);

    if (fx == FX_NEON) { // glowing tube along the outline, pulsing, a shine running around each letter
        if (fxRainbow(g)) C = hsv2rgb(frac(ang / 6.2831853 + t * 0.35 + sdd * 0.137), 0.9, 1.0);
        float pulse = 0.72 + 0.28 * sin(t * 3.1 + sdd * 0.7);
        float sh    = pow(saturate(cos(ang - t * 2.4 - sdd)), 14.0);
        float glow  = (halo(g, ai, lc, ar, 0.07) * 0.6 + halo(g, ai, lc, ar, 0.17) * 0.4) * (1.0 - F) * I;
        float3 rgb  = C * edge * 1.7 * pulse + W * edge * (0.25 + 0.75 * sh) * 0.6 + C * glow * 1.1 * pulse + C * 0.16 * F;
        float  a    = saturate(edge * 0.9 + F * 0.3 + glow * 0.3);
        return float4(rgb, a);
    }
    if (fx == FX_FIRE) { // flames rise off the top of the letter, embers inside, sparks floating up
        float Hf = 0.8 * I, flame = 0;
        float rH = g.pxRange / max(abs(g.uv.w - g.uv.y) * atlasSize(ai).y, 1.0); // one SDF unit in glyph heights
        float wx = (vnoise(q * float2(5, 2.2) + float2(0, t * 3.0)) - 0.5) * 0.2;
        [unroll] for (int j = 0; j < 12; ++j) { // look down for the letter; the SDF gives the exact gap between steps
            float d  = j / 11.0 * Hf;
            float vj = glyphV(g, ai, lc + float2(wx * (d / Hf + 0.25) / ar, d));
            if (vj > 0.02) flame = max(flame, 1.0 - (d + max(0.0, 0.5 - vj) * rH) / Hf);
        }
        float tongue = fbm(float2(q.x * 7.0, q.y * 2.0 - t * 3.0) + sdd); // tongues: only the hot noise survives higher up
        float heat = saturate(flame * 1.45 - (1.0 - tongue) * 1.15) * (1.0 - F);
        float ember = 0.6 + 0.4 * fbm(q * 7.0 + float2(0, t * 1.6) + sdd);
        float3 body = lerp(fireRamp(0.45 + 0.3 * ember), C, 0.2);
        float3 rgb  = body * F + fireRamp(heat) * heat * 1.35 + float3(1, 0.7, 0.3) * edge * 0.35;
        float  a    = saturate(F + heat * 0.9);
        [unroll] for (int k = 0; k < 4; ++k) { // sparks
            float  ph  = frac(t * 0.55 + hash11(k * 3.1 + sdd));
            float2 pos = float2(hash11(k * 7.3 + sdd * 1.1) * ar + sin(ph * 9.0 + k) * 0.05, 0.25 - ph * 1.1);
            float  sp  = smoothstep(0.035 * I, 0.0, length(q - pos)) * sin(ph * 3.14159);
            rgb += float3(1.0, 0.6, 0.15) * sp * 1.4;
            a = saturate(a + sp * 0.8);
        }
        return float4(rgb, a);
    }
  if (fx == FX_WATER) {
    float rH = g.pxRange / max(abs(g.uv.w - g.uv.y) * atlasSize(ai).y, 1.0);
    float vHere = glyphV(g, ai, lc);
    float sheen = saturate((vHere - glyphV(g, ai, lc + float2(0.0, -0.045))) * 7.0);
    float gloss = smoothstep(0.65, 0.0, lc.y) * 0.18;
    float3 rgb = (C * 0.82 + W * sheen * 0.4 + W * gloss) * F;
    float a = F;
    float2 bg = q * 8.0; float2 bcl = floor(bg); float bh = hash21(bcl + sdd);
    if (bh > 0.55 && F > 0.5) {
      float2 bp = bcl + 0.25 + 0.5 * hash22(bcl + sdd * 1.3);
      float br = 0.13 + 0.12 * hash11(bh * 9.0);
      float2 rel = (bg - bp) / br; float rr = length(rel);
      float inB = smoothstep(1.0, 0.9, rr);
      float hlt = smoothstep(0.32, 0.08, length(rel - float2(-0.35, -0.4)));
      rgb = lerp(rgb, C * 0.55 * F, pow(rr, 3.0) * 0.55 * inB) + W * hlt * 0.9 * inB * F;
    }
    float dL = (0.5 - vHere) * rH;
    for (int k = 0; k < 2; ++k) {
      float fk = float(k);
      float cx = (0.18 + 0.64 * hash11(sdd * 3.1 + fk * 5.7)) * ar;
      if (abs(q.x - cx) > 0.16) continue;
      float yb = -1.0; float vb = 0.5;
      for (int s = 0; s < 16; ++s) { float y = 1.0 - float(s) / 15.0; float vv = glyphV(g, ai, float2(cx / ar, y)); if (vv > 0.5) { yb = y; vb = vv; break; } }
      if (yb < 0.0) continue;
      yb += (vb - 0.5) * rH;
      float cyc = t * 0.14 * (0.7 + 0.6 * hash11(fk + sdd)) + hash11(fk * 2.3 + sdd * 1.7);
      if (hash11(floor(cyc) * 3.7 + fk + sdd) < 0.35) continue; // some cycles this spot stays dry
      float ph = frac(cyc);
      float sI = sqrt(I);
      float grow = smoothstep(0.0, 0.55, ph);
      float r = lerp(0.025, 0.075, grow) * sI;
      float stretch = smoothstep(0.45, 0.72, ph) * 0.15 * sI;
      float2 c = float2(cx, yb + r * 0.85 + stretch);
      float dS; float rr = r;
      if (ph < 0.72) {
        float neckR = lerp(r * 0.8, 0.011, smoothstep(0.45, 0.72, ph));
        dS = smin(length((q - c) * float2(1.0, 0.92)) - r, sdSeg(q, float2(cx, yb - 0.02), c) - neckR, 0.03);
      } else {
        float f = (ph - 0.72) / 0.28;
        float rb = 0.028 * (1.0 - f) * sI;
        float dLeft = length(q - float2(cx, yb + rb * 0.5)) - rb;
        c = float2(cx, yb + 0.065 * sI * 0.85 + 0.15 * sI + f * f * 1.15);
        rr = 0.06 * sI;
        float2 rq = q - c;
        float tear = rq.y < 0.0 ? length(rq * float2(1.0, 0.55 + 0.2 * (1.0 - f))) - rr : length(rq) - rr;
        dS = min(dLeft, tear);
      }
      float dAll = smin(dL, dS, 0.035);
      float cov = smoothstep(aa, -aa, dAll) * (1.0 - F) * saturate((1.9 - q.y) / 0.1);
      if (cov <= 0.0) continue;
      float depth = saturate(-dS / max(rr, 1e-3));          // 0 at the surface, 1 in the middle
      float2 n = (q - c) / max(rr, 1e-3);
      float ringA = smoothstep(0.28, 0.0, depth);                     // thin dark edge (total internal reflection)
      float cres = smoothstep(0.55, 0.1, depth) * smoothstep(0.05, 0.75, n.y); // light focused through the drop: bright lower rim
      float hl = smoothstep(0.24, 0.06, length(n - float2(-0.36, -0.38)));
      float hl2 = smoothstep(0.12, 0.0, length(n - float2(0.3, -0.5))) * 0.5;
      float3 body = lerp(C, float3(0.7, 0.85, 1.0), 0.4);
      float3 dropRgb = float3(0.02, 0.03, 0.05) * ringA * 0.5 + body * 0.14 + (body * 0.5 + W * 0.45) * cres * 0.75 + W * (hl + hl2);
      float dropA = saturate(0.16 + ringA * 0.5 + cres * 0.35 + hl + hl2);
      rgb = rgb * (1.0 - cov * dropA) + dropRgb * cov;
      a = a * (1.0 - cov * dropA) + dropA * cov;
    }
    return float4(rgb, a);
  }
    if (fx == FX_ICE) { // frosted, cracked ice with glints; snowflakes drift off the letters
        float3 icy  = lerp(C, float3(0.78, 0.93, 1.0), 0.6);
        float2 vo   = voronoiEdge(q * 6.0 + sdd);
        float  crack = 1.0 - smoothstep(0.0, 0.06, vo.x);
        float  frost = fbm(q * 14.0 + sdd);
        float2 cell  = floor(q * 16.0);
        float  hh    = hash21(cell + sdd);
        float  glint = step(0.9, hh) * pow(saturate(sin(t * 3.0 + hh * 60.0)), 24.0) * smoothstep(0.3, 0.0, length(frac(q * 16.0) - 0.5));
        float  glow  = halo(g, ai, lc, ar, 0.08) * (1.0 - F) * 0.45 * I;
        float3 rgb   = (icy * (0.72 + 0.28 * frost) * (1.0 - crack * 0.35) + W * crack * 0.25 + W * glint) * F
                     + W * edge * 0.6 + float3(0.55, 0.8, 1.0) * glow;
        float  a     = saturate(F + edge * 0.5 + glow * 0.4);
        [unroll] for (int k = 0; k < 7; ++k) {
            float  h1 = hash11(k * 1.7 + sdd), h2 = hash11(k * 3.3 + sdd * 1.3), h3 = hash11(k * 5.1 + sdd * 0.7);
            float  ph = frac(t * 0.2 * (0.7 + 0.6 * h1) + h2);
            float2 st = float2(h3 * ar, 0.15 + 0.7 * hash11(k + 9.1 + sdd));
            float2 pos = st + float2(sin(ph * 6.0 + k) * 0.07 + (h1 - 0.5) * 0.5 * ph, ph * 0.55);
            float  sz  = (0.035 + 0.03 * h2) * (0.6 + 0.4 * I);
            float2 rel = q - pos;
            float  r   = length(rel), an = atan2(rel.y, rel.x) + ph * 3.0 + k;
            float  arms = pow(abs(cos(3.0 * an)), 6.0);
            float  d   = r - sz * (0.3 + 0.7 * arms);
            float  fl  = smoothstep(aa, -aa, d) * sin(ph * 3.14159);
            rgb += W * fl * (1.0 - F * 0.5);
            a = saturate(a + fl * 0.85);
        }
        return float4(rgb, a);
    }
    if (fx == FX_ELECTRIC) { // dark charged core, crackling edge, lightning jumping across
        float3 ec    = lerp(C, float3(0.55, 0.82, 1.0), 0.55);
        float  crack = 0.55 + 0.9 * vnoise(q * 18.0 + t * 14.0);
        float  hl    = halo(g, ai, lc, ar, 0.1);
        float3 rgb   = lerp(C * 0.3, float3(0.04, 0.08, 0.25), 0.5) * F * (0.8 + 0.4 * vnoise(q * 9.0 - t * 6.0)) + ec * edge * 1.8 * crack;
        float  a     = saturate(F + edge * 0.8);
        [unroll] for (int k = 0; k < 2; ++k) {
            float tb  = floor(t * 7.0 + k * 0.5);
            float on  = step(0.35, hash11(tb * 1.3 + k + sdd));
            float y   = 0.15 + 0.7 * hash11(tb + k * 13.0 + sdd) + (fbm(float2(q.x * 5.0, tb + k * 7.0)) - 0.5) * 0.55;
            float bolt = exp(-abs(q.y - y) * Hpx / 1.6) * on * saturate(hl * 3.0);
            rgb += lerp(ec, W, 0.5) * bolt * 1.6;
            a = saturate(a + bolt * 0.8);
        }
        float fl = 0.6 + 0.4 * step(0.5, hash11(floor(t * 20.0) + sdd));
        rgb += ec * hl * (1.0 - F) * 0.55 * fl * I;
        a = saturate(a + hl * (1.0 - F) * 0.25 * I);
        return float4(rgb, a);
    }
    if (fx == FX_LAVA) { // cooled rock with glowing cracks that slowly flow and pulse
        float2 vo    = voronoiEdge(q * 5.0 + float2(0, t * 0.15) + sdd);
        float  crack = 1.0 - smoothstep(0.03, 0.22, vo.x);
        float  pulse = 0.7 + 0.3 * sin(t * 2.0 + vo.y * 6.2831);
        float3 rock  = float3(0.22, 0.10, 0.07) * (0.7 + 0.6 * fbm(q * 9.0 + sdd)) + float3(0.35, 0.08, 0.0) * smoothstep(0.35, 0.0, vo.x);
        float3 lav   = lerp(fireRamp(0.72 + 0.28 * pulse), C, 0.2);
        float  glow  = halo(g, ai, lc, ar, 0.06) * (1.0 - F) * I;
        float3 rgb   = lerp(rock, lav * 1.3, crack * pulse) * F + lav * edge * 0.7 + float3(1.0, 0.35, 0.05) * glow * 0.7;
        float  a     = saturate(F + glow * 0.35);
        return float4(rgb, a);
    }
    if (fx == FX_GLITCH) { // rows jump sideways, colour channels split, scanlines
        float tt   = floor(t * 9.0);
        float band = floor(lc.y * 10.0);
        float jump = (hash21(float2(band, tt) + sdd) - 0.5) * 0.28 * step(0.72, hash11(tt + sdd * 1.3)) * I;
        float sp   = (0.02 + 0.04 * step(0.78, hash11(tt * 2.1 + sdd))) * I;
        float r    = glyphCov(g, ai, lc + float2((jump + sp) / ar, 0));
        float gg   = glyphCov(g, ai, lc + float2(jump / ar, 0));
        float b    = glyphCov(g, ai, lc + float2((jump - sp) / ar, 0));
        float3 tint = lerp(C, W, 0.35);
        float3 rgb = float3(r * tint.r + r * 0.35, gg * tint.g, b * tint.b + b * 0.35);
        rgb *= 0.82 + 0.18 * sin(i.pos.y * 3.14159);
        float flash = step(0.95, hash21(float2(band, tt * 1.7) + sdd));
        rgb = lerp(rgb, W * max(r, max(gg, b)), flash);
        float a = max(r, max(gg, b));
        return float4(rgb, a);
    }
  if (fx == FX_GALAXY) {
    float neb = fbm(q * 2.2 + float2(t * 0.03, t * 0.02) + sdd);
    float neb2 = fbm(q * 4.5 - float2(t * 0.02, 0.0) + sdd * 1.7);
    float3 col = float3(0.006, 0.006, 0.02) + float3(0.18, 0.06, 0.32) * pow(neb, 3.0) * 1.6 + float3(0.04, 0.08, 0.22) * pow(neb2, 4.0) * 2.0;
    col += C * pow(neb, 3.0) * 0.12;
    float2 g1 = q * 38.0 + float2(t * 0.15, 0.0); float h1 = hash21(floor(g1) + sdd); float2 j1 = hash22(floor(g1) + 7.0);
    float s1 = step(0.78, h1) * smoothstep(0.12, 0.0, length(frac(g1) - j1 * 0.6 - 0.2)) * (0.35 + 0.65 * frac(h1 * 13.0)) * (0.75 + 0.25 * sin(t * 4.0 + h1 * 80.0));
    float2 g2 = q * 7.0 + float2(t * 0.06, 0.0); float2 c2 = floor(g2); float h2 = hash21(c2 + sdd * 2.1);
    float2 p2 = frac(g2) - (0.25 + 0.5 * hash22(c2 + 3.7));
    float twk = 0.6 + 0.4 * sin(t * 2.5 + h2 * 40.0);
    float core = exp(-dot(p2, p2) * 600.0);
    float spikes = (exp(-abs(p2.x) * 80.0) * exp(-abs(p2.y) * 6.0) + exp(-abs(p2.y) * 80.0) * exp(-abs(p2.x) * 6.0)) * 0.75;
    float s2 = step(0.6, h2) * (core * 1.6 + spikes) * twk * (0.6 + 0.4 * frac(h2 * 31.0));
    float3 starC = lerp(float3(0.75, 0.85, 1.0), float3(1.0, 0.9, 0.75), frac(h2 * 7.0));
    float3 rimC = lerp(float3(0.55, 0.5, 1.0), float3(0.82, 0.86, 1.0), 0.5 + 0.5 * sin(t * 0.7 + ang));
    float hl = halo(g, ai, lc, ar, 0.08) * (1.0 - F) * I;
    float3 rgb = (col + W * s1 + starC * s2) * F + rimC * edge * 0.45 + float3(0.22, 0.1, 0.45) * hl * 0.35;
    return float4(rgb, saturate(F + edge * 0.5 + hl * 0.2));
  }
    return float4(base.rgb * F * base.a, F * base.a);
}

float angDiff(float a, float b) { float d = a - b; return d - 6.2831853 * floor((d + 3.14159265) / 6.2831853); }

float4 PSGlyph(V2P i) : SV_Target {
    Glyph  g     = gGlyphs[i.inst];
    float4 fillC = unpack(g.fill);
    if (g.flags & F_GRADIENT) fillC = lerp(fillC, unpack(g.fill2), saturate(i.grad));
    float  k     = g.alpha * misc.y;
    if (g.pad1 > g.pad0) { // vertical clip (screen y 0..1, BF menu scroll areas): 10 px fade centred on each band edge
        // (no discard: fwidth() below needs every pixel of the quad alive)
        float sy   = i.pos.y / max(misc.w, 1.0);
        float fade = 10.0 / max(misc.w, 1.0);
        k *= saturate((sy - g.pad0) / fade + 0.5) * saturate((g.pad1 - sy) / fade + 0.5);
    }

    if (g.flags & F_BACKDROP) { // fill = darkening colour (a = amount), pxRange = blur, glowW = clear radius px, glowK = feather px
        float2 halfPx = 0.5 / max(fwidth(i.uv), 1e-6);
        float  r      = length((i.uv - 0.5) * 2.0 * halfPx);
        float  mask   = g.glowK > 0.0 ? smoothstep(g.glowW, g.glowW + g.glowK, r) : 1.0;
        float3 c      = sceneAt(i.pos.xy, saturate(g.pxRange) * mask);
        c             = lerp(c, fillC.rgb, saturate(fillC.a) * (0.35 + 0.65 * mask));
        return float4(c * k, k);
    }
    if (g.flags & F_PETAL) { // ring sector: outlineW = inner / outer radius, fill2 = centre angle | half span << 16 (1/100 deg)
        float2 halfPx = 0.5 / max(fwidth(i.uv), 1e-6);
        float2 p      = (i.uv - 0.5) * 2.0 * halfPx;       // px from the quad centre, y down
        float  R2     = min(halfPx.x, halfPx.y) - 1.0, R1 = R2 * saturate(g.outlineW);
        float  ca     = float(g.fill2 & 0xFFFFu) * (3.14159265 / 18000.0);
        float  hs     = float(g.fill2 >> 16) * (3.14159265 / 18000.0);
        float  r      = length(p), an = atan2(p.y, p.x);
        float  rm = (R1 + R2) * 0.5, hw = (R2 - R1) * 0.5, rad = hw * 0.42;
        float2 q  = float2(abs(r - rm) - (hw - rad), (abs(angDiff(an, ca)) - hs) * r + rad);
        float  d  = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - rad;
        if (g.flags & F_GLASS) return glassShade(g, d, i.pos.xy, fillC, k);
        float a = saturate(0.5 - d) * fillC.a;
        return float4(fillC.rgb * a, a) * k;
    }
    if (g.flags & F_SOLID) { // outlineW = corner radius (fraction of the smaller half-extent), glowW = edge softness px
        float2 halfPx = 0.5 / max(fwidth(i.uv), 1e-6);
        float  r      = min(halfPx.x, halfPx.y) * saturate(g.outlineW);
        float2 q      = abs(i.uv - 0.5) * 2.0 * halfPx - (halfPx - r);
        float  d      = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
        if (g.flags & F_GLASS) return glassShade(g, d, i.pos.xy, fillC, k);
        // soft edge (glowW > 1.5 px: glows / shadows): fade to 0 exactly at the quad's edge, full at glowW px inside
        // (the old 0.5 - d / glowW left alpha 0.5 at the edge, a hard rectangle around every glow)
        float  a      = (g.glowW > 1.5 ? saturate(-d / g.glowW) : saturate(0.5 - d / max(1.0, g.glowW))) * fillC.a;
        return float4(fillC.rgb * a, a) * k;
    }

    const uint ai = min(g.atlas, kAtlases - 1);
    if (g.flags & F_IMAGE) { // cover art / icons / thumbnails: straight-alpha rgba, tinted
        float2    span  = g.uv.zw - g.uv.xy;
        float2    local = (i.uv - g.uv.xy) / (abs(span.x) > 1e-6 && abs(span.y) > 1e-6 ? span : float2(1, 1));
        float4    t;
        if (g.flags & F_BLUR) { // [LyricsFlow player] 32 taps on a golden-angle spiral, weighted to the middle; edges fade
            float r    = g.glowW;
            float wsum = 0;
            t          = 0;
            [unroll] for (int s = 0; s < 32; ++s) {
                float  rr = sqrt((s + 0.5) / 32.0);
                float  an = s * 2.39996323;
                float  w  = 1.0 - 0.6 * rr;
                t += atlasSampleLevel(ai, sLin, i.uv + float2(cos(an), sin(an)) * rr * r * span, 0) * w;
                wsum += w;
            }
            t /= wsum;
            float e = min(min(local.x, 1.0 - local.x), min(local.y, 1.0 - local.y));
            t.a *= pow(saturate(e / max(r * 1.6, 1e-3)), 1.5);
        } else {
            t = atlasSample(ai, sLin, i.uv);
        }
        t *= fillC;
        if (g.outlineW > 0) { // [LyricsFlow player] rounded corners (outlineW = radius / smaller half-extent)
            float2 halfPx = 0.5 / max(fwidth(local), 1e-6);
            float  r      = min(halfPx.x, halfPx.y) * saturate(g.outlineW);
            float2 q      = abs(local - 0.5) * 2.0 * halfPx - (halfPx - r);
            float  d      = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
            t.a *= saturate(0.5 - d);
        }
        return float4(t.rgb * t.a, t.a) * k;
    }

    if (g.flags & F_BITMAP) { // pixel font: hard texels, no SDF maths
        float a = atlasSample(ai, sPoint, i.uv).r;
        float4 o = float4(fillC.rgb, 1.0) * (fillC.a * a);
        if (misc.z > 0.5) o = max(o, float4(0.2, 0.0, 0.0, 0.2));
        return o * k;
    }

    float3    s   = atlasSample(ai, sLin, i.uv).rgb;
    float     v   = (g.flags & F_MSDF) ? median3(s) : s.r; // 0.5 = outline of the glyph
    if (i.lc.x < 0.0 || i.lc.y < 0.0 || i.lc.x > 1.0 || i.lc.y > 1.0) v = 0.0; // [letter fx] margin: never the neighbour glyph
    // screen pixels per SDF unit (msdf-atlas-gen's screenPxRange)
    float2 unitRange  = g.pxRange / atlasSize(ai);
    float2 screenTex  = 1.0 / max(fwidth(i.uv), 1e-6);
    float  spr        = max(0.5 * dot(unitRange, screenTex), 1.0);
    float  sd         = (v - 0.5) * spr;                         // screen px, + inside
    float  fillA      = saturate(sd + 0.5);
    float  outA       = g.outlineW > 0 ? saturate(sd + g.outlineW * spr + 0.5) : fillA;
    float4 outC       = unpack(g.outline);
    if (fxOf(g) != 0u) { // [letter fx]
        float4 fo = letterFx(g, ai, i, sd, fillA, fillC);
        if (misc.z > 0.5) fo = max(fo, float4(0.2, 0.0, 0.0, 0.2));
        return fo * (k * fillC.a);
    }
    float4 glowC      = unpack(g.glow);
    float  glowA      = g.glowK > 0 ? g.glowK * glowC.a * pow(saturate(1.0 + (v - 0.5) / max(g.glowW, 1e-3)), 2.0) : 0.0;

    // premultiplied: fill over outline over glow
    float3 rgb = fillC.rgb * (fillA * fillC.a) + outC.rgb * ((outA - fillA) * outC.a);
    float  a   = fillA * fillC.a + (outA - fillA) * outC.a;
    rgb += glowC.rgb * glowA * (1.0 - a);
    a += glowA * (1.0 - a);
    float4 o = float4(rgb, a);
    if (misc.z > 0.5) o = max(o, float4(0.2, 0.0, 0.0, 0.2));
    return o * k;
}

// ---- S2 "under the HUD" (staged, unverified): the game's UI was redirected into our own target (UiCapture);
// after the glyphs are drawn on the back buffer, the UI goes back on top with this full-screen pass.
Texture2D gUiTarget : register(t17);
struct VFull { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VFull VSComposite(uint id : SV_VertexID) {
    VFull  o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.uv      = uv;
    o.pos     = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float4 PSComposite(VFull i) : SV_Target { return gUiTarget.Sample(sPoint, i.uv); } // UI target is premultiplied-ish (see UiCapture alpha fix)

// ---- glass blur: frame copy (t18) -> quarter res (t19), then separable gaussian ping-pong t19 -> t20 -> t19
float4 PSDown(VFull i) : SV_Target {
    float w, h;
    gScene.GetDimensions(w, h);
    float2 t = 1.0 / float2(w, h);
    float3 c = gScene.SampleLevel(sLin, i.uv + t * float2(-1, -1), 0).rgb + gScene.SampleLevel(sLin, i.uv + t * float2(1, -1), 0).rgb
             + gScene.SampleLevel(sLin, i.uv + t * float2(-1, 1), 0).rgb + gScene.SampleLevel(sLin, i.uv + t * float2(1, 1), 0).rgb;
    return float4(c * 0.25, 1);
}
float3 gauss9(Texture2D src, float2 uv, float2 dir) {
    float w, h;
    src.GetDimensions(w, h);
    float2 t = dir * 2.0 / float2(w, h); // spread 2 texels per step: wide frost at quarter res
    float3 c = src.SampleLevel(sLin, uv, 0).rgb * 0.2270270270;
    c += (src.SampleLevel(sLin, uv + t * 1.3846153846, 0).rgb + src.SampleLevel(sLin, uv - t * 1.3846153846, 0).rgb) * 0.3162162162;
    c += (src.SampleLevel(sLin, uv + t * 3.2307692308, 0).rgb + src.SampleLevel(sLin, uv - t * 3.2307692308, 0).rgb) * 0.0702702703;
    return c;
}
float4 PSBlurH(VFull i) : SV_Target { return float4(gauss9(gBlur, i.uv, float2(1, 0)), 1); }
float4 PSBlurV(VFull i) : SV_Target { return float4(gauss9(gBlurB, i.uv, float2(0, 1)), 1); }
