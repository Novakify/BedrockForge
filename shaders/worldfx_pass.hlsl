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
    float4 c3; // x = A, y = B, z = Wz (projection terms: d = (A z + B) / (Wz z)), w = flags (1 = scene is BGRA)
    float4 c4; // xy = target size, z = scene row pitch (bytes), w = depth row pitch (bytes)
    float4 c5; // xyz = sun direction (world), w = shadow strength
    float4 c6; // x = occlusion strength, y = rim strength, z = debug view (0 off, 1 shadows, 2 occlusion), w = 0
};
ByteAddressBuffer gScene : register(t0); // the colour target, rows of c4.z bytes
ByteAddressBuffer gDepth : register(t1); // D24 in the low 24 bits, rows of c4.w bytes

float  hash12(float2 p) { p = frac(p * float2(0.1031, 0.1030)); p += dot(p, p.yx + 33.33); return frac((p.x + p.y) * p.x); }
int2   clampPx(int2 p) { return clamp(p, int2(0, 0), int2(c4.xy) - 1); }
float  rawDepth(int2 p) { p = clampPx(p); return float(gDepth.Load(uint(p.y) * uint(c4.w) + uint(p.x) * 4u) & 0xFFFFFFu) / 16777215.0; }
bool   isSky(float d) { return d >= 0.99999; }
float  viewZ(float d) { return abs(c3.y / (d * c3.z - c3.x)); } // distance along the camera's forward axis
float3 sceneAt(int2 p) {
    p = clampPx(p);
    uint  v = gScene.Load(uint(p.y) * uint(c4.z) + uint(p.x) * 4u);
    float3 c = float3(v & 255u, (v >> 8) & 255u, (v >> 16) & 255u) / 255.0;
    return c3.w > 0.5 ? c.bgr : c;
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

float4 PSWorldFx(VSOut i) : SV_Target {
    int2  px = int2(i.pos.xy);
    float d  = rawDepth(px);
    if (isSky(d)) discard;                                   // the sky is the sky pass's business
    float3 col = sceneAt(px);
    float  z   = viewZ(d);
    float3 P   = posAt(float2(px) + 0.5, z);
    // normal from the depth buffer: the flatter neighbour on each axis (no halos at block edges)
    float3 pl = posPx(px - int2(1, 0)), pr = posPx(px + int2(1, 0)), pu = posPx(px - int2(0, 1)), pd = posPx(px + int2(0, 1));
    float3 dx = abs(dot(pr - P, c0.xyz)) < abs(dot(P - pl, c0.xyz)) ? pr - P : P - pl;
    float3 dy = abs(dot(pd - P, c0.xyz)) < abs(dot(P - pu, c0.xyz)) ? pd - P : P - pu;
    float3 n  = normalize(cross(dy, dx));
    if (dot(n, -P) < 0.0) n = -n;
    float3 L  = c5.xyz;
    float  j  = hash12(float2(px)), j2 = hash12(float2(px.yx) + 13.1);

    // ---- sun shadow: walk toward the sun; anything in front of the ray on screen blocks it
    float vis = 1.0;
    float ndl = dot(n, L);
    [branch] if (ndl > 0.02 && c5.w > 0.0) {
        [loop] for (int k = 0; k < 24; k++) {
            float f = (float(k) + j) / 24.0;
            float t = 0.04 + 42.0 * f * f;
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
    [unroll] for (int k2 = 0; k2 < 10; k2++) {
        float a = (float(k2) + j) * 2.39996;
        float r = sqrt((float(k2) + j2) / 10.0);
        float h = sqrt(max(1.0 - r * r, 0.0));
        float rad = 1.5 * lerp(0.2, 1.0, float(k2) / 9.0);
        float3 q = P + n * 0.03 + (tA * cos(a) * r + tB * sin(a) * r + n * h) * rad;
        float3 s = toPx(q);
        if (s.z <= 0.05) continue;
        float sd = rawDepth(int2(s.xy));
        if (isSky(sd)) continue;
        float dz = s.z - viewZ(sd);
        occ += dz > 0.02 ? saturate(1.6 / max(dz, 1.6)) : 0.0;
    }
    float ao = 1.0 - occ / 10.0;
    ao = lerp(1.0, ao, saturate(1.0 - (z - 40.0) / 40.0));

    // ---- apply (linear light). Shadowed sunlit faces fall to the violet sky light; occlusion darkens gently
    float3 lin = pow(col, 2.2);
    float  sh  = (1.0 - vis) * lit * c5.w;
    float3 shadeCol = float3(0.30, 0.29, 0.46);               // what is left without the sun: the violet sky
    lin *= lerp(float3(1, 1, 1), shadeCol, sh);
    lin *= lerp(1.0, ao, c6.x);

    // ---- rim: an edge with open sky right behind it, on the sun's side, catches a thin warm line
    float3 sp = toPx(L * 1000.0);
    if (c6.y > 0.0 && sp.z > 0.0) {
        float2 to = normalize(sp.xy - float2(px) + 1e-3);
        float open = 0.0;
        [unroll] for (int k3 = 1; k3 <= 2; k3++) if (isSky(rawDepth(int2(float2(px) + to * (1.5 * k3))))) open += 0.5;
        float near = exp(-length((sp.xy - float2(px)) / c4.y) * 2.4);
        lin += float3(1.5, 0.74, 0.30) * open * near * c6.y * 0.55;
    }
    float3 o = pow(max(lin, 0.0), 1.0 / 2.2);
    if (c6.z > 0.5 && c6.z < 1.5) o = lerp(float3(1, 0.85, 0.6), float3(0.2, 0.2, 0.45), sh);
    if (c6.z > 1.5) o = ao.xxx;
    o += (j - 0.5) / 255.0;
    return float4(o, 1.0);
}
