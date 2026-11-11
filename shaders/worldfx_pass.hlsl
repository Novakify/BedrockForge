// Renderer Foundation - World FX screen pass (by Novak). Runs right after the world's opaque blocks + mobs (at Bedrock's
// cloud draw, before glass / water / the hand / the HUD), with a copy of the frame's colour and depth:
//   * sun shadows: every pixel walks toward the sun through the depth buffer - towers, trees, players cast them
//   * soft occlusion: corners, ledges and the ground under blocks hold a little shade
//   * rim: block edges with open sky behind them, on the sun's side, catch a thin warm line
// Same light as the Cloud Sea lab (Bedrock Forge tools/WorldFx/lab). Compiled by tools/gen_worldfx.py.

cbuffer C : register(b0) {
    float4 c0; // xyz = camera forward, w = tan(fovx/2)
    float4 c1; // xyz = camera right,   w = tan(fovy/2)
    float4 c2; // xyz = camera up,      w = time
    float4 c3; // x = near plane (0.025), y = living world strength (sea light, the air: F6), z = 0, w = flags (1 = scene is BGRA)
    float4 c4; // xy = target size, z = scene row pitch (bytes), w = depth row pitch (bytes)
    float4 c5; // xyz = sun direction (world), w = shadow strength
    float4 c6; // x = occlusion strength, y = rim strength, z = debug view (0 off, 1 shadows, 2 occlusion), w = match phase
    float4 c7; // xyz = camera position (world), w = cloud deck base (world y)
    float4 c8; // xy = map centre (world xz), z = sky time (same clock as the sky pass), w = 1: cloud sea in front of terrain
    float4 c9; // x = god ray strength, y = cloud shadow strength, z = quality 0..1 (RF lowers it to hold the frame rate), w = ink look strength
};
// the match (Bedrock Forge MatchFx, filled per frame by SkyPass): team lights + bed beacons, block shards, placement pop
cbuffer M : register(b1) {
    float4 m0;       // x = teams, y = block events, z = team light strength (F6), w = shard strength (F6)
    float4 tPos[16]; // bed centre (world), w = strength 0..1 (fades in, dies when the bed breaks)
    float4 tCol[16]; // team colour (linear), w = break flare
    float4 eA[8];    // block corner (world, min), w = age (s)
    float4 eB[8];    // block colour (sRGB 0..1), w = kind (1 broken, 2 placed)
    float4 eC[8];    // x = seed, y = blocks below the block the pieces land on (-1 = nothing: they fall away)
    float4 m1;       // x = trail blocks, y = shockwave strength (F6), z = 1: a kill is playing, w = 1: cosmetic beds
    float4 tAge[16]; // x = seconds since that bed broke (-1 = standing), y = enemies at the bed 0..1, z = team out 0..1 (fades in), w = 1: your team
    float4 trMin, trMax; // bounds of the trail blocks (world)
    float4 gA[48];   // trail block corner (world, min), w = age (s)
    float4 gB[48];   // its colour (sRGB)
    float4 bVis[16]; // the server's bed model per team: xyz feet (world), w = yaw (radians), w = -100: none
    float4 fallen[24]; // one star per final kill this match: xyz = sky direction, w = age (s)
    float4 m2;         // x = fallen stars, y = projectile trails
    float4 fCol[24];   // their team colours
    float4 pInfo[8];   // projectile trails: rgb = shooter's team colour, w = kind + 8 * points
    float4 pPts[96];   // 8 x 12 points, newest first: xyz world, w = seconds ago
};
#define CS_NOISE_REG t2
#define CS_PUFF_REG t3
#include "cloud_sea_common.hlsli"
ByteAddressBuffer gScene : register(t0); // the colour target, rows of c4.z bytes
ByteAddressBuffer gDepth : register(t1); // D24 in the low 24 bits, rows of c4.w bytes

int2   clampPx(int2 p) { return clamp(p, int2(0, 0), int2(c4.xy) - 1); }
float  rawDepth(int2 p) { p = clampPx(p); return float(gDepth.Load(uint(p.y) * uint(c4.w) + uint(p.x) * 4u) & 0xFFFFFFu) / 16777215.0; }
bool   isSky(float d) { return d >= 0.99999; }
// distance along the camera's forward axis. Bedrock's world depth is standard 0..1 with near = c3.x (0.025): checked
// against a probe frame (ground rows fit n / (1 - d) exactly); the camera snapshot's projection is not the world's.
float  viewZ(float d) { return c3.x / max(1.0 - d, 1e-7); }
float4 sceneAt(int2 p) {
    p = clampPx(p);
    uint  v = gScene.Load(uint(p.y) * uint(c4.z) + uint(p.x) * 4u);
    float4 c = float4(v & 255u, (v >> 8) & 255u, (v >> 16) & 255u, v >> 24) / 255.0;
    return c3.w > 0.5 ? c.bgra : c;
}
// camera-relative position of pixel p (centre) at view distance z
float3 posAt(float2 p, float z) {
    float2 ndc = float2(p.x / c4.x * 2.0 - 1.0, 1.0 - p.y / c4.y * 2.0);
    return (c0.xyz + c1.xyz * (ndc.x * c0.w) + c2.xyz * (ndc.y * c1.w)) * z;
}
float3 posPx(int2 p) { return posAt(float2(p) + 0.5, viewZ(rawDepth(p))); }
// camera-relative position -> pixel (x, y) + view distance (z); z <= 0 behind the camera
float3 toPx(float3 q) {
    float z = dot(q, c0.xyz);
    float2 ndc = float2(dot(q, c1.xyz) / (z * c0.w), dot(q, c2.xyz) / (z * c1.w));
    return float3((ndc.x * 0.5 + 0.5) * c4.x, (0.5 - ndc.y * 0.5) * c4.y, z);
}

struct VSOut { float4 pos : SV_Position; };
VSOut VSWorldFx(uint id : SV_VertexID) {
    VSOut o;
    o.pos = float4(id == 2 ? 3.0 : -1.0, id == 1 ? 3.0 : -1.0, 0.0, 1.0);
    return o;
}

// ---- god rays: march from the pixel toward the sun on screen; every step that sees open sky lets light through.
//      Islands, players and trees cut real shadow wedges into the beams. Returns linear light to add.
float3 sunShafts(int2 px, float j) {
    float3 sp = toPx(SUN * 4000.0);
    if (sp.z <= 0.0 || c8.w < 0.5) return float3(0, 0, 0);
    float2 d  = sp.xy - float2(px);
    float  dl = length(d);
    float  reach = c4.y * 0.85;                                // beams fade out ~a screen height from the sun
    float  near = saturate(1.0 - dl / reach);
    if (near <= 0.0 || c9.x <= 0.0 || c9.z < 0.12) return float3(0, 0, 0);
    int    N = int(lerp(7.0, 16.0, c9.z));                    // jittered per pixel: few taps read as many
    float2 stepv = d / float(N) * min(1.0, reach / max(dl, 1.0));
    float  acc = 0.0, wsum = 0.0, w = 1.0;
    float  decay = pow(0.93, 22.0 / float(N));
    [loop] for (int k = 0; k < N; k++) {
        float2 q = float2(px) + stepv * (float(k) + j);
        if (q.x < 0 || q.y < 0 || q.x >= c4.x || q.y >= c4.y) { acc += w; wsum += w; w *= decay; continue; }  // off screen toward the sun: assume sky
        acc += isSky(rawDepth(int2(q))) ? w : 0.0;
        wsum += w;
        w *= decay;
    }
    float open = acc / max(wsum, 1e-4);
    float beam = open * near * near * (0.6 + 0.4 * near);
    return lerp(float3(1.00, 0.58, 0.26), float3(0.45, 0.56, 0.85), NIGHT) * beam * c9.x;
}

// cloud shadows: the high cloud deck (y 900) seen from this point toward the low sun
float cloudShadow(float3 W) {
    float  t = (900.0 - W.y) / max(SUN.y, 0.03);
    float2 q = W.xz + SUN.xz * t;
    float2 p = q / 520.0 + TIME * float2(0.0030, 0.0011);
    return smoothstep(0.74, 0.86, hcDens(p, c9.z > 0.6 ? 3 : 1));
}

// ================================================================ the match
// how dark the scene is: team light reads more as the sky goes
float darkK() { return saturate(DUSK * 0.55 + NIGHT * 0.45); }
// team light falling on a terrain point W (world) with normal n: islands glow in their team colour
float3 teamLight(float3 W, float3 n) {
    float3 acc = 0.0;
    int nt = int(m0.x);
    [loop] for (int i = 0; i < nt; i++) {
        float  st = tPos[i].w + tCol[i].w * 0.6;
        if (st <= 0.002) continue;
        float3 v = tPos[i].xyz + float3(0, 1.2, 0) - W;
        float  d = length(v);
        if (d > 24.0) continue;
        float  f = pow(saturate(1.0 - d / 24.0), 2.0) / (1.0 + 0.030 * d * d);
        float  wrap = saturate(dot(n, v / max(d, 1e-3)) * 0.6 + 0.4);
        acc += tCol[i].rgb * f * wrap * st;
    }
    return acc * m0.z * (0.30 + 1.10 * darkK());
}
// a team that is out: its island loses its colour and light, as if the island itself died (0..1 at terrain point W)
float islandOut(float3 W) {
    float k = 0.0;
    int nt = int(m0.x);
    [loop] for (int i = 0; i < nt; i++) {
        float e = tAge[i].z;
        if (e <= 0.0) continue;
        float d = length((W - tPos[i].xyz) * float3(1.0, 0.6, 1.0));
        k = max(k, e * smoothstep(30.0, 16.0, d));
    }
    return k;
}
// your bed is being rushed: the edge of the screen beats in your team's colour (0 when it is safe)
float3 bedAlarm(float3 rd) {
    int nt = int(m0.x);
    float3 acc = 0.0;
    [loop] for (int i = 0; i < nt; i++) {
        if (tAge[i].w < 0.5 || tAge[i].y < 0.02 || tAge[i].x >= 0.0) continue;
        float beat = pow(0.5 + 0.5 * sin(TIME * 7.0), 4.0);
        float edge = smoothstep(0.80, 0.45, dot(rd, c0.xyz));      // the rim of the view, not the middle
        acc += lerp(tCol[i].rgb, float3(1.0, 0.25, 0.15), 0.35) * edge * beat * tAge[i].y * 0.55;
    }
    return acc;
}
// bed beacons: a soft column of team light rising from every standing bed (read the map at a glance); a bed that
// breaks flares, then its beacon falls back into the island and goes out. Ray from the camera (rd), up to tMax.
float3 beacons(float3 rd, float tMax) {
    float3 acc = 0.0;
    int nt = int(m0.x);
    float2 rxz = rd.xz;
    float  r2 = max(dot(rxz, rxz), 1e-5);
    [loop] for (int i = 0; i < nt; i++) {
        float st = tPos[i].w, fl = tCol[i].w;
        if (st + fl <= 0.002) continue;
        float3 b = tPos[i].xyz - CAM;
        float  t0 = dot(b.xz, rxz) / r2;
        if (t0 > tMax + 0.75) continue;                    // the column passes behind what this pixel shows (a roof)
        float  t = clamp(t0, 0.0, tMax);
        float2 q = rxz * t - b.xz;
        float  r = length(q);
        float  h = rd.y * t - b.y;                       // height above the bed where the ray passes the column
        float  top = lerp(3.0, 120.0, st) + fl * 40.0;
        float  prof = smoothstep(-0.4, 0.6, h) * exp(-max(h, 0.0) / (0.30 * top)) * smoothstep(top, top * 0.5, h);
        float  w = 0.22 + 0.6 * fl;                       // a tight bright core...
        float  core = exp(-r * r / (w * w)), haze = exp(-r * r / (16.0 * w * w)) * 0.16; // ...in a soft glow
        float  flow = 0.78 + 0.22 * sin(h * 0.45 - TIME * 3.2 + float(i) * 2.1);           // light rising up the column
        float  path = 1.0 / max(sqrt(r2), 0.25);          // looking along the column you see more of it
        float  orb = exp(-pow(length(float3(q.x, h - 0.4, q.y)) / 0.8, 2.0));
        float3 hot = lerp(tCol[i].rgb, float3(1, 1, 1), 0.45);
        // under attack: the beacon beats like an alarm (fast, hard) - you can see a rush on any island from yours
        float thr = tAge[i].y;
        float beat = pow(0.5 + 0.5 * sin(TIME * 9.0 + float(i)), 6.0);
        core *= 1.0 + thr * (2.2 * beat - 0.4);
        hot = lerp(hot, float3(1.0, 0.95, 0.9), thr * beat * 0.5);
        acc += (hot * core * flow * 0.30 + tCol[i].rgb * haze * 0.20) * prof * path * (st + 2.0 * fl) + tCol[i].rgb * orb * 0.35 * (st + fl);
    }
    return acc * m0.z * (0.60 + 0.80 * darkK());
}
// ---- shards: a broken block bursts into 8 tumbling pieces of itself (its colour, a pixel grain like its texture)
float3x3 rotAxis(float3 a, float ang) {
    float s = sin(ang), c = cos(ang), t = 1.0 - c;
    return float3x3(t * a.x * a.x + c, t * a.x * a.y - s * a.z, t * a.x * a.z + s * a.y,
                    t * a.x * a.y + s * a.z, t * a.y * a.y + c, t * a.y * a.z - s * a.x,
                    t * a.x * a.z - s * a.y, t * a.y * a.z + s * a.x, t * a.z * a.z + c);
}
float h1(float x) { return frac(sin(x * 91.3458) * 47453.5453); }
// ray (camera-relative origin 0, dir rd) against a box (centre p, half size e, rotation R): t of entry, local normal
bool hitBox(float3 rd, float3 p, float3 e, float3x3 R, out float tN, out float3 nl, out float3 lp) {
    float3 o = mul(-p, R), d = mul(rd, R);              // into the box frame (R columns = box axes)
    float3 inv = 1.0 / select(abs(d) > 1e-6, d, 1e-6);
    float3 t0 = (-e - o) * inv, t1 = (e - o) * inv;
    float3 tmin = min(t0, t1), tmax = max(t0, t1);
    tN = max(max(tmin.x, tmin.y), tmin.z);
    float tF = min(min(tmax.x, tmax.y), tmax.z);
    nl = tmin.x >= tN ? float3(-sign(d.x), 0, 0) : (tmin.y >= tN ? float3(0, -sign(d.y), 0) : float3(0, 0, -sign(d.z)));
    lp = (o + d * tN) / e;
    return tN < tF && tN > 0.05;
}
// returns rgb (display) + a (1 = covered), sceneT = how far the scene is along rd
bool  nearRay2(float3 rd, float3 C, float r) { float tc = dot(C, rd); return tc > -r && dot(C, C) - tc * tc < r * r; }
float segGlow(float3 rd, float3 a, float3 b, float w, float tMax) {   // light along a segment (camera at 0), depth-tested
    float3 ba = b - a;
    float  bb = dot(ba, ba), bd = dot(ba, rd), bo = dot(ba, -a), dO = dot(rd, -a);
    float  den = bb - bd * bd;
    float  sP = den > 1e-6 ? saturate((bo - bd * dO) / den) : 0.0;
    float3 q = a + ba * sP;
    float  tr = max(dot(q, rd), 0.0);
    float3 dd = rd * tr - q;
    return tr < tMax + 0.05 ? exp(-dot(dd, dd) / (w * w)) : 0.0;
}
float4 shards(float3 col0, float3 rd, float sceneT) {
    float  best = sceneT;
    float3 col = 0.0;
    bool   hit = false;
    int ne = int(m0.y);
    [loop] for (int i = 0; i < ne; i++) {
        float  age = eA[i].w, kind = eB[i].w;
        float3 corner = eA[i].xyz - CAM;
        float3 alb = pow(max(eB[i].rgb, 0.0), 2.2);
        float  seed = eC[i].x;
        if (kind > 1.5 && kind < 2.5) {
            // placed: it MATERIALIZES. For 0.42 s the block is not there - its cell is a hologram: you see through it
            // (dark glass, scanlines, a wireframe), while the block builds itself inside out of 512 small voxels of
            // light that arrive bottom-up, each flashing as it lands, fed by thin sparks streaking in. Then one flash
            // and the real block stands there, its edges glinting for a moment.
            const float DUR = 0.42;
            if (age > DUR + 0.30) continue;
            float3 bc = corner + 0.5;
            if (!nearRay2(rd, bc, 2.0)) continue;
            float3 hue = alb / max(max(alb.r, max(alb.g, alb.b)), 0.05);
            hue = lerp(hue, float3(0.75, 0.95, 1.0), 0.35);
            float3 I0 = float3(1, 0, 0), I1 = float3(0, 1, 0), I2 = float3(0, 0, 1);
            float  tN; float3 nl, lp;
            bool   inCell = hitBox(rd, bc, float3(0.503, 0.503, 0.503), float3x3(1, 0, 0, 0, 1, 0, 0, 0, 1), tN, nl, lp) && tN <= min(sceneT, best) + 0.03;
            if (inCell) {
                float3 ad = (1.0 - abs(lp)) * 0.5;                              // blocks to each pair of faces
                float  m1 = min(ad.x, min(ad.y, ad.z)), m3 = max(ad.x, max(ad.y, ad.z));
                float  ed = (ad.x + ad.y + ad.z) - m1 - m3;                    // to this face's nearest edge
                float  wire = step(ed, 1.0 / 32.0);                            // a half-texel line: crisp, not a glow
                if (age >= DUR) {                                              // done: the real block, a glint on its edges
                    if (sceneT <= tN + 0.03) {
                        float3 lin = pow(max(col0, 0.0), 2.2) + hue * wire * exp(-(age - DUR) * 13.0) * 1.1;
                        best = sceneT; col = lin; hit = true;
                    }
                } else {
                    // what is behind the cell: the world if the block is not in the frame yet, else the sky (it is hidden)
                    float3 inv = 1.0 / select(abs(rd) > 1e-6, rd, 1e-6);
                    float3 t1 = (corner + 1.0) * inv, t0 = corner * inv;
                    float  tF = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), max(t0.z, t1.z));
                    float3 bg = sceneT > tF + 0.02 ? pow(max(col0, 0.0), 2.2) : pow(max(toDisplay(csBackground(rd, true)), 0.0), 2.2);
                    // march the 8x8x8 voxels of the block: the first one that has arrived is what you see
                    float3 pe = saturate(rd * tN - corner) * 8.0;
                    float3 st = sign(rd), td = abs(inv);
                    float3 vx = clamp(floor(pe + rd * 1e-3), 0.0, 7.0);
                    float3 tm = (vx + max(st, 0.0) - pe) * inv;
                    float3 vn = nl; float got = 0.0, born = 0.0;
                    [loop] for (int q = 0; q < 22; q++) {
                        float ta = vx.y * (0.24 / 8.0) + hash13(vx + seed * 0.01) * 0.12;      // bottom-up, ragged
                        if (age >= ta) { got = 1.0; born = age - ta; break; }
                        if (tm.x < tm.y && tm.x < tm.z) { vx.x += st.x; tm.x += td.x; vn = float3(-st.x, 0, 0); }
                        else if (tm.y < tm.z)           { vx.y += st.y; tm.y += td.y; vn = float3(0, -st.y, 0); }
                        else                            { vx.z += st.z; tm.z += td.z; vn = float3(0, 0, -st.z); }
                        if (any(vx < 0.0) || any(vx > 7.0)) break;
                    }
                    float  scan = step(0.5, frac((rd.y * tN + CAM.y) * 16.0 - TIME * 3.0));    // scanlines, a texel high
                    float3 c = bg * 0.42 + hue * (0.020 + 0.030 * scan);                       // the empty hologram: dark glass
                    if (got > 0.5) {
                        float shade = 0.55 + 0.25 * vn.y + 0.12 * abs(vn.x);
                        float3 vox = lerp(alb, hue, 0.22) * shade * (0.80 + 0.30 * scan) + hue * exp(-born * 20.0) * 0.7;
                        c = lerp(c, vox, 0.86);
                    }
                    c += hue * wire * (0.55 + 0.25 * sin(TIME * 30.0));
                    c = lerp(c, lerp(alb, hue, 0.5) * 1.5, 0.75 * smoothstep(DUR - 0.05, DUR, age));              // the flash it becomes real in
                    best = min(sceneT, tN); col = c; hit = true;
                }
            }
            if (age < DUR) {   // the sparks that feed it: thin streaks arriving from all around
                float3 addS = 0.0;
                [loop] for (int k = 0; k < 7; k++) {
                    float  sk = seed * 0.001 + float(k) * 3.71;
                    float3 tgt = corner + float3(h1(sk), h1(sk + 1.0), h1(sk + 2.0));
                    float  ta = (tgt.y - corner.y) * 0.24 + 0.04 + 0.08 * h1(sk + 3.0);
                    float  u = (age - (ta - 0.11)) / 0.11;
                    if (u < 0.0 || u > 1.0) continue;
                    float3 from = normalize(float3(h1(sk + 4.0), h1(sk + 5.0) * 0.8 + 0.1, h1(sk + 6.0)) - float3(0.5, 0.0, 0.5)) * (0.9 + 0.5 * h1(sk + 7.0));
                    float3 a0 = tgt + from * (1.0 - u * u), a1 = tgt + from * (1.0 - min(u * u + 0.30, 1.0));
                    addS += hue * segGlow(rd, a0, a1, 0.010, min(sceneT, best)) * 0.9;
                }
                if (dot(addS, addS) > 1e-6) {
                    if (!hit) { col = pow(max(col0, 0.0), 2.2); best = sceneT; hit = true; }
                    col += addS;
                }
            }
            continue;
        }
        if (kind > 4.5) continue;   // respawn / victory: screen and sky effects, not pieces
        if (kind > 2.5 && kind < 4.5) continue;   // kills are drawn as light (killLight), not pieces
        if (kind > 2.5) { // a kill (old voxel body, unused): the player's body, in voxels of their team colour, holds for a beat, then bursts
            if (age > 3.6) continue;
            float3 base = corner + float3(0.5, 0.0, 0.5);                  // feet
            float  up2 = 2.5 * saturate((age - 1.7) / 1.9);
            float3 bc2 = base + float3(0, 0.9 + up2, 0);
            float  br2 = 1.6 + 2.0 * min(age, 0.9) + up2;
            float  tb3 = dot(bc2, rd);
            if (tb3 < -br2 || dot(bc2, bc2) - tb3 * tb3 > br2 * br2) continue;
            float  hold = 0.22;                                           // the hit-stop: the body freezes, white-hot
            float  ab = max(age - hold, 0.0);
            float3 team = alb / max(max(alb.r, max(alb.g, alb.b)), 0.05);
            [loop] for (int k = 0; k < 20; k++) {
                // 20 voxels make the figure: legs (2x3), body (2x3), arms (2x2), head (2x2)
                float  sk = seed * 0.001 + float(k) * 5.17;
                float3 cell = k < 6 ? float3((k & 1) ? 0.15 : -0.15, 0.15 + 0.3 * float(k >> 1), 0)
                            : (k < 12 ? float3(((k - 6) & 1) ? 0.15 : -0.15, 0.90 + 0.3 * float((k - 6) >> 1), 0)
                            : (k < 16 ? float3(((k - 12) & 1) ? 0.45 : -0.45, 1.05 + 0.3 * float((k - 12) >> 1), 0)
                                      : float3(((k - 16) & 1) ? 0.12 : -0.12, 1.62 + 0.24 * float((k - 16) >> 1), 0)));
                // the burst: out from the body's middle, a body-sized blast (not confetti across the island)
                float3 dir = normalize(float3(cell.x * 2.0, 0.0, 0.0) + (float3(h1(sk), h1(sk + 1) * 0.5, h1(sk + 2)) - 0.5) * 1.4 + float3(0, 0.6, 0));
                float3 v = dir * (1.5 + 1.5 * h1(sk + 3)) + float3(0, 1.4, 0);
                const float G = 9.0;
                float y0 = cell.y;
                float tl = (v.y + sqrt(max(v.y * v.y + 2.0 * G * y0, 0.0))) / G;
                float ta = min(ab, tl);
                float3 p = base + cell + float3(v.x, 0, v.z) * ta + float3(0, v.y * ta - 0.5 * G * ta * ta, 0);
                float  e1 = (k >= 16 ? 0.12 : 0.15);
                if (ab > tl) { p.y = base.y; p.xz += v.xz * 0.10 * (1.0 - exp(-(ab - tl) * 6.0)); }
                // then they leave: each piece lifts off the ground and drifts up, shrinking to a spark
                float  rise = saturate((age - 1.7 - 0.6 * h1(sk + 12)) / 1.3);
                p.y += rise * rise * (2.5 + 2.0 * h1(sk + 13));
                e1 *= 1.0 - rise;
                if (e1 < 0.004) continue;
                float3 ax = normalize(float3(h1(sk + 7), h1(sk + 8), h1(sk + 9)) - 0.5 + 1e-3);
                float  ang = min(ab, tl) * (8.0 + 6.0 * h1(sk + 10)) + rise * 6.0;
                if (ab > tl && rise <= 0.0) ang = round(ang / 1.5708) * 1.5708;
                float3x3 R = rotAxis(ax, ang);
                if (ab > tl && rise <= 0.0) p.y += dot(abs(R[1]), float3(e1, e1, e1));
                float  tN; float3 nl, lp;
                if (!hitBox(rd, p, float3(e1, e1, e1), R, tN, nl, lp) || tN >= best) continue;
                float3 nw = mul(R, nl);
                float  edge = smoothstep(0.62, 1.0, max(max(abs(lp.x) * (1 - abs(nl.x)), abs(lp.y) * (1 - abs(nl.y))), abs(lp.z) * (1 - abs(nl.z))));
                // white-hot at the moment of death, cooling into the team's colour; the edges keep burning
                float  heat = age < hold ? 1.0 : exp(-ab * 4.0);
                float3 body = team * (0.30 + 0.45 * saturate(dot(nw, SUN)) * LIGHTK + 0.12 * nw.y);
                float3 c = lerp(body, float3(1.7, 1.6, 1.5), heat * 0.9) + team * edge * (1.1 + 1.6 * heat + 1.2 * rise);
                best = tN, col = c, hit = true;
            }
            continue;
        }
        if (age > 2.4) continue;
        float  floorY = eC[i].y >= 0.0 ? corner.y - eC[i].y : -1e6;   // the top of what is under the block
        // a rough bound for the whole burst first (most pixels never see one)
        float3 bc = corner + 0.5 + float3(0, max(1.0 * age - 7.0 * age * age, floorY - corner.y - 0.5), 0);
        float  br = 1.2 + 2.8 * min(age, 0.8);
        float  tb = dot(bc, rd);
        if (tb < -br || dot(bc, bc) - tb * tb > br * br) continue;
        float  fade = 1.0 - smoothstep(1.5, 2.4, age);           // they lie there a moment, then shrink away
        // the game rebuilds the chunk a few frames after the break, so the block can still be in the depth buffer:
        // pieces inside its cell must not be cut by it
        float3 S = rd * sceneT;
        bool   ghost = age < 0.6 && all(S > corner - 0.03) && all(S < corner + 1.03);
        float  lim = (ghost && best >= sceneT) ? 1e9 : best;
        [loop] for (int k = 0; k < 12; k++) {     // small pieces: a fight must stay readable through them
            float  sk = seed * 0.001 + float(k) * 7.31;
            float3 o0 = float3(((k & 1) ? 0.70 : 0.30), ((k & 2) ? 0.70 : 0.30), ((k & 4) ? 0.70 : 0.30)) + (float3(h1(sk), h1(sk + 1), h1(sk + 2)) - 0.5) * (k < 8 ? 0.14 : 0.6);
            float3 dir = normalize(o0 - 0.5 + float3(0, 0.45, 0));
            float3 v = dir * (1.4 + 1.3 * h1(sk + 3)) + float3(0, 1.6, 0);
            float3 e = float3(0.050 + 0.035 * h1(sk + 4), 0.045 + 0.030 * h1(sk + 5), 0.050 + 0.035 * h1(sk + 6)) * fade;
            const float G = 14.0;
            // fly, land on what was under the block (bounce once, slide a little, lie still); nothing under: fall away
            float y0 = corner.y + o0.y, yF = floorY + e.y;
            float tl = (v.y + sqrt(max(v.y * v.y + 2.0 * G * (y0 - yF), 0.0))) / G;   // when it reaches the floor
            float ta = min(age, tl);
            float3 p = corner + o0 + float3(v.x, 0, v.z) * ta + float3(0, v.y * ta - 0.5 * G * ta * ta, 0);
            float  spin = ta;
            if (age > tl) {
                float  a2 = age - tl, vb = 0.28 * (G * tl - v.y);                    // one small bounce
                float  tb2 = 2.0 * vb / G;
                float  ab = min(a2, tb2);
                p.y = yF + max(vb * ab - 0.5 * G * ab * ab, 0.0);
                float  slide = 0.35 * (1.0 - exp(-a2 * 5.0));
                p.xz += v.xz * slide * 0.5;
                spin = tl + 0.4 * min(a2, tb2);
            }
            float3 ax = normalize(float3(h1(sk + 7), h1(sk + 8), h1(sk + 9)) - 0.5 + 1e-3);
            float  ang = spin * (6.0 + 7.0 * h1(sk + 10)) + h1(sk + 11) * 6.28;
            if (age > tl + 0.25) ang = round(ang / 1.5708) * 1.5708;            // lie flat once settled
            float3x3 R = rotAxis(ax, lerp(ang, round(ang / 1.5708) * 1.5708, smoothstep(tl, tl + 0.25, age)));
            if (age > tl) p.y += dot(abs(R[1]), e) - e.y;   // resting on its real lowest point, not sunk into the ground
            float  tN; float3 nl, lp;
            if (!hitBox(rd, p, e, R, tN, nl, lp) || tN >= lim) continue;
            float3 nw = mul(R, nl);
            // the face as a little piece of texture: 8x8 "pixels" of the block's colour, a darker rim
            float2 fuv = abs(nl.x) > 0.5 ? lp.yz : (abs(nl.y) > 0.5 ? lp.xz : lp.xy);
            float2 cell = floor((fuv * 0.5 + 0.5) * 6.0);
            float  grain = 0.84 + 0.30 * hash12(cell + float2(k * 13, seed * 0.01));
            float  rim = 1.0 - 0.28 * smoothstep(0.70, 1.0, max(abs(fuv.x), abs(fuv.y)));
            float  ndl = saturate(dot(nw, SUN));
            float3 amb = lerp(float3(0.40, 0.37, 0.52), float3(0.22, 0.28, 0.55), NIGHT) * (0.70 + 0.30 * nw.y);
            float3 sunC = lerp(float3(1.05, 0.74, 0.52), float3(0.70, 0.80, 1.05), NIGHT) * LIGHTK;
            float3 world = lerp(lerp(float3(1, 1, 1), float3(0.86, 0.78, 0.94), DUSK), float3(0.40, 0.47, 0.76), NIGHT); // same grade as the terrain
            best = tN, lim = tN, col = alb * grain * rim * (amb + sunC * ndl) * world, hit = true;
        }
    }
    return hit ? float4(pow(max(col, 0.0), 1.0 / 2.2), best) : float4(0, 0, 0, -1.0);
}
// ---- cosmetic beds (by Novak): our own bed model drawn where the server's bed model stands (BF hides theirs on this
//      screen only). Built from boxes like a Bedrock .geo model, pixel-art materials at 16 texels per block (so it sits
//      in the world's own resolution, nothing smooth or "rendered"), team-coloured blanket and a glowing crest.
//      Box: local min, local max (x across, y up, z along the bed; origin = the model's feet), material id.
static const float3 kBedMin[11] = {
    float3(-0.50, 0.10, -1.00), float3(-0.50, 0.00, -1.00), float3(-0.45, 0.28, -0.95), float3(-0.48, 0.36, -0.25),
    float3(-0.48, 0.50, -0.30), float3(-0.34, 0.46, -0.90), float3(-0.50, 0.10, -1.08), float3(-0.56, 0.95, -1.12),
    float3(-0.58, 0.00, -1.12), float3(-0.50, 0.10, 0.96),  float3(-0.18, 0.52, -1.11)};
static const float3 kBedMax[11] = {
    float3(0.50, 0.28, 1.00),   float3(0.50, 0.10, 1.00),   float3(0.45, 0.46, 0.95),   float3(0.48, 0.50, 0.97),
    float3(0.48, 0.54, -0.12),  float3(0.34, 0.58, -0.55),  float3(0.50, 0.95, -0.96),  float3(0.56, 1.02, -0.94),
    float3(0.58, 1.12, -0.94),  float3(0.50, 0.52, 1.08),   float3(0.18, 0.88, -1.08)};
// 0 dark wood, 1 linen, 2 team blanket, 3 crest, 4 wood with a glowing team trim on its edges
// (frame, legs block, mattress, blanket, blanket fold, pillow, headboard, cap, head posts, footboard, crest)
static const int kBedMat[11] = {4, 0, 1, 2, 1, 1, 4, 4, 4, 4, 3};
float3 bedMaterial(int m, float3 lp, float3 nl, float3 team, float thr, float age) {
    if (m == 4) m = 0;                                               // (the trim is added by the caller)
    float3 t = floor(lp * 16.0);                                     // texel (1/16 block)
    float  h = hash12(t.xz * 1.7 + t.y * 3.1);
    if (m == 0) { // dark stained wood: planks along the bed, a darker groove every 4 texels, team trim along the top edge
        float plank = frac(t.y / 4.0) < 0.26 ? 0.72 : 1.0;
        float3 c = float3(0.20, 0.12, 0.09) * (0.82 + 0.30 * h) * plank;
        return c;
    }
    if (m == 1) { // linen: off-white weave
        float weave = ((int(t.x) + int(t.z)) & 1) == 0 ? 0.94 : 1.0;
        return float3(0.86, 0.84, 0.80) * weave * (0.92 + 0.10 * h);
    }
    if (m == 2) { // blanket: the team colour, quilted squares with a stitched seam
        float2 q = frac(float2(t.x, t.z) / 5.0);
        float seam = (q.x < 0.2 || q.y < 0.2) ? 0.62 : 1.0;
        return lerp(team * 0.85, team, h) * seam;
    }
    return team;   // crest: its own light (added by the caller)
}
float4 cosmeticBeds(float3 rd, float sceneT) {
    int nt = int(m0.x);
    float best = sceneT; float3 col = 0.0; bool hit = false;
    [loop] for (int i = 0; i < nt; i++) {
        if (bVis[i].w < -50.0) continue;
        float  yaw0 = bVis[i].w;
        float3 o = bVis[i].xyz + float3(-sin(yaw0), 0.0, cos(yaw0)) * 0.5 - CAM;   // the two blocks' centre
        float  tb = dot(o, rd);
        if (tb < -2.5 || dot(o, o) - tb * tb > 2.6 * 2.6) continue;      // nowhere near this pixel
        float  yaw = bVis[i].w;
        float  cy = cos(yaw), sy = sin(yaw);
        float3x3 R = float3x3(cy, 0, -sy, 0, 1, 0, sy, 0, cy);          // columns: local axes in world
        float3 team = pow(max(tCol[i].rgb, 0.0), 1.0);
        float  thr = tAge[i].y;
        // when its defence is being broken the bed trembles (a little, fast)
        float3 shake = float3(sin(TIME * 61.0), 0.0, cos(TIME * 53.0)) * 0.012 * thr;
        [loop] for (int b = 0; b < 11; b++) {
            float3 e = (kBedMax[b] - kBedMin[b]) * 0.5;
            float3 c = (kBedMax[b] + kBedMin[b]) * 0.5;
            float3 p = o + mul(R, c) + shake;
            float  tN; float3 nl, lp;
            if (!hitBox(rd, p, e, R, tN, nl, lp) || tN >= best) continue;
            float3 local = c + lp * e - kBedMin[b];                        // texel space from the box corner
            float3 nw = mul(R, nl);
            float3 alb = bedMaterial(kBedMat[b], local, nl, team, thr, 0.0);
            float  ndl = saturate(dot(nw, SUN));
            float3 amb = lerp(float3(0.40, 0.37, 0.52), float3(0.22, 0.28, 0.55), NIGHT) * (0.75 + 0.25 * nw.y);
            float3 sunC = lerp(float3(1.05, 0.74, 0.52), float3(0.70, 0.80, 1.05), NIGHT) * LIGHTK;
            float3 world = lerp(lerp(float3(1, 1, 1), float3(0.86, 0.78, 0.94), DUSK), float3(0.40, 0.47, 0.76), NIGHT);
            float3 lit = alb * (amb + sunC * ndl) * world;
            if (kBedMat[b] == 3) {   // the crest: a round emblem that breathes in the team colour (fast when attacked)
                float2 uv = (lp.xy);
                float  ring = smoothstep(0.95, 0.80, length(uv)) * (1.0 - smoothstep(0.55, 0.40, length(uv)) * 0.6);
                float  beat = 0.65 + 0.35 * sin(TIME * (2.0 + 9.0 * thr));
                lit = alb * 0.25 + team * ring * (1.2 + 0.8 * beat) * (0.6 + 0.8 * darkK());
            }
            if (kBedMat[b] == 2) lit += team * 0.06 * (0.6 + darkK());         // the blanket holds a little of its glow
            if (kBedMat[b] == 4) {   // a fine line of team light along the frame's edges, breathing slowly
                float3 ad = 1.0 - abs(lp);                                     // 0 at the box's faces
                ad = ad * e;                                                   // in blocks
                float  m1v = min(ad.x, min(ad.y, ad.z)), m3v = max(ad.x, max(ad.y, ad.z));
                float  ed  = (ad.x + ad.y + ad.z) - m1v - m3v;                 // distance to this face's edge
                float  br  = 0.75 + 0.25 * sin(TIME * (1.6 + 7.0 * thr) + float(i));
                lit += team * exp(-ed * 90.0) * 1.3 * br * (0.6 + 0.8 * darkK());
            }
            best = tN, col = lit, hit = true;
        }
        // the ornament: a small crystal of team light floating over the headboard, turning and bobbing
        {
            float3 cpos = o + mul(R, float3(0.0, 1.38 + 0.06 * sin(TIME * 1.7 + float(i)), -1.02));
            float  spin = TIME * (0.9 + 3.0 * thr);
            float3x3 Rc = mul(R, rotAxis(normalize(float3(0.0, 1.0, 0.0)), spin));
            Rc = mul(Rc, rotAxis(normalize(float3(1.0, 0.0, 1.0)), 0.6155));       // stood on a corner
            float  tN; float3 nl, lp;
            if (hitBox(rd, cpos, float3(0.09, 0.09, 0.09), Rc, tN, nl, lp) && tN < best) {
                float3 nw = mul(Rc, nl);
                float  fr = pow(1.0 - saturate(dot(-rd, nw)), 2.0);
                col = team * (0.9 + 0.6 * fr) + float3(1, 1, 1) * fr * 0.5;
                best = tN; hit = true;
            }
        }
    }
    return hit ? float4(pow(max(col, 0.0), 1.0 / 2.2), best) : float4(0, 0, 0, -1.0);
}
// ---- "Your Figura" bed (by Novak): the player's own Figura stick figure sits where the bed stands, on its sitdown
//      pose, instead of a bed. Black body, a rim of team light (the avatar's own look); it breathes, looks around,
//      fidgets fast while the bed is under attack, and when the bed breaks it topples over backwards and gutters out.
//      Footprint: a fine line of team light on the ground round the two blocks the server's bed occupies (so its
//      hitbox stays readable now that the bed itself is not drawn).
#include "figura_bed.hlsli"
float figCap(float3 ro, float3 rd, float3 pa, float3 pb, float r) {   // ray vs capsule (Inigo Quilez), t or -1
    float3 ba = pb - pa, oa = ro - pa;
    float baba = dot(ba, ba), bard = dot(ba, rd), baoa = dot(ba, oa), rdoa = dot(rd, oa), oaoa = dot(oa, oa);
    float a = baba - bard * bard, b = baba * rdoa - baoa * bard, c = baba * oaoa - baoa * baoa - r * r * baba;
    float h = b * b - a * c;
    if (h >= 0.0 && a > 1e-6) {
        float t = (-b - sqrt(h)) / a, y = baoa + t * bard;
        if (y > 0.0 && y < baba) return t;
        float3 oc = (y <= 0.0) ? oa : ro - pb;
        b = dot(rd, oc); c = dot(oc, oc) - r * r; h = b * b - c;
        if (h > 0.0) return -b - sqrt(h);
    } else if (baba < 1e-6) {
        float bb = dot(rd, oa), cc = oaoa - r * r, hh = bb * bb - cc;
        if (hh > 0.0) return -bb - sqrt(hh);
    }
    return -1.0;
}
float3 figCapN(float3 p, float3 a, float3 b, float r) {
    float3 ba = b - a, pa = p - a;
    float  h = saturate(dot(pa, ba) / max(dot(ba, ba), 1e-6));
    return (pa - h * ba) / r;
}
float figSegDist(float3 ro, float3 rd, float3 a, float3 b, out float tRay) { // closest approach, ray to segment
    float3 ba = b - a, oa = ro - a;
    float  bb = dot(ba, ba), bd = dot(ba, rd), bo = dot(ba, oa), dO = dot(rd, oa);
    float  den = bb - bd * bd;
    float  s = den > 1e-6 ? saturate((bo - bd * dO) / den) : 0.0;   // param on segment (rd normalised)
    float3 q = a + ba * s;
    tRay = max(dot(q - ro, rd), 0.0);
    return length(ro + rd * tRay - q);
}
#include "death_scenes.hlsli"
float4 figuraBeds(float3 rd, float sceneT, inout float3 halo) {
    int nt = int(m0.x);
    float best = sceneT; float3 col = 0.0; bool hit = false;
    [loop] for (int i = 0; i < nt; i++) {
        if (bVis[i].w < -50.0) continue;
        float  yaw = bVis[i].w;
        float3 F = float3(-sin(yaw), 0.0, cos(yaw));                    // towards the foot of the bed
        float3 Rt = float3(-F.z, 0.0, F.x);                              // the figure's right
        float3 ctr = bVis[i].xyz + F * 0.5 - CAM;                        // the two blocks' centre (camera-relative)
        float  tb = dot(ctr, rd);
        float  brk  = tAge[i].x;                                         // >= 0: the bed broke this long ago
        float  bound = brk >= 0.0 ? 5.5 : 2.4;
        if (tb < -bound || dot(ctr, ctr) - tb * tb > bound * bound) continue;
        float3 team = tCol[i].rgb;
        float  thr  = tAge[i].y;
        float  gone = brk >= 0.0 ? saturate((brk - 2.7) / 0.9) : 0.0;   // the pieces lie where they fell a moment, then go
        if (gone >= 1.0) continue;
        float  tm = TIME + float(i) * 7.3;
        // idle life: breath (arms and head rise a little), the head drifting as it looks around; fidgety when attacked
        float  breath = sin(tm * 1.9) * 0.22;
        float  look = 1.0 + 3.5 * thr;
        float3 headOff = float3(sin(tm * 0.43 * look) * 0.55 + sin(tm * 1.31 * look) * 0.2, breath * 1.2, sin(tm * 0.29 * look) * 0.35);
        float3 jit = float3(sin(TIME * 47.0), 0.0, cos(TIME * 41.0)) * 0.18 * thr;
        float3 base = ctr - F * 0.42;                                  // seat point
        // into figure space (pixels): x right, y up, z back (the figure faces -z = the foot of the bed)
        float3 ro = float3(dot(-base, Rt), -base.y, dot(-base, -F)) * 16.0;
        float3 dl = float3(dot(rd, Rt), rd.y, dot(rd, -F));
        int    nPc = 1;                                                 // the bed breaks: he comes apart at the joints - head, body, each arm and leg bone whole
        float  flash = brk >= 0.0 ? exp(-brk * 14.0) : 0.0;
        [unroll] for (int c = 0; c < FIG_N; c++) {
            float4 A = kFigA[c], B = kFigB[c];
            float3 qa = A.xyz, qb = B.xyz;
            int part = int(B.w);
            if (part == 0) { qa += headOff; qb += headOff; }
            if (part == 2) { qa.y += breath * saturate(qa.y / 10.0); qb.y += breath * saturate(qb.y / 10.0); }
            if (part == 1) qb.y += breath * 0.5 * saturate(qb.y / 10.7) * step(9.0, qb.y);
            qa += jit; qb += jit;
            [loop] for (int pc = 0; pc < nPc; pc++) {
                float3 pa = lerp(qa, qb, float(pc) / float(nPc)), pb = lerp(qa, qb, float(pc + 1) / float(nPc));
                float  rad = A.w;
                if (brk >= 0.0) {   // each bone is thrown from the chest, tumbles, bounces once and lies where it lands
                    float  sk = float(c) * 7.13 + float(i) * 1.9;
                    float3 m = (pa + pb) * 0.5;
                    float3 dir = normalize(m - float3(0.0, 6.0, -1.0) + (float3(h1(sk), h1(sk + 1), h1(sk + 2)) - 0.5) * 3.0);
                    float3 v = float3(dir.x, 0.0, dir.z) * (11.0 + 12.0 * h1(sk + 3)) + float3(0.0, 26.0 + 22.0 * h1(sk + 4) + (part == 0 ? 14.0 : 0.0), 0.0);
                    const float G = 130.0;
                    float  yF = rad + 0.2;                                                       // lying on the ground
                    float  tl = (v.y + sqrt(max(v.y * v.y + 2.0 * G * max(m.y - yF, 0.0), 0.0))) / G;
                    float3 off; float spin;
                    if (brk < tl) { off = float3(v.x * brk, v.y * brk - 0.5 * G * brk * brk, v.z * brk); spin = brk; }
                    else {
                        float u2 = brk - tl, vb = 0.32 * (G * tl - v.y), tb2 = 2.0 * vb / G, ub = min(u2, tb2);
                        float roll = (part == 0 ? 0.9 : 0.35) * (1.0 - exp(-u2 * 3.0));           // the head rolls on a little
                        off = float3(v.x * (tl + roll * 0.4), yF - m.y + max(vb * ub - 0.5 * G * ub * ub, 0.0), v.z * (tl + roll * 0.4));
                        spin = tl + 0.5 * ub + roll * 0.3;
                    }
                    float3 ax = normalize(float3(h1(sk + 5), h1(sk + 6) * 0.3, h1(sk + 7)) - float3(0.5, 0.0, 0.5) + 1e-3);
                    float  ang = spin * (5.0 + 6.0 * h1(sk + 8));
                    if (brk > tl + 0.2) {   // settled: a bone lies flat
                        float3 hh0 = mul(rotAxis(ax, ang), (pb - pa) * 0.5);
                        hh0.y *= saturate(1.0 - (brk - tl - 0.2) * 5.0);
                        pa = m + off - hh0; pb = m + off + hh0;
                    } else {
                        float3 hh = mul(rotAxis(ax, ang), (pb - pa) * 0.5);
                        pa = m + off - hh; pb = m + off + hh;
                    }
                    pa.y = max(pa.y, rad); pb.y = max(pb.y, rad);
                    rad *= 1.0 - gone;
                    if (rad < 0.02) continue;
                }
                float  t = figCap(ro, dl, pa, pb, rad);
                float  tw = t / 16.0;
                if (t > 0.0 && tw < best) {
                    float3 nl = figCapN(ro + dl * t, pa, pb, rad);
                    float3 nw = normalize(nl.x * Rt + float3(0.0, nl.y, 0.0) - nl.z * F);
                    float  fr = pow(1.0 - saturate(dot(-rd, nw)), 2.5);
                    float  ndl = saturate(dot(nw, SUN));
                    float3 amb = lerp(float3(0.40, 0.37, 0.52), float3(0.22, 0.28, 0.55), NIGHT) * (0.75 + 0.25 * nw.y);
                    float3 sunC = lerp(float3(1.05, 0.74, 0.52), float3(0.70, 0.80, 1.05), NIGHT) * LIGHTK;
                    float  spec = pow(saturate(dot(reflect(rd, nw), SUN)), 24.0) * 0.25 * LIGHTK;
                    float  pulse = 0.85 + 0.15 * sin(tm * (1.6 + 8.0 * thr));
                    float  hot = brk >= 0.0 ? 1.0 + 1.5 * exp(-brk * 3.0) : 1.0;   // the pieces fly out burning, then cool
                    col = float3(0.022, 0.022, 0.026) * (amb + sunC * ndl) + spec
                        + team * fr * 1.9 * pulse * hot * (0.7 + 0.6 * darkK()) + float3(1, 1, 1) * flash * 2.0;
                    best = tw; hit = true;
                }
                // a soft halo of team light round the limbs
                float tr; float dd = figSegDist(ro, dl, pa, pb, tr) - rad;
                if (tr / 16.0 < best) halo += team * exp(-max(dd, 0.0) * 1.3) * 0.012 * (1.0 - gone) * (0.6 + 0.8 * darkK()) * (1.0 + 6.0 * flash);
            }
        }
        // the footprint: a fine line of team light round the server's bed (two blocks), on the ground
        if (rd.y < -0.01 && brk < 0.0) {
            float tg = (bVis[i].y + 0.01 - CAM.y) / rd.y;                // the ground under the bed
            if (tg > 0.0 && tg < best + 0.05) {
                float3 g = CAM + rd * tg - (bVis[i].xyz + F * 0.5);
                float2 q = float2(dot(g, Rt), dot(g, F));
                float2 dq = abs(q) - float2(0.5, 1.0);
                float  ed = abs(max(dq.x, dq.y));
                if (max(dq.x, dq.y) < 0.02) {
                    float corner = (abs(q.x) > 0.27 && abs(q.y) > 0.77) ? 1.0 : 0.12;   // brackets at the corners
                    float L = exp(-ed * 160.0) * corner;
                    halo += team * L * 0.8 * (0.75 + 0.25 * sin(tm * 1.6)) * (0.6 + 0.8 * darkK());
                }
            }
        }
    }
    return hit ? float4(pow(max(col, 0.0), 1.0 / 2.2), best) : float4(0, 0, 0, -1.0);
}
// ---- bed-break shockwave: when a bed falls a ring of its team's light races out over the cloud sea and across the
//      islands, and the sky flashes with it - the whole map sees which island just lost its bed
float waveRing(float d, float age) {
    float R = 6.0 + 55.0 * age, w = 2.0 + 3.0 * age;
    float fade = pow(saturate(1.0 - age / 7.0), 1.2);
    return exp(-pow((d - R) / w, 2.0)) * fade;
}
float3 shockwaves(float3 rd, float sceneT, bool sky) {
    float3 acc = 0.0;
    int nt = int(m0.x);
    float  seaTop = SEA_Y + SEA_AMP * 1.1;
    [loop] for (int i = 0; i < nt; i++) {
        float age = tAge[i].x;
        if (age < 0.0 || age > 7.0) continue;
        float3 col = tCol[i].rgb;
        float  R = 6.0 + 55.0 * age, fade = pow(saturate(1.0 - age / 7.0), 1.2);
        // on the cloud sea (a plane at its tops): a bright ring
        if (rd.y < -0.005) {
            float t = (CAM.y - seaTop) / -rd.y;
            if (t > 0.0 && t < sceneT) {
                float2 h = CAM.xz + rd.xz * t - tPos[i].xz;
                acc += col * waveRing(length(h), age) * 4.0;
            }
        }
        // and a wall of light rising from it: a cylinder of radius R round the bed, brightest at the bottom
        float2 o = CAM.xz - tPos[i].xz, d = rd.xz;
        float  a = dot(d, d), b = dot(o, d), c = dot(o, o) - R * R;
        float  disc = b * b - a * c;
        if (disc > 0.0 && a > 1e-5) {
            float sq = sqrt(disc);
            [unroll] for (int s2 = 0; s2 < 2; s2++) {
                float t = (-b + (s2 == 0 ? -sq : sq)) / a;
                if (t <= 0.0 || t >= sceneT) continue;
                float h = CAM.y + rd.y * t - (tPos[i].y - 8.0);              // rises from just under the islands
                float2 nxz = (o + d * t) / R;
                float graze = min(1.0 / max(abs(dot(nxz, normalize(d))), 0.12), 8.0);   // seen edge-on it is brighter
                acc += col * fade * smoothstep(-2.0, 2.0, h) * exp(-max(h, 0.0) / 40.0) * graze * 0.30;
            }
        }
        if (sky) acc += col * 0.30 * exp(-age * 2.0) * exp(-max(rd.y, 0.0) * 3.0); // the horizon flashes
    }
    return acc * m1.y;
}
// the same ring passing over the islands (terrain point W)
float3 shockwaveOn(float3 W) {
    float3 acc = 0.0;
    int nt = int(m0.x);
    [loop] for (int i = 0; i < nt; i++) {
        float age = tAge[i].x;
        if (age < 0.0 || age > 7.0) continue;
        acc += tCol[i].rgb * waveRing(length(W.xz - tPos[i].xz), age);
    }
    return acc * m1.y;
}
// ---- fresh blocks: what was just placed glows along its edges in its own colour, fading over ~8 s. Bridges light up
//      as they are built - a rush across the map is a line of team colour you can read from your island
float3 trails(float3 W) {
    // the fuse: a bridge is drawn in light as it is built. Each block's edges light from the side of the block placed
    // before it (the line travels the way you build), the shared edge between them is a bright stitch, and a pulse
    // runs down the chain at the builder's own pace. After ~1 s it calms to a soft edge glow, gone by ~9 s.
    if (m1.x < 0.5 || any(W < trMin.xyz - 0.05) || any(W > trMax.xyz + 0.05)) return 0.0;
    float3 acc = 0.0;
    int n = int(m1.x);
    [loop] for (int i = 0; i < n; i++) {
        float3 l = W - gA[i].xyz;
        if (any(l < -0.02) || any(l > 1.02)) continue;
        float  age = gA[i].w;
        float  code = gB[i].w;
        int    side = int(fmod(code, 8.0) + 0.5);
        float  chainN = floor(code / 8.0);
        float3 e3 = min(saturate(l), 1.0 - saturate(l));
        float  a = min(e3.x, min(e3.y, e3.z)), c = max(e3.x, max(e3.y, e3.z));
        float  edge = (e3.x + e3.y + e3.z) - a - c;                 // distance to the nearest edge of this face
        float3 col = pow(max(gB[i].rgb, 0.0), 2.2);
        col = col / max(max(col.r, max(col.g, col.b)), 0.05);
        // how far along the block the light has travelled from the stitched side (0..1 over 0.12 s)
        float  along = 0.5;
        if (side == 1) along = l.x; else if (side == 2) along = 1.0 - l.x; else if (side == 3) along = l.y;
        else if (side == 4) along = 1.0 - l.y; else if (side == 5) along = l.z; else if (side == 6) along = 1.0 - l.z;
        float  drawn = side > 0 ? step(along, age / 0.12) : smoothstep(0.0, 0.10, age);
        // the stitch: the edges of the face it shares with the block before it
        float  stitchD = side == 1 ? l.x : side == 2 ? 1.0 - l.x : side == 3 ? l.y : side == 4 ? 1.0 - l.y : side == 5 ? l.z : side == 6 ? 1.0 - l.z : 1.0;
        float  stitch = side > 0 ? exp(-stitchD * 40.0) * exp(-edge * 30.0) : 0.0;
        // the pulse down the chain: one bright band every ~6 blocks, moving forward
        float  pulse = 0.5 + 0.5 * cos((chainN - (TIME - 0.0) * 9.0) * 1.05);
        float  fresh = exp(-age * 2.2);                              // the first second is the show
        float  calm  = (age < 0.6 ? 1.0 : exp(-(age - 0.6) * 0.38));
        // rails: on a chained block the light runs along the edges PARALLEL to the build direction, so a bridge reads
        // as two continuous filaments of light, not a ladder of squares; the cross edges only flash as the stitch
        float  edgeL = exp(-edge * 32.0);
        if (side > 0) {
            int ax = (side + 1) / 2;                                   // 1 x, 2 y, 3 z: the chain's axis
            float3 ea = e3;
            if (ax == 1) ea.x = 1.0; else if (ax == 2) ea.y = 1.0; else ea.z = 1.0;   // ignore distance to cross edges
            float a2 = min(ea.x, min(ea.y, ea.z)), c2 = max(ea.x, max(ea.y, ea.z));
            float railD = (ea.x + ea.y + ea.z) - a2 - c2;
            edgeL = exp(-railD * 32.0);
        }
        acc += col * drawn * (edgeL * (0.55 + 1.6 * fresh + 0.7 * pulse * fresh) * calm + stitch * 2.2 * fresh + 0.05 * calm);
    }
    return acc;
}
// ---- final kill: the player's soul leaves as a streak of team light shooting into the sky, and a ring runs out over
//      the cloud sea under it. A normal kill gets a short, low flare.
float3 killLight(float3 rd, float sceneT) {
    // a kill, written in light: the player is "unwritten" - rings of their team's light trace their body from the
    // feet up in 0.2 s (a 2-frame white flash on the first), then lift off and drift upward, thinning out, like the
    // lines that drew them coming apart. A final kill sends the last ring up as a thin beam that pins their star into
    // the sky (the fallen). Everything is additive light: the fight behind it stays readable.
    float3 acc = 0.0;
    int ne = int(m0.y);
    [loop] for (int i = 0; i < ne; i++) {
        float kind = eB[i].w, age = eA[i].w;
        if (kind < 2.5 || kind > 4.5 || age > 4.2) continue;
        bool  fin = kind > 3.5;
        float3 team = pow(max(eB[i].rgb, 0.0), 2.2);
        team = team / max(max(team.r, max(team.g, team.b)), 0.05);
        float3 hot = lerp(team, float3(1, 1, 1), 0.55);
        float3 base = eA[i].xyz + float3(0.5, 0.0, 0.5) - CAM;   // feet
        // quick reject: a 3-block column around the body (rings rise up to ~4 blocks)
        float2 o2 = base.xz;
        float  tb = dot(float3(o2.x, 0, o2.y), rd) / max(dot(rd.xz, rd.xz), 1e-5);
        // rings
        [loop] for (int k = 0; k < 7; k++) {
            float hk   = 0.15 + 0.27 * float(k);                       // feet .. head
            float born = 0.028 * float(k);                             // written upward in ~0.2 s
            float a    = age - born;
            if (a < 0.0) continue;
            float lift = max(a - 0.35, 0.0);
            float y    = hk + lift * (0.7 + 0.12 * float(k)) + lift * lift * 0.35;   // a slow drift up, not a launch
            float r    = 0.36 * (1.0 + 0.30 * saturate(lift / 1.2)) * (1.0 + 0.25 * exp(-a * 9.0));
            float life = (1.0 - smoothstep(0.7, 1.5, lift)) * (fin && k == 6 ? 1.0 - smoothstep(0.3, 0.5, lift) : 1.0);
            if (life <= 0.0 || abs(rd.y) < 1e-4) continue;
            float t = (base.y + y) / rd.y;                             // the ring's horizontal plane
            if (t <= 0.05 || t > sceneT + 0.2) continue;
            float2 q = rd.xz * t - base.xz;
            float  d = abs(length(q) - r);
            float  w = 0.012 + 0.0009 * t;
            float  flash = a < 0.06 ? 2.5 : 1.0;                       // the 2-frame strike
            // as it rises the ring comes apart: 8 arcs, more of them gone the higher it gets
            float  th = atan2(q.y, q.x) / 6.2831853 + 0.5 + 0.07 * float(k);
            float  arc = floor(th * 8.0);
            float  keep = step(saturate(lift * 0.9), h1(arc * 1.37 + float(k) * 7.1 + eC[i].x * 0.01));
            float  arcEnd = smoothstep(0.0, 0.06, frac(th * 8.0)) * smoothstep(1.0, 0.94, frac(th * 8.0));
            float  ringK = lift > 0.0 ? keep * lerp(1.0, arcEnd, saturate(lift * 3.0)) : 1.0;
            acc += lerp(team, hot, exp(-a * 5.0)) * (exp(-pow(d / w, 2.0)) * 1.6 + exp(-d / (w * 4.0)) * 0.12) * life * flash * ringK;
        }
        // the strike: for a moment the whole body is a column of white-hot light
        if (age < 0.18) {
            float2 o = -base.xz, d2 = rd.xz;
            float  dd = max(dot(d2, d2), 1e-6);
            float  t = -dot(o, d2) / dd;
            if (t > 0.05 && t < sceneT + 0.3) {
                float r2 = length(o + d2 * t);
                float yy = rd.y * t - base.y;
                float body = smoothstep(-0.05, 0.1, yy) * smoothstep(1.95, 1.75, yy);
                acc += hot * exp(-pow(r2 / 0.28, 2.0)) * body * (1.0 - age / 0.18) * 2.2;
            }
        }
        // final: the beam that carries them up to their star
        if (fin && age > 0.5 && age < 3.0) {
            float2 o = -base.xz, d2 = rd.xz;
            float  dd = max(dot(d2, d2), 1e-6);
            float  t = -dot(o, d2) / dd;
            if (t > 0.05 && t < sceneT + 0.3) {
                float r2  = length(o + d2 * t);
                float yy  = rd.y * t - base.y;
                float ab  = age - 0.5;
                float top = 2.0 + 18.0 * ab + 90.0 * ab * ab;            // it climbs out of the body, then speeds up
                float tail = max(top - 25.0 - 40.0 * ab, 1.8);
                float on = smoothstep(tail - 1.0, tail, yy) * smoothstep(top + 0.5, top, yy);
                float w = 0.05 + 0.0015 * t;
                acc += hot * exp(-pow(r2 / w, 2.0)) * on * 2.0 * (1.0 - smoothstep(1.6, 2.5, ab));
            }
        }
    }
    return acc * m0.w;
}
// ---- a hit lands: the victim's own pixels (the world pass runs after players are drawn) flash white-hot for two
//      frames, then their team's colour rims them for ~0.12 s, then nothing. Yours hit harder; hits you take show
//      only as a thin edge flash (bedAlarm-free: see matchOver).
float3 hitOn(float3 W, float3 lin) {
    int ne = int(m0.y);
    [loop] for (int i = 0; i < ne; i++) {
        if (eB[i].w < 7.5 || eB[i].w > 8.5) continue;
        float age = eA[i].w, who = eC[i].z;
        if (who > 1.5) continue;                                     // you: you are not on your own screen
        float3 c = eA[i].xyz + float3(0.5, 0.0, 0.5);
        float2 dxz = W.xz - c.xz;
        float  hy = W.y - c.y;
        if (dot(dxz, dxz) > 0.75 * 0.75 || hy < -0.1 || hy > 2.05) continue;   // the body's column
        float3 team = pow(max(eB[i].rgb, 0.0), 2.2);
        team = team / max(max(team.r, max(team.g, team.b)), 0.05);
        float k = who > 0.5 ? 1.0 : 0.6;
        if (age < 0.045) lin = lerp(lin, float3(2.2, 2.1, 2.0), 0.75 * k);                 // the strike
        else if (age < 0.16) lin += team * (lin * 2.5 + 0.06) * k * (1.0 - (age - 0.045) / 0.115);
    }
    return lin;
}
// the kill lights the ground around it: a flash in the team's colour, then the embers' glow
float3 killGlow(float3 W) {
    float3 acc = 0.0;
    int ne = int(m0.y);
    [loop] for (int i = 0; i < ne; i++) {
        float kind = eB[i].w, age = eA[i].w;
        if (kind < 2.5 || kind > 4.5 || age > 4.2) continue;
        float3 team = pow(max(eB[i].rgb, 0.0), 2.2);
        team = team / max(max(team.r, max(team.g, team.b)), 0.05);
        float3 d = W - (eA[i].xyz + float3(0.5, 0.9, 0.5));
        float  r2 = dot(d, d);
        float  flash = (age < 0.22 ? 1.0 : exp(-(age - 0.22) * 5.0)) + 0.25 * exp(-age * 0.7);
        acc += team * flash * exp(-r2 / (kind > 3.5 ? 9.0 : 5.0));
    }
    return acc * m0.w;
}
// ---- you respawn: the world comes back from the middle of your view outward, a ring of your team's colour on the
//      front, behind it everything still grey (you were not there yet)
float3 respawnFx(float3 o, float3 rd, float age, float3 team) {
    // you are written back in: a line of your team's light sweeps from the ground up through the view in ~0.7 s,
    // the world below it already in colour, above it still grey (you were not here yet)
    float up = dot(rd, c2.xyz) / max(dot(rd, c0.xyz), 0.2);          // screen height, -1..1 (roughly)
    float H  = -1.1 + age * 3.2;                                      // the sweep line
    float back = smoothstep(H + 0.06, H - 0.06, up);
    float l  = dot(o, float3(0.299, 0.587, 0.114));
    float3 r = lerp(l * float3(0.55, 0.58, 0.68), o, back);
    r += lerp(team, float3(1, 1, 1), 0.4) * exp(-pow((up - H) / 0.012, 2.0)) * 0.55 * saturate(1.4 - age);
    return r;
}
// ---- a team wins: the fireworks show (fireworks.hlsli)
#include "fireworks.hlsli"
// ---- the final fight (night): a storm far out over the cloud sea - silent bolts on the horizon now and then, the
//      sky lighting up behind the clouds with them (makes the last minutes feel like the end of something)
float3 storm(float3 rd) {
    if (NIGHT < 0.6) return 0.0;
    float period = 7.0;
    float cyc = floor(TIME / period), ph = TIME - cyc * period;
    float when = 1.0 + 4.0 * h1(cyc * 1.7);                       // a random moment in each cycle
    float a = ph - when;
    if (a < 0.0 || a > 0.9) return 0.0;
    float flick = exp(-a * 7.0) + 0.6 * exp(-max(a - 0.18, 0.0) * 9.0) * step(0.18, a);  // a double strike
    float az = h1(cyc * 3.1) * 6.2832;
    float3 dir = normalize(float3(cos(az), 0.0, sin(az)));
    float along = dot(normalize(float3(rd.x, 0.0, rd.z)), dir);
    float3 col = float3(0.62, 0.70, 1.0);
    float glow = pow(saturate(along), 18.0) * exp(-max(rd.y, 0.0) * 6.0) * 0.9;   // the clouds lit from inside
    // the bolt: a jagged line from high in the sky down to the sea, in the storm's direction
    float side = dot(normalize(float3(rd.x, 0.0, rd.z)), normalize(float3(-dir.z, 0.0, dir.x)));
    float h = rd.y;
    float jag = (h1(floor(h * 40.0) + cyc) - 0.5) * 0.012 + (h1(floor(h * 9.0) + cyc * 2.0) - 0.5) * 0.02;
    float bolt = along > 0.9 && h > -0.02 && h < 0.32 ? exp(-pow((side - jag) / 0.0018, 2.0)) : 0.0;
    return col * (glow + bolt * 3.0) * flick * saturate((NIGHT - 0.6) / 0.3);
}
// ---- the fallen: every player knocked out of the match leaves a star of their colour in the sky. It falls into place
//      as a streak, then stays and twinkles - by the final fight the sky holds everyone who is gone
float3 fallenStars(float3 rd) {
    float3 acc = 0.0;
    int n = int(m2.x);
    [loop] for (int i = 0; i < n; i++) {
        float3 d = fallen[i].xyz; float age = fallen[i].w;
        float3 col = fCol[i].rgb / max(max(fCol[i].r, max(fCol[i].g, fCol[i].b)), 0.05);
        // arriving: it comes down from the zenith side over 1.2 s, a short streak behind it
        float  k = saturate(age / 1.2);
        float3 from = normalize(d + float3(0, 0.6, 0) * (1.0 - k));
        float  ang = acos(clamp(dot(rd, from), -1.0, 1.0));
        float  tw = 0.75 + 0.25 * sin(TIME * (2.0 + 0.7 * float(i)) + float(i) * 3.7);
        float  star = exp(-pow(ang / 0.0016, 2.0)) * 3.0 + exp(-ang / 0.006) * 0.25;
        float3 tail = normalize(d + float3(0, 0.6, 0) * (1.0 - k + 0.08));
        float3 ab3 = tail - from, ap = rd - from;
        float  tt = saturate(dot(ap, ab3) / max(dot(ab3, ab3), 1e-8));
        float  streak = k < 1.0 ? exp(-pow(length(ap - ab3 * tt) / 0.0012, 2.0)) * (1.0 - tt) * 1.5 : 0.0;
        acc += lerp(col, float3(1, 1, 1), 0.35) * (star * tw * (0.5 + 0.5 * NIGHT + 0.3 * DUSK) + streak);
    }
    return acc;
}
// ---- the arrow writes its path: a thin ribbon of light behind every projectile, hanging ~0.6 s, never over the
//      arrow itself (trajectory stays readable). Shooter's team colour; pearls violet, fireballs ember
float3 projTrails(float3 rd, float sceneT) {
    float3 acc = 0.0;
    int np = int(m2.y);
    [loop] for (int i = 0; i < np; i++) {
        float  w4 = pInfo[i].w;
        int    kind = int(fmod(w4, 8.0) + 0.5);
        int    n = int(w4 / 8.0);
        float3 col = pInfo[i].rgb;
        if (kind == 2) col = float3(0.62, 0.30, 1.0);
        if (kind == 3) col = float3(1.0, 0.45, 0.12);
        if (kind == 4) col = float3(0.92, 0.95, 1.0);
        float3 core = lerp(col, float3(1, 1, 1), 0.55);
        float  along = 0.0;                                          // distance from the head, along the trail
        [loop] for (int j = 0; j + 1 < n; j++) {
            float4 A = pPts[i * 12 + j], B = pPts[i * 12 + j + 1];
            float3 a = A.xyz - CAM, b = B.xyz - CAM;
            float  segL = length(b - a);
            if (segL < 1e-3) continue;
            // closest points between the view ray (0 + rd t) and the segment a..b
            float3 u = rd, v = (b - a) / segL, w0 = -a;
            float  bb = dot(u, v), dd = dot(u, w0), ee = dot(v, w0);
            float  den = 1.0 - bb * bb;
            float  s1 = den > 1e-5 ? (ee - bb * dd) / den : 0.0;    // along the segment (closest to the ray)
            s1 = clamp(s1, 0.0, segL);
            float3 q = a + v * s1;
            float  tq = dot(q, rd);
            if (tq <= 0.05 || tq > sceneT + 0.3) { along += segL; continue; }
            float  dist = length(q - rd * tq);
            float  pos = along + s1;                                 // blocks behind the head
            float  age = lerp(A.w, B.w, s1 / segL);
            float  w = (0.018 + 0.0012 * tq) * (1.0 + 1.5 * saturate(pos / 6.0));   // thin at the head, wider behind
            float  life = exp(-age * 4.5) * smoothstep(0.25, 0.7, pos);              // starts behind the arrow
            float  c1 = exp(-pow(dist / (w * 0.35), 2.0)), halo = exp(-dist / (w * 1.6)) * 0.25;
            acc += (core * c1 * 1.4 + col * halo) * life;
            along += segL;
        }
    }
    return acc;
}
// everything the match adds over a finished pixel (display colour o) along view ray rd, scene at distance sceneT
// ---- the match tightening (m2.w = tension 0..1: how many teams are out). Far out over the cloud sea a storm gathers -
//      heat lightning inside the horizon clouds, more often the fewer teams are left; the frame closes in a little
//      (edges darken, contrast firms). When exactly two teams remain a line of light arcs over the map from one
//      bed to the other, each team's colour running toward the middle: the duel.
// the storm's lightning: x = how bright this instant (0 = none), y = which way it is (azimuth), z = seconds into the strike
float3 stormStrike(float T) {
    float period = lerp(8.0, 2.6, T);
    float cyc = floor(TIME / period), ph = TIME - cyc * period;
    float a = ph - (0.3 + (period - 1.3) * h1(cyc * 1.7));        // a random moment in each cycle
    if (a < 0.0 || a > 0.9) return float3(0.0, 0.0, a);
    float flick = exp(-a * 7.0) + 0.6 * exp(-max(a - 0.18, 0.0) * 9.0) * step(0.18, a);  // a double strike
    return float3(flick, h1(cyc * 3.1) * 6.2832, a);
}
float3 farStorm(float3 rd, float T) {
    float period = lerp(8.0, 2.6, T);
    float cyc = floor(TIME / period);
    float3 st = stormStrike(T);
    float flick = st.x, az = st.y, a = st.z;
    if (flick <= 0.0) return 0.0;
    float3 dir = float3(cos(az), 0.0, sin(az));
    float3 fl = normalize(float3(rd.x, 0.0, rd.z));
    float along = dot(fl, dir);
    float glow = pow(saturate(along), 22.0) * exp(-max(rd.y, 0.0) * 9.0) * exp(-max(-rd.y - 0.10, 0.0) * 14.0);   // the far clouds lit from inside
    float side = dot(fl, float3(-dir.z, 0.0, dir.x));
    float jag = (h1(floor(rd.y * 40.0) + cyc) - 0.5) * 0.012 + (h1(floor(rd.y * 9.0) + cyc * 2.0) - 0.5) * 0.02;
    float bolt = along > 0.9 && rd.y > -0.01 && rd.y < 0.22 ? exp(-pow((side - jag) / 0.0015, 2.0)) * step(a, 0.25) : 0.0;
    return lerp(float3(0.62, 0.70, 1.0), float3(1.0, 0.72, 0.62), DUSK * (1.0 - NIGHT)) * (glow * 0.55 + bolt * 2.2) * flick * T;
}
float3 duelArc(float3 rd, float sceneT) {
    int nt = int(m0.x);
    if (nt < 3) return 0.0;
    int ia = -1, ib = -1, left = 0;
    [loop] for (int i = 0; i < nt; i++)
        if (tAge[i].z < 0.5) { left++; if (ia < 0) ia = i; else ib = i; }
    if (left != 2 || ib < 0) return 0.0;
    float3 A = tPos[ia].xyz + float3(0, 2, 0) - CAM, B = tPos[ib].xyz + float3(0, 2, 0) - CAM;
    float  H = 12.0 + 0.16 * length(B - A);
    float3 acc = 0.0, prev = A;
    [loop] for (int k = 1; k <= 20; k++) {
        float  u = float(k) / 20.0, um = u - 0.025;
        float3 P = lerp(A, B, u) + float3(0, 4.0 * H * u * (1.0 - u), 0);
        float  w = max(0.07, 0.0022 * length(P));
        float  run = frac(TIME * 0.30);                                              // both colours run to the middle and meet
        float  pulse = exp(-pow((um - run * 0.5) * 16.0, 2.0)) + exp(-pow((um - (1.0 - run * 0.5)) * 16.0, 2.0));
        float3 c = um < 0.5 ? tCol[ia].rgb : tCol[ib].rgb;
        acc += c * segGlow(rd, prev, P, w, sceneT) * (0.22 + 1.1 * pulse);
        prev = P;
    }
    float3 M = (A + B) * 0.5 + float3(0, H, 0);                                      // where they meet: a spark
    float3 mq = M - rd * dot(M, rd);
    float  mw = max(0.25, 0.008 * length(M));
    if (dot(M, rd) > 0.0 && dot(M, rd) < sceneT) acc += float3(1, 1, 1) * exp(-dot(mq, mq) / (mw * mw)) * exp(-pow((frac(TIME * 0.30) - 0.98) * 9.0, 2.0)) * 1.5;
    return acc * (0.6 + 0.6 * darkK());
}
float3 tensionOver(float3 o, float3 rd, float sceneT) {
    float T = m2.w;
    if (T <= 0.01) return o;
    float3 lin = pow(max(o, 0.0), 2.2);
    if (sceneT > 1e8) lin += farStorm(rd, T);
    if (m0.x > 2.5) lin += duelArc(rd, sceneT);
    o = pow(lin, 1.0 / 2.2);
    float v = saturate(1.0 - dot(rd, c0.xyz));
    o *= 1.0 - 0.20 * T * smoothstep(0.25, 0.75, v);
    return lerp(o, o * o * (3.0 - 2.0 * o), 0.20 * T);
}
// ---- the air is alive, and it changes with the sky: pollen drifting in the golden hour's light, fireflies at dusk,
//      stardust sifting down at night; the wind picks up as teams go out. They live in the world (you walk through
//      them), are a pixel or two across, and none comes within 3 blocks of your eyes - nothing to mistake for a
//      thing you must react to.
float3 airLife(float3 rd, float sceneT) {
    float K = c3.y;
    if (K <= 0.0 || c9.z < 0.45) return 0.0;
    float wGold = (1.0 - DUSK) * (1.0 - NIGHT), wDusk = DUSK * (1.0 - NIGHT), wNight = NIGHT;
    static const float S = 3.4;                                 // at most one to an S-block cell
    float3 wind = float3(0.42, 0.0, 0.26) * (1.0 + 3.5 * m2.w) + float3(0.0, lerp(0.05, -0.28, wNight), 0.0);
    float3 drift = wind * TIME;
    float  tMax = min(sceneT, 17.5);
    float  pxA = 2.0 * c0.w / c4.x;
    float3 best = 0.0;
    [loop] for (int k = 0; k < 9; k++) {
        float t = 3.2 + float(k) * 1.7;
        if (t > tMax) break;
        float3 cell = floor((CAM + rd * t - drift) / S);
        float  h = hash13(cell + 17.3);
        float  pop = wGold * 0.50 + wDusk * 0.28 + wNight * 0.55;                    // how many cells hold one
        if (h > pop) continue;
        float  h2 = hash13(cell + 41.7), h3 = hash13(cell + 5.9);
        float3 wob = float3(sin(TIME * (0.5 + h2) + h * 40.0), sin(TIME * (0.4 + h3) + h2 * 40.0), cos(TIME * (0.45 + h) + h3 * 40.0)) * lerp(0.10, 0.55, wDusk);
        float3 d = (cell + 0.22 + 0.56 * float3(h2, h3, frac(h * 7.31))) * S + drift + wob - CAM;
        float  tt = dot(d, rd);
        if (tt < 3.0 || tt > sceneT) continue;
        float3 off = d - rd * tt;
        float  sz = max(0.016 + 0.012 * h2, tt * pxA * 1.25);                        // never thinner than a pixel and a bit
        float  I = exp(-dot(off, off) / (sz * sz)) * saturate((tt - 3.0) / 1.5) * saturate((17.5 - tt) / 4.0);
        if (I < 0.003) continue;
        float  tw = 0.5 + 0.5 * sin(TIME * (1.2 + 2.5 * h3) + h2 * 60.0);
        float3 cG = float3(1.00, 0.80, 0.48) * (0.20 + 1.3 * pow(saturate(dot(rd, SUN)), 3.0)) * (0.6 + 0.4 * tw);   // pollen: it is the sun in it you see
        float3 cD = float3(1.00, 0.72, 0.26) * pow(tw, 3.0) * 1.25;                                                   // fireflies: slow pulses
        float3 cN = float3(0.72, 0.84, 1.00) * (0.25 + 0.9 * pow(tw, 6.0));                                           // stardust: glints
        float3 c = (cG * wGold + cD * wDusk + cN * wNight) * I;
        best = max(best, c);
    }
    return best * K * 0.55;
}
// a player lost to the void: where they went under, the cloud sea opens in a ring of their team's colour
float3 voidSplash(float3 rd, float sceneT, float3 at, float age, float3 team) {
    if (c8.w < 0.5 || at.y > SEA_Y + 9.0 || age > 2.6 || rd.y > -0.02) return 0.0;
    float t = (SEA_Y + 2.0 - CAM.y) / rd.y;
    if (t <= 0.0 || t > sceneT) return 0.0;
    float2 q = (CAM + rd * t).xz - (at.xz + 0.5);
    float  r = length(q), R = 1.5 + 13.0 * (1.0 - exp(-age * 1.6));
    float  ring = exp(-pow((r - R) / (0.55 + 0.5 * age), 2.0)) + 0.5 * exp(-pow((r - R * 0.55) / (0.4 + 0.4 * age), 2.0));
    float  core = exp(-r * r / 9.0) * exp(-age * 3.5) * 1.6;
    return lerp(team, float3(1, 1, 1), 0.35) * (ring * exp(-age * 1.5) * 0.85 + core) * exp(-t / 260.0);
}
float3 matchOver(float3 o, float3 rd, float sceneT) {
    [branch] if (m1.w > 0.5 && m0.x > 0.5) {
        if (m2.z > 0.5) {
            float3 halo = 0.0;
            float4 bd = figuraBeds(rd, sceneT, halo);
            if (bd.w > 0.0) { o = bd.rgb; sceneT = bd.w; }
            o = pow(pow(max(o, 0.0), 2.2) + halo, 1.0 / 2.2);
        } else {
            float4 bd = cosmeticBeds(rd, sceneT);
            if (bd.w > 0.0) { o = bd.rgb; sceneT = bd.w; }
        }
    }
    [branch] if (m0.y > 0.5 && m0.w > 0.0) {
        float4 sh = shards(o, rd, sceneT);
        if (sh.w > 0.0) { o = sh.rgb; sceneT = sh.w; }
    }
    [branch] if (m0.y > 0.5 && m1.z > 0.5) {   // a kill: its death scene
        o = deathScenes(o, rd, sceneT);
    }
    [branch] if (m0.x > 0.5 && (m0.z > 0.0 || m1.y > 0.0)) {
        float3 lin = pow(max(o, 0.0), 2.2) + beacons(rd, min(sceneT, 900.0)) * (1.0 + 0.6 * m2.w) + shockwaves(rd, sceneT, sceneT > 1e8);
        o = pow(lin, 1.0 / 2.2);
    }
    [branch] if (sceneT > 1e8 && m2.x > 0.5) o = pow(pow(max(o, 0.0), 2.2) + fallenStars(rd), 1.0 / 2.2);
    [branch] if (m2.y > 0.5) o = pow(pow(max(o, 0.0), 2.2) + projTrails(rd, sceneT), 1.0 / 2.2);
    [branch] if (m1.z > 0.5) {
        int ne = int(m0.y);
        float3 add = 0.0;
        [loop] for (int i = 0; i < ne; i++) {
            if (eB[i].w < 7.5 || eB[i].w > 8.5) continue;
            float age = eA[i].w, who = eC[i].z;
            float3 team = pow(max(eB[i].rgb, 0.0), 2.2);
            team = team / max(max(team.r, max(team.g, team.b)), 0.05);
            if (who > 1.5) {   // you were hit: a thin flash on the edge of the view
                float edge = smoothstep(0.86, 0.62, dot(rd, c0.xyz));
                add += lerp(team, float3(1, 0.3, 0.2), 0.5) * edge * exp(-age * 14.0) * 0.45;
                continue;
            }
            if (who < 0.5 || age > 0.2) continue;
            // your hit: a crescent of light across the victim, angled by your swing (view yaw), drawn in 0.08 s
            float3 c = eA[i].xyz + float3(0.5, 1.15, 0.5) - CAM;
            float  tc = dot(c, rd);
            if (tc <= 0.1 || tc > sceneT + 1.0) continue;
            float3 q = rd * tc - c;                                  // offset in the plane through the victim
            float3 right = c1.xyz, up = c2.xyz;
            float2 p2 = float2(dot(q, right), dot(q, up));
            float  ang = eC[i].y * 0.0174533 * 0.0 + (h1(eC[i].x * 0.37) - 0.5) * 1.2;   // a slant that varies per hit
            float2 r2 = float2(cos(ang) * p2.x - sin(ang) * p2.y, sin(ang) * p2.x + cos(ang) * p2.y);
            float  R = 0.75, a = atan2(r2.y, r2.x);
            float  sweep = saturate(age / 0.08);
            float  arcOn = smoothstep(-2.4, -2.2, a) * smoothstep(-2.4 + 3.6 * sweep + 0.2, -2.4 + 3.6 * sweep, a);
            float  w = 0.035 * (1.0 - saturate((a + 2.4) / 3.6) * 0.8);  // tapering
            float  d = abs(length(r2) - R);
            add += lerp(float3(1, 1, 1), team, 0.35) * exp(-pow(d / w, 2.0)) * arcOn * (1.0 - saturate((age - 0.08) / 0.12)) * 1.8;
        }
        o = pow(pow(max(o, 0.0), 2.2) + add, 1.0 / 2.2);
    }
    [branch] if (m1.z > 0.5) {
        int ne = int(m0.y);
        [loop] for (int i = 0; i < ne; i++) {
            float kind = eB[i].w, age = eA[i].w;
            float3 team = pow(max(eB[i].rgb, 0.0), 2.2);
            team = team / max(max(team.r, max(team.g, team.b)), 0.05);
            if (kind > 4.5 && kind < 5.5 && age < 1.6) o = respawnFx(o, rd, age, team);
            if (kind > 2.5 && kind < 4.5) o = pow(pow(max(o, 0.0), 2.2) + voidSplash(rd, sceneT, eA[i].xyz, age, team), 1.0 / 2.2);
            if (kind > 5.5 && kind < 6.5 && age < FW_LEN) {   // a win: the fireworks show - in the sky, on the cloud sea, and its light on the islands
                float3 l = pow(max(o, 0.0), 2.2);
                l = sceneT > 500.0 ? fwSky(l, rd, age, team, eC[i].x, eC[i].w, c9.z, 0.75 + 0.45 * darkK()) : fwWorld(l, age, team, eC[i].x, eC[i].w);
                o = pow(max(l, 0.0), 1.0 / 2.2);
            }
            if (kind > 6.5 && age < 0.7) { // your kill: a hard beat - the frame punches bright and warm at the edges, then settles
                float edge = smoothstep(0.92, 0.55, dot(rd, c0.xyz));
                float k = exp(-age * 7.0);
                o = lerp(o, saturate(o * 1.25 + 0.04), k * 0.6) + lerp(team, float3(1.0, 0.85, 0.55), 0.5) * edge * k * 0.35;
            }
        }
    }
    o = tensionOver(o, rd, sceneT);
    [branch] if (c3.y > 0.0) o = pow(pow(max(o, 0.0), 2.2) + airLife(rd, sceneT), 1.0 / 2.2);
    return o;
}
bool matchActive() { return c3.y > 0.0 || m2.w > 0.01 || m2.x > 0.5 || m2.y > 0.5 || (m0.x > 0.5 && (m0.z > 0.0 || m1.y > 0.0 || m1.w > 0.5)) || (m0.y > 0.5 && m0.w > 0.0); }

float4 PSWorldFx(VSOut i) : SV_Target {
    int2  px = int2(i.pos.xy);
    float d  = rawDepth(px);
    // sky pixels: the sky pass drew the gradient + high clouds; the cloud sea goes in here, where only pixels that are
    // still sky after the world is drawn pay for its ray march
    [branch] if (isSky(d)) {
        CAM = c7.xyz; TIME = c8.z; SEA_Y = c7.w; MAP_C = c8.xy; csSetPhase(c6.w); QUALITY = c9.z;
        float3 rd = normalize(posAt(float2(px) + 0.5, 1.0));
        if (c8.w < 0.5) { if (!matchActive()) discard; return float4(matchOver(sceneAt(px).rgb, rd, 1e9), 1.0); }
        float  js = hash12(float2(px));
        float3 rays = sunShafts(px, js) * 0.55 * LIGHTK;                  // over open sky the beams are subtler (it is already bright)
        float4 sea = (rd.y > 0.02 && rd.y > (SEA_Y + SEA_AMP * 7.6 - CAM.y) / 180.0) ? float4(0, 0, 0, 0) : csSeaPixel(rd);
        // the full sky (high clouds, stars, moon) is drawn here, on pixels that are really sky - the sky pass only lays a
        // cheap gradient under the whole screen (most of it is covered by the world afterwards)
        float3 base = toDisplay(sea.a > 0.0 ? sea.rgb : csBackground(rd, false));
        float3 lb = pow(max(base, 0.0), 2.2) + rays * (sea.a > 0.0 ? 1.0 : 0.6);
        float3 o = pow(max(lb, 0.0), 1.0 / 2.2);
        o = matchOver(o, rd, 1e9) + (js - 0.5) / 255.0 + (hash12(float2(px.yx) + 7.0) - 0.5) / 255.0;
        return float4(o, 1.0);
    }
    float4 sc  = sceneAt(px);
    float3 col = sc.rgb;
    float  fogVis = sc.a;                                     // Cloud Sea blocks write 1 - the game's fog here
    float  z   = viewZ(d);
    float3 P   = posAt(float2(px) + 0.5, z);
    CAM = c7.xyz; TIME = c8.z; SEA_Y = c7.w; MAP_C = c8.xy; csSetPhase(c6.w); QUALITY = c9.z;
    float3 rdT = P / max(length(P), 1e-4);
    // ---- the cloud sea is a real surface: ground below the cloud deck goes under it, peaks above it rise out of it
    //      (the sky pass only draws it where nothing else is; without this the loaded terrain shows as a square)
    [branch] if (c8.w > 0.5) {
        CAM = c7.xyz; TIME = c8.z; SEA_Y = c7.w; MAP_C = c8.xy;
        float  dist = length(P);
        float3 rd   = P / dist;
        float  yTop = SEA_Y + SEA_AMP * 7.6;
        float  tTop = rd.y < 0.0 ? (CAM.y - yTop) / -rd.y : 1e9;
        if (CAM.y <= yTop) tTop = 0.0;
        // both ends of the view segment above the sea's local top -> the whole segment is (skips the march for
        // nearly every map pixel; only ground that dips toward the deck pays for it)
        bool above = min(CAM.y, CAM.y + P.y) > seaTopAlong(CAM.xz, CAM.xz + P.xz);
        if (rd.y < 0.02 && tTop < dist && !above) {
            float  tHit;
            float4 sea = cloudSea(CAM, rd, dist, tHit);
            if (sea.a > 0.0 && tHit < dist) {
                float3 o = matchOver(toDisplay(sea.rgb), rd, tHit) + (hash12(float2(px)) - 0.5) / 255.0;
                return float4(o, 1.0);
            }
        }
    }
    // block look off (shadows unticked): only the far-edge fade into the sky is left to do
    [branch] if (c5.w <= 0.0 && c6.x <= 0.0 && c6.y <= 0.0 && c6.z < 0.5 && c9.w <= 0.0) {
        if ((fogVis >= 0.995 || c8.w < 0.5) && !matchActive()) discard;
        float3 o0 = col;
        if (fogVis < 0.995 && c8.w > 0.5 && length(P) > 40.0) o0 = lerp(toDisplay(csBackground(rdT, true)), col, smoothstep(0.0, 1.0, fogVis));
        return float4(matchOver(o0, rdT, length(P)) + (hash12(float2(px)) - 0.5) / 255.0, 1.0);
    }
    // normal from the depth buffer: the flatter neighbour on each axis (no halos at block edges)
    float3 pl = posPx(px - int2(1, 0)), pr = posPx(px + int2(1, 0)), pu = posPx(px - int2(0, 1)), pd = posPx(px + int2(0, 1));
    float3 dx = abs(dot(pr - P, c0.xyz)) < abs(dot(P - pl, c0.xyz)) ? pr - P : P - pl;
    float3 dy = abs(dot(pd - P, c0.xyz)) < abs(dot(P - pu, c0.xyz)) ? pd - P : P - pu;
    float3 n  = normalize(cross(dy, dx));
    if (dot(n, -P) < 0.0) n = -n;
    float3 L  = SUN;                                         // the living sky's key light (sun, or the moon at night)
    float  j  = hash12(float2(px)), j2 = hash12(float2(px.yx) + 13.1);

    // ---- sun shadow: walk toward the sun; anything in front of the ray on screen blocks it
    float vis = 1.0;
    float ndl = dot(n, L);
    // cost scales with distance: near pixels get the full walk, far ones a short one, past 140 blocks none
    float qs = lerp(0.45, 1.0, c9.z);
    int   nSh = int((z < 25.0 ? 20.0 : (z < 60.0 ? 12.0 : 7.0)) * qs);
    float tMax = z < 25.0 ? 30.0 : (z < 60.0 ? 26.0 : 18.0);   // the low sun throws long shadows: ~1 block steps
    [branch] if (ndl > 0.02 && c5.w * LIGHTK > 0.02 && z < 140.0) {
        [loop] for (int k = 0; k < nSh; k++) {
            float f = (float(k) + j) / float(nSh);
            float t = 0.04 + tMax * f * sqrt(f);
            float3 q = P + n * (0.03 + 0.002 * z) + L * t;
            float3 s = toPx(q);
            if (s.z <= 0.05 || s.x < 0.0 || s.y < 0.0 || s.x >= c4.x || s.y >= c4.y) break;
            float sd = rawDepth(int2(s.xy));
            if (isSky(sd)) continue;
            float diff = s.z - viewZ(sd);
            if (diff > 0.03 + 0.012 * t + 0.0015 * z && diff < 2.5 + 0.08 * t) { vis = 0.0; break; }
        }
        vis = lerp(1.0, vis, saturate(1.0 - (z - 90.0) / 60.0));   // far away the depth buffer is too coarse: fade out
    }
    float lit = smoothstep(0.02, 0.25, ndl);                  // faces turned away were already shaded by the block shader

    // ---- soft occlusion: how much of the open sky around this point nearby blocks hide
    float occ = 0.0;
    float3 tA = normalize(abs(n.y) > 0.5 ? cross(n, float3(1, 0, 0)) : cross(n, float3(0, 1, 0)));
    float3 tB = cross(n, tA);
    int   nAo = (c6.x <= 0.0 || c9.z < 0.2) ? 0 : (z < 30.0 ? (c9.z > 0.5 ? 6 : 4) : (z < 70.0 ? 3 : 0));
    [loop] for (int k2 = 0; k2 < nAo; k2++) {
        float a = (float(k2) + j) * 2.39996;
        float r = sqrt((float(k2) + j2) / float(nAo));
        float h = sqrt(max(1.0 - r * r, 0.0));
        float rad = 1.5 * lerp(0.2, 1.0, float(k2) / float(max(nAo - 1, 1)));
        float3 q = P + n * 0.03 + (tA * cos(a) * r + tB * sin(a) * r + n * h) * rad;
        float3 s = toPx(q);
        if (s.z <= 0.05) continue;
        float sd = rawDepth(int2(s.xy));
        if (isSky(sd)) continue;
        float dz = s.z - viewZ(sd);
        occ += dz > 0.02 ? saturate(1.6 / max(dz, 1.6)) : 0.0;
    }
    float ao = nAo > 0 ? 1.0 - occ / float(nAo) : 1.0;
    ao = lerp(1.0, ao, saturate(1.0 - (z - 45.0) / 25.0));

    // ---- ink look: the world drawn like an animated film, not lit like a shader pack. Hand-inked lines on every block
    //      silhouette and corner (from the depth buffer: two blocks side by side on one face get no line, so the line
    //      follows the shapes - islands, bridges, players), cel light (no blur: smeared texture is the generic look)
    float ink = 0.0;
    [branch] if (c9.w > 0.0 && z < 120.0) {
        // An outline that is always there: every shape's silhouette gets one line, drawn on the shape itself (on its
        // own edge pixel, against whatever is behind it - terrain or open sky), and every outer block corner a lighter
        // one. Depth alone decides it, so it cannot flicker with the light. Leaves and grass (depth jumping on most
        // sides) are left alone: lines there are noise.
        float thr = 0.22 + 0.022 * z;                                // how far behind counts as "behind" (blocks)
        // the line keeps its weight on any screen: 1 px at 1080p, 2 at 1440p, 3 at 4K (a 1 px line at 4K is a hair -
        // there some frames, gone the next)
        int   r  = clamp(int(c4.x / 1400.0 + 0.5), 1, 4);
        int   r2 = (r + 1) / 2;
        float pxK = float(r) * (2.0 * c0.w / c4.x) / 0.003;           // the same corner bends 1/z less across a smaller pixel
        float w0 = 1.0 / z;
        float sil = 0.0, silIn = 0.0, steps = 0.0, wn[4];
        static const int2 kO[4] = {int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1)};
        [unroll] for (int q4 = 0; q4 < 4; q4++) {
            float dn = rawDepth(px + kO[q4] * r);
            float zn = viewZ(dn);
            bool  sk = isSky(dn);
            float gap = sk ? 1e3 : zn - z;                            // > 0: the neighbour is behind this pixel
            sil = max(sil, smoothstep(thr, thr * 1.5, gap));
            steps += abs(gap) > thr ? 1.0 : 0.0;
            wn[q4] = sk || abs(gap) > thr ? w0 : 1.0 / zn;
            if (r2 < r) {                                             // the line's core: full weight right at the edge, softer further in
                float d2 = rawDepth(px + kO[q4] * r2);
                silIn = max(silIn, smoothstep(thr, thr * 1.5, isSky(d2) ? 1e3 : viewZ(d2) - z));
            } else silIn = sil;
        }
        sil = 0.62 * sil + 0.38 * silIn;
        // outer corners: 1/z is a straight line across a flat face, so where it bends toward the camera two faces meet
        float bend = max(-(wn[0] + wn[1] - 2.0 * w0), -(wn[2] + wn[3] - 2.0 * w0)) / w0;
        float crease = smoothstep(0.0035 * pxK, 0.0075 * pxK, bend) * saturate(1.0 - (z - 22.0) / 14.0) * (1.0 - sil);
        float busy = steps >= 3.0 ? 0.0 : 1.0;
        float far  = saturate(1.0 - (z - 60.0) / 60.0);
        ink = max(sil, crease * 0.5) * busy * far * c9.w;
    }

    // ---- apply (linear light). Shadowed sunlit faces fall to the violet sky light; occlusion darkens gently
    float3 lin = pow(col, 2.2);
    CAM = c7.xyz; TIME = c8.z; SEA_Y = c7.w; MAP_C = c8.xy;
    float  cs  = (c8.w > 0.5 && c9.y > 0.0 && c9.z > 0.25) ? cloudShadow(CAM + P) * c9.y : 0.0;
    float  sh  = max((1.0 - vis) * lit * c5.w, cs * lit * 0.55 * (1.0 - NIGHT)) * min(LIGHTK, 1.0);
    float3 shadeCol = lerp(float3(0.46, 0.44, 0.62), float3(0.48, 0.54, 0.76), NIGHT); // without the key light: violet sky / blue night (light enough to fight in)
    lin *= lerp(float3(1, 1, 1), shadeCol, sh);
    lin *= lerp(1.0, ao, c6.x);
    // cel light: the shadow terminator gets a thin painted band of warm light on the lit side
    [branch] if (c9.w > 0.0) {
        float band = saturate(ndl * 6.0) * (1.0 - saturate(ndl * 6.0 - 1.0)) * (1.0 - sh) * LIGHTK;
        lin *= 1.0 + band * 0.35 * c9.w * lerp(float3(1.0, 0.72, 0.45), float3(0.6, 0.7, 1.0), NIGHT);
        // top faces read as lit from the sky (the "painted" key/fill split of a film frame)
        lin *= lerp(1.0, n.y > 0.7 ? 1.10 : 0.94, c9.w);
    }

    // ---- rim: an edge with open sky right behind it, on the sun's side, catches a thin warm line
    float3 sp = toPx(L * 1000.0);
    if (c6.y > 0.0 && sp.z > 0.0) {
        float2 to = normalize(sp.xy - float2(px) + 1e-3);
        float open = 0.0;
        [unroll] for (int k3 = 1; k3 <= 2; k3++) if (isSky(rawDepth(int2(float2(px) + to * (1.5 * k3))))) open += 0.5;
        float near = exp(-length((sp.xy - float2(px)) / c4.y) * 2.4);
        lin += lerp(float3(1.5, 0.74, 0.30), float3(0.62, 0.74, 1.05), NIGHT) * open * near * c6.y * 0.55 * LIGHTK;
    }
    lin += sunShafts(px, j) * (1.0 - 0.5 * exp(-z / 30.0)) * LIGHTK;   // air close to you holds less light
    lin *= lerp(lerp(float3(1, 1, 1), float3(0.86, 0.78, 0.94), DUSK), float3(0.40, 0.47, 0.76), NIGHT); // the world follows the sky
    // ---- sea light: the cloud sea under the map glows with the hour, and everything is lit by it from below - the
    //      undersides of bridges and islands most, walls a little. It moves, slowly, like light off water. When the
    //      storm of the last teams breaks, its lightning comes up through the sea too.
    [branch] if (c3.y > 0.0 && c8.w > 0.5) {
        float3 W = CAM + P;
        float  under = saturate(-n.y) * 1.0 + (1.0 - abs(n.y)) * 0.20;
        float  hgt = saturate(1.0 - (W.y - SEA_Y) / 90.0);
        float  mv = 0.72 + 0.56 * vnoise(W.xz * 0.11 + float2(TIME * 0.21, -TIME * 0.13));
        float3 seaC = lerp(lerp(float3(1.00, 0.68, 0.38), float3(1.00, 0.46, 0.60), DUSK), float3(0.34, 0.52, 1.00), NIGHT);
        float3 add = seaC * under * (0.30 + 0.70 * hgt) * mv * lerp(0.85, 1.9, NIGHT);
        float3 st = m2.w > 0.01 ? stormStrike(m2.w) : float3(0, 0, 0);
        add += lerp(float3(0.62, 0.70, 1.0), float3(1.0, 0.72, 0.62), DUSK * (1.0 - NIGHT)) * st.x * m2.w * (0.30 + 1.2 * under);
        lin *= 1.0 + add * lerp(1.0, ao, 0.6) * c3.y;
    }
    [branch] if (m0.x > 0.5) { // your bed is gone: the whole world loses some warmth - you are on your last life
        int nt2 = int(m0.x);
        float lost = 0.0;
        [loop] for (int i2 = 0; i2 < nt2; i2++) if (tAge[i2].w > 0.5 && tAge[i2].x >= 0.0) lost = saturate((tAge[i2].x - 1.0) / 4.0);
        if (lost > 0.0) {
            float l2 = dot(lin, float3(0.2126, 0.7152, 0.0722));
            lin = lerp(lin, lerp(lin, l2 * float3(0.85, 0.90, 1.05), 0.35), lost);
        }
    }
    [branch] if (m0.x > 0.5) { // an eliminated team's island: grey, cold, dim
        float io = islandOut(CAM + P);
        if (io > 0.0) {
            float  l = dot(lin, float3(0.2126, 0.7152, 0.0722));
            lin = lerp(lin, l * float3(0.55, 0.60, 0.72), io * 0.85);
        }
    }
    [branch] if (m0.x > 0.5 && m0.z > 0.0) lin += (lin * 2.6 + 0.045) * teamLight(CAM + P, n);
    [branch] if (m0.x > 0.5 && m1.y > 0.0) lin += (lin * 2.0 + 0.03) * shockwaveOn(CAM + P);
    // (killGlow is gone: the death scenes light their own ground)
    [branch] if (m1.z > 0.5) lin = hitOn(CAM + P, lin);
    [branch] if (m1.x > 0.5) lin += trails(CAM + P) * (0.85 + 0.6 * darkK());
    float3 o = pow(max(lin, 0.0), 1.0 / 2.2);
    // the edge of the render distance: fade into what the sky pass shows behind this pixel (clouds / sky), not into
    // one flat colour - that flat fog against the cloud sea is what made the loaded area look like a square
    [branch] if (fogVis < 0.995 && c8.w > 0.5 && z > 40.0) {   // only far pixels can be fogged (entities may not write the alpha)
        CAM = c7.xyz; TIME = c8.z; SEA_Y = c7.w; MAP_C = c8.xy;
        float3 bg = toDisplay(csBackground(normalize(P), true));
        o = lerp(bg, o, smoothstep(0.0, 1.0, fogVis));
    }
    // the ink: deep, coloured (the darkest tone of the frame, not black), drawn over everything near
    [branch] if (ink > 0.0) {   // the line is the surface's own colour, deepened (never a flat black stroke)
        o = lerp(o, o * float3(0.42, 0.40, 0.48), saturate(ink) * 0.85);
    }
    if (c6.z > 0.5 && c6.z < 1.5) o = lerp(float3(1, 0.85, 0.6), float3(0.2, 0.2, 0.45), sh);
    if (c6.z > 1.5) o = ao.xxx;
    o = matchOver(o, rdT, length(P));
    o += (j - 0.5) / 255.0;
    return float4(o, 1.0);
}
