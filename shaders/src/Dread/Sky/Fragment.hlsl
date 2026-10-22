// Sky - Dread: drained, darker dome that sinks into a grey horizon haze.
float4 main(PSIn i) : SV_Target {
    float3 dir     = normalize(i.texcoord3 + float3(0.0, 1e-4, 0.0));
    float3 c       = desat(i.color0.rgb, 0.3) * 0.72;
    float  horizon = exp(-abs(dir.y) * 6.0);
    c = lerp(c, float3(0.30, 0.32, 0.34) * (luma(i.color0.rgb) * 0.9 + 0.1), horizon * 0.75);
    return float4(dreadGrain(c, i.position.xy), i.color0.a);
}
