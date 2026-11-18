// Cloud Sea / Novak: the item in your hand, Transparent pass (the game draws first-person items here too - Fernando, Nov 15:
// they stayed vanilla in first person while only the Opaque pass had the glass). Same body as Opaque.
// @uniform FogColor vec4
// @uniform ViewPositionAndTime vec4
#if defined(FLAG_MULTICOLORTINT_ON)
cbuffer FragmentUniforms : register(b1) { column_major float4x4 u_view; float4 ChangeColor; column_major float4x4 u_invView; float4 OverlayColor; float4 ColorBased;
    float4 DitherParams2[3]; float4 DitherParams; float4 DitheringEnabledToggle; float4 MultiplicativeTintColor; float4 FogColor; float4 ViewPositionAndTime; };
#else
cbuffer FragmentUniforms : register(b1) { column_major float4x4 u_view; float4 ChangeColor; column_major float4x4 u_invView; float4 OverlayColor; float4 ColorBased;
    float4 DitherParams2[3]; float4 DitherParams; float4 DitheringEnabledToggle; float4 FogColor; float4 ViewPositionAndTime; };
#endif
// @include Body.hlsli
