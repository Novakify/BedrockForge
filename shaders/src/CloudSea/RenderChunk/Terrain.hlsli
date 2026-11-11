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

// ---- living blocks ----------------------------------------------------------------------------------------------
// Obsidian is a window into the void: three layers of pixel stars deep inside the block slide with parallax as you
// move and twinkle; a faint violet nebula drifts behind them. The texture's bright streaks stay on top (the polished
// surface), the stars live in its dark body. Pixel-sized like the texture, so it reads as part of the art.
float3 csVoid(float2 f, float3 vt, float t) {   // f: 0..1 across the face, vt: the eye in face space (z out of the face)
    float3 acc = float3(0, 0, 0);
    float  inv = 1.0 / max(vt.z, 0.22);
    [unroll] for (int L = 0; L < 3; L++) {
        float  depth = 0.30 + 0.45 * float(L);
        float2 q  = f - vt.xy * inv * depth + float2(0.37, 0.71) * float(L);
        float2 cell = floor(q * 32.0);                                 // one texel of the 32x pack
        float  h  = csHash(cell + 17.0 * float(L));
        float  on = step(0.9935 - 0.002 * float(L), h);                 // a few, not glitter
        float  tw = 0.55 + 0.45 * sin(t * (0.9 + 2.3 * frac(h * 91.7)) + h * 60.0);
        float3 sc = lerp(float3(0.62, 0.45, 1.00), float3(1.00, 0.92, 0.95), frac(h * 13.1));
        acc += sc * on * tw * (1.0 - 0.30 * float(L)) * 1.1;
    }
    float2 qn = f - vt.xy * inv * 1.6;
    float  neb = csVN(qn * 3.0 + t * float2(0.015, 0.006)) * 0.6 + csVN(qn * 7.0 - t * 0.01) * 0.4;
    acc += float3(0.16, 0.04, 0.28) * smoothstep(0.50, 0.90, neb) * 0.35;
    return acc;
}

float4 main(PSIn i) : SV_Target {
    csBegin(csLuma(s_LightMapTextureTexture.SampleLevel(s_LightMapTextureSampler, float2(0.0, 1.0), 0.0).rgb), ViewPositionAndTime);
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
    int2   tile = (ti / 32) * 32;                  // the pack's tiles are 32x32 texels (Faithful 32x, flipbook frames too)
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
    float3 T = float3(0, 0, 0), B = float3(0, 0, 0);
    [branch] if ((m.bump > 0.0 || m.sparkle > 0.0) && fade > 0.0) {
        int2 lo = tile, hi = tile + 31;
        float hC = csLuma(t0.rgb);
        float hL = csLuma(s_MatTextureTexture.Load(int3(clamp(ti - int2(1, 0), lo, hi), 0)).rgb);
        float hR = csLuma(s_MatTextureTexture.Load(int3(clamp(ti + int2(1, 0), lo, hi), 0)).rgb);
        float hU = csLuma(s_MatTextureTexture.Load(int3(clamp(ti - int2(0, 1), lo, hi), 0)).rgb);
        float hD = csLuma(s_MatTextureTexture.Load(int3(clamp(ti + int2(0, 1), lo, hi), 0)).rgb);
        // texture axes in the world: from derivatives, snapped to the block's axes (stable across a face)
        float3 dpx = ddx(wp), dpy = ddy(wp);
        float2 dtx = ddx(tx), dty = ddy(tx);
        float  det = dtx.x * dty.y - dtx.y * dty.x;
        T = (dpx * dty.y - dpy * dtx.y) * (det != 0.0 ? 1.0 / det : 0.0);   // world per texel along u
        B = (dpy * dtx.x - dpx * dty.x) * (det != 0.0 ? 1.0 / det : 0.0);   // world per texel along v
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
    // torches and lamps: warm, soft, and alive (a slow flame breathing, each light its own)
    float blk = pow(saturate(lm.x), 1.7);
    float flick = 0.90 + 0.10 * csVN(float2(CS_TIME * 2.3, dot(floor(wp * 0.25), float3(1.7, 9.1, 4.3))));
    lit += alb * float3(1.0, 0.62, 0.32) * blk * 1.4 * flick * (1.0 - 0.6 * saturate(lm.y));
#if !defined(PASS_TRANSPARENT)
    // obsidian: the void inside (strongest at night, never on the surface streaks)
    [branch] if (mid == MAT_OBSIDIAN && fade > 0.0 && dot(T, T) > 0.5) {
        float2 f  = float2(ti - tile) / 32.0;
        float3 vt = float3(dot(V, T), dot(V, B), dot(V, n));
        float  body = 1.0 - smoothstep(0.06, 0.14, csLuma(t0.rgb));
        lit += csVoid(f, vt, CS_TIME) * body * fade * lerp(0.6, 1.0, max(CS_DUSK, CS_NIGHT)) * lerp(0.6, 1.0, lm.y);
    }
    // end stone: a few of its flecks are stars - they twinkle, more as the sky darkens
    [branch] if (mid == MAT_ENDSTONE && fade > 0.0) {
        float h = csHash(float2(ti) + 3.7);
        float on = step(0.975, h) * smoothstep(0.55, 0.75, csLuma(t0.rgb));
        float tw = pow(0.5 + 0.5 * sin(CS_TIME * (0.7 + 1.6 * frac(h * 37.0)) + h * 80.0), 6.0);
        lit += lerp(float3(1.0, 0.92, 0.70), float3(0.80, 0.86, 1.15), CS_NIGHT) * on * tw * fade * lerp(0.35, 1.1, max(CS_DUSK, CS_NIGHT)) * lm.y;
    }
#endif
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
#if defined(PASS_TRANSPARENT)
    lit = csHaze(lit, wp, cam, i.color2.w);
    return float4(csEnc(lit), a);
#else
    // the game's fog is NOT painted here: its amount goes out in alpha (opaque blocks ignore alpha) and the World FX
    // pass fades the block into whatever the Cloud Sea sky shows behind it (clouds or sky), so the render edge is soft
    lit = csHaze(lit, wp, cam, 0.0);
    return float4(csEnc(lit), 1.0 - i.color2.w);
#endif
}
