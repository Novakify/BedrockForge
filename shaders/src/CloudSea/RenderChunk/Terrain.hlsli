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
#if defined(CS_NOVAK)
    // the tile's real size: 1 / the world size of one texel (16 or 32). (Seen in game: the atlas here holds 16x tiles;
    // taking every tile as 32 put half the joints mid-"tile", so relief read across into the NEXT block's texture and
    // one side of those joints fell into a dark strip.)
    float3 dpT = ddx(i.texcoord3), dqT = ddy(i.texcoord3);
    float2 dtT = ddx(tx), duT = ddy(tx);
    float  dtD = dtT.x * duT.y - dtT.y * duT.x;
    float3 TT  = (dpT * duT.y - dqT * dtT.y) * (dtD != 0.0 ? 1.0 / dtD : 0.0);
    int    TS  = 1.0 / max(length(TT), 1e-4) > 24.0 ? 32 : 16;
#else
    const int TS = 32;
#endif
    int2   tile = (ti / TS) * TS;                  // the pack's tiles (Faithful 32x, flipbook frames too)
    float4 t0   = s_MatTextureTexture.Load(int3(ti, 0));
#if defined(PASS_TRANSPARENT)
    int    mid  = 0;
#else
    int    mid  = int(round((1.0 - t0.a) * 255.0 / 4.0));
    if (mid > MAT_LAST) mid = 0;
#endif
    CsMat  m    = csMat(mid);
#if defined(CS_NOVAK)
    // Novak Glass: the sun stands higher (26 degrees, not 6.5), so the relief read from the texture's light/dark pattern
    // lights up on floors - a pack's darker border row turned into a dark strip beside every joint. Blocks sealed in
    // glass are smoother anyway: a third of the relief.
    m.bump *= 0.35;
#endif
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
            // (seen in game: marching off the tile smeared its border ring into glowing seams between blocks - the
            //  march now fades out within 2 texels of the block's edge and never leaves the tile)
            float2 e2 = min(q, float(TS) - q);
            stepT *= saturate((min(e2.x, e2.y) - 1.0) / 2.0);
            float  layer = 0.0, hgt = 0.0;
            [unroll] for (int k = 0; k < 6; k++) {
                int2 tq = tile + int2(clamp(floor(q), 0.0, float(TS - 1)));
                hgt = 1.0 - csLuma(s_MatTextureTexture.Load(int3(tq, 0)).rgb) * 1.25;   // dark = deep
                if (layer >= hgt) break;
                float2 qn = q + stepT;
                if (any(qn < 0.5) || any(qn > float(TS) - 0.5)) break;
                q = qn; layer += 1.0 / 6.0;
            }
            int2 tn = tile + int2(clamp(floor(q), 0.0, float(TS - 1)));
            if (any(tn != ti)) {
                float4 tN = s_MatTextureTexture.Load(int3(tn, 0));
                c = tN.rgb * tint;
                ti = tn; t0 = float4(tN.rgb, t0.a);
            }
            pxShade = lerp(1.0, 0.72 + 0.28 * saturate(1.0 - layer * 1.4), near);   // the deeper it is cut, the less light
        }
    }
#endif
#if defined(CS_NOVAK) && defined(PASS_OPAQUE)
    // ---- Novak Glass, ordinary blocks: the block is a solid core sealed inside a clear glass cube. The core sits 1/16
    //      in from every edge and 2/16 behind the face, so as you move it slides behind the shell (whole texels: it
    //      stays pixel art). Red, green and blue bend by different amounts, so the core's edge splits into colour on
    //      the side you look past. Works on any pack (the tile size comes from the texel's size in the world).
    float3 ngIn = float3(1, 1, 1);   // per channel: 1 = the core shows here, 0 = the glass shell
    float  ngOn = 0.0, ngLine = 0.0, ngDbg = 0.0;
    [branch] if (mid != MAT_GEM && mid != MAT_QUARTZ && mid != MAT_ICE && mid != MAT_LAMP && mid != MAT_CARVED) {
        float  tppN = max(fwidth(tx.x), fwidth(tx.y));
        float  nearN = 1.0 - smoothstep(0.45, 1.0, tppN);
        float3 VwN = normalize(ViewPositionAndTime.xyz - i.texcoord3);
        float3 nN = csNormal(i.texcoord3, VwN, true);
        [branch] if (nearN > 0.0 && csMax3(abs(nN)) > 0.98) {
            float3 dpx = ddx(i.texcoord3), dpy = ddy(i.texcoord3);
            float2 dtx = ddx(tx), dty = ddy(tx);
            float  det = dtx.x * dty.y - dtx.y * dty.x;
            float3 Tn = (dpx * dty.y - dpy * dtx.y) * (det != 0.0 ? 1.0 / det : 0.0);
            float3 Bn = (dpy * dtx.x - dpx * dty.x) * (det != 0.0 ? 1.0 / det : 0.0);
            float  S  = exp2(round(log2(clamp(1.0 / max(length(Tn), 1e-4), 8.0, 128.0))));
            float3 aT = abs(Tn), aB = abs(Bn);
            Tn = aT.x >= max(aT.y, aT.z) ? float3(sign(Tn.x), 0, 0) : (aT.y >= aT.z ? float3(0, sign(Tn.y), 0) : float3(0, 0, sign(Tn.z)));
            Bn = aB.x >= max(aB.y, aB.z) ? float3(sign(Bn.x), 0, 0) : (aB.y >= aB.z ? float3(0, sign(Bn.y), 0) : float3(0, 0, sign(Bn.z)));
            float3 vt = float3(dot(VwN, Tn), dot(VwN, Bn), dot(VwN, nN));
            float2 org = floor(tx / S) * S;
            float2 q = tx - org;                                   // texels inside the tile
            float  u = S / 16.0;                                   // 1/16 of a block, in texels
            // how far the view slides over the core: the ray bends into the glass (Snell), so even at a grazing look it
            // travels at most ~0.9 of the depth sideways. (Seen in game: with no bend the far side of every block grew
            // a band a third of a block wide.) Each colour bends a little differently: the fringe.
            float  sinV = min(length(vt.xy), 0.999);
            float2 dirV = -vt.xy / max(sinV, 1e-4);
            float3 sinG = sinV / float3(1.36, 1.50, 1.68);
            float3 tanG = sinG / sqrt(1.0 - sinG * sinG) * (1.3 * u) * nearN;   // the core sits 1.3/16 behind the face
            float2 qR = floor(q + dirV * tanG.r) + 0.5, qG = floor(q + dirV * tanG.g) + 0.5, qB = floor(q + dirV * tanG.b) + 0.5;
            // the joint between two blocks is 2 texels on a 32x pack, 1 on a 16x one (a whole texel is the least there
            // is: there the core is set in on its low sides only). Seen in game: a 1/8-block joint read as bathroom grout.
            float  wLo = max(1.0, S / 32.0), wHi = S >= 32.0 ? S / 32.0 : 0.0;
            float inG = all(qG > wLo) && all(qG < S - wHi) ? 1.0 : 0.0;
            ngIn = float3(all(qR > wLo) && all(qR < S - wHi) ? 1.0 : 0.0, inG, all(qB > wLo) && all(qB < S - wHi) ? 1.0 : 0.0);
            ngLine = 1.0 - ngIn.g;                                  // the joint itself carries the light
            float2 qc = clamp(qG, wLo + 0.5, S - wHi - 0.5);       // shell pixels see the core's side wall: its edge texel
            int2 tn = int2(org) + int2(floor(qc));
            if (inG < 0.5) {
                // a shell pixel sees the core's side wall. Not the core's edge texel repeated across the gap (a pack's
                // darker border row smeared into a dark strip beside every joint): the block's own average colour.
                c = s_MatTextureTexture.SampleLevel(s_MatTextureSampler, (float2(org) + S * 0.5) / float2(aw, ah), 4.0).rgb * tint;
            } else if (any(tn != ti)) {
                float4 tN = s_MatTextureTexture.Load(int3(tn, 0));
                c = tN.rgb * tint;
                ti = tn; t0 = float4(tN.rgb, t0.a);
                ngDbg = 1.0;
            }
            ngOn = nearN;
            // (no shade on the joint: seen in game, any darkening there read as a dirty band beside every block)
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
        int2 lo = tile, hi = tile + TS - 1;
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
#if defined(CS_NOVAK)
        // no relief on a tile's outer 3 texels: the pack's darker border row read as a groove, and under the higher sun
        // one side of every joint fell into a dark strip (seen in game on end stone floors)
        { int2 ql = ti - tile; int e2 = min(min(ql.x, ql.y), min(TS - 1 - ql.x, TS - 1 - ql.y)); if (e2 < 3) g = 0.0; }
#endif
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
#if defined(CS_NOVAK)
    // the far mip is sampled inside the texel's own tile: at a tile's edge it blended in the NEXT tile of the atlas
    float2 meanUV = clamp(tx, float2(tile) + 4.0, float2(tile + TS) - 4.0) / float2(aw, ah);
#else
    float2 meanUV = i.texcoord0;
#endif
    // calm the texture's pixel noise per material: pull toward the tile's average (its far mip)
#if defined(PASS_OPAQUE)
    float3 mean = s_MatTextureTexture.SampleLevel(s_MatTextureSampler, meanUV, 3.0).rgb * tint;
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
        float2 f  = float2(ti - tile) / float(TS);
        float3 vt = float3(dot(V, T), dot(V, B), dot(V, n));
        float  inv = 1.0 / max(vt.z, 0.25);
        float3 deep = float3(0, 0, 0);
        [unroll] for (int L = 1; L <= 2; L++) {
            float2 q = frac(f - vt.xy * inv * (0.28 * float(L)));
            float3 tc = s_MatTextureTexture.Load(int3(tile + int2(q * float(TS)), 0)).rgb;
            deep += csLin(tc) * float3(0.45, 0.78, 1.10) * (L == 1 ? 0.55 : 0.35);
            float hb = csHash(floor(q * float(TS)) + 7.0 * float(L));
            deep += float3(0.75, 0.90, 1.0) * step(0.992, hb) * 0.9 * (1.0 / float(L));
        }
        float3 amb = lerp(float3(0.20, 0.19, 0.33), CS_SUNC * 0.35, 0.4) * lerp(0.15, 1.0, skyK);
        float  F = pow(1.0 - saturate(vt.z), 3.0);
        lit = lit * (0.62 + 0.38 * F) + deep * amb * 1.6 * (1.0 - F);
    }
#endif
#if defined(CS_NOVAK) && defined(PASS_OPAQUE)
    [branch] if (ngOn > 0.0) {
        float3 shell = (1.0 - ngIn) * ngOn;                        // per channel: where they disagree is the colour fringe
        float3 ambG = lerp(float3(0.20, 0.19, 0.33), CS_SUNC * 0.35, 0.4) * lerp(0.15, 1.0, skyK);
        // the glass round the core: each material has its own - wool is frosted (milky, soft), wood is amber, the rest
        // clear and cold
        bool   wood = mid == MAT_PLANKS || mid == MAT_LOG;
        float  clar = mid == MAT_WOOL ? 0.62 : (wood ? 0.46 : 0.50);
        float3 gt = wood ? float3(1.0, 0.78, 0.48) : (mid == MAT_WOOL ? float3(1.0, 0.98, 0.96) : float3(0.88, 0.95, 1.0));
        float3 glassC = lit * 1.03 + gt * (ambG * (mid == MAT_WOOL ? 0.70 : 0.50) + CS_SUNC * 0.05 * sunVis);   // glass is a little lighter than what it holds
        lit = lerp(lit, glassC, shell * clar);
        // the inner corner, where the shell meets the core: it catches the light by day and holds its own at night
        float3 hueG = alb / max(csMax3(alb), 1e-3);
        lit += ngLine * ngOn * lerp(float3(1, 1, 1), hueG, 0.35) * (ambG * 0.10 * (1.0 - CS_NIGHT) + 0.055 * CS_NIGHT);
        // the glass face over the whole block: the sky lies in it at grazing angles
        float  Fg = 0.02 + 0.98 * pow(1.0 - saturate(dot(n, V)), 5.0);
        float  kG = saturate(Fg * 0.60) * ngOn;
        lit = lit * (1.0 - kG) + csReflect(reflect(-V, n)) * lerp(0.30, 1.0, skyK) * kG;
    }
#endif
#if defined(CS_NOVAK) && !defined(PASS_TRANSPARENT)
    // ---- Novak Glass, real glass: gem blocks, ice and quartz are cast glass. You look INTO the block: the far inner
    //      walls and their edges show through the face and slide as you move, the sky comes in through the side walls
    //      tinted by the glass it crossed, and the inner edges split into colour (three bends, one per channel).
    [branch] if ((mid == MAT_GEM || mid == MAT_QUARTZ || mid == MAT_ICE) && fade > 0.0 && dot(T, T) > 0.5) {
        // face coordinates from the tile's own texels (the world position here is camera-relative: no block grid in it)
        float2 f  = (tx - float2(tile)) / float(TS);
        float3 vt = float3(dot(V, T), dot(V, B), dot(V, n));
        float  trans = mid == MAT_GEM ? 0.55 : (mid == MAT_ICE ? 0.40 : 0.22);
        float3 bodyC = csLin(s_MatTextureTexture.SampleLevel(s_MatTextureSampler, meanUV, 3.0).rgb * tint);
        float3 hue = bodyC / max(csMax3(bodyC), 1e-3);
        float4 ex = ngExit(f, vt, 0.66);
        float3 dw = normalize(-(vt.x * T + vt.y * B) * 0.66 - n * max(vt.z, 0.05));
        float3 ambN = lerp(float3(0.20, 0.19, 0.33), CS_SUNC * 0.35, 0.4) * lerp(0.15, 1.0, skyK);
        float  cellv = 0.86 + 0.28 * csHash(floor(ex.xy * 16.0) + bcell * 3.1 + 5.0);
        // the far wall: the glass's own colour, deeper the more glass the light crossed; a side wall lets some sky in
        float3 absorb = pow(hue, 0.8 + ex.w * (mid == MAT_QUARTZ ? 0.8 : 2.4));
        float3 back = bodyC * absorb * ambN * 2.6 * cellv;
        float3 through = ex.z < 0.995 ? lerp(back, csReflect(dw) * absorb * lerp(0.25, 1.0, skyK), 0.42) : back;
        float3 lines = float3(ngInnerEdge(ngExit(f, vt, 0.63).xyz), ngInnerEdge(ex.xyz), ngInnerEdge(ngExit(f, vt, 0.69).xyz));
        float3 inner = through + lines * lerp(float3(1, 1, 1), hue, 0.25) * (0.10 + 0.50 * skyK * (1.0 - 0.5 * CS_NIGHT) + 0.16 * CS_NIGHT);
        lit = lerp(lit, inner, trans * fade);
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
        float2 f  = float2(ti - tile) / float(TS);
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
#if defined(CS_NOVAK)
        glint *= lerp(1.0, 0.25, CS_NIGHT);   // the moon in glass: a glint, not a white square
#endif
        float  k    = saturate(F * (water ? 0.85 : 0.7));
        // straight-alpha blend: put the reflection in without darkening what is behind the glass
        float  aOut = a + k * (1.0 - a);
        lit  = (lit * a * (1.0 - k) + refl * k) / max(aOut, 1e-3) + glint / max(aOut, 0.05);
        a    = saturate(max(aOut, csLuma(glint) * 0.8));
#if defined(CS_NOVAK)
        // Novak Glass: a glass block is a solid cube, not a film - its far inner edges show through the face (colour
        // split), on full-block faces only (panes and slabs' cut faces are skipped)
        [branch] if (!water && csMax3(abs(n)) > 0.98 && fade > 0.0) {
            // texture axes in the world (from derivatives) and the tile size (1 / world size of a texel: 16x or 32x packs)
            float3 dpx = ddx(wp), dpy = ddy(wp);
            float2 dtx = ddx(tx), dty = ddy(tx);
            float  det = dtx.x * dty.y - dtx.y * dty.x;
            float3 Tg = (dpx * dty.y - dpy * dtx.y) * (det != 0.0 ? 1.0 / det : 0.0);
            float3 Bg = (dpy * dtx.x - dpx * dty.x) * (det != 0.0 ? 1.0 / det : 0.0);
            float  S  = exp2(round(log2(clamp(1.0 / max(length(Tg), 1e-4), 8.0, 128.0))));
            float3 aT = abs(Tg), aB = abs(Bg);
            Tg = aT.x >= max(aT.y, aT.z) ? float3(sign(Tg.x), 0, 0) : (aT.y >= aT.z ? float3(0, sign(Tg.y), 0) : float3(0, 0, sign(Tg.z)));
            Bg = aB.x >= max(aB.y, aB.z) ? float3(sign(Bg.x), 0, 0) : (aB.y >= aB.z ? float3(0, sign(Bg.y), 0) : float3(0, 0, sign(Bg.z)));
            float2 f  = frac(tx / S);
            float3 vt = float3(dot(V, Tg), dot(V, Bg), abs(dot(V, n)));
            float3 gl = csLin(c);
            float3 hue = gl / max(csMax3(gl), 1e-3);
            float3 lines = float3(ngInnerEdge(ngExit(f, vt, 0.63).xyz), ngInnerEdge(ngExit(f, vt, 0.66).xyz), ngInnerEdge(ngExit(f, vt, 0.69).xyz));
            float3 Lc = lines * lerp(float3(1, 1, 1), hue, 0.4) * (0.06 + 0.42 * skyK * (1.0 - 0.5 * CS_NIGHT) + 0.14 * CS_NIGHT) * fade;
            float  a2 = saturate(max(a, csLuma(Lc)));
            lit = (lit * a + Lc) / max(a2, 0.05);
            a = a2;
        }
#endif
    }
#endif
#if defined(PASS_TRANSPARENT)
    lit = csHaze(lit, wp, cam, i.color2.w);
    return float4(csEnc(lit), a);
#else
#if defined(CS_NOVAK)
    // ---- Novak Glass at night: the islands become part of the neon city around them (Fernando, Nov 2026). As the
    //      city lights up (the same curve as its buildings) ordinary blocks go to cool steel and indigo, their sides
    //      catch two-tone street light - magenta from one side of the map, cyan from the other - and their tops go
    //      rain-slick, mirroring the city glow. Team colours (wool, concrete, terracotta, glazed) keep their colour
    //      exactly: you must always read the teams. Lights, flowers and glass keep theirs too.
    {
        float NK = smoothstep(0.45, 1.20, CS_PHASE) * lerp(0.35, 1.0, skyK);
        bool  team = mid == MAT_WOOL || mid == MAT_CONCRETE || mid == MAT_POWDER || mid == MAT_TERRACOTTA || mid == MAT_GLAZED;
        bool  keep = team || mid == MAT_LAMP || mid == MAT_FLOWER || mid == MAT_GEM;
        [branch] if (NK > 0.001) {
            float  L = csLuma(lit);
            if (!keep) lit = lerp(lit, L * float3(0.62, 0.68, 1.10) + lit * 0.25, 0.70 * NK);   // steel / indigo
            float  azS = atan2(n.z, n.x);
            float3 street = lerp(float3(1.00, 0.10, 0.62), float3(0.08, 0.72, 1.00), 0.5 + 0.5 * sin(azS * 1.0 + 2.0));
            float  side = 1.0 - abs(n.y);
            float  Fs = pow(1.0 - saturate(dot(n, V)), 2.0);
            float3 albK = keep ? alb : lerp(alb, csLuma(alb).xxx, 0.6);
            lit += street * (albK * 0.55 + 0.03) * side * (0.35 + 0.65 * Fs) * 0.55 * NK;      // street light on the sides
            [branch] if (n.y > 0.5) {                                                               // rain-slick tops
                float3 r  = reflect(-V, n);
                float  azR = atan2(r.z, r.x);
                float3 cityC = lerp(float3(1.00, 0.12, 0.60), float3(0.10, 0.48, 1.00), 0.5 + 0.5 * sin(azR * 1.3 + 2.0));
                float  Fw = 0.04 + 0.96 * pow(1.0 - saturate(dot(n, V)), 5.0);
                float  wet = 0.55 + 0.45 * csVN(wp.xz * 0.9 + float2(CS_TIME * 0.05, 0.0));          // puddles: patchy
                float  streak = 0.6 + 0.4 * csVN(float2(azR * 30.0, (1.0 - r.y) * 6.0));            // the lights drag into streaks
                lit = lerp(lit, lit * 0.80, 0.30 * NK * wet * (keep ? 0.3 : 1.0));                    // wet = darker
                lit += cityC * Fw * streak * wet * 0.55 * NK * lerp(0.4, 1.0, skyK);
            }
        }
    }
#endif
    // the game's fog is NOT painted here: its amount goes out in alpha (opaque blocks ignore alpha) and the World FX
    // pass fades the block into whatever the Cloud Sea sky shows behind it (clouds or sky), so the render edge is soft
    lit = csHaze(lit, wp, cam, 0.0);
#if defined(NG_DEBUG) && NG_DEBUG == 2 && defined(CS_NOVAK) && defined(PASS_OPAQUE)
    if (ngOn > 0.0) lit = float3(saturate((1.0 - dot(nS, n)) * 40.0), csLuma(alb) * 1.4, ngIn.g < 0.5 ? 0.6 : 0.0);
#elif defined(NG_DEBUG) && defined(CS_NOVAK) && defined(PASS_OPAQUE)
    if (ngOn > 0.0) {
        float L = csLuma(lit);
        if (ngIn.g < 0.5) lit = float3(L, 0, L);
        else if (ngDbg > 0.5) lit = float3(0, L, 0);
        if (cav < 0.95) lit = lerp(lit, float3(0, 0, 1), 0.7);
    }
#endif
    return float4(csEnc(lit), 1.0 - i.color2.w);
#endif
}
