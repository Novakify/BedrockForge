// Renderer Foundation - World FX: Cloud Sea sky (by Novak). Drawn in place of Bedrock's sky (SkyPass stage 2):
// a sunset sky, high clouds lit from below, and a sea of cloud tops far under the map.
// Same maths as the Cloud Sea lab (Bedrock Forge tools/WorldFx/lab/cloudsea.glsl) - keep the two in step.
// Compiled offline by tools/gen_worldfx.py -> src/WorldFxShaders.h.

cbuffer C : register(b0) {
    float4 c0; // xyz = camera forward, w = tan(fovx/2)
    float4 c1; // xyz = camera right,   w = tan(fovy/2)
    float4 c2; // xyz = camera up,      w = time (seconds)
    float4 c3; // xyz = camera position (world), w = cloud deck base (world y)
    float4 c4; // xy = map centre (world xz: the towers keep clear of it), z = far depth, w = 0
};
#define CS_NOISE_REG t0
#define CS_PUFF_REG t1
#include "cloud_sea_common.hlsli"

struct VSOut { float4 pos : SV_Position; float3 dir : TEXCOORD0; };
VSOut VSCloudSea(uint id : SV_VertexID) {
    float2 v = float2(id == 2 ? 3.0 : -1.0, id == 1 ? 3.0 : -1.0);
    VSOut  o;
    o.pos = float4(v, c4.z, 1.0);
    o.dir = c0.xyz + c1.xyz * (v.x * c0.w) + c2.xyz * (v.y * c1.w);
    return o;
}
float4 PSCloudSea(VSOut i) : SV_Target {
    CAM = c3.xyz; TIME = c2.w; SEA_Y = c3.w; MAP_C = c4.xy;
    float3 rd = normalize(i.dir);
    float  mu = dot(rd, SUN);
    float  ang = acos(clamp(mu, -1.0, 1.0));
    float3 col = skyBase(rd);
    float4 hc = highClouds(rd, mu);
    float  disc = smoothstep(0.0125, 0.0095, ang);
    float3 sun = float3(3.2, 2.3, 1.25) * disc + float3(1.5, 0.78, 0.30) * exp(-ang * 38.0) * 0.8;
    col += sun * (1.0 - hc.a * 0.85);
    col = col * (1.0 - hc.a) + hc.rgb;
    float  tHit;
    float4 sea = cloudSea(CAM, rd, 3400.0, tHit);
    if (sea.a > 0.0) col = sea.rgb;
    // the wide sun glow (the lab draws it in its light-shaft pass; until the in-game shaft pass exists it lives here)
    col += float3(1.0, 0.47, 0.17) * 0.42 * exp(-ang * 3.4) * (1.0 - 0.5 * hc.a);
    float3 o = toDisplay(col);
    o += (hash12(i.pos.xy) - 0.5) / 255.0 + (hash12(i.pos.yx + 7.0) - 0.5) / 255.0; // dither: no banding
    return float4(o, 1.0);
}
