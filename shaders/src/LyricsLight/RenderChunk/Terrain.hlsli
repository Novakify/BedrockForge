// LyricsLight terrain (RenderChunk): same inputs/bindings as vanilla. Vanilla texture/tint/fog maths, our lighting.
// PASS_ALPHATEST / PASS_TRANSPARENT come from ShaderForge per pass.
// @uniform ViewPositionAndTime vec4
Texture2D    s_LightMapTextureTexture : register(t0);
SamplerState s_LightMapTextureSampler : register(s0);
Texture2D    s_MatTextureTexture      : register(t1);
SamplerState s_MatTextureSampler      : register(s1);
#if defined(FLAG_SEASONS_ON) && !defined(PASS_TRANSPARENT)
// leaves: vanilla tint = colour.a * lerp(1, seasons(colour.xy) * 2, colour.b)  (decoded from the Seasons variant)
Texture2D    s_SeasonsTextureTexture  : register(t2);
SamplerState s_SeasonsTextureSampler  : register(s2);
#define LL_TINT(c) ((c).a * (((s_SeasonsTextureTexture.Sample(s_SeasonsTextureSampler, (c).xy).rgb * 2.0 - 1.0) * (c).b) + 1.0))
#define LL_SEASONS 1
#else
#define LL_TINT(c) ((c).rgb)
#define LL_SEASONS 0
#endif
#if (defined(PASS_ALPHATEST) || defined(PASS_TRANSPARENT)) && defined(FLAG_DITHERING_ON)
cbuffer FragmentUniforms : register(b1) {
    float4x4 u_view; float4 DitherParams2[3]; float4 DitherParams; float4 FogColor; float4 ViewPositionAndTime;
};
#else
cbuffer FragmentUniforms : register(b1) { float4 FogColor; float4 ViewPositionAndTime; };
#endif

float3 llLightAt(float2 uv) { return s_LightMapTextureTexture.Sample(s_LightMapTextureSampler, uv).rgb; }

float4 main(PSIn i) : SV_Target {
    float4 d = s_MatTextureTexture.Sample(s_MatTextureSampler, i.texcoord0);
#if defined(PASS_ALPHATEST)
    bool kill = d.a < 0.5;
#if defined(FLAG_DITHERING_ON)
    if (i.texcoord2.x > 0.5)
        kill = kill || bfDitherKill(i.color3.xyz, ViewPositionAndTime.xyz, u_view[2].xyz, i.color1, DitherParams2[2], DitherParams);
#endif
    if (kill) discard;
    float3 c = d.rgb * LL_TINT(i.color0);
    float  a = d.a;
#else
    float3 c = d.rgb * LL_TINT(i.color0);
#if defined(PASS_TRANSPARENT)
    float  a = d.a * i.color0.a;
#else
    float  a = i.color0.a;
#endif
    if (!LL_SEASONS && i.texcoord2.y > 0.5) { c = lerp(d.rgb, c, d.a) * i.color0.a; a = 1.0; }
#if defined(PASS_TRANSPARENT) && defined(FLAG_DITHERING_ON)
    if (i.texcoord2.x > 0.5 && bfDitherKill(i.color3.xyz, ViewPositionAndTime.xyz, u_view[2].xyz, i.color1, DitherParams2[2], DitherParams)) a = 0.0;
#endif
#endif
    float3 albedo = c;
    float2 lm  = i.texcoord1;                      // x = block light, y = sky light
    float  t   = ViewPositionAndTime.w;
    float3 wp  = i.texcoord3;
    float3 V   = normalize(ViewPositionAndTime.xyz - wp);
#if defined(PASS_OPAQUE)
    float3 n   = llNormal(wp, V, true);
#else
    float3 n   = llNormal(wp, V, false);
#endif

    float3 vanilla = llLightAt(lm);
    float3 skyRaw  = llLightAt(float2(0.0, lm.y));
    float  dayLvl  = llLuma(llLightAt(float2(0.0, 1.0))) * 1.15;
    LLDir  dir     = llDir(FogColor.rgb, n, lm.y, dayLvl);

    float3 skyPart = skyRaw * llSkyShape(dir, n);
    float3 blk     = llBlock(lm.x, t);
    float3 light   = skyPart + blk * (1.0 - 0.6 * saturate(llLuma(skyPart)));
    light          = max(light, vanilla * 0.4);    // caves never go darker than the game would draw them
    c *= light;

#if LL_EXTREME
    // lamps, glowstone, lava, lanterns: bright texels in strong block light glow past the lightmap
    float e = smoothstep(0.62, 0.95, llLuma(albedo)) * smoothstep(0.72, 1.0, lm.x);
    c += albedo * e * 0.55;
    // ores and gems in the dark: saturated texels away from any light breathe faintly
    float chroma = llMax3(albedo) - min(albedo.r, min(albedo.g, albedo.b));
    float ore    = smoothstep(0.34, 0.60, chroma) * (1.0 - lm.y) * (1.0 - smoothstep(0.2, 0.6, lm.x));
    c += albedo * ore * (0.10 + 0.04 * sin(t * 1.7 + dot(floor(wp), float3(1.3, 2.1, 0.7))));
    // a soft sun sheen on faces turned toward the key light
    float3 R = reflect(-V, n);
    c += dir.sunCol * pow(saturate(dot(R, LL_KEY)), 14.0) * 0.07 * dir.sunVis * dir.day * llLuma(albedo);
#endif

#if defined(PASS_TRANSPARENT)
    // water: top faces with the blue-green water tint get the sky in them (FogColor = the sky's horizon)
    bool water = n.y > 0.9 && i.color0.b > i.color0.r + 0.06 && a < 0.99;
    if (water) {
        float ripple = llLuma(d.rgb) - 0.5;           // the animated water texture gives world-stable ripples
        float3 nw = normalize(float3(ripple * 0.22, 1.0, ripple * 0.14));
        float3 Rw = reflect(-V, nw);
        float  fres = 0.03 + 0.97 * pow(1.0 - saturate(dot(nw, V)), 5.0);
        float  lvl  = max(llLuma(FogColor.rgb), 0.04);
        float3 horizon = FogColor.rgb;
        float3 zenith  = dir.hue * lvl * float3(0.62, 0.74, 1.0);
        float3 skyRef  = lerp(horizon, zenith, saturate(Rw.y * 1.6)) * lerp(0.35, 1.0, lm.y);
#if LL_EXTREME
        c = lerp(c * float3(0.80, 0.97, 1.0), skyRef, fres * 0.9);
        float3 glint = dir.sunCol * pow(saturate(dot(Rw, LL_KEY)), 240.0) * 2.6 * dir.sunVis * dir.day;
        a = lerp(a, 1.0, fres * 0.65);
#else
        c = lerp(c, skyRef, fres * 0.6);
        float3 glint = dir.sunCol * pow(saturate(dot(Rw, LL_KEY)), 90.0) * 0.6 * dir.sunVis * dir.day;
        a = lerp(a, 1.0, fres * 0.4);
#endif
        c += glint;
        a  = max(a, saturate(llLuma(glint)));
    }
#endif

    c = llGrade(c);
    c = lerp(c, FogColor.rgb, i.color2.w);
    return float4(c, a);
}
