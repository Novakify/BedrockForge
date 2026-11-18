// ItemInHandTextured: the block in your hand (and held by others). Cloud Sea: the same cloud block as the ones in the
// world (Fernando, Nov 15: "the block in your hand in first person still looks vanilla") - pastel, heaped in soft
// billows with a bright scallop on the lit side of each, the sun or the moon in its crests. The billows are a field in
// view space, so they stay on the block as you walk. Novak Glass: the game's own look (texture x light), unchanged.
Texture2D    s_MatTextureTexture : register(t0);
SamplerState s_MatTextureSampler : register(s0);
float4 main(PSIn i) : SV_Target {
    float4 d = s_MatTextureTexture.Sample(s_MatTextureSampler, i.texcoord0);
#if defined(PASS_ALPHATEST)
    if (d.a < 0.5) discard;
#endif
    float3 mc = dot(MatColor.rgb, float3(1, 1, 1)) > 0.01 ? MatColor.rgb : float3(1, 1, 1);
    float3 c = d.rgb * mc * ((i.color0.rgb - 1.0) * ColorBased.x + 1.0);
    c = lerp(c, c * ChangeColor.rgb, i.color0.a * step(0.001, dot(ChangeColor.rgb, float3(1, 1, 1))));
    c = lerp(c, OverlayColor.rgb, OverlayColor.a);
    float3 light = i.color1.rgb;
    float  lv = saturate(csLuma(light));
#if defined(PASS_TRANSPARENT)
    float  aOut = d.a;
#else
    float  aOut = 1.0;
#endif
#if defined(CS_NOVAK)
    float3 o = c * light;
#else
    csBeginActor(FogColor, ViewPositionAndTime);
    float  T = ViewPositionAndTime.w;
    bool   haveView = dot(u_view[0], u_view[0]) > 1e-6;
    float3 vp = haveView ? mul(u_view, float4(i.texcoord3, 1.0)).xyz : i.texcoord3;
    float3 nv = cross(ddx(vp), ddy(vp));
    nv = dot(nv, nv) > 1e-20 ? normalize(nv) : float3(0, 0, 1);
    if (dot(nv, -normalize(vp)) < 0.0) nv = -nv;
    // the billows: a smooth field through the block (height + slope in one call), two sizes
    // (Nov 15: the field was in view space - when you swing or the hand bobs the block moves through it and the billows
    //  raced across it. It lives on the block's texture now: atlas texels / 16 = one block face, so it stays put.)
    uint aw, ah; s_MatTextureTexture.GetDimensions(aw, ah);
    float2 tu = i.texcoord0 * float2(aw, ah) / 16.0;
    float3 q  = float3(tu * 2.2, 0.0);
    float4 b1 = csN3d(q), b2 = csN3d(q * 2.4 + 7.3);
    float  hB = b1.x * 0.65 + b2.x * 0.35;
    float2 g2 = (b1.yz * 0.65 + b2.yz * (0.35 * 2.4)) * 2.2;            // slope along the texture's u, v
    float3 dpx = ddx(vp), dpy = ddy(vp); float2 dux = ddx(tu), duy = ddy(tu);
    float  det = dux.x * duy.y - dux.y * duy.x;
    float3 Tu = (dpx * duy.y - dpy * dux.y) * (abs(det) > 1e-12 ? 1.0 / det : 0.0);
    float3 Tv = (dpy * dux.x - dpx * duy.x) * (abs(det) > 1e-12 ? 1.0 / det : 0.0);
    Tu = dot(Tu, Tu) > 1e-12 ? normalize(Tu) : float3(1, 0, 0); Tv = dot(Tv, Tv) > 1e-12 ? normalize(Tv) : float3(0, 1, 0);
    float3 g  = Tu * g2.x + Tv * g2.y;
    g -= nv * dot(g, nv);
    float3 nP = normalize(nv - g * 0.42);
    float3 La = normalize(float3(0.35, 0.80, 0.45));                       // the view model's key light: high, to the right
    float  t  = saturate(0.5 + (dot(nP, La) - dot(nv, La)) * 1.0 + (hB - 0.5) * 0.35);
    // pastel: a cloud is light (dark blocks most of all); less at night so team colours still tell apart
    float3 cc = lerp(c, float3(1.00, 0.97, 0.94) * (0.62 + 0.38 * csMax3(c)), (0.28 + 0.34 * (1.0 - csMax3(c))) * lerp(1.0, 0.55, CS_NIGHT));
    float3 shadeC = cc * lerp(float3(0.76, 0.74, 0.92), float3(0.72, 0.77, 0.95), CS_NIGHT);
    float3 crestC = lerp(cc, float3(1.00, 0.97, 0.92), 0.28) * 1.10;
    float3 pc = lerp(shadeC, lerp(cc, crestC, smoothstep(0.48, 0.95, t)), smoothstep(0.05, 0.55, t));
    // the scallop: a bright arc where each heap turns toward the light
    float  aHi = (hB - 0.58) / 0.07;
    pc += (crestC * 1.04 - pc) * exp(-aHi * aHi) * saturate(dot(normalize(-g + 1e-4), La) * 1.3) * 0.55;
    // the face's own light (the block's three faces must still read as a cube), then the hour's
    float  face = 0.74 + 0.30 * saturate(dot(nv, La));
    float3 hour = lerp(lerp(float3(1.00, 0.90, 0.78), float3(1.00, 0.74, 0.70), CS_DUSK), float3(0.62, 0.72, 1.00), CS_NIGHT);
    float3 lin = csLin(pc) * face * hour * lerp(0.40, 1.0, max(lv, 0.75 * CS_NIGHT));
    float3 glowC = lerp(lerp(float3(1.00, 0.80, 0.46), float3(1.00, 0.50, 0.36), CS_DUSK), float3(0.46, 0.62, 1.00), CS_NIGHT);
    lin += lerp(glowC, csLin(cc) * 1.8, 0.45) * smoothstep(0.55, 0.90, hB) * 0.07;      // the sun or the moon in the crests
    lin += csLin(cc) * float3(0.92, 0.96, 1.10) * 0.16 * CS_NIGHT;
    float3 o = csEnc(lin);
#endif
    o = lerp(o, i.color2.rgb, i.color2.a);
    return float4(o, aOut);
}
