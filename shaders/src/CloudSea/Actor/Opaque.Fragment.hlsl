// Cloud Sea mobs / players (Actor), plain variants: vanilla texture / overlay maths, the same sunset light as the
// blocks, so players don't look pasted onto the world.
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
    c  = lerp(c, OverlayColor.rgb, OverlayColor.a);         // hurt flash etc. stay vanilla
    float  lv  = csLuma(i.color3.rgb);                      // the game's light level here (normal shading divided out below)
    float3 wp  = i.texcoord3;
    float3 cam = ViewPositionAndTime.xyz;
    float3 V   = normalize(cam - wp);
    float3 n   = csNormal(wp, V, false);
    float  sky = smoothstep(0.30, 0.80, lv);
    float3 lit = csLight(csLin(c), n, sky, sky * 0.9 + 0.1, 0.0, V);
    lit = csHaze(lit, wp, cam, i.color2.a);
    return float4(csEnc(lit), 1.0);
}
