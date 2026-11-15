// @uniform u_view mat4
// @uniform FogColor vec4
// @uniform ViewPositionAndTime vec4
#if defined(FLAG_MULTICOLORTINT_ON)
cbuffer FragmentUniforms : register(b1) { float4 ChangeColor; float4 OverlayColor; float4 AlphaMaskedTint; float4 ColorBased; float4 MatColor; float4 MultiplicativeTintColor; column_major float4x4 u_view; float4 FogColor; float4 ViewPositionAndTime; };
#else
cbuffer FragmentUniforms : register(b1) { float4 ChangeColor; float4 OverlayColor; float4 AlphaMaskedTint; float4 ColorBased; float4 MatColor; column_major float4x4 u_view; float4 FogColor; float4 ViewPositionAndTime; };
#endif
// @include Body.hlsli
