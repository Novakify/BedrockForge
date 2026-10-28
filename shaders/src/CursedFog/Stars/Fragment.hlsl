// Stars - vanilla (COLOR.rgb * COLOR.a * StarsColor, decoded 1.26) + this pack's colour grade.
// @only Instancing=Off
cbuffer FragmentUniforms : register(b1) { float4 StarsColor; };
float3 celestialGrade(float3 c) { float a = (c.r + c.g + c.b) / 3.0; c = lerp(a.xxx, c, SATURATION); c = lerp(c, CF_RED * a * 1.4, 0.3); return max(c, 0.0); }
float4 main(PSIn i) : SV_Target {
    return float4(celestialGrade(i.color0.rgb * i.color0.a * StarsColor.rgb), i.color0.a);
}
