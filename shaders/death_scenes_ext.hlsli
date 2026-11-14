// ---- death scenes 7-13, added to the shuffle: the Finishers finishers (by akeresmods), Ragdollified's ragdolls (by
//      raiiiden) and Sable Ragdolls (by Leonardoinc22), all used with their authors' permission; Bedrock / Renderer
//      Foundation port by Novak.
//      7 Finisher (First Finisher)  8 Shield Counter  9 Headbutt (Quick Finisher 4)  10 Kick (Quick Finisher 5)
//      11 Ultimate Attack  12/13 Ragdollified (melee throw / side hit)  14/15 Sable Ragdoll (slump / roll)
//      (the CPU picks the ragdoll's variant, so its sounds - DeathSounds.cpp - land with what is drawn)
//      The finishers play the mod's own animation clips (baked at 20 Hz by tools/gen_death_tables.py, easings and all);
//      the killer is a phantom - an ink figure with a lit rim - since the real one is already off fighting. The
//      ragdolls are the mods' rigid-body ragdolls, simulated offline with Bullet and baked. Camera moves, cinematic
//      bars, screen tints and the QTE of the finishers are left out on purpose: on a server they would take the
//      screen away from the player mid-fight; their flashes are kept as light at the hit instead.
#include "death_tables.hlsli"

float2 dtH(uint u) { return f16tof32(uint2(u & 0xffffu, u >> 16)); }
// one bone of a Finishers clip at frame fr (20 Hz): rotation (rad, Bedrock euler), offset (px), scale
void dtBone(uint off, uint n, float fr, int b, out float3 r, out float3 p, out float s) {
    fr = clamp(fr, 0.0, float(n - 1u));
    uint  i0 = uint(fr), i1 = min(i0 + 1u, n - 1u);
    float u = fr - float(i0);
    uint  a = off + (i0 * 8u + uint(b)) * 4u, c = off + (i1 * 8u + uint(b)) * 4u;
    float2 a0 = dtH(kClip[a]), a1 = dtH(kClip[a + 1u]), a2 = dtH(kClip[a + 2u]), a3 = dtH(kClip[a + 3u]);
    float2 c0 = dtH(kClip[c]), c1 = dtH(kClip[c + 1u]), c2 = dtH(kClip[c + 2u]), c3 = dtH(kClip[c + 3u]);
    r = lerp(float3(a0, a1.x), float3(c0, c1.x), u);
    p = lerp(float3(a1.y, a2), float3(c1.y, c2), u);
    s = lerp(a3.x, c3.x, u);
}
float3x3 dtEuler(float3 r) { return mul(rotZ(r.z), mul(rotY(r.y), rotX(r.x))); }
// Bedrock's pivots (the Java biped's): every part turns about its own, the body about the neck
static const float3 kPB[6] = { float3(0, 24, 0), float3(0, 24, 0), float3(-5, 22, 0), float3(5, 22, 0), float3(-1.9, 12, 0), float3(1.9, 12, 0) };

struct DRig { float3 hand; float3 handF; float3 handD; float3 handL; float3 head; float fing; float3 fingD; };   // vectors, not matrices: fewer live registers
// a body playing a clip: O = feet, R = its facing; parts: bit j drawn
DRig dsClip(inout DHit h, float3 rd, float3 O, float3x3 R, uint off, uint n, float fr, int parts, DFx fx) {
    DRig g;
    float3 rr, rp, r, p; float rs, s;
    dtBone(off, n, fr, 6, rr, rp, rs);
    float3x3 Rr = mul(R, dtEuler(rr));
    float3 Or = O + mul(R, rp * DPX);
    // pixels nowhere near the body (most of the scene's ball) get a rough rig and skip the bones: everything drawn
    // off the rig (sword, finger, the flash at the head) lies within this 2.1-block ball
    g.head = Or + mul(Rr, float3(0, 28.0 * DPX, 0)); g.hand = Or + mul(Rr, float3(-6.0, 12.0, 0) * DPX); g.handL = Or + mul(Rr, float3(6.0, 12.0, 0) * DPX);
    g.handF = mul(Rr, float3(0, 0.15, 1)); g.handD = mul(Rr, float3(0, -1, 0)); g.fing = 0.0; g.fingD = g.handD;
    if (!nearRay(rd, Or + mul(Rr, float3(0, 16.0 * DPX, 0)), 2.1)) return g;
    bool near = true;
    [loop] for (int j = 0; j < 6; j++) {
        dtBone(off, n, fr, j, r, p, s);
        float3x3 Rj = mul(Rr, dtEuler(r));
        float3 P = Or + mul(Rr, (kPB[j] + p) * DPX) + mul(Rj, (kPC[j] - kPB[j]) * DPX);
        if (j == 0) g.head = P;
        if (j == 2) {
            g.hand = P + mul(Rj, float3(0, -5.0, 0) * DPX); g.handF = mul(Rj, float3(0, 0.15, 1)); g.handD = mul(Rj, float3(0, -1, 0));
            float3 fr2, fp2; float fs2;
            dtBone(off, n, fr, 7, fr2, fp2, fs2);                   // Finishers' extra bone (the gesture's finger), on this hand
            g.fing = fs2; g.fingD = mul(Rj, mul(dtEuler(fr2), float3(0, -1, 0)));
        }
        if (j == 3) g.handL = P + mul(Rj, float3(0, -5.0, 0) * DPX);
        if (near && ((parts >> j) & 1) != 0) dsPart(h, rd, j, P, Rj, 1.0, -99.0, 99.0, fx);
    }
    return g;
}

// ------------------------------------------------------------------ ragdoll bakes
float4 dtQuat(uint u0, uint u1) { return float4(dtH(u0), dtH(u1)); }
float3x3 quatM(float4 q) {
    q = normalize(q);
    float x = q.x, y = q.y, z = q.z, w = q.w;
    return float3x3(1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
                    2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                    2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y));
}
// part j of a bake at frame fr: centre (blocks, from the feet) + rotation; mirror: the bake seen in a mirror (x flipped,
// left and right swapped) - one simulation, two different falls
void dtRag(uint off, float fr, int j, bool mirror, out float3 P, out float3x3 R) {
    int  jj = mirror && j >= 2 ? (j ^ 1) : j;
    fr = clamp(fr, 0.0, 96.0);
    uint i0 = uint(fr), i1 = min(i0 + 1u, 96u);
    float u = fr - float(i0);
    uint a = off + (i0 * 6u + uint(jj)) * 4u, c = off + (i1 * 6u + uint(jj)) * 4u;
    float3 pa = float3(dtH(kRag[a]), dtH(kRag[a + 1u]).x), pc = float3(dtH(kRag[c]), dtH(kRag[c + 1u]).x);
    float4 qa = float4(dtH(kRag[a + 1u]).y, dtH(kRag[a + 2u]), dtH(kRag[a + 3u]).x);
    float4 qc = float4(dtH(kRag[c + 1u]).y, dtH(kRag[c + 2u]), dtH(kRag[c + 3u]).x);
    if (dot(qa, qc) < 0.0) qc = -qc;
    P = lerp(pa, pc, u);
    float4 q = lerp(qa, qc, u);
    if (mirror) { P.x = -P.x; q = float4(q.x, -q.y, -q.z, q.w); }
    R = quatM(q);
}
// the whole ragdoll: O = the feet where it died, RY = the bake's turn
void dsRagdoll(inout DHit h, float3 rd, float3 O, float3x3 RY, uint off, float fr, bool mirror, int sever, DFx fx) {
    float3 P; float3x3 R;
    dtRag(off, fr, 1, mirror, P, R);
    float3 C = O + mul(RY, P);
    bool nearC = nearRay(rd, C, 1.7);
    if (!nearC && sever < 0) return;                       // the attached body lies within 1.7 of its chest
    [loop] for (int j = 0; j < 6; j++) {
        if (j != sever && !nearC) continue;
        dtRag(off, fr, j, mirror, P, R);
        float3 W = O + mul(RY, P);
        if (j == sever && !nearRay(rd, W, 0.6)) continue;
        // a severed limb shows its cut at the shoulder end
        dsPart(h, rd, j, W, mul(RY, R), 1.0, -99.0, j == sever ? 5.9 : 99.0, fx);
    }
}

// ------------------------------------------------------------------ Finishers' effects, rebuilt as world pieces
// ground_crack: jagged dark spokes out from a point (gp in blocks, texel-snapped), grown to `grow` of R
float fnCrack(float2 gp, float R, float seed, float grow, int n) {
    float r = length(gp);
    if (r > R * grow + 0.05 || grow <= 0.0) return 0.0;
    float a = atan2(gp.y, gp.x), k = 0.0;
    [loop] for (int i = 0; i < n; i++) {
        float ai = (float(i) + 0.7 * h1(seed + float(i) * 1.7)) * 6.2832 / float(n);
        float len = R * (0.45 + 0.55 * h1(seed + float(i) * 7.3)) * grow;
        float wob = (h1(seed + float(i) * 3.1 + floor(r * 9.0)) - 0.5) * 0.10 * r;      // the line wanders, block by block
        float da = (frac((a - ai) / 6.2832 + 0.5) - 0.5) * 6.2832 * r + wob;
        float w = 0.045 * saturate(1.0 - r / max(len, 0.01)) + 0.010;
        if (r < len && abs(da) < w) k = max(k, 1.0 - r / max(len, 0.01) * 0.5);
    }
    return k;
}
// air light at a point (a flash), depth-tested
float fnFlash(float3 rd, float3 P, float r, float tMax) {
    float tc = dot(P, rd);
    if (tc > tMax + r) return 0.0;
    float d2 = max(dot(P, P) - tc * tc, 0.0);
    return exp(-d2 / (r * r));
}
// the ground under the scene: crater (dark bowl, raised rim), crack, shockwave ring - decal colour into dec
void fnGround(bool onG, float3 S, float3 C, float t0, float dur, float rad, float crackR, float seed, float t,
              float3 o, inout float3 dec, inout float decK, inout float3 add, float3 ringCol) {
    float u = t - t0;
    if (!onG || u < 0.0 || u > dur) return;
    float2 gp = floor((S.xz - C.xz) * 16.0) / 16.0 + 1.0 / 32.0;
    float  r = length(gp);
    float  life = 1.0 - ease((u - dur * 0.7) / (dur * 0.3));
    float3 cur = decK > 0.0 ? dec : o;
    if (rad > 0.0 && r < rad * 1.25) {
        float n = hash12(floor(gp * 16.0) + seed);
        float bowl = smoothstep(rad, rad * 0.2, r), rim = exp(-pow((r - rad) / (rad * 0.18), 2.0));
        float3 c = cur * lerp(1.0, 0.30 + 0.2 * n, bowl * life) * (1.0 + 0.35 * rim * life);
        dec = c; decK = 1.0; cur = c;
    }
    if (crackR > 0.0) {
        float k = fnCrack(gp, crackR, seed, easeOut(u / 0.18), 11);
        if (k > 0.0) { dec = cur * lerp(1.0, 0.12, k * life); decK = 1.0; }
    }
    if (u < 0.45) {   // the shockwave: a ring of light runs out along the ground
        float rr = (rad > 0.0 ? rad * 2.2 : 1.6) * easeOut(u / 0.45);
        add += ringCol * exp(-pow((r - rr) / 0.05, 2.0)) * (1.0 - u / 0.45) * 0.9;
    }
}
// ground debris + chunks of the floor thrown up (Finishers' ground_debris / flying_blocks)
void fnDebris(inout DHit h, float3 rd, float3 C, float u, float seed, float k, float floorY) {
    dsBurst(h, rd, C + float3(0, 0.04, 0), u, int(10.0 + 8.0 * k), seed, 1.6 * k, 2.4 * k, 14.0, 1.1, 0.035,
            hex3(96, 92, 88), hex3(140, 132, 120), floorY, 0.0);
    dsBurst(h, rd, C + float3(0, 0.08, 0), u, int(3.0 + 3.0 * k), seed + 40.0, 1.1 * k, 3.6 * k, 15.0, 1.3, 0.10,
            hex3(110, 104, 96), hex3(78, 74, 70), floorY, 0.0);
}
// Minecraft's poof: white puffs that rise and shrink
void fnPoof(inout DHit h, float3 rd, float3 C, float u, float seed, int n, float sz) {
    dsBurst(h, rd, C, u, n, seed + 70.0, 1.1, 0.7, -1.4, 0.8, sz, hex3(232, 232, 236), hex3(186, 186, 194), -1e5, float3(0.05, 0.05, 0.05));
}
// crit stars (impact_burst / crit): small, bright, quick
void fnCrit(inout DHit h, float3 rd, float3 C, float u, float seed, int n, float v) {
    dsBurst(h, rd, C, u, n, seed + 90.0, v, 0.8, 5.0, 0.55, 0.025, hex3(255, 246, 204), hex3(255, 214, 120), -1e5, float3(1.6, 1.4, 0.9));
}
// a sweep / slash arc of light: centre C, axes U (start) and V (towards the end), radius r, drawn up to sweep 0..1
void fnArc(inout float3 add, float3 rd, float3 C, float3 U, float3 V, float r, float sweep, float k, float3 col, float tMax) {
    if (k <= 0.001 || !nearRay(rd, C, r + 0.3)) return;
    [loop] for (int l = 0; l < 8; l++) {
        float a0 = 2.4 * float(l) / 8.0 * sweep - 1.2, a1 = 2.4 * float(l + 1) / 8.0 * sweep - 1.2;
        float3 q0 = C + (U * cos(a0) + V * sin(a0)) * r, q1 = C + (U * cos(a1) + V * sin(a1)) * r;
        add += col * dsGlowSeg(rd, q0, q1, 0.035, tMax) * k * (0.3 + 0.7 * float(l) / 8.0);
    }
}
// the phantom's sword: ink blade with a white edge, from the fist along the arm's forward axis
void fnSword(inout DHit h, float3 rd, DRig g, float glint) {
    float3 dir = normalize(g.handF);
    float3x3 RB = basisOf(dir, 0.0);
    dsBox(h, rd, g.hand + dir * 0.46, float3(0.022, 0.40, 0.05), RB, hex3(10, 10, 14), hex3(236, 226, 255) * (0.9 + 2.5 * glint));
    dsBox(h, rd, g.hand + dir * 0.02, float3(0.11, 0.025, 0.035), RB, hex3(20, 18, 26), hex3(236, 226, 255) * 0.5);   // the guard
}
float fnProg(float T) {   // First Finisher's walk-in (its movement keyframes): progress 0..1 by tick
    if (T < 8.0)  return 0.06 * (T / 8.0) * (T / 8.0);
    if (T < 24.0) { float u = (T - 8.0) / 16.0; u = u < 0.5 ? 4 * u * u * u : 1 - pow(-2 * u + 2, 3) / 2; return lerp(0.06, 0.38, u); }
    if (T < 42.0) { float u = (T - 24.0) / 18.0; u = u < 0.5 ? 2 * u * u : 1 - pow(-2 * u + 2, 2) / 2; return lerp(0.38, 0.82, u); }
    if (T < 52.0) return lerp(0.82, 1.0, easeOut((T - 42.0) / 10.0));
    return 1.0;
}

// ------------------------------------------------------------------ the scenes
// view: the camera's yaw when the kill came (eC.y), so a pair is laid out side-on to the one who saw it
void deathScenesExt(int sc, float t, float3 o, float3 rd, float3 feet, float yaw, float view, float seed, float3 team, float3 glow,
                    bool onG, float3 S, float gr, inout DHit h, inout float3 add, inout float3 dec, inout float decK, inout DFx fx) {
    float3 vf = float3(cos(view), 0, sin(view));                 // where they were looking
    float  sgn = h1(seed * 0.71) < 0.5 ? -1.0 : 1.0;
    float3 A = float3(-vf.z, 0, vf.x) * sgn;                     // across their view: the victim faces along it
    float  yv = atan2(A.x, A.z);
    float3x3 RV = rotY(yv), RA = rotY(yv + 3.14159265);
    float3 rimC = hex3(236, 226, 255);
    DFx fg = fx; fg.ghost = 1.0; fg.slot = -1.0; fg.hurt = 0.0;   // the phantom
    float  floorY = feet.y;
    float3 warm = float3(1.0, 0.94, 0.80);

    if (sc == 7) {
        // ============ FIRST FINISHER: the phantom drops in three blocks off, stomps in (a crater at every step),
        // the victim kneels and looks up at it; the sword goes up, holds, and comes down
        float T = t * 20.0 + 2.0;
        float3 A0 = feet + A * 3.4, A1 = feet + A * 1.62;
        float  pr = fnProg(T);
        float3 AO = lerp(A0, A1, pr) + float3(0, T < 4.0 ? pow((4.0 - T) / 4.0, 2.0) * 7.0 : 0.0, 0);
        float  KT = 74.0;
        fg.ash = max(1.0 - saturate((T - 1.0) / 2.0) * 1.2, ease((T - 88.0) / 9.0));
        DRig g = dsClip(h, rd, AO, RA, FN_FINISHER, FN_FINISHER_N, T, 63, fg);
        if (fg.ash < 0.95) fnSword(h, rd, g, exp(-pow((T - 66.0) / 2.0, 2.0)));
        // the stomps: a crater where each landing foot comes down
        [unroll] for (int k = 0; k < 5; k++) {
            float tk = 4.0 + 12.0 * float(k);
            float3 Ck = lerp(A0, A1, fnProg(tk)) - A * 0.1;
            fnGround(onG, S, Ck, tk / 20.0 - 0.1, 4.5, 0.62 + 0.07 * float(k), k == 0 ? 2.0 : (k == 4 ? 2.45 : 0.0), seed + float(k), t, o, dec, decK, add, glow * 0.5);
            if (k == 0 || k == 2 || k == 3) fnDebris(h, rd, Ck, t - (tk / 20.0 - 0.1), seed + 10.0 * float(k), k == 0 ? 1.0 : 0.7, floorY);
        }
        // the victim: kneels (Finishers' stagger), then the stroke, then a vanilla death
        float u = max(T - KT, 0.0) / 20.0;
        float tip = 1.5708 * saturate(u / 0.55) * saturate(u / 0.55);
        fx.flash = step(KT, T) * step(T, KT + 2.0);
        fx.hurt = max(fx.hurt, saturate(1.0 - u * 2.0) * step(KT, T));
        float3 MID = (feet + A1) * 0.5;
        if (T < KT + 28.0) {
            float3x3 Rv = mul(RV, rotZ(tip * sgn));
            dsClip(h, rd, feet, Rv, FN_STAGGER, FN_STAGGER_N, min(T, KT), 63, fx);
        }
        fnPoof(h, rd, feet + float3(0, 0.4, 0) + A * 0.3, (T - KT - 28.0) / 20.0, seed, 14, 0.07);
        // the stroke
        fnArc(add, rd, MID + float3(0, 0.9, 0), float3(0, 1, 0), -A, 0.95, easeOut((T - 70.0) / 4.0), exp(-max(T - 74.0, 0.0) / 4.0) * step(68.0, T), lerp(glow, rimC, 0.6) * 1.6, h.t);
        fnGround(onG, S, MID, KT / 20.0 - 0.1, 4.0, 0.9, 4.0, seed + 31.0, t, o, dec, decK, add, glow);
        fnDebris(h, rd, MID, t - (KT / 20.0 - 0.1), seed + 33.0, 1.6, floorY);
        fnCrit(h, rd, feet + float3(0, 1.05, 0) + A * 0.3, (T - KT) / 20.0, seed, 22, 2.6);
        add += warm * fnFlash(rd, MID + float3(0, 1.0, 0), 0.6, h.t) * exp(-max(T - KT, 0.0) / 2.0) * step(KT, T) * 0.55;
        fnPoof(h, rd, MID + float3(0, 0.4, 0), (T - KT) / 20.0, seed + 5.0, 10, 0.06);
    } else if (sc == 8) {
        // ============ SHIELD COUNTER: they drop; a phantom rises beside the body, walks out towards you, turns -
        // and gives you the finger. A flash off its head, and it's gone
        float T = t * 20.0;
        float u = T / 20.0;
        float tip = 1.5708 * saturate(u / 0.55) * saturate(u / 0.55);
        fx.hurt = saturate(1.0 - u * 2.0);
        float3 toCam = -vf;
        float3x3 RC = rotY(atan2(toCam.x, toCam.z));
        if (T < 52.0) dsBody(h, rd, feet, mul(RV, rotZ(tip * sgn)), 1.0, -99.0, 99.0, fx);
        fnPoof(h, rd, feet + float3(0, 0.3, 0) + mul(RV, float3(sgn * 0.6, 0, 0)), (T - 51.0) / 20.0, seed, 14, 0.07);
        float3 AS = feet + A * 1.1 - toCam * 0.5;
        float  walk = saturate((T - 4.0) / 28.0);
        float3 AO = AS + toCam * 2.0 * walk;
        fg.ash = max(1.0 - saturate(T / 6.0) * 1.2, ease((T - 66.0) / 12.0));
        DRig g = dsClip(h, rd, AO, RC, FN_TAUNT, FN_TAUNT_N, max(T - 4.0, 0.0), 63, fg);
        if (g.fing > 1.05 && fg.ash < 0.95) {   // the finger: up off the fist, two pixels to three as it extends
            float L = 1.5 * g.fing;
            float3 ax = normalize(g.fingD);
            dsBox(h, rd, g.hand + g.handD * 1.2 * DPX + ax * L * DPX, float3(0.6, L, 0.6) * DPX, basisOf(-ax, 0.0), hex3(10, 10, 14), rimC * 1.4);
        }
        float TF = 55.0;   // the gesture lands (tick 51 of the clip)
        add += float3(0.86, 0.91, 1.0) * fnFlash(rd, g.head, 0.5, h.t) * exp(-max(T - TF, 0.0) / 2.5) * step(TF, T) * 0.6;
        fnCrit(h, rd, g.head, (T - TF) / 20.0, seed + 3.0, 16, 2.0);
        fnGround(onG, S, AO, TF / 20.0, 2.0, 0.0, 0.0, seed + 8.0, t, o, dec, decK, add, rimC * 0.8);
        fnPoof(h, rd, AO + float3(0, 0.9, 0), (T - TF) / 20.0, seed + 4.0, 8, 0.05);
    } else if (sc == 9 || sc == 10) {
        // ============ HEADBUTT (Quick Finisher 4) / KICK (Quick Finisher 5): a phantom steps out of the air right in
        // front of them; one blow, the victim reels (startle / pain), and goes down
        bool  hb = sc == 9;
        float T = (t - 0.45) * 20.0;
        float HT = hb ? 4.0 : 5.0, KT = hb ? 14.0 : 34.0;
        float gap = hb ? 0.66 : 0.76;
        float3 AO = feet + A * gap;
        fg.ash = max(1.0 - saturate(t / 0.4) * 1.15, ease((T - KT - 8.0) / 12.0));
        DRig g = dsClip(h, rd, AO, RA, hb ? FN_HEADBUTT : FN_KICK, hb ? FN_HEADBUTT_N : FN_KICK_N, max(T, 0.0), 63, fg);
        fnPoof(h, rd, AO + float3(0, 0.8, 0), t - 0.05, seed + 2.0, 8, 0.05);   // where it formed
        float u = max(T - KT, 0.0) / 20.0;
        float dir = hb ? -1.0 : sgn;                                            // the headbutt sends them over backwards
        float tip = 1.5708 * saturate(u / 0.55) * saturate(u / 0.55);
        fx.hurt = max(fx.hurt, saturate(1.0 - (T - HT) / 8.0) * step(HT, T));
        fx.flash = step(HT, T) * step(T, HT + 1.5);
        if (T < KT + 20.0) {
            float3x3 Rv;
            if (hb) {   // the startle already tips them back 70 degrees; the death carries it on to the ground
                Rv = mul(RV, rotX(-0.35 * tip));
                dsClip(h, rd, feet, Rv, FN_STARTLE, FN_STARTLE_N, max(T - HT, 0.0), 63, fx);
            } else {
                Rv = mul(RV, rotZ(tip * dir));
                dsClip(h, rd, feet, Rv, FN_PAIN, FN_PAIN_N, min(max(T - HT, 0.0), KT - HT), 63, fx);
            }
        }
        fnPoof(h, rd, feet + float3(0, 0.3, 0) - A * (hb ? 1.0 : 0.0), (T - KT - 20.0) / 20.0, seed, 14, 0.07);
        float3 MID = (feet + AO) * 0.5;
        float3 HP = hb ? feet + float3(0, 1.62, 0) + A * 0.18 : MID + float3(0, 0.85, 0);
        float  ut = (T - HT) / 20.0;
        add += float3(1.0, 0.95, 0.82) * fnFlash(rd, HP, 0.5, h.t) * exp(-max(T - HT, 0.0) / 2.0) * step(HT, T) * 0.6;
        fnCrit(h, rd, HP, ut, seed, 20, 2.4);
        if (!hb) {
            fnArc(add, rd, MID + float3(0, 0.85, 0), float3(0, -1, 0), -A, 0.6, easeOut(ut / 0.12), exp(-max(ut, 0.0) * 9.0) * step(0.0, ut), lerp(glow, rimC, 0.6) * 1.5, h.t);
            dsBurst(h, rd, feet + float3(0, 1.7, 0), ut, 10, seed + 6.0, 1.3, 1.0, 6.0, 0.7, 0.03, hex3(140, 20, 24), hex3(90, 10, 14), -1e5, 0.0);   // damage hearts
        }
        fnPoof(h, rd, MID + float3(0, 0.7, 0), ut, seed + 5.0, 9, 0.05);
        fnGround(onG, S, MID, (HT + 1.0) / 20.0 + 0.45, 1.6, 0.0, 0.0, seed + 8.0, t, o, dec, decK, add, glow * 0.7);
        fnDebris(h, rd, AO, ut + 0.1, seed + 9.0, 0.45, floorY);
    } else if (sc == 11) {
        // ============ ULTIMATE ATTACK: the phantom crouches, draws both hands back, and throws them forward - a
        // blast runs along the ground, a chain of explosions, and it throws the victim, burnt, across the island
        float T = (t - 0.4) * 20.0;
        float3 AO = feet + A * 3.6;
        fg.ash = max(1.0 - saturate(t / 0.4) * 1.15, ease((t - 3.2) / 0.7));
        DRig g = dsClip(h, rd, AO, RA, FN_ULT, FN_ULT_N, max(T, 0.0), 63, fg);
        float3 dirB = -A;
        float  tb = 0.4 + 0.75, vb = 9.0;                 // the blast leaves the hands at the clip's 0.75 s
        float3 hands = (g.hand + g.handL) * 0.5;
        float  beam = saturate((t - tb) / 0.06) * (1.0 - ease((t - 2.7) / 0.3));
        float3 B0 = AO + dirB * 0.45 + float3(0, 1.1, 0);
        if (beam > 0.0) {   // the beam off the hands
            float run = min((t - tb) * vb, 7.0);
            float3 B1 = B0 + dirB * run + float3(0, -0.6 * saturate(run / 4.0), 0);
            float  fl = 0.85 + 0.15 * sin(TIME * 60.0);
            add += float3(1.0, 0.82, 0.42) * dsGlowSeg(rd, hands, B1, 0.16, h.t) * beam * 0.55 * fl;
            add += float3(1.2, 1.15, 1.05) * dsGlowSeg(rd, hands, B1, 0.05, h.t) * beam * 1.0 * fl;
            add += float3(1.0, 0.85, 0.5) * fnFlash(rd, hands, 0.35, h.t) * beam * 0.7;
        }
        // the blast points: one every half block along the line, each a short fireball, a scorch and a few stones
        [loop] for (int k = 0; k < 12; k++) {
            float  d = 0.6 + 0.55 * float(k);
            float  tk = tb + d / vb;
            float  uk = t - tk;
            if (uk < -0.05) break;
            float3 Pk = AO + dirB * d + A * 0.0 + float3((h1(seed + float(k)) - 0.5) * 0.5, 0, (h1(seed + float(k) * 3.3) - 0.5) * 0.5);
            float  rk = 0.45 + 0.6 * saturate(float(k) / 6.0);
            if (uk < 0.6 && uk > 0.0) {
                float f = exp(-uk * 6.0);
                add += float3(1.0, 0.82, 0.55) * fnFlash(rd, Pk + float3(0, 0.4 + uk * 1.2, 0), rk * (0.45 + 0.6 * uk), h.t) * f * 0.8;
                add += float3(1.0, 0.98, 0.9) * fnFlash(rd, Pk + float3(0, 0.3, 0), rk * 0.28, h.t) * f * f * 0.9;
            }
            if (k % 2 == 0) dsBurst(h, rd, Pk + float3(0, 0.05, 0), uk, 6, seed + float(k) * 13.0, 1.4, 3.0, 14.0, 1.0, 0.045, hex3(70, 60, 52), hex3(30, 26, 24), floorY, float3(0.6, 0.25, 0.05) * exp(-max(uk, 0.0) * 3.0));
            if (onG) {
                float2 gq = S.xz - Pk.xz;
                float  sc2 = exp(-dot(gq, gq) / (rk * rk * 0.5)) * saturate(uk * 8.0) * (1.0 - ease((t - 4.0) / 0.6));
                if (sc2 > 0.02) { dec = (decK > 0.0 ? dec : o) * (1.0 - 0.75 * sc2); decK = 1.0; add += float3(1.0, 0.35, 0.05) * sc2 * exp(-max(uk, 0.0) * 1.5) * 0.25; }
            }
        }
        // the victim: caught as the chain reaches them, thrown along it (Ragdollified's side throw, turned to the blast)
        float th = tb + 3.6 / vb;
        float3x3 RT = rotY(atan2(dirB.x, dirB.z) - 1.5708);
        fx.charK = ease((t - th) / 0.5);
        fx.ash = ease((t - 3.4) / 0.9);
        fx.flash = step(th, t) * step(t, th + 0.06);
        fx.hurt = saturate(1.0 - (t - th) * 3.0) * step(th, t);
        if (t < th) dsBody(h, rd, feet, RV, 1.0, -99.0, 99.0, fx);
        else dsRagdoll(h, rd, feet, RT, RD_SIDE, (t - th) * 20.0, false, -1, fx);
        fnCrit(h, rd, feet + float3(0, 1.0, 0), t - th, seed + 1.0, 16, 2.5);
    } else if (sc == 12 || sc == 13) {
        // ============ RAGDOLLIFIED: the killing blow throws the body as a ragdoll - it flies, lands, flops, a limb
        // may tear away on a hard landing; it lies a moment and then comes apart
        bool  melee = sc == 12;
        bool  mir = h1(seed * 0.29) < 0.5;
        uint  off = melee ? RD_MELEE : RD_SIDE;
        float3x3 RB = rotY(yaw);
        fx.hurt = saturate(1.0 - t * 2.2);
        fx.flash = step(t, 0.05);
        fx.ash = ease((t - 2.7) / 0.8);
        int sv = melee ? (mir ? 3 : 2) : -1;
        dsRagdoll(h, rd, feet, RB, off, t * 20.0, mir, sv, fx);
        if (melee) {   // the tear: blood off the shoulder, a spatter where it lands
            float3 P; float3x3 R;
            dtRag(off, 0.86 * 20.0, 1, mir, P, R);
            float3 sh = feet + mul(RB, P + mul(R, float3(mir ? 5.0 : -5.0, 4.0, 0) * DPX));
            dsBurst(h, rd, sh, t - 0.86, 14, seed + 4.0, 1.5, 1.8, 12.0, 0.9, 0.032, hex3(110, 2, 2), hex3(181, 18, 18), floorY, 0.0);
            if (onG) { float2 bq = floor((S.xz - sh.xz) * 16.0) / 16.0; float bl = step(hash12(floor(bq * 16.0) + seed), 0.55) * step(length(bq), 0.42 * easeOut((t - 0.9) / 0.4)); if (bl > 0.0) { dec = lerp(decK > 0.0 ? dec : o, pow(hex3(120, 6, 6), 1.0 / 2.2), 0.85 * (1.0 - ease((t - 3.6) / 0.6))); decK = 1.0; } }
        }
        int hb = melee ? 0 : 6;
        [unroll] for (int k = 0; k < 6; k++) {   // dust where it slams down (the bake's own landings)
            float4 e = kRagHit[hb + k];
            float3 Pk = feet + mul(RB, float3(mir ? -e.x : e.x, 0.02, e.y));
            dsBurst(h, rd, Pk, t - e.z, 5, seed + 50.0 + float(k), 0.9 * e.w, 0.5, 3.0, 0.7, 0.04, hex3(176, 168, 152), hex3(140, 132, 120), floorY, 0.0);
        }
        float3 Pc; float3x3 Rc;
        dtRag(off, 96.0, 1, mir, Pc, Rc);
        dsBurst(h, rd, feet + mul(RB, Pc), t - 2.75, 18, seed + 7.0, 1.4, 1.6, 2.5, 1.0, 0.045, team * 0.7, team * 0.35, -1e5, glow * 0.6);
    } else {
        // ============ SABLE RAGDOLL: the body goes limp where it stood and falls under its own weight - knees fold,
        // it slumps; or it is knocked over and rolls (the ragdoll's own roll) and reaches out once. Then it is gone
        bool  slump = sc == 14;
        bool  mir = h1(seed * 0.29) < 0.5;
        uint  off = slump ? SB_SLUMP : SB_ROLL;
        float3x3 RB = rotY(yaw);
        fx.hurt = saturate(1.0 - t * 2.5);
        if (t < 3.4) dsRagdoll(h, rd, feet, RB, off, t * 20.0, mir, -1, fx);   // it vanishes with the poof
        int hb = slump ? 12 : 18;
        [unroll] for (int k = 0; k < 6; k++) {   // the thuds
            float4 e = kRagHit[hb + k];
            float3 Pk = feet + mul(RB, float3(mir ? -e.x : e.x, 0.02, e.y));
            dsBurst(h, rd, Pk, t - e.z, 6, seed + 50.0 + float(k), 0.8, 0.35, 2.0, 0.8, 0.045, hex3(176, 168, 152), hex3(140, 132, 120), floorY, 0.0);
            if (onG) { float2 gq = S.xz - Pk.xz; float k2 = exp(-dot(gq, gq) / 0.08) * saturate((t - e.z) * 10.0) * 0.25 * (1.0 - ease((t - 3.4) / 0.4)); dec = (decK > 0.0 ? dec : o) * (1.0 - k2); decK = 1.0; }
        }
        float3 Pc; float3x3 Rc;
        dtRag(off, min(t, 3.4) * 20.0, 1, mir, Pc, Rc);
        fnPoof(h, rd, feet + mul(RB, Pc) + float3(0, 0.15, 0), t - 3.4, seed, 16, 0.08);
    }
}
