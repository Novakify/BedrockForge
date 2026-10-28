// Clouds - Dread: heavy, grey, slightly darker underside.
// @uniform FogColor vec4
cbuffer FragmentUniforms : register(b1) { float4 FogColor; };
float4 main(PSIn i) : SV_Target {
    float3 c = desat(i.color0.rgb, 0.25) * 0.62;
    return float4(c, i.color0.a);
}
