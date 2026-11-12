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
struct NgDome { float3 rdF; float edge; float edgeC; float3 hcell; };
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
    float  gl = smoothstep(0.70, 0.97, vnoise(float2(along * 1.1 + lineId * 7.1, fam * 13.0 + TIME * 0.05)));
    float  fade = smoothstep(0.03, 0.22, e);
    D.edge  = (1.0 - smoothstep(0.0, px * 1.2, d)) * gl * fade;
    D.edgeC = (1.0 - smoothstep(0.0, px * 5.0, d)) * gl * fade;
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
    float fr = fbm(q * 7.0, 3) * 0.7 + fbm(q * 19.0, 2) * 0.3;   // a frayed outline
    return saturate((cov * 2.1 - 0.80) - b * 0.90 - fr * 0.36 + 0.33);
}
// the same field, coarse (the big lumps only): enough for the light march (cost: seen 5.3 ms for the pass at 4K)
float ngAcFieldLo(float2 q, float cov) {
    return saturate((cov * 2.1 - 0.80) - (1.0 - vnoise(q * float2(4.9, 3.6))) * 0.68 - 0.10 + 0.30);   // one fetch, no cells
}
float4 ngAlto(float3 rd, float3 Ld, bool cheap) {
    float e = rd.y;
    [branch] if (e < 0.015) return float4(0, 0, 0, 0);
    float2 P = rd.xz / (e * 0.55 + 0.16) * 1.5 + TIME * float2(0.006, 0.002);   // (overhead a little denser: seen in game, huge soft blobs up there)
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
float3 ngVoid(float3 ro, float3 rd, float3 Ld) {
    float  dn = saturate(-rd.y);
    float3 hor = ngSkyBase(normalize(float3(rd.x, 0.0, rd.z)));
    float3 deep = lerp(lerp(float3(0.020, 0.034, 0.130), float3(0.030, 0.014, 0.075), DUSK), float3(0.0030, 0.0022, 0.012), NIGHT);
    float3 col = lerp(hor, deep, smoothstep(0.0, 0.07, dn));   // (seen in game: a wider fade read as a fog wall)
    // the void is a still, dark glass sea far below: a dim mirror of the sky and its clouds, broken by slow swells
    // (seen in game: a flat navy void under a busy sky read as unfinished)
    {
        float  tW = (ro.y - (SEA_Y - 120.0)) / max(dn, 0.01);
        float  F  = 0.04 + 0.96 * pow(1.0 - dn, 5.0);
        float  wM = min(F * 1.6, 0.75) * exp(-tW / 2200.0) * smoothstep(0.0, 0.02, dn);
        [branch] if (wM > 0.03) {                              // looking steeply down it is too faint to pay for
            float2 wq = (ro.xz + rd.xz * tW) / 90.0;
            float2 sw = float2(fbm(wq * 0.6 + TIME * 0.02, 2), fbm(wq * 0.6 + 7.7 - TIME * 0.015, 2)) - 0.5;
            float3 rr = normalize(float3(rd.x + sw.x * 0.05, dn, rd.z + sw.y * 0.05));
            float3 m  = ngSkyBase(rr);
            float4 a  = NG_CLOUDS > 0.0 ? ngAlto(rr, Ld, true) : float4(0, 0, 0, 0);
            a.a *= NG_CLOUDS;
            m = lerp(m, a.rgb * lerp(float3(1.0, 1.0, 1.0), float3(0.075, 0.075, 0.15), NIGHT), a.a * 0.6);
            col = lerp(col, m * 0.55, wM);
        }
    }
    // the floor of the void, far below: light through the dome draws moving patches of caustic net on it
    float  tF = (ro.y - (SEA_Y - 260.0)) / max(dn, 0.02);
    float2 fp = (ro.xz + rd.xz * tF) / 55.0;
    float  fadeF = exp(-tF / 1500.0) * smoothstep(0.10, 0.45, dn);
    [branch] if (fadeF > 0.01 && QUALITY > 0.15) {
        float cs = ngCaustic(fp) * smoothstep(0.35, 0.75, fbm(fp * 0.25 + 3.0, 3));
        float3 cc = lerp(lerp(float3(0.30, 0.62, 1.00), float3(0.85, 0.40, 0.95), DUSK), float3(0.45, 0.30, 1.00), NIGHT);
        col += cc * cs * fadeF * lerp(0.16, 0.30, NIGHT);
    }
    // glitter: motes of glass hanging in the void, catching the light
    float3 gq = rd * 260.0, gc = floor(gq);
    float  hh = hash13(gc + 5.0);
    if (hh > 0.985) {
        float3 sp = normalize(gc + 0.5 + (ngHash3(gc) - 0.5) * 0.6);
        float  a = length(rd - sp) * 260.0;
        col += lerp(float3(0.75, 0.88, 1.0), ngSpecS(frac(hh * 91.0)), 0.5) * exp(-a * a * 6.0) * (0.5 + 0.5 * sin(TIME * (1.0 + 3.0 * hh) + hh * 40.0)) * smoothstep(0.03, 0.2, dn) * 0.9;
    }
    float  fadeH = smoothstep(0.015, 0.09, dn);
    [branch] if (fadeH > 0.0) {
        [unroll] for (int L = 3; L >= 0; L--) {
            float4 sh = ngShards(ro, rd, SEA_Y + 14.0 - 34.0 * float(L), 58.0 + 26.0 * float(L), 13.0 * float(L) + 2.0, Ld);
            col = col * (1.0 - sh.a * fadeH) + sh.rgb * fadeH;
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
    [loop] for (int k = 0; k < 6; k++) {
        float  hk = 1.0 + float(k) * 0.16;                          // stacked sheets: the folds hang down
        float2 p = rd.xz / (e + 0.05) * (0.55 / hk);
        float  v = p.y + 0.9 * sin(p.x * 0.55 + t * 2.0) + fbm(p * 0.45 + float2(t, 0.0), 3) * 1.6;
        float  band = exp(-pow(sin(v * 0.9) / 0.20, 2.0));
        float  rays = 0.55 + 0.45 * vnoise(float2(p.x * 9.0 + v * 2.0, t * 3.0));
        float3 c = lerp(float3(0.45, 0.30, 1.00), ngFilm(v * 0.12 + 0.35 + float(k) * 0.03) * float3(0.70, 1.00, 1.00), 0.55);
        acc += c * band * rays * (1.0 - float(k) / 6.0);
    }
    return acc * 0.055 * smoothstep(0.03, 0.20, e) * (1.0 - smoothstep(0.55, 0.95, e));
}
float3 ngBackground(float3 rd) {
    float3 Ld = PHASE > 1.45 ? SUN : SUNDISC;                         // the sun by day, the moon at night
    [branch] if (rd.y < 0.0) return ngVoid(CAM, rd, Ld);
    NgDome D  = ngDome(rd);
    float3 rf = D.rdF;
    float3 col = ngSkyBase(rf) * (0.965 + 0.07 * D.hcell.x);          // each pane a breath brighter or darker
    float  e = rd.y;
    col += ngSpecS(saturate(0.5 - e * 90.0)) * exp(-pow(e * 110.0 - 0.6, 2.0)) * lerp(0.06, 0.12, NIGHT) * (1.0 - 0.5 * DUSK);   // the dome's base ring
    float4 ci = ngCirrus(rf, Ld);
    float3 ciC = ci.rgb * lerp(lerp(float3(0.95, 0.97, 1.0), float3(0.95, 0.60, 0.70), DUSK), float3(0.10, 0.10, 0.22), NIGHT);
    col = lerp(col, ciC, ci.a * lerp(0.38, 0.18, NIGHT));   // (quieter: the altocumulus is the body now)
    float4 ac = NG_CLOUDS > 0.0 ? ngAlto(rf, Ld, false) : float4(0, 0, 0, 0);
    ac.a *= NG_CLOUDS;
    float3 acT = lerp(float3(1.0, 1.0, 1.0), float3(0.075, 0.075, 0.15), NIGHT);   // night: dark shapes that hide the stars
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
    [branch] if (NIGHT > 0.001) {
        col += (csStars(rd) * 1.25 * (1.0 - ac.a) + csMoon(rd) * (1.0 - ac.a * 0.85)) * NIGHT;   // clouds hide them
        col += ngVeil(rd) * NIGHT;
        float am = acos(clamp(dot(rd, SUN), -1.0, 1.0)), x2 = (am - 0.384) / 0.0175;
        col += float3(0.62, 0.66, 0.95) * (x2 < 0.0 ? exp(-x2 * x2 * 2.0) : exp(-x2 * 0.6)) * 0.035 * NIGHT * smoothstep(-0.02, 0.05, rd.y);   // the moon's ring
    }
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
