// Clouds - Dread: heavy, grey, slightly darker underside.
float4 main(PSIn i) : SV_Target {
    float3 c = desat(i.color0.rgb, 0.25) * 0.8;
    return float4(c, i.color0.a);
}
