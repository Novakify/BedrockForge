// ---- the materials a body can turn into, for the Finishers 2 death effects (by kyryi / dulimo, used with permission;
//      Bedrock / Renderer Foundation port by Novak). dsPart blends one over the skin by fx.matK. Every material is
//      shaded in the world's own 1/16-block texels, like the blocks around it - no soft gradients, no smears.
//      alb = the skin's colour at this texel, nw = world normal, q = position in the part (px), e = its half size,
//      nl = face normal (part space), n = this texel's random, hgt = height up the body 0..1, C = the tint asked for.
#define M_HOLO 0
#define M_ICE 1
#define M_STONE 2
#define M_GOLD 3
#define M_CHROME 4
#define M_JELLY 5
#define M_SIL 6
#define M_RAINBOW 7
#define M_GLITCH 8
#define M_SAND 9
#define M_LAVA 10
#define M_PAPER 11
#define M_PIXEL 12
#define M_INK 13
#define M_CRYSTAL 14
#define M_WIRE 15
#define M_STARS 16
#define M_SHADOW 17
#define M_BRICK 18
#define M_GIFT 19

float3 hsv3(float h, float s, float v) {
    float3 k = saturate(abs(frac(h + float3(0, 2, 1) / 3.0) * 6.0 - 3.0) - 1.0);
    return pow(v * lerp(float3(1, 1, 1), k, s), 2.2);
}
// the reflected sky, blocky: for chrome / gold / ice
float3 dsEnv(float3 r) {
    float y = r.y;
    float3 sky = lerp(float3(0.55, 0.62, 0.80), float3(0.20, 0.32, 0.62), saturate(y));
    float3 gnd = float3(0.10, 0.09, 0.08);
    float3 c = y > 0.0 ? sky : gnd;
    c += float3(1.2, 1.1, 0.95) * pow(saturate(dot(r, SUN)), 64.0) * 3.0;      // the sun's hot spot
    c += float3(0.6, 0.6, 0.65) * exp(-abs(y) * 30.0);                          // the bright horizon band
    return c * lerp(1.0, 0.35, NIGHT);
}
float dsEdge(float3 q, float3 e, float3 nl) {   // distance (px) from this texel to the face's nearest edge
    float3 ad = e - abs(q);
    float3 m = float3(abs(nl.x) > 0.5 ? 99.0 : ad.x, abs(nl.y) > 0.5 ? 99.0 : ad.y, abs(nl.z) > 0.5 ? 99.0 : ad.z);
    return min(m.x, min(m.y, m.z));
}
float3 dsMat(int m, float3 alb, float3 nw, float3 rd, float3 q, float3 e, float3 nl, float n, int j, float hgt, float3 C, float seed) {
    float  lum = dot(alb, float3(0.3, 0.55, 0.15));
    float  ed = dsEdge(q, e, nl);
    float  fres = pow(1.0 - saturate(abs(dot(nw, rd))), 3.0);
    float3 tx = floor(q + e);
    float  n2 = hash12(tx.xy * 5.3 + tx.z * 2.1 + float(j) * 7.7 + 31.0);
    [branch] switch (m) {
    case M_HOLO: {   // translucent-looking blue light: scanlines crawl up it, brighter at the edges, a flicker
        float scan = step(0.5, frac(tx.y * 0.5 - TIME * 3.0));
        float fl = 0.85 + 0.15 * step(0.5, frac(TIME * 13.0 + seed));
        return C * (0.25 + 0.5 * lum + 0.45 * scan + (ed < 0.75 ? 0.9 : 0.0)) * fl * 1.4; }
    case M_ICE: {    // pale blue-white, glossy, white fracture lines, a cold inner glow
        float crack = step(0.86, hash12(float2(tx.x + tx.z * 3.0, floor(tx.y * 0.5)) + float(j) * 3.0));
        float3 b = dsLit(lerp(float3(0.55, 0.78, 0.95), float3(0.80, 0.92, 1.0), n * 0.5 + lum * 0.4), nw);
        return lerp(b, dsEnv(reflect(rd, nw)), 0.25 + 0.4 * fres) + float3(0.6, 0.85, 1.0) * (crack * 0.5 + 0.08); }
    case M_STONE: {  // cobbled grey with dark joints; the skin's light and dark survive faintly
        float3 b = lerp(float3(0.30, 0.30, 0.31), float3(0.50, 0.50, 0.50), n) * (0.75 + 0.5 * lum);
        float joint = step(0.82, hash12(floor(tx.xy * 0.5) + tx.z + float(j)));
        return dsLit(b * (1.0 - 0.45 * joint) + C * 0.0, nw); }
    case M_GOLD: {   // polished gold: the sky in it, a hot rim
        float3 g = float3(1.0, 0.68, 0.22);
        return dsLit(g * (0.35 + 0.3 * lum), nw) + g * dsEnv(reflect(rd, nw)) * (0.55 + 0.6 * fres) + g * (ed < 0.75 ? 0.25 : 0.0); }
    case M_CHROME: { // a mirror: the world in it, black edges, a faint tint
        return dsEnv(reflect(rd, nw)) * (0.85 + 0.25 * n) * C * (ed < 0.6 ? 0.25 : 1.0); }
    case M_JELLY: {  // candy jelly: saturated, a white highlight, glows from inside
        float hl = step(0.9, saturate(dot(reflect(rd, nw), SUN))) + step(e.y - 1.6, q.y) * step(abs(q.x), 0.9) * 0.6;
        return C * (0.35 + 0.35 * lum + 0.3 * n) * 1.2 + C * 0.25 + float3(1, 1, 1) * hl * 0.8 + C * fres * 0.6; }
    case M_SIL: {    // a dark silhouette with a coloured rim
        return float3(0.012, 0.012, 0.016) + C * (ed < 0.75 ? 1.2 : 0.0) + C * fres * 0.4; }
    case M_RAINBOW: { // cycling the rainbow, banded up the body
        return dsLit(hsv3(frac(TIME * 0.9 + hgt * 1.2 + float(j) * 0.07), 0.85, 0.95), nw) + hsv3(frac(TIME * 0.9 + hgt), 0.8, 1.0) * 0.35; }
    case M_GLITCH: { // RGB tear: rows slide, channels split, a few texels burn white
        float row = floor(tx.y * 0.5 + floor(TIME * 18.0) * 7.0);
        float sh = step(0.7, h1(row + seed)) * (h1(row * 3.1 + seed) - 0.5) * 2.0;
        float3 c = alb;
        c.r = alb.r * (1.0 + sh); c.b = alb.b * (1.0 - sh);
        float3 tint = sh > 0.0 ? float3(1.0, 0.1, 0.4) : float3(0.1, 0.9, 1.0);
        return dsLit(c, nw) + tint * abs(sh) * 0.9 + step(0.97, h1(row * 7.7 + tx.x + seed)) * 1.5; }
    case M_SAND: {   // tan grains
        return dsLit(lerp(float3(0.62, 0.48, 0.28), float3(0.84, 0.70, 0.46), n2) * (0.85 + 0.3 * lum), nw); }
    case M_LAVA: {   // black crust, glowing seams that throb
        float seam = step(0.72, hash12(float2(tx.x + tx.z * 4.0, floor(tx.y * 0.5)) + float(j) * 5.0));
        float3 crust = dsLit(float3(0.05, 0.035, 0.03) * (0.7 + 0.6 * n), nw);
        return crust + float3(1.0, 0.36, 0.05) * (seam * (0.9 + 0.4 * sin(TIME * 6.0 + n * 20.0)) + 0.12) * 1.6; }
    case M_PAPER: {  // white paper with a fold crease
        float fold = step(abs(q.x + q.y * 0.5), 0.6);
        return dsLit(lerp(float3(0.86, 0.85, 0.80), float3(0.95, 0.94, 0.90), n) * (1.0 - 0.18 * fold), nw); }
    case M_PIXEL: {  // 8-bit: four steps of colour and a black outline
        float3 p = floor(pow(alb, 1.0 / 2.2) * 4.0 + 0.5) / 4.0;
        return (ed < 0.75 ? float3(0, 0, 0) : pow(p, 2.2) * 1.15); }
    case M_INK: {    // glossy black ink
        return float3(0.006, 0.006, 0.01) + dsEnv(reflect(rd, nw)) * (0.12 + 0.5 * fres) + float3(1, 1, 1) * step(0.97, saturate(dot(reflect(rd, nw), SUN))) * 1.2; }
    case M_CRYSTAL: { // amethyst: faceted purple, light caught inside
        float facet = h1(dot(floor(q * 0.5), float3(1.3, 7.1, 3.7)) + float(j));
        return C * (0.25 + 0.6 * facet) * 1.1 + dsEnv(reflect(rd, nw)) * C * 0.6 * fres + C * (ed < 0.75 ? 0.6 : 0.0); }
    case M_WIRE: {   // synthwave wireframe: black faces, neon edges and a grid
        float grid = (frac((tx.y + 0.5) * 0.25) < 0.26 || frac((tx.x + tx.z + 0.5) * 0.25) < 0.26) ? 0.35 : 0.0;
        return float3(0.01, 0.0, 0.02) + C * ((ed < 0.75 ? 1.6 : 0.0) + grid); }
    case M_STARS: {  // a body of night sky: deep blue, stars that twinkle
        float st = step(0.93, n2) * (0.6 + 0.4 * sin(TIME * 5.0 + n * 30.0));
        return float3(0.01, 0.015, 0.05) + float3(0.9, 0.95, 1.2) * st * 1.8 + C * (ed < 0.75 ? 0.35 : 0.0); }
    case M_SHADOW: { return float3(0.004, 0.004, 0.006); }
    case M_BRICK: {  // terracotta bricks with mortar lines
        float2 b = float2(tx.x + tx.z + floor(tx.y * 0.5) * 2.0, tx.y);
        float mortar = (frac(b.x * 0.25) < 0.26 || frac(b.y * 0.5) < 0.51) ? 1.0 : 0.0;
        return dsLit(lerp(lerp(float3(0.42, 0.16, 0.10), float3(0.55, 0.22, 0.14), n), float3(0.62, 0.60, 0.55), mortar * 0.8), nw); }
    default: {       // M_GIFT: wrapping paper in the tint, a white ribbon cross
        float rib = (abs(q.x) < 1.0 || abs(q.z) < 1.0) ? 1.0 : 0.0;
        return dsLit(lerp(C * (0.8 + 0.2 * n), float3(0.95, 0.93, 0.88), rib), nw); }
    }
}
