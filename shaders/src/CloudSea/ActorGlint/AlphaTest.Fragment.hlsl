// Cloud Sea enchanted equipment / mobs (ActorGlint AlphaTest): the item lit like everything else (material ids as in
// Actor AlphaTest), and instead of the purple wash the enchantment is pale warm light that slides along the surface,
// strongest at the silhouette and on the bright texels (edges, highlights) - an aura of light, not a colour filter.
// @only Change_Color=Off
// @only Emissive=Off
// @only MaskedMultitexture=Off
// @uniform FogColor vec4
// @uniform ViewPositionAndTime vec4
Texture2D    s_MatTextureTexture  : register(t0);
SamplerState s_MatTextureSampler  : register(s0);
Texture2D    s_MatTexture1Texture : register(t1);
SamplerState s_MatTexture1Sampler : register(s1);
cbuffer FragmentUniforms : register(b1) {
    float4 UseAlphaRewrite; float4x4 u_view; float4x4 u_invView; float4 OverlayColor; float4 ColorBased;
    float4 DitherParams2[3]; float4 DitherParams; float4 DitheringEnabledToggle; float4 GlintColor; float4 MatColor;
    float4 TileLightColor; float4 FogColor; float4 ViewPositionAndTime;
};

float4 main(PSIn i) : SV_Target {
    csBeginActor(FogColor, ViewPositionAndTime);
    float4 t = s_MatTextureTexture.Sample(s_MatTextureSampler, i.texcoord0);
    if (t.a < 0.5) discard;
    uint tw, th, lv0;
    s_MatTextureTexture.GetDimensions(0, tw, th, lv0);
    int2  tx  = int2(frac(i.texcoord0) * float2(tw, th));
    float a0  = s_MatTextureTexture.Load(int3(tx, 0)).a;
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
    if (id == MAT_METAL || id == MAT_GEM) { m = csMat(id); m.bump = 0.0; if (id == MAT_METAL) { m.refl = 0.75; m.rough = 48.0; } }
    else { m = csMat(MAT_NONE); m.bump = 0.0; m.spec = 0.08; m.rough = 24.0; }
    float3 gn  = normalize(n + (csHash3(float2(tx)) - 0.5) * 0.9);
    float3 lit = csLight(csLin(c), n, n, sky, sky * 0.9 + 0.1, 0.0, V, m, 1.0, gn, 1.0);
    float  fp  = csViewModel(lit, csLin(c), n, V, m, gn, sky, length(cam - wp), u_view[0].xyz, u_view[1].xyz);
    // the enchantment: two slow layers of the vanilla glint pattern, kept as a thin pale-gold sheen
    float g1 = s_MatTexture1Texture.Sample(s_MatTexture1Sampler, i.texcoord2.xy).r;
    float g2 = s_MatTexture1Texture.Sample(s_MatTexture1Sampler, i.texcoord2.zw).r;
    float g  = saturate(g1 * g2 * 2.2 + pow(saturate(max(g1, g2)), 6.0) * 0.6);
    float rim = pow(1.0 - saturate(dot(n, V)), 1.6);
    float bright = smoothstep(0.35, 0.9, csLuma(csLin(c)));
    float k  = g * (0.35 + 0.65 * max(rim, bright)) * saturate(csLuma(GlintColor.rgb) * 3.0 + 0.4);
#if defined(CS_NOVAK)
    // ---- Novak Glass: enchanted items are glass too, and the enchantment is a hologram on the glass - thin-film
    //      rainbow light that shifts with the angle you see it from, a band of spectral light sweeping along it, and
    //      tiny glitter sparks, all living inside the vanilla enchant pattern. It burns brighter toward the endgame.
    [branch] if (tw <= 32 && th <= 32) lit = ngItemGlass(c, n, V, gn, sky, ViewPositionAndTime.w, wp);
    {
        float  T    = ViewPositionAndTime.w;
        float  hueA = frac((1.0 - saturate(dot(n, V))) * 1.4 + T * 0.05 + g * 0.35);
        float3 film = 0.5 + 0.5 * cos(6.2832 * (hueA + float3(0.0, 0.33, 0.67)));
        float  sw   = frac(dot(wp, float3(0.6, 1.0, 0.3)) * 0.9 - T * 0.35);
        float  band = exp(-pow((sw - 0.5) * 14.0, 2.0));
        float  hs   = csHash(float2(tx) + floor(T * 6.0) * 7.31);
        float  spark = step(0.985, hs) * (0.6 + 0.4 * sin(T * 30.0 + hs * 50.0));
        float  hk   = lerp(0.8, 1.5, smoothstep(0.30, 0.95, saturate(CS_PHASE * 0.5)));
        lit += film * (k * 0.9 + band * g * 0.9 * (0.4 + 0.6 * rim)) * hk + float3(1.0, 1.0, 1.0) * spark * g * 1.6;
    }
#else
    lit += lerp(float3(1.00, 0.84, 0.62), float3(0.75, 0.85, 1.10), CS_NIGHT) * k * 0.9;
#endif
    lit = csHaze(lit, wp, cam, i.color2.a * (1.0 - fp));
    return float4(csEnc(lit), 1.0);
}
