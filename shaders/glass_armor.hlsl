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
    // M3: leather dye + enchant
    int    dyeRow, dyeRowGlint;
    uint   dbgRow;
    float  enchFlow;
    // fused glass: how much of the real material shows (0 = pure glass), thin-film shimmer strength
    float  fusedK, filmK, pad3, pad4;
};
#ifndef GA_GLINT
#define GA_GLINT 0
#endif
#ifndef GA_LEATHER
#define GA_LEATHER 0
#endif
// the game's own pixel constant buffer (Actor / ActorGlint: b1 space0, bound by the game's root arguments we
// re-apply): leather's dye (ChangeColor) lives there
cbuffer GameActor : register(b1, space0) { float4 gGame[32]; };
ByteAddressBuffer gPrev : register(t2, space9); // last finished frame (lyrics, sky, post) for reflections

// Must match the Actor vertex shader's output signature exactly (probe M0):
// SV_Position o0, COLOR1 o1, COLOR0 o2, COLOR2 o3, COLOR3 o4, TEXCOORD0.xy o5, TEXCOORD3.xyz o6
struct PSIn {
    float4 pos : SV_Position;
    float4 c1  : COLOR1;
    float4 c0  : COLOR0;
    float4 c2  : COLOR2;
#if GA_GLINT
    float4 lu  : TEXCOORD2; // ActorGlint: glint layer UVs (unused, here so the signature matches)
#endif
    float4 c3  : COLOR3;
    float2 uv  : TEXCOORD0;
    float3 t3  : TEXCOORD3;
};

// chain: drawn as a steel mesh set in a whole glass plate (iron's shape) - its own texture is rings and holes, which
// as glass read as a checkerboard of outlines over the skin (seen in game 2026-11-11)
#if GA_VARIANT == 2 || GA_VARIANT == 3
#define GA_SHAPE (GA_VARIANT + 2)
#else
#define GA_SHAPE GA_VARIANT
#endif
bool painted(int2 t) {
    if (t.x < 0 || t.y < 0 || t.x > 63 || t.y > 31) return false;
    uint w = kMasks[GA_SHAPE][t.y * 2 + (t.x >> 5)];
    return ((w >> (31 - (t.x & 31))) & 1u) != 0;
}
bool ringAt(int2 t) {   // chain: a ring of the mesh (the texels Mojang drew)
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

// ---- Fused glass v2 (by Novak): the glass armour and the real material are one piece. Mojang's armour art says
//      what goes where (tools/GlassArmor/make_roles.py: the class of every texel - line work, shade, fill, highlight):
//        * where the artist drew LINES (rims, seams, the outline of each plate) there is polished METAL set into the
//          glass - round wire that mirrors the world around you (the sky you stand under is in it) and catches a light
//        * where they drew FILLS the plate is cut CRYSTAL in the material's colour - still see-through, still bending
//          what is behind it, thicker where the art is shaded
//        * where they drew HIGHLIGHTS the crystal is polished thin and flashes as you turn
//      Per material: iron = steel filigree in clear plate glass; gold = gold leaf in amber with gold flake; diamond =
//      every texel a cut facet (each bends the world its own way, splits it into colour, throws star glints);
//      netherite = smoked black glass with ember running in the seams; copper = copper wire, amber glass, verdigris;
//      chain = a mesh of steel beads, open between; leather = frosted glass in the team's dye with a stitched seam.
float gaHash(float2 q) { return frac(sin(dot(q, float2(127.1, 311.7))) * 43758.5453); }
float3 gaHue(float h) { return saturate(abs(frac(h + float3(0.0, 0.333, 0.667)) * 6.0 - 3.0) - 1.0); }
int roleAt(int2 t) {
    t = clamp(t, int2(0, 0), int2(63, 31));
    int  i = t.y * 64 + t.x;
    uint w = kRoles[GA_VARIANT][i >> 4];
    return int((w >> uint(30 - 2 * (i & 15))) & 3u);
}
bool metalAt(int2 t, int fi, int m) { return painted(t) && faceOf(t) == fi && (m == 1 ? ringAt(t) : roleAt(t) == 0); }

// glass: the plate as plain glass armour draws it (the world behind, bent). Returns the fused plate
float3 fusedPlate(int m, float3 glass, float2 t, int2 b, int fi, float2 p, float2 fs, float2 px, float2 off, float e, float S,
                  float pxPerTexel, float3 dye) {
    int    role = roleAt(b);
    float2 f = frac(t) - 0.5;
    float  h = gaHash(float2(b));
    float  view = px.x / res.x * 1.3 + px.y / res.y * 0.7;      // turns with the plate on screen
    float  nearK = smoothstep(0.9, 2.6, pxPerTexel);             // far away a texel is a pixel: no fine relief there
    float3 metal, tint;
    float  tintK;
    if (m == 2)      { metal = float3(0.88, 0.90, 0.95); tint = float3(0.84, 0.91, 1.00); tintK = 0.20; }   // iron
    else if (m == 4) { metal = float3(1.00, 0.74, 0.22); tint = float3(1.00, 0.70, 0.20); tintK = 0.50; }   // gold
    else if (m == 5) { metal = float3(0.90, 0.98, 1.00); tint = float3(0.16, 0.90, 0.98); tintK = 0.62; }   // diamond (set in white metal)
    else if (m == 6) { metal = float3(0.34, 0.29, 0.33); tint = float3(0.13, 0.09, 0.17); tintK = 0.80; }   // netherite
    else if (m == 3) { metal = float3(0.95, 0.52, 0.30); tint = float3(1.00, 0.58, 0.32); tintK = 0.34; }   // copper
    else if (m == 1) { metal = float3(0.80, 0.83, 0.88); tint = float3(0.80, 0.86, 0.96); tintK = 0.28; }   // chain
    else             { metal = dye * 0.50 + 0.04;        tint = dye;                      tintK = 0.50; }   // leather: the dye

    bool wire  = m == 1 ? ringAt(b) : role == 0;
    bool plate = !wire && role == 1 && (m == 2 || m == 3 || m == 4 || m == 6);   // a metal armour's drawn shade: brushed plate
    if (wire || plate) {
        // ---- metal. Line work is round wire standing proud (curved across its width only; chain = a bead per link);
        //      shade is flat brushed plate. Both mirror what is really around them: the frame, up and away from the
        //      plate - so mostly the sky you stand under.
        float2 nn = float2(0.0, 0.0);
        if (wire) {
            float lx = m == 1 ? 0.0 : 0.5 * (float(metalAt(b + int2(-1, 0), fi, m)) + float(metalAt(b + int2(1, 0), fi, m)));
            float ly = m == 1 ? 0.0 : 0.5 * (float(metalAt(b + int2(0, -1), fi, m)) + float(metalAt(b + int2(0, 1), fi, m)));
            if (lx > 0.75 && ly > 0.75) { lx = 0.6; ly = 0.6; }      // a solid patch of line work: a low dome, not a flat spot
            nn = f * 2.0 * float2(1.0 - lx, 1.0 - ly) * nearK;
        }
        float3 N  = normalize(float3(nn * 0.95, 0.60));
        float  brush = gaHash(float2(float(b.y) * 3.0 + floor(t.y * 3.0), floor(float(b.x) / 5.0)));   // fine horizontal grain
        float2 bo = plate ? float2((brush - 0.5) * 26.0 * S, 0.0) : float2(0.0, 0.0);
        float3 env = (sceneAt(px + nn * 120.0 * S + bo + float2(0.0, -90.0 * S)) + sceneAt(px + nn * 64.0 * S + bo + float2(22.0 * S, -150.0 * S))) * 0.5;
        float  spec = wire ? pow(saturate(dot(N, normalize(float3(-0.36, -0.72, 0.60)))), 22.0) : 0.0;
        float  band = exp(-pow(frac((p.x + p.y * 0.6) / max(fs.x + fs.y, 1.0) * 0.8 + view * 0.9 - time * 0.05) - 0.5, 2.0) / 0.006);
        float3 c = metal * (wire ? (m == 1 ? 0.38 + 0.55 * dot(env, float3(0.3, 0.4, 0.3)) : 0.24 + 1.05 * env) : (0.40 + 0.75 * env) * (0.90 + 0.14 * brush)) * (1.0 - 0.38 * dot(nn, nn));   // chain: steel-grey beads, not the colour behind
        c += (spec * 0.32 + band * (plate ? 0.50 : 0.30)) * lerp(float3(1, 1, 1), metal, 0.35);
        if (m == 6) {   // netherite: ember runs in the seams and breathes
            float pulse = 0.55 + 0.45 * sin(time * 1.3 + h * 6.0 + (p.x + p.y) * 0.5);
            if (wire) c = c * 0.55 + float3(1.60, 0.42, 0.07) * pulse * saturate(1.0 - dot(nn, nn) * 1.4) * 0.9;
        }
        if (m == 3 && h > 0.80) c = lerp(c, float3(0.30, 0.78, 0.66) * (0.35 + 0.8 * env), 0.75);   // copper: verdigris here and there
        return c;
    }
    // ---- crystal: the world behind, bent, soaked in the material's colour - and holding enough colour of its own
    //      that a diamond chestplate is aqua and a gold one is amber whatever stands behind it
    float3 g = glass;
    float  fh = gaHash(floor(float2(b) * 0.5) + 2.3);              // a 2x2-texel facet
    if (m == 5) {   // diamond: every texel is a facet - it bends the world its own way and splits it into colour
        float2 fo = (float2(gaHash(float2(b) + 3.1), gaHash(float2(b) + 7.7)) - 0.5) * 38.0 * S * nearK;
        g.r = sceneAt(px + off + fo).r;
        g.g = sceneAt(px + off + fo * 1.14).g;
        g.b = sceneAt(px + off + fo * 1.30).b;
    }
    float thick = role == 1 ? 1.45 : (role == 3 ? (m == 5 ? 0.85 : 0.50) : 1.0);   // drawn shade = thicker crystal, drawn highlight = polished thin
    g = lerp(g, g * tint * 1.30 + tint * 0.07, saturate(tintK * thick));
    float selfK = m == 5 ? 0.46 : (m == 4 ? 0.34 : (m == 3 ? 0.28 : (m == 0 ? 0.30 : (m == 6 ? 0.55 : (m == 2 ? 0.30 : 0.62)))));   // iron: frosted steel-white; chain: frosted steel between the rings (lighter: the skin behind made a checkerboard)
    float3 own = tint * (m == 5 ? (role == 1 ? 0.42 : 0.62) + 0.34 * fh : (role == 1 ? 0.50 : 0.72));
    g = lerp(g, own, selfK * (role == 3 ? (m == 5 ? 0.85 : 0.6) : 1.0));
    // the metal stands proud of the glass: it shades the crystal under it and lights the crystal over it
    float up = float(metalAt(b + int2(0, -1), fi, m)), dn = float(metalAt(b + int2(0, 1), fi, m));
    float lf2 = float(metalAt(b + int2(-1, 0), fi, m));
    g *= 1.0 - 0.30 * nearK * max(up * smoothstep(0.15, -0.5, f.y), lf2 * smoothstep(0.15, -0.5, f.x) * 0.6);
    g += 0.07 * nearK * dn * smoothstep(-0.1, 0.5, f.y) * tint;
    if (role == 3) {   // polished: it flashes when it turns to the light
        float tw = pow(saturate(sin(h * 40.0 + view * 9.0 + time * 0.8)), 16.0);
        g += (0.07 + 0.50 * tw) * lerp(float3(1, 1, 1), tint, 0.30);
        if (m == 5) {  // and a diamond throws a star
            float2 a = abs(f);
            g += tw * tw * (exp(-a.x * 26.0) * exp(-a.y * 5.0) + exp(-a.y * 26.0) * exp(-a.x * 5.0)) * 0.9 * nearK;
        }
    }
    if (m == 4) {      // gold flake hanging in the amber
        float2 sub = floor(t * 3.0);
        float  hf = gaHash(sub + 11.0);
        if (hf > 0.93) g += float3(1.0, 0.80, 0.35) * pow(saturate(sin(hf * 60.0 + view * 12.0 + time * 0.5)), 6.0) * 0.55 * nearK;
    }
    if (m == 6) g += float3(1.0, 0.30, 0.05) * 0.05 * (0.5 + 0.5 * sin(time * 1.3 + h * 6.0));   // the ember's glow inside the dark glass
    if (m == 0) {      // leather: frosted, with a stitched seam one texel in from the rim
        g = lerp(g, tint * 0.75 + 0.05, 0.22);
        float st = step(0.5, frac((p.x + p.y) * 0.5)) * smoothstep(0.55, 0.35, e) * smoothstep(0.15, 0.3, e);
        g = lerp(g, tint * 0.35, st * 0.5 * nearK);
    }
    return g;
}

float4 PSGlassArmor(PSIn i) : SV_Target {
    float2 t = i.uv * float2(64.0, 32.0); // armor texel space
    int2   b = int2(floor(t));
    if (pad == 1u) return float4(1.0, 0.0, 1.0, 1.0); // debug: does the replayed draw land at all?
    if (pad == 2u) return float4(saturate(i.c1.rgb), 1); // debug: what the game's vertex shader hands us
    if (pad == 3u) return float4(saturate(i.c0.rgb), 1);
    if (pad == 4u) return float4(saturate(i.c2.rgb), 1);
    if (pad == 5u) return float4(saturate(i.c3.rgb), 1);
    if (pad == 6u) return float4(saturate(float3(i.c0.a, i.c2.a, i.c3.a)), 1);
    if (pad == 7u) return float4(frac(i.t3), 1);
    if (pad == 8u) return float4(saturate(gGame[min(dbgRow, 31u)].rgb), 1);
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
#if GA_GLINT
    if (enchFlow > 0.5) { float per = (p.x + p.y) / (fs.x + fs.y); ench = pow(0.5 + 0.5 * sin((per * 2.0 - time * 0.55) * 6.2831), 6.0); }
#endif
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
    // leather: the item's dye (Hive team colour) is the neon and the glass colour
    float3 baseEdge = GA_EDGE, baseTint = GA_TINT;
    float  dyeK = 0;
#if GA_LEATHER
    {
        int row = GA_GLINT ? dyeRowGlint : dyeRow;
        if (row >= 0) {
            float3 dye = saturate(gGame[min(row, 31)].rgb);
            float  dm  = max(max(dye.r, dye.g), max(dye.b, 1e-3));
            float3 dv  = dye / dm;                                         // vivid: brightest channel = 1
            dv = saturate(lerp(dot(dv, float3(0.299, 0.587, 0.114)).xxx, dv, 1.3));
            baseEdge = dv;
            baseTint = dye;
            dyeK     = 1;
        }
    }
#endif
    // Novak Glass: the armor follows the match like the weapons (pad3: 0 the start .. 1 the endgame; pad4: Novak on).
    // Every material: at the start clear Future Aero glass - the body a pale aqua-white with a breath of the material's
    // colour, the outline a soft clean white-aqua; by the endgame neon - the material's colour turned up and glowing.
    float ngK = pad4 > 0.5 ? smoothstep(0.30, 0.95, pad3) : -1.0;
    if (ngK >= 0.0) baseTint = lerp(float3(0.86, 0.95, 1.00), baseTint, 0.35);   // the same glass all match (night lights it, it does not recolour it)
    float3 edgeB = lerp(baseEdge, acc, cMix * 0.85 * (1.0 - 0.6 * dyeK)); // team colour stays readable over the cover
    edgeB = lerp(edgeB, wordCol * 1.2, saturate(wordColK * boot) * 0.85); // colour words flash the outline
    // beat-pulse: brighter on every kick, a big flare on the drop
    float kick  = mus * beat * beatK;
    float flare = drop * boot * react;
    float3 edgeC = edgeB * (1.0 + ench * 1.2 + kick * 0.9 + flare * 1.4 + ignite * 1.2);
    if (ngK >= 0.0) {
        float3 aeroE = lerp(float3(0.90, 0.97, 1.00), edgeC, 0.35) * 0.80;
        float3 neonE = saturate(edgeC * 1.35) * (1.45 + 0.30 * sin(time * 2.2));
        edgeC = lerp(aeroE, neonE, ngK);
    }
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
    c   = lerp(c, lerp(float3(0.0065, 0.0032, 0.018), baseTint, 0.35 + 0.25 * dyeK), 0.10 + 0.06 * dyeK);
    // cover-coloured glass: a light colour cast through the glass (it stays see-through); a dark cover deepens it
    float cLum = dot(cover, float3(0.299, 0.587, 0.114));
    c *= lerp(float3(1, 1, 1), lerp(float3(1, 1, 1), acc, 0.30) * lerp(1.0, 0.72, saturate(1.0 - cLum * 1.6)), cMix);
    c += acc * 0.025 * cMix;

    // fused: the armour's own material and the glass, one piece (fusedPlate)
    {
        int    gm = GA_VARIANT / 2;
        float3 fp = fusedPlate(gm, c, t, b, fi, p, fs, px, off, e, S, pxPerTexel, baseTint);
        c = lerp(c, fp, saturate(fusedK));
        // thin-film shimmer: the hue slides with the plate's turn and the bevel - a soap-bubble skin on the glass
        float  film = frac(e * 1.6 + (px.x / res.x) * 0.9 - (px.y / res.y) * 0.6 + time * 0.035 + float(fi) * 0.11);
        c += gaHue(film) * (0.03 + 0.14 * e) * filmK * (gm == 1 || roleAt(b) == 0 ? 0.35 : 1.0);
    }

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
    float3 E   = baseEdge;
    float  sat = max(max(E.r, E.g), E.b) - min(min(E.r, E.g), E.b);
    c += lerp(edgeC, edgeC * palC, 0.5) * (0.24 + 0.30 * ench + 0.25 * kick + 0.4 * flare) * edgeLv * lerp(0.45, 1.0, saturate(sat * 1.5)) * pow(saturate(1.0 + d / max(bevel, 1.0)), 3.0);
    c += edgeC * 0.015 * edgeLv;
    float aa = max(fwidth(dIn), 1e-4) * 1.1;
    float lw = 0.19 + 0.07 * kick + 0.10 * min(flare, 1.0) + 0.06 * max(ngK, 0.0);  // a touch thicker on the kick, thicker still on the drop (and at the Novak endgame)
    float ln = 1.0 - smoothstep(lw - aa, lw + aa, dIn);
    if (ngK >= 0.0) {   // Novak night / endgame: the glass lights itself (seen in game: very dim, hard to read at night)
        c += baseTint * 0.24 * ngK + edgeC * 0.12 * ngK;
        edgeLv = max(edgeLv, ngK);                       // the outline at full strength however dark it is
    }
    c = lerp(c, edgeC * 1.25, ln * saturate(edgeLv));
    return float4(saturate(c), 1.0);
}
