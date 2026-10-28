// SunMoon - vanilla (texture * SunMoonColor, decoded 1.26) + this pack's colour grade. Additive blend kept.
// @only Instancing=Off
cbuffer FragmentUniforms : register(b1) { float4 SunMoonColor; };
Texture2D    s_SunMoonTextureTexture : register(t0);
SamplerState s_SunMoonTextureSampler : register(s0);
float3 celestialGrade(float3 c) { return max(saturateColor(c, 1.25), 0.0); }
float4 main(PSIn i) : SV_Target {
    float4 c = s_SunMoonTextureTexture.Sample(s_SunMoonTextureSampler, i.texcoord0) * SunMoonColor;
    return float4(celestialGrade(c.rgb), c.a);
}
