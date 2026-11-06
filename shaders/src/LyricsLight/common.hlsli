// LyricsLight by Novak - terrain + mob lighting that follows the sky (original work, MIT).
// LL_TIER 1 = Soft (gentle, cheap), 2 = Extreme (water reflections + sun glints, glowing lamps/ores, flicker, punchier).
// The sky colour comes from FogColor: the game's own fog, or the LyricsForge sky once Bedrock Forge feeds it in.
#ifndef LL_TIER
#define LL_TIER 1
#endif
#define LL_EXTREME (LL_TIER >= 2)

float  llLuma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
float  llMax3(float3 c) { return max(c.r, max(c.g, c.b)); }
float3 llSat(float3 c, float s) { float l = llLuma(c); return lerp(float3(l, l, l), c, s); }

// key light: high in the south-west, so tops read sunlit, one side warm and the other in cool shade
static const float3 LL_KEY = float3(-0.4376, 0.8127, 0.3845);

// the sky's hue at a fixed brightness (the game darkens fog at night; we want the colour, not the level)
float3 llSkyHue(float3 fog) {
    float m = llMax3(fog);
    float3 h = m > 1e-3 ? fog / m : float3(0.62, 0.76, 1.0);
    return lerp(float3(0.62, 0.76, 1.0), h, 0.72);
}
float3 llUnitLuma(float3 c) { return c / max(llLuma(c), 1e-3); }

// face normal from the world position; blocks snap to their axis (no edge noise), plants/mobs keep the true one
float3 llNormal(float3 wp, float3 V, bool snap) {
    float3 n = normalize(cross(ddx(wp), ddy(wp)));
    if (dot(n, V) < 0.0) n = -n;
    if (snap) {
        float3 a = abs(n);
        float  m = llMax3(a);
        if (m > 0.92) n = a.x == m ? float3(sign(n.x), 0.0, 0.0) : (a.y == m ? float3(0.0, sign(n.y), 0.0) : float3(0.0, 0.0, sign(n.z)));
    }
    return n;
}

// soft shoulder: keeps highlights from clipping to flat white
float3 llShoulder(float3 c) {
    float m = llMax3(c);
    if (m <= 0.82) return c;
    float k = 0.82 + 0.18 * (1.0 - exp(-(m - 0.82) / 0.18));
    return c * (k / m);
}

float3 llGrade(float3 c) {
#if LL_EXTREME
    c = llSat(c, 1.16);
    c = lerp(c, c * c * (3.0 - 2.0 * c), 0.22);   // gentle S-curve: deeper shade, richer mids
#else
    c = llSat(c, 1.07);
#endif
    return llShoulder(c);
}

struct LLDir { float3 sunCol; float3 hue; float day; float sunVis; float wrap; };

// everything the lighting needs about the sun and sky at this pixel
LLDir llDir(float3 fog, float3 n, float skyLevel, float dayLevel) {
    LLDir o;
    o.day    = saturate(dayLevel);
    o.hue    = llSkyHue(fog);
    // sunlight: warm by day, cool silver under the moon, coloured a little by the sky (sunset skies -> golden light)
    float3 sun  = lerp(float3(1.0, 0.90, 0.76), o.hue * float3(1.15, 1.0, 0.85), 0.25);
    o.sunCol = lerp(float3(0.70, 0.79, 1.0), sun, smoothstep(0.30, 0.75, o.day));
    o.sunVis = smoothstep(0.80, 0.97, skyLevel);                  // open sky: stands in for "not shadowed"
    o.wrap   = saturate((dot(n, LL_KEY) + 0.25) / 1.25);
    return o;
}

// relative light shaping on top of the game's own sky light: lit faces warm and brighter, shaded faces cool
float3 llSkyShape(LLDir d, float3 n) {
#if LL_EXTREME
    const float lo = 0.74, hi = 1.26, tintAmt = 0.55;
#else
    const float lo = 0.84, hi = 1.16, tintAmt = 0.34;
#endif
    float k     = d.wrap * d.sunVis * lerp(0.45, 1.0, d.day);
    float bright = lerp(lo, hi, k) + 0.05 * saturate(n.y);
    float3 tint = lerp(llUnitLuma(d.hue), llUnitLuma(d.sunCol), k);
    return bright * lerp(float3(1.0, 1.0, 1.0), tint, tintAmt);
}

// torchlight: warm, falls off softly; Extreme flickers a touch
float3 llBlock(float level, float t) {
    float b = pow(saturate(level), 1.7);
#if LL_EXTREME
    b *= 1.22 * (1.0 + 0.035 * sin(t * 9.1) + 0.025 * sin(t * 13.7 + 1.3));
#else
    b *= 1.06;
#endif
    return float3(1.0, 0.68, 0.40) * b;
}

// Vanilla distance-dither fade (matches RenderDragon's Actor/RenderChunk alpha-test code): true = discard this pixel.
//   fwd = view forward (u_view row 2), cam = camera position, clip = clip-space position from the vertex stage,
//   p2 = DitherParams2[k] (x start, y end, z cell size), p = DitherParams (x,y = screen size)
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
