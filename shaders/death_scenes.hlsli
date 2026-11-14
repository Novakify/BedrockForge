// ---- death scenes: the seven kill animations of LyricsFlow's death animations (by 374m, used with the author's
//      permission), rebuilt for Bedrock / Renderer Foundation by Novak: Chains of the Deep, Blackjack, Tentacles,
//      Lightning Ash, Giant Hand, Hungry Chest, Executioner - and 7-13 from Finishers, Ragdollified and Sable Ragdolls
//      (death_scenes_ext.hlsli). One per kill, dealt from a shuffle bag (SkyPass).
//      Everything is ray-traced boxes / capsules in the World FX pass, depth-tested against the world, drawn at the
//      world's own resolution (1/16 block texels, face-shaded like blocks; particles are little cubes, not soft blobs).
//      The victim's body wears their real skin when Bedrock Forge sent it (gSkins), else a mannequin in team colours.
//      Event: eA = feet block (world, min) + age, eB = team colour + kind (3 kill, 4 final), eC.x = seed,
//      eC.y = the camera's yaw when the kill came, eC.w = scene + 16 * (skin slot + 1).
Texture2D<float4> gSkins : register(t4);   // 64 x (64 * 16): one classic skin per slot, top to bottom
static const float DPX = 0.05859375;       // one skin pixel in blocks (a 32 px body is 1.875 blocks)

struct DHit { float t; float3 c; bool hit; };
struct DFx  { float3 team; float slot; float charK; float ash; float flash; float hurt; float clipY; float seed; float ghost; };

float3 dsLit(float3 alb, float3 nw) {
    float  ndl = saturate(dot(nw, SUN));
    float3 amb = lerp(float3(0.40, 0.37, 0.52), float3(0.22, 0.28, 0.55), NIGHT) * (0.72 + 0.28 * nw.y);
    float3 sunC = lerp(float3(1.05, 0.74, 0.52), float3(0.70, 0.80, 1.05), NIGHT) * LIGHTK;
    float3 world = lerp(lerp(float3(1, 1, 1), float3(0.86, 0.78, 0.94), DUSK), float3(0.40, 0.47, 0.76), NIGHT);
    return alb * (amb + sunC * ndl) * world;
}
float3x3 rotY(float a) { float c = cos(a), s = sin(a); return float3x3(c, 0, s, 0, 1, 0, -s, 0, c); }
float3x3 rotX(float a) { float c = cos(a), s = sin(a); return float3x3(1, 0, 0, 0, c, -s, 0, s, c); }
float3x3 rotZ(float a) { float c = cos(a), s = sin(a); return float3x3(c, -s, 0, s, c, 0, 0, 0, 1); }
float3x3 basisOf(float3 T, float spin) {   // a frame whose y axis runs along T, turned by spin about it
    float3 up = abs(T.y) < 0.95 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 b1 = normalize(cross(up, T)), b3 = cross(b1, T);
    float c = cos(spin), s = sin(spin);
    float3 u = b1 * c + b3 * s, w = -b1 * s + b3 * c;
    return float3x3(u.x, T.x, w.x, u.y, T.y, w.y, u.z, T.z, w.z);
}
float3 hex3(float r, float g, float b) { return pow(float3(r, g, b) / 255.0, 2.2); }
float  ease(float x) { x = saturate(x); return x * x * (3.0 - 2.0 * x); }
float  easeOut(float x) { x = saturate(x); return 1.0 - (1.0 - x) * (1.0 - x) * (1.0 - x); }
float  backOut(float x) { x = saturate(x) - 1.0; return 1.0 + x * x * (2.70158 * x + 1.70158); }
float3 bez(float3 a, float3 b, float3 c, float u) { return lerp(lerp(a, b, u), lerp(b, c, u), u); }
bool   nearRay(float3 rd, float3 C, float r) { float tc = dot(C, rd); return tc > -r && dot(C, C) - tc * tc < r * r; }

// a box with a pixel-art surface: local texels (16 per block), a darker 1-texel rim on every face, optional edge glow
void dsBoxR(inout DHit h, float3 rd, float3 p, float3 e, float3x3 R, float3 alb, float3 glow, float rimK) {
    float tN; float3 nl, lp;
    if (!hitBox(rd, p, e, R, tN, nl, lp) || tN >= h.t) return;
    float3 q = (lp * 0.5 + 0.5) * e * 2.0 * 16.0;                          // texels from the box corner
    float3 tx = floor(q);
    float  n = hash12(tx.xy * 1.3 + tx.z * 7.7 + dot(e, float3(31.0, 17.0, 5.0)));
    float3 ad = min(q, e * 32.0 - q);                                      // texels to each pair of faces
    float  m1 = min(ad.x, min(ad.y, ad.z)), m3 = max(ad.x, max(ad.y, ad.z));
    float  ed = (ad.x + ad.y + ad.z) - m1 - m3;                            // to this face's nearest edge
    float  rim = ed < 1.0 ? rimK : 1.0;
    h.t = tN; h.c = dsLit(alb * (0.90 + 0.18 * n) * rim, mul(R, nl)) + glow * exp(-ed * 1.6); h.hit = true;
}
void dsBox(inout DHit h, float3 rd, float3 p, float3 e, float3x3 R, float3 alb, float3 glow) { dsBoxR(h, rd, p, e, R, alb, glow, 0.72); }
void dsCap(inout DHit h, float3 rd, float3 a, float3 b, float r, float3 alb, float3 rim, float emis) {
    float t = figCap(float3(0, 0, 0), rd, a, b, r);
    if (t <= 0.05 || t >= h.t) return;
    float3 nw = figCapN(rd * t, a, b, r);
    float  fr = pow(1.0 - saturate(dot(-rd, nw)), 2.5);
    h.t = t; h.c = dsLit(alb, nw) + rim * fr + alb * emis; h.hit = true;
}
// a burst of n little cubes thrown from C, u seconds ago: speed v0, lift up, gravity g (negative: they rise), life L
// (they shrink over it), size e0, landing on floorY (camera-relative world y; -1e5: none)
void dsBurst(inout DHit h, float3 rd, float3 C, float u, int n, float seed, float v0, float up, float g, float L, float e0,
             float3 colA, float3 colB, float floorY, float3 emis) {
    if (u < 0.0 || u > L) return;
    if (!nearRay(rd, C, (v0 * 1.2 + abs(up)) * u + abs(g) * 0.5 * u * u + e0 * 3.0 + 0.3)) return;
    [loop] for (int k = 0; k < n; k++) {
        float  sk = seed + float(k) * 2.39;
        float3 dir = normalize(float3(h1(sk), h1(sk + 1.1) * 0.9 + 0.05, h1(sk + 2.2)) - float3(0.5, 0.0, 0.5));
        float3 v = dir * v0 * (0.45 + 0.75 * h1(sk + 3.3)) + float3(0, up, 0);
        float3 p = C + v * u + float3(0, -0.5 * g * u * u, 0);
        float  life = 1.0 - (u / L);
        float  e = e0 * (0.6 + 0.6 * h1(sk + 4.4)) * saturate(life * 2.5);
        float  ang = u * (5.0 + 6.0 * h1(sk + 5.5));
        if (p.y < floorY + e) { p.y = floorY + e; ang = 0.0; }
        if (e < 0.004) continue;
        float tN; float3 nl, lp;
        float3x3 R = rotAxis(normalize(dir + float3(0.01, 0.3, 0.02)), ang);
        if (!hitBox(rd, p, float3(e, e, e), R, tN, nl, lp) || tN >= h.t) continue;
        float3 alb = lerp(colA, colB, h1(sk + 6.6));
        h.t = tN; h.c = dsLit(alb, mul(R, nl)) + emis * life; h.hit = true;
    }
}
float dsGlowSeg(float3 rd, float3 a, float3 b, float w, float tMax) {     // light along a segment (additive), depth-tested
    float tr; float d = figSegDist(float3(0, 0, 0), rd, a, b, tr);
    return tr < tMax + 0.05 ? exp(-(d * d) / (w * w)) : 0.0;
}

// ------------------------------------------------------------------ the victim
// parts: 0 head, 1 body, 2 right arm, 3 left arm, 4 right leg, 5 left leg (px; the body faces +z, feet at y 0)
static const float3 kPC[6] = { float3(0, 28, 0), float3(0, 18, 0), float3(-6, 18, 0), float3(6, 18, 0), float3(-2, 6, 0), float3(2, 6, 0) };
static const float3 kPE[6] = { float3(4, 4, 4), float3(4, 6, 2), float3(2, 6, 2), float3(2, 6, 2), float3(2, 6, 2), float3(2, 6, 2) };
static const float2 kPUV[6] = { float2(0, 0), float2(16, 16), float2(40, 16), float2(32, 48), float2(0, 16), float2(16, 48) };
static const float3 kPP[6] = { float3(0, 24, 0), float3(0, 12, 0), float3(-5, 22, 0), float3(5, 22, 0), float3(-2, 12, 0), float3(2, 12, 0) };

float3 dsMannequin(int j, float3 nl, float3 q, float3 e, float3 team) {   // no skin: a figure in the team's colours
    float  top = e.y - q.y;                                                 // px from the part's top
    float3 tx = floor(q + e);
    float  n = hash12(tx.xy * 1.9 + tx.z * 5.3 + float(j) * 13.0);
    float3 skin = float3(0.55, 0.33, 0.22), dark = team * 0.10 + 0.012;
    float3 c;
    if (j == 0) {
        bool hair = nl.y > 0.5 || top < 2.0 || (nl.z < 0.5 && top < 6.0 && nl.y > -0.5);
        c = hair ? dark : skin;
        if (nl.z > 0.5 && top >= 4.0 && top < 5.0 && abs(abs(q.x) - 2.0) < 1.0) c = abs(q.x) > 2.0 ? float3(0.85, 0.85, 0.85) : float3(0.03, 0.03, 0.05);
        if (nl.z > 0.5 && top >= 6.0 && top < 7.0 && abs(q.x) < 1.0) c = skin * 0.6;
    } else if (j == 1) {
        c = team * 0.62;
        if (top < 1.0 || abs(top - 6.0) < 0.5) c = team * 0.9 + 0.05;       // collar and a band
        if (top > 11.0) c = dark;                                           // belt
    } else if (j < 4) c = top > 9.0 ? skin : (top > 8.0 ? team * 0.9 + 0.05 : team * 0.55);
    else c = top > 9.0 ? float3(0.03, 0.03, 0.035) : dark * 1.6 + team * 0.06;
    return c * (0.90 + 0.20 * n);
}
float3 dsSkin(int j, float3 nl, float3 q, float3 e, DFx fx) {
    if (fx.slot < -0.5) return dsMannequin(j, nl, q, e, fx.team);
    float w = e.x * 2.0, hh = e.y * 2.0, d = e.z * 2.0, tu, tv;
    if (nl.y > 0.5)       { tu = d + (q.x + e.x);           tv = (q.z + e.z); }
    else if (nl.y < -0.5) { tu = d + w + (q.x + e.x);       tv = (q.z + e.z); }
    else if (nl.z > 0.5)  { tu = d + (q.x + e.x);           tv = d + (e.y - q.y); }
    else if (nl.z < -0.5) { tu = 2.0 * d + w + (e.x - q.x); tv = d + (e.y - q.y); }
    else if (nl.x < -0.5) { tu = (q.z + e.z);               tv = d + (e.y - q.y); }
    else                  { tu = d + w + (e.z - q.z);       tv = d + (e.y - q.y); }
    int2 t = int2(clamp(floor(float2(tu, tv)), float2(0, 0), float2(2.0 * d + 2.0 * w - 1.0, d + hh - 1.0))) + int2(kPUV[j]);
    int  row = 64 * int(fx.slot + 0.5);
    float4 c = gSkins.Load(int3(t.x, t.y + row, 0));
    if (j == 0) { float4 hat = gSkins.Load(int3(t.x + 32, t.y + row, 0)); c = hat.a > 0.5 ? hat : c; }
    return pow(max(c.rgb, 0.0), 2.2);
}
// one part: P = its centre (camera-relative), R = its axes, s = scale; only local y in cLo..cHi (px from the centre)
// is kept - a slice shows a cut face
void dsPart(inout DHit h, float3 rd, int j, float3 P, float3x3 R, float s, float cLo, float cHi, DFx fx) {
    float3 e = kPE[j];
    float  yl = max(-e.y, cLo), yh = min(e.y, cHi);
    if (yh - yl < 0.01 || s < 0.02) return;
    float  cy = (yl + yh) * 0.5, k = DPX * s;
    float3 eh = float3(e.x, (yh - yl) * 0.5, e.z);
    float  tN; float3 nl, lp;
    if (!hitBox(rd, P + mul(R, float3(0, cy, 0)) * k, eh * k, R, tN, nl, lp) || tN >= h.t) return;
    if (rd.y * tN < fx.clipY) return;
    float3 q = clamp(lp * eh + float3(0, cy, 0), -e + 0.001, e - 0.001);
    float3 tx = floor(q + e);
    float  n = hash12(tx.xy * 1.7 + tx.z * 9.1 + float(j) * 3.3);
    float  hgt = (kPC[j].y + q.y) / 32.0;
    if (fx.ash > 0.0 && hgt + n * 0.35 > 1.35 - fx.ash * 1.4) return;        // crumbling away, from the top down
    if (fx.ghost > 0.0) {   // the Finishers phantom: ink, a lit one-texel rim, two white eyes, a faint glitter inside
        float3 ad = e - abs(q);
        float3 m = float3(abs(nl.x) > 0.5 ? 99.0 : ad.x, abs(nl.y) > 0.5 ? 99.0 : ad.y, abs(nl.z) > 0.5 ? 99.0 : ad.z);
        float  ed = min(m.x, min(m.y, m.z));
        float  top = e.y - q.y;
        float  eyes = (j == 0 && nl.z > 0.5 && top >= 4.0 && top < 5.0 && abs(q.x) >= 1.0 && abs(q.x) < 3.0) ? 1.0 : 0.0;
        float  spark = step(0.985, hash12(tx.xy * 3.1 + tx.z * 1.7 + floor(TIME * 7.0) + float(j) * 5.0));
        float  front = fx.ash > 0.0 ? saturate(1.0 - abs((1.35 - fx.ash * 1.4) - (hgt + n * 0.35)) / 0.08) : 0.0;   // the edge it forms / goes at
        float3 rimC = float3(0.83, 0.76, 1.0);
        float3 cg = dsLit(float3(0.012, 0.011, 0.016) * (0.8 + 0.4 * n), mul(R, nl)) + rimC * ((ed < 0.75 ? 0.55 : 0.0) + eyes * 3.2 + spark * 0.5 + front * 2.0);
        h.t = tN; h.c = lerp(cg, float3(2.6, 2.6, 2.6), fx.flash); h.hit = true;
        return;
    }
    bool   cut = (nl.y > 0.5 && yh < e.y - 0.01) || (nl.y < -0.5 && yl > -e.y + 0.01);
    float3 alb = cut ? lerp(hex3(110, 2, 2), hex3(181, 18, 18), n) * (min(e.x - abs(q.x), e.z - abs(q.z)) < 1.0 ? 0.55 : 1.0) : dsSkin(j, nl, q, e, fx);
    float3 nw = mul(R, nl);
    float3 c = dsLit(alb, nw);
    if (fx.charK > 0.0) {
        float3 cq = floor(q + e);
        float crack = step(0.90, hash12(float2(cq.x + cq.z * 5.0, floor(cq.y * 0.34)) + float(j))) + step(0.93, hash12(float2(floor((cq.x + cq.z) * 0.34), cq.y * 3.0) + float(j) + 9.0));   // thin ember veins
        float3 ember = float3(1.0, 0.33, 0.05) * saturate(crack) * (0.55 + 0.45 * sin(TIME * 7.0 + n * 40.0)) * 1.6;
        c = lerp(c, dsLit(float3(0.016, 0.014, 0.014) * (0.7 + 0.6 * n), nw) + ember * saturate(1.0 - fx.ash * 1.2), fx.charK);
    }
    c = lerp(c, c * float3(1.5, 0.30, 0.26) + float3(0.20, 0.0, 0.0), fx.hurt * 0.65);
    c = lerp(c, float3(2.6, 2.6, 2.6), fx.flash);
    h.t = tN; h.c = c; h.hit = true;
}
// the body at rest (for slices): O = feet centre, R = body axes; keeps body heights yLo..yHi (px)
void dsBody(inout DHit h, float3 rd, float3 O, float3x3 R, float s, float yLo, float yHi, DFx fx) {
    if (!nearRay(rd, O + mul(R, float3(0, 16.0 * DPX * s, 0)), 1.25 * s + 0.05)) return;
    [loop] for (int j = 0; j < 6; j++)
        dsPart(h, rd, j, O + mul(R, kPC[j] * DPX * s), R, s, yLo - kPC[j].y, yHi - kPC[j].y, fx);
}
// the body posed: each limb turns about its joint - a = (pitch about x, roll about z). parts: bit j set = drawn
void dsPosed(inout DHit h, float3 rd, float3 O, float3x3 R, float s, float2 hd, float2 aR, float2 aL, float2 lR, float2 lL, int parts, DFx fx) {
    if (!nearRay(rd, O + mul(R, float3(0, 16.0 * DPX * s, 0)), 1.5 * s + 0.05)) return;
    [loop] for (int j = 0; j < 6; j++) {
        if (((parts >> j) & 1) == 0) continue;
        float2 a = j == 0 ? hd : (j == 2 ? aR : (j == 3 ? aL : (j == 4 ? lR : (j == 5 ? lL : float2(0, 0)))));
        float3x3 Rj = mul(R, mul(rotZ(a.y), rotX(a.x)));
        float3 P = O + mul(R, kPP[j] * DPX * s) + mul(Rj, (kPC[j] - kPP[j]) * DPX * s);
        dsPart(h, rd, j, P, Rj, s, -99.0, 99.0, fx);
    }
}

// ------------------------------------------------------------------ props
float sdSuit(float2 p, int s) {   // p in card space (x right, y up), about +-1: < 0 inside. 0 spade 1 heart 2 club 3 diamond
    if (s == 3) return abs(p.x) * 1.25 + abs(p.y) - 0.95;
    if (s == 2) {
        float d = min(length(p - float2(0.0, 0.42)), min(length(p - float2(-0.42, -0.08)), length(p - float2(0.42, -0.08)))) - 0.40;
        return min(d, max(abs(p.x) - 0.12 - max(-p.y - 0.5, 0.0) * 0.5, abs(p.y + 0.55) - 0.45));
    }
    float2 q = float2(abs(p.x), s == 0 ? -p.y : p.y);                        // a heart (lobes up); the spade is one upside down
    float d = min(length(q - float2(0.36, 0.30)) - 0.42, max(q.x * 0.95 + (-q.y) * 0.80 - 0.62, q.y - 0.45));
    if (s == 0) d = min(d, max(abs(p.x) - 0.10 - max(-p.y - 0.55, 0.0) * 0.6, abs(p.y + 0.72) - 0.30));
    return d;
}
// a playing card (half size 0.25 x 0.35): pixel-art face (20 x 28 texels) or lattice back
void dsCard(inout DHit h, float3 rd, float3 p, float3x3 R, float sc, int idx, float3 glow) {
    float tN; float3 nl, lp;
    if (sc < 0.02 || !hitBox(rd, p, float3(0.25, 0.35, 0.008) * sc, R, tN, nl, lp) || tN >= h.t) return;
    float2 tx = floor((lp.xy * 0.5 + 0.5) * float2(20.0, 28.0));              // texel
    float2 uv = (tx + 0.5) / float2(20.0, 28.0) * 2.0 - 1.0;                  // its centre, -1..1
    bool   border = tx.x < 1.0 || tx.x > 18.0 || tx.y < 1.0 || tx.y > 26.0;
    int    suit = idx & 3;
    bool   red = (suit & 1) == 1;
    float3 ink = red ? hex3(196, 28, 36) : hex3(24, 22, 30);
    float3 alb;
    if (abs(nl.z) < 0.5) alb = hex3(214, 208, 196);                           // the edge
    else if (nl.z > 0.0) {
        alb = hex3(244, 240, 230);
        float2 pc = float2(uv.x * 20.0 / 28.0, uv.y) * 2.1;                   // the suit, large, in the middle
        if (sdSuit(pc, suit) < 0.0) alb = ink;
        float2 pk = (float2(abs(uv.x), abs(uv.y)) - float2(0.66, 0.74)) * float2(20.0 / 28.0, 1.0) * 7.5;   // a pip in each corner
        if (sdSuit(float2(pk.x, uv.y > 0.0 ? pk.y : -pk.y), suit) < 0.0) alb = ink;
        if (border) alb = hex3(226, 220, 206);
    } else {
        bool lat = fmod(tx.x + tx.y, 4.0) < 1.0 || fmod(tx.x - tx.y + 40.0, 4.0) < 1.0;
        alb = lat ? hex3(236, 226, 210) : hex3(150, 22, 34);
        if (tx.x < 2.0 || tx.x > 17.0 || tx.y < 2.0 || tx.y > 25.0) alb = hex3(150, 22, 34);
        if (border) alb = hex3(236, 226, 210);
    }
    h.t = tN; h.c = dsLit(alb, mul(R, nl)) + glow * (border ? 1.0 : 0.15); h.hit = true;
}
// a tentacle through 4 control points (a Bezier), 12 capsules, tapering; suckers on the side facing axisP
void dsTentacle(inout DHit h, float3 rd, float3 p0, float3 p1, float3 p2, float3 p3, float r0, float grow, float3 axisP, float3 rim, float clipY) {
    float3 mid = (p0 + p1 + p2 + p3) * 0.25;
    if (!nearRay(rd, mid, max(max(length(p0 - mid), length(p3 - mid)), max(length(p1 - mid), length(p2 - mid))) + r0 + 0.1)) return;
    float3 prev = p0;
    [loop] for (int l = 1; l <= 12; l++) {
        float  s0 = float(l) / 12.0 * grow, u = 1.0 - s0;
        float3 P = p0 * u * u * u + p1 * 3.0 * u * u * s0 + p2 * 3.0 * u * s0 * s0 + p3 * s0 * s0 * s0;
        float  r = r0 * lerp(1.0, 0.16, float(l) / 12.0);
        float  t = figCap(float3(0, 0, 0), rd, prev, P, r);
        if (t > 0.05 && t < h.t && rd.y * t > clipY) {
            float3 X = rd * t, nw = figCapN(X, prev, P, r);
            float3 toAx = normalize(float3(axisP.x - X.x, 0.0, axisP.z - X.z));
            float  under = dot(nw, toAx);
            float  along = (float(l) - 1.0 + saturate(dot(X - prev, P - prev) / max(dot(P - prev, P - prev), 1e-5))) / 12.0;
            float3 top = hex3(38, 14, 52) * (0.8 + 0.4 * step(0.5, frac(along * 14.0)));              // dark rings along it
            float3 belly = hex3(168, 92, 132);
            float  suck = step(0.55, under) * step(frac(along * 16.0), 0.45);
            float3 alb = lerp(top, belly, smoothstep(0.15, 0.45, under));
            alb = lerp(alb, hex3(228, 170, 190), suck);
            float  fr = pow(1.0 - saturate(dot(-rd, nw)), 3.0);
            float  spec = pow(saturate(dot(reflect(rd, nw), SUN)), 30.0) * 0.5 * LIGHTK;
            h.t = t; h.c = dsLit(alb, nw) + rim * fr * 0.5 + spec; h.hit = true;
        }
        prev = P;
    }
}
// the giant hand, palm down: C = palm centre, RY = its yaw, curl 0 (open, fingers splayed) .. 1 (closed down round a body)
void dsHand(inout DHit h, float3 rd, float3 C, float3x3 RY, float curl) {
    float3 skin = hex3(227, 170, 130), skinD = hex3(201, 142, 106), nail = hex3(244, 220, 207), vein = hex3(184, 144, 127);
    float3 sleeve = hex3(43, 40, 51), cuff = hex3(28, 26, 34);
    // the sleeve runs up into the sky: only its column and the hand itself can be hit
    float2 ax = C.xz - rd.xz * (dot(C.xz, rd.xz) / max(dot(rd.xz, rd.xz), 1e-5));
    if (dot(ax, ax) > 2.6 * 2.6) return;
    float tN; float3 nl, lp;
    if (hitBox(rd, C, float3(0.95, 0.22, 0.80), RY, tN, nl, lp) && tN < h.t) {      // the palm; veins and knuckles drawn on its back
        float3 tx = floor((lp * 0.5 + 0.5) * float3(30.0, 7.0, 26.0));
        float  n = hash12(tx.xy * 1.3 + tx.z * 7.7);
        float3 alb = skin;
        if (nl.y > 0.5) {
            float fingerCol = fmod(tx.x + 1.0, 7.5);
            if (tx.z > 20.0 && tx.z < 24.0 && fingerCol > 2.0 && fingerCol < 6.0) alb = hex3(217, 152, 120);                 // knuckles
            if (tx.z > 4.0 && tx.z < 19.0 && (abs(tx.x - 9.0 - floor(tx.z * 0.25)) < 0.5 || abs(tx.x - 21.0 + floor(tx.z * 0.2)) < 0.5)) alb = vein;
        }
        if (nl.y < -0.5) alb = hex3(238, 190, 156);
        h.t = tN; h.c = dsLit(alb * (0.92 + 0.14 * n), mul(RY, nl)); h.hit = true;
    }
    dsBoxR(h, rd, C + mul(RY, float3(0, 0.40, -0.10)), float3(0.70, 0.20, 0.62), RY, skinD, 0.0, 0.9);                          // the wrist
    dsBox(h, rd, C + mul(RY, float3(0, 0.80, -0.10)), float3(0.86, 0.22, 0.78), RY, cuff, 0.0);                                 // the cuff
    dsBoxR(h, rd, C + mul(RY, float3(0, 26.0, -0.10)), float3(0.78, 25.0, 0.70), RY, sleeve, 0.0, 0.85);                        // the sleeve
    [loop] for (int k = 0; k < 5; k++) {
        bool   thumb = k == 4;
        float  fxp = thumb ? -0.55 : (-0.72 + 0.48 * float(k));
        float  len = thumb ? 0.50 : (k == 0 ? 0.50 : (k == 3 ? 0.46 : 0.58));
        float  th = thumb ? 0.21 : 0.19;
        float  sgn = thumb ? -1.0 : 1.0;                                             // the thumb closes from the other side
        float  a1 = lerp(-1.05, 0.30, curl) * sgn, a2 = a1 + lerp(0.40, 0.28, curl) * sgn, a3 = a2 + lerp(0.35, 0.40, curl) * sgn;
        float3 root = float3(fxp, -0.06, thumb ? -0.70 : 0.74);
        float3x3 R1 = rotX(a1), R2 = rotX(a2), R3 = rotX(a3);
        float3 j2 = root + mul(R1, float3(0, -len, 0)), j3 = j2 + mul(R2, float3(0, -len * 0.82, 0));
        dsBoxR(h, rd, C + mul(RY, root + mul(R1, float3(0, -len * 0.5, 0))), float3(th, len * 0.56, th), mul(RY, R1), skin, 0.0, 0.88);
        dsBoxR(h, rd, C + mul(RY, j2 + mul(R2, float3(0, -len * 0.41, 0))), float3(th * 0.93, len * 0.47, th * 0.93), mul(RY, R2), skin, 0.0, 0.88);
        if (!thumb) {
            dsBoxR(h, rd, C + mul(RY, j3 + mul(R3, float3(0, -len * 0.33, 0))), float3(th * 0.86, len * 0.38, th * 0.86), mul(RY, R3), skinD, 0.0, 0.88);
            dsBoxR(h, rd, C + mul(RY, j3 + mul(R3, float3(0, -len * 0.50, th * 0.84))), float3(th * 0.66, len * 0.17, 0.035), mul(RY, R3), nail, 0.0, 0.95);
        } else dsBoxR(h, rd, C + mul(RY, j2 + mul(R2, float3(0, -len * 0.56, -th * 0.90))), float3(th * 0.68, len * 0.20, 0.035), mul(RY, R2), nail, 0.0, 0.95);
    }
}
// the hungry chest: C = centre of its base on the ground, open = lid angle, sq = squash (1 = none)
void dsChest(inout DHit h, float3 rd, float3 C, float3x3 RY, float open, float sq, float eyes, float3 team) {
    if (!nearRay(rd, C + float3(0, 0.5, 0), 1.35)) return;
    float3 wood = hex3(162, 104, 44), woodD = hex3(104, 62, 24), gold = hex3(236, 196, 72);
    float3 S = float3(1.0 / sqrt(sq), sq, 1.0 / sqrt(sq));
    float  bw = 0.4375, bh = 0.3125, lh = 0.14;
    float  tN; float3 nl, lp;
    // the base: planks, a dark frame, the inside dark red when seen from above
    float3 pb = C + float3(0, bh * S.y, 0);
    if (hitBox(rd, pb, float3(bw * S.x, bh * S.y, bw * S.z), RY, tN, nl, lp) && tN < h.t) {
        float3 tx = floor((lp * 0.5 + 0.5) * float3(14.0, 10.0, 14.0));
        float  n = hash12(tx.xy * 1.3 + tx.z * 7.7);
        float2 f = abs(nl.x) > 0.5 ? tx.zy : (abs(nl.y) > 0.5 ? tx.xz : tx.xy);
        float2 fm = abs(nl.y) > 0.5 ? float2(13.0, 13.0) : float2(13.0, 9.0);
        bool   frame = f.x < 1.0 || f.x > fm.x - 1.0 || f.y < 1.0 || f.y > fm.y - 1.0;
        float3 alb = frame ? woodD : wood * (fmod(f.y, 4.0) < 1.0 ? 0.80 : 1.0);
        if (nl.y > 0.5 && !frame) alb = hex3(70, 8, 14) * (0.5 + 0.5 * n);    // its throat
        h.t = tN; h.c = dsLit(alb * (0.9 + 0.2 * n), mul(RY, nl)); h.hit = true;
    }
    float3 hinge = C + mul(RY, float3(0, 2.0 * bh * S.y, -bw * S.z));
    float3x3 RL = mul(RY, rotX(-open));
    float3 pl = hinge + mul(RL, float3(0, lh, bw * S.z));
    if (hitBox(rd, pl, float3(bw * S.x, lh, bw * S.z), RL, tN, nl, lp) && tN < h.t) {
        float3 tx = floor((lp * 0.5 + 0.5) * float3(14.0, 5.0, 14.0));
        float  n = hash12(tx.xy * 2.1 + tx.z * 3.7);
        float2 f = abs(nl.x) > 0.5 ? tx.zy : (abs(nl.y) > 0.5 ? tx.xz : tx.xy);
        float2 fm = abs(nl.y) > 0.5 ? float2(13.0, 13.0) : float2(13.0, 4.0);
        bool   frame = f.x < 1.0 || f.x > fm.x - 1.0 || f.y < 1.0 || f.y > fm.y - 1.0;
        float3 alb = frame ? woodD : wood * (abs(nl.y) > 0.5 ? (fmod(f.x, 4.0) < 1.0 ? 0.80 : 1.0) : 1.0);
        if (nl.y < -0.5 && !frame) alb = hex3(96, 14, 22) * (0.5 + 0.5 * n);  // the roof of its mouth
        h.t = tN; h.c = dsLit(alb * (0.9 + 0.2 * n), mul(RL, nl)); h.hit = true;
    }
    dsBox(h, rd, hinge + mul(RL, float3(0, 0.03, 2.0 * bw * S.z + 0.03)), float3(0.065, 0.10, 0.03), RL, gold, 0.0);            // the latch
    if (open > 0.08) {
        [loop] for (int k = 0; k < 6; k++) {   // teeth: a row on the base's front lip and one under the lid
            float x = (-0.3125 + 0.125 * float(k)) * S.x;
            dsBox(h, rd, C + mul(RY, float3(x, 2.0 * bh * S.y + 0.045, (bw - 0.06) * S.z)), float3(0.035, 0.05, 0.03), RY, hex3(244, 240, 226), 0.0);
            dsBox(h, rd, hinge + mul(RL, float3(x + 0.0625 * S.x, -0.045, (2.0 * bw - 0.06) * S.z)), float3(0.035, 0.05, 0.03), RL, hex3(244, 240, 226), 0.0);
        }
    }
    if (eyes > 0.01 && open > 0.02) {   // two eyes in the dark of the gap
        [unroll] for (int e2 = 0; e2 < 2; e2++) {
            float3 pe = C + mul(RY, float3(e2 == 0 ? -0.16 : 0.16, 2.0 * bh * S.y + 0.045, bw * S.z - 0.10));
            float tE; float3 n2, l2;
            if (hitBox(rd, pe, float3(0.045, 0.03, 0.02), RY, tE, n2, l2) && tE < h.t) { h.t = tE; h.c = lerp(team, float3(1, 1, 1), 0.5) * 3.0 * eyes; h.hit = true; }
        }
    }
}
// the executioner: a hooded figure 2.7 blocks tall. O = feet, RY = facing (+z), arm = how far the arms are raised
// (radians, 0 = down), ash = dissolving. The axe is drawn by the scene (it swings on its own)
void dsExecutioner(inout DHit h, float3 rd, float3 O, float3x3 RY, float arm, float ash, float3 team) {
    if (!nearRay(rd, O + float3(0, 1.7, 0), 2.5)) return;
    float3 robe = hex3(22, 20, 28), robeL = hex3(36, 32, 44), rope = hex3(120, 92, 48);
    [loop] for (int k = 0; k < 8; k++) {
        float3 c, e; float3 col = robe; float3x3 R = RY;
        if (k == 0)      { c = float3(0, 0.45, 0);     e = float3(0.42, 0.45, 0.34); }                     // the skirt of the robe
        else if (k == 1) { c = float3(0, 1.25, 0);     e = float3(0.36, 0.38, 0.27); col = robeL * 0.8; }  // the chest
        else if (k == 2) { c = float3(0, 0.88, 0.0);   e = float3(0.375, 0.04, 0.285); col = rope; }       // a rope belt
        else if (k == 3) { c = float3(0, 2.02, 0.02);  e = float3(0.30, 0.34, 0.31); col = robe * 0.9; }   // the hood
        else if (k == 4) { c = float3(0, 1.98, 0.30);  e = float3(0.20, 0.22, 0.03); col = float3(0.002, 0.002, 0.003); }   // the dark of the face
        else if (k == 5) { c = float3(0, 2.42, -0.10); e = float3(0.16, 0.12, 0.20); col = robe * 0.9; }   // the hood's peak
        else {                                                                                             // the arms
            float3x3 Ra = rotX(-arm);
            c = float3(k == 6 ? -0.50 : 0.50, 1.55, 0) + mul(Ra, float3(0, -0.30, 0)); e = float3(0.13, 0.32, 0.14); R = mul(RY, Ra); col = robeL;
        }
        c *= 1.22; e *= 1.22;                                                                               // he stands over them
        float tN; float3 nl, lp;
        if (!hitBox(rd, O + mul(RY, c), e, R, tN, nl, lp) || tN >= h.t) continue;
        float3 tx = floor((lp * 0.5 + 0.5) * e * 32.0);
        float  n = hash12(tx.xy * 1.3 + tx.z * 7.7 + float(k) * 11.0);
        float  hgt = (c.y + lp.y * e.y) / 3.2;
        if (ash > 0.0 && hgt + n * 0.35 > 1.35 - ash * 1.4) continue;
        float  fold = (k <= 1 && abs(nl.y) < 0.5 && fmod(tx.x + (abs(nl.x) > 0.5 ? tx.z : 0.0), 5.0) < 1.0) ? 0.72 : 1.0;   // folds in the cloth
        h.t = tN; h.c = dsLit(col * (0.85 + 0.3 * n) * fold, mul(R, nl)); h.hit = true;
        if (k == 4 && abs(lp.y - 0.15) < 0.14 && abs(abs(lp.x) - 0.42) < 0.16) h.c = lerp(team, float3(1, 1, 1), 0.3) * 2.2;   // its eyes
    }
}

#include "death_scenes_ext.hlsli"

// ------------------------------------------------------------------ the scenes
float3 deathScenes(float3 o, float3 rd, inout float sceneT) {
    int ne = int(m0.y);
    [loop] for (int i = 0; i < ne; i++) {
        float kind = eB[i].w, t = eA[i].w;
        if (kind < 2.5 || kind > 4.5 || t > 4.8) continue;
        float3 feet = eA[i].xyz + float3(0.5, 0.0, 0.5) - CAM;
        float  code = eC[i].w + 0.5;
        int    sc = int(fmod(code, 16.0));
        // only pixels near the scene: a 4.5-block ball, or (hand, lightning) the column above it
        float2 ax = feet.xz - rd.xz * (dot(feet.xz, rd.xz) / max(dot(rd.xz, rd.xz), 1e-5));
        bool   col = (sc == 3 || sc == 4) && dot(ax, ax) < 3.5 * 3.5 && dot(feet.xz, rd.xz) > -3.5;
        float  cr = sc == 12 ? 7.0 : ((sc == 7 || sc == 11) ? 5.6 : 4.5);   // the ragdolls and the long finishers reach further
        if (!nearRay(rd, feet + float3(0, 1.2, 0), cr) && !col) continue;

        DFx fx; fx.team = pow(max(eB[i].rgb, 0.0), 2.2);
        fx.team = fx.team / max(max(fx.team.r, max(fx.team.g, fx.team.b)), 0.05);
        fx.slot = floor(code / 16.0) - 1.0; fx.charK = 0.0; fx.ash = 0.0; fx.flash = 0.0; fx.hurt = 0.0; fx.clipY = -1e5; fx.seed = eC[i].x; fx.ghost = 0.0;
        float3 team = fx.team, glow = team * (1.1 + 0.9 * darkK());
        float  seed = eC[i].x;
        float  yaw = h1(seed * 0.013) * 6.2832;
        float3x3 RY = rotY(yaw);
        float3 fwd = mul(RY, float3(0, 0, 1)), side = mul(RY, float3(1, 0, 0));
        DHit h; h.t = sceneT; h.c = 0.0; h.hit = false;
        float3 add = 0.0;                                                   // light added on top (linear)
        // the ground under the scene (this pixel, if it is the floor the victim stood on): decals are drawn into it
        float3 S = rd * sceneT;
        bool   onG = abs(S.y - feet.y) < 0.10 && sceneT < 1e7;
        float2 g = floor((S.xz - feet.xz) * 16.0) / 16.0 + 1.0 / 32.0;     // in world texels
        float  gr = length(g);
        float3 dec = o; float decK = 0.0;                                   // decal colour (display) and cover

        if (sc == 0) {
            // ============ CHAINS OF THE DEEP: a black pool opens, four barbed chains burst out and spear the limbs,
            // hoist the body spread-eagled, hold it a beat - then drag it under
            float rp = 1.45 * easeOut(t / 0.3) * (1.0 - ease((t - 3.7) / 0.6));
            float hoist = backOut((t - 0.75) / 0.55);
            float pull = pow(saturate((t - 1.95) / 1.15), 2.2) * 4.6;
            float lift = 1.05 * hoist - pull;
            if (onG && gr < rp + 0.2) {
                float ring = 0.5 + 0.5 * sin((gr - t * 1.1) * 20.0);
                float3 pool = float3(0.012, 0.014, 0.022) + team * 0.05 * ring * smoothstep(rp, rp * 0.3, gr);
                if (gr < rp) { dec = pow(pool, 1.0 / 2.2); decK = 1.0; }
                add += glow * exp(-pow((gr - rp) / 0.05, 2.0)) * 0.55;
            }
            fx.clipY = feet.y + 0.01;
            float spread = hoist * (1.0 - saturate((t - 2.6) / 0.5) * 0.6);
            [unroll] for (int k0 = 0; k0 < 4; k0++) fx.hurt = max(fx.hurt, exp(-max(t - (0.50 + 0.06 * float(k0)), 0.0) * 9.0) * step(0.50 + 0.06 * float(k0), t));
            float sway = sin(t * 5.0) * 0.05 * hoist;
            float3 O = feet + float3(0, lift, 0);
            float3x3 Rb = mul(RY, rotZ(sway));
            float2 aR = float2(0.0, -1.25 * spread), aL = float2(0.0, 1.25 * spread), lR = float2(0.0, -0.38 * spread), lL = float2(0.0, 0.38 * spread);
            dsPosed(h, rd, O, Rb, 1.0, float2(-0.45 * spread + 0.5 * saturate((t - 1.95) * 3.0), 0.0), aR, aL, lR, lL, 63, fx);
            [loop] for (int k = 0; k < 4; k++) {
                float  a = yaw + 0.785 + 1.5708 * float(k);
                float3 anc = feet + float3(cos(a) * 1.05, 0.0, sin(a) * 1.05);
                // where it bites: wrists and ankles of the posed body
                float2 pa = k == 0 ? aR : (k == 1 ? aL : (k == 2 ? lR : lL));
                int    j = k == 0 ? 2 : (k == 1 ? 3 : (k == 2 ? 4 : 5));
                float3x3 Rj = mul(Rb, mul(rotZ(pa.y), rotX(pa.x)));
                float3 att = O + mul(Rb, kPP[j] * DPX) + mul(Rj, (kPC[j] - kPP[j] + float3(0, -4.5, 0)) * DPX);
                float  gk = saturate((t - 0.14 - 0.06 * float(k)) / 0.36);
                if (gk <= 0.0) continue;
                float3 ctl = (anc + att) * 0.5 + float3(cos(a), 0, sin(a)) * 0.9 * (1.0 - hoist * 0.8) + float3(0, 1.7 * (1.0 - hoist), 0);
                if (!nearRay(rd, (anc + att + ctl) / 3.0, 2.4)) continue;
                float3 prevP = anc;
                [loop] for (int l = 1; l <= 16; l++) {
                    float u1 = float(l) / 16.0 * gk;
                    float3 P = bez(anc, ctl, att, u1);
                    float3 T = normalize(P - prevP + 1e-5);
                    float3 M = (P + prevP) * 0.5;
                    if (M.y > feet.y) {
                        float3x3 R = basisOf(T, (l & 1) ? 1.5708 : 0.0);
                        float hot = exp(-pow((float(l) / 16.0 - frac(t * 1.6)) * 6.0, 2.0)) * step(1.95, t);   // a pulse runs down it as it drags
                        if (l == 16 && gk < 1.0) dsBox(h, rd, M + T * 0.06, float3(0.045, 0.13, 0.045), mul(R, rotY(0.785)), hex3(70, 70, 82), glow * 0.9);   // the barb
                        else dsBox(h, rd, M, float3(0.07, length(P - prevP) * 0.64, 0.026), R, hex3(58, 58, 66), glow * (0.03 + 0.9 * hot));
                    }
                    prevP = P;
                }
            }
            dsBurst(h, rd, feet + float3(0, 0.05, 0), t - 2.75, 14, seed, 2.2, 2.6, 11.0, 0.9, 0.05, float3(0.004, 0.005, 0.01), float3(0.02, 0.02, 0.04), feet.y, team * 0.15);
        } else if (sc == 1) {
            // ============ BLACKJACK: eight cards deal themselves round the victim, spin up, then cut through them one
            // after another; the body comes apart in five clean slices like a split deck, and falls
            float TS = 2.05;
            float out1 = ease((t - 3.7) / 0.5);
            [loop] for (int k0 = 0; k0 < 8; k0++) { float d0 = 1.15 + 0.1 * float(k0) + 0.07; fx.hurt = max(fx.hurt, exp(-max(t - d0, 0.0) * 12.0) * step(d0, t)); }
            fx.hurt *= step(t, TS);
            [loop] for (int sl = 0; sl < 5; sl++) {
                float lo = sl == 0 ? 0.0 : float(sl) * 6.0, hi = sl == 4 ? 32.0 : float(sl + 1) * 6.0;
                float u = t - TS;
                float3 offs = 0.0; float3x3 Rs = RY;
                if (u > 0.0) {
                    float dir = (sl & 1) ? 1.0 : -1.0;
                    float slide = easeOut(u / 0.2) * 0.13 * dir;                                           // the deck offsets
                    float uf = max(u - 0.25 - 0.05 * float(4 - sl), 0.0);                                   // then it falls
                    float rest = (float(sl) * 0.16 + 0.19) - (lo + hi) * 0.5 * DPX;                         // its place in the pile
                    float fall = max(-0.5 * 13.0 * uf * uf, rest);
                    offs = side * (slide + dir * 0.35 * min(uf, 0.45)) + fwd * (h1(seed + float(sl)) - 0.5) * 0.5 * min(uf, 0.45) + float3(0, fall, 0);
                    Rs = mul(RY, rotY((h1(seed + float(sl) * 7.0) - 0.5) * 2.2 * min(uf * 2.5, 1.0)));
                }
                dsBody(h, rd, feet + offs * (1.0 - out1) - float3(0, out1 * 1.2, 0), Rs, 1.0, lo, hi, fx);
            }
            [loop] for (int k = 0; k < 8; k++) {
                float  ap = 0.07 * float(k);
                if (t < ap) continue;
                float  d0 = 1.15 + 0.1 * float(k), u = (t - d0) / 0.14;
                float  to = min(t, d0);
                float  ang = yaw + 0.7854 * float(k) + 2.0 * to + 2.6 * to * to;
                float  yh = 1.0 + 0.16 * sin(to * 5.0 + float(k) * 1.3);
                float  r = lerp(0.3, 1.15, easeOut((t - ap) / 0.25));
                float3 po = float3(cos(ang) * r, yh, sin(ang) * r);
                float  cutY = (k == 0 || k == 5 ? 24.0 : (k == 1 || k == 7 ? 12.0 : (k == 2 || k == 4 ? 18.0 : 6.0))) * DPX;
                float3 p; float3x3 R; float scl = easeOut((t - ap) / 0.18) * (1.0 - out1);
                float3 pin = float3(cos(ang) * 1.15, cutY, sin(ang) * 1.15), pex = float3(-cos(ang) * 1.5, cutY, -sin(ang) * 1.5);
                if (u < 0.0) {
                    float wind = ease((t - d0 + 0.18) / 0.18);                                              // it drops to its cutting height and lays flat
                    p = lerp(po, pin, wind);
                    R = mul(rotY(-ang + 1.5708), rotX(1.5708 * wind));
                } else if (u < 1.0) { p = lerp(pin, pex, u); R = mul(rotY(-ang + 1.5708), rotX(1.5708)); }
                else {
                    float uf = t - d0 - 0.14;                                                               // spent: it sails on, settles to the ground
                    float3 v = normalize(pex - pin) * 2.2;
                    p = pex + v * (1.0 - exp(-uf * 3.0)) / 3.0;
                    p.y = max(cutY - 1.6 * uf * uf, 0.02);
                    R = mul(rotY(-ang + 1.5708 + uf * 1.5 * step(0.03, p.y)), rotX(1.5708));
                }
                if (u > 0.0 && u < 3.5) {                                                                    // the cut it left: a ribbon of light
                    float3 a0 = feet + pin, a1 = feet + lerp(pin, pex, saturate(u));
                    add += lerp(glow, float3(1, 1, 1), 0.6) * dsGlowSeg(rd, a0, a1, 0.022, h.t) * exp(-max(u - 1.0, 0.0) * 1.6) * 1.6;
                }
                if (nearRay(rd, feet + p, 0.5)) dsCard(h, rd, feet + p, R, scl, k * 5 + 3, glow * 0.25 * step(u, 1.0));
            }
            // chips: thrown out when the deck splits
            dsBurst(h, rd, feet + float3(0, 1.0, 0), t - TS, 12, seed + 5.0, 2.4, 1.6, 12.0, 2.1, 0.05, hex3(196, 28, 36), hex3(240, 236, 224), feet.y, 0.0);
            if (onG) { float sh = exp(-gr * gr / 0.5) * 0.35 * (1.0 - out1); dec = o * (1.0 - sh); decK = 1.0; }
        } else if (sc == 2) {
            // ============ TENTACLES: a pool, five arms; they coil round the victim, lift, slam them down twice, and
            // haul them under feet first
            float rp = 1.35 * easeOut(t / 0.3) * (1.0 - ease((t - 3.6) / 0.6));
            if (onG && gr < rp + 0.2) {
                float ring = 0.5 + 0.5 * sin((gr - t * 0.9) * 16.0 + atan2(g.y, g.x) * 2.0);
                float3 pool = hex3(14, 6, 20) + hex3(120, 50, 110) * 0.10 * ring * smoothstep(rp, rp * 0.2, gr);
                if (gr < rp) { dec = pow(pool, 1.0 / 2.2); decK = 1.0; }
                add += hex3(150, 70, 150) * exp(-pow((gr - rp) / 0.05, 2.0)) * 0.4;
            }
            float wrap = ease((t - 0.55) / 0.5);
            float lift = 0.55 * ease((t - 0.95) / 0.35);
            // the slams: over to one side (down at 1.62), back up, over to the other (down at 2.12)
            float s1 = ease((t - 1.42) / 0.20) - ease((t - 1.70) / 0.22), s2 = ease((t - 1.92) / 0.20);
            float under = pow(saturate((t - 2.45) / 0.95), 2.0);
            float ang = 1.5 * s1 - 1.5 * s2 + 1.0 * under;
            fx.hurt = max(exp(-max(t - 1.62, 0.0) * 8.0) * step(1.62, t), exp(-max(t - 2.12, 0.0) * 8.0) * step(2.12, t));
            fx.hurt = max(fx.hurt, 0.5 * wrap * (0.5 + 0.5 * sin(t * 40.0)) * step(t, 1.4));
            fx.clipY = feet.y + 0.01;
            float3 O = feet + float3(0, lift * (1.0 - s2) - under * 2.5, 0);
            float3x3 Rb = mul(RY, rotZ(-ang));
            dsPosed(h, rd, O, Rb, 1.0, float2(0.3 * wrap, 0), float2(0, 0.12), float2(0, -0.12), float2(0, 0), float2(0, 0), 63, fx);
            [loop] for (int k = 0; k < 5; k++) {
                float  a0 = 1.2566 * float(k) + 0.4;
                float  grow = easeOut((t - 0.06 * float(k)) / 0.5);
                if (grow <= 0.0) continue;
                float3 base = feet + mul(RY, float3(cos(a0) * 0.95, -0.15, sin(a0) * 0.95));
                float  swA = sin(TIME * 3.1 + float(k) * 2.0) * 0.25, swB = cos(TIME * 2.3 + float(k)) * 0.25;
                // free: it stands and sways; wrapped: it leans in and winds round the body (in the body's frame)
                float  hk = (6.0 + 5.0 * float(k)) * DPX;
                float3 w1 = O + mul(Rb, float3(cos(a0 + 1.4) * 0.52, hk * 0.8, sin(a0 + 1.4) * 0.40)), w2 = O + mul(Rb, float3(cos(a0 + 3.4) * 0.42, hk + 0.25, sin(a0 + 3.4) * 0.30)),
                       w3 = O + mul(Rb, float3(cos(a0 + 5.6) * 0.40, hk + 0.50, sin(a0 + 5.6) * 0.28));
                float3 inw = mul(RY, float3(-cos(a0), 0, -sin(a0)));
                float3 f1 = base + float3(swA * 0.3, 0.95, swB * 0.3), f2 = base + float3(swA, 1.7, swB) + inw * 0.2, f3 = base + float3(swA * 1.6, 2.35, swB * 1.6) + inw * 0.55;
                dsTentacle(h, rd, base, lerp(f1, w1, wrap), lerp(f2, w2, wrap), lerp(f3, w3, wrap), 0.17, grow, O, glow * 0.6, feet.y);
            }
            dsBurst(h, rd, feet + side * 1.55 + float3(0, 0.05, 0), t - 1.62, 12, seed + 1.0, 1.7, 1.9, 10.0, 0.8, 0.055, hex3(122, 86, 52), hex3(74, 60, 48), feet.y, 0.0);
            dsBurst(h, rd, feet - side * 1.55 + float3(0, 0.05, 0), t - 2.12, 12, seed + 2.0, 1.7, 1.9, 10.0, 0.8, 0.055, hex3(122, 86, 52), hex3(74, 60, 48), feet.y, 0.0);
            dsBurst(h, rd, feet + float3(0, 0.05, 0), t - 3.05, 10, seed + 3.0, 1.5, 2.2, 11.0, 0.8, 0.045, hex3(14, 6, 20), hex3(60, 24, 70), feet.y, 0.0);
        } else if (sc == 3) {
            // ============ LIGHTNING ASH: the air charges, a bolt splits the sky and takes them; a charred figure stands
            // glowing at the seams, then crumbles from the head down and blows away, leaving a scorch
            float TS = 0.42;
            float3 bcol = lerp(float3(0.62, 0.74, 1.0), float3(1, 1, 1), 0.6);
            float st0 = step(TS, t);
            fx.charK = smoothstep(TS + 0.05, TS + 0.16, t);
            fx.flash = st0 * exp(-(t - TS) * 22.0);
            fx.ash = ease((t - 1.45) / 1.45);
            float3 O = feet + float3(sin(t * 90.0) * 0.012 * st0 * exp(-(t - TS) * 5.0), 0, 0);
            dsPosed(h, rd, O, RY, 1.0, float2(-0.5 * st0, 0), float2(0.25 * st0, -0.5 * st0), float2(0.25 * st0, 0.5 * st0), float2(0, -0.08), float2(0, 0.08), 63, fx);
            // the charge: a ring of light closes on their feet; then the scorch
            if (onG) {
                float rr = lerp(1.9, 0.2, ease(t / TS));
                add += bcol * exp(-pow((gr - rr) / 0.035, 2.0)) * step(t, TS) * (0.3 + 0.7 * t / TS) * 0.9;
                float a = atan2(g.y, g.x);
                float star = 0.55 + 0.45 * sin(a * 7.0 + seed) * sin(a * 3.0 + 1.0);
                float sc0 = smoothstep(1.15 * star + 0.25, 0.1, gr) * st0 * (1.0 - ease((t - 4.0) / 0.7));
                dec = o * (1.0 - 0.82 * sc0); decK = 1.0;
                add += float3(1.0, 0.35, 0.06) * step(0.93, hash12(g * 16.0 + 3.0)) * sc0 * exp(-(t - TS) * 1.1) * 1.4;
            }
            // the bolt: leaders flicker down first, then the strike (it re-strikes twice), with two forks
            float strike = t < TS ? 0.25 * step(0.6, frac(t * 31.0)) * (t / TS) : (exp(-(t - TS) * 16.0) + 0.7 * step(0.5, frac((t - TS) * 17.0)) * step(t, TS + 0.24));
            if (strike > 0.01) {
                float reach = t < TS ? lerp(0.25, 0.9, t / TS) : 1.0;
                float3 top = feet + float3(3.0, 44.0, -2.0), prev = top;
                float  bolt = 0.0;
                [loop] for (int l = 1; l <= 18; l++) {
                    float  s0 = 1.0 - pow(1.0 - float(l) / 18.0, 2.2);   // short segments near the ground, long ones up high
                    if (s0 > reach) break;
                    float3 P = lerp(top, feet + float3(0, 1.9, 0), s0);
                    float  jag = (1.0 - s0) * 9.0 + 0.55;
                    P.xz += (float2(h1(seed + float(l) * 1.7), h1(seed + float(l) * 3.1)) - 0.5) * jag * (l < 18 ? 1.0 : 0.0);
                    bolt += dsGlowSeg(rd, prev, P, 0.045, h.t) + dsGlowSeg(rd, prev, P, 0.16, h.t) * 0.12;
                    if ((l == 9 || l == 13 || l == 16) && t >= TS) {   // a fork
                        float3 F = P + float3(h1(seed + float(l)) - 0.5, -0.45, h1(seed + float(l) * 9.0) - 0.5) * (l == 16 ? 2.2 : 6.0);
                        float3 Fm = (P + F) * 0.5 + float3(h1(seed + 4.0) - 0.5, 0, h1(seed + 5.0) - 0.5) * 1.5;
                        bolt += (dsGlowSeg(rd, P, Fm, 0.03, h.t) + dsGlowSeg(rd, Fm, F, 0.02, h.t)) * 0.7;
                    }
                    prev = P;
                }
                add += bcol * bolt * strike * 5.0;
                float3 bc = feet + float3(0, 1, 0), bq = bc - rd * dot(bc, rd);
                add += bcol * exp(-dot(bq, bq) / 0.9) * st0 * exp(-(t - TS) * 14.0) * 0.5 * step(dot(feet, rd), h.t + 2.0);
            }
            // embers rise off the figure; ash falls away from the crumbling line and goes with the wind
            dsBurst(h, rd, feet + float3(0, 1.3, 0), t - TS - 0.05, 10, seed + 7.0, 0.5, 0.9, -0.6, 1.3, 0.022, float3(0.02, 0.01, 0.01), float3(0.03, 0.02, 0.02), -1e5, float3(1.0, 0.35, 0.06) * 1.5);
            if (t > 1.45) {
                [loop] for (int k = 0; k < 30; k++) {
                    float  sk = seed * 0.01 + float(k) * 3.3;
                    float3 st = float3((h1(sk) - 0.5) * 0.6, h1(sk + 1.0) * 1.85, (h1(sk + 2.0) - 0.5) * 0.3);
                    float  born = 1.45 + 1.45 * (1.0 - st.y / 1.85) * 0.92;
                    float  u = t - born;
                    if (u < 0.0 || u > 1.5) continue;
                    float3 wind = float3(1.0, 0.0, 0.45) * (0.6 + 0.8 * h1(sk + 3.0));
                    float3 p = feet + mul(RY, st) + wind * u * u * 1.1 + float3(0, -0.9 * u + 0.25 * sin(u * 6.0 + sk), 0);
                    p.y = max(p.y, feet.y + 0.02);
                    float  e = 0.042 * (1.0 - u / 1.5) * (0.6 + 0.8 * h1(sk + 6.0));
                    dsBox(h, rd, p, float3(e, e * 0.4, e), rotAxis(normalize(wind + float3(0, 0.4, 0)), u * 7.0 + sk), float3(0.05, 0.045, 0.045), float3(1.0, 0.4, 0.1) * exp(-u * 4.0) * 0.6);
                }
            }
        } else if (sc == 4) {
            // ============ GIANT HAND: a shadow spreads under them, a huge hand comes down out of the sky, closes,
            // squeezes, and carries them off
            float down = 1.0 - easeOut((t - 0.30) / 0.42);
            float curl = ease((t - 0.74) / 0.16);
            float lift = 1.15 * ease((t - 1.35) / 0.35) + 60.0 * pow(saturate((t - 1.75) / 0.8), 2.4);
            float shake = sin(t * 55.0) * 0.022 * curl * step(t, 1.35);
            if (onG) {
                float rs = lerp(0.35, 1.7, ease(t / 0.7)) * (1.0 - ease((t - 1.6) / 0.5) * 0.8);
                float sh = smoothstep(rs, rs * 0.45, gr) * 0.55 * ease(t / 0.4) * (1.0 - ease((t - 1.7) / 0.6));
                dec = o * (1.0 - sh); decK = 1.0;
            }
            fx.hurt = exp(-max(t - 0.86, 0.0) * 7.0) * step(0.86, t);
            float3 O = feet + float3(shake, lift, 0);
            if (t < 2.6) {
                dsPosed(h, rd, O, RY, 1.0, float2(-0.7 * ease(t / 0.3) * (1.0 - curl), 0), float2(0.0, -0.05), float2(0.0, 0.05), float2(0, 0), float2(0, 0), 63, fx);
                dsHand(h, rd, O + float3(0, 2.12 + 26.0 * down * down, 0), RY, curl);
            }
            dsBurst(h, rd, feet + float3(0, 0.05, 0), t - 0.80, 14, seed + 1.0, 2.0, 0.7, 5.0, 0.7, 0.05, hex3(150, 140, 130), hex3(98, 92, 88), feet.y, 0.0);
            // the rush of it leaving: thin lines of air
            if (t > 1.75 && t < 2.7) {
                [loop] for (int k = 0; k < 6; k++) {
                    float  a = float(k) * 1.047 + seed, u = t - 1.75;
                    float3 b0 = feet + float3(cos(a) * 0.8, 0.5 + 9.0 * u * u + h1(float(k)) * 2.0, sin(a) * 0.8);
                    add += float3(1, 1, 1) * dsGlowSeg(rd, b0, b0 + float3(0, 1.2 + 3.0 * u, 0), 0.012, h.t) * 0.35 * (1.0 - u / 0.95);
                }
            }
        } else if (sc == 5) {
            // ============ HUNGRY CHEST: it bursts out of the ground, peeks, snaps open; a tongue takes them; it chews,
            // burps, and sinks back
            float3 cpos0 = feet + fwd * 1.45;
            float rise = backOut(t / 0.28) - ease((t - 3.05) / 0.6);
            float peek = 0.16 * ease((t - 0.30) / 0.12) * (1.0 - step(0.62, t));
            float wide = 1.75 * backOut((t - 0.62) / 0.14) * (1.0 - ease((t - 1.20) / 0.05));
            float chewT = t - 1.26, chew = (chewT > 0.0 && chewT < 1.0) ? abs(sin(chewT * 12.566)) : 0.0;                  // four chomps
            float burp = 0.55 * (ease((t - 2.40) / 0.08) - ease((t - 2.62) / 0.12));
            float open = max(max(peek, wide), max(chew * 0.34, burp));
            float sq = 1.0 - 0.16 * exp(-max(t - 1.22, 0.0) * 14.0) * step(1.22, t) + chew * 0.10;
            float3 cpos = cpos0 + float3(0, -0.95 * (1.0 - rise) + chew * 0.07, 0);
            float3x3 RC = mul(RY, rotY(3.14159 + 0.10 * sin(t * 26.0) * step(0.30, t) * step(t, 0.62)));                   // it faces the victim
            float3 mouth = cpos + float3(0, 0.72, 0) - fwd * 0.2;
            // the tongue: out at 0.66, on them at 0.80, back with them by 1.18
            float tOut = easeOut((t - 0.66) / 0.14), tIn = ease((t - 0.86) / 0.32);
            float3 bodyC = lerp(feet + float3(0, 1.05, 0), mouth, tIn);                                                    // the victim's middle
            fx.hurt = exp(-max(t - 0.80, 0.0) * 8.0) * step(0.80, t);
            if (t < 1.2) {
                float sB = 1.0 - 0.6 * tIn;
                float3x3 Rb = mul(RY, rotX(1.45 * tIn));
                dsPosed(h, rd, bodyC - mul(Rb, float3(0, 18.0 * DPX * sB, 0)), Rb, sB, float2(0, 0), float2(-2.6 * tIn, 0), float2(-2.6 * tIn, 0), float2(0, 0), float2(0, 0), 63, fx);
            }
            if (t > 0.66 && t < 1.2) {
                float3 tip = lerp(mouth, bodyC + fwd * 0.12, tOut);
                float3 mid = (mouth + tip) * 0.5 + float3(0, 0.30 * sin(saturate((t - 0.66) / 0.5) * 3.1416), 0);
                float3 prevP = mouth;
                [loop] for (int l = 1; l <= 8; l++) {
                    float3 P = bez(mouth, mid, tip, float(l) / 8.0);
                    dsCap(h, rd, prevP, P, 0.075 - 0.003 * float(l), hex3(206, 52, 78), float3(0.5, 0.2, 0.25), 0.0);
                    prevP = P;
                }
                if (tOut > 0.95) dsCap(h, rd, bodyC + fwd * 0.2 + side * 0.28, bodyC - fwd * 0.2 - side * 0.28, 0.07, hex3(206, 52, 78), float3(0.5, 0.2, 0.25), 0.0);   // it has them
            }
            dsChest(h, rd, cpos, RC, open, sq, step(0.30, t) * step(t, 0.62), team);
            dsBurst(h, rd, cpos0 + float3(0, 0.05, 0), t - 0.02, 16, seed + 1.0, 1.9, 2.4, 12.0, 0.8, 0.055, hex3(122, 86, 52), hex3(74, 60, 48), feet.y, 0.0);
            [loop] for (int c4 = 0; c4 < 4; c4++)   // crumbs with every chomp
                dsBurst(h, rd, mouth, t - 1.26 - 0.25 * float(c4) - 0.06, 5, seed + 9.0 + float(c4), 1.6, 1.4, 12.0, 0.6, 0.035, team * 0.5, hex3(150, 20, 24), feet.y, 0.0);
            // the burp: a bone, and a small green cloud of cubes
            dsBurst(h, rd, mouth, t - 2.44, 1, seed + 20.0, 1.8, 2.6, 10.0, 1.3, 0.07, hex3(240, 236, 220), hex3(240, 236, 220), feet.y, 0.0);
            dsBurst(h, rd, mouth + float3(0, 0.1, 0), t - 2.42, 9, seed + 21.0, 0.5, 0.7, -0.4, 0.9, 0.05, hex3(150, 190, 110), hex3(196, 220, 160), -1e5, 0.0);
            dsBurst(h, rd, cpos0 + float3(0, 0.05, 0), t - 3.25, 10, seed + 30.0, 1.3, 1.2, 10.0, 0.6, 0.045, hex3(122, 86, 52), hex3(74, 60, 48), feet.y, 0.0);
            if (onG) { float2 gc = S.xz - cpos0.xz; float sh = exp(-dot(gc, gc) / 0.35) * 0.45 * saturate(rise); dec = o * (1.0 - sh); decK = 1.0; }
        } else if (sc >= 7) {
            deathScenesExt(sc, t, o, rd, feet, yaw, eC[i].y, seed, team, glow, onG, S, gr, h, add, dec, decK, fx);
        } else {
            // ============ EXECUTIONER: a hooded figure rises out of smoke behind them, lifts a great axe, and takes
            // the head in one stroke; the body kneels and falls; the figure blows away
            float TH = 1.02;                                                                                               // the stroke lands
            float rise = easeOut(t / 0.45);
            float gone = ease((t - 2.5) / 0.9);
            float3 EO = feet - fwd * 0.75 - side * 1.0;
            float3x3 RE = mul(RY, rotY(0.93));
            float wind = ease((t - 0.45) / 0.42);                                                                          // the axe goes back
            float sw = easeOut((t - 0.92) / 0.13);                                                                         // and comes round
            float settle = ease((t - 1.5) / 0.5);
            dsExecutioner(h, rd, EO - float3(0, (1.0 - rise) * 0.9, 0), RE, 1.0 * wind + 0.5 * sw - 1.1 * settle, max(1.0 - rise * 1.6, gone), team);
            dsBurst(h, rd, EO + float3(0, 0.3, 0), t, 14, seed + 1.0, 0.7, 1.2, -0.8, 0.9, 0.06, float3(0.012, 0.012, 0.016), float3(0.035, 0.03, 0.045), -1e5, 0.0);
            dsBurst(h, rd, EO + float3(0, 1.2, 0), t - 2.5, 18, seed + 2.0, 0.8, 1.4, -1.2, 1.2, 0.06, float3(0.012, 0.012, 0.016), float3(0.035, 0.03, 0.045), -1e5, 0.0);
            // the axe: it turns about the figure, level with the neck
            if (gone < 0.6 && rise > 0.5) {
                float aa = lerp(lerp(-0.4, -2.3, wind), 1.9, sw);
                float tilt = lerp(lerp(0.9, 0.25, wind), 0.0, sw) + 1.25 * settle;
                float3x3 RA = mul(RE, mul(rotY(aa), rotX(-tilt)));
                float3 hand = EO + float3(0, 1.62 - 0.4 * settle, 0) + mul(RE, mul(rotY(aa), float3(0, 0, 0.35)));
                float3 dir = mul(RA, float3(0, 0, 1));
                float3 headP = hand + dir * 1.15;
                dsCap(h, rd, hand - dir * 0.45, headP + dir * 0.12, 0.035, hex3(92, 58, 32), 0.0, 0.0);                    // the haft
                float gl = exp(-pow((t - 0.80) / 0.06, 2.0));                                                              // a glint runs the edge before it falls
                float3x3 RB = mul(RA, rotZ(0.45));                                                                       // the blades, canted to catch the light
                dsBox(h, rd, headP + mul(RB, float3(0.24, 0, 0)), float3(0.20, 0.028, 0.30), RB, hex3(150, 154, 168), glow * 0.35 + gl * 2.0);
                dsBox(h, rd, headP + mul(RB, float3(-0.24, 0, 0)), float3(0.20, 0.028, 0.30), RB, hex3(150, 154, 168), glow * 0.35 + gl * 2.0);
                dsBox(h, rd, headP, float3(0.05, 0.05, 0.10), RA, hex3(52, 48, 52), 0.0);
                if (t > 0.92 && t < 1.5) {                                                                                  // the arc of the stroke
                    [loop] for (int l = 0; l < 8; l++) {
                        float a0 = lerp(-2.3, 1.9, float(l) / 8.0 * sw), a1 = lerp(-2.3, 1.9, float(l + 1) / 8.0 * sw);
                        float3 q0 = EO + float3(0, 1.62, 0) + mul(RE, mul(rotY(a0), float3(0.2, 0, 1.5))), q1 = EO + float3(0, 1.62, 0) + mul(RE, mul(rotY(a1), float3(0.2, 0, 1.5)));
                        add += lerp(glow, float3(1, 1, 1), 0.55) * dsGlowSeg(rd, q0, q1, 0.03, h.t) * exp(-(t - 0.92) * 6.0) * 1.3 * (float(l) / 8.0);
                    }
                }
            }
            // the victim: frozen, then headless - knees give, it tips forward
            float u = max(t - TH, 0.0);
            fx.flash = step(TH, t) * step(t, TH + 0.05);
            float kneel = ease(u / 0.35), fall = pow(saturate((u - 0.3) / 0.55), 2.0);
            float out1 = ease((t - 3.4) / 0.6);
            float3 BO = feet - float3(0, kneel * 5.5 * DPX * (1.0 - fall) - fall * 2.0 * DPX + out1 * 1.0, 0);
            float3x3 Rb = mul(RY, rotX(1.50 * fall));
            dsPosed(h, rd, BO, Rb, 1.0 - out1 * 0.5, float2(0, 0), float2(0.3 * fall, 0), float2(0.3 * fall, 0), float2(-1.2 * kneel * (1.0 - fall), 0), float2(-1.2 * kneel * (1.0 - fall), 0), t < TH ? 63 : 62, fx);
            if (t >= TH) {   // the head: up and away, one bounce, a short roll
                float3 v = side * 1.5 + fwd * 0.5 + float3(0, 3.4, 0);
                float  y0 = 28.0 * DPX, G = 12.0;
                float  tl = (v.y + sqrt(v.y * v.y + 2.0 * G * (y0 - 4.0 * DPX))) / G;
                float3 hp; float spin;
                if (u < tl) { hp = float3(0, y0, 0) + v * u + float3(0, -0.5 * G * u * u, 0); spin = u * 9.0; }
                else {
                    float u2 = u - tl, vb = 2.2, tb = 2.0 * vb / G, ub = min(u2, tb);
                    hp = float3(0, 4.0 * DPX, 0) + (side * 1.5 + fwd * 0.5) * (tl + 0.45 * (1.0 - exp(-u2 * 2.5))) + float3(0, max(vb * ub - 0.5 * G * ub * ub, 0.0), 0);
                    spin = tl * 9.0 + 3.0 * (1.0 - exp(-u2 * 2.5));
                }
                float3x3 HR = mul(RY, rotAxis(normalize(float3(0.2, 0.3, 1.0)), spin));
                DFx fh = fx; fh.flash = 0.0;
                dsPart(h, rd, 0, feet + hp - float3(0, out1 * 0.6, 0), HR, 1.0 - out1, -99.0, 99.0, fh);
                dsBurst(h, rd, feet + float3(0, 24.0 * DPX, 0), u, 12, seed + 3.0, 1.5, 2.4, 12.0, 0.9, 0.035, hex3(110, 2, 2), hex3(181, 18, 18), feet.y, 0.0);
            }
            if (onG) { float2 gc = S.xz - EO.xz; float sh = (exp(-dot(gc, gc) / 0.4) * (1.0 - gone) * rise + exp(-gr * gr / 0.5) * (1.0 - out1)) * 0.4; dec = o * (1.0 - saturate(sh)); decK = 1.0; }
        }
        // compose: the ground, then the solids, then the light
        if (onG && decK > 0.0 && !h.hit) o = lerp(o, dec, decK);
        if (h.hit) { o = pow(max(h.c, 0.0), 1.0 / 2.2); sceneT = h.t; }
        o = pow(pow(max(o, 0.0), 2.2) + add, 1.0 / 2.2);
    }
    return o;
}
