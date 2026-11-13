// Cloud Sea: the item in your hand (swords, tools: the game draws them as little coloured texel boxes).
// Vanilla gives held items one flat light. Here they get a lighting rig in the sunset palette (warm key, cool violet
// fill, a hot rim from behind like the low sun), and each texel gets a finish from its colour: diamond reads as gem,
// iron as polished steel, gold as warm metal, netherite as dark metal with a violet sheen, handles stay matte.
// The rig lives in view space on purpose: a held item is lit like a first-person view model, so it always reads.
// @uniform u_view mat4
#if defined(CS_NOVAK)
// @uniform FogColor vec4
// @uniform ViewPositionAndTime vec4
#if defined(FLAG_MULTICOLORTINT_ON)
cbuffer FragmentUniforms : register(b1) { float4 ChangeColor; float4 OverlayColor; float4 ColorBased; float4 MultiplicativeTintColor; column_major float4x4 u_view; float4 FogColor; float4 ViewPositionAndTime; };
#else
cbuffer FragmentUniforms : register(b1) { float4 ChangeColor; float4 OverlayColor; float4 ColorBased; column_major float4x4 u_view; float4 FogColor; float4 ViewPositionAndTime; };
#endif
#else
#if defined(FLAG_MULTICOLORTINT_ON)
cbuffer FragmentUniforms : register(b1) { float4 ChangeColor; float4 OverlayColor; float4 ColorBased; float4 MultiplicativeTintColor; column_major float4x4 u_view; };
#else
cbuffer FragmentUniforms : register(b1) { float4 ChangeColor; float4 OverlayColor; float4 ColorBased; column_major float4x4 u_view; };
#endif
#endif

float3 envView(float3 r) { // what a polished surface "sees": violet above, the orange band, pink cloud below
    float y = r.y;
    float3 top = float3(0.10, 0.10, 0.26), band = float3(1.05, 0.42, 0.12), low = float3(0.70, 0.36, 0.40);
    float3 c = lerp(band, top, smoothstep(0.0, 0.55, y));
    c = lerp(c, low, smoothstep(0.0, -0.35, y));
    return c + float3(1.2, 0.7, 0.3) * pow(saturate(1.0 - abs(y - 0.05) * 6.0), 3.0) * 0.6;
}

#if defined(CS_NOVAK)
// what Novak glass sees (view space, y up): the day sky's aqua-to-ultramarine, or the night city - deep violet above,
// a band of neon light along the horizon
float3 ngEnv(float3 r, float day) {
    float  y = r.y;
    float3 dayC   = lerp(float3(0.62, 0.86, 1.00), float3(0.12, 0.32, 0.88), smoothstep(-0.10, 0.60, y)) + float3(1.0, 0.95, 0.85) * pow(saturate(1.0 - abs(y - 0.30) * 3.0), 6.0) * 0.4;
    float3 nightC = lerp(float3(0.34, 0.06, 0.40), float3(0.02, 0.02, 0.09), smoothstep(-0.20, 0.50, y))
                  + float3(0.10, 0.80, 1.00) * pow(saturate(1.0 - abs(y - 0.04) * 5.0), 3.0) * 0.55
                  + float3(1.00, 0.20, 0.75) * pow(saturate(1.0 - abs(y + 0.12) * 6.0), 3.0) * 0.45;
    return lerp(nightC, dayC, day);
}
#endif
float4 main(PSIn i) : SV_Target {
    float3 base = (i.color0.rgb - 1.0) * ColorBased.x + 1.0;
    float3 c = lerp(base, base * ChangeColor.rgb, i.color0.a);
    c = lerp(c, OverlayColor.rgb, OverlayColor.a);
    float3 vanillaLight = i.color1.rgb;                       // the game's light level where you stand
    float  lv = saturate(csLuma(vanillaLight));

    // view-space position + face normal (the texel boxes are flat-faced, so derivatives give clean facets)
    bool   haveView = dot(u_view[0], u_view[0]) > 1e-6;          // in case the game leaves u_view empty here
    float3 vp = haveView ? mul(u_view, float4(i.texcoord3, 1.0)).xyz : i.texcoord3;
    float3 nv = cross(ddx(vp), ddy(vp));
    nv = dot(nv, nv) > 1e-20 ? normalize(nv) : float3(0, 0, 1);
    float3 V  = -normalize(vp);
    if (dot(nv, V) < 0.0) nv = -nv;
    float3 up = float3(0, 1, 0), rt = float3(1, 0, 0);
    float3 Lk = normalize(V * 0.45 + up * 0.70 + rt * 0.55);   // key: warm gold, high on the right
    float3 Lf = normalize(V * 0.60 - rt * 0.70 + up * 0.10);   // fill: cool violet from the left
    float3 Lr = normalize(-V * 0.75 + up * 0.35 + rt * 0.35);  // rim: the low sun behind the blade

    // finish from the texel colour (sRGB)
    float  luma = csLuma(c), chroma = csMax3(c) - min(c.r, min(c.g, c.b));
    float  gem   = smoothstep(0.10, 0.25, c.g - c.r) * smoothstep(0.05, 0.20, c.b - c.r) * smoothstep(0.35, 0.55, csMax3(c)); // diamond / cyan
    float  gold  = smoothstep(0.10, 0.25, c.r - c.b) * smoothstep(0.55, 0.75, c.r) * smoothstep(0.45, 0.6, c.g) * (1.0 - gem);
    float  steel = (1.0 - smoothstep(0.06, 0.14, chroma)) * smoothstep(0.42, 0.62, luma) * (1.0 - gem) * (1.0 - gold);
    float  nether = (1.0 - smoothstep(0.10, 0.24, luma)) * (1.0 - smoothstep(0.10, 0.22, chroma)) * (1.0 - gem);
    float  metal = saturate(gem + gold + steel + nether);
    float  rough = lerp(18.0, lerp(70.0, 140.0, gem), metal);

    float3 alb  = csLin(c);
    float3 tint = lerp(float3(1, 1, 1), alb / max(csMax3(alb), 1e-3), saturate(gold + gem * 0.7 + nether * 0.3));
    float  ndk = saturate(dot(nv, Lk)), ndf = saturate(dot(nv, Lf) * 0.5 + 0.5), ndr = pow(saturate(dot(nv, Lr) * 0.5 + 0.5), 3.0);
    float3 keyC = float3(1.00, 0.72, 0.50) * 1.55, fillC = float3(0.20, 0.19, 0.34), rimC = float3(1.30, 0.55, 0.20);
    float  rimEdge = pow(1.0 - saturate(dot(nv, V)), 2.0);
    float3 col = alb * (keyC * ndk + fillC * ndf + float3(0.18, 0.11, 0.12));
    col += alb * rimC * rimEdge * ndr * 1.4 * (1.0 - metal * 0.5);
    // highlights and reflections: only the metals and gems
    float3 Hk = normalize(Lk + V);
    float  sk = pow(saturate(dot(nv, Hk)), rough) * (rough + 8.0) / 25.0;
    float3 R  = reflect(-V, nv);
    float  F  = lerp(0.04, 0.55, metal) + (1.0 - lerp(0.04, 0.55, metal)) * pow(1.0 - saturate(dot(nv, V)), 5.0);
    col += keyC * tint * sk * metal * 0.9;
    col += rimC * tint * pow(saturate(dot(R, Lr)), rough * 0.5) * metal * 1.2;
    col  = lerp(col, envView(R) * tint * (0.6 + 0.4 * luma), saturate(F * metal * 0.75));
    col += float3(0.22, 0.10, 0.45) * pow(1.0 - saturate(dot(nv, V)), 3.0) * nether * 0.8;   // netherite's violet sheen
    // in the dark (caves, night) the item follows the game's light level instead of glowing
    col *= lerp(0.25, 1.0, lv);
#if defined(CS_NOVAK)
    // ---- Novak Glass: held items are glass that follows the match. At the start it is Future Aero glass - clear, airy,
    //      a breath of the item's colour, a glossy top and a clean white rim, the bright bay-city day in it. As beds fall
    //      it warms through rose and violet, and by the endgame it is neon glass that lights itself: deep saturated
    //      colour, an inner glow that breathes, fine lines of light running up the blade, a cyan / magenta rim, and the
    //      facets splitting the light harder. (The match is read from the game's fog, like the players' shader.)
    {
        float  k    = saturate(csActorPhase(FogColor.rgb) * 0.5);      // 0 the match's start .. 1 its night / endgame
        float  neonK = smoothstep(0.30, 0.95, k);
        float  T    = ViewPositionAndTime.w;
        float3 hue  = alb / max(csMax3(alb), 1e-3);
        float  grey = 1.0 - smoothstep(0.05, 0.18, chroma);           // steel, stone, netherite: no colour of their own
        float  Fg   = 0.05 + 0.95 * pow(1.0 - saturate(dot(nv, V)), 4.0);
        float  glint = pow(saturate(dot(nv, normalize(Lk + V))), 220.0);
        // Future Aero: clear glass, the item's colour as a tint, the day sky bent through it
        float  sp   = lerp(0.010, 0.030, neonK);                       // the colour split grows toward the end
        float3 Tr = refract(-V, nv, 0.67 - sp), Tg = refract(-V, nv, 0.67), Tb = refract(-V, nv, 0.67 + sp);
        float3 aeroTint = lerp(lerp(float3(0.86, 0.95, 1.00), hue, 0.30 + 0.35 * gem), float3(0.80, 0.92, 1.00), grey);
        float3 thruA = float3(ngEnv(Tr, 1.0).r, ngEnv(Tg, 1.0).g, ngEnv(Tb, 1.0).b);
        float3 aero = thruA * aeroTint * lerp(0.75, 1.05, luma);
        aero = lerp(aero, ngEnv(reflect(-V, nv), 1.0), Fg * 0.50);
        aero += float3(1.0, 1.0, 1.0) * smoothstep(0.25, 0.95, nv.y) * 0.18;                 // the glossy top (Aero's bubble shine)
        aero += float3(1.0, 0.98, 0.95) * glint * 2.4;
        aero += float3(0.90, 0.97, 1.00) * pow(1.0 - saturate(dot(nv, V)), 3.0) * 0.30;      // the clean white rim
        aero = lerp(aero, aero * float3(1.05, 0.78, 0.88), smoothstep(0.15, 0.55, k) * (1.0 - neonK));   // dusk: rose / violet
        aero *= lerp(0.45, 1.0, lv);
        // neon: the colour turns up and lights itself
        float3 nHue = grey > 0.5 ? lerp(float3(0.10, 0.85, 1.00), float3(1.00, 0.18, 0.80), 0.5 + 0.5 * sin(vp.y * 7.0 + T * 0.8)) : saturate(hue * hue * 1.3);
        float3 thruN = float3(ngEnv(Tr, 0.0).r, ngEnv(Tg, 0.0).g, ngEnv(Tb, 0.0).b);
        float  breathe = 0.80 + 0.20 * sin(T * 2.2 + vp.y * 3.0);
        float  lines = pow(saturate(1.0 - abs(frac(vp.y * 26.0 - T * 1.6) - 0.5) * 2.0), 24.0); // fine light running up the blade
        float3 neon = thruN * nHue * 0.6 + nHue * (0.42 * breathe + 0.55 * lines * (1.0 - grey * 0.4));
        neon += lerp(float3(0.10, 0.85, 1.00), float3(1.00, 0.20, 0.80), 0.5 + 0.5 * sin(T * 0.9 + vp.x * 9.0)) * pow(1.0 - saturate(dot(nv, V)), 2.2) * 1.1;
        neon += float3(1.0, 0.95, 1.0) * glint * 2.8;
        neon += ngEnv(reflect(-V, nv), 0.0) * Fg * 0.6;
        col = lerp(aero, neon, neonK);
    }
#endif
    float3 o = csEnc(col);
    o = lerp(o, i.color2.rgb, i.color2.a);
    return float4(o, 1.0);
}
