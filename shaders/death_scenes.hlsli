// ---- death scenes: seven kill animations ported from LyricsFlow's death animations (by 374m, used with the author's
//      permission; Bedrock / Renderer Foundation port by Novak). Each kill plays one, dealt from a shuffle bag so the
//      same one never plays twice in a row: Chains of the Deep, Blackjack, Tentacles, Lightning Ash, Giant Hand,
//      Hungry Chest, Executioner. Drawn by ray-tracing boxes and capsules in the World FX pass, depth-tested against
//      the world. The victim is a stand-in body in their team colours (the real player is already gone).
//      Event: eA = feet block (world, min) + age, eB = team colour + kind (3 kill, 4 final), eC.x = seed, eC.w = scene.

struct DHit { float t; float3 c; bool hit; };

float3 dsLit(float3 alb, float3 nw) {
    float  ndl = saturate(dot(nw, SUN));
    float3 amb = lerp(float3(0.40, 0.37, 0.52), float3(0.22, 0.28, 0.55), NIGHT) * (0.75 + 0.25 * nw.y);
    float3 sunC = lerp(float3(1.05, 0.74, 0.52), float3(0.70, 0.80, 1.05), NIGHT) * LIGHTK;
    float3 world = lerp(lerp(float3(1, 1, 1), float3(0.86, 0.78, 0.94), DUSK), float3(0.40, 0.47, 0.76), NIGHT);
    return alb * (amb + sunC * ndl) * world;
}
float3x3 rotY(float a) { float c = cos(a), s = sin(a); return float3x3(c, 0, s, 0, 1, 0, -s, 0, c); }
float3x3 rotX(float a) { float c = cos(a), s = sin(a); return float3x3(1, 0, 0, 0, c, -s, 0, s, c); }
float3x3 rotZ(float a) { float c = cos(a), s = sin(a); return float3x3(c, -s, 0, s, c, 0, 0, 0, 1); }
float3x3 basisOf(float3 T, float spin) {   // columns: a frame whose y axis runs along T
    float3 up = abs(T.y) < 0.95 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 b1 = normalize(cross(up, T)), b3 = cross(b1, T);
    float c = cos(spin), s = sin(spin);
    float3 u = b1 * c + b3 * s, w = -b1 * s + b3 * c;
    return float3x3(u.x, T.x, w.x, u.y, T.y, w.y, u.z, T.z, w.z);
}
// a box (camera-relative centre p, half size e, axes R): lit albedo + optional edge glow
void dsBox(inout DHit h, float3 rd, float3 p, float3 e, float3x3 R, float3 alb, float3 glow) {
    float tN; float3 nl, lp;
    if (!hitBox(rd, p, e, R, tN, nl, lp) || tN >= h.t) return;
    float3 nw = mul(R, nl);
    float  edge = smoothstep(0.70, 1.0, max(max(abs(lp.x) * (1 - abs(nl.x)), abs(lp.y) * (1 - abs(nl.y))), abs(lp.z) * (1 - abs(nl.z))));
    h.t = tN; h.c = dsLit(alb, nw) + glow * edge; h.hit = true;
}
void dsCap(inout DHit h, float3 rd, float3 a, float3 b, float r, float3 alb, float3 rim, float emis) {
    float t = figCap(float3(0, 0, 0), rd, a, b, r);
    if (t <= 0.05 || t >= h.t) return;
    float3 nw = figCapN(rd * t, a, b, r);
    float  fr = pow(1.0 - saturate(dot(-rd, nw)), 2.5);
    h.t = t; h.c = dsLit(alb, nw) + rim * fr + alb * emis; h.hit = true;
}
// the victim: a Minecraft body (feet origin, facing +z) placed by R about the feet, moved by off, scaled by s.
// ash > 0 crumbles it away (texel noise), char darkens it with embers. headOff / headR move the head alone
void dsBody(inout DHit h, float3 rd, float3 feet, float3 off, float3x3 R, float s, float3 team, float ash, float charK,
            float3 headOff, float3x3 headR, float lean) {
    if (s < 0.01) return;
    float3 skin = float3(0.78, 0.58, 0.44), shirt = team * 0.85, pants = team * 0.35 + 0.04, sleeve = team * 0.7;
    [unroll] for (int j = 0; j < 6; j++) {
        float3 c = j == 0 ? float3(0, 1.75, 0) : (j == 1 ? float3(0, 1.125, 0) : (j == 2 ? float3(-0.375, 1.125, 0) :
                   (j == 3 ? float3(0.375, 1.125, 0) : (j == 4 ? float3(-0.125, 0.375, 0) : float3(0.125, 0.375, 0)))));
        float3 e = j == 0 ? float3(0.25, 0.25, 0.25) : (j == 1 ? float3(0.25, 0.375, 0.125) : float3(0.125, 0.375, 0.125));
        float3 alb = j == 0 ? skin : (j == 1 ? shirt : (j < 4 ? sleeve : pants));
        float3x3 Rp = R;
        float3 pc = c;
        if (j == 0 && headOff.y < -30.0) continue;   // (the executioner draws the head on its own)
        if (j == 0) { pc = c + headOff; Rp = mul(R, headR); }
        if (j > 0 && j < 4) pc.z += lean;
        float3 p = feet + off + mul(R, pc * s);
        float tN; float3 nl, lp;
        if (!hitBox(rd, p, e * s, Rp, tN, nl, lp) || tN >= h.t) continue;
        float3 cell = floor((lp * 0.5 + 0.5) * e * 32.0);
        float  n = hash12(cell.xy * 1.3 + cell.z * 7.1 + float(j) * 3.7);
        float  hgt = (p.y - feet.y - off.y) / max(2.0 * s, 0.01);          // 0 feet .. 1 head
        if (ash > 0.0 && n < ash * 1.4 - (1.0 - hgt) * 0.4) continue;       // crumbling, from the top down
        float3 nw = mul(Rp, nl);
        float3 a = alb * (0.85 + 0.25 * n);
        if (j == 0 && nl.z > 0.5 && abs(lp.y + 0.05) < 0.18 && abs(abs(lp.x) - 0.38) < 0.14) a = float3(0.08, 0.08, 0.1); // eyes
        float3 c2 = dsLit(lerp(a, float3(0.035, 0.03, 0.03), charK), nw);
        c2 += float3(1.0, 0.38, 0.08) * charK * step(0.86, n) * (0.6 + 0.4 * sin(TIME * 9.0 + n * 20.0)) * 1.5;
        h.t = tN; h.c = c2; h.hit = true;
    }
}

float4 deathScenes(float3 rd, float sceneT) {
    DHit h; h.t = sceneT; h.c = 0.0; h.hit = false;
    int ne = int(m0.y);
    [loop] for (int i = 0; i < ne; i++) {
        float kind = eB[i].w, t = eA[i].w;
        if (kind < 2.5 || kind > 4.5 || t > 4.0) continue;
        float3 feet = eA[i].xyz + float3(0.5, 0.0, 0.5) - CAM;
        float  tb = dot(feet + float3(0, 1, 0), rd);
        float  rb = 4.5;
        if (eC[i].w > 3.5 && eC[i].w < 4.5) rb = 40.0;                       // the giant hand comes from high up
        if (eC[i].w > 2.5 && eC[i].w < 3.5) rb = 45.0;                       // the lightning comes from the sky
        float3 cc = feet + float3(0, 1, 0);
        if (tb < -rb || dot(cc, cc) - tb * tb > rb * rb) continue;
        int    sc = int(eC[i].w + 0.5);
        float  seed = eC[i].x;
        float3 team = pow(max(eB[i].rgb, 0.0), 2.2);
        team = team / max(max(team.r, max(team.g, team.b)), 0.05);
        float3 glow = team * (1.2 + 0.8 * darkK());
        float  yaw = h1(seed * 0.013) * 6.2832;
        float3x3 RY = rotY(yaw);
        float3x3 I3 = float3x3(1, 0, 0, 0, 1, 0, 0, 0, 1);
        if (sc == 0) {
            // CHAINS OF THE DEEP: four chains burst from the ground, spear into the body, and drag it under
            float sink = -2.8 * pow(smoothstep(0.55, 2.3, t), 1.6);
            float shake = sin(t * 70.0) * 0.03 * smoothstep(0.3, 0.5, t) * (1.0 - smoothstep(2.0, 2.4, t));
            dsBody(h, rd, feet, float3(shake, sink, 0), RY, 1.0, team, 0.0, 0.0, 0.0, I3, 0.0);
            [loop] for (int k = 0; k < 4; k++) {
                float  a = float(k) * 1.5708 + 0.6 + yaw;
                float3 anc = feet + float3(cos(a) * 1.8, -0.1, sin(a) * 1.8);
                float3 att = feet + float3(cos(a) * 0.18, 1.15 + sink + 0.12 * float(k & 1), sin(a) * 0.18);
                float  g = saturate((t - 0.04 * float(k)) / 0.28);
                [loop] for (int l = 0; l < 14; l++) {
                    float s0 = (float(l) + 0.5) / 14.0;
                    if (s0 > g) break;
                    float  sag = sin(s0 * 3.1416) * (0.55 - 0.45 * smoothstep(0.5, 1.0, t));
                    float3 P = lerp(anc, att, s0) + float3(0, sag, 0);
                    float3 P2 = lerp(anc, att, s0 + 0.02) + float3(0, sin((s0 + 0.02) * 3.1416) * (0.55 - 0.45 * smoothstep(0.5, 1.0, t)), 0);
                    float3x3 R = basisOf(normalize(P2 - P), (l & 1) ? 1.5708 : 0.0);
                    bool tip = (l == 13) || (s0 + 1.0 / 14.0 > g && g < 1.0);
                    if (tip) dsBox(h, rd, P, float3(0.06, 0.16, 0.06), mul(R, rotY(0.785)), float3(0.25, 0.25, 0.27), glow * 2.0);
                    else     dsBox(h, rd, P, float3(0.075, 0.105, 0.022), R, float3(0.16, 0.15, 0.16) + 0.04 * h1(float(l)), glow * 0.8);
                }
            }
        } else if (sc == 1) {
            // BLACKJACK: cards deal themselves round the body, close in, and the player is folded into one card that
            // flips and is flicked away into the sky
            float  gone = smoothstep(1.0, 1.25, t);
            dsBody(h, rd, feet, float3(0, 0, 0), RY, 1.0 - gone, team, 0.0, 0.0, 0.0, I3, 0.0);
            [loop] for (int k = 0; k < 6; k++) {
                float  a = float(k) * 1.0472 + t * (4.0 + 3.0 * smoothstep(0.6, 1.0, t)) + yaw;
                float  deal = saturate((t - 0.06 * float(k)) / 0.25);
                float  r = lerp(0.2, 1.25, deal) * (1.0 - smoothstep(0.75, 1.15, t));
                if (deal <= 0.0 || t > 1.2) continue;
                float3 p = feet + float3(cos(a) * r, 0.9 + 0.25 * sin(t * 6.0 + float(k)), sin(a) * r);
                float3x3 R = mul(rotY(-a + 1.5708), rotX(0.15 * sin(t * 9.0 + float(k))));
                float tN; float3 nl, lp;
                if (hitBox(rd, p, float3(0.24, 0.34, 0.012), R, tN, nl, lp) && tN < h.t) {
                    float2 uv = lp.xy;
                    bool   face = nl.z > 0.0;
                    float  border = step(0.86, max(abs(uv.x), abs(uv.y)));
                    float  suit = step(abs(uv.x) * 1.4 + abs(uv.y), 0.32);
                    float3 red = (k & 1) ? float3(0.75, 0.06, 0.06) : float3(0.05, 0.05, 0.06);
                    float3 alb = face ? lerp(lerp(float3(0.93, 0.92, 0.88), red, suit), red, border * 0.6)
                                      : lerp(float3(0.55, 0.05, 0.07), float3(0.85, 0.75, 0.6), step(0.5, frac((uv.x + uv.y) * 5.0)) * 0.35 + border * 0.6);
                    h.t = tN; h.c = dsLit(alb, mul(R, nl)) + glow * 0.15 * border; h.hit = true;
                }
            }
            if (t > 1.0) {   // the player, as a card
                float u = t - 1.0;
                float3 p = feet + float3(0, 1.0 + 1.2 * u * u + 0.4 * u, 0);
                float  sc2 = smoothstep(0.0, 0.2, u) * (1.0 - smoothstep(2.4, 2.9, u + 1.0));
                float3x3 R = mul(RY, mul(rotY(u * 9.0), rotZ(sin(u * 3.0) * 0.4)));
                float tN; float3 nl, lp;
                if (sc2 > 0.01 && hitBox(rd, p, float3(0.42, 0.6, 0.015) * sc2, R, tN, nl, lp) && tN < h.t) {
                    float2 uv = lp.xy;
                    float  border = step(0.88, max(abs(uv.x), abs(uv.y)));
                    // the face: the player's team colour as a figure (head over body) on white; the back: red lattice
                    float  fig = step(length(uv - float2(0, 0.42)), 0.2) + step(abs(uv.x), 0.25) * step(abs(uv.y + 0.15), 0.35);
                    float3 alb = nl.z > 0.0 ? lerp(lerp(float3(0.94, 0.93, 0.9), team * 0.8, saturate(fig)), float3(0.1, 0.1, 0.1), border * 0.7)
                                            : lerp(float3(0.6, 0.05, 0.08), float3(0.9, 0.8, 0.6), step(0.5, frac((uv.x - uv.y) * 6.0)) * 0.3 + border * 0.6);
                    h.t = tN; h.c = dsLit(alb, mul(R, nl)) + glow * border * 0.6 * exp(-u * 1.5); h.hit = true;
                }
            }
        } else if (sc == 2) {
            // TENTACLES: five tentacles rise round the player, coil around them, and pull them down into the ground
            float wrap = smoothstep(0.25, 0.85, t);
            float sink = -2.6 * pow(smoothstep(0.95, 2.5, t), 1.5);
            dsBody(h, rd, feet, float3(0, sink, 0), RY, 1.0, team, 0.0, 0.0, 0.0, I3, sin(t * 30.0) * 0.03 * wrap);
            [loop] for (int k = 0; k < 5; k++) {
                float  a0 = float(k) * 1.2566 + yaw + 0.3 * h1(seed + float(k));
                float  grow = saturate((t - 0.05 * float(k)) / 0.45);
                if (grow <= 0.0) continue;
                float3 prev = 0.0;
                [loop] for (int l = 0; l <= 12; l++) {
                    float  s0 = float(l) / 12.0;
                    float  hgt = s0 * 2.3 * grow;
                    float  ang = a0 + s0 * 4.5 * wrap + sin(TIME * 5.0 + s0 * 7.0 + float(k)) * 0.15;
                    float  rr = lerp(1.35, lerp(1.35 - s0 * 0.6, 0.36, smoothstep(0.1, 0.5, s0)), wrap);
                    float  pull = sink * smoothstep(0.15, 0.6, s0);
                    float3 P = feet + float3(cos(ang) * rr, hgt - 0.2 + pull, sin(ang) * rr);
                    if (l > 0) {
                        float r = lerp(0.17, 0.035, s0);
                        dsCap(h, rd, prev, P, r, float3(0.10, 0.035, 0.12) + 0.05 * step(0.5, frac(s0 * 9.0)), glow * 0.9, 0.0);
                    }
                    prev = P;
                }
            }
        } else if (sc == 3) {
            // LIGHTNING ASH: a bolt splits the sky and strikes them, they char black with embers, then crumble to ash
            // that the wind carries away
            float charK = smoothstep(0.08, 0.2, t);
            float ash = smoothstep(0.9, 2.3, t);
            dsBody(h, rd, feet, float3(sin(t * 80.0) * 0.02 * (1.0 - charK * 0.5), 0, 0), RY, 1.0, team, ash, charK, 0.0, I3, 0.0);
            if (t > 0.05 && t < 0.55) {
                float flick = step(0.35, frac(t * 23.0 + 0.2)) + step(t, 0.18);
                if (flick > 0.0) {
                    float3 prev = feet + float3(0, 42, 0);
                    [loop] for (int l = 1; l <= 12; l++) {
                        float  s0 = float(l) / 12.0;
                        float3 P = lerp(feet + float3(0, 42, 0), feet + float3(0, 1.9, 0), s0);
                        P.xz += (float2(h1(seed + float(l)), h1(seed + float(l) * 3.1)) - 0.5) * 3.0 * (1.0 - s0 * s0) * (l < 12 ? 1.0 : 0.0);
                        dsCap(h, rd, prev, P, 0.07 + 0.05 * (1.0 - s0), lerp(glow, float3(1, 1, 1), 0.75), 0.0, 6.0);
                        prev = P;
                    }
                }
            }
            if (t > 0.9) {   // ash, blown away
                [loop] for (int k = 0; k < 28; k++) {
                    float  sk = seed * 0.01 + float(k) * 3.3;
                    float3 st = float3((h1(sk) - 0.5) * 0.5, h1(sk + 1) * 1.9, (h1(sk + 2) - 0.5) * 0.3);
                    float  born = 0.9 + 1.3 * (1.0 - st.y / 1.9);
                    float  u = t - born;
                    if (u < 0.0 || u > 1.6) continue;
                    float3 wind = float3(0.9, 0.35, 0.4) + (float3(h1(sk + 3), h1(sk + 4), h1(sk + 5)) - 0.5) * 0.6;
                    float3 p = feet + mul(RY, st) + wind * u * (1.2 + u);
                    float  e = 0.055 * (1.0 - u / 1.6);
                    dsBox(h, rd, p, float3(e, e, e), rotAxis(normalize(wind), u * 6.0), float3(0.08, 0.075, 0.07), float3(1.0, 0.4, 0.1) * exp(-u * 3.0));
                }
            }
        } else if (sc == 4) {
            // GIANT HAND: a huge blocky hand reaches down out of the sky, closes round them, and carries them up
            float down = 1.0 - smoothstep(0.0, 0.6, t);
            float grip = smoothstep(0.55, 0.85, t);
            float lift = 34.0 * pow(smoothstep(1.05, 2.9, t), 2.0);
            float yh = 2.35 + 14.0 * down * down + lift;
            float carry = lift + 0.15 * grip;
            dsBody(h, rd, feet, float3(0, carry, 0), RY, 1.0, team, 0.0, 0.0, 0.0, I3, 0.0);
            float3 skin = float3(0.80, 0.60, 0.46);
            float3 pc = feet + float3(0, yh, 0);
            dsBox(h, rd, pc, float3(0.85, 0.22, 0.75), RY, skin * 0.95, 0.0);                // the palm
            dsBox(h, rd, pc + mul(RY, float3(0, 0.2, -0.95)), float3(0.6, 0.35, 0.45), RY, skin * 0.9, 0.0); // the wrist
            [loop] for (int k = 0; k < 6; k++) {    // four fingers at the front edge, two (a thumb) at the back
                bool   back = k >= 4;
                float  fx = back ? (k == 4 ? -0.35 : 0.35) : (-0.63 + 0.42 * float(k));
                float  fz = back ? -0.68 : 0.68;
                float  curl = (0.15 + 1.35 * grip) * (back ? -1.0 : 1.0);
                float3 root = float3(fx, -0.15, fz);
                float3x3 Rf = mul(RY, rotX(curl));
                float3 seg1 = root + mul(rotX(curl), float3(0, -0.42, 0));
                dsBox(h, rd, pc + mul(RY, seg1), float3(0.17, 0.42, 0.17), Rf, skin, 0.0);
                float3x3 Rf2 = mul(RY, rotX(curl * 1.6));
                float3 seg2 = root + mul(rotX(curl), float3(0, -0.84, 0)) + mul(rotX(curl * 1.6), float3(0, -0.36, 0));
                dsBox(h, rd, pc + mul(RY, seg2), float3(0.16, 0.36, 0.16), Rf2, skin * 0.93, 0.0);
                float3 tip = root + mul(rotX(curl), float3(0, -0.84, 0)) + mul(rotX(curl * 1.6), float3(0, -0.68, 0));
                dsBox(h, rd, pc + mul(RY, tip), float3(0.12, 0.05, 0.12), Rf2, float3(0.93, 0.85, 0.8), glow * 0.3);
            }
        } else if (sc == 5) {
            // HUNGRY CHEST: a chest bursts up out of the ground, gapes its teeth, swallows them, slams, burps, sinks
            float rise = smoothstep(0.0, 0.25, t) - smoothstep(2.1, 2.7, t);
            float open = smoothstep(0.25, 0.42, t) * (1.0 - smoothstep(1.0, 1.1, t));
            float hop = (t > 1.2 && t < 1.55) ? sin((t - 1.2) / 0.35 * 3.1416) * 0.35 : 0.0;
            float3 cpos = feet + mul(RY, float3(0, -0.95 + 0.95 * rise + hop, 1.2));
            float3 mouth = cpos + float3(0, 0.75, 0);
            float  suck = smoothstep(0.42, 1.0, t);
            float3 off = (mouth - feet - float3(0, 0.6, 0)) * suck * suck;
            float3x3 Rb = mul(RY, rotX(-1.2 * suck));
            dsBody(h, rd, feet, off, Rb, 1.0 - 0.92 * suck, team, 0.0, 0.0, 0.0, I3, 0.0);
            float3 wood = float3(0.55, 0.34, 0.15), band = float3(0.25, 0.15, 0.07);
            dsBox(h, rd, cpos + float3(0, 0.31, 0), float3(0.45, 0.31, 0.45), RY, wood, 0.0);          // base
            float3 hinge = cpos + mul(RY, float3(0, 0.62, -0.45));
            float3x3 RL = mul(RY, rotX(-1.9 * open));
            dsBox(h, rd, hinge + mul(RL, float3(0, 0.12, 0.45)), float3(0.45, 0.12, 0.45), RL, wood * 1.05, 0.0); // lid
            dsBox(h, rd, hinge + mul(RL, float3(0, 0.06, 0.92)), float3(0.07, 0.1, 0.03), RL, float3(0.9, 0.75, 0.2), glow * 0.4); // latch
            dsBox(h, rd, cpos + mul(RY, float3(0, 0.5, 0.455)), float3(0.46, 0.035, 0.01), RY, band, 0.0);
            if (open > 0.05) {
                [loop] for (int k = 0; k < 5; k++) {   // its teeth
                    float x = -0.34 + 0.17 * float(k);
                    dsBox(h, rd, cpos + mul(RY, float3(x, 0.66, 0.4)), float3(0.04, 0.06, 0.03), RY, float3(0.95, 0.93, 0.85), 0.0);
                    dsBox(h, rd, hinge + mul(RL, float3(x + 0.085, -0.04, 0.85)), float3(0.04, 0.06, 0.03), RL, float3(0.95, 0.93, 0.85), 0.0);
                }
                dsBox(h, rd, cpos + float3(0, 0.6, 0), float3(0.4, 0.02, 0.4), RY, float3(0.35, 0.03, 0.05), glow * 0.2);  // its throat
            }
        } else {
            // EXECUTIONER: a great axe swings down out of nowhere; the head flies, the body drops
            float swing = saturate(t / 0.42);
            float ang = lerp(-2.6, 0.32, swing * swing);
            float axeGone = smoothstep(1.3, 1.7, t);
            float3 piv = feet + mul(RY, float3(0.0, 3.6 + 3.0 * axeGone, -0.7));
            float3 dir = mul(RY, float3(0, -cos(ang), sin(ang)));
            float3 headEnd = piv + dir * 2.0;
            if (axeGone < 0.99) {
                dsCap(h, rd, piv, headEnd, 0.06, float3(0.35, 0.22, 0.12), 0.0, 0.0);                  // the haft
                float3x3 RA = mul(RY, rotX(ang));
                dsBox(h, rd, headEnd + mul(RA, float3(0, 0.05, 0.22)), float3(0.05, 0.32, 0.26), RA, float3(0.42, 0.43, 0.46), glow * 1.2); // the blade
            }
            float  hit = 0.42;
            float  u = max(t - hit, 0.0);
            float  fall = smoothstep(0.55, 1.0, t);
            float3x3 Rb = mul(RY, rotX(1.45 * fall * fall));
            float3 hv = float3(0.6, 2.8, 1.3);
            float3 hoff = t > hit ? mul(RY, hv * u) + float3(0, -0.5 * 9.0 * u * u, 0) : 0.0;
            float  land = (hv.y + sqrt(hv.y * hv.y + 2.0 * 9.0 * 1.6)) / 9.0;
            if (u > land) { float uu = land; hoff = mul(RY, hv * uu) + float3(0, -0.5 * 9.0 * uu * uu, 0); hoff.y = -1.5; }
            float3x3 HR = rotAxis(normalize(float3(1, 0.3, 0.2)), min(u, land) * 9.0);
            // body without its head (head drawn alone below, flying)
            dsBody(h, rd, feet, float3(0, 0, 0), Rb, 1.0, team, 0.0, 0.0, float3(0, -40.0, 0), I3, 0.0);
            float3 hp = t > hit ? feet + float3(0, 1.75, 0) + hoff : feet + mul(RY, float3(0, 1.75, 0));
            dsBox(h, rd, hp, float3(0.25, 0.25, 0.25), t > hit ? mul(RY, HR) : RY, float3(0.78, 0.58, 0.44), 0.0);
            if (t > hit && t < hit + 0.9) {   // the spray: little red cubes from the neck
                [loop] for (int k = 0; k < 14; k++) {
                    float  sk = seed * 0.02 + float(k) * 1.7;
                    float  ub = u - 0.03 * float(k);
                    if (ub < 0.0) continue;
                    float3 v = mul(RY, float3((h1(sk) - 0.5) * 1.6, 2.2 + 1.5 * h1(sk + 1), (h1(sk + 2) - 0.2) * 1.6));
                    float3 p = feet + mul(Rb, float3(0, 1.5, 0)) + v * ub + float3(0, -0.5 * 12.0 * ub * ub, 0);
                    if (p.y < feet.y) p.y = feet.y + 0.03;
                    dsBox(h, rd, p, float3(0.045, 0.045, 0.045), I3, float3(0.55, 0.02, 0.03), 0.0);
                }
            }
        }
    }
    return h.hit ? float4(pow(max(h.c, 0.0), 1.0 / 2.2), h.t) : float4(0, 0, 0, -1.0);
}
