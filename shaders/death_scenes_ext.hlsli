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
        if (T < KT + 18.0) {   // a vanilla death: over, a beat on the ground, poof (inside the scene's 4.8 s)
            float3x3 Rv = mul(RV, rotZ(tip * sgn));
            dsClip(h, rd, feet, Rv, FN_STAGGER, FN_STAGGER_N, min(T, KT), 63, fx);
        }
        fnPoof(h, rd, feet + float3(0, 0.3, 0) + A * 0.3 + mul(RV, float3(sgn * 0.5, 0, 0)), (T - KT - 18.0) / 20.0, seed, 14, 0.07);
        // the stroke
        fnArc(add, rd, MID + float3(0, 0.9, 0), float3(0, 1, 0), -A, 0.95, easeOut((T - 70.0) / 4.0), exp(-max(T - 74.0, 0.0) / 4.0) * step(68.0, T), lerp(glow, rimC, 0.6) * 1.6, h.t);
        fnGround(onG, S, MID, KT / 20.0 - 0.1, 4.0, 0.9, 4.0, seed + 31.0, t, o, dec, decK, add, glow);
        fnDebris(h, rd, MID, t - (KT / 20.0 - 0.1), seed + 33.0, 1.6, floorY);
        fnCrit(h, rd, feet + float3(0, 1.05, 0) + A * 0.3, (T - KT) / 20.0, seed, 22, 2.6);
        add += warm * fnFlash(rd, MID + float3(0, 1.0, 0), 0.6, h.t) * exp(-max(T - KT, 0.0) / 2.0) * step(KT, T) * 0.55;
        fnPoof(h, rd, MID + float3(0, 0.4, 0), (T - KT) / 20.0, seed + 5.0, 10, 0.06);
    } else if (sc == 8) {
        // ============ SHIELD COUNTER (the mod's own beats): the blow lands and the victim doubles over; a phantom
        // forms at their side and walks off across your view (no blade - the mod hides the held item), stops, turns
        // to face you and gives you the finger - a cold flash off its head, a ring at its feet. Only then, behind it,
        // the victim drops (the mod's kill tick, 81). Played at 1.1x so the death and its poof fit the scene.
        float T = t * 22.0;
        float KT = 81.0;
        // the victim: doubled over (Finishers' pain, held), then a vanilla death at the kill tick
        float u = max(T - KT, 0.0) / 20.0;
        float tip = 1.5708 * saturate(u / 0.5) * saturate(u / 0.5);
        fx.hurt = max(saturate(1.0 - T / 10.0), saturate(1.0 - u * 2.5) * step(KT, T));
        if (T < KT + 16.0) dsClip(h, rd, feet, mul(RV, rotZ(tip * sgn)), FN_PAIN, FN_PAIN_N, min(T * 0.6, 12.0), 63, fx);
        fnPoof(h, rd, feet + float3(0, 0.3, 0) + mul(RV, float3(sgn * 0.55, 0, 0)), (T - KT - 16.0) / 20.0, seed, 14, 0.07);
        // the phantom: walks 3.2 blocks across the view (the mod's movement keys), then turns to face the viewer
        float pr = T < 6.0 ? 0.08 * (T / 6.0) * (T / 6.0)
                 : (T < 18.0 ? lerp(0.08, 0.7, smoothstep(0.0, 1.0, (T - 6.0) / 12.0)) : lerp(0.7, 1.0, easeOut(saturate((T - 18.0) / 10.0))));
        float3 AS = feet + A * 0.75;
        float3 AO = AS + A * 3.2 * pr;
        float3 toCam = -vf;
        float  yW = atan2(A.x, A.z), yC = atan2(toCam.x, toCam.z);
        float  dy = frac((yC - yW) / 6.2832 + 0.5) * 6.2832 - 3.14159265;     // the short way round
        float  yaw8 = yW + dy * ease(saturate((T - 28.0) / 16.0));
        fg.ash = max(1.0 - saturate(T / 6.0) * 1.2, ease((T - 70.0) / 14.0));
        DRig g = dsClip(h, rd, AO, rotY(yaw8), FN_TAUNT, FN_TAUNT_N, T, 63, fg);
        fnPoof(h, rd, AS + float3(0, 0.8, 0), T / 20.0, seed + 2.0, 8, 0.05);   // where it formed
        if (g.fing > 1.02 && fg.ash < 0.95) {   // the finger: up off the fist as the bone scales out (1 -> 2)
            float L = 1.8 * g.fing;
            float3 ax = normalize(g.fingD);
            dsBoxR(h, rd, g.hand + g.handD * 1.0 * DPX + ax * L * DPX, float3(0.85, L, 0.85) * DPX, basisOf(-ax, 0.0), hex3(70, 64, 86), rimC * 3.2, 1.0);
        }
        float TF = 51.0;   // the gesture lands: flash, burst off the head, cloud, the ring at its feet a tick later
        add += float3(0.86, 0.91, 1.0) * fnFlash(rd, g.head, 0.55, h.t) * exp(-max(T - TF, 0.0) / 3.0) * step(TF, T) * 0.7;
        fnCrit(h, rd, g.head, (T - TF) / 20.0, seed + 3.0, 18, 2.0);
        fnPoof(h, rd, AO + float3(0, 0.9, 0), (T - TF) / 20.0, seed + 4.0, 10, 0.05);
        fnGround(onG, S, AO, (TF + 1.0) / 22.0, 1.4, 0.0, 0.0, seed + 8.0, t, o, dec, decK, add, float3(0.86, 0.91, 1.0) * 0.9);
    } else if (sc == 9 || sc == 10) {
        // ============ HEADBUTT (Quick Finisher 4) / KICK (Quick Finisher 5): a phantom steps out of the air right in
        // front of them; one blow, the victim reels (startle / pain), and goes down
        bool  hb = sc == 9;
        float T = (t - 0.45) * 20.0;
        float HT = hb ? 4.0 : 5.0, KT = hb ? 14.0 : 34.0;
        float gap = hb ? 0.66 : 0.76;
        float3 AO = feet + A * gap;
        // it goes as soon as its clip is done (the kick would otherwise stand there on one leg)
        float CE = hb ? 19.0 : 23.0;
        fg.ash = max(1.0 - saturate(t / 0.4) * 1.15, ease((T - CE - 1.0) / 10.0));
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
        // ============ ULTIMATE ATTACK - an anime energy wave (the mod's ultimate.attack clip): the phantom forms and
        // powers up - a flickering aura of rising flame-streaks, stones lifting off the ground around it, cracks
        // spreading under its feet, sparks spiralling in to a ball of light between its cupped palms (the mod's 15-tick
        // charge) - then thrusts: a blue-white wave with a white-hot core, energy coiling round it and a fat bright
        // head, the stones blown away, a glowing scorched trench under it. It catches the victim, holds them a beat in
        // the light and hurls them down the line; the wave ends in a dome of light and a crater; they crumble to ash.
        float T = (t - 0.4) * 20.0;                        // the clip's ticks
        float3 dirB = -A;
        float3 AO = feet + A * 3.0;
        fg.ash = max(1.0 - saturate(t / 0.4) * 1.15, ease((t - 3.2) / 0.7));
        DRig g = dsClip(h, rd, AO, RA, FN_ULT, FN_ULT_N, max(T, 0.0), 63, fg);
        float3 hands = (g.hand + g.handL) * 0.5;
        float3 cBeam = float3(0.30, 0.72, 1.0), cCore = float3(1.15, 1.25, 1.35);
        float  tb = 0.4 + 0.75, vb = 12.0, LB = 8.5;
        // the power-up: an aura of flame-streaks rising round the body, flickering; it flares as it fires
        float aura = saturate((t - 0.25) / 0.3) * (1.0 - ease((t - 2.6) / 0.4)) * (1.0 + 0.8 * exp(-max(t - tb, 0.0) * 4.0) * step(tb, t));
        if (aura > 0.0 && nearRay(rd, AO + float3(0, 1.0, 0), 2.8)) {   // wide enough that the glow fades out, never cut
            [unroll] for (int k = 0; k < 9; k++) {
                float a = float(k) * 0.698 + h1(seed + float(k)) * 0.5;
                float ph = frac(t * (1.6 + 0.5 * h1(seed + float(k) * 3.1)) + h1(seed + float(k) * 5.3));
                float r = 0.36 + 0.08 * sin(t * 9.0 + float(k));
                float3 b0 = AO + float3(cos(a) * r, 0.1 + ph * 0.8, sin(a) * r);
                float3 b1 = b0 + float3(cos(a) * 0.08, 0.55 + 0.3 * h1(seed + float(k) * 7.0), sin(a) * 0.08);
                add += cBeam * dsGlowSeg(rd, b0, b1, 0.05, h.t) * aura * 0.55 * sin(ph * 3.14159);
            }
            add += cBeam * fnFlash(rd, AO + float3(0, 0.95, 0), 0.6, h.t) * aura * 0.16;
        }
        // stones lifting off the ground round it while it charges, jittering; blown away from the wave as it fires
        [unroll] for (int k = 0; k < 7; k++) {
            float a = float(k) * 0.9 + h1(seed + float(k) * 2.2) * 0.6, r = 1.0 + 0.7 * h1(seed + float(k) * 4.4);
            float rise = saturate((t - 0.35 - 0.06 * float(k)) / 0.7);
            float blow = max(t - tb, 0.0);
            float3 P = AO + float3(cos(a) * r, 0.05 + 0.35 * easeOut(rise) + 0.04 * sin(t * 13.0 + float(k)), sin(a) * r);
            P += normalize(float3(cos(a), 0.0, sin(a)) + A * 0.8) * blow * 7.0 + float3(0, blow * 2.0 - 9.0 * blow * blow, 0);
            float sz = 0.05 + 0.05 * h1(seed + float(k) * 6.6);
            if (rise > 0.0 && blow < 0.8) dsBox(h, rd, P, float3(sz, sz * 0.8, sz), rotY(float(k) + t * 2.0 * step(tb, t)), hex3(104, 98, 90), cBeam * 0.15 * aura);
        }
        if (onG) {   // cracks spreading under its feet with the charge
            float2 gq = floor((S.xz - AO.xz) * 16.0) / 16.0;
            float ck = fnCrack(gq, 1.9, seed + 3.0, saturate((t - 0.4) / 0.75), 9) * (1.0 - ease((t - 4.1) / 0.6));
            if (ck > 0.0) { dec = (decK > 0.0 ? dec : o) * (1.0 - 0.75 * ck); decK = 1.0; add += cBeam * ck * 0.25 * aura; }
        }
        // the charge: sparks spiralling in to the ball between the palms; the ball grows and throbs
        float chg = saturate(T / 15.0);
        if (T > -2.0 && T < 16.0 && nearRay(rd, hands, 1.4)) {
            [unroll] for (int k = 0; k < 10; k++) {
                float ph = frac(T / 9.0 + float(k) * 0.1);                       // each spark: out -> in, over and over
                float a = T * 0.42 + float(k) * 0.628, r = 1.0 * (1.0 - ph);
                float3 P = hands + float3(cos(a) * r, (h1(seed + float(k)) - 0.5) * 0.8 * (1.0 - ph), sin(a) * r);
                add += cBeam * fnFlash(rd, P, 0.035, h.t) * saturate((T + 2.0) / 3.0) * 2.0 * ph;
            }
        }
        float ballR = (0.06 + 0.16 * chg) * (0.9 + 0.1 * sin(TIME * 40.0));
        float ballK = step(-0.5, T) * (1.0 - ease((t - 2.7) / 0.3));
        add += cBeam * fnFlash(rd, hands, ballR * 2.2, h.t) * ballK * (0.5 + 0.6 * chg);
        add += cCore * fnFlash(rd, hands, ballR * 0.8, h.t) * ballK * (0.8 + 1.2 * chg);
        // the wave: out from the palms at 12 blocks/s, low along the ground, 8.5 blocks; thins away at the end
        float run = clamp((t - tb) * vb, 0.0, LB);
        float beam = step(tb, t) * (1.0 - ease((t - 2.5) / 0.35));
        float3 B0 = AO + dirB * 0.45 + float3(0, 1.05, 0);
        float3 B1 = B0 + dirB * run + float3(0, -0.55 * saturate(run / 5.0), 0);
        if (beam > 0.0 && run > 0.05) {
            float fl = 0.88 + 0.12 * sin(TIME * 55.0) + 0.05 * sin(TIME * 91.0);
            float wide = beam * (0.75 + 0.25 * saturate((t - tb) / 0.15));
            add += cBeam * dsGlowSeg(rd, hands, B1, 0.30 * wide, h.t) * 0.40 * fl;   // the outer glow
            add += cBeam * dsGlowSeg(rd, hands, B1, 0.14 * wide, h.t) * 0.75 * fl;   // the body
            add += cCore * dsGlowSeg(rd, hands, B1, 0.05 * wide, h.t) * 1.30 * fl;   // the white-hot core
            float3 bd = normalize(B1 - hands), bu = normalize(cross(bd, float3(0, 1, 0))), bv = cross(bu, bd);
            [unroll] for (int k = 0; k < 10; k++) {                               // energy coiling round it
                float u0 = (float(k) / 10.0) * run, u1 = u0 + run / 10.0;
                float a0 = u0 * 4.0 - t * 18.0, a1 = u1 * 4.0 - t * 18.0, rr = 0.24 * wide;
                float3 q0 = hands + bd * u0 + (bu * cos(a0) + bv * sin(a0)) * rr, q1 = hands + bd * u1 + (bu * cos(a1) + bv * sin(a1)) * rr;
                add += cCore * dsGlowSeg(rd, q0, q1, 0.03, h.t) * 0.5 * beam;
            }
            [unroll] for (int k = 0; k < 6; k++) {                                // pulses racing down it
                float u = frac(t * 2.6 + float(k) / 6.0) * run;
                float3 Pp = lerp(hands, B1, u / max(run, 0.01));
                add += cCore * fnFlash(rd, Pp, 0.16 * wide, h.t) * 0.55 * beam;
            }
            add += cBeam * fnFlash(rd, B1, 0.62 * wide, h.t) * 0.8 * beam * (run < LB ? 1.0 : 0.5);   // its head
            add += cCore * fnFlash(rd, B1, 0.30 * wide, h.t) * 1.3 * beam * (run < LB ? 1.0 : 0.5);
            add += cBeam * fnFlash(rd, hands, 0.45, h.t) * 0.8 * beam;                               // the muzzle
        }
        // the ground under it: a scorched trench with a glowing edge; dust and stones kicked up as the head passes
        if (onG && run > 0.0) {
            float3 gq3 = S - (AO + dirB * 0.6);
            float along = dot(gq3.xz, dirB.xz), across = abs(dot(gq3.xz, float2(-dirB.z, dirB.x)));
            float n = hash12(floor(S.xz * 16.0) + seed);
            float wdt = 0.42 + 0.12 * sin(along * 3.1 + seed) + (n - 0.5) * 0.08;
            float inLine = step(0.0, along) * step(along, run - 0.2);
            float fade = 1.0 - ease((t - 4.1) / 0.6);
            float burn = inLine * smoothstep(wdt, wdt * 0.5, across) * fade;
            float edge = inLine * exp(-pow((across - wdt) / 0.07, 2.0)) * fade;
            float age = t - (tb + along / vb);
            if (burn > 0.01 || edge > 0.05) {
                dec = (decK > 0.0 ? dec : o) * (1.0 - 0.82 * burn); decK = 1.0;
                add += lerp(float3(1.0, 0.45, 0.08), cBeam, 0.5) * (edge * 0.8 + burn * 0.3) * exp(-max(age, 0.0) * 1.1) * (0.6 + 0.4 * n);
            }
        }
        [loop] for (int k = 0; k < 8; k++) {
            float d = 0.8 + 1.0 * float(k);
            float uk = t - (tb + d / vb);
            if (uk < 0.0) break;
            float3 Pk = AO + dirB * d;
            Pk.y = floorY;
            dsBurst(h, rd, Pk + float3(0, 0.05, 0), uk, 5, seed + float(k) * 13.0, 1.3, 2.6, 14.0, 1.0, 0.045, hex3(70, 60, 52), hex3(40, 34, 30), floorY, float3(0.6, 0.25, 0.05) * exp(-max(uk, 0.0) * 3.0));
            if (k % 3 == 1) dsBurst(h, rd, Pk + float3(0, 0.2, 0), uk, 5, seed + float(k) * 17.0, 0.8, 1.0, -0.6, 0.9, 0.09, hex3(200, 196, 190), hex3(150, 146, 140), -1e5, 0.0);
        }
        // the victim: caught when the head reaches them, held a beat in the light, then hurled down the line
        float th = tb + 2.55 / vb, tl = th + 0.15;
        float3x3 RT = rotY(atan2(dirB.x, dirB.z) - 1.5708);
        fx.charK = ease((t - th) / 0.45);
        fx.ash = ease((t - 3.45) / 0.85);
        fx.flash = step(th, t) * step(t, th + 0.05);
        fx.hurt = saturate(1.0 - (t - th) * 2.0) * step(th, t);
        if (t < tl) {
            float jit = step(th, t) * 0.03;
            dsBody(h, rd, feet + float3(sin(TIME * 70.0) * jit, 0.0, cos(TIME * 63.0) * jit), RV, 1.0, -99.0, 99.0, fx);
        } else dsRagdoll(h, rd, feet, RT, RD_SIDE, (t - tl) * 20.0, false, -1, fx);
        add += cCore * fnFlash(rd, feet + float3(0, 1.0, 0), 0.9, h.t) * exp(-max(t - th, 0.0) * 6.0) * step(th, t) * 1.0;
        fnCrit(h, rd, feet + float3(0, 1.0, 0), t - th, seed + 1.0, 18, 2.8);
        // the end of the wave: a dome of light, a crater and stones
        float te = tb + LB / vb;
        float3 PE = AO + dirB * (LB + 0.45);
        PE.y = floorY;
        float ue = t - te;
        if (ue > 0.0 && ue < 0.9) {
            float rD = 1.7 * easeOut(saturate(ue / 0.35));
            add += cBeam * fnFlash(rd, PE + float3(0, 0.3, 0), rD, h.t) * (1.0 - ease(ue / 0.9)) * 1.0;
            add += cCore * fnFlash(rd, PE + float3(0, 0.3, 0), rD * 0.45, h.t) * exp(-ue * 5.0) * 1.4;
        }
        fnGround(onG, S, PE, te, 3.4, 1.15, 2.6, seed + 21.0, t, o, dec, decK, add, cBeam);
        fnDebris(h, rd, PE, ue, seed + 23.0, 1.4, floorY);
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
