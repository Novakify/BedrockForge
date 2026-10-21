// Bedrock Forge shaderpack "Anime" - Sky fragment (original work, MIT).
// Same interface as the vanilla Sky pass (input signature must match the vertex stage):
//   COLOR0 = sky gradient colour from the game, TEXCOORD3 = world-space direction of the sky dome vertex.
// Effect: cel-shaded sky - the gradient is quantised into soft bands with a brighter horizon line,
// like hand-painted anime backgrounds.
struct PSIn {
    float4 position : SV_Position;
    float4 color    : COLOR0;
    float2 uv       : TEXCOORD0;
    float3 worldPos : TEXCOORD3;
};

float3 saturateColor(float3 c, float amount) {
    float l = dot(c, float3(0.2126, 0.7152, 0.0722));
    return lerp(float3(l, l, l), c, amount);
}

float4 main(PSIn i) : SV_Target {
    float3 dir    = normalize(i.worldPos + float3(0.0, 1e-4, 0.0));
    float  height = saturate(dir.y * 0.5 + 0.5);
    // 6 bands with a small soft edge between them
    float  bands  = 6.0;
    float  f      = height * bands;
    float  edge   = smoothstep(0.0, 0.08, frac(f)) ;
    float  q      = (floor(f) + edge * 0.15) / bands;
    float3 c      = i.color.rgb * lerp(0.82, 1.18, q);
    // horizon glow line
    float  horizon = exp(-abs(dir.y) * 28.0);
    c += horizon * 0.12 * (c + 0.15);
    c  = saturateColor(c, 1.25);
    return float4(c, i.color.a);
}
