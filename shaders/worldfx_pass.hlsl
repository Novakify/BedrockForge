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
    float4 c3; // x = near plane (0.025), y = z = 0, w = flags (1 = scene is BGRA)
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
    float4 m1;       // x = trail blocks, y = shockwave strength (F6), z = 1: a kill is playing
    float4 tAge[16]; // x = seconds since that bed broke (-1 = standing), y = enemies at the bed 0..1, z = team out 0..1 (fades in), w = 1: your team
    float4 trMin, trMax; // bounds of the trail blocks (world)
    float4 gA[48];   // trail block corner (world, min), w = age (s)
    float4 gB[48];   // its colour (sRGB)
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
float4 shards(float3 rd, float sceneT) {
    float  best = sceneT;
    float3 col = 0.0;
    bool   hit = false;
    int ne = int(m0.y);
    [loop] for (int i = 0; i < ne; i++) {
        float  age = eA[i].w, kind = eB[i].w;
        float3 corner = eA[i].xyz - CAM;
        float3 alb = pow(max(eB[i].rgb, 0.0), 2.2);
        float  seed = eC[i].x;
        if (kind > 1.5 && kind < 2.5) { // placed: the block lands with a quick overshoot (only the part bigger than the block shows)
            if (age > 0.4) continue;
            float  sc = 1.0 + 0.13 * exp(-age * 8.0) * cos(age * 24.0);
            float  tN; float3 nl, lp;
            if (!hitBox(rd, corner + 0.5, 0.5 * sc, float3x3(1, 0, 0, 0, 1, 0, 0, 0, 1), tN, nl, lp) || tN >= best) continue;
            float  edge = smoothstep(0.80, 1.0, max(max(abs(lp.x) * (1 - abs(nl.x)), abs(lp.y) * (1 - abs(nl.y))), abs(lp.z) * (1 - abs(nl.z))));
            float3 lit = alb * (0.55 + 0.75 * LIGHTK * saturate(dot(nl, SUN))) + exp(-age * 9.0) * edge * 0.6;
            best = tN, col = lit, hit = true;
            continue;
        }
        if (kind > 4.5) continue;   // respawn / victory: screen and sky effects, not pieces
        if (kind > 2.5) { // a kill: the player's body, in voxels of their team colour, holds for a beat, then bursts
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
        if (age > 4.2) continue;
        float  floorY = eC[i].y >= 0.0 ? corner.y - eC[i].y : -1e6;   // the top of what is under the block
        // a rough bound for the whole burst first (most pixels never see one)
        float3 bc = corner + 0.5 + float3(0, max(1.0 * age - 7.0 * age * age, floorY - corner.y - 0.5), 0);
        float  br = 1.2 + 2.8 * min(age, 0.8);
        float  tb = dot(bc, rd);
        if (tb < -br || dot(bc, bc) - tb * tb > br * br) continue;
        float  fade = 1.0 - smoothstep(3.2, 4.2, age);           // they lie there a moment, then shrink away
        // the game rebuilds the chunk a few frames after the break, so the block can still be in the depth buffer:
        // pieces inside its cell must not be cut by it
        float3 S = rd * sceneT;
        bool   ghost = age < 0.6 && all(S > corner - 0.03) && all(S < corner + 1.03);
        float  lim = (ghost && best >= sceneT) ? 1e9 : best;
        [loop] for (int k = 0; k < 8; k++) {
            float  sk = seed * 0.001 + float(k) * 7.31;
            float3 o0 = float3(((k & 1) ? 0.70 : 0.30), ((k & 2) ? 0.70 : 0.30), ((k & 4) ? 0.70 : 0.30)) + (float3(h1(sk), h1(sk + 1), h1(sk + 2)) - 0.5) * 0.14;
            float3 dir = normalize(o0 - 0.5 + float3(0, 0.45, 0));
            float3 v = dir * (1.4 + 1.3 * h1(sk + 3)) + float3(0, 1.6, 0);
            float3 e = float3(0.13 + 0.07 * h1(sk + 4), 0.11 + 0.06 * h1(sk + 5), 0.13 + 0.07 * h1(sk + 6)) * fade;
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
    if (m1.x < 0.5 || any(W < trMin.xyz - 0.05) || any(W > trMax.xyz + 0.05)) return 0.0;
    float3 acc = 0.0;
    int n = int(m1.x);
    [loop] for (int i = 0; i < n; i++) {
        float3 l = W - gA[i].xyz;
        if (any(l < -0.02) || any(l > 1.02)) continue;
        float3 e3 = min(saturate(l), 1.0 - saturate(l));          // distance to each pair of faces
        float  a = min(e3.x, min(e3.y, e3.z)), c = max(e3.x, max(e3.y, e3.z));
        float  edge = (e3.x + e3.y + e3.z) - a - c;                  // the middle one: distance to the face's edge
        float  age = gA[i].w;
        float  fade = age < 0.6 ? smoothstep(0.0, 0.15, age) : exp(-(age - 0.6) * 0.38);
        float3 col = pow(max(gB[i].rgb, 0.0), 2.2);
        col = col / max(max(col.r, max(col.g, col.b)), 0.05);       // its hue at full brightness
        acc += col * (exp(-edge * 26.0) * 1.1 + 0.10) * fade;
    }
    return acc;
}
// ---- final kill: the player's soul leaves as a streak of team light shooting into the sky, and a ring runs out over
//      the cloud sea under it. A normal kill gets a short, low flare.
float3 killLight(float3 rd, float sceneT) {
    float3 acc = 0.0;
    int ne = int(m0.y);
    [loop] for (int i = 0; i < ne; i++) {
        float kind = eB[i].w, age = eA[i].w;
        if (kind < 2.5 || kind > 4.5 || age > 4.2) continue;
        bool  fin = kind > 3.5;
        float3 team = pow(max(eB[i].rgb, 0.0), 2.2);
        team = team / max(max(team.r, max(team.g, team.b)), 0.05);
        float3 b = eA[i].xyz + float3(0.5, 1.0, 0.5) - CAM;
        // closest approach of the view ray to the vertical line through the body
        float2 o = -b.xz, d = rd.xz;
        float  dd = max(dot(d, d), 1e-6);
        float  t = -dot(o, d) / dd;
        float2 q = o + d * t;
        float  r = length(q);
        float  tr = t;                                   // the ray's distance at the line
        if (tr > 0.0 && tr < sceneT + 0.5) {
            float y = rd.y * tr - b.y;                   // height on the line, relative to the body
            // final: the soul climbs out of the body slowly, then shoots up (the column stays joined to the body a moment)
            float la = saturate((age - 0.22) / 1.6);
            float top = fin ? 2.0 + 240.0 * la * la * la + 6.0 * saturate(age * 4.0) : 2.5 + 4.0 * saturate(age * 3.0);
            float tail = fin ? max(top - 12.0 - 90.0 * la, -1.0) : -1.0;
            float on = smoothstep(-1.0, 0.0, y - tail) * smoothstep(0.5, 0.0, y - top);
            float w  = (fin ? 0.30 : 0.18) * (1.0 + 0.004 * tr);
            float head = exp(-pow((y - top) / (fin ? 6.0 : 1.5), 2.0));
            float life = fin ? exp(-max(age - 1.1, 0.0) * 1.6) : exp(-age * 3.5);
            acc += team * on * (exp(-r * r / (w * w)) * 3.0 + exp(-r / (w * 6.0)) * 0.35) * life * (1.0 + 2.0 * head);
        }
        if (fin) { // a ring on the cloud sea, and the sky flashes once
            float seaTop = SEA_Y + SEA_AMP * 1.1;
            if (rd.y < -0.005) {
                float t2 = (CAM.y - seaTop) / -rd.y;
                if (t2 > 0.0 && t2 < sceneT) {
                    float dd2 = length(CAM.xz + rd.xz * t2 - eA[i].xz);
                    float R = 4.0 + 40.0 * age, w2 = 1.5 + 2.0 * age;
                    acc += team * exp(-pow((dd2 - R) / w2, 2.0)) * pow(saturate(1.0 - age / 4.2), 1.3) * 3.0;
                }
            }
            if (sceneT > 1e8) acc += team * 0.25 * exp(-age * 3.0);
        }
    }
    return acc * m0.w;
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
    float a  = acos(clamp(dot(rd, c0.xyz), -1.0, 1.0));          // angle from the centre of the view
    float R  = age * 0.95;                                       // radians: the whole view in ~1.2 s
    float back = smoothstep(R, R - 0.12, a);                     // already back
    float l  = dot(o, float3(0.299, 0.587, 0.114));
    float3 grey = l * float3(0.55, 0.58, 0.68);
    float3 r = lerp(grey, o, back);
    r += lerp(team, float3(1, 1, 1), 0.45) * exp(-pow((a - R) / 0.025, 2.0)) * 0.30 * saturate(1.5 - age);  // a fine bright edge
    return r;
}
// ---- a team wins: fireworks of their colour over the map - bursts of sparks that bloom, fall and fade
float3 victory(float3 rd, float age, float3 team, float seed) {
    float3 acc = 0.0;
    [loop] for (int b = 0; b < 7; b++) {
        float sb = seed * 0.0007 + float(b) * 3.91;
        float t0 = float(b) * 0.75 + h1(sb) * 0.4;               // one after another
        float ab = age - t0;
        if (ab < 0.0 || ab > 2.6) continue;
        // in front of you (you are looking at the map): within ~55 degrees of where the camera faces
        float  fa = atan2(c0.z, c0.x);
        float az = fa + (h1(sb + 1.0) - 0.5) * 1.9, el = 0.22 + 0.40 * h1(sb + 2.0);
        float3 dir = float3(cos(az) * cos(el), sin(el), sin(az) * cos(el));
        float3 col = b % 3 == 2 ? lerp(team, float3(1, 1, 1), 0.6) : team;
        float rad = 0.16 * (1.0 - exp(-ab * 3.0));               // the shell opens fast then hangs
        float3 dd = rd - dir;
        float drop = 0.035 * ab * ab;                            // and sags as it falls
        dd.y += drop;
        float r = length(dd);
        // sparks: the shell broken into points (angle-hashed)
        float2 uv = float2(atan2(dd.z, dd.x), dd.y / max(r, 1e-4));
        float  spark = step(0.82, hash12(floor(uv * float2(9.0, 7.0)) + float2(b, b * 3)));
        float  shell = exp(-pow((r - rad) / 0.010, 2.0)) * (0.35 + 1.6 * spark);
        float  flash = exp(-r * r / 0.0009) * exp(-ab * 8.0) * 2.0;
        acc += col * (shell + flash) * pow(saturate(1.0 - ab / 2.6), 1.5);
    }
    return acc;
}
// everything the match adds over a finished pixel (display colour o) along view ray rd, scene at distance sceneT
float3 matchOver(float3 o, float3 rd, float sceneT) {
    [branch] if (m0.y > 0.5 && m0.w > 0.0) {
        float4 sh = shards(rd, sceneT);
        if (sh.w > 0.0) { o = sh.rgb; sceneT = sh.w; }
    }
    [branch] if (m0.x > 0.5 && (m0.z > 0.0 || m1.y > 0.0)) {
        float3 lin = pow(max(o, 0.0), 2.2) + beacons(rd, min(sceneT, 900.0)) + shockwaves(rd, sceneT, sceneT > 1e8);
        o = pow(lin, 1.0 / 2.2);
    }
    [branch] if (m0.y > 0.5 && m0.w > 0.0 && m1.z > 0.5) o = pow(pow(max(o, 0.0), 2.2) + killLight(rd, sceneT), 1.0 / 2.2);
    [branch] if (m0.x > 0.5 && m0.z > 0.0) o = pow(pow(max(o, 0.0), 2.2) + bedAlarm(rd) * m0.z, 1.0 / 2.2);
    [branch] if (m1.z > 0.5) {
        int ne = int(m0.y);
        [loop] for (int i = 0; i < ne; i++) {
            float kind = eB[i].w, age = eA[i].w;
            float3 team = pow(max(eB[i].rgb, 0.0), 2.2);
            team = team / max(max(team.r, max(team.g, team.b)), 0.05);
            if (kind > 4.5 && kind < 5.5 && age < 1.6) o = respawnFx(o, rd, age, team);
            if (kind > 6.5 && age < 0.7) { // your kill: a hard beat - the frame punches bright and warm at the edges, then settles
                float edge = smoothstep(0.92, 0.55, dot(rd, c0.xyz));
                float k = exp(-age * 7.0);
                o = lerp(o, saturate(o * 1.25 + 0.04), k * 0.6) + lerp(team, float3(1.0, 0.85, 0.55), 0.5) * edge * k * 0.35;
            }
            if (kind > 5.5 && kind < 6.5 && sceneT > 1e8) o = pow(pow(max(o, 0.0), 2.2) + victory(rd, age, team, eC[i].x), 1.0 / 2.2);
        }
    }
    return o;
}
bool matchActive() { return (m0.x > 0.5 && (m0.z > 0.0 || m1.y > 0.0)) || (m0.y > 0.5 && m0.w > 0.0); }

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
        float4 sea = rd.y > 0.02 ? float4(0, 0, 0, 0) : csSeaPixel(rd);
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
    [branch] if (c9.w > 0.0 && z < 160.0) {
        int   r  = 1;                                                // one pixel: a fine line, not a marker
        float zl = viewZ(rawDepth(px - int2(r, 0))), zr = viewZ(rawDepth(px + int2(r, 0)));
        float zu = viewZ(rawDepth(px - int2(0, r))), zd = viewZ(rawDepth(px + int2(0, r)));
        // silhouettes only (a clear step in depth: a player, an island edge, a bridge against the void) - no lines on
        // block corners or inside foliage, that is what turned leaves into noise
        float rel[4] = {(z - zl) / z, (z - zr) / z, (z - zu) / z, (z - zd) / z};
        float sil = 0.0, steps = 0.0;
        [unroll] for (int q4 = 0; q4 < 4; q4++) { sil = max(sil, smoothstep(0.12, 0.30, rel[q4])); steps += abs(rel[q4]) > 0.08 ? 1.0 : 0.0; }
        float busy = steps >= 3.0 ? 0.0 : 1.0;                     // depth jumping on most sides: leaves / grass, skip
        float far  = saturate(1.0 - (z - 50.0) / 60.0);
        ink = sil * busy * far * c9.w;
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
    [branch] if (m1.z > 0.5 && m0.w > 0.0) lin += (lin * 2.2 + 0.02) * killGlow(CAM + P);
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
