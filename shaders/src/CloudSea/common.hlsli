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

// alb linear (corner shade already in it), n = face normal, sunVis = open sky toward the sun, sky = sky light 0..1,
// leaf = foliage (light passes through), V = toward the eye
float3 csLight(float3 alb, float3 n, float sunVis, float sky, float leaf, float3 V) {
    float green = saturate((alb.g - max(alb.r, alb.b)) * 5.0);   // pure greens refuse to sit in a sunset: toward olive
    alb = lerp(alb, csLuma(alb) * float3(1.22, 0.98, 0.42) * 1.0, green * 0.42);
    float3 sunC = float3(1.00, 0.66, 0.40) * 2.35;
    float  ndl  = max(dot(n, CS_SUN), 0.0);
    float3 amb  = lerp(float3(0.255, 0.135, 0.140), float3(0.160, 0.150, 0.300), smoothstep(-1.0, 1.0, n.y));
    float  toSun = dot(n.xz, normalize(CS_SUN.xz));
    float  side  = 1.0 - abs(n.y);
    amb += float3(0.40, 0.165, 0.070) * max(toSun, 0.0) * side;
    amb += float3(0.105, 0.085, 0.150) * max(-toSun, 0.0) * side;
    amb *= lerp(0.08, 1.0, sky * sky);                             // caves and covered spots lose the sky
    float3 dir = sunC * (ndl + 0.035 * max(n.y, 0.0)) * sunVis;
    if (leaf > 0.5) {
        float mu    = max(dot(-V, CS_SUN), 0.0);
        float trans = pow(mu, 4.0) * 0.9 + 0.12;
        float wrap  = max(dot(n, CS_SUN) * 0.5 + 0.5, 0.0);
        dir = (sunC * wrap * 0.55 + float3(1.15, 0.80, 0.32) * 2.1 * trans) * sunVis;
        amb *= 1.7;
    }
    return alb * (amb + dir);
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
