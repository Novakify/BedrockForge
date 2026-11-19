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
    // [Purxiy] Block Outline: the block you aim at (glided), Wings, Cape (Bedrock Forge 2026-11-17)
    float4 bo0;      // rgb colour (linear), w = line width px (0 = off)
    float4 bo1;      // xyz the block's low corner (camera-relative), w = glow
    float4 bo2;      // x = fill, y = style (0 glow, 1 clean, 2 corners), z = the face you look at (0..5, -1 none)
    float4 wg0;      // wings colour (linear), w = size (0 = off)
    float4 wg1;      // x = design (0 seraph, 1 butterfly, 2 prism), y = glow, z = flap, w = 1: first person too
    float4 cp0;      // cape colour (linear), w = glow (0 = off)
    float4 cp1;      // second colour, w = design (0 silk, 1 aurora, 2 starfield)
    float4 cp2;      // x = sway, y = your speed (0..1.6), z = 1: in the air
    // [Purxiy] Cyberspace / Cute World (Bedrock Forge 2026-11-17): whole-world looks
    float4 cy0;      // Cyberspace: rgb accent (linear), w = reality 0..1 (how far the world turns digital; 0 = off -> -1)
    float4 cy1;      // x = glow, y = circuits, z = scan interval (s), w = sign: data sky on / off, |w| - 1 = seconds since Cyberspace came on
    float4 cu0;      // Cute World: rgb blush / heart colour (linear), w = softness (-1 = off)
    float4 cu1;      // x = hearts amount, y = sparkles, z = pastel strength, w = 1: on
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
            float3 m  = (cell + 0.32 + 0.36 * hash33(cell * 1.71 + 4.1)) * C;
            float  ph = h.y * 6.2831;
            m += float3(sin(t * 0.7 * sp + ph), sin(t * 0.5 * sp + ph * 1.3) * 0.6, cos(t * 0.6 * sp + ph)) * 0.12 * C * (style >= 2 ? 0.6 : 1.0);
            // a particle is only seen by rays that pass through its cell: its glow must be gone before the cell wall,
            // or every particle sits in a faint see-through box (seen in game)
            float3 lo = m - cell * C, hi = (cell + 1.0) * C - m;
            float  margin = min(min(min(lo.x, hi.x), min(lo.y, hi.y)), min(min(lo.z, hi.z), 1e3));
            float3 rel = m - o;                                       // camera-relative
            if (amb3.z > 0.5) {                                       // they swirl aside as you move through them
                float r0 = length(rel);
                float push = min(0.9 * exp(-r0 * r0 * 0.6), 0.5 * margin);
                rel += normalize(rel + 1e-4) * push;
                margin -= push;
            }
            float tp = dot(rel, rd);
            if (tp > 0.25 && tp < tMax) {
                float  dd   = length(rel - rd * tp);
                float  size = amb1.w * (style == 1 ? 0.035 : style == 3 ? 0.07 : style == 2 ? 0.10 : 0.075) * (0.6 + 0.8 * h.z) * (1.0 + tp * 0.035);   // (seen in game at 4K: the first sizes read as dust; far ones grow a little so they stay visible)
                float  coc  = amb3.y > 0.5 ? max(0.0, 2.2 - tp) * 0.03 : 0.0;   // near ones: a soft round bokeh
                float  r    = size + coc;
                float  core = exp(-dd * dd / (r * r));
                float  halo = amb2.x * exp(-dd / (r * 4.0)) * 0.22 * smoothstep(0.8, 3.5, tp);   // close ones: no big fog ball
                float  tw   = 1.0 - amb2.z * (0.5 + 0.5 * sin(t * (2.0 + 3.0 * h.y) + ph * 3.0));   // twinkle / breathe
                float  fade = smoothstep(tMax, tMax * 0.65, tp) * (r > size ? size * size / (r * r) * 1.6 + 0.12 : 1.0);
                float3 cc   = lerp(amb0.rgb, amb1.rgb, h.z);
                if (style == 3) cc = lerp(float3(0.85, 0.90, 1.0), cc, 0.25);   // snow stays white
                float  win  = smoothstep(margin, margin * 0.45, dd);          // zero at the cell wall: no box
                win *= smoothstep(0.3, 1.3, tp);                              // one brushing the lens fades instead of filling the view
                // (2026-11-19, "the particles aren't as shiny"): a white-hot heart in every light, and each style its own
                // shape - stardust flashes four-point stars, petals are turning ovals with a lit edge, snow catches glints
                float3 v   = rel - rd * tp;
                float3 ax  = normalize(cross(rd, float3(0, 1, 0)) + float3(1e-4, 0, 0)), ay = cross(ax, rd);
                float  x   = dot(v, ax) / r, y = dot(v, ay) / r;
                float  hotC = exp(-dd * dd / (r * r * 0.16));
                float  shape = core;
                if (style == 1) {                                             // stardust: star spikes, flashing in turn
                    float fl = pow(saturate(sin(t * (1.3 + h.y) + ph * 5.0)), 5.0);
                    shape += (exp(-abs(x) * 0.5 - y * y * 9.0) + exp(-abs(y) * 0.5 - x * x * 9.0)) * (0.25 + 1.2 * fl);
                } else if (style == 2) {                                      // petals: a turning oval, lit along one side
                    float  ang = t * (0.8 + h.x) * sp + ph;
                    float2 q   = float2(cos(ang) * x + sin(ang) * y, -sin(ang) * x + cos(ang) * y) * float2(0.75, 1.5 + 0.8 * abs(sin(ang * 0.7)));
                    float  e   = dot(q, q);
                    shape = smoothstep(1.3, 0.7, e) * (0.75 + 0.5 * saturate(q.x)) + exp(-e * 0.8) * 0.2;
                    hotC *= 0.4;
                } else if (style == 3) {                                      // snow: a soft flake that catches glints
                    float g = pow(saturate(sin(t * 4.0 + ph * 7.0)), 12.0);
                    shape += g * (exp(-abs(x) * 0.8 - y * y * 12.0) + exp(-abs(y) * 0.8 - x * x * 12.0)) * 0.8;
                }
                acc += (cc * (shape * 2.2 + halo * 1.6) + float3(1.0, 0.98, 0.95) * hotC * 1.6) * tw * fade * win * lerp(0.75, 1.0, night);
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
float3 worldEvents(float3 P, bool sky, float t, float upN, float3 rd) {   // upN: how much the surface here faces up (1 = floor)
    int n = min(int(fx6.w), 16);
    if (n <= 0 || sky) return 0;
    float3 L = 0;
    [loop] for (int i = 0; i < n; ++i) {
        float3 c = evP[i].xyz; float age = evP[i].w; int kind = int(evK[i].x + 0.5); float seed = evK[i].z;
        float3 dv = P - c;
        if (kind == 1 && fx0.w > 0.0) {                        // jump: one ring spreading from your feet
            // (Purxiy reference, videos 1 / 5: a THIN neon ring flat on the ground where you took off - creamy white
            //  core, the colour glowing out from it, the hue drifting round the ring (cooler on one side, warmer on
            //  the other), shooting out with a hard ease-out to ~1.3 blocks, then fading.)
            float u = age / max(fx1.x, 0.05);
            if (u < 1.0 && abs(dv.y) < 0.6) {
                float r = length(dv.xz), an = atan2(dv.z, dv.x);
                float R = fx0.w * 1.3 * (1.0 - pow(1.0 - saturate(u * 1.6), 4.0));
                float w = 0.022 + 0.02 * u;
                float core = exp(-pow((r - R) / (w * 0.45), 2.0));
                float glow = exp(-pow((r - R) / (w * 2.2), 2.0)) * 0.55 + fx1.y * 0.18 * exp(-abs(r - R) / (w * 6.0));
                float3 warm = lerp(fx0.rgb, float3(1.0, 0.72, 0.62), 0.35), cool = lerp(fx0.rgb, float3(0.70, 0.72, 1.0), 0.35);
                float3 cc = lerp(cool, warm, 0.5 + 0.5 * sin(an + seed * 6.3 + age * 1.5));
                cc = lerp(cc, r > R ? warm : cool, 0.3);                  // a warmer outer edge, a lavender inner one
                float fade = pow(1.0 - u, 1.1) * smoothstep(0.6, 0.2, abs(dv.y));
                L += (float3(1.0, 0.96, 0.93) * core * 1.6 + cc * glow * 2.0) * fade;
            }
        } else if ((kind == 2 || kind == 3) && (fx1.z > 0.0 || fx3.w > 0.0)) {
            // (in game 2026-11-19: the ink was painted on everything standing in its column - the player you hit, walls,
            //  fences - and read as a bug. It lies on floors only now: surfaces that face up.)
            if (fx1.z > 0.0 && dv.y < 0.3 && dv.y > -2.6 && upN > 0.6) {       // splash: ink on the ground under the hit, fading slowly
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
                    float ink = max(blob, drops) * smoothstep(1.0, 0.6, u) * smoothstep(0.0, 1.1, 2.6 + dv.y) * smoothstep(0.6, 0.8, upN);
                    float3 cc = lerp(fx2.rgb, fx3.rgb, 0.5 + 0.5 * sin(an * 2.0 + seed * 5.0));
                    L += cc * ink * (0.25 + 0.9 * fx2.w * (1.0 - u * 0.7)) * (1.0 + 2.0 * exp(-age * 6.0));
                }
            }
            // on the one you hit (Fernando 2026-11-19: "hit the player too, just look better"): no ink smeared on them -
            // a white-hot point where the blow lands, a wave of the splash colours running over the body from it, and a
            // few short rays fanning out across it. Gone in under half a second; a critical lands harder.
            if (fx1.z > 0.0 && upN < 0.6) {
                float u2 = age / 0.42;
                float r3 = length(dv), Sz = fx1.z;
                if (u2 < 1.0 && r3 < 1.1 * Sz) {
                    float R    = 0.85 * Sz * (1.0 - pow(1.0 - u2, 3.0));
                    float wave = exp(-pow((r3 - R) / (0.07 * Sz), 2.0)) * (1.0 - u2);
                    float core = exp(-r3 * r3 / (0.02 * Sz * Sz)) * exp(-age * 18.0);
                    float an   = atan2(dv.y, dot(dv.xz, normalize(float2(-rd.z, rd.x) + 1e-5)));   // round the blow, as you see it
                    float rays = pow(0.5 + 0.5 * cos(an * 7.0 + seed * 40.0), 18.0) * exp(-r3 / (0.45 * Sz)) * exp(-age * 9.0);
                    float3 cc  = lerp(fx2.rgb, fx3.rgb, saturate(r3 / (0.8 * Sz)));
                    L += (cc * (wave * 1.4 + rays * 1.2) + float3(1.0, 0.97, 0.92) * core * 2.5) * (0.5 + fx2.w) * (kind == 3 ? 1.6 : 1.0);
                }
                // (Purxiy reference, video 5: ~100-150 ms after the blow the whole target washes pink-white, then
                //  clears) - the body's box round the hit point
                float2 bxz = dv.xz;
                if (length(bxz) < 0.5 * Sz && dv.y > -1.45 && dv.y < 0.75) {
                    float wash = exp(-pow((age - 0.12) / 0.07, 2.0));
                    L += float3(1.0, 0.85, 1.0) * wash * 0.9 * (0.5 + fx2.w) * (kind == 3 ? 1.3 : 1.0);
                }
            }
            if (fx3.w > 0.0) {                                   // ripple: a flash, and a wave of light over what is near
                // (Fernando 2026-11-19: "ripple doesn't look that good, not shiny". Now a prismatic wave: the ring splits
                //  into its colour and white like light through glass, a second fainter echo follows it, the ground
                //  ring gets the same, and a burst of glints rides the front.)
                float u = age / max(fx4.w, 0.05);
                if (u < 1.0) {
                    float d3 = length(dv), ease = 1.0 - pow(1.0 - u, 2.0), fade = pow(1.0 - u, 1.3);
                    float R = 2.6 * ease, W = 0.12 + 0.06 * u;
                    float3 cA = fx4.rgb, cW = float3(1.0, 0.98, 0.96);
                    float3 cB = float3(fx4.b, fx4.r, fx4.g) * 0.8 + 0.2;     // a shifted hue for the outer fringe
                    float3 ring = cA * exp(-pow((d3 - R) / W, 2.0)) + cW * 1.3 * exp(-pow((d3 - R + 0.04) / (W * 0.35), 2.0))
                                + cB * 0.7 * exp(-pow((d3 - R - W * 1.2) / (W * 0.7), 2.0))
                                + cA * 0.45 * exp(-pow((d3 - R * 0.62) / (W * 0.8), 2.0));   // the echo
                    float3 flash = (cW * 0.9 + cA * 0.7) * exp(-d3 * 3.4) * exp(-age * 14.0);   // a quick pop, not a white-out
                    float3 Lr = ring * fade + flash;
                    if (fx5.x > 0.5 && dv.y < 0.2 && dv.y > -2.4) {   // the rings rolling over the ground
                        float r = length(dv.xz), R2 = 3.2 * (1.0 - pow(1.0 - u, 2.5)), an = atan2(dv.z, dv.x);
                        float gl = pow(0.5 + 0.5 * cos(an * 9.0 + seed * 30.0 + u * 4.0), 24.0);   // glints on the front
                        Lr += (cA * exp(-pow((r - R2) / 0.09, 2.0)) + cW * 0.9 * exp(-pow((r - R2 + 0.02) / 0.03, 2.0))
                               + cA * 0.4 * exp(-pow((r - R2 * 0.6) / 0.07, 2.0)) + cW * gl * 2.0 * exp(-pow((r - R2) / 0.12, 2.0))) * fade;
                    }
                    if (kind == 3 && fx5.y > 0.5) Lr *= 1.8;          // a critical hit lands harder
                    L += Lr * fx3.w * 1.4;
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
// ---- Aura "Ascension" (Fernando 2026-11-17: an ascended-warrior aura - original, no characters / names / assets): a
// golden flame shell hugging your silhouette that licks upward to a pointed crown above the head, a hot rim at its
// edge, blue-white arcs of electricity snapping on and off around the body, golden motes rising through it, and its
// light on the ground at your feet. Traced in the plane facing the camera through your body's axis (like the halo),
// depth-tested against the world. au0.w = size, au1.x = speed, au1.y = glow.
float ascFbm(float2 p) { return vnoise2(p) * 0.6 + vnoise2(p * 2.07 + 13.1) * 0.3 + vnoise2(p * 4.13 + 7.7) * 0.1; }
float3 ascension(float3 P, float3 rd, float dist, bool sky, float t, bool fp) {
    float3 L = 0;
    float  S = au0.w, spd = max(au1.x, 0.05), glow = au1.y;
    float3 gold = float3(1.00, 0.72, 0.08), hot = float3(1.00, 0.90, 0.45), ember = float3(1.00, 0.55, 0.10);
    float3 volt = float3(0.62, 0.84, 1.00);
    // its light on the ground round your feet (pulses with the flames)
    if (!sky) {
        float2 g = P.xz - pl.xz;
        float  gy = abs(P.y - pl.y);
        float  fl = 0.85 + 0.15 * sin(t * 9.0 * spd) * sin(t * 13.7 * spd + 1.3);
        L += gold * 0.32 * (0.4 + glow) * exp(-dot(g, g) / (1.6 * S * S)) * smoothstep(0.5, 0.0, gy) * fl;
    }
    if (fp) return L;   // from your own eyes: only its light on the ground (the shell would fill the screen)
    // the closest approach of the view ray to your axis (feet - 0.1 .. 3.3 above them)
    float3 A = pl.xyz + float3(0, -0.1, 0), v = float3(0, 3.4, 0);
    float  b = dot(rd, v), c = dot(v, v), d = dot(rd, -A), e = dot(v, -A);
    float  den = c - b * b;
    if (den < 1e-5) return L;
    float  hu = (e - b * d) / den;                     // along the axis (unclamped)
    // (in game 2026-11-18: a thin gold beam ran from the crown up into the sky - rays passing above your head were
    //  clamped onto the top of the axis, so the crown drew along a line forever. Above the crown / below the feet: none)
    if (hu > 1.06 || hu < -0.04) return L;
    float  h  = saturate(hu);
    float  sc = max(b * h - d, 0.0);                   // along the ray
    float3 Q  = A + v * h, Pr = rd * sc;
    float3 right = cross(float3(0, 1, 0), rd);
    right = dot(right, right) > 1e-4 ? normalize(right) : float3(1, 0, 0);
    float  x  = dot(Pr - Q, right);                    // across (signed), in blocks
    float  y  = hu * 3.4 - 0.1;                        // height above your feet (the crown fades out past the top)
    float  ax = abs(x);
    if (ax > 1.6 * S + 0.4 || sc < 0.2) return L;      // nowhere near: done
    // depth: a wall in front hides it; your own body in front lets the front sheet of flame show (fainter)
    if (dist < sc - 1.2) return L;
    float front = dist < sc - 0.25 ? 0.45 : 1.0;
    // ---- the flame shell (tuned on an offline preview, 6 passes: no hairline rims, no hard bands, no box at the crown).
    //      f = how far inside the flame (in body widths, > 0 inside): the body's outline grown out, a dome tapering to
    //      a crown, its edge torn into tongues that stream upward (mirrored left / right: no seam down the middle)
    float body = 0.30 + 0.26 * S, top = 2.05 + 0.85 * S;
    float prof = y < 1.7 ? body * (0.82 + 0.18 * saturate((y + 0.1) / 0.9)) : body * pow(saturate((top - y) / (top - 1.7)), 0.75);
    float up   = saturate((y - 0.6) / 1.6);
    float n1   = ascFbm(float2(ax * 2.2 + 3.0 + 0.15 * sin(y * 1.3), y * 1.6 - t * 3.4 * spd));
    float spike = pow(saturate(n1 - 0.45), 1.5) * (0.9 + 1.6 * up);
    float env  = saturate((top + 0.25 - y) / 0.55);
    float R    = (prof + body * (0.35 * (n1 - 0.5) + spike * 1.1)) * env;
    float f    = (R - ax) / body - (1.0 - env);                                // above the crown: well outside
    float ct   = saturate(1.0 - ax / (body * 1.1)) * saturate((y - 1.7) / 0.4) * saturate((top + 0.55 - y) / 0.55);
    float cn   = saturate(ascFbm(float2(x * 5.0, y * 1.1 - t * 4.2 * spd)) - 0.48) * 2.6;
    f = max(f, cn * ct * 2.6 - 0.45);                                            // crown tongues shooting up
    float lowF = saturate((y + 0.15) / 0.45);                                    // it rises from your feet
    if (f > -1.0) {
        float streak = pow(saturate(ascFbm(float2(x * 6.5, y * 0.75 - t * 5.0 * spd)) * 1.4 - 0.35), 2.0);   // licks streaming up
        float dens   = pow(saturate((f + 0.10) / 0.45), 1.2) * (0.30 + 0.70 * streak);
        float edge   = exp(-pow((f - 0.02) / 0.14, 2.0));
        float halo   = f < 0.0 ? exp(f / 0.14) : 0.0;
        float core   = saturate((f - 0.35) / 0.6);
        float flick  = 0.85 + 0.15 * sin(t * 23.0 * spd + y * 4.0) * sin(t * 17.0 * spd + x * 7.0);
        float3 col   = gold * (dens * 1.05 + edge * 0.55) + hot * (edge * 0.45 + core * dens * 0.35) + lerp(gold, ember, 0.5) * halo * 0.22 * (0.3 + glow);
        L += col * flick * lowF * front * (0.8 + 0.5 * glow);
    }
    // ---- rising golden motes
    {
        float2 m  = float2(x, y + t * 1.1 * spd) * float2(5.0, 3.0);
        float2 mi = floor(m), mf = frac(m) - 0.5;
        float  hs = hash12b(mi);
        if (hs > 0.86 && ax < body * 1.6 && y > 0.0 && y < top) {
            float2 o = (float2(hash12b(mi + 3.1), hash12b(mi + 7.3)) - 0.5) * 0.6;
            float  k = exp(-dot(mf - o, mf - o) * 160.0) * (0.6 + 0.4 * sin(t * 8.0 + hs * 40.0));
            k *= smoothstep(top, top * 0.75, y) * smoothstep(0.0, 0.15, y);  // they fade out as they rise (no flat cut at the top)
            L += hot * k * 1.3 * front;
        }
    }
    // ---- electricity: up to three arcs snap on and off round the body, each a jagged bolt along the flame's edge
    [unroll] for (int k = 0; k < 3; ++k) {
        float ep   = t * (2.3 + 0.9 * float(k)) * spd + float(k) * 0.37;
        float ei   = floor(ep), life = frac(ep);
        float sd   = hash12b(float2(ei, float(k) * 7.1));
        if (life > 0.45 || sd < 0.15) continue;                               // off much of the time: a crackle, not a light
        float on   = step(0.35, frac(sin(t * 61.0 + sd * 90.0) * 43758.5));    // it flickers while it lives
        float sgn  = hash12b(float2(sd, 2.2)) > 0.5 ? 1.0 : -1.0;
        float y0   = 0.25 + 1.6 * hash12b(float2(sd, 5.5));
        float len  = 0.55 + 0.6 * hash12b(float2(sd, 9.9));
        float best = 1e9;
        float2 pa  = float2(sgn * body * (0.75 + 0.35 * hash12b(float2(sd, 1.3))), y0);
        [unroll] for (int j = 1; j <= 6; ++j) {
            float  fj = float(j) / 6.0;
            float2 pb = float2(sgn * body * (0.65 + 0.55 * hash12b(float2(sd + fj, 3.7))), y0 + len * fj + 0.12 * (hash12b(float2(sd, fj * 9.0)) - 0.5));
            pb.x += (hash12b(float2(fj * 13.0, sd)) - 0.5) * 0.22;
            float hh;
            best = min(best, segDist2(float2(x, y), pa, pb, hh));
            pa = pb;
        }
        float dd = sqrt(best), w = 0.014 + 0.007 * S;
        float fade = 1.0 - life / 0.45;
        L += (hot * exp(-dd * dd / (w * w)) * 2.6 + volt * exp(-dd / (w * 5.0)) * 0.9 * (0.4 + glow)) * on * fade * front;
    }
    return L;
}
float3 cosmetics(float3 P, float3 rd, float dist, bool sky, float t) {
    float3 L = 0;
    if (pl.w < -50.0) return L;
    bool const fp = length(pl.xyz + float3(0, 1.62, 0)) < 0.6;   // the camera is in your head: first person
    // trail: a glowing ribbon painted on the ground along your path (surfaces near the path's height)
    int n = min(int(cs1.w), 32);
    float3 tbd = P - trB.xyz;
    [branch] if (cs0.w > 0.0 && n > 1 && !sky && dot(tbd, tbd) < trB.w * trB.w) {   // audit: only near the path
        float best = 1e9, bestAge = 0.0, bestY = 0.0, side = 0.0;
        [loop] for (int i = 0; i + 1 < n; ++i) {
            float h;
            float d2 = segDist2(P.xz, trP[i].xz, trP[i + 1].xz, h);
            if (d2 < best) {
                best = d2; bestAge = lerp(trP[i].w, trP[i + 1].w, h); bestY = lerp(trP[i].y, trP[i + 1].y, h);
                float2 dir = trP[i].xz - trP[i + 1].xz;              // which side of the path this point is on (signed)
                side = (dir.x * (P.z - trP[i].z) - dir.y * (P.x - trP[i].x)) / max(length(dir), 1e-4);
            }
        }
        float u = bestAge / max(cs0.w, 0.05);
        if (u < 1.0 && abs(P.y - bestY) < 0.6) {
            // (Fernando 2026-11-19: next to Purxiy the trail "pales". It was one soft line. Now: a white-hot core over the
            //  colour, light flowing back along it, a soft taper at the tail - and each style its own shape.)
            int    st   = int(cs1.z + 0.5);
            float  w    = 0.10 * cs1.x, d = sqrt(best);
            float  life = pow(1.0 - u, 1.2) * smoothstep(0.0, 0.04, bestAge + 0.01);
            float  flow = 0.75 + 0.25 * sin(bestAge * 18.0 - t * 6.0);   // light running back along the path
            float3 col  = lerp(cs0.rgb, float3(1.0, 0.97, 0.94), 0.35 * exp(-bestAge * 3.0));   // fresh end burns whiter
            float  k = 0.0, coreW = 0.0;
            if (st == 1) {                                                // path: a wide glowing band with lit edges
                float W2 = w * 2.4;
                float band = smoothstep(W2, W2 * 0.7, d);
                float edge = exp(-pow((d - W2 * 0.82) / (w * 0.22), 2.0));
                float chev = 0.5 + 0.5 * sin(bestAge * 30.0 - t * 9.0 + abs(side) * 8.0);   // chevrons rolling back
                k = band * (0.25 + 0.25 * chev) + edge * 1.2;
                coreW = exp(-pow(d / (w * 0.35), 2.0)) * 0.5;
            } else if (st == 2) {                                         // helix: two strands twisting round the path
                float ph = bestAge * 14.0 - t * 2.0;
                float o1 = sin(ph) * w * 1.6, o2 = -o1;
                float s1 = exp(-pow((side - o1) / (w * 0.32), 2.0)), s2 = exp(-pow((side - o2) / (w * 0.32), 2.0));
                float front = 0.6 + 0.4 * cos(ph);                         // the strand passing over glows brighter
                float near = smoothstep(w * 2.6, w * 1.9, d);             // only beside the path (not on past its ends)
                s1 *= near; s2 *= near;
                k = s1 * front + s2 * (1.6 - front) * 0.8 + cs1.y * 0.15 * exp(-d / (w * 2.5));
                coreW = (s1 * front + s2 * (1.6 - front) * 0.8) * 0.45;
            } else if (st == 3) {                                         // stardust: twinkling sparks in a faint line
                float2 cell = floor(P.xz * 9.0);
                float  rnd  = frac(sin(dot(cell, float2(12.9898, 78.233))) * 43758.5453);
                float  tw   = pow(saturate(sin(t * 5.0 + rnd * 40.0)), 4.0);
                float2 sp   = (cell + 0.5 + (float2(frac(rnd * 13.7), frac(rnd * 71.3)) - 0.5) * 0.5) / 9.0;   // a round spark in its cell
                float  rr   = length(P.xz - sp) * 9.0;
                float  spark = step(0.72, rnd) * smoothstep(w * 2.2, w * 0.6, d) * exp(-rr * rr * 18.0) * (0.8 + 2.6 * tw);
                k = spark * 1.6 + exp(-pow(d / w, 2.0)) * 0.25;
                coreW = spark * tw * 0.8;
            } else {                                                      // line: a bright ribbon
                k = exp(-pow(d / w, 2.0)) + cs1.y * 0.35 * exp(-d / (w * 3.0));
                coreW = exp(-pow(d / (w * 0.35), 2.0));
            }
            float keep = life * flow * smoothstep(0.6, 0.15, abs(P.y - bestY));
            L += (col * k * 1.5 + float3(1.0, 0.98, 0.95) * coreW * 1.2) * keep;
        }
    }
    // aura / halo are lights in the air: closest approach of the view ray to each (depth-tested against the world)
    [branch] if (au0.w > 0.0 && int(au1.z + 0.5) == 4) L += ascension(P, rd, dist, sky, t, fp && au1.w < 0.5);   // Ascension
    else [branch] if (au0.w > 0.0 && (!fp || au1.w > 0.5)) {
        // (Fernando 2026-11-19: next to Purxiy the auras "pale" - Orbit / Fireflies / Spiral / Constellation were the same
        //  few soft dots. Now every light is a white-hot core in a coloured glow with a comet tail along its path, and
        //  each style has its own structure: Orbit rides a faint ring, Spiral is a double helix, Constellation's stars
        //  are joined by thin lines and flash a four-point spike.)
        int   style = int(au1.z + 0.5);
        int   N     = style == 1 ? 9 : style == 2 ? 2 : style == 3 ? 7 : 6;
        float sz    = au0.w;
        float3 c    = pl.xyz + float3(0, 1.0, 0);
        float  rb   = 0.75 * sz * 1.2 + 1.0 + 0.2 * sz;              // audit: the ray must pass the aura's sphere at all
        float  tc   = dot(c, rd);
        float3 cp   = c - rd * tc;
        if (dot(cp, cp) > rb * rb || tc < -rb) N = 0;
        float3 white = float3(1.0, 0.98, 0.95);
        float3 prevStar = 0; float3 firstStar = 0;
        [loop] for (int i = 0; i < N; ++i) {
            float fi = float(i);
            float tw = 0.78 + 0.22 * sin(t * 3.0 + fi * 2.1);
            float3 head = 0;
            [loop] for (int k = 0; k < (style == 2 ? 22 : 8); ++k) {                      // k = 0: the light; 1..7: its comet tail, back along its path
                float tk = t - float(k) * (style == 2 ? 0.018 : 0.075) / max(au1.x, 0.3);   // the spiral's tail packs tighter: a ribbon, not beads
                float ph = fi / float(N) * 6.2831 + tk * 0.9 * au1.x;
                float r = 0.75 * sz, yy = 0.0;
                if (style == 1) {                                       // fireflies: wander, blink
                    ph = fi * 2.4 + tk * 0.4 * au1.x * (0.6 + frac(fi * 0.37));
                    r *= 0.6 + 0.6 * frac(fi * 0.71);
                    yy = 0.8 * sin(tk * 0.7 * au1.x + fi * 1.7);
                } else if (style == 2) {                                // spiral (Purxiy ref, video 3): comets on a rising helix
                    float cid = fmod(fi, 2.0);                           // two comets, half a turn apart
                    float u = frac(tk * 0.22 * au1.x + cid * 0.5);
                    ph = tk * 6.2831 * 0.55 * au1.x + cid * 3.1416;
                    yy = u * 2.2 - 1.1;
                    r *= 0.85 + 0.15 * sin(u * 3.1416);
                } else if (style == 3) {                                // constellation: fixed stars, the sky slowly turning
                    ph = fi * 2.399 + tk * 0.12 * au1.x;
                    r *= 0.75 + 0.35 * frac(fi * 0.618);
                    yy = (frac(fi * 0.381) - 0.5) * 1.6;
                } else yy = 0.18 * sin(ph * 2.0) + 0.1 * sin(pl.w + ph); // orbit: a tilted ring
                float3 q = c + float3(cos(ph) * r, yy, sin(ph) * r);
                if (k == 0) head = q;
                if (style == 3 && k > 0) break;                          // stars don't streak
                float tp = dot(q, rd);
                if (tp < 0.2 || tp > dist + 0.3) continue;
                float d = length(q - rd * tp);
                float kn = float(k) / (style == 2 ? 22.0 : 8.0);           // 0 head .. 1 end of the tail
                float s = 0.048 * sz * (k == 0 ? 1.0 : 0.85 - 0.64 * kn);   // tapers, never below ~0.2
                float fade = k == 0 ? 1.0 : 0.7 * pow(1.0 - float(k) / (style == 2 ? 22.0 : 8.0), 1.3);
                float blink = style == 1 ? smoothstep(-0.2, 0.6, sin(t * 1.7 + fi * 4.1)) : 1.0;
                float core = exp(-d * d / (s * s * 0.25));               // white-hot centre
                float glow = exp(-d * d / (s * s)) * 1.6 + au1.y * 0.30 * exp(-d / (s * 4.0));
                L += (white * core * 2.4 * (k == 0 ? 1.0 : 0.35) + au0.rgb * glow * 1.4) * fade * tw * blink;
                if (style == 3 && k == 0) {                              // a four-point spike on each star, flashing in turn
                    float3 v = q - rd * tp;
                    float3 ax = normalize(cross(rd, float3(0, 1, 0)) + 1e-5), ay = cross(ax, rd);
                    float  x = dot(v, ax) / s, y = dot(v, ay) / s;   // spikes ~6 radii long
                    float  flash = pow(saturate(sin(t * 0.9 + fi * 1.3)), 6.0);
                    L += (white + au0.rgb) * (exp(-abs(x) * 0.35 - y * y * 6.0) + exp(-abs(y) * 0.35 - x * x * 6.0)) * 0.6 * (0.35 + flash) * tw;
                }
            }
            if (style == 3 && i > 0) {                                   // the lines of the constellation (star to star)
                float3 a = prevStar, b = head, ab = b - a;
                float3 w0 = -a;                                          // ray from the eye (origin) along rd vs segment a..b
                float  bb = dot(rd, ab), cc2 = dot(ab, ab), dd = dot(rd, w0), ee = dot(ab, w0);
                float  den = cc2 - bb * bb;
                float  sc = den > 1e-6 ? saturate((ee - bb * dd) / den) : 0.0;   // (Sunday: the segment's parameter)
                float3 pS = a + ab * sc;
                float  tp = dot(pS, rd);
                if (tp > 0.2 && tp < dist + 0.3) {
                    float d = length(pS - rd * tp), lw = 0.011 * sz;
                    L += (au0.rgb * 1.4 + white * 0.25) * exp(-d * d / (lw * lw)) * (0.6 + 0.4 * sin(t * 1.3 + fi));
                }
            }
            if (i == 0) firstStar = head;
            prevStar = head;
        }
        [branch] if (style == 0 && N > 0) {                              // orbit: the faint ring the lights ride on
            float3 nrm = normalize(float3(0.1 * cos(pl.w), 1, 0.1 * sin(pl.w)));
            float  den = dot(rd, nrm);
            if (abs(den) > 1e-3) {
                float tp = dot(c, nrm) / den;
                if (tp > 0.2 && tp < dist + 0.3) {
                    float3 hp = rd * tp - c;
                    float rr = length(hp - nrm * dot(hp, nrm)), R = 0.75 * sz, w = 0.008 * sz;
                    L += au0.rgb * (exp(-pow((rr - R) / w, 2.0)) * 0.6 + 0.12 * exp(-abs(rr - R) / (w * 6.0)));
                }
            }
        }
    }
    [branch] if (ha0.w > 0.0 && (!fp || ha1.w > 0.5)) {
        float3 hc = pl.xyz + float3(0, 1.95 + 0.15 * ha1.y + 0.03 * sin(t * 1.6), 0);   // audit: hugs the head (was 0.4 above it)
        bool const crown = int(ha1.z + 0.5) == 2;
        // a crown is WORN (in game 2026-11-19 it floated over the head like a halo): its rim sits on the head, just under
        // the top, no bob; the Height slider only nudges it
        if (crown) hc = pl.xyz + float3(0, 1.80 + 0.06 * (ha1.y - 1.0), 0);
        // the halo's plane (horizontal, a slight tilt); the ray meets it at one point: distance to the ring there
        int   dz   = int(ha1.z + 0.5);
        float tilt = dz == 5 ? 0.38 : dz == 2 ? 0.0 : 0.08;            // fallen: knocked askew; a crown sits level
        float3 nrm = normalize(float3(tilt * sin(pl.w), 1, tilt * cos(pl.w)));
        float den = dot(rd, nrm);
        if (abs(den) > 1e-3) {
            float tp = dot(hc, nrm) / den;
            if (tp > 0.2 && tp < dist + 0.3) {
                float3 hp = rd * tp - hc;
                float R = (crown ? 0.31 : 0.28) * ha0.w, rr = length(hp - nrm * dot(hp, nrm));
                float wdt = 0.032 * ha0.w;   // (2026-11-19: thicker - they read as a thread next to Purxiy)
                float k = exp(-pow((rr - R) / wdt, 2.0));
                float an = atan2(hp.z, hp.x);
                // (in game 2026-11-19: only Seraph read as a design - the others were the same thin ring a touch dimmer.
                //  Each is its own shape now; Crown also gets a real band of points, below.)
                if (dz == 1) {                                             // sigil: three rings, the inner and outer broken and turning against each other
                    k += 0.9 * exp(-pow((rr - R * 0.68) / (wdt * 0.8), 2.0)) * step(0.5, frac((an + t * 0.5) * 1.9099));
                    k += 0.7 * exp(-pow((rr - R * 1.24) / (wdt * 0.7), 2.0)) * step(0.7, frac((an - t * 0.35) * 3.8197));
                } else if (dz == 2) k *= 0.9;                              // crown: the ring is its base
                else if (dz == 3) {                                        // stardust: ten beads of light running round, no ring
                    float bead = pow(0.5 + 0.5 * cos((an + t * 0.4) * 10.0), 26.0);
                    k = exp(-pow((rr - R) / (wdt * 1.7), 2.0)) * bead * 2.2 + 0.10 * k;
                } else if (dz == 4) k = exp(-pow((rr - R) / (wdt * 2.2), 2.0)) * (0.30 + 0.70 * smoothstep(-1.0, 1.0, sin(an - t)));   // eclipse: a wide ring, light sweeping round it
                else if (dz == 5) k *= step(0.24, frac(an * 0.15915 + 0.1)) * (0.75 + 0.25 * sin(t * 9.0 + an * 3.0));       // fallen: askew, a piece missing, guttering
                // the soft glow dies out within a hand's width of the ring (it used to reach a block out across the
                // halo's plane at full Glow: a faint see-through sheet over your head in third person)
                float gl = (rr - R) / (wdt * 6.0);
                // shine: a white-hot line along the ring's middle and a glint of light running round it
                float coreL = exp(-pow((rr - R) / (wdt * 0.32), 2.0)) * saturate(k * 1.5);
                float glint = pow(0.5 + 0.5 * cos(an - t * 1.4), 48.0) * saturate(k * 2.0) + pow(0.5 + 0.5 * cos(an + 2.6 - t * 0.9), 64.0) * saturate(k * 2.0) * 0.6;
                float3 hot = float3(1.0, 0.98, 0.94);
                L += crown ? ha0.rgb * (k * 0.55 + ha1.x * 0.08 * exp(-gl * gl))     // a crown's base is its rim, not a glowing ring
                           : ha0.rgb * (k * 2.0 + ha1.x * 0.45 * exp(-gl * gl)) + hot * (coreL * 1.4 + glint * 2.4);
            }
        }
        [branch] if (dz == 2) {   // crown: an upright band on the ring, rising to five points (the ray against its cylinder, near and far wall)
            float  R = 0.31 * ha0.w;
            float2 o = -hc.xz, dv2 = rd.xz;
            float  qa = dot(dv2, dv2), qb = dot(o, dv2), qc = dot(o, o) - R * R;
            float  disc = qb * qb - qa * qc;
            if (qa > 1e-6 && disc > 0.0) {
                float sq = sqrt(disc);
                [unroll] for (int w = 0; w < 2; ++w) {
                    float s = (-qb + (w == 0 ? -sq : sq)) / qa;
                    if (s < 0.2 || s > dist + 0.3) continue;
                    float3 h3 = rd * s - hc;
                    float  a5 = atan2(h3.z, h3.x) + pl.w;                 // turns with you
                    // five crisp points (a triangle wave, not a soft swell - in the lab the sine read as jelly), each rising
                    // from a low band; a jewel of light on every tip
                    float  ph  = frac(a5 * 1.273240) * 2.0 - 1.0;          // -1..1 across one of the eight (Purxiy ref: 6-8 spikes)
                    float  tri = saturate(1.0 - abs(ph) * 1.15);
                    float  pk  = tri * tri * (3.0 - 2.0 * tri);
                    float  H  = ha0.w * (0.060 + 0.150 * pk);             // the band's height here (taller: it read as a ribbon)
                    if (h3.y < -0.004 || h3.y > H + 0.02 * ha0.w) continue;
                    float  inB  = step(h3.y, H);
                    float  edge = exp(-pow((H - h3.y) / (0.008 * ha0.w), 2.0));          // the lit cut edge along the top
                    float  rimB = exp(-pow((h3.y - 0.006 * ha0.w) / (0.010 * ha0.w), 2.0)); // the rim along the bottom
                    float  body = (0.75 + 0.25 * ha1.x) * lerp(1.0, 0.65, h3.y / max(H, 1e-3));    // metal: brighter at the base
                    float  sweep = pow(saturate(1.0 - abs(frac(a5 * 0.159 - t * 0.12) - 0.5) * 8.0), 2.0);   // a shine travelling round the band
                    float2 tipP = float2(ph * 0.075 * ha0.w, h3.y - ha0.w * 0.212);
                    float  gem  = exp(-dot(tipP, tipP) / (0.00030 * ha0.w * ha0.w)) * step(abs(ph), 0.35);
                    L += (ha0.rgb * (inB * body + 1.6 * edge * inB + 1.2 * rimB) + float3(1.0, 0.96, 0.88) * inB * sweep * 0.9) * (w == 0 ? 1.0 : 0.45)
                       + float3(1.0, 0.99, 0.95) * gem * 3.4;   // pearl tips
                }
            }
        }
    }
    return L;
}

float lum3(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
float3 chroma(float3 c, float3 P, bool sky, float spareC) {
    // (Fernando 2026-11-19: next to Purxiy it "pales". The colour now bleeds in like wet paint: a ragged, flowing edge
    //  (no clean circles), a bright saturated rim where it is spreading, and fresh colour a little richer than life
    //  before it settles; the waiting world is a cool, slightly deeper grey so the colour pops against it.)
    float k = max(1.0 - spareC, ch0.w);                            // living things keep their colour
    float rim = 0.0;
    int n = min(int(ch1.w), 48);
    float const R2 = (ch1.x + 3.0) * (ch1.x + 3.0);
    float3 wq = P + c5.xyz;                                        // world position: the edge pattern stays put as you move
    float  wob = 0.55 * sin(wq.x * 1.7 + sin(wq.z * 1.3) * 1.5) + 0.45 * sin(wq.z * 2.3 + sin(wq.x * 0.9 + wq.y) * 1.2);
    [loop] for (int i = 0; i < n && k < 0.999; ++i) {               // chP.w = the fade (CPU), 1 fresh .. 0 forgotten
        float3 d = P - chP[i].xyz;
        if (dot(d, d) > R2 || chP[i].w <= k) continue;               // too far, or cannot raise k
        float  r = length(float2(d.x, d.z)) + max(abs(d.y) - 2.5, 0.0) + wob * 0.45 * ch1.x * 0.3;
        float  e = smoothstep(ch1.x, ch1.x * 0.45, r);
        rim = max(rim, chP[i].w * exp(-pow((r - ch1.x * 0.78) / (0.16 * ch1.x + 0.15), 2.0)) * (1.0 - e * 0.6));
        k = max(k, chP[i].w * e);
    }
    float  lc   = lum3(c);
    float3 grey = (lc * lc * 0.25 + lc * 0.75) * ch0.rgb / max(lum3(ch0.rgb), 0.05);   // a touch deeper than flat grey
    float3 rich = lerp(lc.xxx, c, 1.25);                                               // fresh colour: a little richer
    float3 col  = lerp(c, max(rich, 0.0), saturate(k) * 0.6);
    float3 o    = lerp(grey, col, saturate(k));
    float3 vivid = max(lerp(lc.xxx, c, 2.2), 0.0) * 1.15 + 0.02;                      // the spreading edge, saturated and lit
    return lerp(o, vivid, saturate(rim) * 0.55);
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
        float across = exp(-pow(fu / width, 2.0)) * smoothstep(0.5, 0.36, abs(fu));   // zero at the column edge (no seams)
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
    float Rmax = bk0.w * 34.0 * c4.y / 2160.0;   // audit 2: 44 px + 16 taps cost 3-4.7 ms at 4K
    float R = bokehCoc(zz, zf, mode, p) * Rmax;
    float3 own = sceneAt(p); own *= own;
    if (R < 0.75) return own;
    float3 acc = own; float ws = 1.0;
    float ga = ign(float2(p)) * 6.2831;
    // the sky is smooth already: a light 5-tap blur there; the world 10 taps with the depth test
    int   N  = sky ? 5 : 10;
    float iN = 1.0 / float(N);
    [loop] for (int k = 0; k < N; ++k) {
        float r = sqrt((float(k) + 0.5) * iN) * R, a = ga + float(k) * 2.39996;
        int2  q = p + int2(cos(a) * r, sin(a) * r);
        float3 c = sceneAt(q); c *= c;
        float w = 1.0 + bk1.x * 6.0 * smoothstep(0.55, 0.95, lum3(c));
        [branch] if (!sky) {
            float dS = rawDepth(q);
            float zS = isSky(dS) ? 1e4 : viewZ(dS);
            if (zS < zz - 0.5) w *= saturate(bokehCoc(zS, zf, mode, q) * Rmax - r + 1.0);   // a nearer, sharper thing does not smear back here
        }
        acc += c * w; ws += w;
    }
    float3 o = acc / ws;
    float coc = R / max(Rmax, 1e-3);
    return o + float3(0.55, 0.42, 0.95) * bk1.z * 0.04 * coc * smoothstep(0.4, 0.9, lum3(o));   // a faint lilac fringe
}



// ---- [Purxiy] Block Outline: the 12 edges of the block you aim at, as depth-tested lines of light; the face you look at
// lit faintly. Only pixels whose ray passes the block's sphere do any work.
float segRayDist(float3 rd, float3 A, float3 B, out float tRay) {   // closest approach of the view ray (from the camera) to AB
    float3 u = B - A, w0 = -A;
    float  b = dot(rd, u), c = dot(u, u), d = dot(rd, w0), e = dot(u, w0);
    float  D = max(c - b * b, 1e-6);
    float  tc = saturate((e - b * d) / D);                   // a = dot(rd, rd) = 1
    tRay = b * tc - d;
    return length(w0 + rd * tRay - u * tc);
}
float3 blockOutline(float3 rd, float3 P, float dist, bool sky) {
    if (bo0.w <= 0.0) return 0;
    float3 lo = bo1.xyz - 0.002, hi = bo1.xyz + 1.002, cen = bo1.xyz + 0.5;
    float  tc = dot(cen, rd);
    float3 cp = cen - rd * tc;
    if (dot(cp, cp) > 1.2 || tc < -1.0) return 0;
    float  pxRad = c4.y / (2.0 * c1.w);                          // screen px per radian
    int    style = int(bo2.y + 0.5);
    float3 L = 0;
    [unroll] for (int k = 0; k < 12; ++k) {
        // edges: 4 along x, 4 along y, 4 along z
        int ax = k / 4, j = k % 4;
        float3 A = lo, B = lo;
        float2 o = float2(j & 1, j >> 1);
        if (ax == 0) { A = float3(lo.x, lerp(lo.y, hi.y, o.x), lerp(lo.z, hi.z, o.y)); B = float3(hi.x, A.y, A.z); }
        else if (ax == 1) { A = float3(lerp(lo.x, hi.x, o.x), lo.y, lerp(lo.z, hi.z, o.y)); B = float3(A.x, hi.y, A.z); }
        else { A = float3(lerp(lo.x, hi.x, o.x), lerp(lo.y, hi.y, o.y), lo.z); B = float3(A.x, A.y, hi.z); }
        float t;
        float dd = segRayDist(rd, A, B, t);
        if (t < 0.05 || (!sky && t > dist + 0.04)) continue;       // behind the camera / hidden by the world
        float px = dd / max(t, 1e-3) * pxRad;                      // distance from the lnK in screen px
        float w  = bo0.w * 0.5;
        float lnK = 1.0 - smoothstep(w - 0.6, w + 0.6, px);
        if (style == 2) {                                          // corners: only the ends of each edge
            float3 q = rd * t;
            float  along = dot(q - A, normalize(B - A));
            lnK *= step(abs(along - 0.5), 0.5) * (1.0 - step(0.2, min(along, 1.0 - along)));
        }
        float glowK = style == 1 ? 0.0 : bo1.w;
        float cm = 1.0;
        if (style == 2) {   // corners: the glow too stays at the corners
            float3 q2 = rd * t;
            float  al = dot(q2 - A, normalize(B - A));
            cm = 1.0 - smoothstep(0.16, 0.24, min(al, 1.0 - al));
        }
        L += bo0.rgb * (lnK * 1.25 + glowK * 0.45 * exp(-px / (3.0 + 6.0 * bo1.w)) * cm);
    }
    // the face you look at, faintly filled (a soft inner glow toward its edges)
    if (!sky && bo2.z >= 0.0 && bo2.x > 0.0) {
        float3 q = P - bo1.xyz;                                    // in the block, 0..1
        if (all(q > -0.01) && all(q < 1.01)) {
            int f = int(bo2.z + 0.5);
            float on = f == 0 ? step(q.y, 0.01) : f == 1 ? step(0.99, q.y) : f == 2 ? step(q.z, 0.01) : f == 3 ? step(0.99, q.z) : f == 4 ? step(q.x, 0.01) : step(0.99, q.x);
            float2 uv = f < 2 ? q.xz : f < 4 ? q.xy : q.zy;
            float  e  = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
            L += bo0.rgb * on * bo2.x * (0.35 + 0.65 * exp(-e * 9.0));
        }
    }
    return L;
}

// ---- [Purxiy] Wings + Cape: shapes in the air behind you, traced as planes (like the halo); `over` = a translucent
// layer they lay over the world (rgb, a), the return value is light added on top
float sdSeg(float2 p, float2 a, float2 b) { float2 pa = p - a, ba = b - a; return length(pa - ba * saturate(dot(pa, ba) / dot(ba, ba))); }
float3 wingsCape(float3 rd, float dist, float t, inout float4 over, inout float capeT) {
    float3 L = 0;
    if (pl.w < -50.0) return L;
    bool  fp  = length(pl.xyz + float3(0, 1.62, 0)) < 0.6;
    float yaw = pl.w;
    float3 fwd  = float3(-sin(yaw), 0, cos(yaw));
    float3 side = float3(cos(yaw), 0, sin(yaw));
    float3 up   = float3(0, 1, 0);
    // -------- wings
    [branch] if (wg0.w > 0.0 && (!fp || wg1.w > 0.5)) {
        int   design = int(wg1.x + 0.5);
        float S = wg0.w;
        float flap = wg1.z * (0.22 + 0.25 * cp2.z) * sin(t * (2.2 + 3.0 * cp2.z));
        float3 A0 = pl.xyz + up * 1.38 - fwd * 0.20;
        [unroll] for (int sgn = -1; sgn <= 1; sgn += 2) {
            float fold = 0.55 + flap;                              // how far back the wing sweeps from the side (radians)
            float3 w = normalize(side * float(sgn) * cos(fold) - fwd * sin(fold));
            float3 A = A0 + side * float(sgn) * 0.10;
            float3 n = normalize(cross(w, up));
            float den = dot(rd, n);
            if (abs(den) < 1e-4) continue;
            float tp = dot(A, n) / den;
            if (tp < 0.2 || tp > dist + 0.05) continue;
            float3 h = rd * tp - A;
            float u = dot(h, w) / S, v = dot(h, up) / S;            // u: out along the wing, v: up
            if (u < 0.0 || u > (design == 0 ? 2.1 : 1.45) || (design == 0 && (v > 0.8 || v < -1.3))) continue;
            float inside = 0.0, rim = 0.0, pat = 0.0;
            if (design == 1) {                                     // butterfly: a big upper lobe, a smaller lower one
                // (in game 2026-11-19: both lobes ended on a straight vertical line at the back - "cut off". They now
                //  narrow into the shoulder: the height is squeezed toward the root, so each lobe is a teardrop)
                float pin = lerp(0.16, 1.0, smoothstep(0.0, 0.42, u));
                float2 pv = float2(u, v / pin);
                float d1 = length((pv - float2(0.55, 0.38)) / float2(0.62, 0.48)) - 1.0;
                float d2 = length((pv - float2(0.42, -0.30)) / float2(0.42, 0.36)) - 1.0;
                float d  = min(d1, d2) * pin;
                inside = 1.0 - smoothstep(-0.02, 0.02, d);
                rim    = exp(-abs(d) * 30.0);
                pat    = exp(-pow(length(float2(u, v) - float2(0.72, 0.42)) / 0.11, 2.0)) + 0.5 * exp(-pow(length(float2(u, v) - float2(0.45, -0.28)) / 0.08, 2.0));
            } else if (design == 0) {                              // seraph: a covert arm and twelve overlapping rounded flight feathers, longer
                                                                   // toward the tip, fanning from straight down to out and back
                float2 p = float2(u, v);
                float2 a0 = float2(0.0, 0.02), a1 = float2(0.38, 0.46), a2 = float2(1.0, 0.42);
                float dArm = min(sdSeg(p, a0, a1) - 0.14, sdSeg(p, a1, a2) - 0.11);
                float d1 = 9.0, d2 = 9.0, ax = 9.0;                // nearest / second nearest feather, its shaft
                [unroll] for (int k = 0; k < 12; ++k) {
                    float tk = float(k) / 11.0;
                    float2 B = tk < 0.35 ? lerp(a0, a1, tk / 0.35) : lerp(a1, a2, (tk - 0.35) / 0.65);
                    float an = radians(lerp(-100.0, -42.0, tk * tk));
                    float L = lerp(0.36, 0.78, pow(tk, 0.7)) * (1.0 + 0.04 * sin(t * 1.3 + tk * 9.0));
                    float2 E = B + float2(cos(an), sin(an)) * L;
                    float r = lerp(0.135, 0.105, tk);
                    float2 pa = p - B, ba = E - B;
                    float h = saturate(dot(pa, ba) / dot(ba, ba));
                    float dk = length(pa - ba * h) - r * (1.0 - 0.35 * h * h);   // tapers a little to a round tip
                    if (dk < d1) { d2 = d1; d1 = dk; ax = length(pa - ba * h) / max(r, 1e-3); } else d2 = min(d2, dk);
                }
                float d = min(dArm, d1);
                inside = 1.0 - smoothstep(-0.012, 0.012, d);
                rim    = exp(-abs(d) * 40.0);
                float sep  = exp(-pow((d2 - d1) / 0.010, 2.0)) * step(d1, dArm);        // the line between two feathers
                float shaft = exp(-pow(ax / 0.10, 2.0)) * step(d1, dArm) * 0.6;           // each feather's quill
                pat = 0.55 + 0.45 * saturate(1.0 - (0.44 - v) * 0.9) - 0.55 * sep + 0.35 * shaft + 0.25 * step(dArm, d1);
            } else {                                               // prism: a long wing, cut into crystal facets
                float top = 0.14 + 0.30 * pow(sin(3.14159 * saturate(u / 1.25)), 0.6) + 0.16 * u;      // the arm of the wing, swept up
                // (in game 2026-11-19: the wing was widest at its tip and ended on a straight cut. It now swells to
                //  two thirds of its length and closes to a point at the tip)
                float span = (0.20 + 0.40 * pow(saturate(u / 0.85), 0.8)) * (1.0 - smoothstep(0.80, 1.30, u));
                float bot = top - span + 0.045 * abs(sin(u * 13.0)) * saturate(span * 6.0);
                float d   = max(max(v - top, bot - v), u - 1.30);
                inside = 1.0 - smoothstep(-0.015, 0.015, d);
                rim    = exp(-abs(d) * 34.0);
                float2 g = float2(u * 6.0 + v * 3.2, v * 6.5 - u * 1.4);
                float2 gi = floor(g), gf = frac(g);
                float tri = step(gf.x, gf.y);
                float hh = frac(sin(dot(gi + tri * 7.31, float2(12.9898, 78.233))) * 43758.5453);
                float edgeF = min(min(gf.x, gf.y), min(1.0 - max(gf.x, gf.y), abs(gf.x - gf.y)));
                pat = 0.25 + 0.75 * hh + 0.9 * exp(-edgeF * 40.0);
            }
            if (inside <= 0.0 && rim <= 0.01) continue;
            float root = smoothstep(0.04, 0.22, u);                 // they grow out of the back, no hard seam on it
            inside *= root; rim *= root;
            float3 c = lerp(wg0.rgb, float3(1.0, 0.97, 1.0), saturate(v * 1.6 + 0.1) * 0.45);   // whiter toward the top edge
            if (design == 2) c = lerp(wg0.rgb, float3(0.75, 0.9, 1.0), pat * 0.6) * (0.85 + 0.3 * pat);
            float glow = wg1.y;
            if (design == 0) {                                     // seraph: soft lit feathers, the rim only a gentle edge light
                c = lerp(wg0.rgb, float3(1.0, 0.97, 1.0), saturate(pat - 0.35) * 0.75);
                L += c * (rim * (0.25 + 0.45 * glow) + inside * pat * (0.10 + 0.22 * glow));
            } else
                L += c * (rim * (0.9 + glow) + inside * (0.08 + 0.30 * pat) * (0.6 + glow));
            float a = inside * (design == 2 ? 0.30 : design == 0 ? 0.55 * saturate(pat) : 0.22);
            over.rgb = lerp(over.rgb, c * 0.6 + 0.08, a * (1.0 - over.a));
            over.a = over.a + a * (1.0 - over.a);
        }
    }
    // -------- cape: hangs from the shoulders, swings back as you run, ripples
    [branch] if (cp0.w > 0.0 && !fp) {
        int   design = int(cp1.w + 0.5);
        // (in game 2026-11-17: it clipped through the armour and the body. A chestplate stands ~0.19 out from the body's
        // centre, the cape hung from 0.17 at ~6 degrees - so it now hangs from behind the armour and always clears the
        // legs; turning (cp2.w: how fast the body turns, 0..1) lifts it out like real cloth instead of cutting through)
        float th = 0.17 + 0.55 * saturate(cp2.y) * cp2.x + 0.35 * cp2.z * cp2.x + 0.30 * saturate(cp2.w) * cp2.x + 0.04 * sin(t * 1.7) * cp2.x;
        float3 C0 = pl.xyz + up * 1.42 - fwd * 0.235;
        float3 D  = normalize(-up * cos(th) - fwd * sin(th));
        float3 n  = normalize(cross(side, D));
        float den = dot(rd, n);
        if (abs(den) > 1e-4) {
            float tp = dot(C0, n) / den;
            if (tp > 0.2 && tp < dist + 0.03) {
                float3 h = rd * tp - C0;
                float s = dot(h, side), l = dot(h, D);
                float hw = 0.31 + 0.06 * l;
                float bottom = 1.02 + 0.05 * sin(s * 11.0 + t * 3.0);
                float d = max(abs(s) - hw, max(-l, l - bottom));
                float inside = 1.0 - smoothstep(-0.01, 0.01, d);
                if (inside > 0.5) capeT = min(capeT, tp);   // [ATMOS_DEPTH] the cloth is solid: later draws (glass armour) go behind it
                if (inside > 0.0) {
                    float wave = sin(l * 7.0 - t * (3.0 + 4.0 * cp2.y)) * 0.5 + 0.5;
                    float shade = 0.55 + 0.45 * wave;                // the folds catch the light
                    float3 c;
                    float3 add = 0;
                    if (design == 1) {                               // aurora: two colours flowing down in bands
                        float b = 0.5 + 0.5 * sin(l * 5.0 + s * 4.0 - t * 1.6 + 2.0 * sin(s * 3.0 + t * 0.7));
                        float b2 = 0.5 + 0.5 * sin(l * 9.0 - s * 6.0 - t * 2.3);
                        c = lerp(cp0.rgb * 0.45, float3(0.05, 0.08, 0.12), 0.35) + lerp(cp0.rgb, float3(0.35, 0.95, 0.75), b * b2) * 0.35;
                        add = lerp(cp0.rgb, float3(0.35, 0.95, 0.75), b) * cp0.w * 0.45 * pow(b * (0.6 + 0.4 * b2), 3.0);
                    } else if (design == 2) {                        // starfield: night cloth with stars
                        c = lerp(float3(0.02, 0.02, 0.05), cp0.rgb * 0.15, l);
                        float2 g = float2(s, l) * 18.0;
                        float2 ci = floor(g), cf = frac(g) - 0.5;
                        float hs = frac(sin(dot(ci, float2(127.1, 311.7))) * 43758.5453);
                        float st = step(0.82, hs) * exp(-dot(cf, cf) * 40.0) * (0.6 + 0.4 * sin(t * 3.0 + hs * 30.0));
                        add = cp1.rgb * st * 1.6;
                    } else {                                         // silk: the colour fades darker down the cape, a sheen moves down it
                        c = lerp(cp0.rgb, cp0.rgb * 0.35, l) * 0.8;
                        add = cp1.rgb * 0.25 * cp0.w * exp(-pow((l - frac(t * 0.25) * 1.4 + 0.2) / 0.08, 2.0));
                    }
                    float edge = exp(-abs(d) * 50.0);
                    c *= shade;
                    over.rgb = lerp(over.rgb, c, inside * (1.0 - over.a));
                    over.a = over.a + inside * (1.0 - over.a);
                    L += (add + cp0.rgb * edge * cp0.w * 0.8) * inside;
                }
            }
        }
    }
    return L;
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
    float3 skyC = lerp(float3(0.42, 0.48, 0.62), float3(0.03, 0.035, 0.06), night);
    // audit: the reflection is the real scene - where the mirrored ray points far away, looked up on screen (the sky,
    // distant hills); a fixed grey-blue read milky at sunset. Off screen: the grey-blue.
    float3 r = reflect(rd, n);
    float3 fp = P + r * 220.0;
    float  fz = dot(fp, c0.xyz);
    if (fz > 0.5) {
        float2 nd = float2(dot(fp, c1.xyz) / (fz * c0.w), dot(fp, c2.xyz) / (fz * c1.w));
        float2 sp = float2((nd.x * 0.5 + 0.5) * c4.x, (0.5 - nd.y * 0.5) * c4.y);
        float  edge = smoothstep(0.0, 0.08, min(min(sp.x / c4.x, 1.0 - sp.x / c4.x), min(sp.y / c4.y, 1.0 - sp.y / c4.y)));
        if (edge > 0.0) { float3 sc = sceneAt(int2(sp)); skyC = lerp(skyC, sc * sc, edge); }
    }
    skyC += float3(0.7, 0.75, 0.95) * wxL.x * 0.6;
    float fres = 0.04 + 0.96 * pow(1.0 - saturate(dot(-rd, n)), 5.0);
    L *= lerp(1.0, 0.55, wetK * (0.55 + 0.45 * puddle));                      // wet things are darker
    L = lerp(L, skyC, saturate(wetK * (fres * (0.4 + 0.6 * puddle) + 0.38 * puddle)));   // and glossy; puddles mirror
    // ripples in the puddles: a ring per 0.6-block cell, each on its own beat
    float2 g = wp.xz * 1.7; float2 cl = floor(g), f = frac(g) - 0.5;
    float h = frac(sin(dot(cl, float2(12.9898, 78.233))) * 43758.5453);
    float ph = frac(t * (0.9 + 0.5 * h) * wx0.y + h);
    float ring = exp(-pow((length(f - (h - 0.5) * 0.3) - ph * 0.42) / 0.03, 2.0)) * (1.0 - ph);
    L += (skyC + 0.04) * ring * puddle * wetK * 0.9;
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


// ---- [Purxiy] Cyberspace: the world as light, wires and data. Every block edge a glowing line, circuit traces with nodes
// across the faces, a scan pulse rolling out from you, a dark data sky. Original code.
float cyHash(float3 p) { return frac(sin(dot(p, float3(127.1, 311.7, 74.7))) * 43758.5453); }
float3 cyberspace(float3 L, int2 px, float3 P, float3 rd, float dist, bool sky, float t) {
    float3 acc = cy0.rgb;
    float reality = saturate(cy0.w);
    float lumW = dot(L, float3(0.2126, 0.7152, 0.0722));
    if (sky) {   // the data sky: deep and dark, a faint horizon line, columns of light falling slowly
        float3 c = lerp(acc * 0.06, float3(0.005, 0.006, 0.012), saturate(rd.y * 1.5 + 0.2));
        c += acc * exp(-abs(rd.y) * 40.0) * 0.6;
        if (cy1.w > 0.0) {
            float an = atan2(rd.z, rd.x) * 110.0;
            float col = floor(an), fc = frac(an);
            float h = frac(sin(col * 12.9898) * 43758.5453);
            float y = frac(rd.y * 3.0 + t * (0.15 + 0.3 * h) + h);
            float wcol = abs(fc - 0.5) * 2.0;                       // 0 in the column's middle .. 1 at its edge
            c += acc * step(0.72, h) * (1.0 - smoothstep(0.10, 0.25, wcol)) * pow(y, 10.0) * 1.3 * saturate(rd.y * 3.0 + 0.3);
        }
        float3 ghost = acc * lumW * 0.55 + lumW * 0.06;          // what shows through: the clouds as faint accent ghosts, never their own hue
        c += ghost * 0.35;
        float ageS = abs(cy1.w) - 1.0;
        return lerp(L, lerp(ghost, c, reality), saturate((ageS - 0.35) * 5.0));   // the sky turns once the front has passed
    }
    float3 n = cross(posAt(px + int2(1, 0)) - P, posAt(px + int2(0, 1)) - P);
    n = normalize(n + 1e-6); if (dot(n, rd) > 0.0) n = -n;
    float3 an = abs(n);
    float3 wp = c5.xyz + P + n * 0.002;
    float2 uv = an.x > an.y && an.x > an.z ? wp.zy : (an.y > an.z ? wp.xz : wp.xy);
    float2 f = frac(uv), cell = floor(uv);
    float2 aa = max(fwidth(uv), 1e-4);
    // block edges
    float2 e2 = min(f, 1.0 - f) / aa;
    float edge = 1.0 - smoothstep(0.6, 1.8, min(e2.x, e2.y));
    // circuit traces: each face gets a few lines at quarter steps and a node where they end
    float3 fid = float3(cell, floor(dot(wp, an)));
    float h1 = cyHash(fid), h2 = cyHash(fid + 7.31);
    float lane = (floor(h1 * 3.0) + 1.0) * 0.25;
    float2 q = f - lane;
    float traceA = (1.0 - smoothstep(0.6, 1.6, abs(q.y) / aa.y)) * step(h2, 0.65) * step(f.x, 0.25 + 0.6 * h2);
    float traceB = (1.0 - smoothstep(0.6, 1.6, abs(f.x - (1.0 - lane)) / aa.x)) * step(0.35, h2) * step(1.0 - 0.7 * h1, f.y);
    float node = 1.0 - smoothstep(0.035, 0.06, length(f - float2(0.25 + 0.6 * h2, lane)));
    float circuits = saturate(traceA + traceB + node * step(h2, 0.65)) * cy1.y;
    // the scan: a ring rolling out from you every few seconds
    float per = max(cy1.z, 1.0);
    float rr = length(P.xz);
    float front = frac(t / per) * 60.0;
    float scan = exp(-pow((rr - front) / 1.2, 2.0)) * (1.0 - front / 60.0);
    float pulse = 0.55 + 0.45 * sin(t * 2.0 + h1 * 6.28);
    float fog = exp(-dist * 0.012);
    float3 base = acc * (0.05 + 0.25 * lumW) + lumW * 0.08;               // the world, dark and tinted
    float3 lines = acc * (edge * (0.9 + 1.5 * scan) * cy1.x * 1.4 + circuits * pulse * 0.9 * cy1.x + scan * 0.35);
    float3 dig = base + lines * fog + acc * scan * 0.12;
    // (Purxiy reference, video 3: switching on, a bright wavefront dome sweeps out from you and the world turns digital
    //  behind it - ~250 ms to the horizon, a white-hot front with the accent glowing behind it)
    float age = abs(cy1.w) - 1.0;
    float R = age * 260.0;                                                // blocks: past the view in ~0.4 s
    float d3 = length(P);
    float inside = smoothstep(R + 1.5, R - 1.5, d3);
    float frontW = exp(-pow((d3 - R) / (0.7 + 0.012 * R), 2.0)) * exp(-age * 2.5);
    float3 o = lerp(L, dig, reality * inside);
    return o + (float3(1.0, 0.98, 0.95) * 0.7 + acc * 1.1) * frontW * step(age, 1.5);
}

// ---- [Purxiy] Cute World: a soft pastel world with drifting hearts and little sparkles. Original code.
float3 cuteWorld(float3 L, float3 P, float3 rd, float dist, bool sky, float t) {
    float soft = saturate(cu0.w);
    float lumC = dot(L, float3(0.2126, 0.7152, 0.0722));
    // pastel: colours lifted toward a milky pink-lilac, contrast eased
    float3 pastel = lerp(L, lumC * float3(1.0, 0.84, 0.95) + 0.12, 0.42 * cu1.z) * (0.94 + 0.08 * soft);
    pastel = lerp(pastel, sqrt(max(pastel, 0.0)) * 0.95, 0.25 * soft);
    float3 o = lerp(L, pastel, cu1.z > 0.0 ? 1.0 : 0.0);
    // hearts drifting up in the air around you (a few per cell of a 3-block grid, depth-tested)
    float3 hl = 0;
    [unroll] for (int k = 1; k <= 4; ++k) {   // (2026-11-19: bigger, glossier, more of them - they were specks)
        float R = float(k) * 2.5;
        float tp = R;
        if (tp > dist) break;
        float3 q = rd * tp + c5.xyz;
        float3 cid = floor(q / 3.0);
        float h = cyHash(cid + float(k));
        if (h > cu1.x * 0.85) continue;
        float life = frac(t * 0.12 + h);                                  // 0..1 as it rises; it fades in and out (no pop at the top)
        float3 ctr = (cid + 0.5) * 3.0 + float3(0, life * 1.6 - 0.8, 0);   // drifts inside its own cell
        float3 d = rd * dot(ctr - c5.xyz, rd) - (ctr - c5.xyz);
        float dz = dot(ctr - c5.xyz, rd);
        if (dz < 0.3 || dz > dist) continue;
        float2 pl2 = float2(dot(d, normalize(cross(rd, float3(0, 1, 0)))), d.y) / (0.20 + 0.08 * h);
        pl2.y = pl2.y + 0.25;                                               // (was flipped: the hearts hung upside down)
        float hx = abs(pl2.x);
        float heart = pl2.y - sqrt(max(hx, 0.0)) * 0.55;                  // a classic heart curve
        float sd = length(float2(hx, heart)) - 0.75;
        float fill = 1.0 - smoothstep(-0.05, 0.08, sd);
        float halo = exp(-max(sd, 0.0) * 3.5) * (1.0 - fill) * 0.7;       // a soft glow around it
        float2 hp = pl2 - float2(-0.32, 0.62);
        float hi = exp(-dot(hp, hp) * 9.0) * fill + exp(-dot(hp - float2(0.12, -0.1), hp - float2(0.12, -0.1)) * 60.0) * fill * 1.5;   // candy gloss + a hard glint
        float edge = smoothstep(-0.32, 0.0, sd) * fill;                     // a deeper tone toward the outline
        float3 hc = lerp(cu0.rgb * (1.15 - 0.4 * edge), float3(1.0, 0.97, 0.99), saturate(0.4 * hi));
        float win = smoothstep(0.85, 0.55, length(d));                      // the glow ends well inside the cell: no box
        win *= smoothstep(0.0, 0.18, life) * smoothstep(1.0, 0.72, life);  // born and gone softly
        win *= smoothstep(1.35, 0.7, abs(dz - R));                         // as you move it fades instead of blinking out
        hl += (hc * fill * 1.0 + cu0.rgb * halo * 0.8) * win * (0.8 + 0.2 * sin(t * 2.0 + h * 20.0));
    }
    // sparkles on the ground / scenery near you
    if (!sky && cu1.y > 0.0 && dist < 24.0) {
        float3 wp = (c5.xyz + P) * 4.0;
        float3 ci = floor(wp);
        float hs = cyHash(ci);
        float tw = pow(saturate(sin(t * 3.0 + hs * 40.0)), 16.0);
        float3 fq = frac(wp) - 0.5;
        o += float3(1.0, 0.95, 1.0) * step(0.975 - 0.03 * cu1.y, hs) * tw * exp(-dot(fq, fq) * 22.0) * 2.2 * (1.0 - dist / 24.0);
    }
    return o + hl;
}

#if ATMOS_DEPTH
struct AtmosOut { float4 c : SV_Target; float dep : SV_Depth; };
AtmosOut PSWorldFxAtmos(VSOut i) {
#else
float4 PSWorldFxAtmos(VSOut i) : SV_Target {
#endif
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
    float3 nq  = cross(ddx(q), ddy(q));                       // the surface's facing, from the depth around this pixel
    float  upN = abs(nq.y) / max(length(nq), 1e-9);
    [branch] if (fx6.w > 0.5) ambL += worldEvents(q, sky, t, upN, rd);
    [branch] if (cs0.w > 0.0 || au0.w > 0.0 || ha0.w > 0.0) ambL += cosmetics(q, rd, sky ? 1e4 : dist, sky, t);
    float4 overC = 0;                                        // [Purxiy] wings / cape cloth over the world
    float capeT = 1e9;
    [branch] if (wg0.w > 0.0 || cp0.w > 0.0) ambL += wingsCape(rd, sky ? 1e4 : dist, t, overC, capeT);
    [branch] if (bo0.w > 0.0) ambL += blockOutline(rd, q, sky ? 1e4 : dist, sky);
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
    bool const post = ch0.w > -0.5 || wx1.w > 0.5 || bk0.w > 0.0 || cy0.w > 0.0 || cu1.w > 0.5;
    float3 radL = 0;
    [branch] if (rad1.w > 0.5 && !sky && aInfo.x > 0.5) radL = radiance(int2(px), q, zc);   // outlines on players / mobs / items
    if (!drew && trans > 0.998 && dot(ambL, 1.0) < 0.002 && !post && dot(radL, 1.0) < 0.002 && overC.a < 0.002) discard;
    if (!drew) outL = col * trans + acc;
    outL = lerp(outL, overC.rgb, saturate(overC.a));
    [branch] if (ch0.w > -0.5) outL = chroma(outL, q, sky, spareC);       // the grey world, colour where you have walked
    outL += radL;
    [branch] if (cy0.w > 0.0) outL = cyberspace(outL, int2(px), q, rd, sky ? 1e4 : dist, sky, t);
    [branch] if (cu1.w > 0.5) outL = cuteWorld(outL, q, rd, sky ? 1e4 : dist, sky, t);
    [branch] if (wx1.w > 0.5 && wx0.x < 0.5) {                           // rain: wet ground, lightning
        [branch] if (!sky && wx1.y > 0.0) outL = wetGround(outL, int2(px), q, rd, dist, t, night);
        outL = lightning(outL, rd, sky);
    }
    [branch] if (wx1.w > 0.5) outL += weather(rd, sky ? 1e4 : dist, t, night);
    float3 o = sqrt(max(outL + ambL, 0.0)) + (ign(px.yx + 13.7) - 0.5) / 255.0;
#if ATMOS_DEPTH
    AtmosOut r;
    r.c = float4(o, 1.0);
    // the depth stays the scene's, except where the cape's cloth is in front: there it is the cloth's (viewZ inverted)
    r.dep = capeT < 1e8 ? saturate(1.0 - 0.025 * length(ray) / max(capeT, 0.05)) : d;
    return r;
#else
    return float4(o, 1.0);
#endif
}
