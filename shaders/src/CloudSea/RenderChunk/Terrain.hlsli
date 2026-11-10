// Cloud Sea terrain (RenderChunk): vanilla texture / tint / dither maths, Cloud Sea light.
// PASS_ALPHATEST / PASS_TRANSPARENT come from ShaderForge per pass.
// @uniform ViewPositionAndTime vec4
Texture2D    s_LightMapTextureTexture : register(t0);
SamplerState s_LightMapTextureSampler : register(s0);
Texture2D    s_MatTextureTexture      : register(t1);
SamplerState s_MatTextureSampler      : register(s1);
#if defined(FLAG_SEASONS_ON) && !defined(PASS_TRANSPARENT)
Texture2D    s_SeasonsTextureTexture  : register(t2);
SamplerState s_SeasonsTextureSampler  : register(s2);
#define CS_TINT(c) ((c).a * (((s_SeasonsTextureTexture.Sample(s_SeasonsTextureSampler, (c).xy).rgb * 2.0 - 1.0) * (c).b) + 1.0))
#define CS_SEASONS 1
#else
#define CS_TINT(c) ((c).rgb)
#define CS_SEASONS 0
#endif
#if (defined(PASS_ALPHATEST) || defined(PASS_TRANSPARENT)) && defined(FLAG_DITHERING_ON)
cbuffer FragmentUniforms : register(b1) {
    float4x4 u_view; float4 DitherParams2[3]; float4 DitherParams; float4 FogColor; float4 ViewPositionAndTime;
};
#else
cbuffer FragmentUniforms : register(b1) { float4 FogColor; float4 ViewPositionAndTime; };
#endif

float4 main(PSIn i) : SV_Target {
    float4 d = s_MatTextureTexture.Sample(s_MatTextureSampler, i.texcoord0);
#if defined(PASS_ALPHATEST)
    bool kill = d.a < 0.5;
#if defined(FLAG_DITHERING_ON)
    if (i.texcoord2.x > 0.5)
        kill = kill || bfDitherKill(i.color3.xyz, ViewPositionAndTime.xyz, u_view[2].xyz, i.color1, DitherParams2[2], DitherParams);
#endif
    if (kill) discard;
    float3 tint = CS_TINT(i.color0);
    float3 c = d.rgb * tint;
    float  a = d.a;
#else
    float3 tint = CS_TINT(i.color0);
    float3 c = d.rgb * tint;
#if defined(PASS_TRANSPARENT)
    float  a = d.a * i.color0.a;
#else
    float  a = i.color0.a;
#endif
    if (!CS_SEASONS && i.texcoord2.y > 0.5) { c = lerp(d.rgb, c, d.a) * i.color0.a; a = 1.0; }
#if defined(PASS_TRANSPARENT) && defined(FLAG_DITHERING_ON)
    if (i.texcoord2.x > 0.5 && bfDitherKill(i.color3.xyz, ViewPositionAndTime.xyz, u_view[2].xyz, i.color1, DitherParams2[2], DitherParams)) a = 0.0;
#endif
#endif
    float3 wp  = i.texcoord3;
    float3 cam = ViewPositionAndTime.xyz;
    float3 V   = normalize(cam - wp);
#if defined(PASS_OPAQUE)
    float3 n   = csNormal(wp, V, true);
#else
    float3 n   = csNormal(wp, V, false);
#endif
    float2 lm  = i.texcoord1;                      // x = block light, y = sky light
    // calm the texture's pixel noise a little: pull toward the tile's average (its far mip)
#if defined(PASS_OPAQUE)
    float3 mean = s_MatTextureTexture.SampleLevel(s_MatTextureSampler, i.texcoord0, 3.0).rgb * tint;   // atlas has 4 mips
    c = lerp(mean, c, 0.78);
#endif
    float3 alb = csLin(c / csVanillaShade(n));
#if defined(PASS_ALPHATEST)
    float leaf = (CS_SEASONS || csMax3(tint) - min(tint.r, min(tint.g, tint.b)) > 0.04) ? 1.0 : 0.0;
#else
    float leaf = CS_SEASONS ? 1.0 : 0.0;
#endif
    float sunVis = smoothstep(0.78, 0.97, lm.y);
    float3 lit = csLight(alb, n, sunVis, lm.y, leaf, V);
    // torches and lamps: warm, soft
    float blk = pow(saturate(lm.x), 1.7);
    lit += alb * float3(1.0, 0.62, 0.32) * blk * 1.4 * (1.0 - 0.6 * saturate(lm.y));
    lit = csHaze(lit, wp, cam, i.color2.w);
    return float4(csEnc(lit), a);
}
