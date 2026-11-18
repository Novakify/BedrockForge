// Actor AlphaTest - Cursed Fog port
// @only Change_Color=Off
// @only Emissive=Off
// @only MaskedMultitexture=Off
// @uniform FogColor vec4
// @uniform FogControl vec4
// @uniform LightWorldSpaceDirection vec4
// @uniform u_viewRect vec4
Texture2D    s_MatTextureTexture : register(t0);
SamplerState s_MatTextureSampler : register(s0);
cbuffer FragmentUniforms : register(b1) {
    float4 UseAlphaRewrite; float4x4 u_view; float4x4 u_invView; float4 OverlayColor; float4 ColorBased;      // vanilla
    float4 DitherParams2[3]; float4 DitherParams; float4 DitheringEnabledToggle; float4 MatColor; float4 TintedAlphaTestEnabled;
    float4 FogColor; float4 FogControl; float4 LightWorldSpaceDirection; float4 u_viewRect;                   // added
};
// @include _body.hlsli
