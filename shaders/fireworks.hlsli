// Renderer Foundation - World FX: the victory fireworks show (by Novak).
// Reference: a Chinese New Year city show. Tiers of shells going up together left and right of a centre line, gold
// brocade crowns that hang and glitter, colour peonies with a gold heart, rings, palms, silver strobes, comet fans off
// the horizon, salutes, a finale barrage - and the smoke of it drifting, lit by the next burst, with the light of it
// all on the cloud sea and on the islands.
//
// How a shell is drawn: its stars all fly the same way (out along their own direction, slowing, then sagging), so a
// pixel can ask "which star would be HERE if this bit of trail was laid s seconds after the burst?" - one division, and
// the answer is a point in the shell's direction space, where the stars sit on a jittered grid. A shell has ~400 stars
// and a pixel only ever looks at the four nearest per trail piece.

static const float3 FW_GOLD  = float3(1.25, 0.70, 0.22);
static const float3 FW_EMBER = float3(0.95, 0.22, 0.04);
static const float3 FW_WHITE = float3(1.00, 0.96, 0.88);
static const int    FW_SHELLS = 52;      // 24 in the body of the show (12 pairs), 23 in the finale, the last five together
static const float  FW_FIN_DT = 0.15;    // the finale: one shell every this many seconds
static const float  FW_LEN = 15.5;       // seconds, the last glitter gone

float  fwH(float x) { return frac(sin(x * 12.9898 + 4.1414) * 43758.5453); }
float2 fwH2(float2 p, float s) {
    float3 q = frac(float3(p.x, p.y, p.x) * float3(0.1031, 0.1030, 0.0973) + s * 0.1537);
    q += dot(q, q.yzx + 33.33);
    return frac((q.xx + q.yz) * q.zy);
}

// per type: 0 peony, 1 chrysanthemum, 2 brocade crown, 3 silver strobe, 4 ring, 5 palm, 6 pistil (a peony's gold heart)
static const float kFwK[7]    = {3.6, 3.2, 2.5, 3.6, 3.6, 2.3, 3.8};        // how fast the stars slow
static const float kFwG[7]    = {0.030, 0.050, 0.082, 0.026, 0.030, 0.070, 0.030}; // sag (shell radii / s^2)
static const float kFwL[7]    = {0.34, 0.62, 1.75, 0.07, 0.11, 1.25, 0.20}; // seconds of trail behind each star
static const float kFwCell[7] = {0.086, 0.096, 0.104, 0.118, 0.0, 0.0, 0.170}; // star grid (smaller = more stars)
static const float kFwLife[7] = {2.4, 2.9, 4.4, 3.2, 2.3, 3.5, 1.9};
static const float kFwW[7]    = {1.0, 0.9, 0.85, 0.80, 1.35, 1.9, 1.0};      // line width
static const float kFwPw[7]   = {1.7, 1.8, 1.25, 3.0, 1.8, 1.4, 2.0};       // how the trail fades along its length

// trail pieces are anchored to the burst (so the glitter stays where it was laid): close together while the stars
// fly out, then one every FW_DS seconds while they hang
static const float FW_DR = 0.07;
static const float FW_J0 = 13.0;
float fwSeqT(float j, float k, float ds) {
    float s0 = -log(1.0 - FW_J0 * FW_DR) / k;
    return j <= FW_J0 ? -log(1.0 - j * FW_DR) / k : s0 + (j - FW_J0) * ds;
}
float fwSeqJ(float s, float k, float ds) {
    float s0 = -log(1.0 - FW_J0 * FW_DR) / k;
    return s <= s0 ? (1.0 - exp(-k * s)) / FW_DR : FW_J0 + (s - s0) / ds;
}

// one piece of one star's trail (A older end, B newer end), seen from P. del0/del1: how long ago each end was laid
float3 fwPiece(float2 P, float2 A, float2 B, float w, float del0, float del1, bool head, int type, float3 col, float id, float a) {
    float2 sg = B - A, ap = P - A;
    float  tu = dot(ap, sg) / max(dot(sg, sg), 1e-9);
    float  tt = saturate(tu);
    float2 dv = ap - sg * tt;
    float  d2 = dot(dv, dv), w2 = w * w;
    if (d2 > w2 * 16.0) return 0.0;
    float  L = kFwL[type];
    float  del = lerp(del0, del1, tt);
    float  body = (tu >= 0.0 && tu < 1.0) ? exp(-d2 / w2) + 0.05 * exp(-d2 / (w2 * 6.0)) : 0.0;   // a core and a little glow
    float  prof = pow(saturate(1.0 - del / L), kFwPw[type]);
    float  bv = fwH(id * 3.71);
    float  bri = 0.38 + 0.62 * bv * bv + (bv > 0.93 ? 0.6 : 0.0);   // a few stars burn brighter than the rest
    if (type == 0 || type == 6) {                                    // a peony is a ball: its far side is dimmer and finer, and it shimmers
        bool back = fwH(id * 1.37 + 0.5) < 0.5;
        bri *= (back ? 0.45 : 1.0) * (0.80 + 0.20 * sin(TIME * 21.0 + id * 40.0));
        w2 *= back ? 0.6 : 1.0;
    }
    float3 c;
    if (type == 2 || type == 5) {          // brocade / palm: gold that cools to ember down the trail, and glitters
        c = lerp(FW_GOLD, FW_EMBER, pow(saturate(del / L), 0.6));
        float gl = step(0.80, fwH(id * 7.13 + floor((a - del) * 46.0) * 1.37 + floor(TIME * 19.0) * 0.731));
        prof *= lerp(1.0, 0.42 + 2.6 * gl, smoothstep(0.05, 0.22, del));
    } else if (type == 1) {                // chrysanthemum: the star in colour, its tail charcoal gold
        c = lerp(col, FW_GOLD * float3(1.0, 0.8, 0.6), smoothstep(0.03, 0.22, del));
        c = lerp(c, FW_GOLD, smoothstep(1.5, 2.3, a) * 0.8);        // the tips turn gold before they go
    } else if (type == 3) {
        c = FW_WHITE;
    } else if (type == 6) {
        c = FW_GOLD;
    } else {
        c = col;
    }
    float3 o = c * body * prof * bri;
    if (head) {                            // the star itself: a hot point
        float2 dh = P - B;
        float  hd = exp(-dot(dh, dh) / (w2 * 2.2)) + 0.05 * exp(-dot(dh, dh) / (w2 * 12.0));
        float  on = 1.0;
        if (type == 3) on = a > 0.45 ? step(0.68, fwH(id * 5.3 + floor(TIME * 11.0 + fwH(id) * 11.0) * 0.37)) * 3.4 : 1.0;   // silver strobes: a third lit at a time
        float3 hc = (type == 2 || type == 5 || type == 6) ? lerp(FW_GOLD, FW_WHITE, 0.40) : lerp(c, FW_WHITE, 0.10);
        o += hc * hd * bri * on * (type == 2 ? 0.8 : 1.7);
    }
    return o;
}

// one shell. P: this pixel on the shell's plane, in shell radii (y up). a: seconds since it burst. w: line width (radii)
float3 fwShellDraw(float2 P, float a, int type, float3 col, float sd, float w, int maxPieces) {
    float life = kFwLife[type];
    if (a <= 0.0 || a >= life) return 0.0;
    float k = kFwK[type], g = kFwG[type], L = kFwL[type], h = kFwCell[type];
    float Ga = g * a * a;
    if (abs(P.x) > 1.08 || P.y > 1.08 || P.y < -(1.08 + Ga)) return 0.0;
    w *= kFwW[type];
    float  fade = smoothstep(life, life * 0.55, a);
    float  hot = exp(-a * 7.0);
    if (type == 2 && a > L + 0.6) {                             // (before that the tails still reach back to the burst)
        // A crown that has opened and hangs: its stars have all but stopped moving outward, so each trail is one
        // near-vertical curve. The stars whose trail could cross this pixel sit on a short line of directions - walk
        // that line a grid row at a time (two stars a row), and for each star solve where on ITS trail the pixel is.
        float sA = max(a - L, 0.0);
        float rA = 1.0 - exp(-k * a), rB = 1.0 - exp(-k * sA);
        float2 qT = float2(P.x, P.y + Ga) / rA, qB = float2(P.x, P.y + g * sA * sA) / rB;
        if (abs(qT.x) > 1.06 || qB.y > 1.06 || qT.y < -1.06) return 0.0;
        float lT = min(length(qT), 1.0), lB = min(length(qB), 1.0);
        float2 wT = qT * (sqrt(max(1.0 - sqrt(max(1.0 - lT * lT, 0.0)), 0.0)) / max(length(qT), 1e-5));
        float2 wB = qB * (sqrt(max(1.0 - sqrt(max(1.0 - lB * lB, 0.0)), 0.0)) / max(length(qB), 1e-5));
        int   r1 = int(floor(wT.y / h)), r0 = max(int(floor(wB.y / h)), r1 - (maxPieces + 1));
        float3 acc2 = 0.0;
        float  w2 = w * w;
        [loop] for (int r = r0; r <= r1; r++) {
            float tr = saturate(((float(r) + 0.5) * h - wB.y) / max(wT.y - wB.y, 1e-4));
            float c0 = floor(lerp(wB.x, wT.x, tr) / h - 0.5);
            [unroll] for (int c3 = 0; c3 < 2; c3++) {
                float2 cell = float2(c0 + float(c3), float(r));
                float2 jt = fwH2(cell, sd);
                float2 ws = (cell + jt) * h;
                float  wl2 = dot(ws, ws);
                if (wl2 >= 1.0) continue;
                float2 qs = ws * sqrt(2.0 - wl2);
                float  id = jt.x * 91.7 + jt.y * 37.3 + sd;
                float  bv = fwH(id * 3.71);
                float  bri = 0.38 + 0.62 * bv * bv + (bv > 0.93 ? 0.6 : 0.0);
                // the star itself
                float2 dh = P - (qs * rA - float2(0.0, Ga));
                float  hd = exp(-dot(dh, dh) / (w2 * 2.2)) + 0.05 * exp(-dot(dh, dh) / (w2 * 12.0));
                acc2 += lerp(FW_GOLD, FW_WHITE, 0.40) * hd * bri * 0.8;
                // its trail: when did it pass this height?
                float s1 = sqrt(max((qs.y * rA - P.y) / g, 0.0));
                float s2 = sqrt(max((qs.y * (1.0 - exp(-k * s1)) - P.y) / g, 0.0));
                if (s2 <= sA || s2 >= a) continue;
                float dx = P.x - qs.x * (1.0 - exp(-k * s2));
                float d2 = dx * dx;
                if (d2 > w2 * 16.0) continue;
                float del = a - s2;
                float prof = pow(saturate(1.0 - del / L), kFwPw[2]);
                float gl = step(0.80, fwH(id * 7.13 + floor(s2 * 46.0) * 1.37 + floor(TIME * 19.0) * 0.731));
                prof *= lerp(1.0, 0.42 + 2.6 * gl, smoothstep(0.05, 0.22, del));
                acc2 += lerp(FW_GOLD, FW_EMBER, pow(saturate(del / L), 0.6)) * (exp(-d2 / w2) + 0.05 * exp(-d2 / (w2 * 6.0))) * prof * bri;
            }
        }
        return acc2 * fade;
    }
    float  ds = (type == 2 || type == 5) ? 0.19 : 0.11;        // long trails are laid in longer pieces
    int    j1 = int(floor(fwSeqJ(a, k, ds)));
    int    j0 = max(max(int(floor(fwSeqJ(max(a - L, 0.0), k, ds))), j1 - (maxPieces - 1)), 0);
    bool   line1 = type == 4 || type == 5;                     // rings and palms: stars on a line of directions, not a sphere
    float  n1 = type == 4 ? 46.0 : 11.0;
    float  tilt = type == 4 ? 0.30 + 0.5 * fwH(sd + 2.2) : 1.0;
    float  rot = type == 4 ? (fwH(sd + 3.3) - 0.5) * 1.4 : fwH(sd + 3.3) * 6.2832;
    float  cr = cos(rot), sr = sin(rot);
    float3 acc = 0.0;
    [loop] for (int j = j0; j <= j1; j++) {
        float s0 = fwSeqT(float(j), k, ds), s1 = j == j1 ? a : fwSeqT(float(j + 1), k, ds);
        if (s1 - s0 < 1e-4) continue;
        float sm = 0.5 * (s0 + s1);
        float r0 = 1.0 - exp(-k * s0), r1 = 1.0 - exp(-k * s1), rm = 1.0 - exp(-k * sm);
        float G0 = g * s0 * s0, G1 = g * s1 * s1;
        float2 q = (P + float2(0.0, g * sm * sm)) / max(rm, 0.02);   // where a star would have to point to be here
        float  q2 = dot(q, q);
        if (q2 > 1.35) continue;
        bool   head = j == j1;
        if (line1) {
            float2 ql = float2(q.x * cr + q.y * sr, (-q.x * sr + q.y * cr) / tilt);
            float  fi = atan2(ql.y, ql.x) / 6.2831853 * n1 - 0.5;
            float  i0 = floor(fi);
            [unroll] for (int c1 = 0; c1 < 2; c1++) {
                float im = fmod(i0 + float(c1) + n1 * 2.0, n1);
                float jt = fwH(im * 1.931 + sd * 0.731);
                float ph = (im + 0.5 + (jt - 0.5) * 0.5) * 6.2831853 / n1;
                float rr = type == 5 ? 0.70 + 0.30 * fwH(im * 3.1 + sd) : 1.0;
                float2 l = rr * float2(cos(ph), sin(ph) * tilt);
                float2 qs = float2(l.x * cr - l.y * sr, l.x * sr + l.y * cr);
                acc += fwPiece(P, qs * r0 - float2(0.0, G0), qs * r1 - float2(0.0, G1), w, a - s0, a - s1, head, type, col, im + sd, a);
            }
        } else {
            float  ql = min(sqrt(q2), 1.0);
            float  wl = sqrt(max(1.0 - sqrt(max(1.0 - ql * ql, 0.0)), 0.0));        // equal-area: a sphere of stars, seen flat
            float2 wq = q2 > 1e-10 ? q * (wl / sqrt(q2)) : float2(0.0, 0.0);
            float2 b0 = floor(wq / h - 0.5);
            [unroll] for (int c2 = 0; c2 < 4; c2++) {
                float2 cell = b0 + float2(float(c2 & 1), float(c2 >> 1));
                float2 jt = fwH2(cell, sd);
                float2 ws = (cell + jt) * h;
                float  w2 = dot(ws, ws);
                if (w2 >= 1.0) continue;
                float2 qs = ws * sqrt(2.0 - w2);
                acc += fwPiece(P, qs * r0 - float2(0.0, G0), qs * r1 - float2(0.0, G1), w, a - s0, a - s1, head, type, col,
                               jt.x * 91.7 + jt.y * 37.3 + sd, a);
            }
        }
    }
    acc = lerp(acc, FW_WHITE * dot(acc, float3(0.33, 0.34, 0.33)), hot * 0.7);          // white-hot as it opens
    return acc * fade;
}

// the programme: shell b of the show - when it bursts, where, how big, what kind
static const int kFwBodyType[12] = {0, 0, 4, 1, 0, 2, 1, 5, 0, 4, 3, 2};
static const int kFwBodyTier[12] = {0, 1, 1, 2, 0, 2, 1, 0, 1, 2, 0, 2};
// when shell b goes up, give or take its small random delay (cheap: used to skip the shells that are not burning)
float fwT0(int b) {
    return b < 24 ? 0.55 + float(b >> 1) * 0.56 : (b < 47 ? 7.35 + float(b - 24) * FW_FIN_DT : 10.95 + float(b - 47) * 0.035);
}
// its burst time, kind and size (no direction: the light of the show needs no more)
void fwShellKind(int b, float seed, out float tb, out float R, out int type, out int tier) {
    float sb = seed * 0.0013 + float(b) * 1.731;
    int   pr = b >> 1;
    float sp = seed * 0.0013 + float(pr) * 3.117;
    float t0 = fwT0(b);
    if (b < 24) {
        type = kFwBodyType[pr];
        tier = kFwBodyTier[pr];
        t0 += fwH(sp) * 0.16 + ((b & 1) ? 0.06 : 0.0);
    } else if (b < 47) {                                        // the finale: one after another, all over
        float hh = fwH(sb + 5.0);
        type = hh < 0.50 ? 2 : (hh < 0.80 ? 0 : (hh < 0.86 ? 3 : (hh < 0.95 ? 1 : 5)));
        tier = type == 2 ? (fwH(sb + 6.0) < 0.6 ? 2 : 1) : (type == 3 ? 0 : (fwH(sb + 6.0) < 0.5 ? 1 : 0));
        t0 += fwH(sb) * 0.05;
    } else {                                                    // and the last five crowns together, wall to wall
        type = 2; tier = 2;
    }
    float hv = fwH(sb + 3.0);
    R = tier == 0 ? 0.105 + 0.04 * hv : (tier == 1 ? 0.17 + 0.05 * hv : 0.25 + 0.07 * hv);
    if (type == 2) R *= 1.15;
    if (b >= 47) R = 0.34;
    tb = t0 + 0.55;                                             // it climbs for 0.55 s first
}
// where it bursts
float3 fwShellDir(int b, float seed, float yaw, int tier) {
    float sb = seed * 0.0013 + float(b) * 1.731;
    float sp = seed * 0.0013 + float(b >> 1) * 3.117;
    float side = (b & 1) ? 1.0 : -1.0;                          // pairs go up left and right of the centre line together
    float az = b < 24 ? side * (0.15 + (tier == 2 ? 0.34 : 0.62) * fwH(sp + 1.0))
             : (b < 47 ? (fwH(sb + 2.0) - 0.5) * 2.5 : (float(b - 47) - 2.0) * 0.36 + (fwH(sb + 2.0) - 0.5) * 0.08);
    float hv = fwH(sb + 3.0);
    float el = tier == 0 ? 0.13 + 0.05 * hv : (tier == 1 ? 0.25 + 0.09 * hv : 0.40 + 0.10 * hv);
    if (b >= 47) el = 0.50 + 0.05 * hv;
    az += yaw;
    return float3(cos(az) * cos(el), sin(el), sin(az) * cos(el));
}
float3 fwColour(int type, int b, float3 team) {
    if (type == 2 || type == 5 || type == 6) return FW_GOLD;
    if (type == 3) return FW_WHITE;
    // the winner's colour, and its two neighbours on the wheel so the sky is not one flat hue
    int v = b % 4;
    float3 c = v == 1 ? lerp(team, team.gbr, 0.26) : (v == 3 ? lerp(team, team.brg, 0.26) : team);
    return c / max(max(c.r, max(c.g, c.b)), 0.05) * 1.15;
}
// how much light a shell gives off, a seconds after its burst
float fwGlow(float a, int type) {
    if (a <= 0.0) return 0.0;
    float life = kFwLife[type];
    return 2.4 * exp(-a * 5.5) + (type == 2 ? 0.62 : 0.42) * smoothstep(life, life * 0.5, a) * smoothstep(0.0, 0.25, a);
}

// the light of the whole show at this moment (for the islands, the smoke): the sum of every burning shell
float3 fwLight(float age, float3 team, float seed) {
    float3 L = 0.0;
    [loop] for (int b = 0; b < FW_SHELLS; b++) {
        float a0 = age - fwT0(b) - 0.55;
        if (a0 < -0.25 || a0 > 4.8) continue;                    // not burning: skip it before any real work
        float tb, R; int type, tier;
        fwShellKind(b, seed, tb, R, type, tier);
        float a = age - tb;
        if (a <= 0.0) continue;
        L += fwColour(type, b, team) * fwGlow(a, type) * (R / 0.2);
    }
    float as0 = age - 8.1;
    if (as0 > 0.0 && as0 < 3.6) {
        [loop] for (int s = 0; s < 12; s++) {                    // the salutes
            float as = as0 - (float(s) * 0.24 + fwH(seed * 0.0017 + float(s) * 2.3) * 0.1);
            if (as > 0.0 && as < 0.6) L += FW_WHITE * 3.0 * exp(-as * 12.0);
        }
    }
    return L;
}

// a comet off the horizon (the opening fan, the fan that starts the finale): gold, glittering tail.
// p: this pixel as (azimuth from the show's centre line, elevation), radians
float3 fwComets(float2 p, float age, float seed) {
    float3 acc = 0.0;
    if (p.y < -0.03 || p.y > 0.72 || abs(p.x) > 1.5) return acc;
    float w = max(1.7 * 2.0 * c0.w / c4.x, 0.0017);
    [loop] for (int f = 0; f < 2; f++) {
        float af = age - (f == 0 ? 0.0 : 6.75);
        if (af < 0.0 || af > 1.9) continue;
        [loop] for (int i = 0; i < 11; i++) {
            float fi = float(i) - 5.0;
            float ai = af - abs(fi) * (f == 0 ? 0.045 : 0.03);              // the middle one first, then outwards
            if (ai <= 0.0 || ai > 1.55) continue;
            float x0 = fi * (f == 0 ? 0.15 : 0.20);
            float th = fi * (f == 0 ? 0.105 : -0.115) + (fwH(seed * 0.003 + float(i + f * 11)) - 0.5) * 0.04;   // fanning out / crossing
            float H = 0.46 - abs(fi) * 0.012;
            if (abs(p.x - x0 - sin(th) * H * 0.5) > 0.36) continue;
            float2 dv = float2(sin(th), cos(th));
            float id = float(i + f * 11) + seed * 0.01;
            float2 prev = float2(0.0, 0.0);
            [loop] for (int m = 0; m <= 10; m++) {
                float sm = ai - float(m) * 0.085;                            // newest first
                float u = max(sm, 0.0);
                float2 pos = float2(x0, -0.01) + dv * H * (1.0 - exp(-u * 3.0)) / 0.95 - float2(0.0, 0.045 * u * u);
                if (m > 0) {
                    float2 sg = prev - pos, ap = p - pos;                    // pos older, prev newer
                    float  tu = dot(ap, sg) / max(dot(sg, sg), 1e-9);
                    if (tu >= 0.0 && tu < 1.0) {
                        float2 d = ap - sg * tu;
                        float  del = (float(m) - tu) * 0.085;
                        float2 gq = floor(p / (w * 1.15));
                        float  gl = step(0.70, fwH(dot(gq, float2(12.99, 78.23)) + id * 7.13 + floor(TIME * 19.0) * 0.731));
                        float  ww = w * (1.0 + del * 3.0);                   // the tail spreads
                        float  I = exp(-dot(d, d) / (ww * ww)) * pow(saturate(1.0 - del / 0.85), 1.2) * lerp(1.0, 0.5 + 2.2 * gl, smoothstep(0.03, 0.14, del));
                        acc += lerp(FW_GOLD, FW_EMBER, saturate(del / 0.85)) * I;
                    }
                } else {
                    float2 dh = p - pos;
                    acc += lerp(FW_GOLD, FW_WHITE, 0.5) * (exp(-dot(dh, dh) / (w * w * 2.5)) + 0.06 * exp(-dot(dh, dh) / (w * w * 14.0))) * 1.5;
                }
                prev = pos;
            }
        }
    }
    return acc * smoothstep(-0.02, 0.02, p.y);
}

// gerbs: a row of gold fountains on the horizon through the finale. p as in fwComets
float3 fwFountains(float2 p, float age, float seed) {
    float af = age - 6.9;
    if (af < 0.0 || af > 4.6 || p.y < -0.01 || p.y > 0.34 || abs(p.x) > 1.25) return 0.0;
    float3 acc = 0.0;
    float on = smoothstep(0.0, 0.35, af) * (1.0 - smoothstep(3.9, 4.6, af));
    [loop] for (int i = 0; i < 7; i++) {
        float x0 = (float(i) - 3.0) * 0.34;
        float2 d = p - float2(x0, -0.005);
        if (abs(d.x) > 0.15) continue;
        float r = length(d), th = atan2(d.x, d.y);                 // angle off straight up
        float H = 0.30 * on * (0.9 + 0.1 * sin(af * 9.0 + float(i)));
        if (r > H || abs(th) > 0.42) continue;
        // sparks: thin lines of angle, each a train of dashes running outward and curling over
        float ti = floor((th + 0.42) * 110.0);
        float hs = fwH(ti * 1.7 + float(i) * 13.1 + seed * 0.01);
        float run = frac(r / 0.075 - af * (2.2 + hs * 1.6) + hs * 7.0);
        float dash = smoothstep(0.55, 0.9, run) * step(0.35, fwH(ti + floor(r / 0.075 - af * (2.2 + hs * 1.6) + hs * 7.0) * 3.3));
        float ln = exp(-pow((frac((th + 0.42) * 110.0) - 0.5) * 2.4, 2.0) * 3.0);
        float fall = pow(saturate(1.0 - r / H), 0.8) * (1.0 - smoothstep(0.30, 0.42, abs(th)));
        acc += lerp(FW_WHITE, FW_GOLD, saturate(r / H * 1.6)) * dash * ln * fall * 2.2;
        acc += FW_GOLD * exp(-r * r / (0.012 * 0.012)) * 1.2 * on;   // the hot throat
    }
    return acc;
}

// the show over a sky pixel. lin: the sky behind (linear). Returns the sky with smoke, glow and stars
float3 fwSky(float3 lin, float3 rd, float age, float3 team, float seed, float yaw, float quality, float gain) {
    if (age >= FW_LEN) return lin;
    float  pxA = 2.0 * c0.w / c4.x;
    float  wAng = max(1.55 * pxA, 0.0015);
    int    maxPieces = quality > 0.6 ? 10 : (quality > 0.25 ? 7 : 5);
    bool   lean = quality < 0.25;                              // the frame rate is hurting: stars only
    float3 stars = 0.0, glow = 0.0;
    float  smoke = 0.0;
    bool   below = rd.y < 0.0;                                   // the cloud sea: it only takes the glow, mirrored
    float3 rv = below ? float3(rd.x, -rd.y, rd.z) : rd;
    int nS = int(m4.x);
    [loop] for (int k = 0; k < nS; k++) {                       // only the shells in the air (the CPU set them up this frame)
        float4 s0 = fwS[k * 3], s1 = fwS[k * 3 + 1], s2 = fwS[k * 3 + 2];
        float3 dir = s0.xyz;
        float cd = dot(rv, dir);
        if (cd < 0.2) continue;
        float  R = s0.w, a = s2.x, bh = s2.w;
        int    type = int(s2.y + 0.5);
        float3 ex = normalize(float3(dir.z, 0.0, -dir.x)), ey = cross(dir, ex);
        float3 pr = rv / cd - dir;
        float2 pa = float2(dot(pr, ex), dot(pr, ey));           // radians-ish around the burst point
        float2 pq = pa / R;
        if (dot(pq, pq) > 40.0 && a > 0.0) {                     // far outside it: only its faint light (in the air / on the sea)
            glow += s1.rgb * s1.w * (below ? exp(-dot(pq, pq) / 22.0) * 0.016 * (R / 0.2) : exp(-dot(pq, pq) / 14.0) * 0.0015);
            continue;
        }
        float3 col = s1.rgb;
        float  gI = s1.w;
        if (below) {                                             // light on the cloud sea under it
            glow += col * gI * exp(-dot(pa, pa) / (R * R * 22.0)) * 0.016 * (R / 0.2);
            continue;
        }
        glow += col * gI * (exp(-dot(pa, pa) / (R * R * 2.2)) * 0.008 + exp(-dot(pa, pa) / (R * R * 14.0)) * 0.0015);   // the air around it lights up
        if (a < 0.0) {                                           // the climb: a spark on a short gold tail, slowing as it tops out
            float u = (a + 0.55) / 0.55, d0 = (1.0 - u) * (1.0 - u) * dir.y * 0.9;
            float2 hd = float2(sin(u * 22.0 + bh * 50.0) * 0.004 * (1.0 - u), -d0), tl = float2(0.0, -d0 - 0.035 * (1.0 - u) - 0.006);
            float2 sg = hd - tl, ap = pa - tl;
            float  tt = saturate(dot(ap, sg) / max(dot(sg, sg), 1e-9));
            float2 dv = ap - sg * tt;
            stars += FW_GOLD * exp(-dot(dv, dv) / (wAng * wAng)) * tt * tt * 1.2;
            continue;
        }
        if (a > 0.15 && !lean) {                                 // its smoke: drifts, thins, takes the light of what bursts next
            float2 ps = pa - float2(0.012, -0.004) * a;
            float  rs = R * (0.50 + 0.16 * a);
            float  dn = exp(-dot(ps, ps) / (rs * rs));
            if (dn > 0.01) {
                float n = gNoise.SampleLevel(gSmp, ps * (1.4 / R) * 0.11 + float2(bh, a * 0.012), 0).r;
                smoke += dn * (0.35 + 1.1 * n) * smoothstep(0.15, 0.7, a) * (1.0 - smoothstep(kFwLife[type], kFwLife[type] + 3.0, a)) * 0.20;
            }
        }
        if (a < kFwLife[type]) {
            float2 P = pa / R;
            stars += fwShellDraw(P, a, type, col, s2.z, wAng / R, maxPieces) * (type == 2 ? 0.62 : 1.5);
            if (type == 0 && R > 0.16 && !lean) stars += fwShellDraw(P / 0.42, a, 6, FW_GOLD, s2.z * 1.1818 + 3.1, wAng / (R * 0.42), maxPieces);
            stars += lerp(col, FW_WHITE, 0.75) * exp(-dot(pa, pa) / (R * R * 0.010)) * exp(-a * 11.0) * 1.6;   // the pop
        }
    }
    // the comet fans and the salutes sit low, on the centre line
    float azp = atan2(rd.z, rd.x) - yaw;
    azp -= 6.2831853 * floor(azp / 6.2831853 + 0.5);
    float2 pe = float2(azp * cos(asin(saturate(abs(rd.y)))), asin(clamp(rd.y, -1.0, 1.0)));
    if (!below) {
        stars += fwComets(pe, age, seed);
        stars += fwFountains(pe, age, seed);
        [loop] for (int s = 0; s < 12 && age > 8.1 && age < 12.5 && pe.y < 0.5; s++) {
            float hs = seed * 0.0017 + float(s) * 2.3;
            float as = age - (8.1 + float(s) * 0.24 + fwH(hs) * 0.1);
            if (as <= 0.0 || as > 1.3) continue;
            float2 c = float2((fwH(hs + 1.0) - 0.5) * 1.9, 0.11 + 0.22 * fwH(hs + 2.0));
            float2 d = pe - c;
            float  r2 = dot(d, d);
            if (r2 > 0.03) continue;
            float  r = sqrt(r2), th = atan2(d.y, d.x);
            float  fl = exp(-as * 13.0);
            float  rays = pow(abs(cos(th * 4.0 + hs)), 24.0) * exp(-r / 0.022) + pow(abs(cos(th * 4.0 + hs + 0.39)), 40.0) * exp(-r / 0.012) * 0.6;
            stars += FW_WHITE * fl * (exp(-r2 / (0.0045 * 0.0045)) * 5.0 + rays * 2.4 + exp(-r2 / (0.05 * 0.05)) * 0.35);
            // what a salute leaves: a cloud of crackle, tiny sparks snapping on and off
            float2 g = floor(d / (wAng * 3.2));
            float  on = step(0.955, fwH(dot(g, float2(12.99, 78.23)) + floor(TIME * 20.0) * 0.37 + hs));
            float2 gc = (g + 0.5) * (wAng * 3.2) - d;
            stars += lerp(FW_GOLD, FW_WHITE, 0.6) * on * exp(-dot(gc, gc) / (wAng * wAng * 1.3)) * exp(-r2 / (0.075 * 0.075)) * smoothstep(0.05, 0.2, as) * (1.0 - smoothstep(0.6, 1.3, as)) * 2.6;
        }
    }
    float end = 1.0 - smoothstep(FW_LEN - 1.5, FW_LEN, age);
    if (below) return 1.0 - (1.0 - saturate(lin)) * exp(-glow * end * gain * smoothstep(0.0, -0.10, rd.y));   // none at the horizon line, most where the sea is under the burst
    // smoke first (it hides a little of the sky and carries the show's light), then the glow in the air, then the stars
    float  sm = saturate(smoke);
    float3 smokeCol = float3(0.032, 0.030, 0.042) + glow * 0.7;
    lin = lerp(lin, smokeCol, sm * 0.5);
    // light adds up like light on film: a spark is its own colour, a pile of them goes pale, nothing clips flat
    return 1.0 - (1.0 - saturate(lin)) * exp(-(glow + stars * 0.9) * end * gain);
}

// the show's light on the islands and everything on them (lin: the pixel, linear)
// L: the show's light this frame (fwLight, worked out once per frame by Renderer Foundation - SkyPass fwcpu::light)
float3 fwWorld(float3 lin, float age, float3 L) {
    if (age >= FW_LEN) return lin;
    return lin * (1.0 + L * 0.034) + L * 0.0015;
}
