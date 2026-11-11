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

// value noise that repeats every `per` cells (so a field over chunk-local coordinates is seamless across chunks)
float csPVN(float2 p, float per) {
    float2 i0 = floor(p), f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float2 a = fmod(i0 + per * 64.0, per), b = fmod(i0 + 1.0 + per * 64.0, per);
    return lerp(lerp(csHash(a), csHash(float2(b.x, a.y)), f.x), lerp(csHash(float2(a.x, b.y)), csHash(b), f.x), f.y);
}
// water height: three octaves drifting with the wind; 16 blocks = 8, 20 and 40 cells (whole numbers: seamless)
float csRipple(float2 uv, float t) {
    return csPVN(uv * 0.5 + float2(t * 0.30, t * 0.18), 8.0) * 0.55
         + csPVN(uv * 1.25 - float2(t * 0.42, -t * 0.27), 20.0) * 0.30
         + csPVN(uv * 2.5 + float2(t * 0.65, t * 0.50), 40.0) * 0.15;
}

float4 main(PSIn i) : SV_Target {
    csBegin(csLuma(s_LightMapTextureTexture.SampleLevel(s_LightMapTextureSampler, float2(0.0, 1.0), 0.0).rgb), ViewPositionAndTime);
    float4 d = s_MatTextureTexture.Sample(s_MatTextureSampler, i.texcoord0);
#if defined(PASS_ALPHATEST)
    // cut-outs carrying a material id (leaves, plants, flowers, lamps: alpha 255 - 4 * id on their solid texels) keep
    // the same coverage as vanilla: the test is against half of that texel's own solid alpha
    float aId;
    {
        uint aw0, ah0;
        s_MatTextureTexture.GetDimensions(aw0, ah0);
        float a0 = s_MatTextureTexture.Load(int3(int2(floor(i.texcoord0 * float2(aw0, ah0))), 0)).a;
        aId = a0 > 0.5 ? a0 : 0.62;
    }
    bool kill = d.a < 0.5 * aId;
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
    if (mid > MAT_LAST) mid = 0;
#endif
    CsMat  m    = csMat(mid);
#if !defined(PASS_TRANSPARENT)
    // carved blocks: real depth. A short march into the face (the texel's darkness = how deep it is cut), pixel-art
    // exact (whole texels, no smear), up to ~2.5 texels of shift; the cut walls fall into shade. Near blocks only.
    float pxShade = 1.0;
    [branch] if (mid == MAT_CARVED) {
        float tppP = max(fwidth(tx.x), fwidth(tx.y));
        float near = 1.0 - smoothstep(0.35, 0.8, tppP);
        if (near > 0.0) {
            float3 nP = csNormal(i.texcoord3, normalize(ViewPositionAndTime.xyz - i.texcoord3), true);
            float3 dpx = ddx(i.texcoord3), dpy = ddy(i.texcoord3);
            float2 dtx = ddx(tx), dty = ddy(tx);
            float  det = dtx.x * dty.y - dtx.y * dty.x;
            float3 Tp = (dpx * dty.y - dpy * dtx.y) * (det != 0.0 ? 1.0 / det : 0.0);
            float3 Bp = (dpy * dtx.x - dpx * dty.x) * (det != 0.0 ? 1.0 / det : 0.0);
            float3 aT = abs(Tp), aB = abs(Bp);
            Tp = aT.x >= max(aT.y, aT.z) ? float3(sign(Tp.x), 0, 0) : (aT.y >= aT.z ? float3(0, sign(Tp.y), 0) : float3(0, 0, sign(Tp.z)));
            Bp = aB.x >= max(aB.y, aB.z) ? float3(sign(Bp.x), 0, 0) : (aB.y >= aB.z ? float3(0, sign(Bp.y), 0) : float3(0, 0, sign(Bp.z)));
            float3 Vw = normalize(ViewPositionAndTime.xyz - i.texcoord3);
            float3 vt = float3(dot(Vw, Tp), dot(Vw, Bp), max(dot(Vw, nP), 0.25));
            float2 stepT = -vt.xy / vt.z * (2.6 / 6.0) * near;          // texels per layer
            float2 q = float2(ti - tile) + 0.5;
            float  layer = 0.0, hgt = 0.0;
            [unroll] for (int k = 0; k < 6; k++) {
                int2 tq = tile + int2(clamp(floor(q), 0.0, 31.0));
                hgt = 1.0 - csLuma(s_MatTextureTexture.Load(int3(tq, 0)).rgb) * 1.25;   // dark = deep
                if (layer >= hgt) break;
                q += stepT; layer += 1.0 / 6.0;
            }
            int2 tn = tile + int2(clamp(floor(q), 0.0, 31.0));
            if (any(tn != ti)) {
                float4 tN = s_MatTextureTexture.Load(int3(tn, 0));
                c = tN.rgb * tint;
                ti = tn; t0 = float4(tN.rgb, t0.a);
            }
            pxShade = lerp(1.0, 0.72 + 0.28 * saturate(1.0 - layer * 1.4), near);   // the deeper it is cut, the less light
        }
    }
#endif
    // obsidian: the polished streaks catch the sky, the dark body stays dark (the void lives there) - one even mirror
    // over the whole face read as a muddy haze in game
    if (mid == MAT_OBSIDIAN) m.refl *= 0.25 + 1.3 * smoothstep(0.08, 0.22, csLuma(t0.rgb));
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
#if !defined(PASS_TRANSPARENT)
    c *= pxShade;
#endif
    float3 alb = csLin(c / csVanillaShade(n));
    if (mid == MAT_OBSIDIAN) alb *= 0.55;           // volcanic glass: dark in any light, its reflections and the void carry it
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
    // ---- living blocks: the wind, light that breathes, depth (a per-block id and the gust come from the vertex
    //      shader in color2: x = gust 0..1, yz = chunk-local u, v - see _shared/RenderChunkWave.vertex.hlsli)
    float  gust = saturate(i.color2.x);
    float2 bcell = floor(i.color2.yz - float2(n.x, n.z + 16.0 * n.y) * 0.5);   // this block, inside its subchunk
    float  bh = csHash(bcell * float2(1.31, 0.73) + 11.0);
    float  skyK = saturate(lm.y);
#if !defined(PASS_TRANSPARENT)
    // The Hive's own blocks (hive:red_oak_leaves, hive:small_dried_grass...) carry no material id (their art is the
    // server's): an untagged cut-out with a natural colour outdoors is taken for foliage
    float folk = 0.0;
#if defined(PASS_ALPHATEST)
    if (mid == 0) {
        float mx = csMax3(c), mn = min(c.r, min(c.g, c.b));
        folk = (mx - mn) / max(mx, 1e-3) > 0.22 && mx > 0.08 && skyK > 0.6 ? 1.0 : 0.0;
    }
#endif
    [branch] if (mid == MAT_LEAVES || mid == MAT_GRASS || mid == MAT_PLANT || (mid == 0 && (leaf > 0.5 || folk > 0.5))) {
        // a gust flips leaves and blades over: their pale undersides show, the wind visibly rolls across the island
        // (seen in game: a grey wash read as dead leaves - the gust is a lift of light, a touch toward sage, colour kept)
        float k = gust * skyK * skyK * (mid == MAT_GRASS ? 0.55 : 0.75);
        lit *= 1.0 + 0.22 * k;
        lit = lerp(lit, lit * float3(1.02, 1.04, 0.96) + csLuma(lit) * float3(0.03, 0.05, 0.04), k * 0.5);
        // single leaves catch the sun as they turn (each block, each texel its own rhythm; more inside a gust)
        [branch] if (mid != MAT_GRASS && fade > 0.0) {
            float h  = csHash(float2(ti) * 0.917 + bcell * 7.31);
            // (seen in game: 7% of texels read as gold noise, also inside the canopy - now a few, on the sunny outside)
            float on = step(0.975, h) * smoothstep(0.15, 0.45, dot(n, CS_SUN)) * smoothstep(0.92, 1.0, skyK);
            float fl = pow(saturate(sin(CS_TIME * (1.4 + 2.0 * frac(h * 13.7)) + h * 40.0)), 18.0) * (0.25 + 1.0 * gust);
            lit += CS_SUNC * float3(1.0, 0.92, 0.66) * 0.40 * fl * on * sunVis * fade * (1.0 - CS_NIGHT);
        }
        // grass: dew. A few blades hold a drop that winks as you move (cool white, more after the sun goes)
        [branch] if (mid == MAT_GRASS && n.y > 0.5 && fade > 0.0) {
            float h  = csHash(float2(ti) * 1.37 + bcell * 3.17);
            float on = step(0.982, h);
            float tw = pow(saturate(0.5 + 0.5 * sin(dot(V, float3(23.0, 17.0, 29.0)) + h * 90.0 + CS_TIME * 0.6)), 24.0);
            lit += lerp(float3(1.0, 0.95, 0.85), float3(0.75, 0.85, 1.15), CS_NIGHT) * on * tw * fade * skyK * lerp(0.35, 1.0, max(CS_DUSK, CS_NIGHT));
        }
    }
    [branch] if (mid == MAT_FLOWER) {
        // petals (the texels that aren't stem green) hold the light: backlit by day, a soft glow of their own after dusk
        float pet = smoothstep(0.04, 0.16, max(alb.r, alb.b) - alb.g) + smoothstep(0.55, 0.75, csLuma(alb)) * 0.6;
        pet = saturate(pet);
        float breath = 0.80 + 0.20 * sin(CS_TIME * 1.3 + bh * 6.28);
        lit += alb * CS_SUNC * pow(saturate(dot(-V, CS_SUN)), 4.0) * 0.9 * pet * sunVis;
        lit += alb * float3(1.0, 0.92, 1.05) * (0.05 + 0.55 * max(CS_DUSK, CS_NIGHT)) * pet * breath * lerp(0.5, 1.0, skyK);
    }
    [branch] if (mid == MAT_LAMP) {
        // lights breathe: a slow swell, and a flame's quick flutter on top (each lamp its own)
        float bright = smoothstep(0.30, 0.65, csLuma(c));
        float breath = 0.93 + 0.07 * sin(CS_TIME * 1.1 + bh * 6.28) + 0.06 * (csVN(float2(CS_TIME * 9.0, bh * 50.0)) - 0.5);
        lit = lit * (1.0 + 0.30 * bright * breath) + alb * float3(1.0, 0.80, 0.55) * 0.22 * bright * breath;
    }
    [branch] if (mid == MAT_SNOW && fade > 0.0) {
        // diamond dust: a few crystals flash when YOUR eye lines up with them (not only the sun's), each its own colour
        float3 hh = csHash3(float2(ti) * 1.11 + bcell * 5.7);
        float on = step(0.955, hh.x);
        float3 dR = normalize(hh - 0.5 + n * 1.1);
        float g2 = pow(saturate(dot(reflect(-V, n), dR)), 500.0);
        float3 fire = saturate(abs(frac(hh.y + float3(0.0, 0.333, 0.667)) * 6.0 - 3.0) - 1.0);
        lit += (0.55 + 0.9 * fire) * g2 * on * fade * 1.6 * lerp(0.35, 1.0, skyK);
        // snow in shade goes blue (the sky's light), in the sun it stays warm
        float sunlit = sunVis * saturate(dot(n, CS_SUN) * 5.0);
        lit = lerp(lit, lit * float3(0.84, 0.95, 1.22), (1.0 - sunlit) * 0.45 * (1.0 - CS_NIGHT));
    }
    [branch] if (mid == MAT_ICE && fade > 0.0 && dot(T, T) > 0.5) {
        // looking INTO ice: its own pattern twice more, deeper down, cold and dim, with trapped bubbles
        float2 f  = float2(ti - tile) / 32.0;
        float3 vt = float3(dot(V, T), dot(V, B), dot(V, n));
        float  inv = 1.0 / max(vt.z, 0.25);
        float3 deep = float3(0, 0, 0);
        [unroll] for (int L = 1; L <= 2; L++) {
            float2 q = frac(f - vt.xy * inv * (0.28 * float(L)));
            float3 tc = s_MatTextureTexture.Load(int3(tile + int2(q * 32.0), 0)).rgb;
            deep += csLin(tc) * float3(0.45, 0.78, 1.10) * (L == 1 ? 0.55 : 0.35);
            float hb = csHash(floor(q * 32.0) + 7.0 * float(L));
            deep += float3(0.75, 0.90, 1.0) * step(0.992, hb) * 0.9 * (1.0 / float(L));
        }
        float3 amb = lerp(float3(0.20, 0.19, 0.33), CS_SUNC * 0.35, 0.4) * lerp(0.15, 1.0, skyK);
        float  F = pow(1.0 - saturate(vt.z), 3.0);
        lit = lit * (0.62 + 0.38 * F) + deep * amb * 1.6 * (1.0 - F);
    }
#endif
#if !defined(PASS_TRANSPARENT)
    // obsidian: the void inside (strongest at night, never on the surface streaks)
    // obsidian's dark body under the golden light read grey-brown (dark x warm = mud): the light on it goes cold and
    // glassy, the polished streaks keep the sun's colour
    if (mid == MAT_OBSIDIAN) {
        float body0 = 1.0 - smoothstep(0.06, 0.16, csLuma(t0.rgb));
        lit = lerp(lit, csLuma(lit) * float3(0.70, 0.62, 1.10), 0.75 * body0);
    }
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
            // ripples: wind-driven little waves (three octaves, seamless across chunks), the sun breaks into a glittering
            // path on them
            float2 p0 = i.color2.yz;
            float  e = 0.06;
            float  h0 = csRipple(p0, CS_TIME), hx = csRipple(p0 + float2(e, 0), CS_TIME), hz = csRipple(p0 + float2(0, e), CS_TIME);
            float  amp = 0.55 * lerp(0.6, 1.0, gust);
            nw = normalize(float3(-(hx - h0) / e * amp, 1.0, -(hz - h0) / e * amp));
        }
        float3 R    = reflect(-V, nw);
        float  ndv  = saturate(dot(nw, V));
        float  F    = (water ? 0.02 : 0.05) + (water ? 0.98 : 0.95) * pow(1.0 - ndv, 5.0);
        float3 refl = csReflect(R) * lerp(0.35, 1.0, lm.y);
        float3 glint = float3(1.6, 1.15, 0.75) * pow(saturate(dot(R, CS_SUN)), water ? 600.0 : 400.0) * (water ? 3.0 : 3.0) * sunVis;
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
