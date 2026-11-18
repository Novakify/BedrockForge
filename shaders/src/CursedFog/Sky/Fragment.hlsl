// Sky dome - Cursed Fog gbuffers_skybasic (+ composite pass-through for depth 1, + final).
// @uniform FogColor vec4
// @uniform SkyColor vec4
// @uniform u_viewRect vec4
cbuffer FragmentUniforms : register(b1) { float4 FogColor; float4 SkyColor; float4 u_viewRect; };

float fogify(float x, float w) { return w / (x * x + w); }

float4 main(PSIn i) : SV_Target {
    float3 dir   = normalize(i.texcoord3 + float3(0.0, 1e-4, 0.0));
    float  upDot = dir.y;
    float3 color = lerp(SkyColor.rgb, FogColor.rgb, fogify(max(upDot, 0.0), 0.25));
    float  state = cfStateFromFog(FogColor.rgb);
    float  rain  = 0.0;
    float  average = (color.r + color.g + color.b) / 3.0;
    color = lerp(average.xxx, color, 0.35);
#if RED_FILTER == 1
    color = lerp(color, CF_RED, 0.3 * state * (1 - rain));
#endif
    color = lerp(color, float3(0.5, 0.5, 0.5), 0.25 * state * rain);
    float2 vs = u_viewRect.z > 1.0 ? u_viewRect.zw : float2(1920.0, 1080.0);
    // composite: Albedo = pow(colortex0, 2.2) for sky pixels; composite1 leaves the sky unfogged; then final.
    return float4(cfFinal(pow(max(color, 0.0), 2.2), i.position.xy - u_viewRect.xy, vs, 0.0), i.color0.a);
}
