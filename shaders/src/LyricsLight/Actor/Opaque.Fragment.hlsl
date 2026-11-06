// LyricsLight mobs / players (Actor), plain variants: vanilla texture/overlay/fog maths, the same sky-following light
// as the terrain (warm lit side, cool shade); Extreme adds a sky-coloured rim so mobs stand out from the background.
// @only Change_Color=Off
// @only Emissive=Off
// @only MaskedMultitexture=Off
// @uniform FogColor vec4
// @uniform ViewPositionAndTime vec4
Texture2D    s_MatTextureTexture : register(t0);
SamplerState s_MatTextureSampler : register(s0);
cbuffer FragmentUniforms : register(b1) { float4 OverlayColor; float4 ColorBased; float4 MatColor; float4 FogColor; float4 ViewPositionAndTime; };

float4 main(PSIn i) : SV_Target {
    float3 c = s_MatTextureTexture.Sample(s_MatTextureSampler, i.texcoord0).rgb * MatColor.rgb;
    c *= (i.color0.rgb - 1.0) * ColorBased.x + 1.0;
    c  = lerp(c, OverlayColor.rgb, OverlayColor.a);
    float3 light = i.color3.rgb;                       // the game's per-vertex light (normal + light level)
    float3 wp = i.texcoord3;
    float3 V  = normalize(ViewPositionAndTime.xyz - wp);
    float3 n  = llNormal(wp, V, false);
    float  lv = llLuma(light);
    LLDir  dir = llDir(FogColor.rgb, n, smoothstep(0.35, 0.85, lv) * 0.97 + 0.03, smoothstep(0.2, 0.8, llLuma(FogColor.rgb) * 1.6));
    c *= light * llSkyShape(dir, n);
#if LL_EXTREME
    float rim = pow(1.0 - saturate(dot(n, V)), 3.0);
    c += dir.hue * rim * 0.22 * lerp(0.4, 1.0, lv);
#endif
    c = llGrade(c);
    c = lerp(c, i.color2.rgb, i.color2.a);
    return float4(c, 1.0);
}
