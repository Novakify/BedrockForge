// Renderer Foundation - Glass Body pixel shader (Purxiy "Glass": You / Other players). Runs instead of the game's
// Actor pixel shader on a PLAYER's own draw (body in third person, arm in first person), replayed after the actor pass
// like Glass Armor (shaders/glass_armor.hlsl), with a copy of the colour + depth targets taken right then.
// The same glass as the approved armor, read from the player's own skin instead of an armor mask:
//   * clear glass with a fixed twist per face (static - nothing swims), small chromatic split
//   * the skin's own colours tint the glass (you stay recognisable: face, hair, clothes), the second layer (hat, jacket,
//     sleeves, trousers) a thicker pane over it
//   * a neon line round every face and every painted shape, in a vivid version of the skin's colour there
//   * Frosted / Glow / Shimmer / Colour from the client's Glass module (shared with the armor), LyricsForge beat on the line
// The skin comes from OUR copy of what Bedrock Forge grabbed (t3 space9: 64x64 RGBA8, rows of 256 bytes), not the game's
// texture - so it works whatever the game's root signature looks like.
#include "glass_body_data.hlsli" // generated: kBodyFaces[72], kBodyFacesSlim[72]

ByteAddressBuffer gScene : register(t0, space9);
ByteAddressBuffer gDepth : register(t1, space9);
ByteAddressBuffer gPrev  : register(t2, space9);
ByteAddressBuffer gSkin  : register(t3, space9);
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

// the Actor vertex shader's output signature (probe M0) - same as the armor's non-enchanted build
struct PSIn {
    float4 pos : SV_Position;
    float4 c1  : COLOR1;
    float4 c0  : COLOR0;
    float4 c2  : COLOR2;
    float4 c3  : COLOR3;
    float2 uv  : TEXCOORD0;
    float3 t3  : TEXCOORD3;
};

float4 skinAt(int2 t) {
    if (t.x < 0 || t.y < 0 || t.x > 63 || t.y > 63) return float4(0, 0, 0, 0);
    uint v = gSkin.Load(uint(t.y) * 256u + uint(t.x) * 4u);
    return float4(v & 255u, (v >> 8) & 255u, (v >> 16) & 255u, v >> 24) / 255.0;
}
bool slimArms() {   // GlassBodyMath.h isSlim: the classic arm's back columns 54-55 are empty on a slim skin
    return skinAt(int2(54, 20)).a + skinAt(int2(55, 24)).a + skinAt(int2(54, 28)).a + skinAt(int2(55, 31)).a < 0.02;
}
int4 faceRect(int i, bool slim) { return slim ? kBodyFacesSlim[i] : kBodyFaces[i]; }
int faceOf(int2 t, bool slim) {
    [loop] for (int i = 0; i < 72; ++i) {
        int4 r = faceRect(i, slim);
        if (t.x >= r.x && t.y >= r.y && t.x < r.x + r.z && t.y < r.y + r.w) return i;
    }
    return -1;
}
bool painted(int2 t) { return skinAt(t).a > 0.04; }

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
float3 prevAt(float2 px) { return prevTexel(int2(px * float2(prevW, prevH) / res)); }
float depthAt(float2 px) {
    int2 p = clamp(int2(px), int2(0, 0), int2(res) - 1);
    return float(gDepth.Load(uint(p.y) * depthPitch + uint(p.x) * 4) & 0xFFFFFFu) / 16777215.0;
}
float gaHash(float2 q) { return frac(sin(dot(q, float2(127.1, 311.7))) * 43758.5453); }
// a vivid version of a colour (brightest channel 1, a little more saturated); near-black / grey -> a cool white
float3 vivid(float3 c) {
    float m = max(max(c.r, c.g), c.b);
    float n = min(min(c.r, c.g), c.b);
    float3 v = c / max(m, 1e-3);
    v = saturate(lerp(dot(v, float3(0.299, 0.587, 0.114)).xxx, v, 1.35));
    float grey = 1.0 - smoothstep(0.06, 0.20, m - n) * smoothstep(0.03, 0.10, m);   // no hue to speak of
    return lerp(v, float3(0.86, 0.93, 1.00), grey);
}

float4 PSGlassBody(PSIn i) : SV_Target {
    float2 t = i.uv * 64.0;   // skin texel space (a 128x128 skin's UVs are normalised: the same 64x64 layout)
    int2   b = int2(floor(t));
    if (pad == 1u) return float4(1.0, 0.0, 1.0, 1.0);   // debug: does the replayed draw land?
    float4 sk = skinAt(b);
    if (sk.a <= 0.04) discard;                           // the skin's own shape (the second layer's holes)
    bool const slim = slimArms();
    int  fi = faceOf(b, slim);
    int4 fr = faceRect(max(fi, 0), slim);
    bool const over = fi >= 0 && ((fi / 6) & 1) == 1;    // the second layer (hat, jacket, sleeves, trousers)
    float2 fs = float2(fr.zw), p = t - float2(fr.xy);

    // distance (texels) to the edge of this face or of the painted shape on it
    float dIn = 1.0;
    [unroll] for (int dy = -1; dy <= 1; ++dy)
    [unroll] for (int dx = -1; dx <= 1; ++dx) {
        if (dx == 0 && dy == 0) continue;
        int2 n = b + int2(dx, dy);
        if (painted(n) && faceOf(n, slim) == fi) continue;
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
    float  halfPx = min(fs.x, fs.y) * 0.5 * pxPerTexel;
    float  bevel = min(16.0 * S, halfPx * 0.85);
    float  e     = pow(1.0 - saturate(-d / max(bevel, 1.0)), 2.2);

    // fixed twist around the face centre (static: nothing moves), gentler than the armor's - a body is bigger glass
    float2 rel = p - fs * 0.5;
    float  rr  = length(rel) / max(min(fs.x, fs.y) * 0.5, 1e-3);
    float  ph  = gaHash(float2(fr.xy) + 0.7);
    float  ang = swirl * 0.7 * smoothstep(1.15, 0.0, rr) * (ph > 0.5 ? 1.0 : -1.0);
    float  cs = cos(ang), sn = sin(ang);
    float2 fo = float2(cs * rel.x - sn * rel.y, sn * rel.x + cs * rel.y) - rel;
    fo += ripple * float2(sin(p.y * 0.9 + ph * 6.0), cos(p.x * 0.9 + ph * 4.0));
    float2 off = abs(dj) > 1e-8 ? float2(ty.y * fo.x - ty.x * fo.y, -tx.y * fo.x + tx.x * fo.y) / dj : float2(0, 0);
    off  = clamp(off, -36.0 * S, 36.0 * S);
    off += -n * 12.0 * lens * S * e;
    // crystal, like the approved armor (Fernando 2026-11-13: crystallised glass, not flat sheets): each face is cut into
    // facets (static Voronoi in texel space); each bends the world its own way and catches the light on its own, the
    // cut lines glint. They fade out where a facet is only a few pixels across (far away: one clean crystal).
    float2 cg = p / 2.4 + float2(float(fi) * 3.7, float(fi) * 1.3);
    float2 ci = floor(cg), cf = frac(cg);
    float  d1 = 9.0, d2 = 9.0;
    float2 cid = ci;
    [unroll] for (int yy = -1; yy <= 1; ++yy)
    [unroll] for (int xx = -1; xx <= 1; ++xx) {
        float2 o  = float2(xx, yy);
        float2 hh = float2(gaHash(ci + o), gaHash(ci + o + 17.3));
        float2 r  = o + 0.15 + 0.7 * hh - cf;
        float  dd = dot(r, r);
        if (dd < d1) { d2 = d1; d1 = dd; cid = ci + o; } else if (dd < d2) d2 = dd;
    }
    float  cgap   = sqrt(d2) - sqrt(d1);
    float  cutLn  = 1.0 - smoothstep(0.0, max(fwidth(cgap) * 1.3, 0.035), cgap);
    float  cellPx = 1.0 / max(max(length(ddx(cg)), length(ddy(cg))), 1e-4);
    float  facetK = smoothstep(5.0, 12.0, cellPx);
    cutLn *= facetK;
    float  fh1 = gaHash(cid + 5.1), fh2 = gaHash(cid + 9.7);
    float2 tilt = float2(fh1, fh2) * 2.0 - 1.0;
    off += tilt * 11.0 * S * (1.0 - 0.5 * e);
    // only bend what is well behind you (the ground at your feet, a wall you lean on stay put)
    float2 px    = i.pos.xy;
    float  self  = 1.0 - i.pos.z;
    float  ratio = min(self / max(1.0 - depthAt(px), 1e-6), self / max(1.0 - depthAt(px + off), 1e-6));
    off *= smoothstep(1.03, 1.18, ratio);
    float frz = saturate(frost);
    off *= lerp(1.0, 0.45, frz);

    float3 c;
    c.r = sceneAt(px + off).r;
    c.g = sceneAt(px + off * 1.04).g;
    c.b = sceneAt(px + off * 1.08).b;

    // the skin in the glass: its colour soaks the glass and a share of it stays its own, so the face, hair and clothes
    // read at a glance; the second layer is a thicker pane
    float3 skc  = sk.rgb;
    float  skL  = dot(skc, float3(0.299, 0.587, 0.114));
    // bright parts (a white stripe, shoes) stay clear glass instead of going milky; dark parts (hair) keep their colour
    float  soak = (over ? 0.55 : 0.45) * lerp(1.0, 0.65, skL), own = (over ? 0.34 : 0.24) * lerp(1.0, 0.45, skL);
    c = lerp(c, c * skc * 1.25 + skc * 0.08, soak);
    c = lerp(c, skc, own);
    float3 tintF = float3(tintPk & 255u, (tintPk >> 8) & 255u, (tintPk >> 16) & 255u) / 255.0;
    if (frz > 0.001) {   // frosted: the world behind blurred a little and milky (the armor's frost, same numbers)
        float  R  = (2.0 + 30.0 * frz) * S;
        float  a0 = gaHash(px * 0.37 + 3.1) * 6.2831853;
        float3 bd = 0;
        [unroll] for (int k = 0; k < 16; ++k) {
            float  an = a0 + float(k) * 2.3999632;
            float  rk = sqrt((float(k) + 0.5) / 16.0) * R;
            bd += texel(int2(px + off + float2(cos(an), sin(an)) * rk));
        }
        bd /= 16.0;
        bd = lerp(bd, dot(bd, float3(0.2126, 0.7152, 0.0722)).xxx, 0.30 * frz);
        float milk = 0.06 + 0.46 * frz * frz;
        bd = bd * (1.0 - milk) + lerp(tintF, tintF * skc, 0.35) * milk * 0.92;
        bd += (gaHash(px * 0.71 + 13.0) - 0.5) * 0.045 * frz;
        bd = lerp(bd, lerp(bd * skc * 1.25 + skc * 0.08, skc, own), soak);   // still your skin
        c = lerp(c, bd, smoothstep(0.0, 0.30, frz));
    }

    // the facets: each its own light, the cut lines glint, the odd facet flares as you turn (calmer when frosted)
    {
        float calm = 1.0 - 0.75 * frz;
        float fl   = dot(tilt, normalize(float2(-0.45, -0.89)));
        float3 hueC = vivid(skc);
        c *= lerp(1.0, lerp(1.0, 0.88 + 0.22 * (0.5 + 0.5 * fl), 0.35 + 0.65 * facetK), calm);
        c += lerp(float3(1, 1, 1), hueC, 0.45) * cutLn * (0.14 + 0.12 * (0.5 + 0.5 * fl)) * calm;
        float spk = pow(saturate(sin(fh1 * 60.0 + (px.x + px.y) / max(res.x, 1.0) * 7.0 + time * 0.5)), 40.0);
        c += lerp(float3(1, 1, 1), hueC, 0.3) * spk * smoothstep(0.30, 0.0, sqrt(d1)) * 0.45 * calm;
    }
    // reflection of the last finished frame (lyrics, sky, effects) at the rims, like the armor
    if (havePrev) {
        float2 rp   = px - off * 1.6 + n * (20.0 * S);
        float3 refl = prevAt(rp);
        float  w    = reflectAmt * (0.08 + 0.45 * e) * smoothstep(0.25, 0.85, dot(refl, float3(0.299, 0.587, 0.114)) + 0.25);
        c = 1.0 - (1.0 - c) * (1.0 - refl * w);
    }
    // LyricsForge: the line breathes with the kick, flares on the drop, impact frames light the glass
    float mus   = active * react;
    float kick  = mus * beat * beatK;
    float flare = drop * react;
    float3 edgeC = vivid(skc) * (1.0 + kick * 0.9 + flare * 1.4);
    // Novak Glass endgame: the line goes neon and the glass glows softly (same curve as the armor)
    float ngK = pad4 > 0.5 && pad4 < 1.5 ? smoothstep(0.30, 0.95, pad3) : 0.0;
    edgeC *= 1.0 + 0.45 * ngK;
    c += lerp(tintF, tintF * skc, 0.25) * glowK * (0.10 + 0.20 * e);
    if (shimmer > 0.5) {
        float ph2 = frac(time / 6.0 + gaHash(float2(fr.xy)) * 0.37 + float(fi) * 0.013);
        float sp2 = (p.x / fs.x) * 0.7 + (p.y / fs.y) * 0.3;
        float k2  = (sp2 - (ph2 * 3.2 - 1.1)) / 0.09;
        c += exp(-k2 * k2) * (0.10 + 0.10 * frz) * lerp(float3(1, 1, 1), tintF, 0.3);
    }
    // the menu rim: white, brighter towards the light (top left)
    float  rim = 1.0 - smoothstep(0.0, 1.6, abs(d + 0.9));
    float  lit = 0.45 + 0.55 * max(dot(n, normalize(float2(-0.45, -0.89))), 0.0);
    c += 0.75 * (rim * lit * 0.70 + e * 0.06);
    c += flashCol * flash * mus * (0.5 + 1.0 * e);
    // the neon line and its light glowing into the glass
    c += edgeC * (0.18 + 0.25 * kick + 0.35 * flare + 0.20 * ngK) * pow(saturate(1.0 + d / max(bevel, 1.0)), 3.0);
    c = lerp(c, c * lerp(float3(1, 1, 1), vivid(skc), 0.3), 0.5 * ngK) + vivid(skc) * 0.06 * ngK;
    float aa = max(fwidth(dIn), 1e-4) * 1.1;
    float lw = (over ? 0.15 : 0.19) + 0.07 * kick + 0.10 * min(flare, 1.0);
    float ln = 1.0 - smoothstep(lw - aa, lw + aa, dIn);
    c = lerp(c, edgeC * 1.2, ln * lerp(0.85, 1.0, ngK));
    return float4(saturate(c), 1.0);
}
