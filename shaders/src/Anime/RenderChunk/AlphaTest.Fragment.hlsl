// Terrain (RenderChunk) - same inputs and bindings as vanilla; vanilla math + cel lighting + a little saturation.
// PASS_ALPHATEST / PASS_TRANSPARENT are defined by ShaderForge per pass.
Texture2D    s_LightMapTextureTexture : register(t0);
SamplerState s_LightMapTextureSampler : register(s0);
Texture2D    s_MatTextureTexture      : register(t1);
SamplerState s_MatTextureSampler      : register(s1);
cbuffer FragmentUniforms : register(b1) { float4 FogColor; };

float4 main(PSIn i) : SV_Target {
    float4 d = s_MatTextureTexture.Sample(s_MatTextureSampler, i.texcoord0);
#if defined(PASS_ALPHATEST)
    if (d.a < 0.5) discard;
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
#endif
    float3 light = s_LightMapTextureTexture.Sample(s_LightMapTextureSampler, i.texcoord1).rgb;
    c *= toonLight(light);
    c  = saturateColor(c, 1.15);
    c  = lerp(c, FogColor.rgb, i.color2.w);
    return float4(c, a);
}
