// Renderer Foundation - World FX: Cloud Sea sky (by Novak). Drawn in place of Bedrock's sky (SkyPass stage 2):
// a sunset sky, high clouds lit from below, and a sea of cloud tops far under the map.
// Same maths as the Cloud Sea lab (Bedrock Forge tools/WorldFx/lab/cloudsea.glsl) - keep the two in step.
// Compiled offline by tools/gen_worldfx.py -> src/WorldFxShaders.h.

cbuffer C : register(b0) {
    float4 c0; // xyz = camera forward, w = tan(fovx/2)
    float4 c1; // xyz = camera right,   w = tan(fovy/2)
    float4 c2; // xyz = camera up,      w = time (seconds)
    float4 c3; // xyz = camera position (world), w = cloud deck base (world y)
    float4 c4; // xy = map centre (world xz: the towers keep clear of it), z = far depth, w = 0
};
Texture2D    gNoise : register(t0); // 256x256 RGBA8 value noise (4 independent channels)
Texture2D    gPuff  : register(t1); // 1024x1024 R16 cloud-top height with mips (domes riding on domes)
SamplerState gSmp   : register(s0); // linear, wrap

static const float3 SUN     = float3(0.17605, 0.11247, -0.97794); // low sun, ~6.5 degrees up (north)
static const float  SEA_AMP = 24.0;
static float3 CAM;
static float  TIME, SEA_Y;
static float2 MAP_C;

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
float3 skyBase(float3 d) {
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
    float  dn = hcDens(p, 5);
    float  fine = fbm(p * 21.0, 3);
    float  near = smoothstep(0.05, 0.30, d.y);
    float  dens = smoothstep(0.765, 0.830, dn - (fine - 0.5) * 0.10 * near);
    float  tau = smoothstep(0.765, 0.965, dn);
    float  shadeS = smoothstep(0.765, 0.93, hcDens(p + sunXZ * 0.040, 4));
    float2 along = float2(dot(q, sunXZ), dot(q, float2(-sunXZ.y, sunXZ.x)));
    float2 pc = float2(along.x / 3400.0, along.y / 760.0) + TIME * float2(0.0015, 0.0);
    pc += (float2(fbm(pc * 1.7 + 9.0, 3), fbm(pc * 1.7 + 23.0, 3)) - 0.5) * 0.9;
    float cir = smoothstep(0.46, 0.80, fbm(pc * float2(1.0, 2.2), 5));
    cir *= cir * smoothstep(0.40, 0.66, fbm(q / 2300.0 + 40.0, 3));

    float  sunSide = mu * 0.5 + 0.5;
    float3 lit = lerp(float3(0.46, 0.130, 0.170), float3(1.10, 0.300, 0.050), pow(sunSide, 2.0));
    lit = lerp(lit, float3(1.55, 0.74, 0.20), pow(max(mu, 0.0), 9.0));
    float3 shade = lerp(float3(0.062, 0.052, 0.120), float3(0.120, 0.070, 0.135), sunSide);
    float  ripple = fbm(rot(p) * float2(9.0, 15.0), 3) - 0.5;
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
    float  tw = smoothstep(0.56, 0.78, vnoise(w / 760.0 + 2.7)) * far;
    return h * (0.42 + 0.55 * big) + big * 0.55 + tw * (1.6 + 3.6 * big + 1.5 * h);
}
float seaY(float2 w, float lod) { return SEA_Y + SEA_AMP * seaH(w, lod); }

float4 cloudSea(float3 ro, float3 rd) {
    float yTop = SEA_Y + SEA_AMP * 7.6;
    if (rd.y >= 0.0 && ro.y > yTop) return float4(0.0, 0.0, 0.0, 0.0);
    if (rd.y > 0.02) return float4(0.0, 0.0, 0.0, 0.0); // looking up: nothing tall enough out there to hit
    float t = 0.0;
    if (ro.y > yTop) t = (ro.y - yTop) / -rd.y;
    if (t > 3400.0) return float4(0.0, 0.0, 0.0, 0.0);
    bool  hit = false;
    float lod = 0.0, tPrev = t;
    [loop] for (int i = 0; i < 80; i++) {
        float3 p = ro + rd * t;
        lod = max(log2(t * 0.011), 0.0);
        float dh = p.y - seaY(p.xz, lod);
        if (dh < 0.004 * t + 0.03) { hit = true; break; }
        tPrev = t;
        t += max(dh * 0.40, 0.008 * t + 0.30);
        if (t > 3400.0 || (rd.y > 0.0 && p.y > yTop)) break;
    }
    if (!hit) return float4(0.0, 0.0, 0.0, 0.0);
    [unroll] for (int k = 0; k < 6; k++) {
        float  tm = 0.5 * (tPrev + t);
        float3 pm = ro + rd * tm;
        if (pm.y - seaY(pm.xz, lod) < 0.0) t = tm; else tPrev = tm;
    }
    float3 p = ro + rd * t;
    float  e = 2.2 + t * 0.006;
    float  h0 = seaY(p.xz, lod);
    float  nl = lod + 1.2;
    float  hc0 = seaY(p.xz, nl);
    float3 n = normalize(float3(hc0 - seaY(p.xz + float2(e, 0.0), nl), e, hc0 - seaY(p.xz + float2(0.0, e), nl)));
    float2 l2 = normalize(SUN.xz);
    float  slope = SUN.y / length(SUN.xz);
    float  sh = 1.0;
    [unroll] for (int j = 0; j < 5; j++) {
        float s = 7.0 * pow(2.3, float(j));
        float over = seaY(p.xz + l2 * s, lod + 1.0 + float(j) * 0.6) - (h0 + slope * s);
        sh *= 1.0 - smoothstep(-0.05 * s, 0.20 * s, over) * 0.78;
    }
    float ndl = dot(n, SUN);
    float grain = fbm(p.xz / 5.0 + 3.0, 4) + 0.5 * vnoiseG(p.xz / 1.3);
    grain = lerp(0.75, grain, exp(-t / 260.0));
    float cav = saturate((h0 - seaY(p.xz, lod + 3.4)) / (SEA_AMP * 0.13) + 0.55);
    float lit = sh * (0.30 + 0.70 * smoothstep(-0.35, 0.55, ndl)) * (0.72 + 0.38 * grain) * (0.34 + 0.66 * cav);
    float crease = smoothstep(0.05, 0.85, (h0 - SEA_Y) / SEA_AMP);
    float mu = dot(rd, SUN);
    float3 amb = lerp(float3(0.020, 0.023, 0.070), float3(0.066, 0.062, 0.155), crease) * (0.60 + 0.40 * n.y) * (0.85 + 0.2 * grain);
    amb += float3(0.22, 0.075, 0.040) * max(dot(normalize(n.xz + 1e-4), l2), 0.0) * (1.0 - n.y) * 0.5 * crease;
    float  phase = lerp(0.60, 1.0, smoothstep(-1.0, 0.35, mu));
    float3 direct = lerp(float3(0.95, 0.36, 0.20), float3(1.00, 0.335, 0.105), smoothstep(-1.0, 0.35, mu)) * 1.02 * lit * phase;
    float  crest = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);
    float3 through = float3(1.5, 0.62, 0.18) * crest * pow(max(mu, 0.0), 5.0) * sh * 0.9;
    float3 col = amb + direct + through;
    float  f = saturate((1.0 - exp(-t / 1000.0)) / (1.0 - exp(-3.4)));
    float3 hz = skyBase(normalize(float3(rd.x, max(rd.y, 0.0) + 0.030, rd.z)));
    col = lerp(col, hz, f * f * (3.0 - 2.0 * f));
    return float4(col, 1.0);
}

float3 toDisplay(float3 c) {
    c = max(c, 0.0);
    float3 k = max(c - 0.55, 0.0);
    c = min(c, 0.55) + k / (1.0 + k * 1.9);
    return pow(c, 1.0 / 2.2);
}
float hash12(float2 p) { p = frac(p * float2(0.1031, 0.1030)); p += dot(p, p.yx + 33.33); return frac((p.x + p.y) * p.x); }

struct VSOut { float4 pos : SV_Position; float3 dir : TEXCOORD0; };
VSOut VSCloudSea(uint id : SV_VertexID) {
    float2 v = float2(id == 2 ? 3.0 : -1.0, id == 1 ? 3.0 : -1.0);
    VSOut  o;
    o.pos = float4(v, c4.z, 1.0);
    o.dir = c0.xyz + c1.xyz * (v.x * c0.w) + c2.xyz * (v.y * c1.w);
    return o;
}
float4 PSCloudSea(VSOut i) : SV_Target {
    CAM = c3.xyz; TIME = c2.w; SEA_Y = c3.w; MAP_C = c4.xy;
    float3 rd = normalize(i.dir);
    float  mu = dot(rd, SUN);
    float  ang = acos(clamp(mu, -1.0, 1.0));
    float3 col = skyBase(rd);
    float4 hc = highClouds(rd, mu);
    float  disc = smoothstep(0.0125, 0.0095, ang);
    float3 sun = float3(3.2, 2.3, 1.25) * disc + float3(1.5, 0.78, 0.30) * exp(-ang * 38.0) * 0.8;
    col += sun * (1.0 - hc.a * 0.85);
    col = col * (1.0 - hc.a) + hc.rgb;
    float4 sea = cloudSea(CAM, rd);
    if (sea.a > 0.0) col = sea.rgb;
    // the wide sun glow (the lab draws it in its light-shaft pass; until the in-game shaft pass exists it lives here)
    col += float3(1.0, 0.47, 0.17) * 0.42 * exp(-ang * 3.4) * (1.0 - 0.5 * hc.a);
    float3 o = toDisplay(col);
    o += (hash12(i.pos.xy) - 0.5) / 255.0 + (hash12(i.pos.yx + 7.0) - 0.5) / 255.0; // dither: no banding
    return float4(o, 1.0);
}
