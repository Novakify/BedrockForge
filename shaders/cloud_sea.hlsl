// Renderer Foundation - World FX: Cloud Sea sky (by Novak). Drawn in place of Bedrock's sky (SkyPass stage 2):
// a sunset sky, high clouds lit from below, and a sea of cloud tops far under the map.
// Same maths as the Cloud Sea lab (Bedrock Forge tools/WorldFx/lab/cloudsea.glsl) - keep the two in step.
// Compiled offline by tools/gen_worldfx.py -> src/WorldFxShaders.h.

cbuffer C : register(b0) {
    float4 c0; // xyz = camera forward, w = tan(fovx/2)
    float4 c1; // xyz = camera right,   w = tan(fovy/2)
    float4 c2; // xyz = camera up,      w = time (seconds)
    float4 c3; // xyz = camera position (world), w = cloud deck base (world y)
    float4 c4; // xy = map centre (world xz: the towers keep clear of it), z = quality 0..1, w = match phase (0 golden, 1 dusk, 2 night)
    float4 c5; // Vibrant Visuals: x = far depth, y = HDR gain, z = 1 in VV (0 classic), w = top of the cloud deck's fog (world y)
    float4 c6; // Vibrant Visuals: xy = the sun's compass direction (xz), zw = the moon's - the game's own, so VV's light and shadows line up with the sky
};
#define NG_ATMOS 2
#define CS_NOISE_REG t0
#define CS_PUFF_REG t1
#include "cloud_sea_common.hlsli"

struct VSOut { float4 pos : SV_Position; float3 dir : TEXCOORD0; };
VSOut VSCloudSea(uint id : SV_VertexID) {
    float2 v = float2(id == 2 ? 3.0 : -1.0, id == 1 ? 3.0 : -1.0);
    VSOut  o;
    o.pos = float4(v, c5.z > 0.5 ? c5.x : 1.0, 1.0);   // VV: at the far plane, depth-tested: only pixels that are still sky
    o.dir = c0.xyz + c1.xyz * (v.x * c0.w) + c2.xyz * (v.y * c1.w);
    return o;
}
float4 PSCloudSea(VSOut i) : SV_Target {
    CAM = c3.xyz; TIME = c2.w; SEA_Y = c3.w; MAP_C = c4.xy; ATMOS = c4.z >= 1.5 ? 1.0 : 0.0;   // quality + 2 = the Novak Glass atmosphere
    csSetPhase(c4.w);
    QUALITY = c4.z - 2.0 * ATMOS;
    float3 rd = normalize(i.dir);
    float3 col = skyBase(rd); // just the gradient: the World FX pass draws the real sky on pixels still sky
    float3 o = toDisplay(col);
    o += (hash12(i.pos.xy) - 0.5) / 255.0 + (hash12(i.pos.yx + 7.0) - 0.5) / 255.0; // dither: no banding
    return float4(o, 1.0);
}

// Vibrant Visuals: the whole Cloud Sea sky (gradient, sun, high clouds, the moon, stars and the cloud sea itself) in
// linear light straight into the game's HDR target - VV tonemaps it with the rest of the frame. Only sky pixels get it
// (the pipeline depth-tests at the far plane). Inside the cloud deck the sky goes to the deck's own lit colour, so
// falling into the clouds reads like the geometry around you, which VV's volumetric fog already wraps.
float4 PSCloudSeaVV(VSOut i) : SV_Target {
    CAM = c3.xyz; TIME = c2.w; SEA_Y = c3.w; MAP_C = c4.xy; ATMOS = c4.z >= 1.5 ? 1.0 : 0.0;
    if (dot(c6.xy, c6.xy) > 0.5) { SUN_AZ = c6.xy; MOON_AZ = c6.zw; }
    csSetPhase(c4.w);
    QUALITY = c4.z - 2.0 * ATMOS;
    float3 rd  = normalize(i.dir);
    float3 col = csBackground(rd, true);
    float  inDeck = saturate((c5.w - CAM.y) / 18.0);
    [branch] if (inDeck > 0.0) {
        float3 lit = lerp(float3(1.00, 0.72, 0.48), float3(0.30, 0.36, 0.52), NIGHT) * lerp(0.55, 0.20, DUSK);
        col = lerp(col, lit * (0.75 + 0.25 * saturate(dot(rd, LDIR) * 0.5 + 0.5)), inDeck * inDeck * (3.0 - 2.0 * inDeck));
    }
    return float4(max(col, 0.0) * c5.y, 1.0);
}
