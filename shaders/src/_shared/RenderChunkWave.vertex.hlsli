// RenderChunk vertex shader: an exact HLSL rebuild of the game's own (decoded from its DXIL: packed UVs, lightmap
// nibbles, fog factor, sub-pixel jitter) + swaying leaves / plants in the AlphaTest pass.
// Sway formula: Cursed Fog gbuffers_terrain.vsh by LoLip_p (CC BY 4.0) - two sine waves, x/y/z weights 0.35/0.25/0.15,
// clamped so a vertex never leaves its block. Plants are detected by their biome tint (vertex colour not grey), since
// Bedrock gives shaders no block ids.
cbuffer VertexUniforms : register(b0) {
    column_major float4x4 u_proj  : packoffset(c0);
    column_major float4x4 u_view  : packoffset(c4);
    column_major float4x4 u_model : packoffset(c8);
    float4 FogAndDistanceControl : packoffset(c24);
    float4 FogColor              : packoffset(c25);
    float4 MeshContext           : packoffset(c26);
    float4 RenderChunkFogAlpha   : packoffset(c27);
    float4 SubPixelOffset        : packoffset(c28);
    float4 ViewPositionAndTime   : packoffset(c29);
};

#ifndef WAVE_STRENGTH
#define WAVE_STRENGTH 1.0
#endif
// WAVE_STYLE 0 = Cursed Fog sway, 1 = Hysteria wind (include/vertex/displacement.glsl),
//            2 = Insanity WavingBlocks (lib/vertex/waving.glsl). Packs pick one before the @include.
#ifndef WAVE_STYLE
#define WAVE_STYLE 0
#endif

float wvHash(float2 p) { return frac(sin(dot(p, float2(12.9898, 4.1414))) * 43758.5453); }
float wvNoise(float2 p) {              // Insanity Noise2D (value noise, -0.5..0.5)
    float2 f = floor(p), r = frac(p); r = r * r * (3.0 - 2.0 * r);
    float n0 = lerp(wvHash(f), wvHash(f + float2(0, 1)), r.y);
    float n1 = lerp(wvHash(f + float2(1, 0)), wvHash(f + float2(1, 1)), r.y);
    return lerp(n0, n1, r.x) - 0.5;
}
float3 wvCalcMove(float3 pos, float t, float density, float speed, float2 mult) {   // Insanity CalcMove
    pos = pos * density + float3(0.0, t * (speed * 6.0), 0.0);
    float3 w = float3(wvNoise(pos.yz + 0.667), wvNoise(pos.xz - 0.633), wvNoise(pos.xy));
    return w * float3(mult, mult.x);
}
float3 wvHysWind(float3 wp, float t, float strength) {                             // Hysteria get_wind_displacement
    const float2 dir = float2(0.8660254, 0.5);                                     // 30 degrees
    t *= 0.3;                                                                      // wind_speed
    // noisetex.y (512 px, mean 0.43 / sd 0.14, ~8 px features) sampled at 0.05*(xz + dir*t): value noise with the same stats
    float g = 0.429 + 0.66 * wvNoise(0.05 * (wp.xz + dir * t) * 512.0 / 8.0);
    g *= g;
    float3 gust = float3(dir.x * g, 0.1 * g, dir.y * g);
    float3 q = 32.0 * wp + 3.0 * t + float3(0.0, 2.3999632, 4.7999264);
    float3 wobble = sin(q) + 0.5 * sin(2.0 * q) + 0.25 * sin(4.0 * q);
    return strength * (gust + 0.1 * wobble);
}

VSOut main(VSIn i) {
    VSOut o = (VSOut)0;
    uint2 u   = (uint2)round(i.texcoord0 * 65535.0);
    float2 uv = float2((u << 1) & 65534u) * (1.0 / 65535.0) + float2((u >> 15) & 1u) * (1.0 / 16384.0) - (1.0 / 32768.0);
    uint2 t1  = (uint2)round(i.texcoord1 * 65535.0);
    float2 flags = float2((t1 & 256u) != 0u);
    float2 lm    = float2((t1.y >> 4) & 15u, t1.y & 15u) * (1.0 / 15.0);

    float3 worldPos = mul(u_model, float4(i.position0, 1.0)).xyz;
#if defined(PASS_ALPHATEST) || defined(PASS_DEPTHONLYALPHATEST) || defined(FLAG_SEASONS_ON)
    float3 c = i.color0.rgb;
  #if defined(FLAG_SEASONS_ON)
    const bool leaf = true;                       // the Seasons variants draw leaves (colour = seasons-map uv)
    bool sway = true;
  #else
    const bool leaf = false;
    bool sway = abs(c.r - c.g) > 0.02 || abs(c.g - c.b) > 0.02;  // biome-tinted plants: grass, ferns, vines
  #endif
    if (sway) {
        float3 p = i.position0;                   // chunk-local: stable while the camera moves
        float  t = ViewPositionAndTime.w;
  #if WAVE_STYLE == 1
        float sky = lm.y;
        worldPos += wvHysWind(p, t, sky * sky * 0.25 * (leaf ? 0.5 : 1.0)) * WAVE_STRENGTH;
  #elif WAVE_STYLE == 2
        worldPos += (leaf ? wvCalcMove(p, t, 0.25, 1.0, float2(0.28, 0.18))
                          : wvCalcMove(p, t, 1.35, 1.0, float2(0.25, 0.06))) * WAVE_STRENGTH;
  #else
        float fy = frac(p.y - 0.1);
        float wave = 0.05 * sin(3.0 * 3.14 * (t * 0.2 + p.x / 2.5 + p.z / 5.0))
                   + 0.05 * -sin(2.0 * 3.14 * (t * 0.4 + p.x / 6.0 + p.z / 12.0));
        float w = clamp(wave, -fy, 1.0 - fy) * WAVE_STRENGTH;
        worldPos += float3(0.35, 0.25, 0.15) * w;
  #endif
    }
#endif
    float  dist = length(ViewPositionAndTime.xyz - worldPos);
    float3 fc   = MeshContext.x > 0.5 ? float3(0.99, 1.0, 1e5) : FogAndDistanceControl.xyz;
    float  fog  = saturate((dist / fc.z + RenderChunkFogAlpha.x - fc.x) / (fc.y - fc.x));
    float4x4 proj = u_proj;
    proj[0][2] += SubPixelOffset.x;
    proj[1][2] -= SubPixelOffset.y;
    float4 view = mul(u_view, float4(worldPos, 1.0));
    o.position  = mul(proj, view);
#ifdef HAS_OUT_COLOR0
    o.color0    = i.color0;
#endif
#ifdef HAS_OUT_TEXCOORD2
    o.texcoord2 = flags;
#endif
#ifdef HAS_OUT_COLOR2
    o.color2    = float4(FogColor.rgb, fog);
#endif
#ifdef HAS_OUT_TEXCOORD1
    o.texcoord1 = lm;
#endif
#ifdef HAS_OUT_TEXCOORD0
    o.texcoord0 = uv;
#endif
#ifdef HAS_OUT_TEXCOORD3
    o.texcoord3 = worldPos;
#endif
#ifdef HAS_OUT_COLOR1
    o.color1 = o.position;              // vanilla: clip position (distance dithering)
#endif
#ifdef HAS_OUT_COLOR3
    o.color3 = float4(worldPos, 0.0);   // vanilla: world position
#endif
    return o;
}
