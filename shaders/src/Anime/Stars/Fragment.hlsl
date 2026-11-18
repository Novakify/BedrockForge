// Stars - vanilla (COLOR.rgb * COLOR.a * StarsColor, decoded 1.26) + this pack's colour grade.
// @only Instancing=Off
cbuffer FragmentUniforms : register(b1) { float4 StarsColor; };
float3 celestialGrade(float3 c) { return max(saturateColor(c, 1.25), 0.0); }
float4 main(PSIn i) : SV_Target {
    return float4(celestialGrade(i.color0.rgb * i.color0.a * StarsColor.rgb), i.color0.a);
}
