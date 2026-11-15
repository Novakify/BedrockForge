// Cloud Sea: the item in your hand (swords, tools: the game draws them as little coloured texel boxes).
// Vanilla gives held items one flat light. Here they get a lighting rig in the sunset palette (warm key, cool violet
// fill, a hot rim from behind like the low sun), and each texel gets a finish from its colour: diamond reads as gem,
// iron as polished steel, gold as warm metal, netherite as dark metal with a violet sheen, handles stay matte.
// The rig lives in view space on purpose: a held item is lit like a first-person view model, so it always reads.
// @uniform u_view mat4
// @uniform FogColor vec4
// @uniform ViewPositionAndTime vec4
#if defined(FLAG_MULTICOLORTINT_ON)
cbuffer FragmentUniforms : register(b1) { float4 ChangeColor; float4 OverlayColor; float4 ColorBased; float4 MultiplicativeTintColor; column_major float4x4 u_view; float4 FogColor; float4 ViewPositionAndTime; };
#else
cbuffer FragmentUniforms : register(b1) { float4 ChangeColor; float4 OverlayColor; float4 ColorBased; column_major float4x4 u_view; float4 FogColor; float4 ViewPositionAndTime; };
#endif

// @include Body.hlsli
