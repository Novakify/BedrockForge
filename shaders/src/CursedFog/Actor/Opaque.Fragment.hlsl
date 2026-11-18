// Actor Opaque - Cursed Fog port
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
    float4 OverlayColor; float4 ColorBased; float4 MatColor;                                   // vanilla
    float4 FogColor; float4 FogControl; float4 LightWorldSpaceDirection; float4 u_viewRect;    // added
};
// @include _body.hlsli
