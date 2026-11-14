// ---- death scenes 16-64: Finishers 2 (by kyryi / dulimo, MCreator; used with permission) - its 40 death effects and
//      its special finishers, rebuilt for Bedrock / Renderer Foundation by Novak. Scene k = code - 16:
//       0 Hologram   1 Brickfall  2 Rocket     3 Black Hole  4 Lightning   5 Ice Shatter  6 Glitch    7 Balloon Pop
//       8 Sand Decay 9 Origami   10 Flower Bloom 11 Fire Burst 12 Time Freeze 13 Pixel Burst 14 Portal 15 Ink Splash
//      16 Meteor    17 Orbital Laser 18 Hydraulic Press 19 Home Run 20 Twister 21 Petrify 22 Golden Statue 23 Gift Box
//      24 Jelly     25 Lava Melt 26 Soap Bubble 27 Shadow Drag 28 Soul Release 29 Bat Swarm 30 Constellation
//      31 Synthwave 32 Manga 33 Crystal Burst 34 Giant Sword 35 Shattered Mirror 36 Kaleidoscope 37 Fireflies
//      38 Abduction 39 Substitution
//      40 World Slash 41 Judgment of Time 42 Starfall 43 Void 44 Dragon Vein 45 Tsunami 46 Thousand Blades 47 Anime
//      48 Chrono Fracture (death_scenes_f2s.hlsli)
//      The mod plays these on mobs, from its own renderers; here each is a scene round the victim (their skin), with
//      the same beats as the mod's descriptions and code. Its full-screen world changes (the specials) play inside a
//      bubble round the kill instead, and no camera moves: on a server they would take the screen away mid-fight.
//      Everything is drawn in 1/16-block texels like the rest of the pool; the body's materials are death_mats.hlsli.

float f2Cull(int k) {   // how far from the feet a scene can draw (blocks)
    if (k == 2 || k == 16 || k == 17 || k == 19 || k == 30 || k == 38) return 16.0;   // the sky ones
    if (k >= 40) return 9.0;
    if (k == 3 || k == 20 || k == 26 || k == 34 || k == 7) return 7.0;
    return 4.8;
}
void f2Body(inout DHit h, float3 rd, float3 O, float3x3 R, float s, float3 sq, DFx f) {
    gS3 = sq; dsBody(h, rd, O, R, s, -99.0, 99.0, f); gS3 = float3(1, 1, 1);
}
// cubes thrown out of the body's volume (the mod voxelises the mob and throws its voxels)
void f2Cubes(inout DHit h, float3 rd, float3 C, float u, int n, float seed, float v0, float up, float g, float L, float sz,
             float3 cA, float3 cB, float floorY, float3 emis) {
    dsBurst(h, rd, C, u, n, seed, v0, up, g, L, sz, cA, cB, floorY, emis);
}
float3 f2Conf(float i) { return hsv3(frac(i * 0.618), 0.75, 1.0); }   // confetti colours
// a point of light (additive), depth-tested
float f2Glow(float3 rd, float3 P, float r, float tMax) { return fnFlash(rd, P, r, tMax); }
// a ring on the ground (additive light): centre C, radius r, width w
float f2Ring(bool onG, float3 S, float3 C, float r, float w) {
    if (!onG) return 0.0;
    float d = length(S.xz - C.xz);
    return exp(-pow((d - r) / w, 2.0));
}

#if !defined(DS_GROUP) || DS_GROUP == 4
#include "death_scenes_f2s.hlsli"
#endif

#if !defined(DS_GROUP) || DS_GROUP == 2
void f2A(int k, float t, float3 o, float3 rd, float3 feet, float yaw, float view, float seed, float3 team, float3 glow,
              bool onG, float3 S, float gr, inout DHit h, inout float3 add, inout float3 dec, inout float decK, inout DFx fx) {
    float3x3 RY = rotY(yaw);
    float3 fwd = mul(RY, float3(0, 0, 1)), side = mul(RY, float3(1, 0, 0));
    float3 C = feet + float3(0, 0.95, 0);                 // the body's middle
    float  floorY = feet.y;
    fx.hurt = saturate(1.0 - t * 4.0);                    // the killing hit, a red flash
    float3 one = float3(1, 1, 1);

    [branch] if (k == 0) {
        // HOLOGRAM: a blue hologram body breaks apart into glowing cubes
        fx.mat = M_HOLO; fx.matC = float3(0.25, 0.65, 1.0); fx.matK = ease((t - 0.15) / 0.35);
        fx.ash = ease((t - 1.5) / 0.9);
        f2Body(h, rd, feet, RY, 1.0, one, fx);
        f2Cubes(h, rd, C, t - 1.5, 26, seed, 0.9, 1.4, -1.2, 1.8, 0.035, float3(0.3, 0.7, 1.0), float3(0.6, 0.9, 1.0), -1e5, float3(0.3, 0.7, 1.2));
        add += float3(0.25, 0.6, 1.0) * f2Ring(onG, S, feet, 0.55, 0.06) * saturate(t * 3.0) * (1.0 - ease((t - 2.2) / 0.6)) * 0.8;
    } else if (k == 1) {
        // BRICKFALL: the body topples away from the attacker (a falling tree), turns to brick and crumbles
        float fall = pow(saturate((t - 0.15) / 0.85), 2.2) * 1.5708;
        fx.mat = M_BRICK; fx.matK = ease((t - 0.2) / 0.6);
        float3x3 Rb = mul(RY, rotX(-fall));
        if (t < 1.02) f2Body(h, rd, feet, Rb, 1.0, one, fx);
        float3 land = feet + mul(RY, float3(0, 0.25, -0.95));
        f2Cubes(h, rd, land, t - 1.0, 28, seed, 1.8, 2.4, 14.0, 1.6, 0.07, hex3(150, 58, 38), hex3(118, 44, 30), floorY, 0.0);
        f2Cubes(h, rd, land, t - 1.0, 10, seed + 9.0, 1.2, 0.8, -0.8, 1.2, 0.08, hex3(196, 184, 170), hex3(160, 150, 140), -1e5, 0.0);
    } else if (k == 2) {
        // ROCKET: the body spins skyward like a rocket and bursts into fireworks
        float ut = max(t - 0.35, 0.0);
        float up = 0.6 * ut * ut * ut * 4.0 + 1.5 * ut * ut;
        float spin = t * t * 9.0;
        float3 O = feet + float3(0, up, 0);
        if (t < 2.2) f2Body(h, rd, O, mul(RY, rotY(spin)), 1.0, one, fx);
        // the exhaust: fire at its feet, a smoke trail behind
        if (t > 0.3 && t < 2.2) {
            add += float3(1.0, 0.55, 0.12) * f2Glow(rd, O + float3(0, -0.1, 0), 0.35, h.t) * 1.4;
            add += float3(1.0, 0.85, 0.5) * f2Glow(rd, O + float3(0, -0.05, 0), 0.15, h.t) * 1.6;
            [unroll] for (int i = 0; i < 6; i++) {
                float ti = t - 0.08 * float(i + 1);
                float ui = max(ti - 0.35, 0.0); float yi = 0.6 * ui * ui * ui * 4.0 + 1.5 * ui * ui;
                f2Cubes(h, rd, feet + float3(0, yi, 0), 0.25 + 0.08 * float(i), 1, seed + float(i) * 3.0, 0.3, -0.2, 0.0, 1.2, 0.09 + 0.02 * float(i), hex3(200, 200, 200), hex3(150, 150, 150), -1e5, 0.0);
            }
        }
        // the burst: a ball of coloured sparks
        float ub = t - 2.2;
        float3 B = feet + float3(0, 0.6 * 1.85 * 1.85 * 1.85 * 4.0 + 1.5 * 1.85 * 1.85, 0);
        [unroll] for (int i = 0; i < 3; i++)
            f2Cubes(h, rd, B, ub - 0.05 * float(i), 18, seed + float(i) * 21.0, 4.5, 0.0, 3.0, 1.6, 0.06, f2Conf(seed + float(i)), f2Conf(seed + float(i) + 0.3), -1e5, f2Conf(seed + float(i)) * 1.6);
        add += f2Conf(seed) * f2Glow(rd, B, 2.5, h.t) * exp(-max(ub, 0.0) * 4.0) * step(0.0, ub) * 0.7;
    } else if (k == 3) {
        // BLACK HOLE: a black hole opens above and swallows the body in a spiral
        float3 H = feet + float3(0, 3.2, 0);
        float open = ease((t - 0.2) / 0.5) * (1.0 - ease((t - 2.6) / 0.4));
        float rH = 0.55 * open;
        // the hole: a black ball, a hot accretion ring turning round it
        if (rH > 0.01) {
            float tc = dot(H, rd), d2 = dot(H, H) - tc * tc;
            if (d2 < rH * rH && tc < h.t) { h.t = tc - sqrt(rH * rH - d2); h.c = float3(0, 0, 0); h.hit = true; }
            float3 n = float3(0, 1, 0.25);
            float tp = dot(H, n) / dot(rd, n);
            if (tp > 0.0 && tp < h.t) {
                float3 P = rd * tp - H; float r = length(P);
                float a = atan2(P.z, P.x) + t * 4.0;
                float ring = smoothstep(rH * 1.1, rH * 1.4, r) * smoothstep(rH * 3.2, rH * 1.6, r) * (0.6 + 0.4 * sin(a * 6.0 - r * 9.0));
                add += lerp(float3(1.0, 0.45, 0.9), float3(0.5, 0.3, 1.0), saturate((r - rH) / (rH * 2.0))) * ring * 1.2;
            }
            add += float3(0.6, 0.35, 1.0) * f2Glow(rd, H, rH * 2.5, h.t) * 0.25 * open;
        }
        // the body: lifted, turned in a tightening spiral, stretched thin and shrunk into it
        float pull = ease((t - 0.5) / 1.6);
        float a = pull * 9.0, r = 0.9 * (1.0 - pull) * sin(pull * 3.14159);
        float3 O = lerp(feet, H - float3(0, 0.6, 0), pull) + float3(cos(a) * r, 0, sin(a) * r);
        float s = 1.0 - 0.95 * pull;
        if (pull < 0.99) f2Body(h, rd, O, mul(RY, mul(rotY(a), rotZ(pull * 1.2))), s, float3(1.0 - 0.5 * pull, 1.0 + 1.4 * pull, 1.0 - 0.5 * pull), fx);
        fnPoof(h, rd, H, t - 2.6, seed, 10, 0.06);
    } else if (k == 4) {
        // LIGHTNING STRIKE: lightning strikes, chars the body and crumbles it to ash
        float3 top = feet + float3(0.6, 14.0, 0.3);
        [unroll] for (int s2 = 0; s2 < 2; s2++) {
            float ts = 0.15 + 0.35 * float(s2);
            float on = step(ts, t) * step(t, ts + 0.12);
            if (on > 0.0) {
                float3 a0 = top;
                [unroll] for (int i = 0; i < 7; i++) {
                    float f1 = float(i + 1) / 7.0;
                    float3 a1 = lerp(top, feet + float3(0, 1.8, 0), f1) + (f1 < 0.99 ? float3(h1(seed + float(i) + 10.0 * float(s2)) - 0.5, 0, h1(seed + float(i) * 3.0 + float(s2)) - 0.5) * 1.6 : float3(0, 0, 0));
                    add += float3(0.8, 0.9, 1.3) * (dsGlowSeg(rd, a0, a1, 0.05, h.t) * 2.5 + dsGlowSeg(rd, a0, a1, 0.25, h.t) * 0.5);
                    a0 = a1;
                }
                fx.flash = 1.0;
            }
        }
        add += float3(0.8, 0.9, 1.2) * f2Glow(rd, C, 2.5, h.t) * exp(-max(t - 0.15, 0.0) * 5.0) * step(0.15, t);
        fx.charK = ease((t - 0.2) / 0.3); fx.ash = ease((t - 1.4) / 1.4);
        f2Body(h, rd, feet, RY, 1.0, one, fx);
        f2Cubes(h, rd, C + float3(0, 0.3, 0), t - 1.5, 18, seed, 0.6, 0.6, -0.5, 2.0, 0.035, hex3(60, 56, 54), hex3(30, 28, 28), -1e5, 0.0);
        if (onG) { float sc2 = exp(-gr * gr / 0.5) * saturate(t * 6.0) * (1.0 - ease((t - 4.0) / 0.7)); dec = (decK > 0.0 ? dec : o) * (1.0 - 0.75 * sc2); decK = 1.0; }
    } else if (k == 5) {
        // ICE SHATTER: the body freezes solid, then shatters into ice shards
        fx.mat = M_ICE; fx.matK = ease((t - 0.1) / 0.6);
        if (t < 1.55) f2Body(h, rd, feet, RY, 1.0, one, fx);
        f2Cubes(h, rd, C, t - 1.55, 34, seed, 3.0, 2.0, 14.0, 1.6, 0.05, hex3(180, 220, 245), hex3(230, 245, 255), floorY, float3(0.25, 0.4, 0.55));
        add += float3(0.7, 0.9, 1.0) * f2Glow(rd, C, 1.2, h.t) * exp(-max(t - 1.55, 0.0) * 6.0) * step(1.55, t) * 0.6;
        if (onG) { float fr = smoothstep(0.9, 0.3, gr) * ease((t - 0.1) / 0.5) * (1.0 - ease((t - 3.8) / 0.8)); dec = lerp(decK > 0.0 ? dec : o, float3(0.85, 0.93, 1.0), fr * 0.7); decK = 1.0; }
    } else if (k == 6) {
        // GLITCH: the body stutters with RGB glitches and dissolves into pixels
        float st = floor(t * 12.0);
        float jig = step(0.55, h1(st + seed)) * step(t, 1.8);
        float3 off = (float3(h1(st * 1.3 + seed), 0, h1(st * 2.1 + seed)) - 0.5) * 0.3 * jig;
        fx.mat = M_GLITCH; fx.matK = saturate(t * 3.0);
        fx.ash = ease((t - 1.2) / 1.0);
        f2Body(h, rd, feet + off, RY, 1.0, float3(1.0 + 0.25 * jig * (h1(st + 5.0) - 0.5), 1.0, 1.0), fx);
        f2Cubes(h, rd, C, t - 1.2, 30, seed, 0.8, 0.9, -0.3, 1.6, 0.03, hex3(255, 40, 120), hex3(40, 230, 255), -1e5, float3(0.8, 0.5, 1.0));
    } else if (k == 7) {
        // BALLOON POP: the body inflates into a balloon, floats up and pops into confetti
        float inf = ease((t - 0.15) / 0.6);
        float fl = max(t - 0.75, 0.0);
        float3 O = feet + float3(sin(fl * 2.5) * 0.25, fl * fl * 0.9 + fl * 0.4, 0);
        fx.mat = M_JELLY; fx.matC = hex3(235, 60, 70); fx.matK = inf * 0.75;
        if (t < 2.4) {
            f2Body(h, rd, O, mul(RY, rotZ(sin(fl * 2.0) * 0.15)), 1.0, float3(1.0 + 0.9 * inf, 1.0 + 0.25 * inf, 1.0 + 1.6 * inf), fx);
            add += float3(0.9, 0.9, 0.9) * dsGlowSeg(rd, O, O - float3(0, 0.9, 0), 0.01, h.t) * 0.6 * inf;   // the string
        }
        float3 P = O + float3(0, 1.0, 0);
        [unroll] for (int i = 0; i < 3; i++)
            f2Cubes(h, rd, P, t - 2.4, 14, seed + float(i) * 7.0, 2.6, 1.0, 4.0, 1.8, 0.035, f2Conf(seed + float(i) * 0.37), f2Conf(seed + float(i) * 0.37 + 0.5), floorY, f2Conf(float(i)) * 0.3);
        add += float3(1, 1, 1) * f2Glow(rd, P, 0.8, h.t) * exp(-max(t - 2.4, 0.0) * 12.0) * step(2.4, t) * 1.2;
    } else if (k == 8) {
        // SAND DECAY: the body turns to sand and is blown away by the wind
        fx.mat = M_SAND; fx.matK = ease((t - 0.1) / 0.6);
        fx.ash = ease((t - 0.9) / 1.6);
        f2Body(h, rd, feet, RY, 1.0, one, fx);
        float3 wind = side * 2.2;
        [unroll] for (int i = 0; i < 4; i++) {
            float ui = t - 0.9 - 0.35 * float(i);
            float3 P = feet + float3(0, 1.7 - 0.4 * float(i), 0) + wind * max(ui, 0.0) * 0.0;
            dsBurst(h, rd, P + wind * saturate(ui) * 0.9, ui, 10, seed + float(i) * 11.0, 0.5, 0.3, 1.5, 2.0, 0.025, hex3(200, 164, 110), hex3(160, 128, 80), floorY, 0.0);
        }
        if (onG) { float pile = smoothstep(0.5, 0.15, length(S.xz - feet.xz - side.xz * 0.2)) * ease((t - 1.0) / 1.5) * (1.0 - ease((t - 4.0) / 0.8)); dec = lerp(decK > 0.0 ? dec : o, pow(hex3(200, 164, 110), 1.0 / 2.2), pile * 0.8); decK = 1.0; }
    } else if (k == 9) {
        // ORIGAMI: the body folds into paper planes that glide away
        fx.mat = M_PAPER; fx.matK = ease((t - 0.1) / 0.4);
        float fold = ease((t - 0.6) / 0.6);
        if (t < 1.25) f2Body(h, rd, feet, RY, 1.0, float3(1.0 - 0.6 * fold, 1.0 - 0.5 * fold, 1.0 - 0.8 * fold), fx);
        [loop] for (int i = 0; i < 9; i++) {   // planes: a flat white dart each, banking away on a curve
            float ui = t - 1.2 - 0.04 * float(i);
            if (ui < 0.0 || ui > 2.8) continue;
            float a0 = h1(seed + float(i)) * 6.2832, curve = (h1(seed + float(i) * 3.0) - 0.5) * 1.2;
            float a = a0 + curve * ui;
            float3 P = C + float3(cos(a), 0, sin(a)) * ui * 1.6 + float3(0, ui * 0.8 - ui * ui * 0.12 + 0.12 * sin(ui * 5.0 + float(i)), 0);
            float3x3 Rp = mul(rotY(-a + 1.5708), rotZ(sin(ui * 4.0 + float(i)) * 0.4));
            dsBox(h, rd, P, float3(0.05, 0.006, 0.12), Rp, hex3(236, 234, 226), 0.0);
            dsBox(h, rd, P + mul(Rp, float3(0, -0.02, 0)), float3(0.008, 0.02, 0.1), Rp, hex3(220, 218, 210), 0.0);
        }
    } else if (k == 10) {
        // FLOWER BLOOM: vines and flowers bloom over it, then scatter as petals
        float grow = ease((t - 0.15) / 0.9);
        fx.ash = ease((t - 1.7) / 0.8);
        f2Body(h, rd, feet, RY, 1.0, one, fx);
        [unroll] for (int i = 0; i < 5; i++) {   // vines: green lines spiralling up the body
            float a = float(i) * 1.2566 + seed;
            float3 p0 = feet + float3(cos(a) * 0.32, 0.0, sin(a) * 0.32);
            [unroll] for (int s2 = 0; s2 < 4; s2++) {
                float f0 = float(s2) / 4.0, f1 = float(s2 + 1) / 4.0;
                if (f0 > grow) break;
                float3 q0 = feet + float3(cos(a + f0 * 2.5) * 0.33, f0 * 1.9 * grow, sin(a + f0 * 2.5) * 0.33);
                float3 q1 = feet + float3(cos(a + f1 * 2.5) * 0.33, min(f1, grow) * 1.9 * grow, sin(a + f1 * 2.5) * 0.33);
                add += float3(0.15, 0.6, 0.15) * dsGlowSeg(rd, q0, q1, 0.02, h.t) * 1.2 * (1.0 - fx.ash);
            }
            float3 fl = feet + float3(cos(a + 2.0) * 0.36, 1.2 + 0.15 * float(i), sin(a + 2.0) * 0.36);
            float bs = ease((t - 0.7 - 0.08 * float(i)) / 0.3) * (1.0 - fx.ash);
            if (bs > 0.0) { dsBox(h, rd, fl, float3(0.07, 0.025, 0.07) * bs, rotY(float(i)), i % 2 == 0 ? hex3(240, 120, 170) : hex3(250, 210, 90), 0.0);
                            dsBox(h, rd, fl + float3(0, 0.02, 0), float3(0.025, 0.02, 0.025) * bs, RY, hex3(250, 230, 120), 0.0); }
        }
        f2Cubes(h, rd, C, t - 1.75, 30, seed, 1.0, 0.8, 0.6, 2.2, 0.03, hex3(240, 130, 180), hex3(255, 200, 220), floorY, 0.0);
    } else if (k == 11) {
        // FIRE BURST: the body burns to a glow, then falls apart into embers and smoke
        fx.charK = ease((t - 0.2) / 0.9);
        fx.mat = M_LAVA; fx.matK = ease((t - 0.6) / 0.6) * 0.8;
        fx.ash = ease((t - 1.7) / 0.9);
        f2Body(h, rd, feet, RY, 1.0, one, fx);
        float fl = saturate((t - 0.1) / 0.3) * (1.0 - ease((t - 2.4) / 0.5));
        [unroll] for (int i = 0; i < 8; i++) {   // flames: tongues of light licking up round it
            float a = float(i) * 0.785 + seed, ph = frac(t * 2.2 + h1(seed + float(i)));
            float3 b0 = feet + float3(cos(a) * 0.3, ph * 1.4, sin(a) * 0.3);
            add += lerp(float3(1.0, 0.75, 0.25), float3(1.0, 0.25, 0.05), ph) * dsGlowSeg(rd, b0, b0 + float3(0, 0.45, 0), 0.06, h.t) * fl * (1.0 - ph) * 1.3;
        }
        add += float3(1.0, 0.45, 0.1) * f2Glow(rd, C, 1.3, h.t) * fl * 0.35;
        f2Cubes(h, rd, C, t - 1.7, 26, seed, 0.9, 1.6, -0.8, 1.8, 0.03, hex3(255, 120, 30), hex3(255, 200, 80), -1e5, float3(1.4, 0.5, 0.1));
        f2Cubes(h, rd, C + float3(0, 0.6, 0), t - 1.8, 10, seed + 5.0, 0.4, 1.2, -0.4, 2.0, 0.09, hex3(60, 58, 56), hex3(100, 98, 96), -1e5, 0.0);
    } else if (k == 12) {
        // TIME FREEZE: a clock stops time, then the body shatters in slow motion
        float3 K = feet + float3(0, 2.6, 0) - fwd * 0.2;
        float show = ease((t - 0.1) / 0.3) * (1.0 - ease((t - 3.0) / 0.4));
        float3x3 RK = mul(rotY(atan2(-K.x, -K.z)), rotX(1.5708));          // the clock faces the camera
        if (show > 0.0) {
            float3 n = normalize(-K * float3(1, 0, 1));
            float tp = dot(K, n) / dot(rd, n);
            if (tp > 0.0 && tp < h.t) {
                float3 P = rd * tp - K;
                float3 u = normalize(cross(float3(0, 1, 0), n)), v = float3(0, 1, 0);
                float2 q = float2(dot(P, u), dot(P, v)) / (0.9 * show);
                float r = length(q);
                if (r < 1.0) {
                    float tick = 0.0;
                    [unroll] for (int i = 0; i < 12; i++) { float a = float(i) * 0.5236; float2 d = float2(sin(a), cos(a)); tick = max(tick, step(0.82, dot(q, d)) * step(abs(q.x * d.y - q.y * d.x), 0.04)); }
                    float frozen = min(t, 0.6);                                   // the hands stop when time does
                    float a1 = frozen * 9.0, a2 = frozen * 0.8;
                    float2 d1 = float2(sin(a1), cos(a1)), d2 = float2(sin(a2), cos(a2));
                    float hand = step(abs(q.x * d1.y - q.y * d1.x), 0.03) * step(0.0, dot(q, d1)) * step(dot(q, d1), 0.8) + step(abs(q.x * d2.y - q.y * d2.x), 0.045) * step(0.0, dot(q, d2)) * step(dot(q, d2), 0.5);
                    float rim = step(0.9, r);
                    float3 cc = lerp(float3(0.75, 0.82, 0.95) * 0.25, float3(1.0, 0.92, 0.7), saturate(tick + hand + rim));
                    add += cc * show * 0.9;
                }
            }
        }
        fx.mat = M_STONE; fx.matK = 0.0;
        float fr = step(0.6, t);
        if (t < 2.0) f2Body(h, rd, feet, mul(RY, rotX(-0.25 * saturate(t / 0.6))), 1.0, one, fx);   // knocked back, then held
        // time comes back slowly: the body shatters into slow pieces that hang and drift
        f2Cubes(h, rd, C, (t - 2.0) * 0.45, 30, seed, 1.5, 0.6, 1.5, 0.9, 0.05, team * 0.6, team * 0.3, -1e5, 0.0);
        if (fr > 0.0) {   // the world round it greys and stops with it
            float fk = saturate(1.0 - length(S - C) / 4.5) * (1.0 - ease((t - 2.0) / 0.4));
            if (fk > 0.0) { dec = lerp(o, dot(o, float3(0.3, 0.55, 0.15)) * float3(0.85, 0.92, 1.05), 0.6); decK = max(decK, 1.0 + fk); }
        }
    } else if (k == 13) {
        // PIXEL BURST: the body becomes 8-bit pixels and hops away with coins and hearts
        fx.mat = M_PIXEL; fx.matK = step(0.2, t);
        if (t < 1.0) f2Body(h, rd, feet + float3(0, abs(sin(t * 9.0)) * 0.12 * step(0.2, t), 0), RY, 1.0, one, fx);
        [loop] for (int i = 0; i < 14; i++) {   // pixels hop away in arcs, bouncing
            float ui = t - 1.0 - 0.02 * float(i);
            if (ui < 0.0 || ui > 2.6) continue;
            float a = h1(seed + float(i)) * 6.2832, v = 0.8 + 0.8 * h1(seed + float(i) * 3.0);
            float hop = abs(sin(ui * 5.0 + float(i))) * 0.5 * exp(-ui * 0.6);
            float3 P = C + float3(cos(a), 0, sin(a)) * ui * v + float3(0, hop - 0.7 * saturate(ui * 2.0) + 0.1, 0);
            P.y = max(P.y, floorY + 0.06);
            float3 col = i % 5 == 0 ? hex3(250, 210, 40) : (i % 5 == 1 ? hex3(230, 30, 50) : team);
            dsBox(h, rd, P, float3(0.06, 0.06, 0.06) * (i % 5 == 0 ? float3(1, 1, 0.3) : 1.0), rotY(i % 5 == 0 ? ui * 8.0 : 0.0), col, i % 5 < 2 ? col * 0.6 : float3(0, 0, 0));
        }
    } else if (k == 14) {
        // PORTAL WARP: a gate opens on the floor and the body spins down into it
        float open = ease((t - 0.15) / 0.4) * (1.0 - ease((t - 2.4) / 0.4));
        float rP = 0.85 * open;
        if (onG) {
            float d = length(S.xz - feet.xz), a = atan2(S.z - feet.z, S.x - feet.x);
            if (d < rP) {
                float sw = 0.5 + 0.5 * sin(a * 3.0 + d * 14.0 - t * 12.0);
                dec = pow(lerp(float3(0.05, 0.0, 0.12), float3(0.55, 0.15, 0.95), sw * smoothstep(0.0, rP, d)), 1.0 / 2.2); decK = 1.0;
                add += float3(0.6, 0.2, 1.0) * sw * 0.25;
            }
            add += float3(0.75, 0.4, 1.0) * f2Ring(onG, S, feet, rP, 0.05) * open * 1.4;
        }
        float sink = ease((t - 0.5) / 1.4);
        fx.clipY = floorY + 0.01;
        f2Body(h, rd, feet - float3(0, sink * 2.1, 0), mul(RY, rotY(sink * sink * 14.0)), 1.0, one, fx);
        add += float3(0.7, 0.35, 1.0) * f2Glow(rd, feet + float3(0, 0.4, 0), 0.9, h.t) * open * 0.3;
    } else if (k == 15) {
        // INK SPLASH: the body melts into an ink puddle and splatters
        fx.mat = M_INK; fx.matK = ease((t - 0.1) / 0.4);
        float melt = ease((t - 0.45) / 0.9);
        fx.clipY = floorY + 0.005;
        if (t < 1.5) f2Body(h, rd, feet, RY, 1.0, float3(1.0 + 0.8 * melt, 1.0 - 0.95 * melt, 1.0 + 0.8 * melt), fx);
        float rp = 0.85 * ease((t - 0.5) / 1.0);
        if (onG) {
            float2 gq = floor((S.xz - feet.xz) * 16.0) / 16.0;
            float wob = 0.12 * sin(atan2(gq.y, gq.x) * 5.0 + seed) + 0.08 * sin(atan2(gq.y, gq.x) * 11.0);
            float ink = step(length(gq), rp * (1.0 + wob)) * (1.0 - ease((t - 4.0) / 0.7));
            float3 io = pow(float3(0.02, 0.02, 0.03), 1.0 / 2.2) + step(0.97, hash12(floor(gq * 16.0))) * 0.25;
            if (ink > 0.0) { dec = lerp(decK > 0.0 ? dec : o, io, ink); decK = 1.0; }
        }
        f2Cubes(h, rd, feet + float3(0, 0.15, 0), t - 1.3, 18, seed, 2.4, 2.2, 14.0, 1.0, 0.03, hex3(8, 8, 12), hex3(20, 20, 28), floorY, 0.0);
    } else if (k == 16) {
        // METEOR STRIKE: a burning meteor crashes down on the mob and blasts it apart
        float tI = 1.0;
        float3 dirM = normalize(float3(0.45, -1.0, 0.25));
        float3 M = feet + float3(0, 0.5, 0) - dirM * max(tI - t, 0.0) * 14.0;
        if (t < tI) {
            float rM = 0.38;
            float3 Mc = M; float tc = dot(Mc, rd), d2 = dot(Mc, Mc) - tc * tc;
            dsBox(h, rd, M, float3(rM, rM, rM), mul(rotY(t * 5.0), rotX(t * 3.0)), hex3(50, 30, 22), float3(1.4, 0.45, 0.1));
            add += float3(1.0, 0.5, 0.12) * f2Glow(rd, M, 0.9, h.t) * 0.9;
            add += float3(1.0, 0.45, 0.1) * dsGlowSeg(rd, M, M - dirM * 5.0, 0.28, h.t) * 0.6;
            add += float3(1.0, 0.9, 0.6) * dsGlowSeg(rd, M, M - dirM * 2.0, 0.08, h.t) * 1.0;
        }
        if (t < tI) f2Body(h, rd, feet, RY, 1.0, one, fx);
        float u = t - tI;
        add += float3(1.0, 0.6, 0.25) * f2Glow(rd, feet + float3(0, 0.6, 0), 2.2 * easeOut(saturate(u / 0.25)), h.t) * exp(-max(u, 0.0) * 4.0) * step(0.0, u) * 1.4;
        fnGround(onG, S, feet, tI, 3.8, 1.1, 2.8, seed, t, o, dec, decK, add, float3(1.0, 0.5, 0.15));
        fnDebris(h, rd, feet, u, seed, 1.8, floorY);
        f2Cubes(h, rd, C, u, 24, seed + 3.0, 4.0, 3.0, 14.0, 1.6, 0.06, team * 0.6, hex3(40, 20, 15), floorY, float3(0.6, 0.15, 0.03));
    } else if (k == 17) {
        // ORBITAL LASER: a beam from the sky vaporizes the mob
        float w = ease((t - 0.3) / 0.15) * (1.0 - ease((t - 1.6) / 0.3));
        float aim = saturate(t / 0.3);
        float3 top = feet + float3(0, 20.0, 0);
        if (aim > 0.0 && t < 0.3) add += float3(1.0, 0.2, 0.25) * dsGlowSeg(rd, top, feet, 0.012, h.t) * 1.5;   // the targeting line
        if (w > 0.0) {
            add += float3(0.4, 0.9, 1.0) * dsGlowSeg(rd, top, feet, 0.45 * w, h.t) * 0.5;
            add += float3(1.1, 1.2, 1.3) * dsGlowSeg(rd, top, feet, 0.16 * w, h.t) * 1.6;
            add += float3(0.6, 0.95, 1.0) * f2Ring(onG, S, feet, 0.55 + 0.1 * sin(t * 30.0), 0.08) * w;
        }
        fx.mat = M_HOLO; fx.matC = float3(1.2, 1.25, 1.3); fx.matK = ease((t - 0.35) / 0.3);
        fx.ash = ease((t - 0.6) / 0.8);
        f2Body(h, rd, feet, RY, 1.0, one, fx);
        f2Cubes(h, rd, C, t - 0.6, 20, seed, 0.7, 2.5, -1.0, 1.4, 0.03, hex3(160, 230, 255), hex3(255, 255, 255), -1e5, float3(0.8, 1.2, 1.4));
        if (onG) { float sc2 = exp(-gr * gr / 0.4) * saturate((t - 0.3) * 5.0); dec = (decK > 0.0 ? dec : o) * (1.0 - 0.8 * sc2); decK = 1.0; add += float3(1.0, 0.45, 0.1) * sc2 * exp(-max(t - 1.6, 0.0) * 1.5) * 0.6; }
    } else if (k == 18) {
        // HYDRAULIC PRESS: a giant press flattens the mob into scrap
        float drop = t < 0.75 ? ease(t / 0.75) * 0.0 : 1.0;
        float y = t < 0.55 ? 6.0 - 4.0 * ease(t / 0.55) : (t < 0.7 ? 2.0 - 1.9 * ease((t - 0.55) / 0.15) : (t < 1.6 ? 0.1 : 0.1 + 3.5 * ease((t - 1.6) / 0.8)));
        float flat = t < 0.6 ? 1.0 : max(0.08, (y - 0.0) / 1.95);
        flat = t > 0.7 ? 0.06 : flat;
        float3 Pp = feet + float3(0, y, 0);
        float3x3 RP = RY;
        if (t < 3.0) {   // the press: a steel ram on a piston column
            dsBox(h, rd, Pp + float3(0, 0.2, 0), float3(0.75, 0.2, 0.75), RP, hex3(70, 74, 80), 0.0);
            dsBox(h, rd, Pp + float3(0, 0.41, 0), float3(0.8, 0.02, 0.8), RP, hex3(240, 190, 40), 0.0);
            dsBox(h, rd, Pp + float3(0, 4.4, 0), float3(0.22, 4.0, 0.22), RP, hex3(150, 154, 160), 0.0);
        }
        if (t < 2.2) f2Body(h, rd, feet, RY, 1.0, float3(1.0 + 0.6 * (1.0 - flat), flat, 1.0 + 0.6 * (1.0 - flat)), fx);
        f2Cubes(h, rd, feet + float3(0, 0.08, 0), t - 0.7, 16, seed, 2.5, 1.0, 12.0, 0.8, 0.03, hex3(140, 140, 146), hex3(90, 92, 98), floorY, 0.0);
        f2Cubes(h, rd, feet + float3(0, 0.1, 0), t - 2.2, 18, seed + 4.0, 1.6, 2.4, 14.0, 1.4, 0.05, team * 0.5, hex3(120, 120, 126), floorY, 0.0);
        add += float3(1.0, 0.9, 0.7) * f2Glow(rd, feet + float3(0, 0.1, 0), 1.0, h.t) * exp(-max(t - 0.7, 0.0) * 8.0) * step(0.7, t) * 0.8;
    } else if (k == 19) {
        // HOME RUN: the mob is smacked into the sky and becomes a twinkling star
        float u = max(t - 0.15, 0.0);
        float3 dir = normalize(fwd * -1.0 + float3(0, 1.1, 0));
        float d = 14.0 * (1.0 - exp(-u * 1.3));
        float3 O = feet + dir * d;
        float s = max(1.0 - u * 0.45, 0.05);
        if (u < 1.9) f2Body(h, rd, O, mul(RY, mul(rotX(u * 18.0), rotZ(u * 7.0))), s, one, fx);
        add += float3(1, 1, 1) * f2Glow(rd, feet + float3(0, 1.2, 0), 0.9, h.t) * exp(-u * 10.0) * step(0.15, t) * 1.2;   // the crack of the bat
        fnCrit(h, rd, feet + float3(0, 1.2, 0), t - 0.15, seed, 14, 3.0);
        float tw = step(1.9, u) * (1.0 - ease((u - 3.4) / 0.5));
        float3 star = feet + dir * 14.0 * (1.0 - exp(-1.9 * 1.3));
        float sp = 0.7 + 0.3 * sin(t * 18.0);
        add += float3(1.0, 1.0, 0.85) * (f2Glow(rd, star, 0.3, h.t) * 2.0 + dsGlowSeg(rd, star - float3(0.9, 0, 0) * sp, star + float3(0.9, 0, 0) * sp, 0.02, h.t) + dsGlowSeg(rd, star - float3(0, 0.9, 0) * sp, star + float3(0, 0.9, 0) * sp, 0.02, h.t)) * tw;
    }
}
#endif
#if !defined(DS_GROUP) || DS_GROUP == 3
void f2B(int k, float t, float3 o, float3 rd, float3 feet, float yaw, float view, float seed, float3 team, float3 glow,
              bool onG, float3 S, float gr, inout DHit h, inout float3 add, inout float3 dec, inout float decK, inout DFx fx) {
    float3x3 RY = rotY(yaw);
    float3 fwd = mul(RY, float3(0, 0, 1)), side = mul(RY, float3(1, 0, 0));
    float3 C = feet + float3(0, 0.95, 0);                 // the body's middle
    float  floorY = feet.y;
    fx.hurt = saturate(1.0 - t * 4.0);                    // the killing hit, a red flash
    float3 one = float3(1, 1, 1);

    [branch] if (k == 20) {
        // TWISTER: a whirlwind lifts the mob and tears it apart
        float tw = ease((t - 0.1) / 0.4) * (1.0 - ease((t - 3.0) / 0.5));
        [loop] for (int i = 0; i < 26; i++) {   // the funnel: grey-white specks on a widening spiral
            float f = float(i) / 26.0;
            float y = f * 4.0, r = 0.25 + f * 1.1;
            float a = t * (9.0 - f * 4.0) + float(i) * 2.4;
            float3 P = feet + float3(cos(a) * r, y, sin(a) * r);
            dsBox(h, rd, P, float3(0.05, 0.05, 0.05) * tw, rotY(a), lerp(hex3(200, 200, 205), hex3(140, 140, 150), h1(float(i))), 0.0);
        }
        add += float3(0.7, 0.7, 0.75) * f2Glow(rd, feet + float3(0, 2.0, 0), 1.8, h.t) * tw * 0.08;
        float lift = ease((t - 0.4) / 1.4);
        float spin = t * t * 6.0;
        float3 O = feet + float3(0, lift * 2.4, 0);
        // torn apart: the parts fly off one by one
        [loop] for (int j = 0; j < 6; j++) {
            float tj = 1.6 + 0.12 * float(j);
            float uj = max(t - tj, 0.0);
            float a = spin + float(j) * 1.05;
            float3 Pj = O + mul(rotY(spin), kPC[j] * DPX) + float3(cos(a), 0.4, sin(a)) * uj * 3.0 + float3(0, -4.0 * uj * uj, 0);
            if (uj < 1.6) dsPart(h, rd, j, Pj, mul(rotY(spin + uj * 6.0), rotX(uj * 5.0 * float(j % 2))), 1.0, -99.0, 99.0, fx);
        }
    } else if (k == 21) {
        // PETRIFY: the mob turns to stone, cracks and crumbles
        fx.mat = M_STONE; fx.matK = ease((t - 0.15) / 0.8);
        fx.ash = ease((t - 1.9) / 1.2);
        f2Body(h, rd, feet, mul(RY, rotX(-0.12 * saturate(t / 0.2))), 1.0, one, fx);
        f2Cubes(h, rd, C + float3(0, 0.2, 0), t - 1.9, 26, seed, 0.6, 0.2, 9.0, 1.4, 0.05, hex3(110, 110, 112), hex3(80, 80, 82), floorY, 0.0);
    } else if (k == 22) {
        // GOLDEN STATUE: the mob turns to gold and bursts into coins
        fx.mat = M_GOLD; fx.matK = ease((t - 0.15) / 0.6);
        if (t < 1.8) f2Body(h, rd, feet, RY, 1.0, one, fx);
        add += float3(1.0, 0.85, 0.4) * f2Glow(rd, feet + float3(0.25, 1.6, 0.0), 0.25, h.t) * exp(-pow((t - 1.1) / 0.12, 2.0)) * 3.0;   // the glint
        [loop] for (int i = 0; i < 22; i++) {   // coins: flat gold discs spinning out
            float ui = t - 1.8;
            if (ui < 0.0 || ui > 2.4) break;
            float a = h1(seed + float(i)) * 6.2832, v = 1.4 + 2.0 * h1(seed + float(i) * 2.0), vy = 2.5 + 3.0 * h1(seed + float(i) * 5.0);
            float3 P = C + float3(cos(a) * v * ui, vy * ui - 7.0 * ui * ui, sin(a) * v * ui);
            if (P.y < floorY + 0.03) P.y = floorY + 0.03;
            dsBox(h, rd, P, float3(0.07, 0.07, 0.015), mul(rotY(a + ui * 10.0), rotX(ui * 7.0 * step(floorY + 0.031, P.y))), hex3(250, 200, 60), float3(0.4, 0.3, 0.05));
        }
    } else if (k == 23) {
        // GIFT BOX: wrapped into a present that pops open with confetti
        float wrap = ease((t - 0.2) / 0.5);
        float3 col = f2Conf(seed * 0.3);
        float pop = t - 1.7;
        if (t < 0.7) f2Body(h, rd, feet, RY, 1.0, one, fx);
        if (wrap > 0.0 && pop < 0.0) {   // the box closes round them, squeezes once
            float sq = 1.0 + 0.08 * sin(max(t - 0.7, 0.0) * 18.0) * exp(-max(t - 0.7, 0.0) * 3.0);
            float3 bs = float3(0.5, 0.5, 0.5) * wrap * sq;
            float3 B = feet + float3(0, bs.y, 0);
            float3 nn = float3(0, 0, 0);
            dsBox(h, rd, B, bs, RY, col, 0.0);
            dsBox(h, rd, B, bs * float3(0.22, 1.03, 1.03), RY, hex3(245, 240, 230), 0.0);
            dsBox(h, rd, B, bs * float3(1.03, 1.03, 0.22), RY, hex3(245, 240, 230), 0.0);
            dsBox(h, rd, B + float3(0, bs.y + 0.07, 0), float3(0.14, 0.07, 0.05) * wrap, mul(RY, rotZ(0.6)), hex3(245, 240, 230), 0.0);
            dsBox(h, rd, B + float3(0, bs.y + 0.07, 0), float3(0.14, 0.07, 0.05) * wrap, mul(RY, rotZ(-0.6)), hex3(245, 240, 230), 0.0);
        }
        // the lid flies off, confetti
        if (pop >= 0.0 && pop < 1.2) dsBox(h, rd, feet + float3(0, 1.05 + pop * 3.0 - 5.0 * pop * pop, 0), float3(0.53, 0.06, 0.53), mul(RY, rotX(pop * 6.0)), col, 0.0);
        [unroll] for (int i = 0; i < 3; i++)
            f2Cubes(h, rd, feet + float3(0, 0.9, 0), pop - 0.03 * float(i), 16, seed + float(i) * 5.0, 1.8, 3.2, 5.0, 2.0, 0.03, f2Conf(float(i) * 0.31), f2Conf(float(i) * 0.31 + 0.5), floorY, f2Conf(float(i)) * 0.3);
    } else if (k == 24) {
        // JELLY SPLIT: the mob turns to jelly, splits and pops
        fx.mat = M_JELLY; fx.matC = hex3(80, 230, 110); fx.matK = ease((t - 0.1) / 0.5);
        float wob = sin(t * 16.0) * 0.12 * exp(-max(t - 0.4, 0.0) * 1.5) * step(0.3, t);
        float sp = ease((t - 1.2) / 0.5);
        float3 sq = float3(1.0 + wob, 1.0 - wob, 1.0 + wob);
        if (t < 2.2) {
            gS3 = sq;
            [loop] for (int j = 0; j < 6; j++) {   // split down the middle: the halves lean apart
                float sd = (j == 2 || j == 4) ? -1.0 : ((j == 3 || j == 5) ? 1.0 : 0.0);
                float3 offs = side * sd * sp * 0.35;
                if (j < 2) {   // head and body split in two themselves
                    dsPart(h, rd, j, feet + offs + mul(RY, kPC[j] * DPX * sq) - side * sp * 0.12, mul(RY, rotZ(sp * 0.3)), 1.0, -99.0, 99.0, fx);
                } else dsPart(h, rd, j, feet + offs + mul(RY, kPC[j] * DPX * sq), mul(RY, rotZ(-sd * sp * 0.3)), 1.0, -99.0, 99.0, fx);
            }
            gS3 = one;
        }
        f2Cubes(h, rd, C, t - 2.2, 24, seed, 2.0, 2.0, 12.0, 1.2, 0.05, hex3(80, 230, 110), hex3(160, 255, 170), floorY, float3(0.1, 0.4, 0.15));
        if (onG) { float sp2 = step(length(S.xz - feet.xz), 0.6 * ease((t - 2.2) / 0.3)) * step(0.7, hash12(floor(S.xz * 16.0))) * (1.0 - ease((t - 4.0) / 0.7)); if (sp2 > 0.0) { dec = pow(hex3(80, 200, 100), 1.0 / 2.2); decK = 1.0; } }
    } else if (k == 25) {
        // LAVA MELT: melts into lava that flows over the blocks and cools
        fx.mat = M_LAVA; fx.matK = ease((t - 0.1) / 0.5);
        float melt = ease((t - 0.5) / 1.0);
        fx.clipY = floorY + 0.005;
        if (t < 1.55) f2Body(h, rd, feet, RY, 1.0, float3(1.0 + 0.6 * melt, 1.0 - 0.95 * melt, 1.0 + 0.6 * melt), fx);
        if (onG) {
            float2 gq = floor((S.xz - feet.xz) * 16.0) / 16.0;
            float a = atan2(gq.y, gq.x);
            float reach = 1.4 * ease((t - 0.7) / 1.6) * (1.0 + 0.25 * sin(a * 4.0 + seed) + 0.15 * sin(a * 9.0));
            float d = length(gq);
            if (d < reach) {
                float cool = ease((t - 2.2 - d * 0.4) / 1.4);
                float n = hash12(floor(gq * 16.0) + seed);
                float3 hot = lerp(float3(1.0, 0.85, 0.3), float3(1.0, 0.35, 0.05), saturate(d / max(reach, 0.01) + n * 0.3));
                float3 crust = float3(0.06, 0.05, 0.05) * (0.8 + 0.5 * n);
                dec = pow(lerp(hot * 0.9, crust, cool), 1.0 / 2.2) * (1.0 - ease((t - 4.0) / 0.7)) + (decK > 0.0 ? dec : o) * ease((t - 4.0) / 0.7); decK = 1.0;
                add += hot * (1.0 - cool) * 0.6 * (1.0 - ease((t - 4.0) / 0.7));
            }
        }
        add += float3(1.0, 0.4, 0.08) * f2Glow(rd, feet + float3(0, 0.2, 0), 1.2, h.t) * saturate(t * 2.0) * (1.0 - ease((t - 2.5) / 1.5)) * 0.4;
    } else if (k == 26) {
        // SOAP BUBBLE: sealed in a bubble that floats up and bursts
        float seal = ease((t - 0.15) / 0.4);
        float u = max(t - 0.6, 0.0);
        float3 B = feet + float3(sin(u * 1.7) * 0.3, 1.0 + u * 0.9 + u * u * 0.3, cos(u * 1.3) * 0.2);
        float rB = 1.15 * seal;
        bool popped = t > 2.7;
        if (!popped) {
            float s = 1.0 - 0.15 * seal;
            f2Body(h, rd, B - float3(0, 0.95 * s, 0), mul(RY, mul(rotZ(u * 0.6), rotX(u * 0.4))), s, one, fx);
            // the bubble: a thin film - rainbow sheen at the rim, a highlight
            float tc = dot(B, rd), d2 = dot(B, B) - tc * tc;
            if (d2 < rB * rB && tc - sqrt(max(rB * rB - d2, 0.0)) < h.t) {
                float rim = pow(saturate(sqrt(d2) / max(rB, 0.01)), 5.0);
                add += hsv3(frac(rim * 2.0 + t * 0.3 + sqrt(d2) * 2.0), 0.6, 1.0) * rim * 0.9 * seal;
                float3 hp = normalize(rd * (tc - sqrt(max(rB * rB - d2, 0.0))) - B);
                add += float3(1, 1, 1) * step(0.97, dot(hp, normalize(float3(-0.4, 0.7, -0.5)))) * 1.2 * seal;
            }
        }
        f2Cubes(h, rd, B, t - 2.7, 26, seed, 2.4, 0.0, 6.0, 0.9, 0.02, hex3(200, 230, 255), hex3(255, 255, 255), -1e5, float3(0.3, 0.4, 0.5));
        add += float3(1, 1, 1) * f2Glow(rd, B, rB, h.t) * exp(-max(t - 2.7, 0.0) * 10.0) * step(2.7, t) * 0.6;
        if (popped && t < 3.3) f2Body(h, rd, B - float3(0, 0.85, 0) - float3(0, 9.0 * (t - 2.7) * (t - 2.7), 0), RY, 0.85, one, fx);
        fnPoof(h, rd, B - float3(0, 0.8, 0) - float3(0, 3.2, 0) * 0.0, t - 3.3, seed + 3.0, 10, 0.06);
    } else if (k == 27) {
        // SHADOW DRAG: dark hands drag the mob into its own shadow
        float pool = ease((t - 0.1) / 0.4) * (1.0 - ease((t - 3.2) / 0.5));
        if (onG) {
            float2 gq = floor((S.xz - feet.xz) * 16.0) / 16.0;
            float d = length(gq) / (0.95 * pool + 0.001);
            if (d < 1.0) { dec = lerp(decK > 0.0 ? dec : o, float3(0.0, 0.0, 0.01), smoothstep(1.0, 0.5, d) * 0.97); decK = 1.0; }
        }
        float sink = ease((t - 1.0) / 1.5);
        fx.clipY = floorY + 0.01;
        f2Body(h, rd, feet - float3(0, sink * 2.0, 0), mul(RY, rotX(0.15 * sin(t * 9.0) * step(0.6, t) * (1.0 - sink))), 1.0, one, fx);
        [unroll] for (int i = 0; i < 5; i++) {   // the hands: black arms rising out of the pool, gripping the body
            float a = float(i) * 1.2566 + seed;
            float rise = ease((t - 0.5 - 0.1 * float(i)) / 0.4) * (1.0 - sink * 0.8);
            float3 b0 = feet + float3(cos(a) * 0.5, -0.1, sin(a) * 0.5);
            float3 b1 = feet + float3(cos(a) * 0.22, 0.4 + 0.9 * h1(seed + float(i)) - sink * 2.0, sin(a) * 0.22);
            float3 ax = b1 - b0;
            if (rise > 0.0) {
                float L = length(ax) * rise;
                float3 dir = normalize(ax);
                dsBox(h, rd, b0 + dir * L * 0.5, float3(0.06, L * 0.5, 0.06), basisOf(dir, 0.0), hex3(6, 6, 8), float3(0.15, 0.0, 0.2) * 0.2);
                dsBox(h, rd, b0 + dir * L, float3(0.09, 0.05, 0.09), basisOf(dir, 0.0), hex3(6, 6, 8), 0.0);
            }
        }
    } else if (k == 28) {
        // SOUL RELEASE: the soul rises as the body turns to ash
        fx.mat = M_STONE; fx.matK = ease((t - 0.2) / 0.5) * 0.85;
        fx.charK = ease((t - 0.2) / 0.6) * 0.5;
        fx.ash = ease((t - 1.2) / 1.5);
        f2Body(h, rd, feet, RY, 1.0, one, fx);
        float u = max(t - 0.5, 0.0);
        DFx fs = fx; fs.mat = M_HOLO; fs.matC = float3(0.45, 0.95, 1.0); fs.matK = 1.0; fs.ash = ease((t - 2.6) / 1.0); fs.charK = 0.0; fs.hurt = 0.0;
        if (t > 0.5) f2Body(h, rd, feet + float3(sin(u * 3.0) * 0.1, u * 0.9 + u * u * 0.15, 0), mul(RY, rotX(-0.2)), 0.95, one, fs);
        add += float3(0.4, 0.9, 1.0) * f2Glow(rd, feet + float3(0, 1.0 + u * 0.9 + u * u * 0.15, 0), 0.8, h.t) * saturate(u * 2.0) * (1.0 - fs.ash) * 0.35;
        f2Cubes(h, rd, C, t - 1.2, 18, seed, 0.5, 0.5, -0.3, 2.0, 0.03, hex3(80, 78, 76), hex3(50, 48, 46), -1e5, 0.0);
    } else if (k == 29) {
        // BAT SWARM: bursts into a swarm of bats
        if (t < 0.55) f2Body(h, rd, feet, RY, 1.0, one, fx);
        fnPoof(h, rd, C, t - 0.5, seed, 12, 0.07);
        [loop] for (int i = 0; i < 18; i++) {   // bats: a black body, two flapping wings, swirling out and up
            float ui = t - 0.5 - 0.02 * float(i);
            if (ui < 0.0 || ui > 3.0) continue;
            float a = h1(seed + float(i)) * 6.2832 + ui * (1.5 + h1(seed + float(i) * 3.0) * 2.0) * (i % 2 == 0 ? 1.0 : -1.0);
            float r = 0.3 + ui * (1.0 + 0.8 * h1(seed + float(i) * 5.0));
            float3 P = C + float3(cos(a) * r, ui * (0.8 + 0.8 * h1(seed + float(i) * 7.0)) + 0.2 * sin(ui * 9.0 + float(i)), sin(a) * r);
            float fl = sin(ui * 30.0 + float(i)) * 0.7;
            float3x3 Rb = rotY(-a);
            dsBox(h, rd, P, float3(0.04, 0.04, 0.05), Rb, hex3(20, 16, 22), 0.0);
            dsBox(h, rd, P + mul(mul(Rb, rotZ(fl)), float3(0.1, 0, 0)), float3(0.09, 0.008, 0.06), mul(Rb, rotZ(fl)), hex3(30, 24, 32), 0.0);
            dsBox(h, rd, P + mul(mul(Rb, rotZ(-fl)), float3(-0.1, 0, 0)), float3(0.09, 0.008, 0.06), mul(Rb, rotZ(-fl)), hex3(30, 24, 32), 0.0);
            add += float3(1.0, 0.1, 0.1) * f2Glow(rd, P + mul(Rb, float3(0, 0.02, 0.05)), 0.02, h.t) * 1.5;
        }
    } else if (k == 30) {
        // CONSTELLATION: becomes a constellation and rises into the sky
        fx.mat = M_STARS; fx.matC = float3(0.5, 0.7, 1.2); fx.matK = ease((t - 0.15) / 0.5);
        float u = max(t - 1.0, 0.0);
        float rise = u * u * 1.2 + u * 0.5;
        fx.ash = ease((t - 1.1) / 0.6);
        f2Body(h, rd, feet, RY, 1.0, one, fx);
        // the stars of it: the body's joints, rising, with lines between
        float3 J[8];
        J[0] = feet + mul(RY, float3(0, 30, 0) * DPX); J[1] = feet + mul(RY, float3(0, 23, 0) * DPX); J[2] = feet + mul(RY, float3(0, 12, 0) * DPX);
        J[3] = feet + mul(RY, float3(-7, 12, 0) * DPX); J[4] = feet + mul(RY, float3(7, 12, 0) * DPX); J[5] = feet + mul(RY, float3(-2, 1, 0) * DPX);
        J[6] = feet + mul(RY, float3(2, 1, 0) * DPX); J[7] = feet + mul(RY, float3(-6, 22, 0) * DPX);
        float on = ease((t - 0.8) / 0.4) * (1.0 - ease((t - 3.8) / 0.6));
        float tw = 0.75 + 0.25 * sin(t * 7.0);
        if (on > 0.0) {
            [unroll] for (int i = 0; i < 8; i++) { J[i] = C + (J[i] - C) * (1.0 + u * 0.6) + float3(0, rise, 0); add += float3(0.9, 0.95, 1.2) * f2Glow(rd, J[i], 0.06, h.t) * on * tw * 2.5; }
            float3 lc = float3(0.45, 0.6, 1.0) * on * 0.6;
            add += lc * (dsGlowSeg(rd, J[0], J[1], 0.012, h.t) + dsGlowSeg(rd, J[1], J[2], 0.012, h.t) + dsGlowSeg(rd, J[1], J[3], 0.012, h.t) + dsGlowSeg(rd, J[1], J[4], 0.012, h.t)
                       + dsGlowSeg(rd, J[2], J[5], 0.012, h.t) + dsGlowSeg(rd, J[2], J[6], 0.012, h.t));
        }
    } else if (k == 31) {
        // SYNTHWAVE: a neon wireframe over a retro grid and sun
        float on = ease((t - 0.1) / 0.4) * (1.0 - ease((t - 3.2) / 0.5));
        fx.mat = M_WIRE; fx.matC = float3(1.0, 0.2, 0.85); fx.matK = on;
        fx.ash = ease((t - 2.4) / 0.8);
        f2Body(h, rd, feet, RY, 1.0, one, fx);
        if (onG) {   // the grid: magenta lines running toward the sun
            float2 gq = (S.xz - feet.xz);
            float d = length(gq);
            float2 gl = abs(frac(gq * 2.0 + float2(0, t * 1.5)) - 0.5);
            float gline = step(0.44, max(gl.x, gl.y)) * smoothstep(3.0, 1.0, d);
            dec = lerp(decK > 0.0 ? dec : o, float3(0.05, 0.0, 0.1), smoothstep(3.2, 0.5, d) * 0.8 * on); decK = 1.0;
            add += float3(1.0, 0.15, 0.8) * gline * on * 0.9;
        }
        // the sun: a banded disc behind them
        float3 Sn = feet + float3(0, 1.5, 0) + normalize(feet * float3(1, 0, 1)) * 2.5;
        float3 nS = normalize(-Sn * float3(1, 0, 1));
        float tp = dot(Sn, nS) / dot(rd, nS);
        if (tp > 0.0 && tp < h.t && on > 0.0) {
            float3 P = rd * tp - Sn;
            float2 q = float2(dot(P, normalize(cross(float3(0, 1, 0), nS))), P.y) / 1.4;
            if (length(q) < 1.0 && (q.y > 0.0 || frac(q.y * 6.0) > 0.4)) add += lerp(float3(1.0, 0.25, 0.55), float3(1.0, 0.85, 0.2), saturate(q.y * 0.5 + 0.5)) * on * 0.9;
        }
        f2Cubes(h, rd, C, t - 2.4, 20, seed, 0.6, 0.6, -0.4, 1.5, 0.03, hex3(255, 50, 210), hex3(60, 220, 255), -1e5, float3(1.0, 0.3, 1.0));
    } else if (k == 32) {
        // MANGA: pressed paper-thin, it jumps into a comic page and is beaten panel after panel, then the page flips
        float flat = ease((t - 0.15) / 0.3);
        float3 n = normalize(-feet * float3(1, 0, 1));                      // the page faces the viewer
        float3 u = normalize(cross(float3(0, 1, 0), n));
        float pg = ease((t - 0.4) / 0.35);
        float flip = ease((t - 3.0) / 0.6);
        float3 Pc = feet + float3(0, 1.1, 0) - n * 0.3;
        // the page: white with black panel borders, hit bursts in each panel in turn
        float tp = dot(Pc, mul(rotY(flip * 2.0), n)) / dot(rd, mul(rotY(flip * 2.0), n));
        if (pg > 0.0 && tp > 0.0 && tp < h.t) {
            float3 P = rd * tp - Pc;
            float2 q = float2(dot(P, u) / cos(flip * 2.0 + 0.0001), P.y);
            if (abs(q.x) < 1.0 * pg && abs(q.y) < 1.35 * pg) {
                float2 cell = floor((q + float2(1.0, 1.35)) / float2(1.0, 0.9));
                float2 f = frac((q + float2(1.0, 1.35)) / float2(1.0, 0.9));
                float border = (f.x < 0.04 || f.x > 0.96 || f.y < 0.05 || f.y > 0.95 || abs(q.x) > 0.96 * pg || abs(q.y) > 1.31 * pg) ? 1.0 : 0.0;
                float ci = cell.x + cell.y * 2.0;
                float hitT = 0.8 + ci * 0.35;
                float hit = step(hitT, t);
                float2 bc = (f - 0.5) * float2(1.0, 0.9);
                float star = step(length(bc) * (1.0 + 0.4 * sin(atan2(bc.y, bc.x) * 8.0)), 0.25) * hit;
                float lines = step(0.92, frac(atan2(bc.y, bc.x) * 3.0)) * step(0.15, length(bc)) * hit;
                float3 pc = lerp(float3(0.96, 0.95, 0.92), float3(0.02, 0.02, 0.02), max(border, lines * 0.7));
                pc = lerp(pc, float3(1.0, 0.85, 0.1), star * (1.0 - border));
                h.t = tp; h.c = dsLit(pow(pc, 2.2), n) * 1.3; h.hit = true;
            }
        }
        if (t < 0.6) f2Body(h, rd, feet, RY, 1.0, float3(1, 1, 1.0 - 0.9 * flat), fx);
    } else if (k == 33) {
        // CRYSTAL BURST: crystals burst out of the ground through it, turn it to crystal, then shatter into shards
        [unroll] for (int i = 0; i < 9; i++) {
            float a = float(i) * 0.698 + seed, r = 0.15 + 0.5 * h1(seed + float(i));
            float g2 = easeOut(saturate((t - 0.1 - 0.04 * float(i)) / 0.25)) * (1.0 - ease((t - 1.9) / 0.3));
            float L = (0.5 + 0.9 * h1(seed + float(i) * 3.0)) * g2;
            float3 dir = normalize(float3(cos(a) * 0.5, 1.0, sin(a) * 0.5));
            float3 b0 = feet + float3(cos(a) * r, -0.1, sin(a) * r);
            float3 col = i % 3 == 0 ? hex3(150, 220, 250) : hex3(170, 90, 230);
            if (L > 0.02) dsBox(h, rd, b0 + dir * L * 0.5, float3(0.09, L * 0.5, 0.09), basisOf(dir, a), col, col * 0.5);
        }
        fx.mat = M_CRYSTAL; fx.matC = hex3(190, 110, 250); fx.matK = ease((t - 0.3) / 0.5);
        if (t < 2.0) f2Body(h, rd, feet, RY, 1.0, one, fx);
        f2Cubes(h, rd, C, t - 2.0, 32, seed, 2.8, 1.8, 10.0, 1.6, 0.04, hex3(190, 110, 250), hex3(160, 230, 255), floorY, float3(0.6, 0.35, 0.9));
        add += float3(0.8, 0.5, 1.0) * f2Glow(rd, C, 1.4, h.t) * exp(-max(t - 2.0, 0.0) * 6.0) * step(2.0, t) * 0.7;
    } else if (k == 34) {
        // GIANT SWORD: a giant sword falls from the sky and drives the mob into the ground; the earth cracks, the mob
        // is blown apart, and the sword shatters into golden shards
        float tI = 0.8;
        float y = t < tI ? 1.4 + 18.0 * pow(1.0 - t / tI, 2.0) : 1.4 - 0.35 * ease((t - tI) / 0.15);
        float3 Sw = feet + float3(0, y, 0);
        float3x3 RS = rotY(yaw + 0.4);
        if (t < 2.3) {   // the sword, point down: blade, guard, grip, pommel
            dsBox(h, rd, Sw + float3(0, 1.4, 0), float3(0.18, 1.6, 0.05), RS, hex3(210, 214, 222), float3(0.1, 0.1, 0.12));
            dsBox(h, rd, Sw + float3(0, 3.05, 0), float3(0.6, 0.1, 0.12), RS, hex3(230, 180, 50), float3(0.3, 0.2, 0.02));
            dsBox(h, rd, Sw + float3(0, 3.6, 0), float3(0.07, 0.45, 0.07), RS, hex3(70, 40, 30), 0.0);
            dsBox(h, rd, Sw + float3(0, 4.1, 0), float3(0.13, 0.13, 0.13), RS, hex3(230, 180, 50), float3(0.3, 0.2, 0.02));
        }
        float sink = t < tI ? 0.0 : ease((t - tI) / 0.2) * 0.9;
        fx.clipY = floorY + 0.01;
        if (t < 1.4) f2Body(h, rd, feet - float3(0, sink, 0), RY, 1.0, float3(1.0 + 0.3 * sink, 1.0 - 0.3 * sink, 1.0 + 0.3 * sink), fx);
        fnGround(onG, S, feet, tI, 3.8, 0.9, 3.2, seed, t, o, dec, decK, add, float3(1.0, 0.85, 0.4));
        f2Cubes(h, rd, feet + float3(0, 0.3, 0), t - 1.4, 26, seed, 3.5, 3.0, 14.0, 1.4, 0.05, team * 0.6, hex3(110, 20, 20), floorY, 0.0);
        f2Cubes(h, rd, Sw + float3(0, 1.8, 0), t - 2.3, 30, seed + 7.0, 3.0, 1.0, 6.0, 1.8, 0.05, hex3(255, 210, 90), hex3(255, 240, 180), floorY, float3(1.2, 0.9, 0.3));
        add += float3(1.0, 0.85, 0.4) * f2Glow(rd, Sw + float3(0, 1.8, 0), 2.0, h.t) * exp(-max(t - 2.3, 0.0) * 5.0) * step(2.3, t) * 0.8;
    } else if (k == 35) {
        // SHATTERED MIRROR: a pane of mirror slides in front of it and it turns to chrome; cracks spread, the pane
        // bursts into spinning mirror shards
        float3 n = normalize(-feet * float3(1, 0, 1));
        float3 u = normalize(cross(float3(0, 1, 0), n));
        float slide = ease((t - 0.1) / 0.4);
        float3 Pm = feet + float3(0, 1.0, 0) + n * 0.55 + u * (1.0 - slide) * 2.5;
        float br = 2.3;
        if (t < br) {
            float tp = dot(Pm, n) / dot(rd, n);
            if (tp > 0.0 && tp < h.t) {
                float3 P = rd * tp - Pm;
                float2 q = float2(dot(P, u), P.y);
                if (abs(q.x) < 0.7 && abs(q.y) < 1.1) {
                    float cr = 0.0;
                    float grow = ease((t - 1.0) / 1.0);
                    [unroll] for (int i = 0; i < 7; i++) { float a = float(i) * 0.9 + seed; float2 d = float2(cos(a), sin(a)); float along = dot(q, d); cr = max(cr, step(abs(q.x * d.y - q.y * d.x + 0.04 * sin(along * 20.0 + float(i))), 0.012) * step(0.0, along) * step(along, grow * 1.3)); }
                    float frame = step(0.66, abs(q.x)) + step(1.06, abs(q.y));
                    float3 c = lerp(dsEnv(reflect(rd, -n)) * 0.9 + 0.08, float3(0.95, 0.97, 1.0), cr);
                    h.t = tp; h.c = lerp(c, hex3(60, 50, 40), saturate(frame)); h.hit = true;
                }
            }
        }
        fx.mat = M_CHROME; fx.matC = float3(1, 1, 1); fx.matK = ease((t - 0.5) / 0.5);
        if (t < br + 0.05) f2Body(h, rd, feet, RY, 1.0, one, fx);
        [loop] for (int i = 0; i < 20; i++) {   // shards: spinning flat squares of mirror flying out
            float ui = t - br;
            if (ui < 0.0 || ui > 1.8) break;
            float3 dv = normalize(float3(h1(seed + float(i)) - 0.5, h1(seed + float(i) * 2.0) * 0.8, h1(seed + float(i) * 3.0) - 0.5) + n * 0.6);
            float3 P = Pm + float3((h1(float(i) * 5.0) - 0.5) * 1.2, (h1(float(i) * 7.0) - 0.5) * 2.0, 0) + dv * ui * 4.0 + float3(0, -5.0 * ui * ui, 0);
            float3x3 Rs = mul(rotY(ui * 9.0 + float(i)), rotX(ui * 7.0));
            float sz = 0.06 + 0.08 * h1(float(i) * 11.0);
            float tN; float3 nl2, lp2;
            if (hitBox(rd, P, float3(sz, sz, 0.005), Rs, tN, nl2, lp2) && tN < h.t) { h.t = tN; h.c = dsEnv(reflect(rd, mul(Rs, nl2))) + 0.1; h.hit = true; }
        }
        f2Cubes(h, rd, C, t - br, 14, seed, 2.5, 1.0, 9.0, 1.2, 0.04, hex3(200, 205, 215), hex3(150, 155, 165), floorY, 0.0);
    } else if (k == 36) {
        // KALEIDOSCOPE: a mandala of coloured petals turns behind it while it cycles the rainbow, then it all bursts
        // into a spiral of coloured cubes and rings of light
        float on = ease((t - 0.1) / 0.5) * (1.0 - ease((t - 2.2) / 0.2));
        float3 n = normalize(-feet * float3(1, 0, 1));
        float3 u = normalize(cross(float3(0, 1, 0), n));
        float3 Mc = feet + float3(0, 1.1, 0) - n * 0.6;
        float tp = dot(Mc, n) / dot(rd, n);
        if (on > 0.0 && tp > 0.0 && tp < h.t) {
            float3 P = rd * tp - Mc;
            float2 q = float2(dot(P, u), P.y);
            float r = length(q) / (1.5 * on), a = atan2(q.y, q.x) + t * 1.5;
            float petal = step(r, 0.35 + 0.65 * abs(cos(a * 4.0))) * step(r, 1.0);
            float ring = step(abs(frac(r * 3.0) - 0.5), 0.08) * step(r, 1.0);
            if (petal + ring > 0.0) add += hsv3(frac(floor(r * 3.0) * 0.21 + floor((a / 6.2832) * 8.0) * 0.125 + t * 0.4), 0.85, 1.0) * (petal * 0.7 + ring * 0.5) * on;
        }
        fx.mat = M_RAINBOW; fx.matK = ease((t - 0.2) / 0.4);
        if (t < 2.2) f2Body(h, rd, feet, RY, 1.0, one, fx);
        [loop] for (int i = 0; i < 24; i++) {   // the burst: a spiral of coloured cubes
            float ui = t - 2.2;
            if (ui < 0.0 || ui > 1.8) break;
            float a = float(i) * 0.5 + ui * 5.0, r = ui * 2.4 * (0.5 + 0.5 * h1(float(i)));
            float3 P = C + float3(cos(a) * r, (float(i) / 24.0 - 0.5) * 1.5 + ui * 0.6, sin(a) * r);
            float3 col = hsv3(frac(float(i) / 24.0 + t * 0.5), 0.85, 1.0);
            dsBox(h, rd, P, float3(0.05, 0.05, 0.05) * (1.0 - ui / 1.8), rotY(a), col, col * 0.8);
        }
        [unroll] for (int i = 0; i < 3; i++) add += hsv3(float(i) * 0.33 + t * 0.4, 0.8, 1.0) * f2Ring(onG, S, feet, (t - 2.2 - 0.12 * float(i)) * 3.5, 0.06) * step(2.2 + 0.12 * float(i), t) * exp(-max(t - 2.2, 0.0) * 1.5) * 1.2;
    } else if (k == 37) {
        // FIREFLIES: it sinks into a dark silhouette, then breaks into hundreds of blinking lights drifting up
        fx.mat = M_SIL; fx.matC = float3(0.5, 0.9, 0.2); fx.matK = ease((t - 0.1) / 0.6);
        fx.ash = ease((t - 1.3) / 1.0);
        f2Body(h, rd, feet, RY, 1.0, one, fx);
        if (nearRay(rd, C + float3(0, 1.0, 0), 3.0)) {
            [loop] for (int i = 0; i < 40; i++) {
                float hi = float(i);
                float born = 1.3 + h1(hi * 3.7 + seed) * 1.0;
                float ui = t - born;
                if (ui < 0.0 || ui > 2.6) continue;
                float3 P0 = feet + mul(RY, float3((h1(hi) - 0.5) * 0.5, h1(hi * 2.0) * 1.8, (h1(hi * 5.0) - 0.5) * 0.3));
                float3 P = P0 + float3(sin(ui * 1.7 + hi) * 0.3 * ui, ui * (0.35 + 0.3 * h1(hi * 7.0)), cos(ui * 1.3 + hi) * 0.3 * ui);
                float blink = saturate(sin(ui * 6.0 + hi * 3.0) * 1.5 + 0.3) * (1.0 - ease((ui - 1.8) / 0.8));
                add += float3(0.65, 1.0, 0.2) * f2Glow(rd, P, 0.035, h.t) * blink * 2.4;
            }
        }
    } else if (k == 38) {
        // ABDUCTION: a flying saucer slides in above it and a green beam lifts it away; the saucer zips off, leaving
        // a scorched crop circle
        float3 Uo = feet + float3(0, 4.2, 0);
        float in1 = ease((t - 0.1) / 0.6), off = ease((t - 2.6) / 0.5);
        float3 U = Uo + side * (1.0 - in1) * -8.0 + side * off * 14.0 + float3(0, off * 4.0, 0) + float3(0, 0.08 * sin(t * 4.0), 0);
        float3x3 RU = rotY(t * 1.5);
        dsBox(h, rd, U, float3(1.2, 0.12, 1.2), RU, hex3(150, 156, 166), 0.0);
        dsBox(h, rd, U + float3(0, -0.15, 0), float3(0.8, 0.08, 0.8), RU, hex3(110, 114, 122), 0.0);
        dsBox(h, rd, U + float3(0, 0.25, 0), float3(0.45, 0.22, 0.45), RU, hex3(140, 230, 170), float3(0.2, 0.5, 0.3));
        [unroll] for (int i = 0; i < 6; i++) { float a = float(i) * 1.047 + t * 1.5; add += float3(0.4, 1.0, 0.5) * f2Glow(rd, U + float3(cos(a) * 1.05, -0.05, sin(a) * 1.05), 0.06, h.t) * 2.0; }
        float beam = ease((t - 0.6) / 0.2) * (1.0 - ease((t - 2.4) / 0.2));
        if (beam > 0.0) {   // the tractor beam: a widening cone of green light, rings rising up it
            [unroll] for (int i = 0; i < 4; i++) {
                float f = float(i) / 4.0;
                add += float3(0.3, 1.0, 0.45) * dsGlowSeg(rd, U - float3(0, 0.2, 0), feet, 0.35 + 0.3 * f, h.t) * beam * 0.12;
            }
        }
        float lift = ease((t - 0.8) / 1.5);
        if (t < 2.35) f2Body(h, rd, feet + float3(0, lift * 3.6, 0), mul(RY, mul(rotY(t * 2.0), rotX(lift * 0.6))), 1.0 - 0.3 * lift, one, fx);
        if (onG) {   // the crop circle
            float d = length(S.xz - feet.xz);
            float cc = (step(d, 1.2) * step(1.05, d) + step(d, 0.5) + step(abs(d - 0.8), 0.04)) * ease((t - 0.7) / 0.5) * (1.0 - ease((t - 4.0) / 0.7));
            if (cc > 0.0) { dec = (decK > 0.0 ? dec : o) * (1.0 - 0.6 * saturate(cc)); decK = 1.0; }
        }
    } else {
        // SUBSTITUTION: it blurs into afterimages and vanishes in a puff of smoke; a log with a paper charm stands in
        // its place, then is sliced into three
        float blur = ease((t - 0.15) / 0.25);
        if (t < 0.6) {
            f2Body(h, rd, feet, RY, 1.0, one, fx);
            DFx fa = fx; fa.mat = M_HOLO; fa.matC = team * 0.6; fa.matK = 1.0; fa.hurt = 0.0;
            [unroll] for (int i = 1; i < 4; i++) f2Body(h, rd, feet + side * blur * 0.18 * float(i) * (i % 2 == 0 ? -1.0 : 1.0), RY, 1.0, one, fa);
        }
        fnPoof(h, rd, C, t - 0.55, seed, 16, 0.09);
        float logY = 1.6;
        float cut = 1.6;
        if (t > 0.55 && t < 3.65) {   // the log: bark, light end grain, a paper charm stuck on it
            [unroll] for (int p = 0; p < 3; p++) {
                float y0 = float(p) * logY / 3.0;
                float u2 = max(t - cut, 0.0);
                float sl = (p == 2 ? 1.0 : (p == 1 ? -0.6 : 0.0)) * u2;
                float3 Pl = feet + float3(0, y0 + logY / 6.0, 0) + side * sl * 1.2 + float3(0, p == 2 ? -3.0 * u2 * u2 : 0.0, 0);
                float3x3 Rl = mul(RY, rotZ(sl * 0.5));
                dsBox(h, rd, Pl, float3(0.26, logY / 6.0 - 0.005, 0.26), Rl, hex3(102, 76, 48), 0.0);
                dsBox(h, rd, Pl, float3(0.2, logY / 6.0, 0.2), Rl, hex3(170, 136, 90), 0.0);
                if (p == 1) dsBox(h, rd, Pl + mul(Rl, float3(0, 0.05, 0.265)), float3(0.08, 0.17, 0.005), Rl, hex3(236, 228, 200), float3(0.25, 0.05, 0.03));
            }
            fnArc(add, rd, feet + float3(0, logY / 3.0, 0), side, fwd, 0.6, easeOut((t - cut + 0.08) / 0.08), exp(-max(t - cut, 0.0) * 6.0) * step(cut - 0.08, t), float3(1.2, 1.2, 1.3), h.t);
            fnArc(add, rd, feet + float3(0, 2.0 * logY / 3.0, 0), -side, fwd, 0.6, easeOut((t - cut) / 0.08), exp(-max(t - cut - 0.08, 0.0) * 6.0) * step(cut, t), float3(1.2, 1.2, 1.3), h.t);
        }
        fnPoof(h, rd, feet + float3(0, 0.7, 0), t - 3.6, seed + 2.0, 12, 0.08);
    }
}
#endif
void deathScenesF2(int k, float t, float3 o, float3 rd, float3 feet, float yaw, float view, float seed, float3 team, float3 glow,
                   bool onG, float3 S, float gr, inout DHit h, inout float3 add, inout float3 dec, inout float decK, inout DFx fx) {
    // (DS_GROUP picks one group of scenes for the offline lab, which cannot compile them all at once; the game builds all)
#if !defined(DS_GROUP) || DS_GROUP == 4
    if (k >= 40) { f2Special(k - 40, t, o, rd, feet, yaw, view, seed, team, glow, onG, S, gr, h, add, dec, decK, fx); return; }
#endif
#if !defined(DS_GROUP) || DS_GROUP == 2
    if (k < 20) { f2A(k, t, o, rd, feet, yaw, view, seed, team, glow, onG, S, gr, h, add, dec, decK, fx); return; }
#endif
#if !defined(DS_GROUP) || DS_GROUP == 3
    if (k >= 20 && k < 40) { f2B(k, t, o, rd, feet, yaw, view, seed, team, glow, onG, S, gr, h, add, dec, decK, fx); return; }
#endif
}
