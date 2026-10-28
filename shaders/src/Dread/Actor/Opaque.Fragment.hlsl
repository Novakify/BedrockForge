// Mobs / players (Actor), plain variants - Dread: horror light curve, desaturation, heavy fog.
// @only Change_Color=Off
// @only Emissive=Off
// @only MaskedMultitexture=Off
// @uniform FogColor vec4
Texture2D    s_MatTextureTexture : register(t0);
SamplerState s_MatTextureSampler : register(s0);
cbuffer FragmentUniforms : register(b1) { float4 OverlayColor; float4 ColorBased; float4 MatColor; float4 FogColor; };

float4 main(PSIn i) : SV_Target {
    float3 c = s_MatTextureTexture.Sample(s_MatTextureSampler, i.texcoord0).rgb * MatColor.rgb;
    c *= (i.color0.rgb - 1.0) * ColorBased.x + 1.0;
    c  = lerp(c, OverlayColor.rgb, OverlayColor.a);
    c *= dreadLight(i.color3.rgb, float2(0.0, 1.0));
    c  = desat(c, 0.5);
    c  = lerp(c, desat(i.color2.rgb, 0.35) * 0.78, saturate(pow(saturate(i.color2.a), 0.45) * 1.1));
    return float4(c, 1.0);
}
