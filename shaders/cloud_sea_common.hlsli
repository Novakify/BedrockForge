// Cloud Sea - shared sky / cloud-sea maths (sky pass: cloud_sea.hlsl, world pass: worldfx_pass.hlsl).
// Define CS_NOISE_REG / CS_PUFF_REG (texture registers) before including.
Texture2D    gNoise : register(CS_NOISE_REG); // 256x256 RGBA8 value noise (4 independent channels)
Texture2D    gPuff  : register(CS_PUFF_REG);  // 1024x1024 R16 cloud-top height with mips (domes riding on domes)
SamplerState gSmp   : register(s0); // linear, wrap

static float3 SUN = float3(0.17605, 0.11247, -0.97794); // the key light: low sun ~6.5 degrees up (north); the moon at night
static const float  SEA_AMP = 24.0;
static float3 CAM;
static float  TIME, SEA_Y;
static float2 MAP_C;
// ---- living match sky (Bedrock Forge MatchFx): phase 0 golden hour -> 1 dusk (first bed down) -> 2 night (final fight)
static float  PHASE = 0.0, DUSK = 0.0, NIGHT = 0.0;
static float3 SUNDISC = float3(0.17605, 0.11247, -0.97794);
static float  SUNUP = 1.0;  // the sun disc and its warm glow (gone once it has set)
static float  NG_NEON = 0.0;   // Novak: the match tightening (teams out) -> the neon night; 1 = the finale
static float  NG_CLOUDS = 1.0; // Novak: how much of the pearl altocumulus field (the F6 "clouds" amount)
static float  QUALITY = 1.0; // 0..1: Renderer Foundation lowers it to hold the frame rate
// ---- which atmosphere: Cloud Sea (default) or Novak Glass. NG_ATMOS 1 = always Novak Glass (the World FX pass built
//      for it), 2 = chosen at run time (ATMOS; the cheap sky pass), 0 = Cloud Sea only (no Novak Glass code in the build)
#ifndef NG_ATMOS
#define NG_ATMOS 0
#endif
static float  ATMOS = 0.0;
#if NG_ATMOS == 1
#define NG_IS (true)
#elif NG_ATMOS == 2
#define NG_IS (ATMOS > 0.5)
#else
#define NG_IS (false)
#endif
static float  LIGHTK = 1.0; // strength of the key light on the world: sun -> nothing at deep dusk -> moonlight
static const float2 SUN_AZ = float2(0.17717, -0.98418);  // the sun's compass direction (xz)
static const float2 MOON_AZ = float2(-0.85671, 0.51580); // the moon rises on the other side, over the south-west
float3 dirAzEl(float2 az, float el) { return float3(az.x * cos(el), sin(el), az.y * cos(el)); }
void csSetPhase(float k) {
    PHASE = k;
    DUSK  = smoothstep(0.0, 1.0, k);
    NIGHT = smoothstep(1.15, 1.9, k);
    float  elDay = NG_IS ? 26.0 : 6.5;                                // Novak Glass: a higher, cleaner sun (its whole halo fits in the sky)
    float3 disc = dirAzEl(SUN_AZ, radians(lerp(elDay, -4.5, DUSK)));   // where the sun disc is (it sets)
    float3 sun  = dirAzEl(SUN_AZ, radians(lerp(elDay, 1.5, DUSK)));    // the light: the afterglow still comes from there
    float3 moon = dirAzEl(MOON_AZ, radians(31.0));
    SUN    = normalize(lerp(sun, moon, smoothstep(1.3, 1.6, k)));     // swaps while it is darkest: no shadow jump to see
    SUNDISC = disc;
    SUNUP  = smoothstep(-0.075, 0.03, disc.y) * (1.0 - smoothstep(1.0, 1.3, k));
    LIGHTK = lerp(lerp(1.0, 0.40, smoothstep(0.25, 1.0, k)), 0.62, NIGHT) * (1.0 - 0.75 * smoothstep(1.1, 1.45, k) * (1.0 - smoothstep(1.45, 1.8, k)));
}

float3 csGrade(float3 c, float3 rd); // below (the sea uses it)
float2 rot(float2 p) { return float2(0.8 * p.x - 0.6 * p.y, 0.6 * p.x + 0.8 * p.y); }
float vnoise(float2 p) {
    float2 i = floor(p), f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return gNoise.SampleLevel(gSmp, (i + f + 0.5) / 256.0, 0.0).r;
}
float vnoiseG(float2 p) {
    float2 i = floor(p), f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return gNoise.SampleLevel(gSmp, (i + f + 0.5) / 256.0, 0.0).g;
}
float fbm(float2 p, int oct) {
    float s = 0.0, a = 0.5;
    [loop] for (int i = 0; i < oct; i++) { s += a * vnoise(p); p = rot(p) * 2.03 + 17.7; a *= 0.5; }
    return s;
}

// ---------------------------------------------------------------- sky gradient
float3 ngSkyBase(float3 d);
float3 skyBase(float3 d) {
    if (NG_IS) return ngSkyBase(d);
    float  e = max(d.y, 0.0);
    float  mu = dot(normalize(float3(d.x, max(d.y, 0.0), d.z)), SUN);
    float  sunSide = mu * 0.5 + 0.5;
    float  towardSun = pow(sunSide, 5.0);
    float3 zen = float3(0.036, 0.042, 0.135);
    float3 body = lerp(float3(0.085, 0.080, 0.215), float3(0.235, 0.120, 0.235), pow(sunSide, 1.6));
    float3 hot = float3(1.02, 0.300, 0.040);
    float3 warm = float3(0.62, 0.185, 0.085);
    float3 belt = float3(0.290, 0.150, 0.225);
    float3 shadow = float3(0.250, 0.170, 0.245);
    float3 hor = lerp(lerp(shadow, belt, smoothstep(0.0, 0.08, e)), lerp(warm, hot, towardSun), smoothstep(0.30, 0.92, sunSide));
    float  kb = lerp(0.10, 0.24, towardSun);
    float3 c = lerp(hor, body, smoothstep(0.0, kb, e));
    c = lerp(c, zen, smoothstep(0.06, 0.62, e));
    float ang = acos(clamp(dot(d, SUN), -1.0, 1.0));
    c += float3(1.05, 0.50, 0.10) * exp(-ang * 6.0) * exp(-e * 8.0) * 0.75;
    return c;
}

// ---------------------------------------------------------------- high clouds (seen from below, lit from below)
float hcDens(float2 p, int oct) {
    float  cover = fbm(p * 0.30 + 3.1, 4);
    float2 w = p * float2(3.4, 4.6);
    w += (float2(vnoiseG(p * 1.3), vnoiseG(p * 1.3 + 31.0)) - 0.5) * 1.6;
    return cover * 1.05 + fbm(w, oct) * 0.40;
}
float4 highClouds(float3 d, float mu) {
    if (d.y < 0.012) return float4(0.0, 0.0, 0.0, 0.0);
    float2 sunXZ = normalize(SUN.xz);
    float2 q = CAM.xz + d.xz * ((900.0 - CAM.y) / d.y);
    float  fade = smoothstep(0.012, 0.085, d.y);
    float2 p = q / 520.0 + TIME * float2(0.0030, 0.0011);
    float  dn = hcDens(p, QUALITY > 0.5 ? 5 : 3);
    float  fine = fbm(p * 21.0, 2);
    float  near = smoothstep(0.05, 0.30, d.y);
    float  dens = smoothstep(0.765, 0.830, dn - (fine - 0.5) * 0.10 * near);
    float  tau = smoothstep(0.765, 0.965, dn);
    float  shadeS = QUALITY > 0.3 ? smoothstep(0.765, 0.93, hcDens(p + sunXZ * 0.040, 3)) : 0.5;
    float2 along = float2(dot(q, sunXZ), dot(q, float2(-sunXZ.y, sunXZ.x)));
    float2 pc = float2(along.x / 3400.0, along.y / 760.0) + TIME * float2(0.0015, 0.0);
    float  wq = fbm(pc * 1.7 + 9.0, 3);
    pc += (float2(wq, 0.5 + 0.5 * sin(wq * 9.0 + 1.3)) - 0.5) * 0.9;
    float cir = smoothstep(0.46, 0.80, fbm(pc * float2(1.0, 2.2), 4));
    cir *= cir * smoothstep(0.40, 0.66, fbm(q / 2300.0 + 40.0, 3));

    float  sunSide = mu * 0.5 + 0.5;
    float3 lit = lerp(float3(0.46, 0.130, 0.170), float3(1.10, 0.300, 0.050), pow(sunSide, 2.0));
    lit = lerp(lit, float3(1.55, 0.74, 0.20), pow(max(mu, 0.0), 9.0));
    float3 shade = lerp(float3(0.062, 0.052, 0.120), float3(0.120, 0.070, 0.135), sunSide);
    float  ripple = (vnoise(rot(p) * float2(9.0, 15.0)) - 0.5) * 0.75;
    float  back = exp(-2.6 * tau) * (1.0 - 0.65 * shadeS);
    float  front = 0.92 - 0.46 * tau - 0.20 * shadeS;
    float  litF = saturate(lerp(front, back, smoothstep(0.25, 0.80, sunSide)) * (1.0 + 0.7 * ripple * near));
    float3 col = lerp(shade, lit, litF);
    float  a = dens * (0.50 + 0.50 * smoothstep(0.0, 0.5, tau)) * fade;
    float3 cirCol = lerp(float3(0.42, 0.130, 0.20), float3(1.0, 0.36, 0.10), pow(sunSide, 3.0));
    float  ac = cir * 0.34 * fade * (1.0 - a);
    return float4(col * a + cirCol * ac, a + ac);
}

// ---------------------------------------------------------------- the cloud sea (a height field below the map)
float seaH(float2 w, float lod) {
    float2 p = (w + TIME * float2(0.9, 0.35)) / 300.0;
    float  big = gPuff.SampleLevel(gSmp, rot(p) * 0.31 + 0.37, max(lod - 1.7, 0.0)).r;
    float  h = gPuff.SampleLevel(gSmp, p, lod).r;
    float  far = smoothstep(240.0, 520.0, length(w - MAP_C));
    float  base = h * (0.42 + 0.55 * big) + big * 0.55;
    [branch] if (far <= 0.0) return base;                            // no towers near the map: skip their noise
    float  tw = smoothstep(0.56, 0.78, vnoise(w / 760.0 + 2.7)) * far;
    return base + tw * (1.6 + 3.6 * big + 1.5 * h);
}
float seaY(float2 w, float lod) { return SEA_Y + SEA_AMP * seaH(w, lod); }

// conservative top of the cloud sea over the straight segment a->b. Towers only grow 240+ blocks from MAP_C; inside
// that disk the sea never rises above SEA_AMP * 1.52 (base max), and a segment with both ends in the disk stays in it.
static const float CS_DISK = 232.0;
float seaTopAlong(float2 a, float2 b) {
    bool inDisk = length(a - MAP_C) < CS_DISK && length(b - MAP_C) < CS_DISK;
    return SEA_Y + SEA_AMP * (inDisk ? 1.56 : 7.6);
}

float4 cloudSea(float3 ro, float3 rd, float tLimit, out float tHit) {
    tHit = -1.0;
    float yTop = SEA_Y + SEA_AMP * 7.6;
    if (rd.y >= 0.0 && ro.y > yTop) return float4(0.0, 0.0, 0.0, 0.0);
    // looking up: only the tall cloud towers out past the islands can rise above your eye line - march just the rays
    // that could reach one (this used to stop at 0.02 for everything, which sliced the tops off the towers in a
    // perfectly straight line round the horizon)
    if (rd.y > 0.02 && rd.y > (yTop - ro.y) / 180.0) return float4(0.0, 0.0, 0.0, 0.0);
    float t = 0.0;
    if (ro.y > yTop) t = (ro.y - yTop) / -rd.y;
    // inside the tower-free disk the sea is low: skip straight down to its real top (most of the screen from a map)
    float yNear = SEA_Y + SEA_AMP * 1.56;
    if (rd.y < 0.0 && ro.y > yNear) {
        float  tN = (ro.y - yNear) / -rd.y;
        float3 pN = ro + rd * tN;
        if (seaTopAlong(ro.xz, pN.xz) < yNear + 0.5) t = max(t, tN);
    }
    if (t > tLimit) return float4(0.0, 0.0, 0.0, 0.0);
    bool  hit = false;
    float lod = 0.0, tPrev = t;
    int steps = int(lerp(22.0, 44.0, QUALITY));
    [loop] for (int i = 0; i < steps; i++) {
        float3 p = ro + rd * t;
        lod = max(log2(t * 0.011), 0.0);
        float dh = p.y - seaY(p.xz, lod);
        if (dh < 0.004 * t + 0.03) { hit = true; break; }
        tPrev = t;
        t += max(dh * 0.62, 0.016 * t + 0.45);
        if (t > tLimit || (rd.y > 0.0 && p.y > yTop)) break;
    }
    if (!hit) return float4(0.0, 0.0, 0.0, 0.0);
    [unroll] for (int k = 0; k < 5; k++) {
        float  tm = 0.5 * (tPrev + t);
        float3 pm = ro + rd * tm;
        if (pm.y - seaY(pm.xz, lod) < 0.0) t = tm; else tPrev = tm;
    }
    tHit = t;
    float3 p = ro + rd * t;
    float  e = 2.2 + t * 0.006;
    float  h0 = seaY(p.xz, lod);
    float  nl = lod + 1.2;
    float  hc0 = seaY(p.xz, nl);
    float3 n = normalize(float3(hc0 - seaY(p.xz + float2(e, 0.0), nl), e, hc0 - seaY(p.xz + float2(0.0, e), nl)));
    float2 l2 = normalize(SUN.xz);
    float  slope = SUN.y / length(SUN.xz);
    float  sh = 1.0;
    int nShadow = QUALITY > 0.5 ? 3 : (QUALITY > 0.15 ? 1 : 0);
    [loop] for (int j = 0; j < nShadow; j++) {
        float s = 9.0 * pow(3.4, float(j));
        float over = seaY(p.xz + l2 * s, lod + 1.2 + float(j) * 0.9) - (h0 + slope * s);
        sh *= 1.0 - smoothstep(-0.05 * s, 0.20 * s, over) * 0.80;
    }
    float ndl = dot(n, SUN);
    float near = exp(-t / 260.0);
    float grain = 0.75;
    [branch] if (near > 0.02 && QUALITY > 0.3) grain = lerp(0.75, fbm(p.xz / 5.0 + 3.0, 3) + 0.5 * vnoiseG(p.xz / 1.3), near); // far away the grain is too small to see
    float cav = saturate((h0 - seaY(p.xz, lod + 3.4)) / (SEA_AMP * 0.13) + 0.55);
    float lit = sh * (0.30 + 0.70 * smoothstep(-0.35, 0.55, ndl)) * (0.72 + 0.38 * grain) * (0.34 + 0.66 * cav);
    float crease = smoothstep(0.05, 0.85, (h0 - SEA_Y) / SEA_AMP);
    float mu = dot(rd, SUN);
    float3 amb = lerp(float3(0.020, 0.023, 0.070), float3(0.066, 0.062, 0.155), crease) * (0.60 + 0.40 * n.y) * (0.85 + 0.2 * grain);
    amb += float3(0.22, 0.075, 0.040) * max(dot(normalize(n.xz + 1e-4), l2), 0.0) * (1.0 - n.y) * 0.5 * crease;
    float  phase = lerp(0.60, 1.0, smoothstep(-1.0, 0.35, mu));
    float3 direct = lerp(float3(0.95, 0.36, 0.20), float3(1.00, 0.335, 0.105), smoothstep(-1.0, 0.35, mu)) * 1.02 * lit * phase;
    // the sun gone: tops fall into shade, only the slopes facing the afterglow keep a pink edge; moonlight after
    float  dk = DUSK * (1.0 - NIGHT);
    direct *= lerp(1.0, LIGHTK, DUSK) * lerp(float3(1, 1, 1), float3(0.95, 0.80, 1.55), dk); // rose afterglow light
    amb *= lerp(float3(1, 1, 1), float3(1.25, 1.15, 1.55), dk);
    float  crest = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);
    float3 through = float3(1.5, 0.62, 0.18) * crest * pow(max(mu, 0.0), 5.0) * sh * 0.9 * (SUNUP + NIGHT * 0.5);
    float3 col = amb + direct + through;
    float  f = saturate((1.0 - exp(-t / 1000.0)) / (1.0 - exp(-3.4)));
    float3 hz = skyBase(normalize(float3(rd.x, max(rd.y, 0.0) + 0.030, rd.z)));
    col = lerp(col, hz, f * f * (3.0 - 2.0 * f));
    return float4(csGrade(col, rd), 1.0);
}

float3 toDisplay(float3 c) {
    c = max(c, 0.0);
    float3 k = max(c - 0.55, 0.0);
    c = min(c, 0.55) + k / (1.0 + k * 1.9);
    return pow(c, 1.0 / 2.2);
}
float hash12(float2 p) { p = frac(p * float2(0.1031, 0.1030)); p += dot(p, p.yx + 33.33); return frac((p.x + p.y) * p.x); }


// the sky along one view ray (linear colour): gradient, sun, high clouds, and - when withSea - the cloud sea.
// The sky pass draws it without the sea (it runs under the whole screen, before the terrain exists); the World FX pass
// adds the sea only on pixels that are still sky after the world is drawn (csSeaPixel), so terrain never pays for it.
float3 csGlow(float ang, float hcA) {
    if (NG_IS) return float3(0.0, 0.0, 0.0);   // Novak Glass has its own light round the sun (the halo)
    float3 warm = float3(1.0, 0.47, 0.17) * 0.42 * exp(-ang * 3.4) * lerp(1.0, 0.45, DUSK) * (1.0 - NIGHT);
    float3 cool = float3(0.55, 0.68, 1.0) * 0.050 * exp(-ang * 4.0) * NIGHT;   // moon haze
    return (warm + cool) * (1.0 - 0.5 * hcA);
}

// ---- the light leaving: dusk keeps a warm afterglow under where the sun went, the rest sinks to violet and indigo;
//      night turns everything to moonlight (silver tops, deep blue shade). One grade for sky, clouds and sea.
float3 csGrade(float3 c, float3 rd) {
    if (NG_IS) return c;                        // its sky is already written per phase
    float  dn = DUSK * (1.0 - NIGHT);
    [branch] if (dn > 0.001) {
        float  toward = pow(saturate(dot(normalize(rd.xz + 1e-4), SUN_AZ) * 0.5 + 0.5), 6.0) * exp(-max(rd.y, 0.0) * 5.0);
        float3 dk = c * lerp(float3(0.42, 0.34, 0.86) * 0.70, float3(1.05, 0.52, 0.48), toward * 0.85);
        c = lerp(c, dk, dn);
    }
    [branch] if (NIGHT > 0.001) {
        float  lum = dot(c, float3(0.2126, 0.7152, 0.0722));
        float  l2  = pow(lum / 0.45, 1.8) * 0.45;            // moonlight is contrasty: silver tops, deep shade
        float3 cn  = l2 * lerp(float3(0.10, 0.18, 0.62), float3(0.82, 0.92, 1.12), smoothstep(0.05, 0.50, l2)) * 0.80;
        cn += float3(0.003, 0.005, 0.014);
        c = lerp(c, cn, NIGHT);
    }
    return c;
}
float hash13(float3 p) { p = frac(p * 0.1031); p += dot(p, p.zyx + 31.32); return frac((p.x + p.y) * p.z); }
// stars: one possible star per cell of a 3D grid on the unit sphere; a faint band of them (and its glow) across the sky
float3 csStars(float3 rd) {
    float3 band = normalize(float3(0.35, 0.42, 0.84));
    float  inBand = exp(-pow(dot(rd, band) / 0.22, 2.0));
    float3 acc = float3(0.0, 0.0, 0.0);
    [unroll] for (int layer = 0; layer < 2; layer++) {
        float  sc = layer == 0 ? 150.0 : 330.0;
        float3 g = rd * sc, cell = floor(g);
        float  h = hash13(cell + 17.0 * float(layer));
        float  thr = layer == 0 ? 0.955 : lerp(0.985, 0.90, inBand);
        if (h > thr) {
            float3 sp = normalize(cell + 0.5 + (float3(hash13(cell + 3.1), hash13(cell + 7.7), hash13(cell + 11.3)) - 0.5) * 0.7);
            float  a = length(rd - sp) * sc;
            float  mag = pow((h - thr) / (1.0 - thr), 2.2) * (layer == 0 ? 2.6 : 0.9);
            float  tw = 0.70 + 0.30 * sin(TIME * (1.3 + 3.0 * h) + h * 61.0);
            float3 tint = lerp(float3(0.75, 0.84, 1.10), float3(1.10, 0.92, 0.78), frac(h * 37.0));
            acc += tint * mag * tw * exp(-a * a * 28.0);
        }
    }
    float glow = inBand * (0.55 + 0.45 * fbm(rd.xz / (rd.y + 1.2) * 6.0 + 4.0, 3));
    acc += float3(0.020, 0.022, 0.040) * glow;
    return acc * smoothstep(0.0, 0.18, rd.y);
}
float3 csMoon(float3 rd) { // a small cool moon with a little surface, a halo
    float  ang = acos(clamp(dot(rd, SUN), -1.0, 1.0));
    float3 t1 = normalize(cross(SUN, float3(0, 1, 0))), t2 = cross(t1, SUN);
    float2 q = float2(dot(rd, t1), dot(rd, t2)) / 0.016;
    float  disc = smoothstep(1.0, 0.86, length(q));
    float  mare = fbm(q * 2.2 + 5.0, 3);
    float3 m = float3(1.05, 1.07, 1.12) * (0.95 + 0.5 * smoothstep(0.35, 0.7, mare)) * 1.7 * disc;
    return m + float3(0.55, 0.66, 1.0) * (exp(-ang * 26.0) * 0.16 + exp(-ang * 6.0) * 0.030);
}
// ================================================================ Novak Glass: the sky and the crystal void
// A clear, cold, high sky - no weather in it, only light: deep cobalt overhead, pale ice at the horizon. The low sun
// stands in a 22 degree ice halo with a sundog on each side (the sky's own glass). Dusk: violet over a rose band.
// Night: indigo to black-violet, stars, a moon ring. Under the map there is no cloud: a void that falls from the
// horizon's colour into black-violet, with sheets of glass adrift in it, their edges catching the light.
float3 ngSpecS(float t) { t = saturate(t); return saturate(float3(1.5 - abs(t * 4.0 - 1.0) * 1.2, 1.5 - abs(t * 4.0 - 2.0) * 1.2, 1.5 - abs(t * 4.0 - 3.2) * 1.2)); }
float3 ngSkyBase(float3 d) {
    float  e = max(d.y, 0.0);
    float  sunSide = dot(normalize(float3(d.x, max(d.y, 0.0), d.z)), SUN) * 0.5 + 0.5;
    // day: an aqua glow low down (the glass of the dome lit edge-on), a pearl haze round the sun, cerulean, then a
    // deep ultramarine zenith (seen in game: one even mid-blue read as the vanilla sky)
    float3 cD = lerp(lerp(float3(0.26, 0.52, 0.72), float3(0.80, 0.78, 0.74), pow(sunSide, 6.0)), float3(0.060, 0.230, 0.560), smoothstep(0.0, 0.09, e));
    cD = lerp(cD, float3(0.030, 0.036, 0.205), smoothstep(0.18, 0.90, e));
    float3 cK = lerp(lerp(float3(0.19, 0.115, 0.33), float3(1.00, 0.45, 0.36), pow(sunSide, 3.0)), float3(0.072, 0.040, 0.180), smoothstep(0.0, 0.15, e));
    cK = lerp(cK, float3(0.014, 0.012, 0.074), smoothstep(0.10, 0.70, e));
    float3 cN = lerp(float3(0.044, 0.027, 0.112), float3(0.015, 0.012, 0.062), smoothstep(0.0, 0.14, e));
    cN = lerp(cN, float3(0.0035, 0.0035, 0.018), smoothstep(0.08, 0.60, e));
    return lerp(lerp(cD, cK, DUSK), cN, NIGHT);
}
float3 ngHash3(float3 p) { return float3(hash13(p), hash13(p + 17.13), hash13(p + 41.71)); }
float3 ngFilm(float d) { return 0.5 + 0.5 * cos(6.2832 * (d * float3(1.0, 1.18, 1.42) + float3(0.0, 0.08, 0.20))); }   // thin-film colours
// ---- the dome: the sky is seen through a geodesic glass dome. Directions map stereographically onto a plane (panes
//      shrink toward the horizon like a real dome); a triangular lattice there = the panes. Each pane bends the view a
//      little its own way (the sun and its halo break up across them). The edges are NOT a wireframe: an edge only
//      catches the light in places - a few long glints, colour-split near the light, most of the lattice invisible.
struct NgDome { float3 rdF; float edge; float edgeC; float3 hcell; float lnM, lnC, along, lineId, fam; };
NgDome ngDome(float3 rd) {
    NgDome D;
    float  e = max(rd.y, 0.0);
    float2 p = rd.xz / (1.0 + max(rd.y, 0.02)) * 3.6;
    const float2 a0 = float2(1.0, 0.0), a1 = float2(0.5, 0.8660254), a2 = float2(-0.5, 0.8660254);
    float3 g = float3(dot(p, a0), dot(p, a1), dot(p, a2));
    float3 fr = frac(g), dl = min(fr, 1.0 - fr);
    D.hcell = ngHash3(floor(g) + 7.0);
    float2 jit = (D.hcell.xy - 0.5) * 0.030;
    D.rdF = normalize(rd + float3(jit.x, (D.hcell.z - 0.5) * 0.020, jit.y));
    float  px = length(fwidth(p)) + 1e-4;
    float  d = min(dl.x, min(dl.y, dl.z));
    float  fam = dl.x <= min(dl.y, dl.z) ? 0.0 : (dl.y <= dl.z ? 1.0 : 2.0);
    float  lineId = fam == 0.0 ? floor(g.x + 0.5) : (fam == 1.0 ? floor(g.y + 0.5) : floor(g.z + 0.5));
    float  along = fam == 0.0 ? dot(p, float2(-a0.y, a0.x)) : (fam == 1.0 ? dot(p, float2(-a1.y, a1.x)) : dot(p, float2(-a2.y, a2.x)));
    float  gl = smoothstep(lerp(0.70, 0.46, NG_NEON), lerp(0.97, 0.80, NG_NEON), vnoise(float2(along * 1.1 + lineId * 7.1, fam * 13.0 + TIME * 0.05)));
    float  fade = smoothstep(0.03, 0.22, e);
    D.lnM  = (1.0 - smoothstep(0.0, px * 1.2, d)) * fade;   // the whole lattice (the neon night lights it)
    D.lnC = (1.0 - smoothstep(0.0, px * 6.0, d)) * fade;
    D.edge  = D.lnM * gl;
    D.edgeC = (1.0 - smoothstep(0.0, px * 5.0, d)) * gl * fade;
    D.along = along; D.lineId = lineId; D.fam = fam;
    return D;
}
// circumzenithal arc: an upside-down rainbow round the zenith, ~46 degrees over a low sun
float3 ngCza(float3 rd, float3 Ld, float k) {
    float sunEl = asin(clamp(Ld.y, -1.0, 1.0));
    if (sunEl > 0.56 || sunEl < 0.0 || k <= 0.0) return float3(0.0, 0.0, 0.0);
    float colat = acos(clamp(rd.y, -1.0, 1.0));
    float x = (colat - (1.5708 - (sunEl + 0.80))) / 0.030;
    float wnd = smoothstep(0.25, 0.85, dot(normalize(rd.xz + 1e-4), normalize(Ld.xz)));
    return ngSpecS(saturate(0.5 + x * 0.42)) * exp(-x * x * 0.9) * wnd * 0.32 * k;
}
// thin iridescent cirrus: fibres (ridged noise: mares' tails, not soft blobs), white away from the sun, mother-of-pearl
// colours within ~25 degrees of it
// ---- pearl altocumulus: fields of small rounded cloudlets in rows (a mackerel sky) - the day sky's body. Each one
//      is lit from the sun's side (a short march toward the sun over its own density), lavender in its shade, and near
//      the sun its thin edges break into mother-of-pearl colour. (Seen in game: cirrus alone left most of the sky an
//      empty vanilla blue.)
float ngWor(float2 p) {
    float2 g = floor(p), f = frac(p); float d = 9.0;
    [unroll] for (int y = -1; y <= 1; y++) [unroll] for (int x = -1; x <= 1; x++) {
        float2 o = float2(x, y);
        float2 h = float2(hash12(g + o + 3.7), hash12(g + o + 11.3));
        float2 c = o + 0.5 + (h - 0.5) * 0.80 - f;
        d = min(d, dot(c, c));
    }
    return sqrt(d);
}
float ngAcField(float2 q, float cov) {
    float b  = ngWor(q * float2(2.9, 2.1)) * 0.80 + (1.0 - vnoise(q * float2(11.2, 8.2) + 3.1)) * 0.30;   // lumps, smaller ones on them
    float fr = fbm(q * 5.0, 3) * 0.8 + fbm(q * 14.0, 2) * 0.2;   // a frayed outline
    return saturate((cov * 2.1 - 0.80) - b * 0.90 - fr * 0.26 + 0.30);
}
// the same field, coarse (the big lumps only): enough for the light march (cost: seen 5.3 ms for the pass at 4K)
float ngAcFieldLo(float2 q, float cov) {
    return saturate((cov * 2.1 - 0.80) - (1.0 - vnoise(q * float2(4.9, 3.6))) * 0.68 - 0.10 + 0.30);   // one fetch, no cells
}
float4 ngAlto(float3 rd, float3 Ld, bool cheap) {
    float e = rd.y;
    [branch] if (e < 0.015) return float4(0, 0, 0, 0);
    float2 P = rd.xz / (e * 0.55 + 0.16) * 0.95 + TIME * float2(0.006, 0.002);   // big, clean, puffy (the Aero sky)   // (overhead a little denser: seen in game, huge soft blobs up there)
    float2 W = float2(0.92, 0.39);
    float2 q = float2(dot(P, W), dot(P, float2(-W.y, W.x)));
    q.y += 0.22 * sin(q.x * 1.3 + fbm(P * 0.5, 2) * 3.0);                       // the rows ripple
    float cov = fbm(P * 0.20 + float2(5.1, 1.3), 4);
    cov *= 0.85 + 0.30 * smoothstep(-0.6, 0.6, sin(q.y * 6.5 + fbm(P * 0.7, 2) * 4.0));   // banded into rows
    [branch] if (cov < 0.30) return float4(0, 0, 0, 0);                         // open sky: nothing to draw (cheap)
    float d = cheap ? ngAcFieldLo(q, cov) : ngAcField(q, cov);
    [branch] if (d <= 0.002) return float4(0, 0, 0, 0);
    float dSoft = smoothstep(0.0, 0.45, d);                // for the light
    d = smoothstep(0.0, 0.26, d);                          // the outline: crisp (seen in game: soft edges read as smeared)
    float2 sd = normalize(float2(dot(Ld.xz, W), dot(Ld.xz, float2(-W.y, W.x))) + 1e-4);
    float occ = 0.0;
    [branch] if (!cheap) {
        occ  = smoothstep(0.0, 0.45, ngAcFieldLo(q + sd * 0.030, cov)) * 0.85;
        occ += smoothstep(0.0, 0.45, ngAcFieldLo(q + sd * 0.070, cov)) * 0.55;
    } else occ = 0.6;
    float lit = saturate(exp(-occ * 1.25) * 1.25 * (0.75 + 0.5 * (1.0 - dSoft)));   // thin parts let the light through
    float ang = acos(clamp(dot(rd, Ld), -1.0, 1.0));
    float fwd = exp(-ang * 3.0);
    float edge = saturate(1.0 - dSoft * 1.6);
    // dusk: gold-salmon where the low sun reaches, violet in the shade (the sky's own colours, not grey)
    float3 cSh = lerp(float3(0.40, 0.45, 0.66), float3(0.30, 0.22, 0.42), DUSK);
    float3 cLt = lerp(float3(1.04, 1.00, 0.94), lerp(float3(0.95, 0.62, 0.66), float3(1.25, 0.70, 0.45), fwd), DUSK);
    cSh = lerp(cSh, float3(0.45, 0.50, 0.85), NIGHT);   // night: moonlit blue-grey (seen in game: the dusk colours turned muddy brown)
    cLt = lerp(cLt, float3(0.80, 0.86, 1.10), NIGHT);
    float3 c = lerp(cSh, cLt, lit);
    // toward the sun the clouds are backlit: thick bodies go grey, the thin edges glow silver (seen in game: lit white
    // cut-outs round the sun, with green-cyan fringes that read as cheap)
    float back = fwd * (1.0 - DUSK * 0.5);
    c *= lerp(1.0, 0.62 + 0.38 * edge, back);
    c += float3(1.0, 0.96, 0.88) * edge * fwd * 1.6;                            // silver lining
    float3 iri = ngFilm(0.45 + (1.0 - dSoft) * 0.9 + ang * 0.5);
    c = lerp(c, c * 0.65 + iri * 0.55, saturate(edge * smoothstep(0.40, 0.08, ang) * 0.8));   // mother-of-pearl, close in only
    float fade = smoothstep(0.015, 0.12, e) * (1.0 - 0.35 * smoothstep(0.6, 1.0, e));
    return float4(c, d * fade * 0.95);
}
// prism beams: a few broad shafts of light fanning out from the sun, each split into colour along its edges (the
// dome's glass works as a prism) - the signed-off "prism beams + clean sky" day. Faint, slow, in the open sky only.
float3 ngPrismBeams(float3 rd, float3 Ld) {
    float3 t1 = normalize(cross(Ld, float3(0, 1, 0))), t2 = cross(t1, Ld);
    float  a = atan2(dot(rd, t2), dot(rd, t1));
    float  dist = acos(clamp(dot(rd, Ld), -1.0, 1.0));
    float  fall = exp(-dist * 2.4) * (1.0 - exp(-dist * 9.0));     // not on the disc itself, gone ~60 degrees out
    [branch] if (fall < 0.01) return float3(0, 0, 0);
    float3 c;
    [unroll] for (int i = 0; i < 3; i++) {
        float sh = (float(i) - 1.0) * 0.020 * (0.4 + dist * 2.2);    // red / green / blue fan apart with distance
        float x  = a * 7.0 / 6.2832 + sh * 7.0 + TIME * 0.004;
        float v  = vnoise(float2(x, 3.7)) * 0.65 + vnoise(float2(x * 2.3, 9.1)) * 0.35;
        c[i] = smoothstep(0.62, 0.80, v);
    }
    return c * fall;
}
float4 ngCirrus(float3 rd, float3 Ld) {
    float e = max(rd.y, 0.0);
    if (e < 0.02) return float4(0.0, 0.0, 0.0, 0.0);
    float2 p = rd.xz / (e + 0.30) * 2.4 + TIME * float2(0.004, 0.0015);
    float2 q = float2(p.x * 0.55 + p.y * 0.83, p.y * 0.55 - p.x * 0.83);
    float m = fbm(q * 0.30 + 11.0, 3);
    [branch] if (m < 0.40) return float4(0.0, 0.0, 0.0, 0.0);   // no cirrus here: skip the fibres (cost)
    float2 w = float2(fbm(q * 0.8 + 1.7, 3), fbm(q * 0.8 + 8.3, 3));
    float2 qq = q * float2(1.3, 4.0) + w * 1.6 + 3.0;
    float  r = 0.0, amp = 0.55;
    [loop] for (int i = 0; i < (QUALITY > 0.3 ? 5 : 3); i++) { float v = 1.0 - abs(2.0 * vnoise(qq) - 1.0); r += amp * v * v; qq = mul(float2x2(1.6, 1.2, -1.2, 1.6), qq) * float2(1.0, 1.15); amp *= 0.5; }
    float dens = smoothstep(0.54, 0.80, r) * smoothstep(0.40, 0.62, m) * smoothstep(0.04, 0.22, e) * (1.0 - smoothstep(0.45, 0.90, e) * 0.85);   // overhead it streaked like motion blur
    float ang = acos(clamp(dot(rd, Ld), -1.0, 1.0));
    float3 c = lerp(float3(0.92, 0.95, 1.0), ngFilm(r * 2.2 + ang * 0.8), 0.75 * exp(-ang * 2.6));
    return float4(c, dens);
}
// caustics: light through the dome falling on a floor far under the void (a two-scale moving cell net)
float ngCaustic(float2 p) {
    float2 q = p; float t = TIME * 0.15; float v = 0.0;
    [unroll] for (int i = 0; i < 2; i++) {
        float2 g = floor(q), f = frac(q); float d1 = 9.0, d2 = 9.0;
        [unroll] for (int y = -1; y <= 1; y++) [unroll] for (int x = -1; x <= 1; x++) {
            float2 o = float2(x, y); float2 h = float2(hash12(g + o), hash12(g + o + 19.1));
            float2 r = o + 0.5 + 0.42 * sin(t + 6.2832 * h) - f; float d = dot(r, r);
            if (d < d1) { d2 = d1; d1 = d; } else if (d < d2) d2 = d;
        }
        v += pow(1.0 - smoothstep(0.0, 0.06, sqrt(d2) - sqrt(d1)), 3.0) * (i == 0 ? 1.0 : 0.5);
        q = q * 2.03 + 7.3;
    }
    return v;
}
float3 ngHalo(float3 rd, float3 Ld, float k, float dogs) {
    float  ang = acos(clamp(dot(rd, Ld), -1.0, 1.0));
    float  x = (ang - 0.3840) / 0.01745;                              // degrees outside the ring
    float  ring = x < 0.0 ? exp(-x * x * 2.2) : exp(-x * 0.55);
    float3 ringC = lerp(float3(1.00, 0.50, 0.30), float3(0.72, 0.84, 1.00), saturate(x * 0.5 + 0.25));
    float3 c = ringC * ring * 0.15;
    float3 t1 = normalize(cross(Ld, float3(0.0, 1.0, 0.0)));
    [unroll] for (int s = 0; s < 2; s++) {
        float  sg = s == 0 ? -1.0 : 1.0;
        float3 Sd = normalize(Ld * 0.9239 + t1 * (0.3827 * sg));       // 22.5 degrees to the side
        float  dh = dot(rd - Sd, t1) * sg;                             // > 0: further from the sun
        float  dv = rd.y - Sd.y;
        float  dog = exp(-dv * dv / 0.00045) * (dh < 0.0 ? exp(-dh * dh / 0.00030) : exp(-dh / 0.045)) * step(0.5, dot(rd, Sd));
        c += lerp(float3(1.10, 0.55, 0.30), float3(0.85, 0.93, 1.05), saturate(dh * 30.0 + 0.35)) * dog * 0.90 * dogs;
    }
    return c * k * smoothstep(-0.02, 0.05, rd.y);
}
// one layer of glass sheets adrift in the void: rgb = light added, a = how much of what is behind it is covered
float4 ngShards(float3 ro, float3 rd, float planeY, float cellS, float seed, float3 Ld) {
    float  t = (planeY - ro.y) / rd.y;
    if (t <= 0.0 || t > 2600.0) return float4(0.0, 0.0, 0.0, 0.0);
    float2 p = ro.xz + rd.xz * t;
    float2 cell = floor(p / cellS);
    float  h = hash12(cell + seed);
    if (h < 0.60) return float4(0.0, 0.0, 0.0, 0.0);
    float  h2 = hash12(cell * 1.7 + seed + 9.1), h3 = hash12(cell * 2.3 + seed + 4.7);
    float2 q = p - (cell + 0.5 + (float2(h2, h3) - 0.5) * 0.44) * cellS;
    float  a = h * 41.0 + TIME * 0.02 * (h2 - 0.5);
    float  ca = cos(a), sa = sin(a);
    q = float2(ca * q.x - sa * q.y, sa * q.x + ca * q.y);
    float  w = cellS * lerp(0.07, 0.13, h3), l = cellS * lerp(0.20, 0.34, h2);
    float  k = 1.0 - (abs(q.x) / w + abs(q.y) / l);                   // a long kite: 1 at the heart, 0 on its edge
    if (k <= 0.0) return float4(0.0, 0.0, 0.0, 0.0);
    // two facets meeting along the spine; each tilts its own way and holds a different part of the sky
    float  side = q.x < 0.0 ? -1.0 : 1.0;
    float3 n = normalize(float3((ca * side) * 0.55, 1.0, (-sa * side) * 0.55));
    float3 R = reflect(rd, n);
    float  F = 0.06 + 0.94 * pow(1.0 - saturate(dot(-rd, n)), 4.0);
    float3 refl = ngSkyBase(normalize(float3(R.x, abs(R.y) + 0.02, R.z)));
    float  glint = pow(saturate(dot(R, Ld)), 220.0);
    float  edge = smoothstep(0.13, 0.0, k) + smoothstep(0.045, 0.0, abs(q.x) / w) * 0.55;   // rim + spine
    float  far = exp(-t / 1100.0);
    float3 lineC = lerp(float3(1.0, 1.0, 1.0), ngSpecS(frac(q.y / l * 0.5 + h * 3.0)), 0.55) * lerp(0.55, 0.30, DUSK * (1.0 - NIGHT));
    lineC = lerp(lineC, lerp(float3(0.80, 0.72, 1.15), ngSpecS(frac(q.y / l * 0.5 + h * 3.0)), 0.30) * 0.60, NIGHT);   // night: the edges are the light
    float3 c = refl * (F * 1.2 + 0.10 + 0.10 * side) + float3(1.0, 0.97, 0.92) * glint * 1.6 * (1.0 - NIGHT) + lineC * edge;   // the two facets differ: it has a spine
    return float4(c * far, saturate(0.34 + 0.5 * F + edge * 0.6) * far);
}
// the void for a ray going down: from the horizon's colour into the deep, the sheets on four levels
// ======================================================================== the NEON GLASS CITY (Novak Glass)
// Below and around the islands: a futuristic glass city - prism-edged towers, stacked advertising, wet glass streets.
// By day a city of tinted glass mirroring the sky; at night Tokyo / Cyberpunk neon. (Designed in the sky lab with
// Fernando, Nov 2026; this is that code.)
static float GPX = 0.001;                                     // the view ray's angular size of one pixel (set per pixel)
float ngH12(float2 p) { float3 q = frac(((float3)(p.xyx)) * 0.1031); q += dot(q, q.yzx + 33.33); return frac((q.x + q.y) * q.z); }
static const float3 NC=float3(0.10,0.95,1.00), NM=float3(1.00,0.18,0.80), NV=float3(0.55,0.25,1.00);
float3 neonPal(float h){ h=frac(h); return h<0.45?lerp(NC,NV,h/0.45):(h<0.8?lerp(NV,NM,(h-0.45)/0.35):lerp(NM,NC,(h-0.8)/0.2)); }
// ---- the city at night. Studied from the references (Kabukicho / Shibuya streets, a cyberpunk skyline):
//  * the base is near BLACK-NAVY: the colour lives in the lights, so they read as bright
//  * buildings stand shoulder to shoulder and tower over you, layer behind layer - and the city runs on UNDER the
//    islands: you float over its roofs and streets
//  * every face carries advertising: stacked vertical boards, big panels, screens - each its own size and colour
//  * towers: dark glass with LED strips up their edges, bands of light, whole faces that are screens
//  * everything is glazed: a sheen over each face, edges that split light into colour, wet glass streets
float hb(float a, float b){ return ngH12(float2(a,b)); }
float3 signCol(float h){
  if(h<0.22) return float3(1.00,0.06,0.55);
  if(h<0.40) return float3(0.05,0.78,1.00);
  if(h<0.56) return float3(1.00,0.08,0.08);
  if(h<0.68) return float3(1.00,0.96,0.90);
  if(h<0.80) return float3(1.00,0.74,0.05);
  if(h<0.91) return float3(0.20,0.28,1.00);
  return float3(0.15,1.00,0.42);
}
float glyph(float2 uv, float seed){
  float2 gi=floor(uv), gf=frac(uv);
  if(gf.x<0.12||gf.x>0.88||gf.y<0.10||gf.y>0.90) return 0.;
  float2 q=(gf-float2(0.12,0.10))/float2(0.76,0.80);
  float2 b=floor(q*float2(7.,8.));
  float hs=ngH12(gi*7.3+seed);
  float row=step(0.50,ngH12(float2(b.y,hs*91.+gi.x*3.+gi.y*17.)));
  float colm=step(0.58,ngH12(float2(b.x+9.,hs*57.+gi.x*5.+gi.y*13.)));
  return max(row,colm)*step(0.30,ngH12(b+gi*13.+seed));
}
float3 cityHaze(float az){ return lerp(float3(0.85,0.12,0.60),float3(0.10,0.45,1.00),0.5+0.5*sin(az*1.3+2.0)); }
static float SEA_D = 175.0;                                      // the city's streets lie this far under the islands
// one advertisement: pn = 0..1 across the panel, sz = its size in metres. A lightbox behind glass (rounded, brightest
// at its heart, a highlight along the top), neon tubes on a dark board, or a picture ad (shapes, a line of script)
float3 signPanel(float2 pn, float2 sz, float hp, float pxm){
  float3 bg=signCol(frac(hp*7.31)), a2=signCol(frac(hp*7.31+0.31+0.3*frac(hp*3.)));
  float gw=min(sz.x,sz.y), gn=gw>9.0?2.0:1.0;
  float res=1.-smoothstep(0.5,1.5,pxm/(gw/gn/7.));
  float lum=lerp(0.85,1.20,frac(hp*3.7))*(0.93+0.07*sin(TIME*(2.+6.*frac(hp*13.))+hp*50.));
  float dE=min(min(pn.x,1.-pn.x)*sz.x,min(pn.y,1.-pn.y)*sz.y);
  float tube=lerp(1.0,smoothstep(0.0,1.1,dE),res);
  float kind=frac(hp*11.3);
  float3 ink=(bg.r+bg.g+bg.b>2.5)?(frac(hp*41.)<0.5?float3(0.90,0.04,0.08):((float3)(0.02))):((bg.g>0.7&&bg.b<0.2)||frac(hp*43.)<0.55?((float3)(0.015)):((float3)(1.5)));
  float3 c;
  if(kind<0.22){                                              // neon tubes on a dark board
    float g=lerp(0.20,glyph(pn*sz/gw*gn,hp*100.),res);
    c=float3(0.010,0.008,0.020)+bg*1.5*max(g,step(dE,0.45)*res);
  } else if(kind<0.50&&gw>5.0){                               // a picture ad
    float2 q=(pn-0.5)*sz/gw; float t=TIME*0.3+hp*20.;
    float shape=frac(hp*29.)<0.5?step(length(q-float2(0.,0.08)),0.30):step(0.5,frac((q.x+q.y)*2.2+t*0.2));
    c=lerp(bg,a2,shape)*lum;
    float strip=step(pn.y,0.20);                              // a line of script along the bottom
    float g=lerp(0.36,glyph(float2(pn.x*sz.x/(sz.y*0.20),pn.y/0.20),hp*61.),1.-smoothstep(0.5,1.5,pxm/(sz.y*0.20/7.)));
    c=lerp(c,lerp(((float3)(0.02)),((float3)(1.5)),g),strip);
    c=c*(0.55+0.45*tube);
  } else {                                                    // a lightbox with script
    float g=lerp(0.20,glyph(pn*sz/gw*gn,hp*100.),res);
    c=lerp(bg*lum,ink,g)*(0.55+0.45*tube);
  }
  return c+((float3)(1.0))*0.20*smoothstep(0.80,0.97,pn.y)*tube*res*step(0.22,kind);
}
// a building face. lx 0..1 across it, ym metres above the street, Hr its height, Wm its width; sd = its seed
float3 facade(float lx, float ym, float Hr, float Wm, float pxm, float az, float sd, float tall, float nearK){
  float h=hb(sd,1.), h2=hb(sd,2.), h4=hb(sd,4.), ht=hb(sd,5.);
  float yTop=Hr-ym;
  float3 face=float3(0.012,0.013,0.028)+cityHaze(az)*0.035*exp(-ym/120.0);
  float floorH=lerp(3.4,4.4,h4), cols=floor(Wm/lerp(2.8,4.5,h2));
  float2 wg=float2(lx*cols,ym/floorH); float2 wi=floor(wg), wf=frac(wg);
  float wRes=1.-smoothstep(0.35,1.0,max(pxm/floorH,pxm/(Wm/cols)));
  float flOn=step(lerp(0.40,0.80,hb(sd,7.)),hb(wi.y,sd*3.7));     // floors lit in bands; some buildings nearly dark
  float wr=hb(wi.x+wi.y*37.,sd+91.);
  float wOn=max(flOn*step(0.25,wr),step(0.93,wr))*lerp(0.35,1.0,hb(wi.x*3.1,wi.y+sd));   // and the odd light left on
  float wMask=smoothstep(0.10,0.18,wf.x)*smoothstep(0.90,0.82,wf.x)*smoothstep(0.14,0.24,wf.y)*smoothstep(0.86,0.76,wf.y);
  float win=lerp(0.13,wOn*wMask,wRes);
  bool office=h4>0.4;
  float3 wc=office?lerp(float3(0.78,0.90,1.08),float3(0.45,0.85,1.10),step(0.7,h)):float3(1.08,0.70,0.38);
  wc=lerp(wc,float3(1.1,0.55,0.75),step(0.90,hb(wi.x*1.7,wi.y*2.3+sd))*wRes);   // a window lit pink by a screen inside
  float stair=step(abs(lx-lerp(0.2,0.8,h2)),0.5/cols)*step(0.5,h);            // a lit stair core up the face
  float3 c;
  if(ht<0.42&&tall<0.5){
    // a sign building: columns of stacked boards
    c=face+wc*win*0.30;
    float ncol=floor(lerp(2.,4.99,h2));
    float cx=lx*ncol, cix=floor(cx), cfx=frac(cx), colW=Wm/ncol;
    float hc=hb(cix+sd*3.1,41.);
    float ph=lerp(6.,17.,hc*hc);
    float yy=ym+hc*50.; float pi=floor(yy/ph), pf=frac(yy/ph);
    float hp=hb(pi+cix*17.,sd*7.+5.);
    float mx=0.07+0.09*frac(hp*5.3), my=0.5/ph;
    if(cfx>mx&&cfx<1.-mx&&pf>my&&pf<1.-my&&hp>0.12+0.62*smoothstep(35.,130.,ym)&&yTop>3.0)
      c=signPanel(float2((cfx-mx)/(1.-2.*mx),(pf-my)/(1.-2.*my)),float2(colW*(1.-2.*mx),ph-1.0),hp,pxm);
  } else if(ht<0.74||tall<0.25){
    // offices and flats, with big advertisements hung on them
    c=face+wc*win*(office?0.55:0.38)+wc*0.4*stair*wRes;
    c+=signCol(frac(h*5.7))*1.3*(1.-smoothstep(0.,max(pxm,0.6),abs(yTop-1.0)))*step(0.3,h2);
    for(int k=0;k<2;k++){ float fk=float(k);
      float hk=hb(sd,11.+fk*5.); if(hk<0.42) continue;
      float ah=lerp(7.,20.,hb(sd,12.+fk*5.)), a0=lerp(4.,max(Hr*0.55,12.),hb(sd,13.+fk*5.))+fk*Hr*0.3;
      float x0=lerp(0.04,0.30,hb(sd,14.+fk*5.)), x1=lerp(0.70,0.96,hb(sd,15.+fk*5.));
      if(yTop>a0&&yTop<a0+ah&&lx>x0&&lx<x1) c=signPanel(float2((lx-x0)/(x1-x0),1.-(yTop-a0)/ah),float2((x1-x0)*Wm,ah),hk,pxm);
    }
    // and a vertical board down one side
    float vx=h>0.5?0.03:0.84; float vl=min(Hr*0.6,lerp(20.,60.,h2));
    if(hb(sd,31.)>0.50&&lx>vx&&lx<vx+0.13&&yTop>5.0&&yTop<5.0+vl){
      float seg=floor((yTop-5.0)/8.0), sf=frac((yTop-5.0)/8.0);
      c=signPanel(float2((lx-vx)/0.13,1.-sf),float2(0.13*Wm,8.0),hb(seg,sd+33.),pxm);
    }
  } else if(ht<0.89){
    // a dark glass tower: LED strips up its ribs, bands of light, a long banner
    float3 tint=signCol(frac(h*4.3+0.2));
    c=float3(0.006,0.008,0.022)+cityHaze(az)*0.05*(0.4+0.6*lx)+wc*win*0.20;
    float ribs=floor(lerp(2.,6.,h2)); float rx=frac(lx*ribs);
    float wpx=max(pxm/Wm*ribs,0.012);
    for(int k=0;k<3;k++){ float o=(float(k)-1.)*wpx*0.9;
      c[k]+=tint[k]*1.5*(1.-smoothstep(0.,wpx*1.2,abs(min(rx,1.-rx)-abs(o)))); }
    float bs=lerp(38.,70.,h4);
    c+=lerp(tint,((float3)(1.0)),0.4)*(1.-smoothstep(0.,max(pxm,1.2),abs(frac(ym/bs)-0.5)*bs))*1.1;
    c+=ngFilm(yTop*0.02+lx)*1.6*smoothstep(14.,0.,yTop);
    float bl=min(Hr*0.5,lerp(40.,120.,h));
    if(hb(sd,35.)>0.3&&abs(lx-0.5)<0.16&&yTop>24.0&&yTop<24.0+bl){
      float seg=floor((yTop-24.0)/14.0), sf=frac((yTop-24.0)/14.0);
      c=signPanel(float2((lx-0.34)/0.32,1.-sf),float2(0.32*Wm,14.0),hb(seg,sd+37.),pxm);
    }
  } else {
    // a tower whose face is a screen
    c=face+wc*win*0.5;
    float s0=lerp(14.,40.,h2), sh=min(lerp(50.,150.,h4),Hr*0.55);
    if(yTop>s0&&yTop<s0+sh&&abs(lx-0.5)<0.44){
      float2 sp=float2((lx-0.06)/0.88*Wm/sh,(yTop-s0)/sh); float t=TIME*0.2+h*10.;
      float3 a1=signCol(frac(h*3.3)), a2=signCol(frac(h*3.3+0.37)); float3 vid;
      float pat=frac(h*17.);
      if(pat<0.3){ float r=length(sp-float2(Wm/sh*0.5,0.5)); vid=lerp(a1,a2,step(0.5,frac(r*5.0-t)))*step(r,0.46)+a2*0.15; }
      else if(pat<0.6){ vid=lerp(a1,a2,step(0.5,frac((sp.x+sp.y)*4.0+t)))*0.9; }
      else { float n=vnoise(sp*3.0+t); vid=lerp(a1,a2,smoothstep(0.35,0.65,n))+((float3)(1.0))*smoothstep(0.78,0.9,vnoise(sp*6.0-t)); }
      float scan=lerp(1.0,0.72+0.28*step(0.5,frac(ym/1.6)),1.-smoothstep(0.3,0.8,pxm/1.6));
      c=vid*1.15*scan;
    }
    c+=signCol(frac(h*3.3))*1.4*(1.-smoothstep(0.,max(pxm/Wm,0.01)*1.3,min(lx,1.-lx)));
  }
  // glass: a sheen slides over the face, its edges split the light
  float sh2=exp(-pow((lx-0.10-(ym/Hr)*0.55-0.25*sin(h*6.28))/0.09,2.0));
  c+=(float3(0.08,0.11,0.26)+cityHaze(az)*0.12)*sh2*0.20;
  float ew=max(pxm/Wm,0.007);
  for(int k=0;k<3;k++){ float o=(float(k)-1.)*ew*1.3;
    c[k]+=0.80*(1.-smoothstep(0.,ew*1.1,min(abs(lx-ew-o),abs(1.-lx-ew+o))))*(0.35+0.65*ym/Hr); }
  c+=cityHaze(az)*0.22*exp(-ym/45.0)+signCol(h)*0.20*exp(-ym/14.0);     // the glow of the street and its shopfronts
  return c;
}

// ---- the city by DAY: the same glass city in sunlight. Every face is a mirror of the sky (tinted glass: teal, silver,
//      bronze, violet), framed by fine mullions; the sun glints off it and splits into colour at the frames; the screens
//      still play, dimmer against the day; the far city sinks into blue air.
static float LIT = 0.0;                                               // how lit the city is: 0 day .. 1 night (set in setCity)
void setCity(){ LIT=max(NIGHT,smoothstep(0.45,1.20,PHASE)); }   // (Fernando: by the time the sky is half way to night, every building is a night building - no blue glass left)
float3 dayAir(float az){ return ngSkyBase(normalize(float3(cos(az),0.06,sin(az))))*1.05; }
static const float CELL = 54.0, STREET = 7.5, RIN = 440.0;
// ---- DAY: a Frutiger Aero / Japanese "city of the future" (reference: Fernando's bubble-era future city painting).
//      Deep cobalt and teal glass towers, polished and glossy, set in white structural frames; white crowns and helipad
//      tops; green parks and roof gardens; canals and a bay of blue water; clean white plazas. Optimistic, bright.
float3 aeroGlass(float tk){ return tk<0.40?float3(0.06,0.20,0.62):(tk<0.62?float3(0.04,0.38,0.55):(tk<0.80?float3(0.10,0.30,0.75):float3(0.30,0.52,0.70))); }
float3 dayGlass(float3 rd, float3 n, float lx, float ym, float Hr, float Wm, float pxm, float sd, float3 cNight){
  float h=hb(sd,1.), h2=hb(sd,2.), h4=hb(sd,4.);
  float3 g0=aeroGlass(frac(h*3.7+h4));
  float round=step(0.62,h2);                                  // some towers are round: the face turns across its width
  float3 nn=n;
  if(round>0.5){ float3 t=normalize(float3(-n.z,0.,n.x)); float a=(lx-0.5)*2.2; nn=normalize(n*cos(a)+t*sin(a)); }
  float3 r=reflect(rd,nn);
  float3 sky=ngSkyBase(normalize(float3(r.x,abs(r.y)+0.04,r.z)));
  float F=0.20+0.80*pow(1.0-abs(dot(rd,nn)),4.0);
  // polished glass: its own deep colour, the sky and the sun in it, a soft cloud sliding across
  float3 c=g0*0.55+sky*lerp(0.18,0.85,F)*lerp(((float3)(1.0)),g0*2.2,0.45);
  float cl=smoothstep(0.52,0.78,vnoise(float2(lx*1.5+sd*3.1+r.x*2.0,ym/48.0+r.z*2.0)));
  c=lerp(c,float3(0.92,0.95,1.0),cl*smoothstep(0.30,0.95,ym/Hr)*0.40);
  // the Aero gloss: a bright band near the top of every pane run, a vertical sheen down the turn of round towers
  c+=float3(0.85,0.93,1.0)*0.22*smoothstep(0.80,0.98,frac(ym/lerp(18.,32.,h4)))*(1.-round);
  c+=float3(0.9,0.96,1.0)*0.35*exp(-pow((lx-0.30)/0.06,2.0))*round;
  // panes: fine dark joints (most towers), white bands every few floors
  float floorH=lerp(3.4,4.4,h4), cols=floor(Wm/lerp(1.8,3.0,h2));
  float2 gg=float2(lx*cols,ym/floorH); float2 gf=abs(frac(gg)-0.5);
  float res=1.-smoothstep(0.3,1.0,max(pxm/floorH,pxm/(Wm/cols)));
  float joint=max(1.-smoothstep(0.0,max(pxm/(Wm/cols),0.03),0.5-gf.x),1.-smoothstep(0.0,max(pxm/floorH,0.03),0.5-gf.y));
  c=lerp(c,c*0.55,joint*0.6*res);
  float band=step(0.5,h)*(1.-smoothstep(0.0,max(pxm,0.6),abs(frac(ym/lerp(14.,28.,h2))*lerp(14.,28.,h2)-1.0)));
  // the white frame: corner piers and a crown (structural white, catching the sun)
  float pier=max(pxm,lerp(1.2,2.6,h4));
  float edgeW=step(lx*Wm,pier)+step((1.-lx)*Wm,pier);
  float crown=step(Hr-ym,lerp(3.,9.,h));
  float sunF=max(dot(nn,SUN),0.0);
  float3 white=float3(0.86,0.89,0.94)*(0.55+0.55*sunF)+float3(0.05,0.08,0.14);
  c=lerp(c,white,max(max(edgeW,crown),band*0.9)*step(0.25,h4));
  // the sun in the glass: a sharp glint and a broad sheen, split into colour at the joints (prism)
  float sp=max(dot(r,SUN),0.0);
  c+=float3(1.0,0.97,0.9)*(pow(sp,700.0)*16.0+pow(sp,28.0)*0.55)*SUNUP+ngFilm(lx*2.0+ym*0.01+h)*pow(sp,80.0)*joint*2.5*SUNUP;
  c*=lerp(0.70,1.0,smoothstep(0.0,0.30,ym/Hr));               // the foot of the canyon is in shade
  // screens still play on the towers that carry them, softer by day
  float glowM=smoothstep(0.55,1.1,max(cNight.r,max(cNight.g,cNight.b)))*step(0.80,hb(sd,5.));
  return lerp(c,cNight,glowM*0.45);
}
float3 dayRoof(float2 r, float2 sz, float2 id, float3 rd, float3 cNight){
  float h=ngH12(id+71.0), ty=ngH12(id+13.0);
  float dE=min(min(r.x,sz.x-r.x),min(r.y,sz.y-r.y));
  float3 white=float3(0.84,0.87,0.92)*(0.75+0.35*max(SUN.y,0.0)*2.0);
  float3 c=white;
  float3 rr=reflect(rd,float3(0,1,0)); float3 sky=ngSkyBase(normalize(float3(rr.x,abs(rr.y)+0.05,rr.z)));
  if(ty<0.35){                                                // a glass roof / helipad deck in blue glass, white rim
    c=lerp(aeroGlass(h)*0.6+sky*0.45,white,1.-smoothstep(1.5,2.5,dE));
    float rad=length(r-sz*0.5); c=lerp(c,((float3)(0.95)),(1.-smoothstep(0.,0.8,abs(rad-7.0)))*step(0.5,h));   // a pad's ring
  } else if(ty<0.70){                                         // a roof garden: lawn, trees, a pool
    float2 q=r/5.0; float tr=step(0.62,ngH12(floor(q)+id))*smoothstep(0.45,0.2,length(frac(q)-0.5));
    c=lerp(float3(0.30,0.62,0.22),float3(0.10,0.38,0.12),tr);
    float pool=step(abs(r.x-sz.x*0.5),sz.x*0.18)*step(abs(r.y-sz.y*0.6),sz.y*0.12)*step(0.5,h);
    c=lerp(c,float3(0.25,0.70,0.95)+sky*0.3,pool);
    c=lerp(c,white,1.-smoothstep(1.0,2.0,dE));
  } else {                                                    // white roof, plant in pale grey
    c*=0.85+0.15*step(0.5,ngH12(floor(r/4.0)+id));
  }
  c*=lerp(0.78,1.0,smoothstep(0.0,2.0,dE));
  return c;
}
float3 dayStreet(float2 pw, float3 rd, float3 cNight){
  float2 id=floor(pw/CELL), f=pw-id*CELL;
  float2 dB=min(f,CELL-f); float m=min(dB.x,dB.y);
  float3 rr=reflect(rd,float3(0,1,0)); float3 sky=ngSkyBase(normalize(float3(rr.x,abs(rr.y)+0.05,rr.z)));
  float3 c=float3(0.62,0.66,0.72);                                // pale roads
  c=lerp(c,float3(0.90,0.92,0.95),step(STREET-2.0,m));           // white pavements at the kerb
  c=lerp(c,((float3)(0.95)),step(abs(m-1.2),0.12)*step(0.5,frac((dB.x<dB.y?pw.y:pw.x)/5.0)));   // lane marks
  float tr=step(STREET-1.6,m)*step(0.55,ngH12(floor(pw/4.0)));  // trees along the street
  c=lerp(c,float3(0.16,0.45,0.18),tr*smoothstep(0.5,0.2,length(frac(pw/4.0)-0.5)));
  return c;
}
// the ground between the blocks: green parks and blue canals, by district
float districtK(float2 id){ return vnoise(id*0.21+7.0); }
float4 cityLayer(float3 rd, float L){
  float az=atan2(rd.z,rd.x), el=rd.y/max(length(rd.xz),1e-3);
  float R=420.0*pow(1.6,L);
  float lotW=lerp(46.,72.,L/4.);
  float cells=floor(6.2832*R/lotW);
  float u=az/6.2832*cells+L*13.37; float ci=floor(u), fu=frac(u);
  float sd=ci+L*917.0;
  float h=hb(sd,1.), h3=hb(sd,3.), h4=hb(sd,4.);
  float dist=vnoise(float2(ci*lotW/900.0,L*5.0));
  float tall=pow(h3,2.5)*smoothstep(0.25,0.75,dist);
  float Hr=lerp(110.,330.,h*h)+tall*lerp(220.,620.,L/4.);
  float ym=el*R+SEA_D;
  float gap=0.03+0.05*h4;
  if(fu<gap||ym<0.0||ym>Hr+130.0) return ((float4)(0));
  float lx=(fu-gap)/(1.-gap), Wm=lotW*(1.-gap);
  float pxm=GPX*R*(1.0+el*el)+1e-3;
  float hzK=1.0-exp(-R/2400.0);
  float3 hazeC=float3(0.012,0.016,0.050)+cityHaze(az)*0.10*exp(-max(ym,0.)/260.0);
  if(ym>Hr){
    float bbH=lerp(7.,14.,h);
    if(h4>0.40&&tall<0.3&&ym<Hr+bbH&&abs(lx-0.5)<0.44)
      return float4(lerp(signPanel(float2((lx-0.06)/0.88,(ym-Hr)/bbH),float2(Wm*0.88,bbH),hb(sd,21.),pxm),hazeC,hzK*0.6),1.0);
    float spH=lerp(30.,110.,h)*step(0.35,tall);                // a glass spire on the tall ones (the reference's pointed crowns)
    if(spH>0.&&ym<Hr+spH&&abs(lx-0.5)<0.42*(1.0-(ym-Hr)/spH)){
      float k=(ym-Hr)/spH; float3 rr=reflect(rd,-normalize(float3(cos(az),0.,sin(az))));
      float3 dG=aeroGlass(frac(h*5.1))*0.6+ngSkyBase(normalize(float3(rr.x,abs(rr.y)+0.05,rr.z)))*0.6;
      dG=lerp(dG,float3(0.88,0.91,0.96),step(abs(lx-0.5),0.04));                       // a white rib up its middle
      dG+=float3(1.0,0.97,0.9)*pow(max(dot(rr,SUN),0.),40.)*0.6*SUNUP;
      float3 nG=signCol(frac(h*4.3+0.2))*1.4*step(abs(lx-0.5),0.05)+float3(0.02,0.02,0.05);
      float3 cs=lerp(dG,nG,smoothstep(0.0,0.2,LIT*1.25-hb(sd,61.)));
      return float4(lerp(cs,lerp(dayAir(az)*0.9,hazeC,LIT),hzK*0.55),1.0);
    }
    if(tall>0.3&&ym<Hr+spH+12.0&&ym>Hr+spH&&abs(lx-0.5)*Wm<max(pxm,0.5)) return float4(float3(3.0,0.12,0.10)*(0.35+0.65*step(0.5,frac(TIME*0.45+h*7.))),1.0);
    return ((float4)(0));
  }
  float3 c=facade(lx,ym,Hr,Wm,pxm,az,sd,tall,1.0);
  float cornerX=lerp(0.22,0.78,hb(sd,2.));
  float farF=step(cornerX,lx)*step(0.35,h4);
  c*=lerp(1.0,0.60,farF);                                     // a corner: its far face sits in shade
  float3 cn=-normalize(float3(cos(az),0.0,sin(az)));             // the face looks back toward the islands
  float3 tg=float3(-cn.z,0.0,cn.x); float3 nF=normalize(lerp(cn,cn*0.7+tg*0.7,farF));
  float3 cDay=dayGlass(rd,nF,lx,ym,Hr,Wm,pxm,sd,c*(1.0-LIT)*0.0+c);
  cDay=lerp(cDay,dayAir(az)*0.90,hzK*0.50);
  float LITb=smoothstep(0.0,0.20,LIT*1.25-hb(sd,61.));
  return float4(lerp(cDay,lerp(c,hazeC,hzK*0.62),LITb),1.0);
}
float3 layerGlow(float3 rd, float L){
  float az=atan2(rd.z,rd.x), el=rd.y/max(length(rd.xz),1e-3), R=420.0*pow(1.6,L);
  float ym=el*R+SEA_D; if(ym<0.0) return ((float3)(0));
  float u=az*R/260.0+L*7.0; float i=floor(u), f=frac(u); f=f*f*(3.-2.*f);
  return lerp(signCol(hb(i,L+3.)),signCol(hb(i+1.,L+3.)),f)*exp(-ym/150.0)*0.030;
}
float3 cityAt(float3 rd, float3 bg){
  float3 col=bg; float3 glow=((float3)(0.0));
  [loop] for(int L=0;L<5;L++){
    float4 b=cityLayer(rd,float(L));
    if(b.a>0.5){ col=b.rgb; break; }
    if(L<3) glow+=layerGlow(rd,float(L))*LIT;
  }
  return col+glow;
}
float3 cityMirror(float3 rd, float elv){
  float az=atan2(rd.z,rd.x)+(vnoise(float2(rd.x*500.0+rd.z*310.0,elv*260.0+TIME*1.2))-0.5)*0.004;
  float3 col=((float3)(0.0)); float t=SEA_D/elv;
  for(int L=0;L<5;L++){ float R=420.0*pow(1.6,float(L));
    if(R<t) continue;
    float ymr=(R-t)*elv;
    float4 b=cityLayer(float3(cos(az),(ymr*1.3-SEA_D)/R,sin(az)),float(L));
    if(b.a>0.5){ col=b.rgb; break; }
  }
  return col;
}
// ---- the city UNDER the islands: real blocks on a street grid (the ray walks the grid cell by cell), roofs with
//      their lit parapets, glass roofs and pads, walls with the same advertising, wet streets with traffic
float bldH(float2 id){
  float h=ngH12(id*1.37+5.0), d=vnoise(id*0.23+3.0);
  if(districtK(id)<0.30) return 0.0;                         // a park or a canal: open ground
  
  return min(lerp(30.,112.,h*h)+smoothstep(0.50,0.85,d)*42.0*ngH12(id+9.0),148.0);
}
float3 roofCol(float2 r, float2 sz, float2 id, float pxm){
  float h=ngH12(id+71.0), ty=ngH12(id+13.0); float3 tint=signCol(h);
  float3 c=float3(0.014,0.016,0.034);
  float dE=min(min(r.x,sz.x-r.x),min(r.y,sz.y-r.y));
  float res=1.-smoothstep(0.4,1.2,pxm/1.5);
  for(int k=0;k<3;k++) c[k]+=tint[k]*1.25*(1.-smoothstep(0.,max(pxm,0.55),abs(dE-1.0-(float(k)-1.)*0.4)));   // the parapet's light, split
  c+=float3(0.020,0.022,0.040)*step(0.70,ngH12(floor(r/4.0)+id*3.0))*step(3.0,dE);     // plant and ducts
  if(ty<0.30){ float2 w=frac(r/3.0);                              // a glass roof, lit from inside
    c+=lerp(float3(0.45,0.85,1.10),float3(1.05,0.72,0.42),step(0.5,h))*0.22*lerp(0.5,step(0.35,ngH12(floor(r/3.0)+id))*step(0.12,w.x)*step(0.12,w.y),res)*step(4.0,dE); }
  else if(ty<0.42){ float rr=length(r-sz*0.5);                   // a landing pad
    c+=float3(1.0,0.74,0.05)*0.9*(1.-smoothstep(0.,max(pxm,0.5),abs(rr-8.0)))+float3(3.0,0.1,0.1)*step(rr,1.0)*step(0.5,frac(TIME*0.5+h)); }
  else if(ty<0.66){ float bx=abs(r.y-sz.y*0.25);                 // a billboard stands on it: its light spills over the roof
    float3 bc=signCol(ngH12(id+19.0)); c+=bc*0.45*exp(-bx/5.0)+bc*1.3*step(bx,max(pxm,0.6))*step(4.0,r.x)*step(r.x,sz.x-4.0); }
  else if(ty<0.80){ c+=float3(0.05,0.55,0.35)*0.20*step(3.0,dE)*(0.5+0.5*vnoise(r*0.8+id)); }   // a roof garden under glass
  return c;
}
float3 streetCol(float2 pw, float pxm){
  float2 id=floor(pw/CELL), f=pw-id*CELL;
  float2 dB=min(f,CELL-f);                                       // distance to the two street centre lines
  float dx=dB.x, dz=dB.y;                                      // dx small: on a z-running street; dz small: x-running
  float3 c=float3(0.006,0.008,0.020);
  // shopfront light on the wet road: this block's colours, strongest at the kerb, smeared (wet glass)
  float kerb=STREET-min(dx,dz);
  float3 shop=lerp(signCol(ngH12(id+3.0)),signCol(ngH12(id+17.0)),step(0.5,vnoise(pw*0.07)));
  c+=shop*0.80*exp(-max(kerb,0.)/3.5)*(0.6+0.4*vnoise(pw*float2(0.9,0.15)+TIME*0.3));
  bool xs=dz<dx;                                               // on an x-running street?
  float along=xs?pw.x:pw.y, across=xs?(f.y<CELL*0.5?f.y:f.y-CELL):(f.x<CELL*0.5?f.x:f.x-CELL);
  float lane=floor(xs?pw.y/CELL+0.5:pw.x/CELL+0.5);
  float res=1.-smoothstep(0.3,1.0,pxm/0.8);
  c+=float3(0.9,0.75,0.3)*0.25*step(abs(across),0.2)*step(0.5,frac(along/6.0))*res;      // the centre line
  for(int k=0;k<2;k++){ float sg=k==0?1.0:-1.0;                 // traffic: white ahead, red behind
    float car=frac(along/46.0*sg-TIME*0.22+hb(lane,float(k)+3.0));
    float on=step(0.86,car)*step(abs(across-sg*2.6),0.9);
    c+=(k==0?float3(1.3,1.25,1.1):float3(1.5,0.08,0.05))*on*(0.4+0.6*smoothstep(0.86,1.0,car)); }
  float xw=step(dx,STREET+3.0)*step(dz,STREET+3.0);            // crossings at the junctions
  c+=((float3)(0.5))*0.35*xw*step(0.5,frac((xs?across:along)/1.6))*step(STREET-0.5,max(dx,dz))*res;
  return c;
}
float4 cityBelow(float3 ro, float3 rd){
  float dn=-rd.y; if(dn<0.015) return ((float4)(0));
  float3 d=rd; if(abs(d.x)<1e-4) d.x=1e-4; if(abs(d.z)<1e-4) d.z=1e-4;
  float t=max((ro.y-150.0)/dn,0.0), tG=ro.y/dn;
  float2 p=ro.xz+d.xz*t; float2 id=floor(p/CELL);
  float2 stp=sign(d.xz);
  float2 tMax=t+((id+max(stp,0.0))*CELL-p)/d.xz, tDel=CELL/abs(d.xz);
  for(int i=0;i<44;i++){
    if(length((id+0.5)*CELL-ro.xz)>RIN) return ((float4)(0));        // past the inner city: the far rings take over
    float tExit=min(tMax.x,tMax.y), H=bldH(id);
    float2 bmin=id*CELL+STREET, bmax=(id+1.0)*CELL-STREET;
    if(H>1.0){
      float3 ta=(float3(bmin.x,0.,bmin.y)-ro)/d, tb=(float3(bmax.x,H,bmax.y)-ro)/d;
      float3 tmn=min(ta,tb), tmx=max(ta,tb);
      float tn=max(max(tmn.x,tmn.y),tmn.z), tf=min(min(tmx.x,tmx.y),tmx.z);
      if(tn<tf&&tf>0.0){
        float3 q=ro+d*tn; float pxm=GPX*tn+1e-3; float3 c; float2 sz=bmax-bmin;
        float az=atan2(q.z,q.x), sd=id.x*31.7+id.y*57.3;
        float3 cD;
        if(tmn.y>=tmn.x&&tmn.y>=tmn.z){ c=roofCol(q.xz-bmin,sz,id,pxm/max(dn,0.15)); cD=dayRoof(q.xz-bmin,sz,id,d,c); }
        else if(tmn.x>=tmn.z){ c=facade((q.z-bmin.y)/sz.y,q.y,H,sz.y,pxm,az,sd+1.0,0.0,1.0)*0.85;
          cD=dayGlass(d,float3(-sign(d.x),0.,0.),(q.z-bmin.y)/sz.y,q.y,H,sz.y,pxm,sd+1.0,c); }
        else { c=facade((q.x-bmin.x)/sz.x,q.y,H,sz.x,pxm,az,sd+2.0,0.0,1.0);
          cD=dayGlass(d,float3(0.,0.,-sign(d.z)),(q.x-bmin.x)/sz.x,q.y,H,sz.x,pxm,sd+2.0,c); }
        float fog=1.0-exp(-tn/1100.0);
        float3 cn2=lerp(c,float3(0.012,0.016,0.050)+cityHaze(az)*0.06,fog);
        float LITb=smoothstep(0.0,0.20,LIT*1.25-ngH12(id+61.0));
        return float4(lerp(lerp(cD,dayAir(az)*0.85,(1.0-exp(-tn/2600.0))*0.85),cn2,LITb),1.0);
      }
    }
    if(tG<tExit){
      float3 q=ro+d*tG; float3 c=streetCol(q.xz,GPX*tG/max(dn,0.15));
      float fog=1.0-exp(-tG/1100.0);
      float2 cid=floor(q.xz/CELL); float2 ff=q.xz-cid*CELL; float inBlk=step(STREET,min(min(ff.x,CELL-ff.x),min(ff.y,CELL-ff.y)));
      float3 gd=dayStreet(q.xz,d,c);
      if(inBlk>0.5&&bldH(cid)<1.0){
        float dk=districtK(cid);
        float3 rr=reflect(d,float3(0,1,0)); float3 sky=ngSkyBase(normalize(float3(rr.x,abs(rr.y)+0.05,rr.z)));
        if(dk<0.16){ float rip=vnoise(q.xz*0.3+TIME*0.3); gd=float3(0.03,0.30,0.62)+sky*0.55+((float3)(1.0))*pow(max(dot(normalize(rr),SUN),0.),300.)*4.0*rip; }   // water
        else { float2 qq=q.xz/6.0; float tr=step(0.45,ngH12(floor(qq)+cid))*smoothstep(0.45,0.15,length(frac(qq)-0.5));
          gd=lerp(float3(0.34,0.66,0.24),float3(0.10,0.40,0.14),tr);                          // park: lawn and trees
          float path=1.-smoothstep(0.,1.0,abs(frac((ff.x+ff.y)/22.0)-0.5)*22.0);
          gd=lerp(gd,float3(0.90,0.90,0.86),path*0.9); }
      }
      float3 cD=lerp(gd,dayAir(atan2(q.z,q.x))*0.85,(1.0-exp(-tG/2600.0))*0.85);
      return float4(lerp(cD,lerp(c,float3(0.012,0.016,0.050),fog),LIT),1.0);
    }
    if(tMax.x<tMax.y){ tMax.x+=tDel.x; id.x+=stp.x; } else { tMax.y+=tDel.y; id.y+=stp.y; }
  }
  return ((float4)(0));
}
// the city far below in the void, seen down through haze: blocks of light of every size, bright avenues, traffic
float3 voidCity(float3 ro, float3 rd){
  float dn=max(-rd.y,0.0); if(dn<0.003) return ((float3)(0));
  float t=(ro.y-(SEA_Y-520.0))/dn; float2 p=(ro.xz+rd.xz*t);
  float dist=exp(-t/3200.);
  float3 c=((float3)(0));
  // avenues: a coarse irregular grid (every ~6 blocks), bright; small streets: only their lights
  float2 g=p/180.0; float2 jf=g+float2(vnoise(float2(floor(g.y),3.))*0.3,vnoise(float2(floor(g.x),7.))*0.3);
  float2 f=abs(frac(jf)-0.5); float pxw=length(fwidth(g))+1e-4;
  // avenues glow as soft bands of light (a sharp line per street read as a synthwave grid)
  float wav=max(0.018,pxw*2.0);
  float major=step(0.55,ngH12(float2(floor(jf.x),11.))), major2=step(0.55,ngH12(float2(floor(jf.y),13.)));
  float av=max(exp(-pow((0.5-f.x)/wav,2.0))*major,exp(-pow((0.5-f.y)/wav,2.0))*major2);
  float3 avC=neonPal(ngH12(floor(jf))*0.6+0.15);
  c+=avC*av*0.30;
  // the blocks between: light points (windows and lamps), clustered, warm and neon
  float2 q=p/9.0; float2 qi=floor(q), qf=frac(q)-0.5;
  float dens=smoothstep(0.35,0.75,vnoise(p/260.0))*0.8+0.15;
  float on=step(1.0-dens*0.45,ngH12(qi));
  float pt=exp(-dot(qf,qf)*40.0)*on;
  c+=lerp(float3(1.0,0.82,0.6),neonPal(ngH12(qi+2.)),step(0.6,ngH12(qi+9.)))*pt*0.55;
  // traffic: light moving along the avenues
  float tr=pow(frac(jf.x*3.0+TIME*0.08+ngH12(float2(floor(jf.y),1.))),40.)*(1.-smoothstep(0.,0.02,0.5-f.y));
  c+=float3(1.0,0.35,0.25)*tr*1.2;
  return c*dist;
}

float3 ngVoid(float3 ro, float3 rd, float3 Ld) {
    float  dn = saturate(-rd.y);
    float3 hor = ngSkyBase(normalize(float3(rd.x, 0.0, rd.z)));
    float3 deep = lerp(lerp(float3(0.020, 0.034, 0.130), float3(0.030, 0.014, 0.075), DUSK), float3(0.0030, 0.0022, 0.012), NIGHT);
    float3 col = lerp(hor, deep, smoothstep(0.0, 0.07, dn));
    // the void is the city: its wet glass ground holds the city's lights (long streaks at night), its towers stand all
    // round, and right under the islands its blocks and streets
    setCity();                                                     // (the sea pass calls this directly, not via ngBackground)
    GPX = max(length(fwidth(rd)), 1e-5);
    SEA_D = clamp(CAM.y - (SEA_Y - 120.0), 60.0, 420.0);
    float4 cb = cityBelow(float3(ro.x, SEA_D, ro.z), rd);          // under the islands: the city itself
    [branch] if (cb.a > 0.5) col = cb.rgb;
    else {
        float3 base = lerp(dayAir(atan2(rd.z, rd.x)) * 0.35, float3(0.006, 0.008, 0.022) + voidCity(ro, rd) * 0.35, LIT);
        float  elv = dn / max(length(rd.xz), 1e-3);
        float3 refl = cityMirror(rd, elv);
        float  rip = 0.60 + 0.40 * vnoise(float2(rd.x * 240.0 + rd.z * 130.0, elv * 900.0 - TIME * 2.0));
        float  F = 0.40 + 0.60 * pow(1.0 - dn, 3.0);
        float3 nc = base * 0.6 + refl * rip * F;
        col = cityAt(rd, nc);                                       // the buildings' feet stand in front of it
    }
    float  fadeH = smoothstep(0.015, 0.09, dn);
    [branch] if (fadeH > 0.0) {
        [unroll] for (int L = 3; L >= 0; L--) {
            float4 sh = ngShards(ro, rd, SEA_Y + 14.0 - 34.0 * float(L), 58.0 + 26.0 * float(L), 13.0 * float(L) + 2.0, Ld);
            col = lerp(col * (1.0 - sh.a * fadeH) + sh.rgb * fadeH, col, 0.80);
        }
    }
    return col;
}
// the night's veil: prismatic curtains (an aurora seen through glass) - violet, cyan, a breath of green, drawn as
// stacked sheets so they hang in folds with fine vertical rays
float3 ngVeil(float3 rd) {
    float e = rd.y;
    if (e < 0.03) return float3(0.0, 0.0, 0.0);
    float3 acc = float3(0.0, 0.0, 0.0);
    float  t = TIME * 0.035;
    int    nK = QUALITY > 0.5 ? 4 : 3;                              // (was 6 sheets: night cost 6.6 ms at 4K looking up)
    [loop] for (int k = 0; k < nK; k++) {
        float  hk = 1.0 + float(k) * 0.24;                          // stacked sheets: the folds hang down
        float2 p = rd.xz / (e + 0.05) * (0.55 / hk);
        float  v = p.y + 0.9 * sin(p.x * 0.55 + t * 2.0) + fbm(p * 0.45 + float2(t, 0.0), 2) * 1.6;
        float  band = exp(-pow(sin(v * 0.9) / 0.20, 2.0));
        float  rays = 0.55 + 0.45 * vnoise(float2(p.x * 9.0 + v * 2.0, t * 3.0));
        float3 c = lerp(float3(0.45, 0.30, 1.00), ngFilm(v * 0.12 + 0.35 + float(k) * 0.03) * float3(0.70, 1.00, 1.00), 0.55);
        c = lerp(c, lerp(float3(0.05, 0.95, 1.00), float3(1.00, 0.18, 0.85), 0.5 + 0.5 * sin(v * 0.35 + float(k))), NG_NEON * 0.6);   // neon night
        acc += c * band * rays * (1.0 - float(k) / float(nK)) * 1.35;
    }
    return acc * 0.055 * (1.0 + 1.6 * NG_NEON) * smoothstep(0.03, 0.20, e) * (1.0 - smoothstep(0.55, 0.95, e));
}
float3 ngBackground(float3 rd) {
    setCity();
    GPX = max(length(fwidth(rd)), 1e-5);
    SEA_D = clamp(CAM.y - (SEA_Y - 120.0), 60.0, 420.0);           // the city's streets lie ~175 under the islands
    float3 Ld = PHASE > 1.45 ? SUN : SUNDISC;                         // the sun by day, the moon at night
    [branch] if (rd.y < 0.0) return ngVoid(CAM, rd, Ld);
    NgDome D  = ngDome(rd);
    float3 rf = D.rdF;
    float3 col = ngSkyBase(rf) * (0.965 + 0.07 * D.hcell.x);          // each pane a breath brighter or darker
    float  e = rd.y;
    col += ngSpecS(saturate(0.5 - e * 90.0)) * exp(-pow(e * 110.0 - 0.6, 2.0)) * lerp(0.06, 0.12, NIGHT) * (1.0 - 0.5 * DUSK);   // the dome's base ring
    float4 ci = ngCirrus(rf, Ld);
    float3 ciC = ci.rgb * lerp(lerp(float3(0.95, 0.97, 1.0), float3(0.95, 0.60, 0.70), DUSK), float3(0.10, 0.10, 0.22), NIGHT);
    col = lerp(col, ciC, ci.a * 0.18);   // (quieter: the altocumulus is the body now)
    float4 ac = NG_CLOUDS > 0.0 ? ngAlto(rf, Ld, false) : float4(0, 0, 0, 0);
    ac.a *= NG_CLOUDS;
    float3 acT = lerp(float3(1.0, 1.0, 1.0), float3(0.05, 0.05, 0.11) + cityHaze(atan2(rf.z, rf.x)) * 0.30 * exp(-rf.y * 3.0), NIGHT);   // night: clouds lit from below by the city
    col = lerp(col, ac.rgb * acT, ac.a * lerp(1.0, 0.75, NIGHT));
    float  angD = acos(clamp(dot(rf, SUNDISC), -1.0, 1.0));
    float  disc = smoothstep(0.0125, 0.0095, angD);
    col += (float3(6.0, 5.6, 5.0) * disc + float3(1.0, 0.95, 0.86) * (exp(-angD * 30.0) * 1.1 + exp(-angD * 6.0) * 0.20)) * SUNUP * (1.0 - ci.a * 0.5);
    col += ngPrismBeams(rf, SUNDISC) * float3(1.0, 0.97, 0.92) * 0.42 * SUNUP * (1.0 - DUSK * 0.4) * (1.0 - ac.a * 0.85) * (1.0 - ci.a * 0.6);
    col += ngHalo(rf, SUNDISC, SUNUP * lerp(1.0, 1.5, DUSK), 1.0);
    col += ngCza(rd, SUNDISC, SUNUP * (1.0 - DUSK));
    // the parhelic circle: a thin white ring at the sun's height all the way round the sky, with the anthelion (a
    // soft bright spot) opposite the sun - so the sky away from the sun has its own ice-light too
    {
        float  dv = (rd.y - SUNDISC.y) / 0.0085;
        float  opp = saturate(-dot(normalize(rd.xz + 1e-4), normalize(SUNDISC.xz + 1e-4)));
        float  ring = exp(-dv * dv) * (0.035 + 0.030 * opp);
        float  anth = exp(-dv * dv * 0.5) * exp(-pow(acos(saturate(opp)) / 0.05, 2.0)) * 0.12;
        col += float3(0.92, 0.95, 1.0) * (ring + anth) * SUNUP * (1.0 - DUSK) * smoothstep(0.02, 0.08, rd.y);
    }
    float  angL = acos(clamp(dot(rd, Ld), -1.0, 1.0));
    float  nearL = exp(-angL * 2.2);
    float3 lineDay = lerp(float3(0.85, 0.92, 1.0) * 0.06, lerp(float3(1.0, 1.0, 1.0), ngSpecS(frac(angL * 2.2 + D.hcell.y * 0.3)), 0.65) * 0.75, nearL);
    float3 lineC = lerp(lerp(lineDay, lineDay * float3(1.0, 0.70, 0.85), DUSK), float3(0.50, 0.42, 0.95) * 0.045, NIGHT);
    col += lineC * (D.edge + D.edgeC * 0.35);
    // the neon night (teams going out): the dome's lattice lights up in cyan and magenta, light running along its lines,
    // faster and fuller toward the finale. (Strongest at night, which the match brings on as beds break.)
    [branch] if (NG_NEON > 0.01) {
        float  hueN = 0.5 + 0.5 * sin(D.fam * 2.1 + D.lineId * 0.7 + TIME * 0.3);
        float3 neonC = lerp(float3(0.10, 0.95, 1.00), float3(1.00, 0.20, 0.85), hueN);
        float  run = abs(frac(D.along * 0.12 - TIME * lerp(0.10, 0.28, NG_NEON) + D.lineId * 0.37) - 0.5) * 2.0;
        float  pulse = pow(saturate(run), 48.0);   // short bursts of light, not laser lines (seen in game)
        float  k = NG_NEON * lerp(0.45, 1.0, NIGHT);
        col += neonC * k * (D.lnM * (0.035 + 0.75 * pulse) + D.lnC * 0.20 * pulse);
        float  az = atan2(rd.z, rd.x);
        col += lerp(float3(0.10, 0.95, 1.00), float3(1.00, 0.20, 0.85), 0.5 + 0.5 * sin(az * 3.0 + TIME * 0.2)) * exp(-pow(e * 60.0 - 0.6, 2.0)) * 0.16 * k;   // the base ring goes neon
    }
    [branch] if (NIGHT > 0.001) {
        float azN = atan2(rd.z, rd.x);
        col *= lerp(1.0, 0.26, NIGHT);                                // a deep navy night: the colour lives in the lights
        col += (cityHaze(azN) * (exp(-max(rd.y, 0.0) * 9.0) * 0.09 + exp(-max(rd.y, 0.0) * 2.5) * 0.015) + float3(0.012, 0.02, 0.07) * exp(-max(rd.y, 0.0) * 3.0)) * NIGHT;
        col += (csStars(rd) * 0.45 * (1.0 - ac.a) + csMoon(rd) * (1.0 - ac.a * 0.85)) * NIGHT;   // the city's light drowns most stars
        col += ngVeil(rd) * NIGHT;
        float am = acos(clamp(dot(rd, SUN), -1.0, 1.0)), x2 = (am - 0.384) / 0.0175;
        col += float3(0.62, 0.66, 0.95) * (x2 < 0.0 ? exp(-x2 * x2 * 2.0) : exp(-x2 * 0.6)) * 0.035 * NIGHT * smoothstep(-0.02, 0.05, rd.y);   // the moon's ring
    }
    col = cityAt(rd, col);                                            // the city all round, day and night
    return col;
}
float3 csBackground(float3 rd, bool withSea) {
    if (NG_IS) return ngBackground(rd);
    float  mu = dot(rd, SUN);
    float  ang = acos(clamp(mu, -1.0, 1.0));
    float3 col = skyBase(rd);
    float4 hc = highClouds(rd, mu);
    float  angD = acos(clamp(dot(rd, SUNDISC), -1.0, 1.0));
    float  disc = smoothstep(0.0125, 0.0095, angD);
    float3 sun = (float3(3.2, 2.3, 1.25) * disc + float3(1.5, 0.78, 0.30) * exp(-angD * 38.0) * 0.8) * SUNUP;
    col += sun * (1.0 - hc.a * 0.85);
    col = csGrade(col * (1.0 - hc.a) + hc.rgb, rd);
    [branch] if (NIGHT > 0.001) {
        float  e = max(rd.y, 0.0);
        float3 nb = lerp(float3(0.040, 0.055, 0.110), float3(0.006, 0.010, 0.032), smoothstep(0.0, 0.45, e));
        nb = lerp(nb, float3(0.020, 0.030, 0.075), smoothstep(0.0, 0.02, e) * (1.0 - smoothstep(0.02, 0.2, e)) * 0.5);
        float3 hcN = csGrade(hc.rgb, rd);                   // clouds keep their moonlit shapes over the night gradient
        col = lerp(col, nb * (1.0 - hc.a) + hcN, NIGHT);
    }
    [branch] if (NIGHT > 0.001) col += (csStars(rd) * (1.0 - hc.a) + csMoon(rd) * (1.0 - hc.a * 0.8)) * NIGHT;
    [branch] if (withSea) {
        float  tHit;
        float4 sea = cloudSea(CAM, rd, 3400.0, tHit);
        if (sea.a > 0.0) col = sea.rgb;
    }
    col += csGlow(ang, hc.a);
    return col;
}
// the cloud sea alone for a sky pixel: a = 0 where the ray misses it (keep the sky pass's colour)
float4 csSeaPixel(float3 rd) {
    if (NG_IS) { if (rd.y >= 0.0) return float4(0.0, 0.0, 0.0, 0.0); return float4(ngVoid(CAM, rd, PHASE > 1.45 ? SUN : SUNDISC), 1.0); }
    float  tHit;
    float4 sea = cloudSea(CAM, rd, 3400.0, tHit);
    if (sea.a <= 0.0) return float4(0.0, 0.0, 0.0, 0.0);
    float  ang = acos(clamp(dot(rd, SUN), -1.0, 1.0));
    return float4(sea.rgb + csGlow(ang, 0.0), 1.0);
}
