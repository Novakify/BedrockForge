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
    // the city lights people at night in neutral white light, so skins keep their own colours (seen on Hive: every
    // player went the same smudged blue-grey under the moon)
    // menus (home screen / dressing room): no world, so no fog - the fog-driven phase read that as the endgame and gave
    // the character a neon pink / cyan rim and the night light (seen: washed out with a pink or light-blue tint)
    bool const menu = csMax3(abs(FogColor.rgb)) < 1e-4;
    [branch] if (menu) { lit = csHaze(lit, wp, cam, 0.0); return float4(csEnc(lit), 1.0); }
    [branch] if (CS_NIGHT > 0.0 && !(tw <= 32 && th <= 32)) {
        float dif = 0.60 + 0.40 * saturate(dot(n, normalize(float3(0.3, 0.8, 0.5))));
        lit = lerp(lit, csLin(c) * dif * 0.90, CS_NIGHT);
    }
    [branch] if (tw <= 32 && th <= 32) lit = ngItemGlass(c, n, V, gn, sky, ViewPositionAndTime.w, wp);
    // glassy players: a thin glass rim on skins (64 / 128 px) - clean white-aqua at the start, cyan / magenta neon by
    // the endgame (never on your own arm in first person)
    [branch] if (tw >= 64 && tw <= 128 && th >= 64 && th <= 128) {
        float  pk = smoothstep(0.30, 0.95, saturate(CS_PHASE * 0.5));
        float  rimP = pow(1.0 - saturate(dot(n, V)), 3.0);
        float3 rc = lerp(float3(0.85, 0.95, 1.00) * 0.30, lerp(float3(0.10, 0.85, 1.00), float3(1.00, 0.20, 0.80), 0.5 + 0.5 * sin(ViewPositionAndTime.w * 0.7 + wp.y * 2.0)) * 1.3, pk);
        lit += rc * rimP * (1.0 - fp);
    }
#if defined(NG_DBG_TEX)
    lit = tw >= 256 ? float3(4, 0, 4) : (tw >= 64 ? float3(0, 4, 4) : float3(4, 4, 0));
#endif
#endif
    lit = csHaze(lit, wp, cam, i.color2.a * (1.0 - fp));
    return float4(csEnc(lit), 1.0);
}
