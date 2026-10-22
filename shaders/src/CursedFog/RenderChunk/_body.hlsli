// Terrain (RenderChunk) body - Cursed Fog: gbuffers_terrain + composite + composite1 + final in one forward pass.
// Included by Opaque / AlphaTest / Transparent (depth-only passes stay vanilla).
Texture2D    s_LightMapTextureTexture : register(t0);
SamplerState s_LightMapTextureSampler : register(s0);
Texture2D    s_MatTextureTexture      : register(t1);
SamplerState s_MatTextureSampler      : register(s1);
#if (defined(PASS_ALPHATEST) || defined(PASS_TRANSPARENT)) && defined(FLAG_DITHERING_ON)
// vanilla layout of the dithered variants, then the extra uniforms
cbuffer FragmentUniforms : register(b1) {
    float4x4 u_view; float4 DitherParams2[3]; float4 DitherParams; float4 FogColor; float4 ViewPositionAndTime;
    float4 FogAndDistanceControl; float4 LightWorldSpaceDirection; float4 u_viewRect;
};
#else
cbuffer FragmentUniforms : register(b1) {
    float4 FogColor;
    float4 ViewPositionAndTime;      // xyz camera (same space as TEXCOORD3), w = seconds
    float4 FogAndDistanceControl;    // z = render distance (blocks)
    float4 LightWorldSpaceDirection; // sun / moon direction when the game sets it
    float4 u_viewRect;               // bgfx predefined: x, y, width, height
};
#endif

float4 main(PSIn i) : SV_Target {
    // ---- gbuffers_terrain ----
    float4 d = s_MatTextureTexture.Sample(s_MatTextureSampler, i.texcoord0);
#if defined(PASS_ALPHATEST)
    if (d.a < 0.5) discard;
#endif
    float3 albedo = d.rgb * i.color0.rgb; // foliageColor = gl_Color.rgb (NEW_LIGHTING 1)
    float  a      = 1.0;
#if defined(PASS_TRANSPARENT)
    a = d.a * i.color0.a;
#elif !defined(PASS_ALPHATEST)
    if (i.texcoord2.y > 0.5) albedo = lerp(d.rgb, albedo, d.a) * i.color0.a; // vanilla tint mask
#endif
    float t     = ViewPositionAndTime.w;
    float state = cfStateFromSkyLight(s_LightMapTextureTexture.Sample(s_LightMapTextureSampler, float2(0.0, 1.0)).rgb);
    float rain  = 0.0;
#if RED_FILTER == 1
    albedo = lerp(albedo, CF_RED, 0.25 * state * (1 - rain));
#endif
    float2 LC = cfLightmap(i.texcoord1);

    float3 rel = i.texcoord3 - ViewPositionAndTime.xyz; // fragment relative to the camera
    float3 N   = normalize(cross(ddy(i.texcoord3), ddx(i.texcoord3)));
    if (dot(N, rel) > 0.0) N = -N;
    float3 sunDir = LightWorldSpaceDirection.xyz;
    sunDir = dot(sunDir, sunDir) > 0.25 ? normalize(sunDir) : normalize(float3(-0.3, 0.85, 0.42));
    float lightBrightness = saturate(dot(sunDir, N));
    // Shadow map substitute: full sky exposure = sunlit (Bedrock has no shadow pass on Fancy).
    float sunShadow = saturate((i.texcoord1.y * 15.0 - 13.0) * 0.5);

    // ---- composite / composite1 ----
    float3 c   = cfComposite(albedo, LC, lightBrightness, sunShadow, false, state, rain, t);
    float  far = FogAndDistanceControl.z > 1.0 ? FogAndDistanceControl.z : 96.0;
    c = cfFog(c, length(rel), far, FogColor.rgb, state, rain);

    // ---- final ----
    float2 vs = u_viewRect.z > 1.0 ? u_viewRect.zw : float2(1920.0, 1080.0);
    return float4(cfFinal(c, i.position.xy - u_viewRect.xy, vs, t), a);
}
