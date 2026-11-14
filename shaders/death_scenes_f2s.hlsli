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

// the Tsunami's wave: > 0 inside the water. P = (along its travel from the crest, up, along the crest). A ridge with a
// steep face and a long back, the crest leaning out ahead of the face, a slow swell along its length
float f2Wave(float3 P, float HW, float t, float z0, float z1) {
    HW *= smoothstep(z0, z0 + 3.5, P.z) * smoothstep(z1, z1 - 3.5, P.z) + 0.001;
    float yh = P.y / HW;
    float x = P.x + 1.5 * smoothstep(0.5, 1.0, yh) * yh;
    return HW * (x > 0.0 ? exp(-x / 3.4) : exp(-x * x / 1.1)) * (0.93 + 0.07 * sin(P.z * 1.7 + t * 3.0)) - P.y;
}
// the scene at another pixel - but where that pixel is sky, this pixel's own colour: the sky is drawn by this pass,
// not in the scene buffer
float3 f2Scene(int2 p, float3 o) { return isSky(rawDepth(p)) ? o : sceneAt(p).rgb; }
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
        // WORLD SLASH: time stops - colour drains, dust hangs in the air. A colossal sword of light forms in the sky
        // and comes down through everything: a white line runs across the world, the ground opens along it, and
        // the world above the line slides off the world below, the mob with it. Time returns; the halves fall apart
        float stop = ease(t / 0.35) * (1.0 - ease((t - 3.55) / 0.35));
        float3 P0 = feet + float3(0, 1.05, 0);
        float3 cutN = normalize(float3(0, 0.82, 0) + A * 0.57);              // the cut: a plane you see edge-on, a slanted line
        float3 Ld = normalize(A * 0.82 - float3(0, 0.57, 0));                // down along it
        float tCut = 1.55, uc = t - tCut;
        float slide = ease((uc - 0.3) / 1.5) * 0.6 * (1.0 - ease((t - 3.6) / 0.25));
        float3 Pn = rd * dot(P0, rd);                                        // this ray, where it passes the mob
        float sdr = dot(Pn - P0, cutN);
        bool sky = length(S) > 1e5;
        float3 Sw = sky ? rd * 80.0 : S;
        // the world in the bubble: drained, and - above the line - slid down along it
        float3 wc = o;
        if (uc > 0.3 && sdr > 0.0 && bub * stop > 0.0) {
            float3 pp = toPx(Sw - Ld * slide * bub);
            if (pp.z > 0.0) wc = f2Scene(int2(pp.xy), o);
        }
        f2World(dec, decK, lerp(wc, f2Lum3(wc) * float3(0.78, 0.86, 1.0) * 0.82, 0.88), bub * stop);
        // dust, stopped in the air
        [loop] for (int i = 0; i < 16; i++) {
            float hi = float(i);
            float3 M = P0 + (float3(h1(hi + seed), h1(hi * 2.0 + seed), h1(hi * 3.0 + seed)) - 0.5) * float3(6.0, 2.6, 6.0);
            add += float3(0.8, 0.9, 1.0) * f2Glow(rd, M, 0.028, h.t) * stop * 0.8;
        }
        // the sword of light: a blade with a guard and a ring of light at the hilt, hanging over the line, then down it
        float form = ease((t - 0.45) / 0.5), plunge = easeOut((t - 1.42) / 0.2);
        float swK = form * (1.0 - ease((t - 1.8) / 0.4));
        if (swK > 0.0) {
            float3 tip = P0 + Ld * (-2.4 - 1.0 * ease((t - 0.9) / 0.5) + 13.0 * plunge);
            float tp = dot(tip, vf) / max(dot(rd, vf), 1e-4);
            if (tp > 0.0 && tp < h.t + 0.5) {
                float3 X = rd * tp - tip;
                float a = -dot(X, Ld), b = dot(X, cutN);                     // back from the point, across the blade
                const float BL = 7.5;
                float hw = a < 0.0 ? -1.0 : (a < BL ? 0.5 * min(1.0, a / 1.6) : (a < BL + 0.4 ? 1.7 : (a < BL + 2.6 ? 0.16 : (a < BL + 3.1 ? 0.36 : -1.0))));
                float e = abs(b) - hw;
                float core = hw > 0.0 ? smoothstep(0.03, -0.03, e) : 0.0;
                float halo = hw > 0.0 ? exp(-max(e, 0.0) * max(e, 0.0) / 0.12) : 0.0;
                float edge = hw > 0.0 ? exp(-e * e / 0.002) : 0.0;
                float fuller = (a > 1.6 && a < BL) ? exp(-b * b / 0.004) * 0.5 : 0.0;
                float rr = length(float2(a - BL, b));
                float ring = exp(-pow((rr - 2.3) / 0.035, 2.0)) * step(0.35, frac(atan2(b, a - BL) / 6.2832 * 12.0 + t * 0.4)) + exp(-pow((rr - 2.9) / 0.02, 2.0)) * 0.5;
                add += (float3(1.0, 0.97, 0.9) * (core * (0.9 - fuller) + edge * 0.8) + float3(1.0, 0.8, 0.4) * (halo * 0.35 + ring * 0.9)) * swK * 1.5;
            }
        }
        // the line: white through everything, wide for an instant as the blade passes
        float lk = step(0.0, uc) * (0.55 + 2.2 * exp(-uc * 7.0)) * (1.0 - ease((t - 3.5) / 0.3)) * saturate(bub * 1.5);
        float lw = 0.012 + 0.09 * exp(-max(uc, 0.0) * 9.0);
        add += float3(1.0, 0.97, 0.9) * exp(-sdr * sdr / (lw * lw)) * lk;
        add += float3(1, 1, 1) * bub * exp(-pow(uc / 0.04, 2.0)) * 0.22;                                            // the instant itself
        // the ground, opened where the plane meets it: a dark split with hot edges, cooling
        if (onG && uc > 0.0) {
            float2 gq = floor(S.xz * 16.0) / 16.0;
            float gd = abs(dot(float3(gq.x, feet.y, gq.y) - P0, cutN) + 0.03 * (hash12(floor(gq * 4.0)) - 0.5));
            float along = abs(dot(S - feet, vf));
            float wd = (0.035 + 0.05 * ease(uc / 0.6)) * saturate(1.0 - along / 7.5);
            float crack = step(gd, wd), hot = exp(-max(gd - wd, 0.0) / 0.05) * saturate(1.0 - along / 7.5);
            float fade = 1.0 - ease((t - 4.3) / 0.5);
            dec = lerp(decK > 0.0 ? dec : o, float3(0.01, 0.01, 0.015), crack * fade); decK = max(decK, crack > 0.0 ? 1.0 : decK);
            add += float3(1.0, 0.6, 0.2) * hot * (1.0 - crack) * exp(-uc * 1.3) * 0.9 * fade;
        }
        // the mob: cut on the line. The upper half slides with the world, then - time back - off and down
        float back = max(t - 3.6, 0.0);
        if (t < 4.3) {
            if (uc < 0.3) dsBody(h, rd, feet, RY, 1.0, -99.0, 99.0, fx);
            else {
                float3 off = Ld * (slide + back * 1.6) + float3(0, -5.0 * back * back, 0);
                gCut = float4(cutN, dot(P0, cutN));
                dsBody(h, rd, feet, mul(rotZ(0.0), RY), 1.0, -99.0, 99.0, fx);
                gCut = float4(-cutN, -dot(P0 + off, cutN));
                dsBody(h, rd, feet + off, RY, 1.0, -99.0, 99.0, fx);
                gCut = float4(0, 0, 0, 0);
            }
        }
        f2Cubes(h, rd, P0, back - 0.02, 22, seed, 1.8, 1.4, 9.0, 1.2, 0.03, hex3(110, 2, 2), hex3(181, 18, 18), floorY, 0.0);
        fnPoof(h, rd, C, t - 4.3, seed, 16, 0.08);
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
        // VOID: the sound dies - the world goes grey and still. Specks of nothing wink into the air and are drawn
        // together into a pitch-black hole that bends the world round its edge, drinks the light, eats the ground
        // from under itself and pulls the mob in, thin as thread. Then it folds shut: the world flips to its
        // negative for a heartbeat, and both are gone
        float3 H = C + A * 1.15 + float3(0, 0.3, 0);
        float quiet = ease(t / 0.4) * (1.0 - ease((t - 4.0) / 0.4));
        float grow = ease((t - 0.95) / 0.7), coll = ease((t - 3.3) / 0.18);
        float rH = 0.9 * grow * (1.0 + 0.035 * sin(t * 9.0)) * (1.0 - coll);
        float tc = dot(H, rd);
        float3 offv = rd * tc - H; float d = length(offv);
        float3 wc = o; float lensK = 0.0;
        if (rH > 0.01 && tc > 0.0) {
            float RE = rH * 1.8;
            lensK = smoothstep(RE * 3.2, RE * 1.4, d) * step(tc, length(S) + 1.0);
            if (lensK > 0.0) {   // the world behind it, bent: light that passes near comes from further in
                float3 src = H + offv * (1.0 - lensK * RE * RE / max(d * d, 0.01));
                float3 pp = toPx(src);
                if (pp.z > 0.0) wc = f2Scene(int2(pp.xy), o);
            }
        }
        wc = lerp(wc, f2Lum3(wc) * 0.82, 0.75 * quiet);
        if (rH > 0.01 && tc > 0.0) {
            wc *= smoothstep(rH * 0.95, rH * 2.4, d) * 0.85 + 0.15;                                                 // it drinks the light
            float3 rt = normalize(cross(rd, float3(0, 1, 0)));
            float ang = atan2(offv.y, dot(offv, rt));
            float wisp = smoothstep(0.35, 1.0, sin(ang * 5.0 + log(max(d / rH, 0.2)) * 8.0 - t * 5.0)) * exp(-max(d - rH, 0.0) / (rH * 1.1));
            wc = lerp(wc, float3(0.03, 0.0, 0.07), wisp * 0.75 * step(tc, length(S) + 1.0));                        // what it has taken, circling in
        }
        f2World(dec, decK, wc, max(bub * quiet, lensK));
        if (rH > 0.01) {   // the hole itself, and the thin ring of light caught at its edge
            float d2 = d * d;
            if (d2 < rH * rH && tc - sqrt(rH * rH - d2) < h.t && tc > 0.0) { h.t = tc - sqrt(rH * rH - d2); h.c = float3(0, 0, 0); h.hit = true; h.part = -1; }
            float seen = step(tc, h.t + rH);
            add += (float3(0.92, 0.88, 1.0) * exp(-pow((d - rH * 1.07) / (rH * 0.03), 2.0)) * 1.3 + float3(0.35, 0.1, 0.6) * exp(-pow((d - rH * 1.25) / (rH * 0.25), 2.0)) * 0.35) * seen;
        }
        // the specks of nothing: they wink in one by one, hang, then spiral in
        [loop] for (int i = 0; i < 26; i++) {
            float hi = float(i);
            float born = 0.12 + hi * 0.03;
            if (t < born || grow > 0.98) continue;
            float3 P0 = H + (float3(h1(hi + seed), h1(hi * 2.0 + seed), h1(hi * 3.0 + seed)) - 0.5) * float3(5.0, 3.0, 5.0);
            float g2 = grow * grow;
            float3 rel = P0 - H;
            float3 P = H + mul(rotY(g2 * (3.0 + 2.0 * h1(hi * 7.0))), rel) * (1.0 - g2) + float3(0, sin(t * 3.0 + hi) * 0.04, 0);
            float sz = 0.05 * backOut((t - born) / 0.15) * (1.0 - 0.5 * g2);
            dsBox(h, rd, P, float3(sz, sz, sz), rotY(hi + t * 0.7), float3(0, 0, 0), 0.0);
            add += float3(0.8, 0.75, 1.0) * f2Glow(rd, P, sz * 1.7, h.t) * exp(-(t - born) * 8.0) * 1.2;           // the wink as it appears
        }
        // the ground under it, eaten: a ragged black bite with a violet rim, blocks torn loose and falling up into it
        float eat = ease((t - 1.35) / 1.5);
        if (onG && eat > 0.0) {
            float2 gq = floor((S.xz - H.xz) * 16.0) / 16.0;
            float ge = length(gq) + 0.22 * (f2VN(gq * 3.0 + seed) - 0.5) - 2.1 * eat;
            float fade = 1.0 - ease((t - 4.2) / 0.5);
            float bite = step(ge, 0.0) * fade;
            dec = lerp(decK > 0.0 ? dec : o, float3(0, 0, 0), bite); decK = max(decK, bite > 0.0 ? 1.0 : decK);
            add += float3(0.5, 0.2, 0.9) * exp(-abs(ge) / 0.05) * (1.0 - coll) * 0.6 * step(0.0, ge);
        }
        [loop] for (int j = 0; j < 14; j++) {
            float hj = float(j);
            float ph = frac((t - 1.4) * 0.75 + hj / 14.0);
            if (t < 1.4 || t > 3.25) break;
            float a = hj * 2.4 + seed, r0 = 1.0 + h1(hj * 3.0) * 1.2 * eat;
            float3 G0 = float3(H.x + cos(a) * r0, floorY, H.z + sin(a) * r0);
            float3 P = lerp(G0, H, ph * ph) + float3(0, sin(ph * 3.1416) * 0.5, 0);
            float sz = 0.11 * (1.0 - ph) + 0.01;
            dsBox(h, rd, P, float3(sz, sz, sz), mul(rotY(ph * 6.0 + hj), rotX(ph * 4.0)), hex3(70, 68, 76) * (1.0 - ph), 0.0);
        }
        // the mob: dragged off its feet, drawn out long and thin, and in
        float pull = ease((t - 1.5) / 1.2);
        if (pull < 0.99) {
            float3 to = normalize(H - C);
            float3x3 Rp = mul(rotAxis(normalize(cross(float3(0, 1, 0), to) + float3(0, 1e-4, 0)), pull * 1.3), RY);
            float sc = 1.0 - pull * 0.85;
            float3 ctr = lerp(C, H, pull * pull);
            f2Body(h, rd, ctr - mul(Rp, float3(0, 0.95 * sc * (1.0 + 1.2 * pull), 0)), Rp, sc, float3(1.0 - 0.7 * pull, 1.0 + 1.2 * pull, 1.0 - 0.7 * pull), fx);
        }
        // it folds shut: a white seam of light, the world in negative, a ring running out
        float uz = t - 3.3;
        if (uz > 0.0) {
            float3 rt = normalize(cross(rd, float3(0, 1, 0)));
            float dx = dot(offv, rt), dy = offv.y;
            add += float3(1, 1, 1) * exp(-dy * dy / 0.0015) * exp(-dx * dx / (4.0 + uz * 30.0)) * exp(-uz * 7.0) * 3.0 * step(tc, h.t + 1.0);
            add += float3(0.85, 0.8, 1.0) * exp(-pow((d - uz * 13.0) / 0.25, 2.0)) * exp(-uz * 3.5) * 0.7;
            float neg = ease(uz / 0.06) * (1.0 - ease((uz - 0.3) / 0.45));
            f2World(dec, decK, 1.0 - o, bub * neg);
        }
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
        // TSUNAMI: the world sinks into a quiet deep sea - blue dusk, light rippling on the ground, shafts of sun from
        // far above, a great whale drifting past - and the mob hangs in a bubble of air. Then the sea draws back,
        // and from one side comes the wave: a wall of water five blocks high, curling at the crest, foam streaming
        // down its face. It breaks over the mob and carries it off, and leaves the ground wet and laced with foam
        float sea = ease(t / 0.5) * (1.0 - ease((t - 1.75) / 0.45));
        bool sky = length(S) > 1e5;
        if (sea * bub > 0.001) {
            float caus = sky ? 0.0 : pow(saturate(0.5 + 0.5 * sin(S.x * 2.6 + t * 1.8 + sin(S.z * 1.7 + t)) * sin(S.z * 2.9 - t * 1.5 + sin(S.x * 1.3))), 4.0);
            float fog = sky ? 1.0 : saturate(length(S) / 22.0);
            float3 deep = lerp(f2Lum3(o) * float3(0.14, 0.42, 0.66) + float3(0.0, 0.05, 0.12) + caus * float3(0.25, 0.42, 0.45), float3(0.02, 0.16, 0.32), fog * 0.85);
            float shaft = pow(f2VN(float2(dot(rd, A) * 9.0 + t * 0.15, 0.5)), 3.0) * saturate(rd.y * 1.5 + 0.55);
            deep += float3(0.35, 0.6, 0.7) * shaft * 0.35;
            f2World(dec, decK, deep, bub * sea);
        }
        float tPopB = 2.85;
        float3 B = C + float3(0, 0.25 + 0.1 * sin(t * 2.0), 0);
        if (t < tPopB) {
            f2Body(h, rd, B - float3(0, 0.95, 0) + float3(0, 0.0, 0), mul(RY, rotZ(sin(t * 1.3) * 0.12)), 1.0, one, fx);
            add += f2Film(rd, B, 1.2 * ease((t - 0.25) / 0.4), h.t, t, 0.7) * 0.9;
        }
        [loop] for (int i = 0; i < 12; i++) {   // air rising in threads of small bubbles
            if (sea < 0.05) break;
            float hi = float(i), life = frac(t * (0.25 + 0.1 * h1(hi)) + h1(hi * 3.0 + seed));
            float3 Pb = feet + float3((h1(hi * 5.0 + seed) - 0.5) * 5.0, life * 4.5, (h1(hi * 7.0 + seed) - 0.5) * 5.0) + float3(sin(life * 12.0 + hi) * 0.07, 0, 0);
            add += f2Film(rd, Pb, 0.03 + 0.04 * h1(hi * 9.0), h.t, t + hi, 0.0) * sea;
        }
        // the whale: far side of the mob, crossing slowly - long back, pale belly, flippers, a tail that sweeps
        if (sea > 0.02) {
            float3 Wp = C + float3(0, 2.2 + 0.25 * sin(t * 0.9), 0) + vf * 5.0 + A * (5.5 - t * 4.2);
            float3x3 RW = rotY(atan2(-A.x, -A.z));
            float sw2 = sin(t * 2.2) * 0.3;
            float3 wb = lerp(hex3(46, 66, 98), hex3(10, 50, 90), 0.45), wl = lerp(hex3(170, 186, 196), hex3(10, 50, 90), 0.4);
            if (nearRay(rd, Wp, 4.2)) {
                dsBox(h, rd, Wp + mul(RW, float3(0, 0.0, 0.9)), float3(0.62, 0.55, 0.75), RW, wb, 0.0);                    // head
                dsBox(h, rd, Wp + mul(RW, float3(0, 0.02, -0.5)), float3(0.56, 0.5, 0.8), RW, wb, 0.0);
                dsBox(h, rd, Wp + mul(RW, float3(0, -0.4, 0.4)), float3(0.52, 0.2, 1.3), RW, wl, 0.0);                     // belly
                dsBox(h, rd, Wp + mul(RW, float3(0, 0.08, -1.6)), float3(0.36, 0.34, 0.5), mul(RW, rotY(sw2 * 0.5)), wb, 0.0);
                dsBox(h, rd, Wp + mul(RW, mul(rotY(sw2 * 0.5), float3(0, 0.1, -2.35)) + float3(0, 0, 0.0)), float3(0.2, 0.2, 0.4), mul(RW, rotY(sw2)), wb, 0.0);
                dsBox(h, rd, Wp + mul(RW, mul(rotY(sw2), float3(0, 0.12, -2.95))), float3(0.85, 0.06, 0.24), mul(RW, rotY(sw2 * 1.3)), wb, 0.0);   // flukes
                dsBox(h, rd, Wp + mul(RW, float3(0.75, -0.3, 0.5)), float3(0.4, 0.05, 0.2), mul(RW, rotZ(-0.5 + 0.2 * sin(t * 1.5))), wb, 0.0);
                dsBox(h, rd, Wp + mul(RW, float3(-0.75, -0.3, 0.5)), float3(0.4, 0.05, 0.2), mul(RW, rotZ(0.5 - 0.2 * sin(t * 1.5))), wb, 0.0);
                add += float3(0.9, 0.95, 1.0) * f2Glow(rd, Wp + mul(RW, float3(0.6, 0.12, 1.25)), 0.03, h.t) * 1.5 * sea;
            }
        }
        // the wave. Across the view: x = how far along A from the crest (it runs towards -A), y up, z into the view
        float tW = 1.95, vw = 9.5;
        float a0 = 11.0 - (t - tW) * vw;
        float HW = 5.4 * ease((t - tW) / 0.75) * (1.0 - ease((t - 3.55) / 0.9));
        if (HW > 0.05) {
            float zc = dot(-feet, vf);                                      // where you stand along the view
            float z0 = max(zc + 2.6, -4.5), z1 = 9.0;
            float3 X0 = -feet;                                              // the ray's start in wave space
            float3 ro = float3(dot(X0, A) - a0, X0.y, dot(X0, vf)), rv = float3(dot(rd, A), rd.y, dot(rd, vf));
            float3 lo = float3(-2.6, 0.03, z0), hi3 = float3(13.0, HW * 1.05 + 0.3, z1);
            float3 inv = 1.0 / select(abs(rv) > 1e-5, rv, 1e-5);
            float3 ta = (lo - ro) * inv, tb = (hi3 - ro) * inv;
            float3 tmn = min(ta, tb), tmx = max(ta, tb);
            float s0 = max(max(tmn.x, tmn.y), max(tmn.z, 0.05)), s1 = min(min(tmx.x, tmx.y), min(tmx.z, h.t));
            if (z1 > z0 && s0 < s1) {
                // inside the water: under a ridge with a steep face, a long back, and a crest that leans out ahead
                float ds = (s1 - s0) / 28.0, sA = s0, sHit = -1.0;
                float3 P = ro + rv * s0;
                if (f2Wave(P, HW, t, z0, z1) > 0.0) sHit = s0;
                else {
                    [loop] for (int i = 1; i <= 28; i++) {
                        float sB = s0 + ds * float(i);
                        P = ro + rv * sB;
                        if (f2Wave(P, HW, t, z0, z1) > 0.0) {
                            [unroll] for (int b = 0; b < 5; b++) { float sm = (sA + sB) * 0.5; P = ro + rv * sm; if (f2Wave(P, HW, t, z0, z1) > 0.0) sB = sm; else sA = sm; }
                            sHit = sB; break;
                        }
                        sA = sB;
                    }
                }
                if (sHit > 0.0) {
                    P = ro + rv * sHit;
                    float f0 = f2Wave(P, HW, t, z0, z1);
                    float3 Pa = P + float3(0.04, 0, 0), Pb2 = P + float3(0, 0.04, 0), Pc2 = P + float3(0, 0, 0.04);
                    float3 gl = float3(f2Wave(Pa, HW, t, z0, z1) - f0, f2Wave(Pb2, HW, t, z0, z1) - f0, f2Wave(Pc2, HW, t, z0, z1) - f0);
                    float3 nl3 = normalize(-gl + float3(0, 1e-5, 0));
                    float3 nw = normalize(A * nl3.x + float3(0, nl3.y, 0) + vf * nl3.z);
                    float hy = saturate(P.y / max(HW, 0.1));
                    float face = step(P.x, 0.3);                               // the front
                    float3 wcol = lerp(hex3(6, 52, 104), hex3(22, 140, 176), hy * hy);
                    wcol = lerp(wcol, hex3(120, 220, 228), smoothstep(0.75, 1.0, hy) * 0.7);                      // thin water at the crest lets the light through
                    float streak = smoothstep(0.55, 0.8, f2VN(float2(P.z * 2.2, P.y * 0.55 + t * 3.5 + P.x)));
                    wcol = lerp(wcol, hex3(190, 232, 240), streak * face * 0.5 * hy);                             // foam drawn down the face
                    float foam = smoothstep(0.82 - 0.25 * f2VN(float2(P.z * 3.0 + t, P.x * 2.0)), 0.97, hy) + step(P.y, 0.1 + 0.25 * f2VN(float2(P.z * 2.0, t * 2.0))) * face * step(-1.9, P.x);
                    float3 c = dsLit(lerp(wcol, float3(0.93, 0.97, 1.0), saturate(foam)), nw) * 1.25;
                    float fr = pow(1.0 - saturate(dot(-rd, nw)), 3.0);
                    c += dsEnv(reflect(rd, nw)) * fr * 0.5 * (1.0 - saturate(foam)) + float3(1.5, 1.4, 1.2) * pow(saturate(dot(reflect(rd, nw), SUN)), 40.0) * (1.0 - saturate(foam));
                    h.t = sHit; h.c = c; h.hit = true; h.part = -1;
                }
            }
            // spray torn off the crest
            [loop] for (int j = 0; j < 18; j++) {
                float hj = float(j), ph = frac(t * 1.6 + hj * 0.37);
                float3 Ps = feet + A * (a0 - 1.3 - ph * 1.6) + vf * ((h1(hj * 3.0 + floor(t * 1.6 + hj * 0.37)) - 0.5) * 9.0 + 2.0) + float3(0, HW * (0.95 + 0.35 * sin(ph * 3.1416)) - ph * ph * 1.5, 0);
                float sz = 0.05 * (1.0 - ph);
                if (dot(Ps - feet, vf) > z0) dsBox(h, rd, Ps, float3(sz, sz, sz), rotY(hj + t * 5.0), hex3(225, 240, 248), float3(0.15, 0.18, 0.2));
            }
        }
        // behind the wave: wet ground, foam lace sliding back
        if (onG && t > tW) {
            float aG = dot(S - feet, A) - a0;
            float age = aG / vw;
            if (aG > -0.5 && abs(dot(S - feet, vf) - 2.0) < 7.5) {
                float fade = 1.0 - ease((t - 4.2) / 0.55);
                float lace = smoothstep(0.5, 0.62, f2VN(S.xz * 2.4 + float2(t * 0.6, 0))) * smoothstep(0.35, 0.6, f2VN(S.xz * 7.0 - float2(0, t)));
                float3 g0 = (decK > 0.0 ? dec : o) * lerp(1.0, 0.68, fade * saturate(age * 4.0 + 0.5));
                dec = lerp(g0, float3(0.92, 0.96, 1.0), saturate(lace * exp(-max(age, 0.0) * 1.4) * 1.3 + smoothstep(0.3, -0.4, aG)) * fade); decK = max(decK, 1.0);
            }
        }
        // the mob: its bubble bursts on the face of the wave, and it is taken - tumbling in the water, gone with it
        float ut = t - tPopB;
        if (ut > 0.0 && t < 4.1) {
            float3 Pm2 = feet + A * min(0.0, a0 - 1.0) + float3(0, 0.3 + HW * 0.42 * saturate(ut * 3.0), 0) - vf * 0.2;
            f2Body(h, rd, Pm2, mul(RY, mul(rotX(ut * 7.0), rotZ(ut * 4.0))), 1.0, one, fx);
        }
        f2Cubes(h, rd, B, ut, 26, seed, 3.0, 2.0, 8.0, 0.9, 0.035, hex3(200, 230, 248), hex3(120, 190, 230), floorY, float3(0.1, 0.2, 0.3));
        add += float3(0.8, 0.95, 1.0) * f2Glow(rd, B, 1.4, h.t) * exp(-max(ut, 0.0) * 9.0) * step(0.0, ut) * 0.7;
    } else if (k == 6) {
        // THOUSAND BLADES: the sound dies. You stand before the mob, the blade a finger's width out of its sheath; a
        // red light settles in the mob's eye - and you are gone. Only afterimages race round it, every cut left
        // hanging in the air, the count climbing. At a thousand you are behind it, back turned, sliding the blade
        // home - and at the click every cut runs at once
        float still = ease(t / 0.3) * (1.0 - ease((t - 3.9) / 0.4));
        f2World(dec, decK, lerp(o, f2Lum3(o) * 0.72, 0.85), bub * still);
        float tGo = 0.8, tBack = 2.6, tClick = 3.35, ur = t - tClick;
        float3 red = float3(1.0, 0.12, 0.1);
        add += red * f2Glow(rd, feet + mul(RY, float3(1.5, 28.5, 4.2) * DPX), 0.055, h.t) * ease((t - 0.35) / 0.2) * step(t, tClick) * 4.0;   // the eye
        // you, before and after: the stance, the sheath at your hip, the sliver of steel
        float3x3 Rst = rotY(atan2(-A.x, -A.z)), Rtn = rotY(atan2(A.x, A.z));
        bool pre = t < tGo, post = t >= tBack && t < 4.35;
        if (pre || post) {
            DFx fb = fg; fb.ash = pre ? 1.0 - saturate(t / 0.3) * 1.15 : ease((t - 3.95) / 0.4);
            float3 Op = pre ? feet + A * 2.4 : feet - A * 2.4;
            float3x3 Rp = pre ? Rst : Rst;                                    // after: past it, still facing the way you went - your back to it
            float sh = pre ? 0.0 : ease((t - tBack - 0.15) / 0.55);            // how far the blade is home
            dsPosed(h, rd, Op, Rp, 1.0, float2(pre ? 0.12 : 0.3, 0), float2(pre ? -0.55 : -0.35 - 0.25 * (1.0 - sh), pre ? -0.75 : -0.6), float2(-0.25, 0.25), float2(-0.35, 0), float2(0.4, 0), 63, fb);
            float3 hip = Op + mul(Rp, float3(0.3, 0.72, 0.05));
            float3x3 Rk = mul(Rp, rotX(0.35));
            if (fb.ash < 0.9) {
                dsBox(h, rd, hip + mul(Rk, float3(0, 0, -0.35)), float3(0.035, 0.045, 0.5), Rk, hex3(26, 22, 30), float3(0.02, 0.02, 0.03));          // the sheath
                float out_ = pre ? 0.07 : 0.75 * (1.0 - sh);
                dsBox(h, rd, hip + mul(Rk, float3(0, 0, 0.17 + out_ * 0.5)), float3(0.012, 0.03, out_ * 0.5 + 0.001), Rk, hex3(230, 232, 240), float3(0.6, 0.6, 0.7));   // steel
                dsBox(h, rd, hip + mul(Rk, float3(0, 0, 0.18 + out_)), float3(0.06, 0.06, 0.015), Rk, hex3(190, 150, 60), float3(0.2, 0.15, 0.03));     // the guard
                dsBox(h, rd, hip + mul(Rk, float3(0, 0, 0.33 + out_)), float3(0.025, 0.03, 0.14), Rk, hex3(40, 30, 50), 0.0);                            // the grip
                float gl = exp(-pow((t - 0.45) / 0.07, 2.0)) + exp(-pow(ur / 0.05, 2.0));
                float3 gp = hip + mul(Rk, float3(0, 0.03, 0.2));
                float3 rt = normalize(cross(rd, float3(0, 1, 0)));
                float3 gv = rd * dot(gp, rd) - gp;
                add += float3(1, 1, 1) * gl * (exp(-dot(gv, gv) / 0.004) * 2.0 + exp(-gv.y * gv.y / 0.0006) * exp(-pow(dot(gv, rt), 2.0) / 0.5) * 1.5) * step(dot(gp, rd), h.t + 0.5);
            }
        }
        // gone: a streak where you were, then afterimages - whole, mid-cut - a new one twelve times a second, the last
        // two still fading (drawn on every other pixel)
        float3 pxv = toPx(rd * 10.0);
        bool dith = frac((floor(pxv.x) + floor(pxv.y)) * 0.5) < 0.25;
        if (t >= tGo && t < tBack) {
            add += float3(0.85, 0.85, 1.0) * dsGlowSeg(rd, feet + A * 2.4 + float3(0, 0.9, 0), feet + A * 0.4 + float3(0, 0.9, 0), 0.12, h.t) * exp(-(t - tGo) * 12.0);
            [unroll] for (int i = 0; i < 3; i++) {
                float sl = floor((t - tGo) * 12.0) - float(i);
                if (sl < 0.0 || (i > 0 && !dith)) continue;
                float a = h1(sl * 1.37 + seed) * 6.2832, r = 1.25 + 0.5 * h1(sl * 2.7);
                float3 Oa = feet + float3(cos(a) * r, 0.0, sin(a) * r);
                float3x3 Ra = rotY(atan2(-cos(a), -sin(a)));
                float hi2 = h1(sl * 5.1);
                dsPosed(h, rd, Oa, mul(Ra, rotX(0.25)), 1.0, float2(0.1, 0), float2(-1.3 - hi2 * 1.2, 0.3), float2(0.5, 0.3), float2(-0.7, 0), float2(0.55, 0), 63, fg);
                float3 hand = Oa + mul(Ra, float3(-0.32, 1.25 + hi2 * 0.4, 0.55));
                float3 tipd = normalize(mul(Ra, float3(0.5 - hi2, 0.6 * hi2 - 0.1, 1.0)));
                add += float3(0.95, 0.95, 1.0) * dsGlowSeg(rd, hand, hand + tipd * 1.1, 0.02, h.t) * (i == 0 ? 2.0 : 0.6);
            }
        }
        // the cuts: one per afterimage, flashing as it is made, then hanging - and all of them running at the click
        float nCut = min(floor(max(t - tGo, 0.0) * 12.0) + 1.0, 22.0) * step(tGo, t);
        float run = ur > 0.0 ? exp(-ur * 3.5) : 0.0;
        [loop] for (int c = 0; c < 22; c++) {
            if (float(c) >= nCut || ur > 1.2) break;
            float hc = float(c), ti = tGo + hc / 12.0;
            float3 dcut = normalize(float3(h1(hc + seed) - 0.5, (h1(hc * 2.0 + seed) - 0.5) * 1.4, h1(hc * 3.0 + seed) - 0.5));
            float3 c0 = C + (float3(h1(hc * 5.0), h1(hc * 7.0), h1(hc * 9.0)) - 0.5) * float3(0.5, 1.5, 0.5);
            float len = 0.75 + (ur > 0.0 ? min(ur, 0.3) * 3.0 : 0.0);
            float bright = ur > 0.0 ? run * 5.0 : 0.55 + exp(-(t - ti) * 9.0) * 3.0;
            add += lerp(float3(0.95, 0.95, 1.0), red, ur > 0.0 ? 0.6 : 0.25) * dsGlowSeg(rd, c0 - dcut * len, c0 + dcut * len, ur > 0.0 ? 0.012 + ur * 0.03 : 0.008, h.t) * bright;
        }
        // the count
        float cnt = t < tGo ? 0.0 : min(1000.0, floor(pow(saturate((t - tGo) / (tBack - tGo)), 1.6) * 1000.0));
        if (t > tGo && t < tClick + 0.6) {
            float2 q; float tp;
            float3 Pn = C + float3(0, 1.75, 0);
            if (f2Card(rd, Pn, h.t, q, tp)) {
                float nd = cnt >= 1000.0 ? 4.0 : (cnt >= 100.0 ? 3.0 : (cnt >= 10.0 ? 2.0 : 1.0));
                float2 g = q / 0.075 + float2(nd * 2.0 - 0.5, 2.5);
                int ci = int(floor(g.x * 0.25));
                if (g.x >= 0.0 && ci < int(nd)) {
                    float dg = fmod(floor(cnt / pow(10.0, nd - 1.0 - float(ci))), 10.0);
                    float ink = f2Text(float2(g.x - float(ci) * 4.0, g.y), TX_DIGITS + int(dg + 0.5), 1);
                    if (ink > 0.5) { h.t = tp; h.c = cnt >= 1000.0 ? red * 3.0 : float3(1.6, 1.6, 1.8); h.hit = true; h.part = -1; }
                }
            }
        }
        // the mob: whole until the click - then every piece the cuts made comes apart
        if (ur < 0.0) f2Body(h, rd, feet, RY, 1.0, one, fx);
        else if (ur < 1.0) {
            fx.clipY = floorY;
            [loop] for (int j = 0; j < 12; j++) {
                int pj = j / 2; bool top = (j % 2) == 1;
                float hj = float(j);
                float3 dv = float3(h1(hj + seed) - 0.5, 0.25 + 0.5 * h1(hj * 2.0), h1(hj * 3.0 + seed) - 0.5) * ur * 2.6 + float3(0, -5.0 * ur * ur, 0);
                float3x3 Rj = mul(RY, mul(rotX(ur * 5.0 * (h1(hj * 5.0) - 0.5)), rotZ(ur * 5.0 * (h1(hj * 7.0) - 0.5))));
                float3 Pj = feet + mul(RY, kPC[pj] * DPX) + dv;
                Pj.y = max(Pj.y, floorY + 0.08);
                dsPart(h, rd, pj, Pj, Rj, 1.0, top ? 0.0 : -99.0, top ? 99.0 : 0.0, fx);
            }
        }
        f2Cubes(h, rd, C, ur, 30, seed, 2.6, 1.2, 9.0, 1.2, 0.03, hex3(110, 2, 2), hex3(181, 18, 18), floorY, 0.0);
        add += red * f2Glow(rd, C, 1.6, h.t) * run * step(0.0, ur) * 0.5;
        fnPoof(h, rd, feet + float3(0, 0.2, 0), ur - 1.0, seed, 14, 0.07);
    } else if (k == 7) {
        // ANIME: a ring of gold light runs out from the mob and the world is redrawn as an anime cel - flat bright
        // colour, violet shadows with hatching, an ink line round every edge that boils like drawn frames, a painted
        // sky with hard-edged clouds. EPISODE 1: THE FINISH. Then the beating: POW! BAM! WHAM! CRACK! SMASH! BOOM! -
        // each with its impact frame - and ANIME BREAKER!!: an UPPERCUT!! that sends the mob off as a star. To be continued...
        float R7 = t * 11.0;
        float out7 = 1.0 - ease((t - 4.35) / 0.4);
        bool sky = length(S) > 1e5;
        float dist = length(S - C);
        float bubA = f2Bubble(rd, C, 8.0);
        float mask = (sky ? ease((t - 0.45) / 0.4) : 1.0 - smoothstep(R7 - 1.6, R7, dist)) * out7 * bubA;
        float hitT[7] = { 1.0, 1.28, 1.56, 1.84, 2.12, 2.4, 3.1 };
        float imp = 0.0, pulse = 0.0;
        [unroll] for (int i0 = 0; i0 < 7; i0++) { float u0 = t - hitT[i0]; imp = max(imp, step(0.0, u0) * step(u0, i0 == 6 ? 0.1 : 0.045)); pulse = max(pulse, step(0.0, u0) * exp(-u0 * 7.0)); }
        float3 pxv = toPx(rd * 10.0);
        int2 px = int2(pxv.xy);
        if (mask > 0.001) {
            float sc7 = max(1.0, c4.y / 1080.0);
            int so = int(1.6 * sc7 + 0.5);
            float3 col = (o * 2.0 + f2Scene(px + int2(so, so), o) + f2Scene(px + int2(-so, so), o) + f2Scene(px + int2(so, -so), o) + f2Scene(px + int2(-so, -so), o)) / 6.0;
            float l = dot(col, float3(0.299, 0.587, 0.114));
            float chroma = max(col.r, max(col.g, col.b)) - min(col.r, min(col.g, col.b));
            col = lerp(float3(l, l, l), col, 2.3);
            col = lerp(col * float3(0.80, 0.88, 1.22) + float3(0.04, 0.05, 0.12), col, smoothstep(0.02, 0.16, chroma));     // grey things get an anime background's tint
            col = pow(saturate(col), 0.78) * 1.08;
            l = dot(col, float3(0.299, 0.587, 0.114));
            float lit = smoothstep(0.50, 0.53, l), shadow = 1.0 - smoothstep(0.30, 0.33, l);
            col = lerp(col, col * float3(0.62, 0.52, 0.98), shadow);
            col *= lerp(1.0, 1.22, lit);
            col = floor(saturate(col) * 6.0 + 0.5) / 6.0;
            col *= lerp(1.0, 0.78, step(0.5, frac((pxv.x + pxv.y) / (7.0 * sc7))) * shadow);                                  // hatching in the shadows
            col += smoothstep(0.78, 1.0, l) * float3(1.0, 0.95, 0.85) * 0.14;
            // speed lines from the mob, hard on every hit
            float3 pc = toPx(C);
            float2 rel = (pxv.xy - pc.xy) / c4.y;
            float rays = step(0.62, f2VN(float2(atan2(rel.y, rel.x) * 9.0, floor(t * 10.0) * 0.37))) * smoothstep(0.05, 0.45, length(rel));
            if (sky) {
                float ty = saturate(rd.y * 1.8 + 0.08);
                float banded = floor(ty * 6.0) / 6.0 + 0.16 * smoothstep(0.3, 0.7, frac(ty * 6.0));
                float3 sk = banded < 0.5 ? lerp(float3(1.0, 0.82, 0.88), float3(0.45, 0.82, 1.0), banded * 2.0) : lerp(float3(0.45, 0.82, 1.0), float3(0.14, 0.34, 0.92), banded * 2.0 - 1.0);
                float2 cuv = rd.xz / (rd.y * 0.9 + 0.45) * 1.7 + float2(t * 0.02, 0.0);
                float cl = f2VN(cuv * 1.3) * 0.5 + f2VN(cuv * 2.64) * 0.25 + f2VN(cuv * 5.3) * 0.125 + f2VN(cuv * 10.7) * 0.0625;
                float cl2 = f2VN(cuv * 1.3 + float2(0.2, -0.26)) * 0.5 + f2VN(cuv * 2.64 + 0.4) * 0.25 + f2VN(cuv * 5.3 + 0.8) * 0.125;
                float3 cc = lerp(float3(0.86, 0.80, 1.0), float3(1.0, 0.98, 0.96), smoothstep(0.50, 0.52, cl2));
                sk = lerp(sk, cc, smoothstep(0.50, 0.52, cl) * smoothstep(-0.02, 0.18, rd.y));
                col = sk + rays * (0.1 + 0.3 * pulse) * float3(1.0, 0.96, 0.9);
            } else col += rays * (0.04 + 0.22 * pulse) * float3(1.0, 0.92, 0.85);
            // ink: a line wherever depth or tone breaks, redrawn twelve times a second
            float boil = floor(t * 12.0);
            int2 jit = int2((float2(hash12(float2(boil, 1.3)), hash12(float2(boil, 7.1))) - 0.5) * 3.0 * sc7);
            int eo = int(1.7 * sc7 + 0.5);
            int2 pj = px + jit;
            float d0 = viewZ(rawDepth(pj)), dl = viewZ(rawDepth(pj + int2(-eo, 0))), dr = viewZ(rawDepth(pj + int2(eo, 0))), du = viewZ(rawDepth(pj + int2(0, eo))), dd = viewZ(rawDepth(pj + int2(0, -eo)));
            float edgeD = smoothstep(0.02, 0.06, (abs(dl + dr - 2.0 * d0) + abs(du + dd - 2.0 * d0)) / max(d0, 2.0)) * step(d0, 500.0);   // where depth breaks (a flat floor seen edge-on does not)
            float3 lw = float3(0.299, 0.587, 0.114);
            float l0 = dot(sceneAt(pj).rgb, lw);
            float edgeC = smoothstep(0.24, 0.6, abs(l0 - dot(sceneAt(pj + int2(-eo, 0)).rgb, lw)) + abs(l0 - dot(sceneAt(pj + int2(eo, 0)).rgb, lw)) + abs(l0 - dot(sceneAt(pj + int2(0, eo)).rgb, lw)) + abs(l0 - dot(sceneAt(pj + int2(0, -eo)).rgb, lw))) * 0.75;
            float edge = max(edgeD, sky ? 0.0 : edgeC) * (sky ? 1.0 : 1.0 - smoothstep(R7 - 3.0, R7 - 0.5, dist));
            col = lerp(col, float3(0.05, 0.03, 0.14), saturate(edge) * 0.96);
            if (imp > 0.0) col = lerp(float3(step(0.5, dot(col, lw)), step(0.5, dot(col, lw)), step(0.5, dot(col, lw))), float3(1, 1, 1), rays);   // the impact frame: stark black and white
            f2World(dec, decK, col, mask);
        }
        if (!sky) add += float3(1.0, 0.93, 0.65) * exp(-pow((dist - R7) / 0.9, 2.0)) * 1.2 * exp(-t * 0.7) * out7 * bubA;      // the wavefront
        // the title card
        f2Word(h, rd, C + float3(0, 2.3, 0), 0.26 * backOut((t - 0.4) / 0.2) * step(0.4, t) * step(t, 0.98), TX_EPISODE, TL_EPISODE, 0.0, float3(1.7, 1.7, 1.7), float3(0.01, 0.006, 0.03), 1.0);
        // you: wading in with lefts and rights, then down - and up
        float tU = 3.1, uU = t - tU;
        float nh = 0.0, jolt = 0.0, arm = 0.0, which = 0.0;
        [unroll] for (int i = 0; i < 6; i++) {
            float u = t - hitT[i];
            float wind = exp(-pow((u + 0.02) / 0.07, 2.0));
            if (wind > arm) { arm = wind; which = float(i % 2); }
            if (u < 0.0) continue;
            nh += 1.0; jolt += exp(-u * 12.0);
            float3 hp = C + float3(0, (h1(float(i) + seed) - 0.5) * 0.7, 0) + A * 0.3 + vf * -0.35;
            // the burst where it lands, and the word
            float2 q; float tp;
            float bs = backOut(u / 0.07) * step(u, 0.26);
            if (bs > 0.0 && f2Card(rd, hp, h.t, q, tp)) {
                float r = length(q) / (0.42 * bs), a = atan2(q.y, q.x);
                float ed = 0.62 + 0.38 * step(0.5, frac(a / 6.2832 * 11.0 + float(i) * 0.3));
                if (r < ed) { h.t = tp; h.c = r > ed - 0.14 ? float3(0.01, 0.006, 0.03) : (r < ed * 0.45 ? float3(2.0, 2.0, 1.8) : hex3(255, 214, 40) * 1.5); h.hit = true; h.part = -1; }
            }
            int wo = i == 0 ? TX_POW : (i == 1 ? TX_BAM : (i == 2 ? TX_WHAM : (i == 3 ? TX_CRACK : (i == 4 ? TX_SMASH : TX_BOOM))));
            int wl = i == 0 ? TL_POW : (i == 1 ? TL_BAM : (i == 2 ? TL_WHAM : (i == 3 ? TL_CRACK : (i == 4 ? TL_SMASH : TL_BOOM))));
            float sd2 = (i % 2 == 0) ? 1.0 : -1.0;
            if (u < 0.5) f2Word(h, rd, hp + float3(0, 0.75 + u * 0.5, 0) - A * (0.2 + 0.5 * sd2) - vf * 0.3, 0.34 * backOut(u / 0.1) * (1.0 - ease((u - 0.38) / 0.12)), wo, wl, 0.22 * sd2, (i % 2 == 0) ? hex3(255, 226, 50) * 1.6 : float3(1.9, 1.9, 1.9), (i % 2 == 0) ? float3(0.01, 0.006, 0.03) : hex3(210, 30, 40), 0.0);
        }
        float crouch = ease((t - 2.62) / 0.3) * (1.0 - ease(uU / 0.08));
        float rise = easeOut(uU / 0.25) * step(0.0, uU);
        if (t > 0.55 && t < 4.3) {
            DFx fb = fg; fb.ash = max(1.0 - saturate((t - 0.55) / 0.3) * 1.15, ease((t - 3.9) / 0.4));
            float3 Op = feet + A * (1.05 - 0.12 * arm) + float3(0, -0.28 * crouch + 0.7 * rise * (1.0 - ease((uU - 0.3) / 0.5)), 0);
            float3x3 Rp = mul(rotY(atan2(-A.x, -A.z)), rotX(0.12 + 0.3 * crouch - 0.35 * rise));
            float2 aR = float2(-0.5 - 1.1 * arm * (1.0 - which) - 2.5 * rise + 0.6 * crouch, -0.2), aL = float2(-0.5 - 1.1 * arm * which, 0.2);
            dsPosed(h, rd, Op, Rp, 1.0, float2(0.1, 0), aR, aL, float2(-0.45 - 0.5 * crouch, 0), float2(0.35 + 0.3 * crouch, 0), 63, fb);
        }
        f2Word(h, rd, feet + A * 1.05 + float3(0, 2.45, 0), 0.3 * backOut((t - 2.6) / 0.15) * step(2.6, t) * step(t, tU), TX_BREAKER, TL_BREAKER, 0.0, float3(1.9, 1.9, 1.9), hex3(40, 60, 220), 0.0);
        // the mob: rocked by every blow, then launched - up, away, turning, until it is only a glint in the painted sky
        float3 sgo = normalize(float3(0, 1.0, 0) - A * 0.28 + vf * 0.2);
        if (uU < 0.0) f2Body(h, rd, feet - A * 0.06 * nh - A * 0.08 * min(jolt, 1.0), mul(RY, mul(rotX(0.0), rotZ(0.0))), 1.0, one, fx);
        else if (uU < 0.75) f2Body(h, rd, feet - A * 0.36 + sgo * (uU * 26.0), mul(RY, mul(rotX(uU * 14.0), rotZ(uU * 5.0))), 1.0, one, fx);
        if (uU >= 0.0) {
            float2 q; float tp;
            float3 hp = C + float3(0, 0.5, 0) + A * 0.3 - vf * 0.35;
            float bs = backOut(uU / 0.08) * step(uU, 0.4);
            if (bs > 0.0 && f2Card(rd, hp, h.t, q, tp)) {
                float r = length(q) / (0.8 * bs), a = atan2(q.y, q.x);
                float ed = 0.6 + 0.4 * step(0.5, frac(a / 6.2832 * 13.0));
                if (r < ed) { h.t = tp; h.c = r > ed - 0.1 ? float3(0.01, 0.006, 0.03) : (r < ed * 0.5 ? float3(2.2, 2.2, 2.0) : hex3(255, 80, 40) * 1.6); h.hit = true; h.part = -1; }
            }
            if (uU < 0.8) f2Word(h, rd, C + float3(0, 1.9 + uU * 0.4, 0) - vf * 0.6, 0.5 * backOut(uU / 0.12) * (1.0 - ease((uU - 0.65) / 0.15)), TX_UPPERCUT, TL_UPPERCUT, -0.12, hex3(255, 60, 40) * 1.7, float3(2.0, 1.9, 1.2), 0.0);
            add += float3(1.0, 0.9, 0.6) * dsGlowSeg(rd, C, C + sgo * min(uU * 26.0, 30.0), 0.12, h.t) * exp(-uU * 5.0) * 1.5;      // the streak it leaves
            // the star
            float us = uU - 0.75;
            if (us > 0.0 && us < 0.7) {
                float3 sp = sgo * 60.0 + C;
                float3 rt = normalize(cross(rd, float3(0, 1, 0)));
                float3 sv = rd * dot(sp, rd) - sp;
                float sx = dot(sv, rt), sy = sv.y;
                float tw = sin(us / 0.7 * 3.1416) * (0.7 + 0.3 * sin(us * 40.0));
                add += float3(1.0, 1.0, 0.9) * (exp(-(sx * sx + sy * sy) / 0.12) * 2.0 + exp(-sy * sy / 0.02) * exp(-sx * sx / 6.0) + exp(-sx * sx / 0.02) * exp(-sy * sy / 6.0)) * tw * 2.0 * step(0.0, dot(sp, rd));
            }
        }
        f2Word(h, rd, C + float3(0, 2.1, 0), 0.24 * backOut((t - 3.75) / 0.2) * step(3.75, t) * step(t, 4.55), TX_TBC, TL_TBC, 0.0, float3(1.7, 1.7, 1.7), float3(0.01, 0.006, 0.03), 1.0);
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
