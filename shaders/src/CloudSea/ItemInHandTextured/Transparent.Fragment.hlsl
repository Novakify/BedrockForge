// @uniform FogColor vec4
// @uniform ViewPositionAndTime vec4
#if defined(FLAG_MULTICOLORTINT_ON)
cbuffer FragmentUniforms : register(b1) { column_major float4x4 u_view; float4 ChangeColor; column_major float4x4 u_invView; float4 OverlayColor; float4 AlphaMaskedTint; float4 ColorBased;
    float4 DitherParams2[3]; float4 DitherParams; float4 DitheringEnabledToggle; float4 MatColor; float4 MultiplicativeTintColor; float4 FogColor; float4 ViewPositionAndTime; };
#else
cbuffer FragmentUniforms : register(b1) { column_major float4x4 u_view; float4 ChangeColor; column_major float4x4 u_invView; float4 OverlayColor; float4 AlphaMaskedTint; float4 ColorBased;
    float4 DitherParams2[3]; float4 DitherParams; float4 DitheringEnabledToggle; float4 MatColor; float4 FogColor; float4 ViewPositionAndTime; };
#endif
// @include Body.hlsli
