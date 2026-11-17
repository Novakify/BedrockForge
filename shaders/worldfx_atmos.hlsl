// Renderer Foundation - World FX: Haze + Mist, drawn on the world's own targets right after the World FX pass (or on
// their own with any other sky: vanilla, LyricsForge), before glass / water / the hand / the HUD. Original code for
// Renderer Foundation (by Novak).
//
// Reference: the Purxiy client's "Haze" and "Mist" (Reina0613 / OwlStorm / 1j01 - their code is locked; their settings
// and their descriptions were the brief, the light model is ours):
//   Haze  "a soft haze of colour over the world around you": colour, strength, height above the ground, a clear space
//         around you, drift, a glow toward the sun and moon, players and mobs keep their colours
//   Mist  "soft autumn mist that hangs low in the world": density, height, clear space, banks that roll by, warm sun
//         rays through it, players and mobs show clearly through it
//
// Both are integrals along the view ray, not a screen tint:
//   * haze: exponential height fog, solved in closed form (no march): thick at the ground, thinning above it; the
//     colour drifts slowly between the palette's two colours across the world (low-frequency noise in world space)
//   * mist: a thin low layer with banks in it - a short jittered march (6-12 taps, by distance) through 3D value noise
//     that rolls with the wind; forward scattering lights it toward the sun, and beams cut through it
// Everything in linear light; the clear radius fades in softly so there is never a visible wall.
cbuffer C : register(b0) {
    float4 c0;  // xyz = camera forward, w = tan(fovx/2)
    float4 c1;  // xyz = camera right,   w = tan(fovy/2)
    float4 c2;  // xyz = camera up,      w = time (s)
    float4 c3;  // x = flags (1 BGRA, 2 haze on, 4 mist on, 8 haze spares creatures, 16 mist spares creatures), y = ground (world y), z = quality 0..1, w = frame seed
    float4 c4;  // xy = target size, z = scene row pitch (bytes), w = depth row pitch (bytes)
    float4 c5;  // xyz = camera position (world, wrapped for noise), w = time of day (0 noon .. 1)
    float4 c6;  // haze colour A (linear), w = strength
    float4 c7;  // haze colour B (linear), w = height (blocks)
    float4 c8;  // haze: x = clear radius, y = drift, z = glow, w = -
    float4 c9;  // mist colour (linear), w = density
    float4 c10; // mist: x = height, y = clear radius, z = drift, w = rays
};
cbuffer A : register(b1) {   // the things that move by themselves (from Bedrock Forge): upright cylinders, camera-relative
    float4 aInfo;
    float4 aPos[24];
    float4 aDim[24];
    // Ambience (Purxiy "glowing particles drifting through the world around you"), appended after the blur's layout
    float4 amb0;   // rgb = colour A (linear), w = amount 0..2 (0 = off)
    float4 amb1;   // rgb = colour B (linear), w = size
    float4 amb2;   // x = glow, y = speed, z = twinkle, w = range (blocks)
    float4 amb3;   // x = style (0 motes, 1 stardust, 2 petals, 3 snow), y = 1: near ones melt into bokeh, z = 1: they swirl aside, w = daylight share
    // hit / jump effects (Bedrock Forge BF_GetFxEvents): camera-relative position, w = age (s)
    float4 evP[16];
    float4 evK[16];  // x = kind (1 jump, 2 hit, 3 crit, 4 landing), y = a (landing: fall speed), z = seed
    float4 fx0;      // jump circles: rgb, w = size (0 = off)
    float4 fx1;      // x = jump duration, y = jump glow, z = splash size (0 = off), w = splash duration
    float4 fx2;      // splash colour A, w = splash glow
    float4 fx3;      // splash colour B, w = ripple glow (0 = off)
    float4 fx4;      // ripple colour, w = ripple duration
    float4 fx5;      // x = ripple ring, y = crit stars, z = shockwave size (0 = off), w = shockwave glow
    float4 fx6;      // shockwave colour, w = events
    // cosmetics around you (Purxiy trail / aura / halo): your feet and your path, camera-relative
    float4 pl;       // xyz = your feet, w = yaw (radians); pl.w < -50: unknown
    float4 trP[32];  // the trail: xyz = a point of your path, w = age (s), newest first
    float4 cs0;      // trail colour, w = duration (s) (0 = off)
    float4 cs1;      // x = width, y = glow, z = style (0 line, 1 path, 2 helix, 3 stardust), w = points
    float4 au0;      // aura colour, w = size (0 = off)
    float4 au1;      // x = speed, y = glow, z = style (0 orbit, 1 fireflies, 2 spiral, 3 constellation), w = 1: you see it first person
    float4 ha0;      // halo colour, w = size (0 = off)
    float4 ha1;      // x = glow, y = height above the head, z = design (0 seraph, 1 sigil, 2 crown, 3 stardust, 4 eclipse, 5 fallen), w = 1: first person
    // Chroma (Purxiy): the world waits in grey, colour blooms where you walk
    float4 chP[48];  // where you have been: xyz camera-relative, w = age (s)
    float4 ch0;      // rgb = the tone the world waits in (linear), w = how much colour it keeps (-1 = Chroma off)
    float4 ch1;      // x = brush (blocks), y = memory (s), z = 1: living things keep their colour, w = points
    // Radiance (Purxiy): glowing outlines on players / mobs / items
    float4 rad0;     // rgb = colour (linear), w = width (px)
    float4 rad1;     // x = glow, y = fill, z = flow speed, w = 1: on
    // Weather (Purxiy): glassy rain, soft snow, embers
    float4 wx0;      // x = mode (0 rain, 1 snow, 2 fire), y = intensity, z = wind, w = size
    float4 wx1;      // x = range (blocks), w = 1: on
    // Bokeh (Purxiy): sharp where you look, creamy light behind
    float4 bk0;      // x = focus mode (0 auto, 1 distance, 2 tilt-shift), y = focus distance, z = sharp band 0..1, w = blur (0 = off)
    float4 bk1;      // x = highlights, y = 1: nearer than the focus blurs too, z = fringe, w = -
    // Place Animation (Purxiy): light on a block as it is placed / broken (events kind 5 / 6)
    float4 pa0;      // rgb = colour (linear), w = duration (s) (0 = off)
    float4 pa1;      // x = glow, y = 1: broken blocks too
    float4 trB;      // the trail's bounding sphere: xyz centre (camera-relative), w = radius (0 = none)
    float4 wxL;      // lightning: x = flash 0..1, y = bolt azimuth (rad), z = near 0..1, w = bolt age (s) (-1 = none)
};
ByteAddressBuffer gScene : register(t0);
ByteAddressBuffer gDepth : register(t1);

int2   clampPx(int2 p) { return clamp(p, int2(0, 0), int2(c4.xy) - 1); }
float  rawDepth(int2 p) { p = clampPx(p); return float(gDepth.Load(uint(p.y) * uint(c4.w) + uint(p.x) * 4u) & 0xFFFFFFu) / 16777215.0; }
float3 sceneAt(int2 p) {
    p = clampPx(p);
    uint  v = gScene.Load(uint(p.y) * uint(c4.z) + uint(p.x) * 4u);
    float3 c = float3(v & 255u, (v >> 8) & 255u, (v >> 16) & 255u) / 255.0;
    return (uint(c3.x) & 1u) != 0u ? c.bgr : c;
}
float viewZ(float d) { return 0.025 / max(1.0 - d, 1e-7); }
bool  isSky(float d) { return d >= 0.99999; }
float ign(float2 p) { return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715)))); }
float hash13(float3 p) { p = frac(p * 0.1031); p += dot(p, p.zyx + 31.32); return frac((p.x + p.y) * p.z); }
float vnoise3(float3 p) {
    float3 i = floor(p), f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = lerp(lerp(hash13(i), hash13(i + float3(1, 0, 0)), f.x), lerp(hash13(i + float3(0, 1, 0)), hash13(i + float3(1, 1, 0)), f.x), f.y);
    float b = lerp(lerp(hash13(i + float3(0, 0, 1)), hash13(i + float3(1, 0, 1)), f.x), lerp(hash13(i + float3(0, 1, 1)), hash13(i + float3(1, 1, 1)), f.x), f.y);
    return lerp(a, b, f.z);
}
float fbm2(float3 p) { return vnoise3(p); }   // (one octave: the second cost as much as the rest of the mist and was not seen)
float hash12b(float2 p) { float3 q = frac(float3(p.xyx) * 0.1031); q += dot(q, q.yzx + 33.33); return frac((q.x + q.y) * q.z); }
float vnoise2(float2 p) {
    float2 i = floor(p), f = frac(p); f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(hash12b(i), hash12b(i + float2(1, 0)), f.x), lerp(hash12b(i + float2(0, 1)), hash12b(i + float2(1, 1)), f.x), f.y);
}

// 1 = open world, 0 = inside a player / mob / item box
float stillWorld(float3 q) {
    float k = 1.0;
    int   n = min(int(aInfo.x), 24);
    [loop] for (int i = 0; i < n; ++i) {
        float3 d = q - aPos[i].xyz;
        if (d.y < -0.2 || d.y > aDim[i].x + 0.1) continue;
        k = min(k, smoothstep(aPos[i].w - 0.05, aPos[i].w + 0.10, length(d.xz)));
    }
    return k;
}

struct VSOut { float4 pos : SV_Position; };

// optical depth of exponential height fog exp(-(y - g) / H) from the camera along rd for t blocks (closed form)
float hazeDepth(float camY, float3 rd, float t, float g, float H) {
    float y0 = (camY - g) / H;
    float k  = rd.y * t / H;
    float e0 = exp(-max(y0, -8.0));
    return abs(k) < 1e-4 ? e0 * t : e0 * t * (1.0 - exp(-k)) / k;
}

float3 hash33(float3 p) {
    p = frac(p * float3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return frac((p.xxy + p.yxx) * p.zyx);
}
// Ambience: one light per 2-block cell (if the cell's dice say so), found by walking the view ray through the cells
// (a 3D DDA, at most ~28 cells). Snow and petals fall: the grid itself scrolls, so they never pop at a cell edge.
float3 ambience(float3 rd, float dist, float night, float t, float2 px) {
    float amount = amb0.w;
    if (amount <= 0.0) return 0;
    int    style = int(amb3.x + 0.5);
    float  C     = style == 1 ? 1.5 : 2.0;
    float  range = amb2.w;
    float  dayK  = lerp(amb3.w, 1.0, night);                     // how many stay out by day
    float  sp    = amb2.y;
    float3 scroll = style == 3 ? float3(0.25, 1.1, 0.10) * t * sp       // snow: falls, a little wind
                  : style == 2 ? float3(0.45, 0.55, 0.20) * t * sp      // petals: drift down and along
                  : float3(0.08, -0.05, 0.06) * t * sp;                // motes / stardust: a slow drift
    float3 o   = c5.xyz + scroll;                                  // the ray in the scrolled grid
    float  tMax = min(dist, range);
    float3 cell = floor(o / C);
    float3 stp  = sign(rd);
    float3 inv  = 1.0 / max(abs(rd), 1e-5);
    float3 nb   = (cell + max(stp, 0.0)) * C;
    float3 tNext = abs(nb - o) * inv;
    float3 tDel  = C * inv;
    float3 acc = 0;
    float  tt  = 0;
    [loop] for (int k = 0; k < 22 && tt < tMax; ++k) {
        float3 h = hash33(cell + 0.37);
        if (h.x < amount * 0.5 * dayK) {
            float3 m  = (cell + 0.2 + 0.6 * hash33(cell * 1.71 + 4.1)) * C;
            float  ph = h.y * 6.2831;
            m += float3(sin(t * 0.7 * sp + ph), sin(t * 0.5 * sp + ph * 1.3) * 0.6, cos(t * 0.6 * sp + ph)) * 0.18 * C * (style >= 2 ? 0.6 : 1.0);
            float3 rel = m - o;                                       // camera-relative
            if (amb3.z > 0.5) {                                       // they swirl aside as you move through them
                float r0 = length(rel);
                rel += normalize(rel + 1e-4) * 0.9 * exp(-r0 * r0 * 0.6);
            }
            float tp = dot(rel, rd);
            if (tp > 0.25 && tp < tMax) {
                float  dd   = length(rel - rd * tp);
                float  size = amb1.w * (style == 1 ? 0.035 : style == 3 ? 0.07 : style == 2 ? 0.10 : 0.075) * (0.6 + 0.8 * h.z) * (1.0 + tp * 0.035);   // (seen in game at 4K: the first sizes read as dust; far ones grow a little so they stay visible)
                float  coc  = amb3.y > 0.5 ? max(0.0, 2.2 - tp) * 0.03 : 0.0;   // near ones: a soft round bokeh
                float  r    = size + coc;
                float  core = exp(-dd * dd / (r * r));
                float  halo = amb2.x * exp(-dd / (r * 4.0)) * 0.22;
                float  tw   = 1.0 - amb2.z * (0.5 + 0.5 * sin(t * (2.0 + 3.0 * h.y) + ph * 3.0));   // twinkle / breathe
                float  fade = smoothstep(tMax, tMax * 0.65, tp) * (r > size ? size * size / (r * r) * 1.6 + 0.12 : 1.0);
                float3 cc   = lerp(amb0.rgb, amb1.rgb, h.z);
                if (style == 3) cc = lerp(float3(0.85, 0.90, 1.0), cc, 0.25);   // snow stays white
                acc += cc * (core * 2.2 + halo * 1.6) * tw * fade * lerp(0.75, 1.0, night);
            }
        }
        // the next cell along the ray
        if (tNext.x < tNext.y && tNext.x < tNext.z) { tt = tNext.x; tNext.x += tDel.x; cell.x += stp.x; }
        else if (tNext.y < tNext.z) { tt = tNext.y; tNext.y += tDel.y; cell.y += stp.y; }
        else { tt = tNext.z; tNext.z += tDel.z; cell.z += stp.z; }
    }
    return acc;
}

// Jump Circles, Hit Splash, Ripple, Shockwave: light painted onto whatever surface is there (the pixel's own world
// point from the depth buffer), so rings follow steps and slopes instead of floating on a flat disc.
float3 worldEvents(float3 P, bool sky, float t) {
    int n = min(int(fx6.w), 16);
    if (n <= 0 || sky) return 0;
    float3 L = 0;
    [loop] for (int i = 0; i < n; ++i) {
        float3 c = evP[i].xyz; float age = evP[i].w; int kind = int(evK[i].x + 0.5); float seed = evK[i].z;
        float3 dv = P - c;
        if (kind == 1 && fx0.w > 0.0) {                        // jump: one ring spreading from your feet
            float u = age / max(fx1.x, 0.05);
            if (u < 1.0 && abs(dv.y) < 0.6) {
                float r = length(dv.xz), R = fx0.w * 1.4 * (1.0 - pow(1.0 - u, 3.0)), w = 0.07 + 0.10 * u;
                float k = exp(-pow((r - R) / w, 2.0)) + fx1.y * 0.35 * exp(-abs(r - R) / (w * 3.0));
                L += fx0.rgb * k * pow(1.0 - u, 1.4) * 1.6 * smoothstep(0.6, 0.2, abs(dv.y));
            }
        } else if ((kind == 2 || kind == 3) && (fx1.z > 0.0 || fx3.w > 0.0)) {
            if (fx1.z > 0.0 && dv.y < 0.3 && dv.y > -2.6) {       // splash: ink on the ground under the hit, fading slowly
                float u = age / max(fx1.w, 0.1);
                if (u < 1.0) {
                    float2 q = dv.xz;
                    float  r = length(q), an = atan2(q.y, q.x);
                    float  rad = 0.55 * fx1.z * (0.72 + 0.20 * sin(an * 3.0 + seed * 6.3) + 0.12 * sin(an * 7.0 + seed * 11.0)) * (0.55 + 0.45 * smoothstep(0.0, 0.08, u));
                    float  blob = smoothstep(rad, rad * 0.82, r);
                    float  drops = 0.0;
                    [unroll] for (int d = 0; d < 5; ++d) {           // droplets thrown out round it
                        float a2 = seed * 17.0 + float(d) * 1.9, dr = rad * (1.25 + 0.5 * frac(seed * 7.3 + float(d) * 0.37));
                        float2 dp = float2(cos(a2), sin(a2)) * dr;
                        drops = max(drops, smoothstep(0.075 * fx1.z, 0.045 * fx1.z, length(q - dp)));
                    }
                    float ink = max(blob, drops) * smoothstep(1.0, 0.6, u) * smoothstep(0.0, 1.1, 2.6 + dv.y) ;
                    float3 cc = lerp(fx2.rgb, fx3.rgb, 0.5 + 0.5 * sin(an * 2.0 + seed * 5.0));
                    L += cc * ink * (0.25 + 0.9 * fx2.w * (1.0 - u * 0.7)) * (1.0 + 2.0 * exp(-age * 6.0));
                }
            }
            if (fx3.w > 0.0) {                                   // ripple: a flash, and a wave of light over what is near
                float u = age / max(fx4.w, 0.05);
                if (u < 1.0) {
                    float d3 = length(dv), R = 2.6 * (1.0 - pow(1.0 - u, 2.0));
                    float k = exp(-pow((d3 - R) / 0.16, 2.0)) * (1.0 - u) + exp(-d3 * 2.2) * exp(-age * 9.0) * 1.5;
                    if (fx5.x > 0.5 && dv.y < 0.2 && dv.y > -2.4) {   // the ring rolling over the ground
                        float r = length(dv.xz), R2 = 3.2 * (1.0 - pow(1.0 - u, 2.5));
                        k += exp(-pow((r - R2) / 0.10, 2.0)) * (1.0 - u) * 0.8;
                    }
                    if (kind == 3 && fx5.y > 0.5) k *= 1.8;          // a critical hit lands harder
                    L += fx4.rgb * k * fx3.w * 1.4;
                }
            }
        } else if ((kind == 5 || (kind == 6 && pa1.y > 0.5)) && pa0.w > 0.0) {   // a block placed / broken: light on its faces
            float u = age / pa0.w;
            if (u < 1.0) {
                float3 q3 = dv - 0.5;                                   // from the block's middle
                float  box = max(max(abs(q3.x), abs(q3.y)), abs(q3.z));
                if (box < 0.56) {
                    float edge = smoothstep(0.38, 0.5, max(max(min(abs(q3.x), abs(q3.y)), min(abs(q3.y), abs(q3.z))), min(abs(q3.x), abs(q3.z))));
                    float k = kind == 5 ? (1.0 - u) * (0.35 + 0.65 * edge) * (1.0 + 1.5 * exp(-age * 18.0))
                                        : smoothstep(0.0, 0.15, u) * (1.0 - u) * edge;   // breaking: the edges flare as it goes
                    L += pa0.rgb * k * (0.5 + pa1.x * 1.3);
                }
            }
        } else if (kind == 4 && fx5.z > 0.0) {                    // landing: a wave runs out through the ground
            float u = age / 0.9;
            if (u < 1.0 && abs(dv.y) < 0.8) {
                float s = saturate(evK[i].y / 1.2);
                float r = length(dv.xz);
                float R1 = fx5.z * (1.8 + 2.5 * s) * (1.0 - pow(1.0 - u, 2.2)), R2 = R1 * 0.62;
                float k = exp(-pow((r - R1) / 0.14, 2.0)) + 0.55 * exp(-pow((r - R2) / 0.10, 2.0));
                k += fx5.w * 0.3 * exp(-abs(r - R1) / 0.5);
                L += fx6.rgb * k * pow(1.0 - u, 1.6) * (0.6 + 0.8 * s) * smoothstep(0.8, 0.3, abs(dv.y));
            }
        }
    }
    return L;
}

// ---- cosmetics: trail on the ground behind you, an aura of lights around you, a halo over your head
float segDist2(float2 p, float2 a, float2 b, out float h) {
    float2 pa = p - a, ba = b - a;
    h = saturate(dot(pa, ba) / max(dot(ba, ba), 1e-5));
    float2 d = pa - ba * h;
    return dot(d, d);
}
float3 cosmetics(float3 P, float3 rd, float dist, bool sky, float t) {
    float3 L = 0;
    if (pl.w < -50.0) return L;
    bool const fp = length(pl.xyz + float3(0, 1.62, 0)) < 0.6;   // the camera is in your head: first person
    // trail: a glowing ribbon painted on the ground along your path (surfaces near the path's height)
    int n = min(int(cs1.w), 32);
    float3 tbd = P - trB.xyz;
    [branch] if (cs0.w > 0.0 && n > 1 && !sky && dot(tbd, tbd) < trB.w * trB.w) {   // audit: only near the path
        float best = 1e9, bestAge = 0.0, bestY = 0.0;
        [loop] for (int i = 0; i + 1 < n; ++i) {
            float h;
            float d2 = segDist2(P.xz, trP[i].xz, trP[i + 1].xz, h);
            if (d2 < best) { best = d2; bestAge = lerp(trP[i].w, trP[i + 1].w, h); bestY = lerp(trP[i].y, trP[i + 1].y, h); }
        }
        float u = bestAge / max(cs0.w, 0.05);
        if (u < 1.0 && abs(P.y - bestY) < 0.6) {
            float w = 0.10 * cs1.x * (cs1.z > 0.5 && cs1.z < 1.5 ? 2.2 : 1.0), d = sqrt(best);
            float core = exp(-pow(d / w, 2.0)), halo = cs1.y * 0.35 * exp(-d / (w * 3.0));
            float k = (core + halo) * pow(1.0 - u, 1.3) * smoothstep(0.6, 0.15, abs(P.y - bestY));
            if (cs1.z > 2.5) k *= step(0.7, frac(sin(dot(floor(P.xz * 9.0), float2(12.9898, 78.233))) * 43758.5453)) * 2.5;   // stardust: sparks
            L += cs0.rgb * k * 1.4;
        }
    }
    // aura / halo are lights in the air: closest approach of the view ray to each (depth-tested against the world)
    [branch] if (au0.w > 0.0 && (!fp || au1.w > 0.5)) {
        int style = int(au1.z + 0.5);
        int N = style == 1 ? 9 : 6;
        float3 c = pl.xyz + float3(0, 1.0, 0);
        float  rb = 0.75 * au0.w * 1.2 + 1.0 + 0.2 * au0.w;          // audit: the ray must pass the aura's sphere at all
        float  tc = dot(c, rd);
        float3 cp = c - rd * tc;
        if (dot(cp, cp) > rb * rb || tc < -rb) N = 0;
        [loop] for (int i = 0; i < N; ++i) {
            float fi = float(i), ph = fi / float(N) * 6.2831 + t * 0.9 * au1.x;
            float r = 0.75 * au0.w, yy = 0.0;
            if (style == 1) { ph = fi * 2.4 + t * 0.4 * au1.x * (0.6 + frac(fi * 0.37)); r *= 0.6 + 0.6 * frac(fi * 0.71); yy = 0.8 * sin(t * 0.7 * au1.x + fi * 1.7); }
            else if (style == 2) yy = frac(fi / float(N) + t * 0.15 * au1.x) * 2.0 - 1.0;
            else yy = 0.25 * sin(ph * 2.0);
            float3 q = c + float3(cos(ph) * r, yy, sin(ph) * r);
            float tp = dot(q, rd);
            if (tp < 0.2 || tp > dist + 0.3) continue;
            float d = length(q - rd * tp), s = 0.035 * au0.w;
            float tw = 0.75 + 0.25 * sin(t * 3.0 + fi * 2.1);
            L += au0.rgb * (exp(-d * d / (s * s)) * 1.8 + au1.y * 0.25 * exp(-d / (s * 5.0))) * tw;
        }
    }
    [branch] if (ha0.w > 0.0 && (!fp || ha1.w > 0.5)) {
        float3 hc = pl.xyz + float3(0, 1.95 + 0.15 * ha1.y + 0.03 * sin(t * 1.6), 0);   // audit: hugs the head (was 0.4 above it)
        // the halo's plane (horizontal, a slight tilt); the ray meets it at one point: distance to the ring there
        float3 nrm = normalize(float3(0.08 * sin(pl.w), 1, 0.08 * cos(pl.w)));
        float den = dot(rd, nrm);
        if (abs(den) > 1e-3) {
            float tp = dot(hc, nrm) / den;
            if (tp > 0.2 && tp < dist + 0.3) {
                float3 hp = rd * tp - hc;
                float R = 0.28 * ha0.w, rr = length(hp - nrm * dot(hp, nrm));
                int dz = int(ha1.z + 0.5);
                float wdt = 0.025 * ha0.w;
                float k = exp(-pow((rr - R) / wdt, 2.0));
                float an = atan2(hp.z, hp.x);
                if (dz == 1) k += 0.7 * exp(-pow((rr - R * 0.72) / (wdt * 0.6), 2.0)) * step(0.5, frac(an * 1.9099));        // sigil: an inner broken ring
                else if (dz == 2) k *= 0.6 + 0.8 * pow(0.5 + 0.5 * cos(an * 6.0), 6.0);                                       // crown: points
                else if (dz == 3) k *= step(0.55, frac(sin(floor(an * 9.0) * 91.3) * 43758.5));                             // stardust: beads
                else if (dz == 4) k = exp(-pow((rr - R) / (wdt * 2.2), 2.0)) * (0.35 + 0.65 * smoothstep(-1.0, 1.0, sin(an - t)));   // eclipse
                else if (dz == 5) k *= 0.5 + 0.5 * step(0.15, frac(an * 0.159 + 0.1));                                       // fallen: a broken arc
                L += ha0.rgb * (k * 2.0 + ha1.x * 0.3 * exp(-abs(rr - R) / (wdt * 6.0)));
            }
        }
    }
    return L;
}

float lum3(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
float3 chroma(float3 c, float3 P, bool sky, float spareC) {
    float k = max(1.0 - spareC, ch0.w);                            // living things keep their colour
    int n = min(int(ch1.w), 48);
    float const R2 = (ch1.x + 2.5) * (ch1.x + 2.5);
    [loop] for (int i = 0; i < n && k < 0.999; ++i) {               // chP.w = the fade (CPU), 1 fresh .. 0 forgotten
        float3 d = P - chP[i].xyz;
        if (dot(d, d) > R2 || chP[i].w <= k) continue;               // too far, or cannot raise k
        float  r = length(float2(d.x, d.z)) + max(abs(d.y) - 2.5, 0.0);
        k = max(k, chP[i].w * smoothstep(ch1.x, ch1.x * 0.45, r));
    }
    float3 grey = lum3(c) * ch0.rgb / max(lum3(ch0.rgb), 0.05);
    return lerp(grey, c, saturate(k));
}
float3 radiance(int2 p, float3 P, float zc) {
    // inside a player / mob / item box, where the depth behind jumps away: the silhouette
    if (stillWorld(P) > 0.5) return 0;
    float w = rad0.w;
    float zN = 1e9;
    [unroll] for (int k = 0; k < 4; ++k) {
        int2 o = k == 0 ? int2(1, 0) : k == 1 ? int2(-1, 0) : k == 2 ? int2(0, 1) : int2(0, -1);
        float zz = viewZ(rawDepth(p + o * int(w)));
        zN = min(zN, zz > zc + 0.6 ? 0.0 : 1e9);
    }
    float edge = zN < 1.0 ? 1.0 : 0.0;
    float flow = 0.6 + 0.4 * sin(c2.w * 2.0 * rad1.z + (P.x + P.y + P.z) * 3.0);
    return rad0.rgb * (edge * (1.2 + rad1.x) * flow + rad1.y * 0.25);
}
// weather: drops / flakes / embers in 1.5-block cells along the view ray (a DDA like the ambience), depth-tested
// weather (audit 2: the 3D cell walk cost 1.9 ms and still read as a few stray streaks): rain / snow / embers on
// cylinders around you at four distances. Each cylinder is cut into thin columns; each column carries drops that fall
// (or rise) through it. A drop is drawn only if that cylinder is nearer than the world at this pixel, so it is
// depth-correct. Four layers make the depth; the near layer is sparse and big, the far ones dense and fine. ~0.3 ms.
float3 weather(float3 rd, float dist, float t, float night) {
    int   mode = int(wx0.x + 0.5);
    float lh   = length(rd.xz);
    if (lh < 1e-3) return 0;
    float3 L = 0;
    float  ang = atan2(rd.z, rd.x);
    float  slope = rd.y / lh;                                    // height per block of horizontal distance
    [unroll] for (int k = 0; k < 4; ++k) {
        float R = float(k == 0 ? 2.5 : k == 1 ? 5.0 : k == 2 ? 10.0 : 20.0) * (wx1.x / 24.0);
        if (R / lh > dist) break;                                // the world is nearer: this layer (and the farther ones) is hidden
        float y = slope * R;                                     // the height of the ray where it meets this cylinder
        float cols = (mode == 0 ? 520.0 : 300.0) * (0.45 + 0.55 * float(k)) * wx0.y;
        float wa = ang + wx0.z * (mode == 0 ? 0.12 : 0.05) * y / R; // wind leans them
        float u  = wa / 6.2831853 * cols;
        float c  = floor(u), fu = frac(u) - 0.5;
        float h  = frac(sin(c * 12.9898 + float(k) * 78.233) * 43758.5453);
        float h2 = frac(h * 91.17 + 0.31);
        float sp = mode == 0 ? 13.0 : mode == 1 ? 1.1 : -1.4;
        float per = mode == 0 ? 9.0 : 5.0;
        float yy = y + t * sp * (0.85 + 0.3 * h2) + h * per;
        float ph = frac(yy / per) * per;                          // where in its drop period this ray is
        float width = (mode == 0 ? 0.08 : 0.30) * wx0.w / (1.0 + float(k) * 0.4);
        float len   = (mode == 0 ? 0.9 : 0.12) * wx0.w * (1.2 - 0.15 * float(k));
        float across = exp(-pow(fu / width, 2.0));
        float along  = mode == 0 ? smoothstep(0.0, 0.25, ph) * smoothstep(len, len * 0.6, ph)
                                 : exp(-pow((ph - len) / max(len * 0.7, 0.02), 2.0));
        if (h2 > 0.55 * wx0.y + 0.15) along = 0;                   // not every column carries one
        float fade = 1.0 - float(k) * 0.18;
        float3 cc = mode == 0 ? lerp(float3(0.62, 0.70, 0.84), float3(0.20, 0.24, 0.36), night)
                  : mode == 1 ? float3(0.92, 0.95, 1.0) * lerp(1.0, 0.35, night)
                              : float3(1.0, 0.42, 0.10) * (1.3 + 0.7 * sin(t * 7.0 + h * 40.0));
        L += cc * across * along * fade * (mode == 0 ? 0.30 : mode == 1 ? 0.75 : 1.1);
    }
    return L;
}

// Bokeh: a disc gather from the scene copy, radius from how far the pixel is from the focus; bright taps weigh more
// bokeh (audit: the blur read too weak, max 14 px at 4K, and sharp near edges bled halos into the blur behind them).
// Now ~22 px at blur 0.5, and a nearer sample only counts if its own blur reaches this pixel (gather-as-scatter).
float bokehCoc(float z, float zf, int mode, int2 p) {
    if (mode == 2) return smoothstep(bk0.z * 0.5, 0.5, abs(float(p.y) / c4.y - 0.55));   // tilt-shift: a sharp band across the screen
    float rel = (z - zf) / max(zf, 0.5);
    if (bk1.y < 0.5 && rel < 0.0) rel = 0.0;
    return smoothstep(bk0.z * 0.6, bk0.z * 0.6 + 0.8, abs(rel));
}
float3 bokeh(int2 p, float z, bool sky) {
    int mode = int(bk0.x + 0.5);
    float zf = mode == 1 ? bk0.y : min(viewZ(rawDepth(int2(c4.xy * 0.5))), 300.0);   // auto: what is under the crosshair
    float zz = sky ? 1e4 : z;
    float Rmax = bk0.w * 44.0 * c4.y / 2160.0;
    float R = bokehCoc(zz, zf, mode, p) * Rmax;
    float3 own = sceneAt(p); own *= own;
    if (R < 0.75) return own;
    float3 acc = own; float ws = 1.0;
    float ga = ign(float2(p)) * 6.2831;
    [unroll] for (int k = 0; k < 16; ++k) {
        float r = sqrt((float(k) + 0.5) / 16.0) * R, a = ga + float(k) * 2.39996;
        int2  q = p + int2(cos(a) * r, sin(a) * r);
        float3 c = sceneAt(q); c *= c;
        float w = 1.0 + bk1.x * 6.0 * smoothstep(0.55, 0.95, lum3(c));
        float dS = rawDepth(q);
        float zS = isSky(dS) ? 1e4 : viewZ(dS);
        if (zS < zz - 0.5) w *= saturate(bokehCoc(zS, zf, mode, q) * Rmax - r + 1.0);   // a nearer, sharper thing does not smear back here
        acc += c * w; ws += w;
    }
    float3 o = acc / ws;
    float coc = R / max(Rmax, 1e-3);
    return o + float3(0.55, 0.42, 0.95) * bk1.z * 0.04 * coc * smoothstep(0.4, 0.9, lum3(o));   // a faint lilac fringe
}


// ---- rain extras: wet ground (darker, a sky sheen, puddles with ripple rings) and lightning (a flash + a bolt)
float3 posAt(int2 p) {
    float  d = rawDepth(p);
    float2 ndc = float2(p.x / c4.x * 2.0 - 1.0, 1.0 - p.y / c4.y * 2.0);
    float3 ray = c0.xyz + c1.xyz * (ndc.x * c0.w) + c2.xyz * (ndc.y * c1.w);
    return ray * viewZ(d);
}
float3 wetGround(float3 L, int2 px, float3 P, float3 rd, float dist, float t, float night) {
    if (dist > wx1.x * 3.0) return L;
    float3 n = cross(posAt(px + int2(1, 0)) - P, posAt(px + int2(0, 1)) - P);
    float nl = length(n);
    if (nl < 1e-6) return L;
    n /= nl; if (dot(n, rd) > 0.0) n = -n;
    float up = smoothstep(0.80, 0.97, n.y);
    if (up <= 0.0) return L;
    float3 wp = c5.xyz + P;
    float puddle = smoothstep(0.48, 0.72, vnoise2(wp.xz * 0.42 + 3.1));
    float wetK = wx1.y * up * smoothstep(wx1.x * 3.0, wx1.x * 1.5, dist);
    float3 skyC = lerp(float3(0.42, 0.48, 0.62), float3(0.03, 0.035, 0.06), night) + float3(0.7, 0.75, 0.95) * wxL.x * 0.6;
    float fres = 0.04 + 0.96 * pow(1.0 - saturate(dot(-rd, n)), 5.0);
    L *= lerp(1.0, 0.58, wetK * (0.55 + 0.45 * puddle));                      // wet things are darker
    L = lerp(L, skyC, saturate(fres * wetK * (0.35 + 0.65 * puddle)));        // and glossy: the grey sky in them
    // ripples in the puddles: a ring per 0.6-block cell, each on its own beat
    float2 g = wp.xz * 1.7; float2 cl = floor(g), f = frac(g) - 0.5;
    float h = frac(sin(dot(cl, float2(12.9898, 78.233))) * 43758.5453);
    float ph = frac(t * (0.9 + 0.5 * h) * wx0.y + h);
    float ring = exp(-pow((length(f - (h - 0.5) * 0.3) - ph * 0.42) / 0.03, 2.0)) * (1.0 - ph);
    L += skyC * ring * puddle * wetK * 0.55;
    return L;
}
float3 lightning(float3 L, float3 rd, bool sky) {
    if (wxL.x <= 0.0 && (wxL.w < 0.0 || wxL.w > 0.35)) return L;
    L += float3(0.70, 0.76, 1.0) * wxL.x * (sky ? 0.55 * saturate(rd.y + 0.35) : 0.18) * (0.5 + 0.5 * wxL.z);
    [branch] if (sky && wxL.w >= 0.0 && wxL.w < 0.35 && rd.y > 0.0 && rd.y < 0.6) {
        float a = atan2(rd.z, rd.x) - wxL.y;
        a = a - 6.2831853 * floor(a / 6.2831853 + 0.5);                        // -pi..pi around the bolt
        float hy = rd.y;
        float jag = 0.035 * sin(hy * 41.0 + wxL.y * 7.0) + 0.018 * sin(hy * 117.0 + wxL.y * 3.0) + 0.008 * sin(hy * 301.0);
        float d = abs(a - jag * (0.6 - hy) * 3.0);
        float w = 0.0022 * (0.6 + wxL.z);
        float bolt = exp(-pow(d / w, 2.0)) * 3.0 + exp(-d / (w * 8.0)) * 0.35;
        bolt *= smoothstep(0.6, 0.45, hy) * (1.0 - wxL.w / 0.35);
        L += float3(0.82, 0.86, 1.0) * bolt;
    }
    return L;
}

float4 PSWorldFxAtmos(VSOut i) : SV_Target {
    float2 px   = i.pos.xy;
    uint   fl   = uint(c3.x);
    float  d    = rawDepth(int2(px));
    bool   sky  = isSky(d);
    float2 ndc  = float2(px.x / c4.x * 2.0 - 1.0, 1.0 - px.y / c4.y * 2.0);
    float3 ray  = c0.xyz + c1.xyz * (ndc.x * c0.w) + c2.xyz * (ndc.y * c1.w);
    float  zc   = sky ? 0.0 : viewZ(d);
    float  dist = sky ? 600.0 : length(ray) * zc;           // sky: the haze out to the horizon
    float3 rd   = ray / length(ray);
    float3 q    = rd * dist;                                // camera-relative point
    float  camY = c5.y;
    float  t    = c2.w;
    float  jit  = ign(px + c3.w * 5.588238);
    // the sun from the game's time of day (c5.w: 0 noon, 0.25 sunset, 0.5 midnight, 0.75 sunrise); the moon opposite
    float3 SUN  = normalize(float3(-sin(c5.w * 6.2831853), cos(c5.w * 6.2831853), 0.12));
    float  night = smoothstep(-0.05, -0.25, SUN.y);
    float3 LIT  = SUN.y > -0.1 ? SUN : -SUN;                // the moon is opposite the sun
    float  litK = SUN.y > -0.1 ? 1.0 : 0.35;

    float3 col = sceneAt(int2(px)); col *= col;             // ~linear (gamma 2)
    [branch] if (bk0.w > 0.0) col = bokeh(int2(px), zc, sky);
    float3 acc = 0; float trans = 1.0;
    float  spare = 1.0;                                     // one actor test shared by the haze / mist and Chroma
    [branch] if (!sky && ((fl & 24u) != 0u || (ch0.w > -0.5 && ch1.z > 0.5))) spare = stillWorld(q);
    float spareC = ch1.z > 0.5 ? spare : 1.0;
    float3 outL = 0; bool drew = false;

    // ---- mist first (it is nearer the ground, in front of the haze behind it)
    [branch] if ((fl & 4u) != 0u && c9.w > 0.0) {
        float H = c10.x, clr = c10.y;
        float g = c3.y;
        float tMax = min(dist, 160.0);
        // the part of the ray inside the layer [g - 2, g + H * 1.4]
        float yTop = g + H * 1.4, yBot = g - 2.0;
        float tA = 0.0, tB = tMax;
        if (abs(rd.y) > 1e-4) {
            float t1 = (yTop - camY) / rd.y, t2 = (yBot - camY) / rd.y;
            tA = max(0.0, min(t1, t2)); tB = min(tMax, max(t1, t2));
        } else if (camY > yTop || camY < yBot) tB = -1.0;
        tA = max(tA, clr * 0.6);
        [branch] if (tB > tA) {
            int   N  = int(lerp(4.0, 9.0, saturate(c3.z) * saturate((tB - tA) / 40.0)));
            float st = (tB - tA) / float(N);
            float tt = tA + st * jit;
            float3 wind = float3(1.0, 0.0, 0.35) * (t * 0.35 * c10.z);
            float  mu   = dot(rd, LIT);
            float  phase = lerp(0.55, 1.0, night) + (1.0 - night) * (0.9 * pow(saturate(mu), 6.0) + 0.25 * pow(saturate(mu), 2.0));
            // beams: angular streaks around the sun's direction, slowly turning (cheap "light between the trees")
            float3 sx = normalize(cross(LIT, float3(0, 0, 1))), sy = cross(LIT, sx);
            float2 sp = float2(dot(rd, sx), dot(rd, sy));
            float  ang = atan2(sp.y, sp.x);
            float  beam = pow(0.5 + 0.5 * sin(ang * 23.0 + 1.7 * sin(ang * 7.0 + t * 0.05)), 3.0) * pow(0.5 + 0.5 * sin(ang * 11.0 - t * 0.04), 2.0);
            beam *= smoothstep(0.55, 0.95, mu) * (1.0 - night) * c10.w;
            float3 mc = c9.rgb * litK;
            [loop] for (int k = 0; k < N; ++k, tt += st) {
                float3 w  = rd * tt;                              // camera-relative sample
                float  y  = camY + w.y;
                float  hk = saturate(1.0 - (y - g) / H);          // thick at the ground, gone at H
                hk = hk * hk * smoothstep(yBot, g + 0.5, y);
                float3 wp = (c5.xyz + w) * float3(0.045, 0.11, 0.045) + wind * 0.045;
                float  bank = smoothstep(0.28, 0.78, fbm2(wp));     // banks and gaps
                float  dens = c9.w * 0.16 * hk * (0.25 + 0.75 * bank);
                dens *= smoothstep(clr * 0.6, clr * 1.4 + 0.5, tt);   // clear around you, soft edge
                float a = 1.0 - exp(-dens * st);
                float3 L = mc * phase + float3(1.0, 0.78, 0.52) * beam * 1.6;
                acc += trans * a * L;
                trans *= 1.0 - a;
                if (trans < 0.03) break;
            }
            float m = (fl & 16u) != 0u ? spare : 1.0;               // players and mobs show clearly through it
            acc *= m; trans = lerp(1.0, trans, m);
        }
    }
    float3 ambL = 0;
    [branch] if (amb0.w > 0.0) ambL = ambience(rd, sky ? 1e4 : dist, night, t, px);
    [branch] if (fx6.w > 0.5) ambL += worldEvents(q, sky, t);
    [branch] if (cs0.w > 0.0 || au0.w > 0.0 || ha0.w > 0.0) ambL += cosmetics(q, rd, sky ? 1e4 : dist, sky, t);
    // ---- haze: everything the mist let through takes on the haze colour with distance (closed form)
    [branch] if ((fl & 2u) != 0u && c6.w > 0.0) {
        float H   = c7.w, clr = c8.x;
        float t0  = min(clr, dist);
        float od  = hazeDepth(camY + rd.y * t0, rd, max(dist - t0, 0.0), c3.y, H * 0.5) * (0.010 + 0.030 * c6.w);
        float fog = (1.0 - exp(-od)) * c6.w * 1.25;
        fog = saturate(fog) * smoothstep(clr * 0.5, clr * 1.5 + 1.0, dist);
        // the colour drifts across the world between the palette's two colours
        float3 wp  = (c5.xyz + rd * min(dist, 80.0)) * 0.012 + float3(t * 0.010 * c8.y, 0.0, t * 0.006 * c8.y);
        float  mixk = smoothstep(0.25, 0.75, vnoise2(wp.xz + wp.y * 0.37));
        float3 hc  = lerp(c6.rgb, c7.rgb, mixk);
        hc *= lerp(1.0, 0.22, night);                                // dim at night
        float  mu  = dot(rd, LIT);
        hc += lerp(float3(1.0, 0.82, 0.62), float3(0.55, 0.62, 1.0), night) * pow(saturate(mu), 8.0) * c8.z * 0.6 * litK;   // glow toward the sun / moon
        if (sky) fog *= 0.55 * smoothstep(0.45, 0.0, rd.y);         // the sky: only toward the horizon
        float m = (fl & 8u) != 0u ? spare : 1.0;                     // players and mobs keep their colours
        fog *= m;
        float3 behind = col * trans + acc;
        float3 tinted = behind * hc * 1.45 + hc * lerp(0.16, 0.03, night);   // the world takes on the colour, and the air holds a little of it
        outL = lerp(behind, tinted, fog);
        drew = true;
    }
    // audit: Radiance alone only writes its outlines (it cost 0.85 ms at 4K writing every pixel with nobody near)
    bool const post = ch0.w > -0.5 || wx1.w > 0.5 || bk0.w > 0.0;
    float3 radL = 0;
    [branch] if (rad1.w > 0.5 && !sky && aInfo.x > 0.5) radL = radiance(int2(px), q, zc);   // outlines on players / mobs / items
    if (!drew && trans > 0.998 && dot(ambL, 1.0) < 0.002 && !post && dot(radL, 1.0) < 0.002) discard;
    if (!drew) outL = col * trans + acc;
    [branch] if (ch0.w > -0.5) outL = chroma(outL, q, sky, spareC);       // the grey world, colour where you have walked
    outL += radL;
    [branch] if (wx1.w > 0.5 && wx0.x < 0.5) {                           // rain: wet ground, lightning
        [branch] if (!sky && wx1.y > 0.0) outL = wetGround(outL, int2(px), q, rd, dist, t, night);
        outL = lightning(outL, rd, sky);
    }
    [branch] if (wx1.w > 0.5) outL += weather(rd, sky ? 1e4 : dist, t, night);
    float3 o = sqrt(max(outL + ambL, 0.0)) + (ign(px.yx + 13.7) - 0.5) / 255.0;
    return float4(o, 1.0);
}
