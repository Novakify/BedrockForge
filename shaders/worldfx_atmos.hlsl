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
float fbm2(float3 p) { return vnoise3(p) * 0.65 + vnoise3(p * 2.03 + 17.1) * 0.35; }

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
    [loop] for (int k = 0; k < 28 && tt < tMax; ++k) {
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
                float  size = amb1.w * (style == 1 ? 0.018 : style == 3 ? 0.035 : style == 2 ? 0.05 : 0.03) * (0.6 + 0.8 * h.z);
                float  coc  = amb3.y > 0.5 ? max(0.0, 2.2 - tp) * 0.03 : 0.0;   // near ones: a soft round bokeh
                float  r    = size + coc;
                float  core = exp(-dd * dd / (r * r));
                float  halo = amb2.x * exp(-dd / (r * 4.0)) * 0.22;
                float  tw   = 1.0 - amb2.z * (0.5 + 0.5 * sin(t * (2.0 + 3.0 * h.y) + ph * 3.0));   // twinkle / breathe
                float  fade = smoothstep(tMax, tMax * 0.65, tp) * (r > size ? size * size / (r * r) * 1.6 + 0.12 : 1.0);
                float3 cc   = lerp(amb0.rgb, amb1.rgb, h.z);
                if (style == 3) cc = lerp(float3(0.85, 0.90, 1.0), cc, 0.25);   // snow stays white
                acc += cc * (core * 1.6 + halo) * tw * fade * lerp(0.55, 1.0, night);
            }
        }
        // the next cell along the ray
        if (tNext.x < tNext.y && tNext.x < tNext.z) { tt = tNext.x; tNext.x += tDel.x; cell.x += stp.x; }
        else if (tNext.y < tNext.z) { tt = tNext.y; tNext.y += tDel.y; cell.y += stp.y; }
        else { tt = tNext.z; tNext.z += tDel.z; cell.z += stp.z; }
    }
    return acc;
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
    float3 acc = 0; float trans = 1.0;
    float  spare = 1.0;
    [branch] if (!sky && (fl & 24u) != 0u) spare = stillWorld(q);

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
            int   N  = int(lerp(6.0, 12.0, saturate(c3.z) * saturate((tB - tA) / 40.0)));
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
    // ---- haze: everything the mist let through takes on the haze colour with distance (closed form)
    [branch] if ((fl & 2u) != 0u && c6.w > 0.0) {
        float H   = c7.w, clr = c8.x;
        float t0  = min(clr, dist);
        float od  = hazeDepth(camY + rd.y * t0, rd, max(dist - t0, 0.0), c3.y, H * 0.5) * (0.010 + 0.030 * c6.w);
        float fog = (1.0 - exp(-od)) * c6.w * 1.25;
        fog = saturate(fog) * smoothstep(clr * 0.5, clr * 1.5 + 1.0, dist);
        // the colour drifts across the world between the palette's two colours
        float3 wp  = (c5.xyz + rd * min(dist, 80.0)) * 0.012 + float3(t * 0.010 * c8.y, 0.0, t * 0.006 * c8.y);
        float  mixk = smoothstep(0.25, 0.75, vnoise3(wp));
        float3 hc  = lerp(c6.rgb, c7.rgb, mixk);
        hc *= lerp(1.0, 0.22, night);                                // dim at night
        float  mu  = dot(rd, LIT);
        hc += lerp(float3(1.0, 0.82, 0.62), float3(0.55, 0.62, 1.0), night) * pow(saturate(mu), 8.0) * c8.z * 0.6 * litK;   // glow toward the sun / moon
        if (sky) fog *= 0.55 * smoothstep(0.45, 0.0, rd.y);         // the sky: only toward the horizon
        float m = (fl & 8u) != 0u ? spare : 1.0;                     // players and mobs keep their colours
        fog *= m;
        float3 behind = col * trans + acc;
        float3 tinted = behind * hc * 1.45 + hc * lerp(0.16, 0.03, night);   // the world takes on the colour, and the air holds a little of it
        float3 o = lerp(behind, tinted, fog) + ambL;
        o = sqrt(max(o, 0.0)) + (ign(px.yx + 13.7) - 0.5) / 255.0;
        return float4(o, 1.0);
    }
    if (trans > 0.998 && dot(ambL, 1.0) < 0.002) discard;
    float3 o = sqrt(max(col * trans + acc + ambL, 0.0)) + (ign(px.yx + 13.7) - 0.5) / 255.0;
    return float4(o, 1.0);
}
