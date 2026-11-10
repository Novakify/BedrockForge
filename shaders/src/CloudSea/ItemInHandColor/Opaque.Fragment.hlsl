// Cloud Sea: the item in your hand (swords, tools: the game draws them as little coloured texel boxes).
// Vanilla gives held items one flat light. Here they get a lighting rig in the sunset palette (warm key, cool violet
// fill, a hot rim from behind like the low sun), and each texel gets a finish from its colour: diamond reads as gem,
// iron as polished steel, gold as warm metal, netherite as dark metal with a violet sheen, handles stay matte.
// The rig lives in view space on purpose: a held item is lit like a first-person view model, so it always reads.
// @uniform u_view mat4
#if defined(FLAG_MULTICOLORTINT_ON)
cbuffer FragmentUniforms : register(b1) { float4 ChangeColor; float4 OverlayColor; float4 ColorBased; float4 MultiplicativeTintColor; column_major float4x4 u_view; };
#else
cbuffer FragmentUniforms : register(b1) { float4 ChangeColor; float4 OverlayColor; float4 ColorBased; column_major float4x4 u_view; };
#endif

float3 envView(float3 r) { // what a polished surface "sees": violet above, the orange band, pink cloud below
    float y = r.y;
    float3 top = float3(0.10, 0.10, 0.26), band = float3(1.05, 0.42, 0.12), low = float3(0.70, 0.36, 0.40);
    float3 c = lerp(band, top, smoothstep(0.0, 0.55, y));
    c = lerp(c, low, smoothstep(0.0, -0.35, y));
    return c + float3(1.2, 0.7, 0.3) * pow(saturate(1.0 - abs(y - 0.05) * 6.0), 3.0) * 0.6;
}

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
    float3 o = csEnc(col);
    o = lerp(o, i.color2.rgb, i.color2.a);
    return float4(o, 1.0);
}
