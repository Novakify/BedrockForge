// Sky - cel-banded gradient with a horizon glow (inputs as vanilla: COLOR0 gradient, TEXCOORD3 dome direction).
float4 main(PSIn i) : SV_Target {
    float3 dir    = normalize(i.texcoord3 + float3(0.0, 1e-4, 0.0));
    float  height = saturate(dir.y * 0.5 + 0.5);
    float  f      = height * 6.0;
    float  q      = (floor(f) + smoothstep(0.0, 0.08, frac(f)) * 0.15) / 6.0;
    float3 c      = i.color0.rgb * lerp(0.82, 1.18, q);
    c += exp(-abs(dir.y) * 28.0) * 0.12 * (c + 0.15);
    return float4(saturateColor(c, 1.25), i.color0.a);
}
