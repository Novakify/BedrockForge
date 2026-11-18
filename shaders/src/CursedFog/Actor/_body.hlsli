// Actor body - Cursed Fog gbuffers_entities + composite (entity branch) + composite1 + final.
float4 main(PSIn i) : SV_Target {
    // gbuffers_entities: albedo = texture * Color; entityColor (hurt flash) = OverlayColor
    float4 tex = s_MatTextureTexture.Sample(s_MatTextureSampler, i.texcoord0);
#if defined(PASS_ALPHATEST)
    if (tex.a < 0.5) discard;
#endif
    float3 albedo = tex.rgb * MatColor.rgb;
    albedo *= (i.color0.rgb - 1.0) * ColorBased.x + 1.0;
    albedo  = lerp(albedo, OverlayColor.rgb, OverlayColor.a);
    float state = cfStateFromFog(FogColor.rgb);
    float rain  = 0.0;
#if RED_FILTER == 1
    albedo = lerp(albedo, CF_RED, 0.25 * state * (1 - rain));
#endif
    float3 N = normalize(cross(ddy(i.texcoord3), ddx(i.texcoord3)));
    float3 sunDir = LightWorldSpaceDirection.xyz;
    sunDir = dot(sunDir, sunDir) > 0.25 ? normalize(sunDir) : normalize(float3(-0.3, 0.85, 0.42));
    float diffuseLight = saturate(abs(dot(sunDir, N)));
    // Bedrock bakes the entity's tile light into COLOR3 (no separate block / sky levels): sky level from its brightness.
    float  lum = dot(i.color3.rgb, float3(0.2126, 0.7152, 0.0722));
    float2 LC  = float2(0.0, saturate(lum));
    float3 c   = cfComposite(albedo, LC, diffuseLight, saturate((lum - 0.8) * 5.0), true, state, rain, 0.0);
    float  far = FogControl.z > 1.0 ? FogControl.z : 96.0;
    c = cfFog(c, i.color1.w, far, FogColor.rgb, state, rain);
    float2 vs = u_viewRect.z > 1.0 ? u_viewRect.zw : float2(1920.0, 1080.0);
    #if defined(PASS_TRANSPARENT)
    float outA = tex.a * MatColor.a;
#else
    float outA = 1.0;
#endif
    return float4(cfFinal(c, i.position.xy - u_viewRect.xy, vs, 0.0), outA);
}
