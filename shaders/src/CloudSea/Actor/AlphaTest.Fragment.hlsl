// Cloud Sea held equipment and alpha-tested mobs (Actor AlphaTest): same light as the blocks, and World FX equipment
// textures carry a material id in alpha (0.5..1 survives the alpha test): metal blades and plates reflect the Cloud Sea,
// cut crystal sparkles. Vanilla textures have alpha 1 here -> id 0 -> the soft sheen of the Opaque pass.
// @only Change_Color=Off
// @only Emissive=Off
// @only MaskedMultitexture=Off
// @uniform FogColor vec4
// @uniform ViewPositionAndTime vec4
Texture2D    s_MatTextureTexture : register(t0);
SamplerState s_MatTextureSampler : register(s0);
cbuffer FragmentUniforms : register(b1) {   // the AlphaTest pass's own layout (dither + alpha rewrite live here)
    float4 UseAlphaRewrite; float4x4 u_view; float4x4 u_invView; float4 OverlayColor; float4 ColorBased;
    float4 DitherParams2[3]; float4 DitherParams; float4 DitheringEnabledToggle; float4 MatColor; float4 TintedAlphaTestEnabled;
    float4 FogColor; float4 ViewPositionAndTime;
};

#if defined(CS_NOVAK)
// what Novak glass sees: the day sky's aqua-to-ultramarine, or the night city (OLED black, neon on the horizon)
float3 ngEnv(float3 r, float day) {
    float  y = r.y;
    float3 dayC   = lerp(float3(0.62, 0.86, 1.00), float3(0.12, 0.32, 0.88), smoothstep(-0.10, 0.60, y)) + float3(1.0, 0.95, 0.85) * pow(saturate(1.0 - abs(y - 0.30) * 3.0), 6.0) * 0.4;
    float3 nightC = float3(0.004, 0.004, 0.010)
                  + float3(0.10, 0.80, 1.00) * pow(saturate(1.0 - abs(y - 0.04) * 5.0), 3.0) * 0.55
                  + float3(1.00, 0.20, 0.75) * pow(saturate(1.0 - abs(y + 0.10) * 6.0), 3.0) * 0.50;
    return lerp(nightC, dayC, day);
}
// Items as glass that follows the match (k: 0 the start .. 1 the endgame, from the match phase). Start: Future Aero -
// clear glass with the item's colour as a tint, the bright day bent through it, a glossy top and a clean white rim.
// Dusk: rose / violet. Endgame: neon glass that lights itself - deep colour, an inner glow that breathes, light running
// along the item in fine lines, a cyan / magenta rim, and the facets splitting light harder.
float3 ngItemGlass(float3 c, float3 n, float3 V, float3 gn, float sky, float T, float3 wp) {
    float  k     = saturate(CS_PHASE * 0.5);
    float  neonK = smoothstep(0.30, 0.95, k);
    float3 alb   = csLin(c);
    float  luma  = csLuma(c), chroma = csMax3(c) - min(c.r, min(c.g, c.b));
    float3 hue   = alb / max(csMax3(alb), 1e-3);
    float  grey  = 1.0 - smoothstep(0.05, 0.18, chroma);
    float  gem   = smoothstep(0.10, 0.25, c.g - c.r) * smoothstep(0.05, 0.20, c.b - c.r);
    float3 nf    = normalize(lerp(n, gn, 0.35));                     // each texel a slightly different facet
    float  NV    = saturate(dot(nf, V));
    float  Fg    = 0.05 + 0.95 * pow(1.0 - NV, 4.0);
    float3 Lk    = normalize(float3(0.35, 0.80, 0.45));
    float  glint = pow(saturate(dot(nf, normalize(Lk + V))), 180.0);
    float  sp    = lerp(0.012, 0.035, neonK);
    float3 Tr = refract(-V, nf, 0.67 - sp), Tg = refract(-V, nf, 0.67), Tb = refract(-V, nf, 0.67 + sp);
    float3 R  = reflect(-V, nf);
    // Future Aero
    float3 aeroTint = lerp(lerp(float3(0.86, 0.95, 1.00), hue, 0.38 + 0.30 * gem), float3(0.82, 0.92, 1.00), grey * 0.7);
    float3 aero = float3(ngEnv(Tr, 1.0).r, ngEnv(Tg, 1.0).g, ngEnv(Tb, 1.0).b) * aeroTint * lerp(0.70, 1.05, luma);
    aero = lerp(aero, ngEnv(R, 1.0), Fg * 0.50);
    aero += smoothstep(0.30, 0.95, nf.y) * 0.16;                                        // the glossy top
    aero += float3(1.0, 0.98, 0.95) * glint * 2.2;
    aero += float3(0.90, 0.97, 1.00) * pow(1.0 - NV, 3.0) * 0.30;                       // the clean white rim
    aero = lerp(aero, aero * float3(1.05, 0.78, 0.90), smoothstep(0.15, 0.55, k) * (1.0 - neonK));   // dusk
    aero *= lerp(0.50, 1.0, sky);
    // neon
    float3 nHue  = grey > 0.5 ? lerp(float3(0.10, 0.85, 1.00), float3(1.00, 0.18, 0.80), 0.5 + 0.5 * sin(dot(wp, float3(3.1, 7.0, 2.3)) + T * 0.8)) : saturate(hue * hue * 1.3);
    float  breathe = 0.80 + 0.20 * sin(T * 2.2 + wp.y * 3.0);
    float  lines = pow(saturate(1.0 - abs(frac(dot(wp, float3(4.0, 26.0, 4.0)) - T * 1.6) - 0.5) * 2.0), 24.0);
    float3 neon = float3(ngEnv(Tr, 0.0).r, ngEnv(Tg, 0.0).g, ngEnv(Tb, 0.0).b) * nHue * 0.6 + nHue * (0.42 * breathe + 0.55 * lines * (1.0 - grey * 0.4));
    neon += lerp(float3(0.10, 0.85, 1.00), float3(1.00, 0.20, 0.80), 0.5 + 0.5 * sin(T * 0.9 + wp.x * 9.0)) * pow(1.0 - NV, 2.2) * 1.1;
    neon += float3(1.0, 0.95, 1.0) * glint * 2.6 + ngEnv(R, 0.0) * Fg * 0.6;
    return lerp(aero, neon, neonK);
}
#endif
float4 main(PSIn i) : SV_Target {
    csBeginActor(FogColor, ViewPositionAndTime);
    float4 t = s_MatTextureTexture.Sample(s_MatTextureSampler, i.texcoord0);
    if (t.a < 0.5) discard;
    uint tw, th, lv0;
    s_MatTextureTexture.GetDimensions(0, tw, th, lv0);
    int2  tx  = int2(frac(i.texcoord0) * float2(tw, th));
    float a0  = s_MatTextureTexture.Load(int3(tx, 0)).a;    // exact texel alpha: the material id
    int   id  = int(round((1.0 - a0) * 255.0 / 4.0));
    float3 c = t.rgb * MatColor.rgb;
    c *= (i.color0.rgb - 1.0) * ColorBased.x + 1.0;
    c  = lerp(c, OverlayColor.rgb, OverlayColor.a);
    float  lv  = csLuma(i.color3.rgb);
    float3 wp  = i.texcoord3;
    float3 cam = ViewPositionAndTime.xyz;
    float3 V   = normalize(cam - wp);
    float3 n   = csNormal(wp, V, false);
    float  sky = smoothstep(0.30, 0.80, lv);
    CsMat  m;
    if (id == MAT_METAL || id == MAT_GEM) {
        m = csMat(id);
        m.bump = 0.0;
        if (id == MAT_METAL) { m.refl = 0.75; m.rough = 48.0; }
    } else {
        m = csMat(MAT_NONE);
        m.bump = 0.0; m.spec = 0.08; m.rough = 24.0;
    }
    float3 gn  = normalize(n + (csHash3(float2(tx)) - 0.5) * 0.9);  // each texel its own tiny facet
    float3 lit = csLight(csLin(c), n, n, sky, sky * 0.9 + 0.1, 0.0, V, m, 1.0, gn, 1.0);
    // ---- the item in your own hand: lit like a film's view model, not by where the sun happens to be. A key light
    //      high over your right shoulder in the hour's colour, a cool fill from the left, and the real sun (or moon)
    //      as a rim when it is behind the blade - so the sword always reads, and still belongs to the scene.
    float  fp  = csViewModel(lit, csLin(c), n, V, m, gn, sky, length(cam - wp), u_view[0].xyz, u_view[1].xyz);
#if defined(CS_NOVAK)
    // ---- Novak Glass: every item (its own 16/32 px texture - seen in game: not an atlas) (the sword in your hand, tools, the bow, wool,
    //      pearls, fireballs - held or dropped) is glass that follows the match: Future Aero at the start, neon by the
    //      endgame (ngItemGlass). Mob and player skins are small textures and stay as they are.
    [branch] if (tw <= 32 && th <= 32) lit = ngItemGlass(c, n, V, gn, sky, ViewPositionAndTime.w, wp);
#if defined(NG_DBG_TEX)
    lit = tw >= 256 ? float3(4, 0, 4) : (tw >= 64 ? float3(0, 4, 4) : float3(4, 4, 0));
#endif
#endif
    lit = csHaze(lit, wp, cam, i.color2.a * (1.0 - fp));
    return float4(csEnc(lit), 1.0);
}
