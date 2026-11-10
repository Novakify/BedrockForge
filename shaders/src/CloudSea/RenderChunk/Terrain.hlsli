// Cloud Sea terrain (RenderChunk): vanilla texture / tint / dither maths, Cloud Sea light.
// PASS_ALPHATEST / PASS_TRANSPARENT come from ShaderForge per pass.
// @uniform ViewPositionAndTime vec4
Texture2D    s_LightMapTextureTexture : register(t0);
SamplerState s_LightMapTextureSampler : register(s0);
Texture2D    s_MatTextureTexture      : register(t1);
SamplerState s_MatTextureSampler      : register(s1);
#if defined(FLAG_SEASONS_ON) && !defined(PASS_TRANSPARENT)
Texture2D    s_SeasonsTextureTexture  : register(t2);
SamplerState s_SeasonsTextureSampler  : register(s2);
#define CS_TINT(c) ((c).a * (((s_SeasonsTextureTexture.Sample(s_SeasonsTextureSampler, (c).xy).rgb * 2.0 - 1.0) * (c).b) + 1.0))
#define CS_SEASONS 1
#else
#define CS_TINT(c) ((c).rgb)
#define CS_SEASONS 0
#endif
#if (defined(PASS_ALPHATEST) || defined(PASS_TRANSPARENT)) && defined(FLAG_DITHERING_ON)
cbuffer FragmentUniforms : register(b1) {
    float4x4 u_view; float4 DitherParams2[3]; float4 DitherParams; float4 FogColor; float4 ViewPositionAndTime;
};
#else
cbuffer FragmentUniforms : register(b1) { float4 FogColor; float4 ViewPositionAndTime; };
#endif

float4 main(PSIn i) : SV_Target {
    float4 d = s_MatTextureTexture.Sample(s_MatTextureSampler, i.texcoord0);
#if defined(PASS_ALPHATEST)
    bool kill = d.a < 0.5;
#if defined(FLAG_DITHERING_ON)
    if (i.texcoord2.x > 0.5)
        kill = kill || bfDitherKill(i.color3.xyz, ViewPositionAndTime.xyz, u_view[2].xyz, i.color1, DitherParams2[2], DitherParams);
#endif
    if (kill) discard;
    float3 tint = CS_TINT(i.color0);
    float3 c = d.rgb * tint;
    float  a = d.a;
#else
    float3 tint = CS_TINT(i.color0);
    float3 c = d.rgb * tint;
#if defined(PASS_TRANSPARENT)
    float  a = d.a * i.color0.a;
#else
    float  a = i.color0.a;
#endif
    if (!CS_SEASONS && i.texcoord2.y > 0.5) { c = lerp(d.rgb, c, d.a) * i.color0.a; a = 1.0; }
#if defined(PASS_TRANSPARENT) && defined(FLAG_DITHERING_ON)
    if (i.texcoord2.x > 0.5 && bfDitherKill(i.color3.xyz, ViewPositionAndTime.xyz, u_view[2].xyz, i.color1, DitherParams2[2], DitherParams)) a = 0.0;
#endif
#endif
    float3 wp  = i.texcoord3;
    float3 cam = ViewPositionAndTime.xyz;
    float3 V   = normalize(cam - wp);
#if defined(PASS_OPAQUE)
    float3 n   = csNormal(wp, V, true);
#else
    float3 n   = csNormal(wp, V, false);
#endif
    float2 lm  = i.texcoord1;                      // x = block light, y = sky light
    // ---- which material is this? (World FX Materials pack: alpha = 255 - 4 * id, read exactly from mip 0)
    uint aw, ah;
    s_MatTextureTexture.GetDimensions(aw, ah);
    float2 tx   = i.texcoord0 * float2(aw, ah);
    int2   ti   = int2(floor(tx));
    int2   tile = (ti / 16) * 16;                  // vanilla tiles are 16x16 texels
    float4 t0   = s_MatTextureTexture.Load(int3(ti, 0));
#if defined(PASS_TRANSPARENT)
    int    mid  = 0;
#else
    int    mid  = int(round((1.0 - t0.a) * 255.0 / 4.0));
    if (mid > 19) mid = 0;
#endif
    CsMat  m    = csMat(mid);
    // texels get smaller than pixels far away: relief and glints fade out there instead of sparkling
    float  tpp  = max(fwidth(tx.x), fwidth(tx.y));
    float  fade = 1.0 - smoothstep(0.55, 1.25, tpp);
    // ---- relief: the texture's own light/dark pattern as a height map, one little facet per texel
    float3 nS = n;
    float  cav = 1.0;
    float3 glintN = n;
    [branch] if ((m.bump > 0.0 || m.sparkle > 0.0) && fade > 0.0) {
        int2 lo = tile, hi = tile + 15;
        float hC = csLuma(t0.rgb);
        float hL = csLuma(s_MatTextureTexture.Load(int3(clamp(ti - int2(1, 0), lo, hi), 0)).rgb);
        float hR = csLuma(s_MatTextureTexture.Load(int3(clamp(ti + int2(1, 0), lo, hi), 0)).rgb);
        float hU = csLuma(s_MatTextureTexture.Load(int3(clamp(ti - int2(0, 1), lo, hi), 0)).rgb);
        float hD = csLuma(s_MatTextureTexture.Load(int3(clamp(ti + int2(0, 1), lo, hi), 0)).rgb);
        // texture axes in the world: from derivatives, snapped to the block's axes (stable across a face)
        float3 dpx = ddx(wp), dpy = ddy(wp);
        float2 dtx = ddx(tx), dty = ddy(tx);
        float  det = dtx.x * dty.y - dtx.y * dty.x;
        float3 T = (dpx * dty.y - dpy * dtx.y) * (det != 0.0 ? 1.0 / det : 0.0);   // world per texel along u
        float3 B = (dpy * dtx.x - dpx * dty.x) * (det != 0.0 ? 1.0 / det : 0.0);   // world per texel along v
        T -= n * dot(T, n); B -= n * dot(B, n);
        float3 aT = abs(T), aB = abs(B);
        T = aT.x >= max(aT.y, aT.z) ? float3(sign(T.x), 0, 0) : (aT.y >= aT.z ? float3(0, sign(T.y), 0) : float3(0, 0, sign(T.z)));
        B = aB.x >= max(aB.y, aB.z) ? float3(sign(B.x), 0, 0) : (aB.y >= aB.z ? float3(0, sign(B.y), 0) : float3(0, 0, sign(B.z)));
        float2 g = float2(hR - hL, hD - hU) * 0.5;
        nS  = normalize(n - (g.x * T + g.y * B) * (3.2 * m.bump * fade));
        float mean4 = (hL + hR + hU + hD) * 0.25;
        cav = saturate(1.0 - (mean4 - hC) * 3.0);    // a texel darker than its neighbours sits in a dip
        float3 hh = csHash3(float2(ti));
        glintN = normalize(n + (hh - 0.5) * 0.9);
        if (m.sparkle > 0.0 && mid == MAT_ORE) {      // ores: only the bright, coloured texels are crystals
            float chroma = csMax3(t0.rgb) - min(t0.r, min(t0.g, t0.b));
            m.sparkle *= smoothstep(0.18, 0.35, chroma);
            if (chroma > 0.25) { m.spec = 0.6; m.rough = 90.0; }
        }
    }
    // calm the texture's pixel noise per material: pull toward the tile's average (its far mip)
#if defined(PASS_OPAQUE)
    float3 mean = s_MatTextureTexture.SampleLevel(s_MatTextureSampler, i.texcoord0, 3.0).rgb * tint;
    c = lerp(mean, c, lerp(0.78, m.detail, step(0.5, float(mid))));
#endif
    float3 alb = csLin(c / csVanillaShade(n));
#if defined(PASS_ALPHATEST)
    float leaf = (CS_SEASONS || csMax3(tint) - min(tint.r, min(tint.g, tint.b)) > 0.04) ? 1.0 : 0.0;
#else
    float leaf = CS_SEASONS ? 1.0 : 0.0;
#endif
    float sunVis = smoothstep(0.78, 0.97, lm.y);
    float3 lit = csLight(alb, nS, n, sunVis, lm.y, leaf, V, m, cav, glintN, fade);
    // torches and lamps: warm, soft
    float blk = pow(saturate(lm.x), 1.7);
    lit += alb * float3(1.0, 0.62, 0.32) * blk * 1.4 * (1.0 - 0.6 * saturate(lm.y));
#if defined(PASS_TRANSPARENT)
    // ---- glass, ice and water: they reflect the Cloud Sea (fresnel), and catch the sun
    {
        bool  water = n.y > 0.9 && i.color0.b > i.color0.r + 0.06 && a < 0.99;
        float3 nw = n;
        if (water) {
            float ripple = csLuma(d.rgb) - 0.5;      // the animated water texture gives world-stable ripples
            nw = normalize(float3(ripple * 0.22, 1.0, ripple * 0.14));
        }
        float3 R    = reflect(-V, nw);
        float  ndv  = saturate(dot(nw, V));
        float  F    = (water ? 0.02 : 0.05) + (water ? 0.98 : 0.95) * pow(1.0 - ndv, 5.0);
        float3 refl = csReflect(R) * lerp(0.35, 1.0, lm.y);
        float3 glint = float3(1.6, 1.15, 0.75) * pow(saturate(dot(R, CS_SUN)), water ? 240.0 : 400.0) * (water ? 2.2 : 3.0) * sunVis;
        float  k    = saturate(F * (water ? 0.85 : 0.7));
        // straight-alpha blend: put the reflection in without darkening what is behind the glass
        float  aOut = a + k * (1.0 - a);
        lit  = (lit * a * (1.0 - k) + refl * k) / max(aOut, 1e-3) + glint / max(aOut, 0.05);
        a    = saturate(max(aOut, csLuma(glint) * 0.8));
    }
#endif
    lit = csHaze(lit, wp, cam, i.color2.w);
    return float4(csEnc(lit), a);
}
