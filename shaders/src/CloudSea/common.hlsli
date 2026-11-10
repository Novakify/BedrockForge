// Cloud Sea by Novak - World FX block lighting for the Cloud Sea sky (Renderer Foundation F6 > World FX > Cloud Sea).
// Same light as the Cloud Sea lab (tools/WorldFx/lab/cloudsea.glsl): a low golden sun in the north, violet sky light
// in the shade, a rose bounce off the cloud sea from below, glowing leaves, and warm air toward the sun.
// Maths runs in linear light: textures are decoded, lit, then encoded back for the game's target.

static const float3 CS_SUN = float3(0.17605, 0.11247, -0.97794);

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
    c += float3(1.05, 0.50, 0.10) * exp(-ang * 6.0) * exp(-e * 8.0) * 0.75;
    return c;
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
    m.bump = 0.35; m.detail = 0.85; m.spec = 0.03; m.rough = 20.0; m.refl = 0.0; m.metal = 0.0;
    m.wrap = 0.0; m.velvet = 0.0; m.sparkle = 0.0; m.sat = 1.0; m.violet = 0.0;
    switch (id) {
    case MAT_WOOL:       m.bump = 0.18; m.detail = 0.62; m.spec = 0.0;  m.wrap = 0.45; m.velvet = 0.32; m.sat = 1.06; break;
    case MAT_CONCRETE:   m.bump = 0.0;  m.detail = 0.45; m.spec = 0.10; m.rough = 36.0; m.refl = 0.06; m.sat = 1.12; break;
    case MAT_POWDER:     m.bump = 0.30; m.detail = 0.95; m.spec = 0.0;  m.wrap = 0.12; m.sparkle = 0.10; break;
    case MAT_PLANKS:     m.bump = 0.55; m.detail = 1.0;  m.spec = 0.05; m.rough = 18.0; break;
    case MAT_LOG:        m.bump = 0.85; m.detail = 1.0;  m.spec = 0.02; break;
    case MAT_STONE:      m.bump = 0.95; m.detail = 1.0;  m.spec = 0.04; m.rough = 14.0; break;
    case MAT_CUTSTONE:   m.bump = 0.70; m.detail = 0.95; m.spec = 0.07; m.rough = 30.0; m.refl = 0.03; break;
    case MAT_ENDSTONE:   m.bump = 0.80; m.detail = 1.0;  m.spec = 0.04; break;
    case MAT_OBSIDIAN:   m.bump = 0.30; m.detail = 1.0;  m.spec = 0.55; m.rough = 110.0; m.refl = 0.40; m.sparkle = 0.35; m.violet = 1.0; break;
    case MAT_TERRACOTTA: m.bump = 0.28; m.detail = 0.75; m.spec = 0.02; m.sat = 1.05; break;
    case MAT_GLAZED:     m.bump = 0.22; m.detail = 1.0;  m.spec = 0.45; m.rough = 140.0; m.refl = 0.24; break;
    case MAT_METAL:      m.bump = 0.35; m.detail = 0.9;  m.spec = 0.85; m.rough = 60.0; m.refl = 0.60; m.metal = 1.0; break;
    case MAT_GEM:        m.bump = 0.40; m.detail = 1.0;  m.spec = 0.60; m.rough = 90.0; m.refl = 0.30; m.sparkle = 1.0; m.sat = 1.08; break;
    case MAT_ORE:        m.bump = 0.90; m.detail = 1.0;  m.spec = 0.04; m.rough = 14.0; m.sparkle = 0.8; break;
    case MAT_SAND:       m.bump = 0.38; m.detail = 0.9;  m.spec = 0.02; m.sparkle = 0.18; break;
    case MAT_SOIL:       m.bump = 0.60; m.detail = 1.0;  m.spec = 0.0;  break;
    case MAT_BRICKS:     m.bump = 0.90; m.detail = 1.0;  m.spec = 0.04; m.rough = 16.0; break;
    case MAT_QUARTZ:     m.bump = 0.25; m.detail = 0.8;  m.spec = 0.32; m.rough = 70.0; m.refl = 0.14; break;
    case MAT_SNOW:       m.bump = 0.25; m.detail = 0.7;  m.spec = 0.10; m.rough = 24.0; m.wrap = 0.30; m.sparkle = 0.6; break;
    }
    return m;
}
float csHash(float2 p) { p = frac(p * float2(0.1031, 0.1030)); p += dot(p, p.yx + 33.33); return frac((p.x + p.y) * p.x); }
float3 csHash3(float2 p) { return float3(csHash(p), csHash(p + 17.13), csHash(p + 41.71)); }

// what a reflective surface sees: the Cloud Sea sky above, the lit cloud tops below
float3 csReflect(float3 r) {
    if (r.y >= 0.0) return csSky(r);
    float3 sea = lerp(float3(0.62, 0.34, 0.36), float3(0.95, 0.42, 0.18), pow(saturate(dot(normalize(float3(r.x, 0.0, r.z)), CS_SUN) * 0.5 + 0.5), 3.0));
    return lerp(csSky(normalize(float3(r.x, 0.02, r.z))), sea, smoothstep(0.0, -0.12, r.y));
}

// alb linear (corner shade already in it), n = shading normal (relief), ng = face normal, sunVis = open sky toward
// the sun, sky = sky light 0..1, leaf = foliage (light passes through), V = toward the eye, cav = texture cavity 0..1,
// glintN = per-texel tilted normal (sparkle), fade = 1 near, 0 where texels get smaller than pixels
float3 csLight(float3 alb, float3 n, float3 ng, float sunVis, float sky, float leaf, float3 V, CsMat m, float cav, float3 glintN, float fade) {
    float green = saturate((alb.g - max(alb.r, alb.b)) * 5.0);   // pure greens refuse to sit in a sunset: toward olive
    alb = lerp(alb, csLuma(alb) * float3(1.22, 0.98, 0.42), green * 0.42);
    alb = max(lerp(csLuma(alb).xxx, alb, m.sat), 0.0);
    float3 sunC = float3(1.00, 0.72, 0.50) * 1.85;              // soft gold: lit faces glow, they don't turn into orange paint
    float  ndlRaw = dot(n, CS_SUN);
    float  ndl = saturate((ndlRaw + m.wrap) / (1.0 + m.wrap)) * step(-0.02, dot(ng, CS_SUN) + m.wrap); // relief never lights a face turned away
    float3 amb  = lerp(float3(0.255, 0.140, 0.150), float3(0.200, 0.190, 0.330), smoothstep(-1.0, 1.0, n.y));
    float  toSun = dot(n.xz, normalize(CS_SUN.xz));
    float  side  = 1.0 - abs(n.y);
    amb += float3(0.40, 0.165, 0.070) * max(toSun, 0.0) * side;
    amb += float3(0.105, 0.085, 0.150) * max(-toSun, 0.0) * side;
    amb *= lerp(0.08, 1.0, sky * sky);                             // caves and covered spots lose the sky
    amb *= lerp(1.0, cav, m.bump * 0.55 * fade);                   // texture cavities hold a little shade
    float3 dir = sunC * (ndl + 0.035 * max(n.y, 0.0)) * sunVis;
    if (leaf > 0.5) {
        float mu    = max(dot(-V, CS_SUN), 0.0);
        float trans = pow(mu, 4.0) * 0.9 + 0.12;
        float wrap  = max(dot(n, CS_SUN) * 0.5 + 0.5, 0.0);
        dir = (sunC * wrap * 0.55 + float3(1.15, 0.80, 0.32) * 2.1 * trans) * sunVis;
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
        float3 rc = csReflect(R) * lerp(float3(1.0, 1.0, 1.0), sqrt(alb / max(csMax3(alb), 1e-3)), m.metal);
        rc *= lerp(1.0, 0.45 + 1.1 * smoothstep(-0.05, 0.25, R.y) * (1.0 - 0.5 * smoothstep(0.25, 0.8, R.y)), m.metal); // contrast: dark below the horizon line, bright band, dimmer zenith
        c = lerp(c, rc * lerp(0.35, 1.0, sky), saturate(F * m.refl * 1.6)) + rc * m.metal * m.refl * 0.55 * sky;
    }
    // sparkle: single texels catch the sun as you move (texel-locked, so it never crawls or flickers on its own)
    if (m.sparkle > 0.0) {
        float g = pow(saturate(dot(reflect(-V, glintN), CS_SUN)), 260.0);
        c += float3(1.6, 1.25, 0.95) * g * m.sparkle * sunVis * fade * 3.0;
    }
    // obsidian: a cold violet sheen at grazing angles
    c += float3(0.22, 0.10, 0.42) * fres * 0.5 * m.violet * lerp(0.3, 1.0, sky);
    // keep strongly lit warm colours from going neon: past ~70% saturation, ease back toward the luma
    float mx = csMax3(c), mn = min(c.r, min(c.g, c.b));
    float sat = mx > 1e-4 ? (mx - mn) / mx : 0.0;
    return lerp(csLuma(c).xxx, c, 1.0 - 0.28 * smoothstep(0.62, 0.95, sat) * (1.0 - m.metal));
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
    c += float3(1.0, 0.45, 0.16) * (pow(mu, 6.0) * 0.36 + pow(mu, 40.0) * 0.5) * (1.0 - exp(-dist / 160.0));
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
