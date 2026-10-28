// Vibrant Visuals final pass: VV lighting + this pack's own grade (see _shared/ColorPostVV.fragment.hlsli)
float3 vvAces(float3 x) { const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14; return saturate((x * (a * x + b)) / (x * (c * x + d) + e)); }
float3 packGradeVV(float3 scene, float2 uv, float2 pix, float2 dims) { return cfFinal(scene, pix, dims, 0.0); }
// VV exposure trim (2026-10-28): VV's auto exposure + the pack's own tonemap blew the scene out to white at 1.0
#define VV_EXPOSURE_BIAS 0.16
#define VV_DEHAZE 0.2
#define VV_SATURATION 0.45
#define VV_WHITE_BALANCE float3(1.10, 1.0, 0.85)   // Fancy look is warm sepia fog
// @include ../../_shared/ColorPostVV.fragment.hlsli
