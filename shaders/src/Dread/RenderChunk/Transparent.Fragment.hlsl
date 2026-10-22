// Terrain (RenderChunk) - Dread: same inputs/bindings as vanilla; horror light curve, desaturation, heavy fog, grain.
// PASS_ALPHATEST / PASS_TRANSPARENT are defined by ShaderForge per pass.
Texture2D    s_LightMapTextureTexture : register(t0);
SamplerState s_LightMapTextureSampler : register(s0);
Texture2D    s_MatTextureTexture      : register(t1);
SamplerState s_MatTextureSampler      : register(s1);
#if (defined(PASS_ALPHATEST) || defined(PASS_TRANSPARENT)) && defined(FLAG_DITHERING_ON)
cbuffer FragmentUniforms : register(b1) {
    float4x4 u_view; float4 DitherParams2[3]; float4 DitherParams; float4 FogColor; float4 ViewPositionAndTime;
};
#else
cbuffer FragmentUniforms : register(b1) { float4 FogColor; };
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
    float3 c = d.rgb * i.color0.rgb;
    float  a = d.a;
#else
    float3 c = d.rgb * i.color0.rgb;
#if defined(PASS_TRANSPARENT)
    float  a = d.a * i.color0.a;
#else
    float  a = i.color0.a;
#endif
    if (i.texcoord2.y > 0.5) { c = lerp(d.rgb, c, d.a) * i.color0.a; a = 1.0; }
#if defined(PASS_TRANSPARENT) && defined(FLAG_DITHERING_ON)
    // transparent geometry fades out by alpha instead of discarding
    if (i.texcoord2.x > 0.5 && bfDitherKill(i.color3.xyz, ViewPositionAndTime.xyz, u_view[2].xyz, i.color1, DitherParams2[2], DitherParams)) a = 0.0;
#endif
#endif
    float3 light = s_LightMapTextureTexture.Sample(s_LightMapTextureSampler, i.texcoord1).rgb;
    c *= dreadLight(light, i.texcoord1);
    c  = desat(c, 0.62);
    c  = dreadFog(c, FogColor.rgb, i.color2.w);
    c  = dreadGrain(c, i.position.xy);
    return float4(c, a);
}
