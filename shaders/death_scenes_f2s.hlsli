// ---- Finishers 2's special finishers (k = 40..48 of death_scenes_f2.hlsli): World Slash, Judgment of Time,
//      Starfall, Void, Dragon Vein Release, Tsunami, Thousand Blades, Anime, Chrono Fracture. In the mod each takes the
//      whole screen with its own camera; here the world-changing part plays inside a bubble round the kill (bubble
//      radius ~5 blocks, fading out when you are standing in it yourself), so the fight around it stays readable.

// how much of this pixel sees through the bubble (centre B, radius R): the world in it, or the sky behind it
float f2Bubble(float3 rd, float3 B, float R) {
    float tc = dot(B, rd);
    if (tc < 0.0) return 0.0;
    float d = sqrt(max(dot(B, B) - tc * tc, 0.0));
    float k = smoothstep(R, R * 0.8, d);
    return k * saturate((length(B) - 2.0) / 3.0);   // you, standing inside it, keep your screen
}
// recolour the world through the bubble: c = the recoloured display colour, k = cover
void f2World(inout float3 dec, inout float decK, float3 c, float k) {
    if (k <= 0.001) return;
    dec = c; decK = max(decK, 1.0 + k);
}
float3 f2Lum3(float3 o) { return dot(o, float3(0.3, 0.55, 0.15)); }

void f2Special(int k, float t, float3 o, float3 rd, float3 feet, float yaw, float view, float seed, float3 team, float3 glow,
               bool onG, float3 S, float gr, inout DHit h, inout float3 add, inout float3 dec, inout float decK, inout DFx fx) {
    float3x3 RY = rotY(yaw);
    float3 fwd = mul(RY, float3(0, 0, 1)), side = mul(RY, float3(1, 0, 0));
    float3 C = feet + float3(0, 0.95, 0);
    float  floorY = feet.y;
    float3 one = float3(1, 1, 1);
    float3 vf = float3(cos(view), 0, sin(view));
    float3 A = float3(-vf.z, 0, vf.x);                    // across the view
    fx.hurt = saturate(1.0 - t * 4.0);
    DFx fg = fx; fg.ghost = 1.0; fg.slot = gKillerSlot; fg.hurt = 0.0;
    float bub = f2Bubble(rd, C, 5.0);

    [branch] if (k == 0) {
        // WORLD SLASH: time stops; a colossal sword of light splits the world, and the mob with it
        float stop = ease(t / 0.3) * (1.0 - ease((t - 3.4) / 0.4));
        f2World(dec, decK, lerp(o, f2Lum3(o) * float3(0.80, 0.86, 1.0) * 0.8, 0.85), bub * stop);
        float3 cutN = normalize(float3(0.0, 1.0, 0.0) + A * 0.28);       // the cut: a plane through the waist, tipped
        float3 P0 = feet + float3(0, 0.85, 0);
        // the sword: a blade of light high over them, then the sweep down through the world
        float sw = ease((t - 0.5) / 0.6), cut = ease((t - 1.3) / 0.18);
        float3 bladeDir = normalize(lerp(float3(0, 1, 0), A, cut * 0.9 + 0.1));
        float3 hilt = P0 - A * 3.0 + float3(0, 7.0 * (1.0 - sw) + 1.0, 0);
        if (sw > 0.0 && t < 1.9) {
            float k2 = sw * (1.0 - ease((t - 1.6) / 0.3));
            add += float3(1.0, 0.95, 0.8) * dsGlowSeg(rd, hilt, hilt + bladeDir * 9.0, 0.14, h.t) * 1.4 * k2;
            add += float3(1.0, 0.85, 0.5) * dsGlowSeg(rd, hilt, hilt + bladeDir * 9.0, 0.5, h.t) * 0.35 * k2;
        }
        // the split: a line of white light where the plane meets the world, all through the bubble
        float lit = exp(-max(t - 1.48, 0.0) * 1.2) * step(1.3, t);
        float sd = dot(S - P0, cutN);
        float near1 = saturate(1.0 - length(S - P0) / 6.0);
        add += float3(1.0, 0.95, 0.85) * exp(-sd * sd / 0.0015) * lit * near1 * 2.0 * cut;
        // the mob, cut at the line: the top slides off along it after time comes back
        float go = ease((t - 2.2) / 0.8);
        if (t < 3.6) {
            dsBody(h, rd, feet, RY, 1.0, -99.0, 14.0, fx);
            dsBody(h, rd, feet + A * go * 0.8 - float3(0, go * go * 0.6, 0), mul(RY, rotZ(go * 0.5)), 1.0, 14.0, 99.0, fx);
        }
        fnPoof(h, rd, C, t - 3.6, seed, 14, 0.08);
        f2Cubes(h, rd, P0, t - 2.2, 14, seed, 1.5, 1.2, 9.0, 1.2, 0.03, hex3(110, 2, 2), hex3(181, 18, 18), floorY, 0.0);
    } else if (k == 1) {
        // JUDGMENT OF TIME: time stops and leaves echoes; a colossal clock counts down, cuts the mob into twelve,
        // and it shatters like glass
        float stop = ease(t / 0.3) * (1.0 - ease((t - 3.5) / 0.4));
        f2World(dec, decK, lerp(o, f2Lum3(o) * float3(1.0, 0.9, 0.72), 0.85), bub * stop);
        DFx fe = fx; fe.mat = M_HOLO; fe.matC = float3(1.0, 0.85, 0.5); fe.matK = 1.0; fe.hurt = 0.0;
        [unroll] for (int i = 1; i <= 3; i++) {   // echoes: where it was a moment before, fading
            float ek = ease(t / 0.4) * (1.0 - ease((t - 1.0) / 0.5)) * (1.0 - float(i) * 0.25);
            if (ek > 0.05) f2Body(h, rd, feet - A * float(i) * 0.3 * ek, RY, 1.0, one, fe);
        }
        // the clock, behind them, facing you
        float3 n = normalize(-C * float3(1, 0, 1));
        float3 u = normalize(cross(float3(0, 1, 0), n));
        float3 Kc = C + float3(0, 0.6, 0) - n * 1.2;
        float show = ease((t - 0.3) / 0.4) * (1.0 - ease((t - 3.0) / 0.2));
        float count = saturate((t - 0.7) / 1.8) * 12.0;                    // the hand sweeps round twelve times
        float tp = dot(Kc, n) / dot(rd, n);
        if (show > 0.0 && tp > 0.0 && tp < h.t) {
            float3 P = rd * tp - Kc;
            float2 q = float2(dot(P, u), P.y) / (2.4 * show);
            float r = length(q), a = atan2(q.x, q.y);
            if (r < 1.0) {
                float tick = step(0.85, r) * step(abs(frac(a / 0.5236 + 0.5) - 0.5), 0.08);
                float roman = step(0.7, r) * step(r, 0.8) * step(abs(frac(a / 0.5236 + 0.5) - 0.5), 0.15);
                float ha = floor(count) * 0.5236 + ease(frac(count)) * 0.5236;
                float2 d1 = float2(sin(ha), cos(ha));
                float hand = step(abs(q.x * d1.y - q.y * d1.x), 0.025) * step(0.0, dot(q, d1)) * step(dot(q, d1), 0.78);
                float swept = step(fmod(a + 6.2832, 6.2832), ha) * 0.12;
                float rim = step(0.95, r);
                add += lerp(float3(1.0, 0.85, 0.5) * (0.06 + swept), float3(1.2, 1.0, 0.6), saturate(tick + roman + hand + rim)) * show;
            }
        }
        // the twelve cuts: a line of light across the body with each tick
        float3x3 RC = RY;
        [loop] for (int i = 0; i < 12; i++) {
            float ti = 0.7 + float(i) * 0.15;
            if (t < ti) break;
            float a = float(i) * 0.5236 + h1(seed + float(i)) * 0.3;
            float3 d = normalize(A * cos(a) + float3(0, sin(a), 0));
            float3 c0 = C + float3(0, (h1(float(i) * 3.0) - 0.5) * 1.0, 0);
            add += float3(1.0, 0.9, 0.6) * dsGlowSeg(rd, c0 - d * 0.7, c0 + d * 0.7, 0.012, h.t) * (step(t, 3.0) * 1.2 + exp(-(t - ti) * 6.0) * 2.0);
        }
        // the body: whole until it all lets go, then twelve slices sliding apart and shattering
        float br = t - 3.0;
        [loop] for (int i = 0; i < 12; i++) {
            float y0 = float(i) * 32.0 / 12.0 - 0.5, y1 = float(i + 1) * 32.0 / 12.0 - 0.5;
            float ui = max(br, 0.0);
            float3 dv = (A * (h1(float(i) + seed) - 0.5) * 2.0 + fwd * (h1(float(i) * 2.0 + seed) - 0.5)) * ui * 1.5 + float3(0, -3.0 * ui * ui, 0);
            if (br < 0.5) dsBody(h, rd, feet + dv, mul(RY, rotY(ui * (h1(float(i)) - 0.5) * 3.0)), 1.0, y0, y1, fx);
        }
        f2Cubes(h, rd, C, br, 32, seed, 3.0, 1.0, 9.0, 1.4, 0.035, hex3(220, 235, 255), hex3(255, 245, 210), floorY, float3(0.6, 0.6, 0.5));
    } else if (k == 2) {
        // STARFALL: the sky fills with stars and a colossal star falls; a pillar of light meets it, the impact carves
        // a crater and showers star dust
        float night = ease(t / 0.5) * (1.0 - ease((t - 3.6) / 0.5));
        float sky = step(1e6, length(S));
        float3 nc = f2Lum3(o) * float3(0.10, 0.14, 0.32);
        float2 sq = floor(normalize(rd).xz / max(normalize(rd).y + 1.2, 0.2) * 220.0);
        float star = step(0.993, hash12(sq)) * (0.6 + 0.4 * sin(t * 6.0 + hash12(sq * 1.3) * 30.0));
        f2World(dec, decK, nc + float3(0.9, 0.95, 1.1) * star * sky, bub * night);
        float tI = 2.0;
        float3 St = C + float3(1.5, 14.0, 0.8) * pow(saturate(1.0 - (t - 0.6) / (tI - 0.6)), 1.5) + float3(0, 1.0, 0) * step(t, 0.6) * 0.0;
        if (t > 0.6 && t < tI) {   // the star: a white-gold core with four long spikes, trailing light
            float sp = 0.8 + 0.2 * sin(t * 20.0);
            add += float3(1.0, 0.95, 0.8) * (f2Glow(rd, St, 0.6, h.t) * 2.5 + f2Glow(rd, St, 1.6, h.t) * 0.5);
            add += float3(1.0, 0.95, 0.8) * (dsGlowSeg(rd, St - float3(1.6, 0, 0) * sp, St + float3(1.6, 0, 0) * sp, 0.04, h.t) + dsGlowSeg(rd, St - float3(0, 1.6, 0) * sp, St + float3(0, 1.6, 0) * sp, 0.04, h.t)) * 1.2;
            add += float3(0.6, 0.75, 1.0) * dsGlowSeg(rd, St, St + float3(1.5, 14.0, 0.8) * 0.25, 0.35, h.t) * 0.4;
        }
        float pil = ease((t - 1.2) / 0.3) * (1.0 - ease((t - tI - 0.2) / 0.4));
        if (pil > 0.0) add += float3(0.7, 0.85, 1.0) * dsGlowSeg(rd, feet, feet + float3(0, 12.0, 0), 0.35 * pil, h.t) * 0.8;   // the pillar of light from them
        if (t < tI) f2Body(h, rd, feet + float3(0, ease((t - 1.2) / 0.8) * 1.2, 0), RY, 1.0, one, fx);
        float u = t - tI;
        add += float3(1.0, 0.95, 0.85) * f2Glow(rd, feet + float3(0, 0.8, 0), 3.0 * easeOut(saturate(u / 0.3)), h.t) * exp(-max(u, 0.0) * 3.0) * step(0.0, u) * 1.5;
        fnGround(onG, S, feet, tI, 2.6, 1.4, 3.6, seed, t, o, dec, decK, add, float3(0.8, 0.9, 1.0));
        fnDebris(h, rd, feet, u, seed, 2.0, floorY);
        f2Cubes(h, rd, feet + float3(0, 0.5, 0), u, 36, seed + 5.0, 3.5, 3.5, 4.0, 2.2, 0.025, hex3(255, 245, 200), hex3(180, 210, 255), -1e5, float3(1.2, 1.15, 1.0));
    } else if (k == 3) {
        // VOID: the sound dies; specks of nothing gather into a pitch-black hole in front of the mob that swallows
        // light, ground and mob; the world turns to its negative, and the hole is gone - and the mob with it
        float3 H = C + A * 0.9;
        float g1 = ease((t - 0.2) / 1.0);
        [loop] for (int i = 0; i < 24; i++) {   // the specks: black motes drifting in to the hole
            float hi = float(i);
            float3 P0 = H + (float3(h1(hi + seed), h1(hi * 2.0 + seed), h1(hi * 3.0 + seed)) - 0.5) * 4.5;
            float3 P = lerp(P0, H, g1 * g1);
            if (g1 < 0.99) dsBox(h, rd, P, float3(0.04, 0.04, 0.04) * (1.0 - g1 * 0.5), rotY(hi + t), float3(0, 0, 0), 0.0);
        }
        float rH = 0.85 * ease((t - 0.9) / 0.6) * (1.0 - ease((t - 3.0) / 0.25));
        if (rH > 0.01) {   // the hole: pure black, the light round it bent into a thin bright ring
            float tc = dot(H, rd), d2 = dot(H, H) - tc * tc;
            if (d2 < rH * rH && tc < h.t) { h.t = tc - sqrt(rH * rH - d2); h.c = float3(0, 0, 0); h.hit = true; }
            float d = sqrt(d2);
            add += float3(0.85, 0.85, 1.0) * exp(-pow((d - rH * 1.08) / (rH * 0.04), 2.0)) * step(tc, h.t + 2.0) * 0.8;
            f2World(dec, decK, o * smoothstep(rH * 1.0, rH * 2.6, d), saturate(1.0 - d / (rH * 2.6)) * step(tc - 0.5, length(S)));   // it swallows the light round it
        }
        float pull = ease((t - 1.4) / 1.3);
        if (pull < 0.99) f2Body(h, rd, lerp(feet, H - float3(0, 0.5, 0), pull), mul(RY, rotZ(pull * 2.0)), 1.0 - pull, float3(1.0 - 0.6 * pull, 1.0 + pull, 1.0 - 0.6 * pull), fx);
        if (onG && pull > 0.01) { float eat = smoothstep(1.6 * pull, 1.2 * pull, length(S.xz - H.xz)) * (1.0 - ease((t - 3.0) / 0.2)); dec = lerp(decK > 0.0 ? dec : o, float3(0, 0, 0), eat); decK = max(decK, eat > 0.0 ? 1.0 : 0.0); }
        float neg = ease((t - 3.0) / 0.1) * (1.0 - ease((t - 3.5) / 0.6));
        f2World(dec, decK, 1.0 - o, bub * neg);
    } else if (k == 4) {
        // DRAGON VEIN RELEASE: veins of light run in over the ground to the mob; a jade dragon bursts from the earth,
        // spirals into the sky and dives back down, tearing the land open
        float3 jade = float3(0.25, 1.0, 0.6);
        if (onG) {   // the veins: branching lines of jade light converging on the feet
            float2 gq = floor((S.xz - feet.xz) * 16.0) / 16.0;
            float d = length(gq), a = atan2(gq.y, gq.x);
            float run = 4.5 * (1.0 - ease((t - 0.1) / 0.9));
            float vein = 0.0;
            [unroll] for (int i = 0; i < 7; i++) { float ai = float(i) * 0.8976 + seed + 0.3 * sin(d * 2.0 + float(i)); vein = max(vein, step(abs(frac((a - ai) / 6.2832 + 0.5) - 0.5) * 6.2832 * d, 0.05)); }
            vein *= step(run, d) * step(d, 4.5) * (1.0 - ease((t - 1.6) / 0.6));
            add += jade * vein * 1.4;
        }
        // the dragon: a chain of jade scales on a path out of the ground, up round them, into the sky, back down
        float tE = 0.9;
        [loop] for (int i = 0; i < 26; i++) {
            float lag = float(i) * 0.045;
            float u = t - tE - lag;
            if (u < 0.0) continue;
            float3 P;
            if (u < 1.4) { float a = u * 5.0; float r = 1.3 - u * 0.3; P = feet + float3(cos(a) * r, u * 6.0, sin(a) * r); }
            else { float v = u - 1.4; float3 top = feet + float3(cos(7.0) * 0.88, 8.4, sin(7.0) * 0.88); P = lerp(top, feet + float3(0, -2.0, 0), saturate(v / 0.6)) + float3(sin(v * 8.0) * 0.3, 0, 0); }
            if (P.y < floorY - 0.3) continue;
            float sz = i == 0 ? 0.32 : 0.24 * (1.0 - float(i) / 30.0);
            float3 col = lerp(hex3(40, 170, 110), hex3(150, 240, 190), h1(float(i)));
            dsBox(h, rd, P, float3(sz, sz, sz), rotY(u * 5.0 + float(i)), col, jade * (i == 0 ? 0.5 : 0.25));
            if (i == 0) { dsBox(h, rd, P + float3(0.15, 0.35, 0), float3(0.04, 0.2, 0.04), rotZ(0.4), hex3(240, 220, 150), 0.0); dsBox(h, rd, P + float3(-0.15, 0.35, 0), float3(0.04, 0.2, 0.04), rotZ(-0.4), hex3(240, 220, 150), 0.0);
                          add += float3(1.0, 0.3, 0.1) * f2Glow(rd, P + float3(0, 0.1, 0.3), 0.05, h.t) * 3.0; }
            if (i % 4 == 0) add += jade * f2Glow(rd, P, 0.5, h.t) * 0.12;
        }
        float tD = tE + 2.0;
        if (t < tD) f2Body(h, rd, feet + float3(0, ease((t - tE) / 0.4) * 0.6 * (1.0 - ease((t - tE - 1.2) / 0.6)), 0), mul(RY, rotY(ease((t - tE) / 1.4) * 4.0)), 1.0, one, fx);
        fnGround(onG, S, feet, tD, 2.7, 1.3, 4.0, seed, t, o, dec, decK, add, jade);
        fnDebris(h, rd, feet, t - tD, seed, 2.0, floorY);
        f2Cubes(h, rd, C, t - tD, 24, seed + 3.0, 3.0, 3.0, 12.0, 1.4, 0.05, team * 0.6, hex3(40, 170, 110), floorY, jade * 0.3);
        add += jade * f2Glow(rd, feet + float3(0, 0.5, 0), 2.5, h.t) * exp(-max(t - tD, 0.0) * 4.0) * step(tD, t);
    } else if (k == 5) {
        // TSUNAMI: the world sinks into a quiet deep sea; the mob is wrapped in a bubble while a great whale drifts
        // past, until a towering wall of water rises and crashes down, sweeping it away
        float sea = ease(t / 0.6) * (1.0 - ease((t - 3.8) / 0.5));
        float caus = pow(0.5 + 0.5 * sin(S.x * 3.0 + t * 2.0) * sin(S.z * 3.3 - t * 1.7), 3.0) * step(length(S), 1e6);
        f2World(dec, decK, f2Lum3(o) * float3(0.12, 0.38, 0.62) + float3(0.0, 0.05, 0.12) + caus * float3(0.2, 0.35, 0.4), bub * sea);
        float3 B = C + float3(0, 0.2 + 0.1 * sin(t * 2.0), 0);
        float rB = 1.15 * ease((t - 0.3) / 0.4);
        if (rB > 0.01 && t < 3.0) {   // the bubble round them
            float tc = dot(B, rd), d2 = dot(B, B) - tc * tc;
            if (d2 < rB * rB && tc - sqrt(rB * rB - d2) < h.t) add += float3(0.7, 0.9, 1.0) * pow(saturate(sqrt(d2) / rB), 6.0) * 0.8;
        }
        // the whale: a long blue-grey body, a pale belly, a sweeping tail - drifting across behind them
        float3 Wp = C + float3(0, 2.3, 0) - normalize(C * float3(1, 0, 1)) * 3.0 + A * (6.0 - t * 3.6);
        float3x3 RW = rotY(atan2(-A.x, -A.z));
        float sw2 = sin(t * 2.5) * 0.35;
        if (t < 3.3) {
            dsBox(h, rd, Wp, float3(0.55, 0.5, 1.4), RW, hex3(60, 80, 110), 0.0);
            dsBox(h, rd, Wp + mul(RW, float3(0, -0.35, 0.1)), float3(0.5, 0.18, 1.2), RW, hex3(190, 200, 205), 0.0);
            dsBox(h, rd, Wp + mul(RW, float3(0, 0.1, -1.7)), float3(0.25, 0.25, 0.45), mul(RW, rotY(sw2)), hex3(55, 75, 100), 0.0);
            dsBox(h, rd, Wp + mul(RW, mul(rotY(sw2), float3(0, 0.1, -2.25))), float3(0.75, 0.06, 0.2), mul(RW, rotY(sw2)), hex3(55, 75, 100), 0.0);
            add += float3(0.9, 0.95, 1.0) * f2Glow(rd, Wp + mul(RW, float3(0.4, 0.15, 1.1)), 0.03, h.t) * 2.0;
        }
        // the wall of water: rising behind, curling over, crashing through
        float rise = ease((t - 2.2) / 0.6), crash = ease((t - 2.8) / 0.4);
        float3 Wb = C - A * (2.5 - crash * 3.0);
        float hw = 4.5 * rise * (1.0 - crash * 0.9);
        if (rise > 0.0 && t < 3.6) {
            float3x3 RA = rotY(atan2(A.x, A.z));
            dsBox(h, rd, Wb + float3(0, hw * 0.5 - 0.95, 0), float3(3.0, hw * 0.5, 0.35), mul(RA, rotX(crash * 0.9)), hex3(30, 90, 150), float3(0.02, 0.08, 0.12));
            dsBox(h, rd, Wb + float3(0, hw - 0.95, 0) + A * crash * 0.8, float3(3.0, 0.15, 0.4), mul(RA, rotX(crash * 0.9)), hex3(230, 240, 245), float3(0.15, 0.17, 0.18));
        }
        float carry = ease((t - 3.0) / 0.6);
        if (t < 3.6) f2Body(h, rd, feet + A * carry * 3.0 + float3(0, carry * 0.4, 0), mul(RY, rotX(carry * 2.0)), 1.0, one, fx);
        f2Cubes(h, rd, C - A * 0.3, t - 2.95, 30, seed, 3.5, 2.5, 9.0, 1.4, 0.04, hex3(190, 225, 245), hex3(80, 150, 210), floorY, float3(0.1, 0.2, 0.3));
        fnPoof(h, rd, feet + A * 3.0 + float3(0, 0.5, 0), t - 3.6, seed, 12, 0.07);
    } else if (k == 6) {
        // THOUSAND BLADES: the sound dies and the blade slips a finger's width from its sheath. A red light settles in
        // the mob's eye, and you are gone: afterimages race round it, every cut left hanging in the air. At a thousand
        // you sheathe the blade with your back turned, and every cut runs at once
        float still = ease(t / 0.3) * (1.0 - ease((t - 3.4) / 0.4));
        f2World(dec, decK, lerp(o, f2Lum3(o) * 0.75, 0.8), bub * still);
        add += float3(1.0, 0.05, 0.05) * f2Glow(rd, feet + mul(RY, float3(1.5, 28.5, 4.2) * DPX), 0.06, h.t) * ease((t - 0.3) / 0.2) * 4.0;   // the red eye
        // the afterimages: phantoms flickering in at points round them, one after another
        float race = step(0.6, t) * step(t, 2.4);
        [unroll] for (int i = 0; i < 5; i++) {
            float ph = floor(t * 14.0) + float(i) * 3.0;
            float a = h1(ph + seed) * 6.2832, r = 1.2 + 0.4 * h1(ph * 1.7);
            DFx fa = fg; fa.ash = 0.45 + 0.1 * float(i);
            if (race > 0.0) dsBody(h, rd, feet + float3(cos(a) * r, 0.0, sin(a) * r), rotY(-a - 1.5708), 1.0, -99.0, 99.0, fa);
        }
        // the cuts: lines of light hanging in a cage round the body, more and more
        [loop] for (int i = 0; i < 36; i++) {
            float ti = 0.6 + float(i) * 0.05;
            if (t < ti) break;
            float3 d = normalize(float3(h1(float(i) + seed) - 0.5, h1(float(i) * 2.0 + seed) - 0.5, h1(float(i) * 3.0 + seed) - 0.5));
            float3 c0 = C + (float3(h1(float(i) * 5.0), h1(float(i) * 7.0), h1(float(i) * 9.0)) - 0.5) * float3(0.6, 1.6, 0.6);
            float run = step(3.0, t) * exp(-(t - 3.0) * 4.0);
            add += lerp(float3(0.9, 0.9, 1.0), float3(1.0, 0.2, 0.2), 0.3) * dsGlowSeg(rd, c0 - d * 0.6, c0 + d * 0.6, 0.008, h.t) * (step(t, 3.3) * (0.6 + exp(-(t - ti) * 10.0) * 2.0) + run * 4.0);
        }
        // you: standing off with your back turned, sheathing
        if (t > 2.4 && t < 3.6) { DFx fb = fg; fb.ash = ease((t - 3.2) / 0.4); dsBody(h, rd, feet - A * 2.2, rotY(atan2(-A.x, -A.z)), 1.0, -99.0, 99.0, fb); }
        // every cut runs: the body falls apart in pieces
        float br = t - 3.0;
        if (br < 0.0) f2Body(h, rd, feet, RY, 1.0, one, fx);
        else {
            [loop] for (int j = 0; j < 6; j++) {
                float3 dv = (float3(h1(float(j) + seed), 0.5, h1(float(j) * 3.0 + seed)) - 0.5) * br * 2.0 + float3(0, -4.0 * br * br, 0);
                float3 Pj = feet + mul(RY, kPC[j] * DPX) + dv;
                if (Pj.y > floorY - 0.2 && br < 1.0) dsPart(h, rd, j, Pj, mul(RY, rotX(br * 3.0 * (h1(float(j)) - 0.5))), 1.0, -99.0, 99.0, fx);
            }
        }
        f2Cubes(h, rd, C, br, 20, seed, 2.0, 1.0, 9.0, 1.2, 0.03, hex3(110, 2, 2), hex3(181, 18, 18), floorY, 0.0);
        fnPoof(h, rd, C, t - 3.9, seed, 12, 0.07);
    } else if (k == 7) {
        // ANIME: a ring of light runs out from the mob and the world is redrawn as an anime cel - flat colours, ink,
        // a painted sky - for a beating: POW! BAM! WHAM! ... UPPERCUT!! ... to be continued
        float ring = t * 9.0;
        float sky = step(1e6, length(S));
        float on = max(sky * step(0.35, t), step(length(S - C), ring)) * (1.0 - ease((t - 3.8) / 0.4));   // the world turns as the ring passes
        float3 cel = floor(pow(saturate(o), 1.0) * 3.0 + 0.5) / 3.0;
        float lum = f2Lum3(o).x;
        float ink = step(abs(frac(lum * 3.0) - 0.5), 0.04);
        float3 painted = lerp(float3(0.55, 0.8, 1.0), float3(0.25, 0.55, 0.95), saturate(normalize(rd).y * 2.0));
        float3 cc = lerp(lerp(cel * 1.1, cel * 0.15, ink), painted, sky);
        f2World(dec, decK, cc, bub * on);
        add += float3(1.0, 1.0, 0.9) * f2Ring(onG, S, feet, ring, 0.12) * exp(-t * 2.0) * 1.5;
        // the beating: a hit every 0.3 s, the body jolting, an impact star where it lands; the seventh is an uppercut
        float hitT[7] = { 0.5, 0.8, 1.1, 1.4, 1.7, 2.0, 2.4 };
        float jolt = 0.0; float3 jd = 0.0;
        [unroll] for (int i = 0; i < 7; i++) {
            float u = t - hitT[i];
            if (u < 0.0) continue;
            float3 hp = C + float3((h1(float(i) + seed) - 0.5) * 0.6, (h1(float(i) * 2.0) - 0.5) * 0.8 + (i == 6 ? 0.4 : 0.0), 0) + A * 0.3;
            float sz = exp(-u * 5.0);
            // the star: a jagged yellow burst with a black edge, facing you
            float3 nb = normalize(-hp);
            float tp = dot(hp, nb) / dot(rd, nb);
            if (sz > 0.05 && tp > 0.0 && tp < h.t) {
                float3 P = rd * tp - hp; float3 ub = normalize(cross(float3(0, 1, 0), nb)); float3 vb = cross(nb, ub);
                float2 q = float2(dot(P, ub), dot(P, vb)) / (0.55 * (0.6 + 0.4 * sz) * (i == 6 ? 1.6 : 1.0));
                float r = length(q), a = atan2(q.y, q.x);
                float edge = 0.55 + 0.35 * step(0.5, frac(a / 6.2832 * 10.0));
                if (r < edge) { h.t = tp; h.c = r > edge - 0.12 ? float3(0, 0, 0) : (r < edge * 0.45 ? float3(1.6, 0.4, 0.2) : float3(1.6, 1.25, 0.15)); h.hit = true; }
            }
            jolt += exp(-u * 12.0);
            jd += (i == 6 ? float3(0, 1, 0) : -A) * exp(-u * 12.0) * 0.12;
        }
        float up = ease((t - 2.4) / 0.6);
        float3 O = feet + jd + float3(0, up * 3.0 - max(t - 3.0, 0.0) * max(t - 3.0, 0.0) * 6.0, 0);
        if (t < 3.6) f2Body(h, rd, O, mul(RY, rotX(up * 6.0)), 1.0, one, fx);
        fnPoof(h, rd, O + float3(0, 0.6, 0), t - 3.6, seed, 12, 0.07);
        // to be continued: an arrow sign, hand-painted
        float tb = ease((t - 3.4) / 0.3) * (1.0 - ease((t - 4.4) / 0.3));
        if (tb > 0.0) {
            float3 nA = normalize(-feet); float3 uA = normalize(cross(float3(0, 1, 0), nA));
            float3 Ac = feet + float3(0, 0.45, 0) + uA * 0.4;
            float tp = dot(Ac, nA) / dot(rd, nA);
            if (tp > 0.0 && tp < h.t) {
                float3 P = rd * tp - Ac;
                float2 q = float2(dot(P, -uA), P.y) / (0.45 * tb);
                float arrow = (abs(q.y) < 0.28 && q.x > -1.4 && q.x < 0.3) || (q.x >= 0.3 && q.x < 1.0 && abs(q.y) < (1.0 - q.x) * 0.85) ? 1.0 : 0.0;
                if (arrow > 0.0) { h.t = tp; h.c = hex3(130, 90, 30) * 1.4; h.hit = true; }
            }
        }
    } else {
        // CHRONO FRACTURE: one snap of the fingers and the world is gone. In broken spacetime the mob's time, fate,
        // dimension and space are taken one by one, until all of it converges to a single point; the core is
        // crushed, and the mob is gone from existence
        float gone = ease((t - 0.25) / 0.15) * (1.0 - ease((t - 3.8) / 0.4));
        float neg = f2Lum3(o).x;
        float3 nl = normalize(rd);
        float2 fp = floor(float2(atan2(nl.z, nl.x), nl.y) * 9.0);
        float frac1 = step(0.82, hash12(fp + floor(t * 2.0)));
        float lines = step(abs(frac(nl.y * 9.0 + sin(atan2(nl.z, nl.x) * 5.0) * 0.6) - 0.5), 0.02) + step(abs(frac(atan2(nl.z, nl.x) * 4.0 + nl.y * 3.0) - 0.5), 0.015);
        float3 broken = (1.0 - neg) * float3(0.08, 0.07, 0.12) + float3(0.85, 0.8, 1.0) * saturate(lines) * 0.8 + float3(0.25, 0.1, 0.4) * frac1 * 0.3;
        f2World(dec, decK, broken, bub * gone);
        add += float3(1, 1, 1) * f2Glow(rd, C, 3.0, h.t) * exp(-max(t - 0.25, 0.0) * 12.0) * step(0.25, t);   // the snap
        // time: a ghost clock face spinning backward behind it (0.4-1.2)
        float tm = ease((t - 0.4) / 0.2) * (1.0 - ease((t - 1.2) / 0.2));
        if (tm > 0.0) { float3 nC = normalize(-C); float tp = dot(C, nC) / dot(rd, nC); float3 P = rd * tp - C; float r = length(P); float a = atan2(P.y, dot(P, normalize(cross(float3(0, 1, 0), nC)))) + t * 8.0; add += float3(0.85, 0.8, 1.0) * (exp(-pow((r - 1.2) / 0.03, 2.0)) + step(abs(sin(a * 6.0)), 0.08) * step(abs(r - 1.05), 0.08)) * tm * 0.9; }
        // fate: chains of light binding it (1.0-1.8)
        float ch = ease((t - 1.0) / 0.2) * (1.0 - ease((t - 1.8) / 0.2));
        [unroll] for (int i = 0; i < 4; i++) { float a = float(i) * 1.5708 + 0.4; float3 e0 = C + float3(cos(a), (float(i) - 1.5) * 0.4, sin(a)) * 2.0; add += float3(0.9, 0.7, 1.0) * dsGlowSeg(rd, e0, C, 0.02, h.t) * ch * 1.2; }
        // dimension: the body split into offset copies (1.6-2.4); space: it tears into shifted slabs (2.2-2.8)
        float dim = ease((t - 1.6) / 0.3) * (1.0 - ease((t - 2.4) / 0.2));
        float sp = ease((t - 2.2) / 0.2) * (1.0 - ease((t - 2.8) / 0.2));
        float conv = ease((t - 2.6) / 0.5);                                       // all of it converging to one point
        DFx fd = fx; fd.mat = M_HOLO; fd.matC = float3(0.8, 0.6, 1.0); fd.matK = 1.0;
        if (dim > 0.0) { f2Body(h, rd, feet + A * 0.5 * dim, RY, 1.0, one, fd); f2Body(h, rd, feet - A * 0.5 * dim, RY, 1.0, one, fd); }
        if (t < 3.1) {
            [loop] for (int i = 0; i < 4; i++) {
                float y0 = float(i) * 8.0 - 0.5, y1 = float(i + 1) * 8.0 - 0.5;
                float3 shift = A * (h1(float(i) + seed) - 0.5) * 0.5 * sp;
                float s = 1.0 - conv;
                dsBody(h, rd, lerp(feet + shift, C, conv), RY, max(s, 0.02), y0, y1, fx);
            }
        }
        float core = step(2.9, t) * (1.0 - step(3.35, t));
        float3 Kc = C + float3(0, 0.05 * sin(t * 30.0), 0);
        if (core > 0.0) { dsBox(h, rd, Kc, float3(0.08, 0.08, 0.08), mul(rotY(t * 6.0), rotX(t * 4.0)), float3(0.9, 0.8, 1.0), float3(1.2, 0.9, 1.6)); add += float3(0.8, 0.6, 1.0) * f2Glow(rd, Kc, 0.4, h.t) * 0.8; }
        // crushed: an implosion flash, a ring, nothing
        float u = t - 3.35;
        add += float3(1.0, 0.95, 1.0) * f2Glow(rd, Kc, 2.0 * (1.0 - saturate(u / 0.25)), h.t) * step(0.0, u) * exp(-max(u, 0.0) * 4.0) * 1.2;
        add += float3(0.85, 0.75, 1.0) * f2Ring(onG, S, feet, u * 5.0, 0.08) * step(0.0, u) * exp(-max(u, 0.0) * 2.0);
    }
}
