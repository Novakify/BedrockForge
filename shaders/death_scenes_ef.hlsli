// ---- death scenes 65-81: Epic Fight's signature weapon skills (Epic Fight by Yesman and its team, used with
//      permission; Bedrock / Renderer Foundation port by Novak). The phantom performs the skill from Epic Fight's own
//      animation files, posed exactly as the mod poses them (its loader's coordinate fix, its 20-joint armature) and
//      skinned the way its biped mesh is: every limb split at the elbow / knee / waist and carried by its own joint,
//      baked by tools/gen_death_tables.py (kEFClip: clip, frames, weapon, where the last hit lands) - holding the
//      skill's weapon, with a trail through every live swing. The victim takes each hit (the swing's speed peaks,
//      kEFHit), and the last one finishes them the way the skill would.
//       0 The Guillotine (axe)        1 Heartpiercer (spear)        2 Eviscerate (dagger)      3 Sweeping Edge (sword)
//       4 Dancing Edge (dual swords)  5 Battojutsu (uchigatana)     6 Blade Rush (dagger)      7 Wrathful Lightning (trident)
//       8 Tsunami (trident)           9 Steel Whirlwind (greatsword) 10 Demolition Leap (greatsword) 11 Relentless Combo (fists)
//      12 Rushing Tempo (tachi)      13 Grasping Spire (spear)     14 Sharp Stab (longsword)  15 Revelation (longsword)
//      16 Greatsword Slam (greatsword)
#define EF_SWORD 0
#define EF_LONG 1
#define EF_GREAT 2
#define EF_KATANA 3
#define EF_DAGGER 4
#define EF_DUAL 5
#define EF_SPEAR 6
#define EF_TRIDENT 7
#define EF_AXE 8
#define EF_FIST 9

float efCull(int k) {
    float d = float(kEFClip[k].w) * 0.01;
    return max(5.0, d + 3.0);   // the phantom starts d blocks off (the dashes from far): everything is within d + 3
}
float3 efAccent(int k) {   // each skill's light
    if (k == 7) return float3(1.0, 0.95, 0.45);    // lightning
    if (k == 8) return float3(0.0, 0.91, 0.96);    // tsunami (its own trail colour, 0 232 245)
    if (k == 5 || k == 12) return float3(1.0, 0.25, 0.3);
    if (k == 9 || k == 10 || k == 16) return float3(1.0, 0.7, 0.35);
    return float3(0.85, 0.85, 1.0);
}
// one entry e of an Epic Fight clip at frame fr (20 Hz): centre (blocks, attacker frame) + rotation. Entries 0-10 are
// the skin segments (EF_SEG below), 11 the right hand's Tool joint, 12 the left's
void efEnt(uint off, uint n, float fr, int e, out float3 P, out float3x3 R) {
    fr = clamp(fr, 0.0, float(n - 1u));
    uint i0 = uint(fr), i1 = min(i0 + 1u, n - 1u);
    float u = fr - float(i0);
    uint a = kEFBase + off + (i0 * 13u + uint(e)) * 4u, c = kEFBase + off + (i1 * 13u + uint(e)) * 4u;
    float3 pa = float3(dtH(dtU(a)), dtH(dtU(a + 1u)).x), pc = float3(dtH(dtU(c)), dtH(dtU(c + 1u)).x);
    float4 qa = float4(dtH(dtU(a + 1u)).y, dtH(dtU(a + 2u)), dtH(dtU(a + 3u)).x);
    float4 qc = float4(dtH(dtU(c + 1u)).y, dtH(dtU(c + 2u)), dtH(dtU(c + 3u)).x);
    if (dot(qa, qc) < 0.0) qc = -qc;
    P = lerp(pa, pc, u); R = quatM(lerp(qa, qc, u));
}
// the segments: (our part, clip lo, clip hi) - the upper and lower half of each limb overlap a hair so a bent joint
// stays closed
static const float3 kEFSeg[11] = { float3(0, -99, 99), float3(1, -0.5, 99), float3(1, -99, 0.5),
    float3(2, -0.25, 99), float3(2, -99, 0.25), float3(3, -0.25, 99), float3(3, -99, 0.25),
    float3(4, -0.25, 99), float3(4, -99, 0.25), float3(5, -0.25, 99), float3(5, -99, 0.25) };
// each weapon's blade in its Tool frame (z forward), from Epic Fight's own trail definitions: begin, end
void efBlade(uint w, out float3 b0, out float3 b1) {
    b0 = float3(0, 0, 0.2); b1 = float3(0, 0.1, 1.0);
    if (w == EF_SWORD || w == EF_DUAL) { b0 = float3(0, 0.1, 0.1); b1 = float3(0, -0.05, 0.95); }
    else if (w == EF_LONG)   { b1 = float3(0, 0.2, 1.8); }
    else if (w == EF_GREAT)  { b1 = float3(0, -0.24, 2.2); }
    else if (w == EF_KATANA || w == EF_TRIDENT) { b1 = float3(0, -0.22, 1.25); }
    else if (w == EF_SPEAR)  { b0 = float3(0, 0.2, 0.9); b1 = float3(0, 0.25, 2.0); }
    else if (w == EF_AXE)    { b0 = float3(0, -0.15, 0.35); b1 = float3(0, -0.15, 0.7); }
    else if (w == EF_FIST)   { b0 = float3(0, 0, 0); b1 = float3(0, 0, 0.15); }
}
// the phantom's weapon, held in the Tool frame (P, Rt): ink with a lit edge
void efWeapon(inout DHit h, float3 rd, uint w, float3 P, float3x3 Rt, float glint, float3 acc) {
    float3 b0, b1; efBlade(w, b0, b1);
    float3 B0 = P + mul(Rt, b0), B1 = P + mul(Rt, b1);
    float3 d = normalize(B1 - B0), tx = mul(Rt, float3(1, 0, 0));
    float3 Z = normalize(tx - d * dot(tx, d)), X = cross(d, Z);
    float3x3 RB = transpose(float3x3(X, d, Z));               // local y along the blade, z through its flat
    float3 ink = hex3(12, 12, 16), edge = lerp(hex3(236, 226, 255), acc, 0.4) * (0.8 + 2.5 * glint);
    float L = length(B1 - B0);
    if (w == EF_FIST) { dsBox(h, rd, P, float3(0.085, 0.085, 0.085), Rt, ink, edge * 1.2); return; }
    if (w == EF_SPEAR || w == EF_TRIDENT) {   // a shaft through the hand, the head at the trail's end
        float3 butt = P - d * 0.7, tip = B1;
        dsBox(h, rd, (butt + tip) * 0.5, float3(0.028, length(tip - butt) * 0.5, 0.028), RB, hex3(40, 30, 26), edge * 0.15);
        if (w == EF_SPEAR) dsBox(h, rd, tip + d * 0.12, float3(0.06, 0.16, 0.014), RB, ink, edge);
        else { [unroll] for (int i = -1; i <= 1; i++) dsBox(h, rd, tip + d * 0.1 + X * (float(i) * 0.08), float3(0.016, 0.15, 0.016), RB, ink, edge); }
        return;
    }
    if (w == EF_AXE) {   // the haft from the fist, the head where its trail runs
        dsBox(h, rd, P + d * 0.4, float3(0.028, 0.48, 0.028), RB, hex3(40, 30, 26), 0.0);
        dsBox(h, rd, B1 + d * 0.12 + X * 0.1, float3(0.16, 0.16, 0.018), RB, ink, edge);
        return;
    }
    if (w == EF_DAGGER) { B1 = B0 + d * 0.5; L = 0.5; }       // the trail runs past a dagger's point; the dagger itself is short
    float Wd = w == EF_GREAT ? 0.13 : (w == EF_KATANA ? 0.035 : (w == EF_DAGGER ? 0.04 : 0.055));
    dsBox(h, rd, (B0 + B1) * 0.5, float3(Wd, L * 0.5, 0.012), RB, ink, edge);
    dsBox(h, rd, P - d * 0.08, float3(0.022, 0.1, 0.022), RB, hex3(30, 24, 22), 0.0);                    // the grip
    dsBox(h, rd, B0, float3(w == EF_KATANA ? 0.05 : 0.13, 0.022, 0.03), RB, hex3(22, 20, 28), edge * 0.4);   // the guard
}

void deathScenesEF(int k, float t, float3 o, float3 rd, float3 feet, float yaw, float view, float seed, float3 team, float3 glow,
                   bool onG, float3 S, float gr, inout DHit h, inout float3 add, inout float3 dec, inout float decK, inout DFx fx) {
    uint4  m = kEFClip[k];
    uint   off = m.x, n = m.y, w = m.z;
    float  dist = max(float(m.w) * 0.01, 0.9);
    float3 vf = float3(cos(view), 0, sin(view));
    float  sgn = h1(seed * 0.71) < 0.5 ? -1.0 : 1.0;
    float3 A = float3(-vf.z, 0, vf.x) * sgn;                 // across the view: the phantom comes from +A, facing -A
    float3x3 RV = rotY(atan2(A.x, A.z)), RA = rotY(atan2(-A.x, -A.z));
    float3 acc = efAccent(k);
    float  floorY = feet.y;
    float3 C = feet + float3(0, 0.95, 0);
    float  t0 = 0.35;                                         // it forms, then the skill plays at its own speed
    float  T = (t - t0);
    float  fr = max(T, 0.0) * 20.0;
    float  clipEnd = float(n - 1u) / 20.0;
    DFx fg = fx; fg.ghost = 1.0; fg.slot = gKillerSlot; fg.hurt = 0.0;
    fg.ash = max(1.0 - saturate(t / t0) * 1.15, ease((T - clipEnd - 0.15) / 0.5));
    float3 O = feet + A * dist;                               // where it starts; its own root motion carries it from there
    // the phantom: 11 skin segments, each carried by its joint (cheap to skip: nothing near the ray, nothing drawn)
    float3 P1; float3x3 R1;
    efEnt(off, n, fr, 2, P1, R1);                             // the waist: the body's middle
    float3 Pw, PwL; float3x3 Rw, RwL;
    efEnt(off, n, fr, 11, Pw, Rw); Pw = O + mul(RA, Pw); Rw = mul(RA, Rw);
    efEnt(off, n, fr, 12, PwL, RwL); PwL = O + mul(RA, PwL); RwL = mul(RA, RwL);
    if (fg.ash < 0.99 && nearRay(rd, O + mul(RA, P1), 1.6)) {
        gNoCut = true;
        [loop] for (int e = 0; e < 11; e++) {
            float3 Ps; float3x3 Rs;
            efEnt(off, n, fr, e, Ps, Rs);
            float3 sg = kEFSeg[e];
            dsPart(h, rd, int(sg.x), O + mul(RA, Ps), mul(RA, Rs), 1.0, sg.y, sg.z, fg);
        }
        gNoCut = false;
    }
    // the hits: each a flash on the victim and a jolt; the last kills
    uint nh = kEFHits[k];
    float lastHit = kEFHit[k * 6 + max(int(nh) - 1, 0)] + t0;
    float jolt = 0.0, glint = 0.0;
    [loop] for (uint i = 0; i < nh; i++) {
        float th = kEFHit[k * 6 + i] + t0;
        float u = t - th;
        if (u >= 0.0) { jolt += exp(-u * 10.0); fx.hurt = max(fx.hurt, saturate(1.0 - u * 3.0)); }
        glint = max(glint, exp(-pow((t - th) / 0.06, 2.0)));
        if (u >= 0.0 && u < 0.6) {
            float3 hp = C + float3(0, (h1(float(i) + seed) - 0.5) * 0.5, 0) + A * 0.25;
            add += lerp(float3(1, 1, 1), acc, 0.5) * fnFlash(rd, hp, 0.45, h.t) * exp(-u * 14.0) * 0.8;
            fnCrit(h, rd, hp, u, seed + float(i) * 7.0, 10, 2.2);
            dsBurst(h, rd, hp, u, 6, seed + float(i) * 11.0, 1.2, 1.0, 9.0, 0.6, 0.028, hex3(110, 2, 2), hex3(181, 18, 18), floorY, 0.0);
        }
    }
    if (fg.ash < 0.95 && T > -0.2) {
        efWeapon(h, rd, w, Pw, Rw, glint, acc);
        if (w == EF_DUAL || (w == EF_DAGGER && k == 6)) efWeapon(h, rd, w == EF_DUAL ? EF_SWORD : EF_DAGGER, PwL, RwL, glint, acc);
        // the trail, as the mod draws it: a ribbon from the blade's base to its tip over the last few frames of a swing
        float live = 0.0;
        [loop] for (uint i2 = 0; i2 < nh; i2++) { float th = kEFHit[k * 6 + i2] + t0; live = max(live, saturate(1.0 - abs(t - th) / 0.22)); }
        if (live > 0.0 && w != EF_FIST) {
            float3 b0, b1; efBlade(w == EF_DUAL ? EF_SWORD : w, b0, b1);
            float3 prevT = Pw + mul(Rw, b1), prevM = Pw + mul(Rw, lerp(b0, b1, 0.5));
            [loop] for (int s2 = 1; s2 <= 5; s2++) {
                float3 Pp; float3x3 Rp;
                efEnt(off, n, max(fr - float(s2) * 0.6, 0.0), 11, Pp, Rp);
                Pp = O + mul(RA, Pp); Rp = mul(RA, Rp);
                float3 tip = Pp + mul(Rp, b1), mid = Pp + mul(Rp, lerp(b0, b1, 0.5));
                float fade = live * (1.0 - float(s2) / 6.0);
                add += acc * (dsGlowSeg(rd, prevT, tip, 0.03, h.t) * 1.4 + dsGlowSeg(rd, prevM, mid, 0.07, h.t) * 0.35) * fade;
                prevT = tip; prevM = mid;
            }
        }
        if (w == EF_FIST) add += acc * (fnFlash(rd, Pw, 0.12, h.t) + fnFlash(rd, PwL, 0.12, h.t)) * (0.4 + 1.2 * glint);
    }
    // the victim: faces the phantom, jolted by each hit, finished by the last
    float uD = t - lastHit;
    float3 kb = -A * min(jolt, 1.5) * 0.08;
    bool b1 = false, b2 = false; float3 bP1 = feet, bP2 = feet; float3x3 bR1 = RV, bR2 = RV; float bLo = -99.0, bHi = 99.0;
    if (uD < 0.0) {
        { b1 = true; bP1 = feet + kb; bR1 = mul(RV, rotX(-0.15 * min(jolt, 1.0))); bLo = -99.0; bHi = 99.0; }
    } else if (k == 5) {
        // BATTOJUTSU: the phantom is already past; a beat, then the cut it left opens - the top half slides off
        float open = ease((uD - 0.35) / 0.5);
        add += float3(1.0, 0.3, 0.3) * dsGlowSeg(rd, C - A * 0.5 + float3(0, 0.1, 0), C + A * 0.5 - float3(0, 0.25, 0), 0.012, h.t) * step(uD, 0.6) * 2.0;
        if (uD < 1.6) {
            { b1 = true; bP1 = feet; bR1 = RV; bLo = -99.0; bHi = 16.0; }
            { b2 = true; bP2 = feet - A * open * 0.6 - float3(0, open * open * 0.5, 0); bR2 = mul(RV, rotZ(open * 0.6 * sgn)); }
        }
        dsBurst(h, rd, C + float3(0, 0.2, 0), uD - 0.35, 18, seed, 1.6, 1.8, 10.0, 1.1, 0.03, hex3(110, 2, 2), hex3(181, 18, 18), floorY, 0.0);
        fnPoof(h, rd, C, uD - 1.6, seed + 3.0, 14, 0.08);
    } else if (k == 7) {
        // WRATHFUL LIGHTNING: the trident calls it down - a bolt, charring, ash
        if (uD < 0.14) {
            float3 top = feet + float3(0.4, 14.0, 0.2), a0 = top;
            [unroll] for (int i = 0; i < 7; i++) {
                float f1 = float(i + 1) / 7.0;
                float3 a1 = lerp(top, feet + float3(0, 1.8, 0), f1) + (f1 < 0.99 ? float3(h1(seed + float(i)) - 0.5, 0, h1(seed + float(i) * 3.0) - 0.5) * 1.5 : float3(0, 0, 0));
                add += float3(1.0, 0.95, 0.6) * (dsGlowSeg(rd, a0, a1, 0.05, h.t) * 2.5 + dsGlowSeg(rd, a0, a1, 0.25, h.t) * 0.5);
                a0 = a1;
            }
            fx.flash = 1.0;
        }
        add += float3(1.0, 0.95, 0.6) * fnFlash(rd, C, 2.4, h.t) * exp(-uD * 5.0);
        fx.charK = ease(uD / 0.25); fx.ash = ease((uD - 0.8) / 1.2);
        { b1 = true; bP1 = feet; bR1 = RV; bLo = -99.0; bHi = 99.0; }
        if (onG) { float sc2 = exp(-gr * gr / 0.5) * (1.0 - ease((t - 4.1) / 0.6)); dec = (decK > 0.0 ? dec : o) * (1.0 - 0.75 * sc2); decK = max(decK, 1.0); }
    } else if (k == 8) {
        // TSUNAMI: the dash drags a wave with it; the victim is swept off in a burst of water
        float3 sweep = -A * ease(uD / 0.8) * 2.5;
        if (uD < 1.2) { b1 = true; bP1 = feet + sweep + float3(0, sin(saturate(uD / 0.8) * 3.14159) * 0.6, 0); bR1 = mul(RV, rotX(ease(uD / 0.8) * 1.6)); bLo = -99.0; bHi = 99.0; }
        dsBurst(h, rd, C, uD, 34, seed, 3.5, 2.6, 9.0, 1.3, 0.045, hex3(150, 220, 245), hex3(40, 140, 210), floorY, float3(0.0, 0.25, 0.3));
        add += acc * fnFlash(rd, C, 1.8, h.t) * exp(-uD * 4.0) * 0.6;
        fnPoof(h, rd, feet + sweep + float3(0, 0.5, 0), uD - 1.2, seed, 12, 0.07);
    } else if (k == 0 || k == 10 || k == 16) {
        // THE GUILLOTINE / DEMOLITION LEAP / GREATSWORD SLAM: the blow comes down from above - driven into the ground,
        // the ground cracks, stones fly
        float sq = ease(uD / 0.1);
        fx.clipY = floorY + 0.01;
        if (uD < 1.3) { b1 = true; bP1 = feet - float3(0, sq * 0.25, 0); bR1 = mul(RV, rotX(-1.4 * ease(uD / 0.35))); bLo = -99.0; bHi = 99.0; }
        fnGround(onG, S, feet - A * 0.3, lastHit, 3.2, k == 0 ? 0.7 : 1.1, k == 0 ? 2.0 : 3.0, seed, t, o, dec, decK, add, acc);
        fnDebris(h, rd, feet - A * 0.3, uD, seed, k == 0 ? 1.0 : 1.6, floorY);
        fnPoof(h, rd, feet + float3(0, 0.3, 0) - A * 0.9, uD - 1.3, seed, 14, 0.08);
    } else if (k == 1 || k == 9 || k == 11 || k == 13) {
        // HEARTPIERCER / STEEL WHIRLWIND / RELENTLESS COMBO / GRASPING SPIRE: hurled off the last hit as a ragdoll
        uint rag = RD_MELEE;   // thrown back off the blow, away from the phantom
        // the bakes launch along their own axes - RD_MELEE back (-z), RD_SIDE along +x: both turned to fly away from the phantom (-A)
        float3x3 RT = rag == RD_SIDE ? rotY(atan2(-A.x, -A.z) - 1.5708 + 0.3 * sgn) : rotY(atan2(A.x, A.z));
        bool mir = sgn < 0.0;
        if (uD < 2.0) dsRagdoll(h, rd, feet, RT, rag, uD * 20.0, mir, rag == RD_MELEE ? (mir ? 3 : 2) : -1, fx);
        float3 Pv; float3x3 Rv0;
        dtRag(rag, min(uD, 2.0) * 20.0, 1, mir, Pv, Rv0);
        fnPoof(h, rd, feet + mul(RT, Pv) + float3(0, 0.15, 0), uD - 2.0, seed, 14, 0.07);
        dsBurst(h, rd, C, uD, 14, seed + 5.0, 2.0, 1.4, 9.0, 1.0, 0.03, hex3(110, 2, 2), hex3(181, 18, 18), floorY, 0.0);
    } else {
        // the rest: a vanilla death - over, a beat on the ground, poof
        float tip = 1.5708 * saturate(uD / 0.5) * saturate(uD / 0.5);
        if (uD < 1.0) { b1 = true; bP1 = feet - A * 0.1 * saturate(uD * 4.0); bR1 = mul(RV, rotX(-tip)); bLo = -99.0; bHi = 99.0; }
        fnPoof(h, rd, feet + float3(0, 0.3, 0) - A * 0.9, uD - 1.0, seed, 14, 0.07);
    }
    // one body (two for the cut): every branch above only says where - the body itself is built once
    if (b1) dsBody(h, rd, bP1, bR1, 1.0, bLo, bHi, fx);
    if (b2) dsBody(h, rd, bP2, bR2, 1.0, 16.0, 99.0, fx);
}
