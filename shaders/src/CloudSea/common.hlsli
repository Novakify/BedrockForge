// Cloud Sea by Novak - World FX block lighting for the Cloud Sea sky (Renderer Foundation F6 > World FX > Cloud Sea).
// Same light as the Cloud Sea lab (tools/WorldFx/lab/cloudsea.glsl): a low golden sun in the north, violet sky light
// in the shade, a rose bounce off the cloud sea from below, glowing leaves, and warm air toward the sun.
// Maths runs in linear light: textures are decoded, lit, then encoded back for the game's target.

// The key light follows the living match sky (Renderer Foundation's cloud_sea_common.hlsli csSetPhase - same numbers):
// golden hour -> dusk (first bed down) -> moonlit night (last fight). The block shader can't see Renderer Foundation,
// but Bedrock Forge moves the game's time of day with the match, and the game's fog colour follows it: that is the
// phase here (csPhaseFromFog). Call csBegin(FogColor, time) first thing in main().
static float3 CS_SUN   = float3(0.17605, 0.11247, -0.97794);   // toward the sun (the moon at night)
static float3 CS_SUNC  = float3(1.00, 0.72, 0.50) * 1.85;      // its light on the world
static float  CS_DUSK  = 0.0, CS_NIGHT = 0.0, CS_PHASE = 0.0, CS_TIME = 0.0, CS_LIGHTK = 1.0;
static float  CS_CLOUDREFL = 1.0;   // how sharp the clouds are in a reflection (matte surfaces: none - they smear into streaks)
static const float2 CS_SUN_AZ  = float2(0.17717, -0.98418);
static const float2 CS_MOON_AZ = float2(-0.85671, 0.51580);
float3 csDirAzEl(float2 az, float el) { return float3(az.x * cos(el), sin(el), az.y * cos(el)); }
float  csLuma(float3 c);
void csSetPhase(float k) {
    CS_PHASE = k;
    CS_DUSK  = smoothstep(0.0, 1.0, k);
    CS_NIGHT = smoothstep(1.15, 1.9, k);
#if defined(CS_NOVAK)
    float3 sun  = csDirAzEl(CS_SUN_AZ, radians(lerp(26.0, 1.5, CS_DUSK)));   // Novak Glass: a higher sun (Renderer Foundation csSetPhase: same)
#else
    float3 sun  = csDirAzEl(CS_SUN_AZ, radians(lerp(6.5, 1.5, CS_DUSK)));
#endif
    float3 moon = csDirAzEl(CS_MOON_AZ, radians(31.0));
    CS_SUN = normalize(lerp(sun, moon, smoothstep(1.3, 1.6, k)));
    CS_LIGHTK = lerp(lerp(1.0, 0.40, smoothstep(0.25, 1.0, k)), 0.62, CS_NIGHT) * (1.0 - 0.75 * smoothstep(1.1, 1.45, k) * (1.0 - smoothstep(1.45, 1.8, k)));
#if defined(CS_NOVAK)
    // Novak Glass: clean light. A white sun with only a breath of warmth, rose at dusk, a violet-white moon
    float3 gold = float3(1.00, 0.95, 0.87), dusk = float3(1.00, 0.58, 0.62), moonC = float3(0.66, 0.63, 1.00);
    CS_SUNC = lerp(lerp(gold, dusk, CS_DUSK), moonC, smoothstep(1.3, 1.6, k)) * 1.62 * CS_LIGHTK;
#else
    float3 gold = float3(1.00, 0.72, 0.50), dusk = float3(1.00, 0.50, 0.40), moonC = float3(0.58, 0.70, 1.00);
    CS_SUNC = lerp(lerp(gold, dusk, CS_DUSK), moonC, smoothstep(1.3, 1.6, k)) * 1.85 * CS_LIGHTK;
#endif
}
// The phase from the game's daylight: the light map's full-sky texel (no block light) - measured in game 2026-11-11:
// 1.0 at noon and at Bedrock Forge's golden hour (tick 10920), ~0.85 at its dusk (12480), ~0.55 later, ~0.3 at night
// (14400). (The fog colour was tried first: it changes with where you look at sunset, so the world would relight as
// you turn - no.)
float csPhaseFromDaylight(float l) {
    return (1.0 - smoothstep(0.86, 0.97, l)) + (1.0 - smoothstep(0.34, 0.62, l));
}
// Mobs and players have no light map in their pass, and their light level barely moves with the hour (0.88 noon,
// 0.75 night - measured). The fog colour is the only clock there: it goes dark at night whichever way you look (at
// sunset it depends on the view, so it is NOT used for dusk - actors keep the golden light until night).
float csActorPhase(float3 fog) { return 2.0 * (1.0 - smoothstep(0.08, 0.40, csLuma(fog))); }
void csBeginActor(float4 fog, float4 viewPosTime) {
    csSetPhase(csActorPhase(fog.rgb));
    CS_TIME = viewPosTime.w;
}
void csBegin(float daylight, float4 viewPosTime) {
    csSetPhase(csPhaseFromDaylight(daylight));
    CS_TIME = viewPosTime.w;
}

float  csLuma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
float  csMax3(float3 c) { return max(c.r, max(c.g, c.b)); }
float3 csLin(float3 c) { return pow(max(c, 0.0), 2.2); }
float3 csEnc(float3 c) {
    c = max(c, 0.0);
    float3 k = max(c - 0.55, 0.0);
    c = min(c, 0.55) + k / (1.0 + k * 1.9); // linear up to 0.55, then a soft shoulder
    return pow(c, 1.0 / 2.2);
}

// face normal from the world position; blocks snap to their axis (no edge noise), plants/mobs keep the true one
float3 csNormal(float3 wp, float3 V, bool snap) {
    float3 c = cross(ddx(wp), ddy(wp));
    float  l = dot(c, c);
    float3 n = l > 1e-20 ? c * rsqrt(l) : float3(0.0, 1.0, 0.0);
    if (dot(n, V) < 0.0) n = -n;
    if (snap) {
        float3 a = abs(n);
        float  m = csMax3(a);
        if (m > 0.92) n = a.x == m ? float3(sign(n.x), 0.0, 0.0) : (a.y == m ? float3(0.0, sign(n.y), 0.0) : float3(0.0, 0.0, sign(n.z)));
    }
    return n;
}
// the game bakes a fixed shade per face into the vertex colour (top 1, z sides 0.8, x sides 0.6, bottom 0.5);
// our sun replaces it, so it comes back out (smooth-lighting corner shade stays in)
float csVanillaShade(float3 n) {
    float3 a = abs(n);
    if (a.y > 0.9) return n.y > 0.0 ? 1.0 : 0.5;
    if (a.x > 0.9) return 0.6;
    if (a.z > 0.9) return 0.8;
    return 1.0;
}

// the sky (same gradient the Cloud Sea sky draws), used for the haze on far blocks
float3 csSky(float3 d) {
#if defined(CS_NOVAK)
    {   // the Novak Glass sky (Renderer Foundation cloud_sea_common.hlsli ngSkyBase - same numbers)
        float  e = max(d.y, 0.0);
        float  sunSide = dot(normalize(float3(d.x, max(d.y, 0.0), d.z)), CS_SUN) * 0.5 + 0.5;
        float3 cD = lerp(lerp(float3(0.24, 0.44, 0.74), float3(0.62, 0.70, 0.84), pow(sunSide, 8.0)), float3(0.105, 0.215, 0.520), smoothstep(0.0, 0.11, e));
        cD = lerp(cD, float3(0.028, 0.058, 0.235), smoothstep(0.15, 0.85, e));
        float3 cK = lerp(lerp(float3(0.19, 0.115, 0.33), float3(1.00, 0.45, 0.36), pow(sunSide, 3.0)), float3(0.072, 0.040, 0.180), smoothstep(0.0, 0.15, e));
        cK = lerp(cK, float3(0.014, 0.012, 0.074), smoothstep(0.10, 0.70, e));
        float3 cN = lerp(float3(0.044, 0.027, 0.112), float3(0.015, 0.012, 0.062), smoothstep(0.0, 0.14, e));
        cN = lerp(cN, float3(0.0035, 0.0035, 0.018), smoothstep(0.08, 0.60, e));
        return lerp(lerp(cD, cK, CS_DUSK), cN, CS_NIGHT);
    }
#endif
    float  e = max(d.y, 0.0);
    float  mu = dot(normalize(float3(d.x, max(d.y, 0.0), d.z)), CS_SUN);
    float  sunSide = mu * 0.5 + 0.5;
    float  towardSun = pow(sunSide, 5.0);
    float3 zen = float3(0.036, 0.042, 0.135);
    float3 body = lerp(float3(0.085, 0.080, 0.215), float3(0.235, 0.120, 0.235), pow(sunSide, 1.6));
    float3 hor = lerp(lerp(float3(0.250, 0.170, 0.245), float3(0.290, 0.150, 0.225), smoothstep(0.0, 0.08, e)),
                      lerp(float3(0.62, 0.185, 0.085), float3(1.02, 0.300, 0.040), towardSun), smoothstep(0.30, 0.92, sunSide));
    float3 c = lerp(hor, body, smoothstep(0.0, lerp(0.10, 0.24, towardSun), e));
    c = lerp(c, zen, smoothstep(0.06, 0.62, e));
    float ang = acos(clamp(dot(d, CS_SUN), -1.0, 1.0));
    c += float3(1.05, 0.50, 0.10) * exp(-ang * 6.0) * exp(-e * 8.0) * 0.75 * (1.0 - CS_NIGHT);
    // dusk: the sun is down, the gold drains to rose and violet; night: deep blue, a pale glow round the moon
    c *= lerp(float3(1.0, 1.0, 1.0), float3(0.62, 0.44, 0.72), CS_DUSK);
    float3 night = lerp(float3(0.040, 0.050, 0.110), float3(0.010, 0.014, 0.040), smoothstep(0.0, 0.5, e))
                 + float3(0.22, 0.28, 0.45) * exp(-ang * 9.0) * 0.5;
    return lerp(c, night, CS_NIGHT);
}

// ---------------------------------------------------------------- materials
// Material ids come from the World FX Materials pack (tools/WorldFx/make_material_pack.py): alpha = 255 - 4 * id.
// Keep this table in step with CLASSES there.
#define MAT_NONE 0
#define MAT_WOOL 1
#define MAT_CONCRETE 2
#define MAT_POWDER 3
#define MAT_PLANKS 4
#define MAT_LOG 5
#define MAT_STONE 6
#define MAT_CUTSTONE 7
#define MAT_ENDSTONE 8
#define MAT_OBSIDIAN 9
#define MAT_TERRACOTTA 10
#define MAT_GLAZED 11
#define MAT_METAL 12
#define MAT_GEM 13
#define MAT_ORE 14
#define MAT_SAND 15
#define MAT_SOIL 16
#define MAT_BRICKS 17
#define MAT_QUARTZ 18
#define MAT_SNOW 19
#define MAT_ICE 20      // packed ice, blue ice: depth inside
#define MAT_GRASS 21    // grass block tops, moss: the wind rolls over them
#define MAT_LAMP 22     // glowstone, sea lanterns, lanterns, torches: they breathe
#define MAT_LEAVES 23   // leaves: gusts, fluttering glints
#define MAT_PLANT 24    // tall grass, ferns, vines
#define MAT_FLOWER 25   // flowers: petals hold the light, glow softly after dusk
#define MAT_CARVED 26   // chiseled / carved blocks: parallax depth (the only ordinary blocks that get it)
#define MAT_LAST 26
struct CsMat {
    float bump;    // relief from the texture's own light/dark pattern (0 = flat)
    float detail;  // 1 = full texture detail, lower = softer / calmer
    float spec;    // sun highlight strength
    float rough;   // highlight tightness (bigger = smaller, sharper)
    float refl;    // sky reflection at grazing angles
    float metal;   // reflection and highlight take the surface colour
    float wrap;    // light wraps round the surface (fabric)
    float velvet;  // fabric rim glow at grazing angles
    float sparkle; // texel glints (gems, sand grains, snow)
    float sat;     // colour richness
    float violet;  // obsidian's cold sheen
};
CsMat csMat(int id) {
    CsMat m;
    m.bump = 0.35; m.detail = 0.95; m.spec = 0.03; m.rough = 20.0; m.refl = 0.0; m.metal = 0.0;
    m.wrap = 0.0; m.velvet = 0.0; m.sparkle = 0.0; m.sat = 1.0; m.violet = 0.0;
    switch (id) {
    case MAT_WOOL:       m.bump = 0.22; m.detail = 0.92; m.spec = 0.0;  m.wrap = 0.45; m.velvet = 0.32; m.sat = 1.06; break;
    case MAT_CONCRETE:   m.bump = 0.06; m.detail = 0.82; m.spec = 0.10; m.rough = 36.0; m.refl = 0.06; m.sat = 1.12; break;
    case MAT_POWDER:     m.bump = 0.30; m.detail = 0.95; m.spec = 0.0;  m.wrap = 0.12; m.sparkle = 0.10; break;
    case MAT_PLANKS:     m.bump = 0.55; m.detail = 1.0;  m.spec = 0.05; m.rough = 18.0; break;
    case MAT_LOG:        m.bump = 0.85; m.detail = 1.0;  m.spec = 0.02; break;
    case MAT_STONE:      m.bump = 0.95; m.detail = 1.0;  m.spec = 0.04; m.rough = 14.0; break;
    case MAT_CUTSTONE:   m.bump = 0.70; m.detail = 0.95; m.spec = 0.07; m.rough = 30.0; m.refl = 0.03; break;
    case MAT_ENDSTONE:   m.bump = 0.80; m.detail = 1.0;  m.spec = 0.04; break;
    case MAT_OBSIDIAN:   m.bump = 0.30; m.detail = 1.0;  m.spec = 0.55; m.rough = 110.0; m.refl = 0.30; m.sparkle = 0.35; m.violet = 1.0; break;
    case MAT_TERRACOTTA: m.bump = 0.30; m.detail = 0.94; m.spec = 0.02; m.sat = 1.05; break;
    case MAT_GLAZED:     m.bump = 0.22; m.detail = 1.0;  m.spec = 0.45; m.rough = 140.0; m.refl = 0.24; break;
    case MAT_METAL:      m.bump = 0.35; m.detail = 0.9;  m.spec = 0.85; m.rough = 60.0; m.refl = 0.60; m.metal = 1.0; break;
    case MAT_GEM:        m.bump = 0.40; m.detail = 1.0;  m.spec = 0.60; m.rough = 90.0; m.refl = 0.30; m.sparkle = 1.0; m.sat = 1.08; break;
    case MAT_ORE:        m.bump = 0.90; m.detail = 1.0;  m.spec = 0.04; m.rough = 14.0; m.sparkle = 0.8; break;
    case MAT_SAND:       m.bump = 0.38; m.detail = 0.9;  m.spec = 0.02; m.sparkle = 0.18; break;
    case MAT_SOIL:       m.bump = 0.60; m.detail = 1.0;  m.spec = 0.0;  break;
    case MAT_BRICKS:     m.bump = 0.90; m.detail = 1.0;  m.spec = 0.04; m.rough = 16.0; break;
    case MAT_QUARTZ:     m.bump = 0.25; m.detail = 0.92;  m.spec = 0.32; m.rough = 70.0; m.refl = 0.14; break;
    case MAT_SNOW:       m.bump = 0.45; m.detail = 0.95; m.spec = 0.10; m.rough = 24.0; m.wrap = 0.30; m.sparkle = 0.9; break;
    case MAT_ICE:        m.bump = 0.18; m.detail = 1.0;  m.spec = 0.70; m.rough = 160.0; m.refl = 0.26; m.sparkle = 0.5; break;
    case MAT_GRASS:      m.bump = 0.55; m.detail = 1.0;  m.spec = 0.03; m.wrap = 0.10; break;
    case MAT_LAMP:       m.bump = 0.20; m.detail = 1.0;  m.spec = 0.10; m.rough = 40.0; break;
    case MAT_LEAVES:     m.bump = 0.30; m.detail = 1.0;  m.spec = 0.06; m.rough = 30.0; break;
    case MAT_PLANT:      m.bump = 0.0;  m.detail = 1.0;  m.spec = 0.04; break;
    case MAT_CARVED:     m.bump = 0.55; m.detail = 1.0;  m.spec = 0.06; m.rough = 24.0; break;
    case MAT_FLOWER:     m.bump = 0.0;  m.detail = 1.0;  m.spec = 0.05; m.sat = 1.05; break;
    }
    return m;
}
float csHash(float2 p) { p = frac(p * float2(0.1031, 0.1030)); p += dot(p, p.yx + 33.33); return frac((p.x + p.y) * p.x); }
float3 csHash3(float2 p) { return float3(csHash(p), csHash(p + 17.13), csHash(p + 41.71)); }

float csVN(float2 p) {   // value noise (no texture in these materials)
    float2 i = floor(p), f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(csHash(i), csHash(i + float2(1, 0)), f.x), lerp(csHash(i + float2(0, 1)), csHash(i + float2(1, 1)), f.x), f.y);
}
// ---- wind: gust fronts roll across the map from the south-west (~12 blocks a second). The block vertex shader sways
//      leaves and plants harder inside a gust (same field: _shared/RenderChunkWave.vertex.hlsli csGustV); here the
//      gust turns leaves and grass a little silver (their undersides, flipped up), so you SEE the wind cross an island.
#if defined(CS_NOVAK)
// ---- Novak Glass: a block of cast glass. Looking into a face, the ray bends in and leaves through a side wall or the
//      back wall of the cube: xyz = where it leaves (face u, v, depth 0..1 going in), w = how far it went through glass.
float4 ngExit(float2 f, float3 vt, float bend) {
    float3 rd = normalize(float3(-vt.xy * bend, -max(vt.z, 0.05)));
    float2 sd = float2(abs(rd.x) > 1e-4 ? rd.x : 1e-4, abs(rd.y) > 1e-4 ? rd.y : 1e-4);
    float2 ts = (step(0.0, sd) - f) / sd;
    float  t  = min(min(ts.x, ts.y), 1.0 / -rd.z);
    return float4(f + rd.xy * t, -rd.z * t, t);
}
// 1 on the outer texel ring of the inner wall the ray leaves through: the cube's far edges, seen through the glass
// (whole 1/16 texels, so they stay pixel art and slide as you move)
float ngInnerEdge(float3 h) {
    float3 q = (floor(saturate(h) * 15.999) + 0.5) / 16.0;
    float3 e = min(q, 1.0 - q);
    float  mid = e.x + e.y + e.z - min(e.x, min(e.y, e.z)) - max(e.x, max(e.y, e.z));
    return step(mid, 0.04);
}
#endif
float csGust(float2 xz, float t) {
    float2 p = xz * 0.045 - float2(0.8660254, 0.5) * t * 0.55;
    float  g = csVN(p) * 0.65 + csVN(p * 2.1 + 5.3) * 0.35;
    return smoothstep(0.42, 0.86, g);
}
// the high clouds as a polished surface sees them: they drift with the wind and take the hour's light (sun side lit,
// the rest in shade), so every reflection in the world moves with the sky
float3 csSkyMoving(float3 r) {
    float3 c = csSky(r);
    float  e = max(r.y, 0.0);
    float2 p = r.xz / max(r.y, 0.07) * 0.85 + CS_TIME * float2(0.016, 0.006);
    float  n = csVN(p) * 0.55 + csVN(p * 2.3 + 7.1) * 0.30 + csVN(p * 5.3 + 3.3) * 0.15;
    float  cl = smoothstep(0.52, 0.78, n) * smoothstep(0.06, 0.26, e) * (1.0 - smoothstep(0.55, 0.95, e) * 0.6) * CS_CLOUDREFL;   // low clouds stretch into streaks: fade them
    float  toSun = pow(saturate(dot(normalize(float3(r.x, 0.0, r.z)), normalize(float3(CS_SUN.x, 0.0, CS_SUN.z))) * 0.5 + 0.5), 2.0);
    float3 lit = lerp(lerp(float3(1.25, 0.66, 0.48), float3(0.95, 0.42, 0.48), CS_DUSK), float3(0.20, 0.24, 0.36), CS_NIGHT);
    float3 shd = lerp(lerp(float3(0.42, 0.28, 0.42), float3(0.24, 0.16, 0.30), CS_DUSK), float3(0.05, 0.06, 0.11), CS_NIGHT);
#if defined(CS_NOVAK)
    c = lerp(c, lerp(lerp(float3(0.80, 0.86, 0.95), float3(0.70, 0.36, 0.50), CS_DUSK), float3(0.06, 0.06, 0.15), CS_NIGHT), cl * 0.28);   // thin ice cirrus
#else
    c = lerp(c, lerp(shd, lit, toSun), cl * 0.85);
#endif
    // stars between the clouds at night (one direction cell each, they don't crawl)
    if (CS_NIGHT > 0.01) {
        float3 a = abs(r);
        float2 g = (a.x > a.z ? r.zy / a.x : r.xy / max(a.z, 1e-3)) * 90.0;
        float  h = csHash(floor(g));
        float  st = step(0.985, h) * smoothstep(0.35, 0.0, length(frac(g) - 0.5)) * (0.6 + 0.4 * sin(CS_TIME * (1.0 + h * 3.0) + h * 50.0));
        c += float3(0.85, 0.90, 1.0) * st * CS_NIGHT * (1.0 - cl) * smoothstep(0.05, 0.3, e) * 1.6;
    }
    return c;
}

// what a reflective surface sees: the Cloud Sea sky above, the lit cloud tops below
float3 csReflect(float3 r) {
    if (r.y >= 0.0) return csSkyMoving(r);
    float3 sea = lerp(float3(0.62, 0.34, 0.36), float3(0.95, 0.42, 0.18), pow(saturate(dot(normalize(float3(r.x, 0.0, r.z)), CS_SUN) * 0.5 + 0.5), 3.0));
    sea = lerp(lerp(sea, float3(0.55, 0.30, 0.46), CS_DUSK), float3(0.10, 0.14, 0.30), CS_NIGHT);
#if defined(CS_NOVAK)
    sea = lerp(lerp(float3(0.045, 0.075, 0.230), float3(0.060, 0.030, 0.150), CS_DUSK), float3(0.010, 0.008, 0.034), CS_NIGHT);   // the void under the map
#endif
    return lerp(csSky(normalize(float3(r.x, 0.02, r.z))), sea, smoothstep(0.0, -0.12, r.y));
}

// alb linear (corner shade already in it), n = shading normal (relief), ng = face normal, sunVis = open sky toward
// the sun, sky = sky light 0..1, leaf = foliage (light passes through), V = toward the eye, cav = texture cavity 0..1,
// glintN = per-texel tilted normal (sparkle), fade = 1 near, 0 where texels get smaller than pixels
float3 csLight(float3 alb, float3 n, float3 ng, float sunVis, float sky, float leaf, float3 V, CsMat m, float cav, float3 glintN, float fade) {
    float green = saturate((alb.g - max(alb.r, alb.b)) * 5.0);   // pure greens refuse to sit in a sunset: toward olive
#if !defined(CS_NOVAK)
    alb = lerp(alb, csLuma(alb) * float3(1.22, 0.98, 0.42), green * 0.42);
#endif
    alb = max(lerp(csLuma(alb).xxx, alb, m.sat), 0.0);
    float3 sunC = CS_SUNC;                                       // soft gold (the hour's key light): lit faces glow, never orange paint
    float  ndlRaw = dot(n, CS_SUN);
    float  ndl = saturate((ndlRaw + m.wrap) / (1.0 + m.wrap)) * step(-0.02, dot(ng, CS_SUN) + m.wrap); // relief never lights a face turned away
    float3 amb  = lerp(float3(0.255, 0.140, 0.150), float3(0.200, 0.190, 0.330), smoothstep(-1.0, 1.0, n.y));
    float  toSun = dot(n.xz, normalize(CS_SUN.xz));
    float  side  = 1.0 - abs(n.y);
#if defined(CS_NOVAK)
    // Novak Glass: the sky's own cold light from above, the void's from below; a little bounce toward the sun
    amb  = lerp(float3(0.150, 0.165, 0.250), float3(0.230, 0.285, 0.430), smoothstep(-1.0, 1.0, n.y));
    amb += float3(0.200, 0.180, 0.150) * max(toSun, 0.0) * side * (1.0 - 0.55 * CS_DUSK);
    amb += float3(0.070, 0.090, 0.160) * max(-toSun, 0.0) * side;
#else
    amb += float3(0.40, 0.165, 0.070) * max(toSun, 0.0) * side * (1.0 - 0.55 * CS_DUSK);
    amb += float3(0.105, 0.085, 0.150) * max(-toSun, 0.0) * side;
#endif
    // dusk: the light goes rose and violet; night: a cool blue sky light, bright enough to fight in
    amb *= lerp(float3(1.0, 1.0, 1.0), float3(0.92, 0.78, 1.05), CS_DUSK);
#if defined(CS_NOVAK)
    amb *= lerp(float3(1.0, 1.0, 1.0), float3(0.98, 0.86, 1.00), CS_DUSK);
    amb  = lerp(amb, lerp(float3(0.115, 0.105, 0.210), float3(0.185, 0.175, 0.340), smoothstep(-1.0, 1.0, n.y)), CS_NIGHT);   // indigo night
#else
    amb  = lerp(amb, lerp(float3(0.12, 0.13, 0.20), float3(0.19, 0.21, 0.31), smoothstep(-1.0, 1.0, n.y)), CS_NIGHT);   // RF tints the night blue on top
#endif
    amb *= lerp(0.08, 1.0, sky * sky);                             // caves and covered spots lose the sky
    amb *= lerp(1.0, cav, m.bump * 0.55 * fade);                   // texture cavities hold a little shade
    float3 dir = sunC * (ndl + 0.035 * max(n.y, 0.0)) * sunVis;
    if (leaf > 0.5) {
        float mu    = max(dot(-V, CS_SUN), 0.0);
        float trans = pow(mu, 4.0) * 0.9 + 0.12;
        float wrap  = max(dot(n, CS_SUN) * 0.5 + 0.5, 0.0);
#if defined(CS_NOVAK)
        dir = (sunC * wrap * 0.55 + float3(0.92, 1.00, 0.50) * 1.7 * trans) * sunVis;   // leaves glow green-gold, not sunset orange
#else
        dir = (sunC * wrap * 0.55 + float3(1.15, 0.80, 0.32) * 2.1 * trans) * sunVis;
#endif
        amb *= 1.7;
    }
    float3 c = alb * (amb + dir) * (1.0 - 0.72 * m.metal);         // metals have almost no diffuse: their reflections carry them
    float  ndv = saturate(dot(n, V));
    float  fres = pow(1.0 - ndv, 5.0);
    // fabric: a soft glow where the surface turns away from you (light caught in the fibres)
    c += alb * (amb * 0.8 + dir * 0.6) * m.velvet * pow(1.0 - ndv, 3.0);
    // sun highlight (normalised Blinn-Phong); metals tint it with their own colour
    float3 H = normalize(CS_SUN + V);
    float3 tintS = lerp(float3(1.0, 1.0, 1.0), sqrt(alb / max(csMax3(alb), 1e-3)), m.metal);   // sqrt: gold stays gold, not neon
    float  spec = pow(saturate(dot(n, H)), m.rough) * (m.rough + 8.0) / 25.0;
    c += sunC * tintS * spec * m.spec * (0.04 + 0.96 * pow(1.0 - saturate(dot(H, V)), 5.0) + m.metal * 0.5) * sunVis * step(0.0, ndlRaw);
    // sky reflection: the Cloud Sea on polished surfaces
    if (m.refl > 0.0) {
        float3 R = reflect(-V, n);
        float  f0 = lerp(0.04, 0.6, m.metal);
        float  F  = f0 + (1.0 - f0) * fres;
        CS_CLOUDREFL = smoothstep(0.12, 0.30, m.refl);
        float3 rc = csReflect(R);
        rc = lerp(rc, csLuma(rc).xxx, 0.45 * m.metal);   // brushed metal scatters the sky's colour: steel, not pink chrome
        rc *= lerp(float3(1.0, 1.0, 1.0), sqrt(alb / max(csMax3(alb), 1e-3)), m.metal);
        rc *= lerp(float3(1.0, 1.0, 1.0), float3(0.55, 0.45, 0.80), m.violet);   // obsidian: dark violet glass mirrors the sky darker
        CS_CLOUDREFL = 1.0;
        rc *= lerp(1.0, 0.45 + 1.1 * smoothstep(-0.05, 0.25, R.y) * (1.0 - 0.5 * smoothstep(0.25, 0.8, R.y)), m.metal); // contrast: dark below the horizon line, bright band, dimmer zenith
        c = lerp(c, rc * lerp(0.35, 1.0, sky), saturate(F * m.refl * 1.6)) + rc * m.metal * m.refl * 0.55 * sky;
    }
    // sparkle: single texels catch the sun as you move (texel-locked, so it never crawls or flickers on its own)
    if (m.sparkle > 0.0) {
        float3 Rg = reflect(-V, glintN);
        float g = pow(saturate(dot(Rg, CS_SUN)), 260.0);
        // a crystal splits the light: each glint has its own hue (a texel-locked rainbow 'fire', strongest on gems)
        float  hue = frac(dot(glintN, float3(7.13, 3.71, 5.29)) * 3.0);
        float3 fire = saturate(abs(frac(hue + float3(0.0, 0.333, 0.667)) * 6.0 - 3.0) - 1.0);
        float3 gc = lerp(lerp(float3(1.6, 1.25, 0.95), float3(0.9, 1.05, 1.5), CS_NIGHT), 0.5 + fire * 1.6, saturate(m.sparkle - 0.55) * 0.9);
        c += gc * g * m.sparkle * sunVis * fade * 3.0 * max(CS_LIGHTK, 0.35);
    }
    // obsidian: a cold violet sheen at grazing angles
    c += float3(0.22, 0.10, 0.42) * fres * 0.5 * m.violet * lerp(0.3, 1.0, sky);
    // keep strongly lit warm colours from going neon: past ~70% saturation, ease back toward the luma
    float mx = csMax3(c), mn = min(c.r, min(c.g, c.b));
    float sat = mx > 1e-4 ? (mx - mn) / mx : 0.0;
    return lerp(csLuma(c).xxx, c, 1.0 - 0.28 * smoothstep(0.62, 0.95, sat) * (1.0 - m.metal));
}

// ---- the item in your own hand: lit like a film's view model, not by where the sun happens to be. A key light high
//      over your right shoulder in the hour's colour, and the real sun (or moon) as a rim when it is behind the blade -
//      so the sword always reads, and still belongs to the scene. Returns how much of the rig was used (0 far away).
float csViewModel(inout float3 lit, float3 alb, float3 n, float3 V, CsMat m, float3 gn, float sky, float dEye, float3 rt, float3 up) {
    float fp = 1.0 - smoothstep(1.3, 2.1, dEye);
    [branch] if (fp > 0.0) {
        // (the hand pass's u_view is not the camera's - seen in game - so the rig is built from the eye direction and
        //  world up: key above and a little to the side of your eye, mostly from the front)
        up = float3(0, 1, 0);
        rt = cross(up, V);
        rt = dot(rt, rt) > 1e-6 ? normalize(rt) : float3(1, 0, 0);
        float3 keep = CS_SUN, keepC = CS_SUNC;
        CS_SUN  = normalize(V * 0.75 + up * 0.55 + rt * 0.35);
        CS_SUNC = lerp(lerp(float3(1.00, 0.76, 0.56), float3(1.00, 0.62, 0.55), CS_DUSK), float3(0.62, 0.74, 1.00), CS_NIGHT) * 1.55;
        float3 rig = csLight(alb, n, n, 1.0, max(sky, 0.55), 0.0, V, m, 1.0, gn, 1.0);
        CS_SUN = keep; CS_SUNC = keepC;
        float rim = pow(1.0 - saturate(dot(n, V)), 2.5) * pow(saturate(dot(-V, CS_SUN) * 0.5 + 0.5), 3.0);
        rig += alb * CS_SUNC * rim * 0.9 + CS_SUNC * rim * 0.25 * m.metal;
        rig *= lerp(0.35, 1.0, sky);                       // in a cave or under a roof it still follows the light there
        lit = lerp(lit, rig, fp);
    }
    return fp;
}

// air: far blocks drift into the sky behind them; toward the sun the air glows warm
float3 csHaze(float3 c, float3 wp, float3 cam, float vanillaFog) {
    float3 v = wp - cam;
    float  dist = length(v);
    float3 d = v / max(dist, 1e-3);
    float  f = 1.0 - exp(-dist / 520.0);
    f = max(f * 0.85, vanillaFog);                                 // the game's fog still hides the chunk edge
    float3 hz = csSky(normalize(float3(d.x, max(d.y, 0.0) * 0.5 + 0.03, d.z)));
    c = lerp(c, hz, f);
    float mu = max(dot(d, CS_SUN), 0.0);
#if defined(CS_NOVAK)
    return c + lerp(float3(0.55, 0.55, 0.52), float3(0.18, 0.17, 0.36), CS_NIGHT) * (pow(mu, 6.0) * 0.22 + pow(mu, 40.0) * 0.35) * (1.0 - exp(-dist / 160.0)) * (1.0 - 0.6 * CS_DUSK);
#endif
    c += lerp(float3(1.0, 0.45, 0.16), float3(0.20, 0.26, 0.42), CS_NIGHT) * (pow(mu, 6.0) * 0.36 + pow(mu, 40.0) * 0.5) * (1.0 - exp(-dist / 160.0)) * (1.0 - 0.6 * CS_DUSK);
    return c;
}

// Vanilla distance-dither fade (RenderDragon's Actor/RenderChunk alpha-test code): true = discard this pixel.
float bfDitherHash(float2 q) { return frac(q.x * 0.5 + q.y * q.y * 0.75); }
bool bfDitherKill(float3 worldPos, float3 cam, float3 fwd, float4 clip, float4 p2, float4 p) {
    float3 n    = -normalize(fwd);
    float  f    = saturate((dot(n, worldPos - cam) - p2.x) / (p2.y - p2.x));
    float  fade = f * f * (3.0 - 2.0 * f);
    float2 sp   = (clip.xy / clip.w) * 0.5 + 0.5;
    float2 px   = floor(sp * p.xy / p2.z) * p2.z;
    float  t    = (bfDitherHash(floor(px * 0.25)) * 0.25 + bfDitherHash(floor(px * 0.5))) * 0.25 + 0.0078125
                + bfDitherHash(floor(px));
    return fade <= t;
}
