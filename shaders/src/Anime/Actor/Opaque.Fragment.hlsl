// Mobs / players (Actor), plain variants only - vanilla math with cel lighting on the per-vertex light.
// @only Change_Color=Off
// @only Emissive=Off
// @only MaskedMultitexture=Off
Texture2D    s_MatTextureTexture : register(t0);
SamplerState s_MatTextureSampler : register(s0);
cbuffer FragmentUniforms : register(b1) { float4 OverlayColor; float4 ColorBased; float4 MatColor; };

float4 main(PSIn i) : SV_Target {
    float3 c = s_MatTextureTexture.Sample(s_MatTextureSampler, i.texcoord0).rgb * MatColor.rgb;
    c *= (i.color0.rgb - 1.0) * ColorBased.x + 1.0;
    c  = lerp(c, OverlayColor.rgb, OverlayColor.a);
    c *= toonLight(i.color3.rgb);
    c  = saturateColor(c, 1.12);
    c  = lerp(c, i.color2.rgb, i.color2.a);
    return float4(c, 1.0);
}
