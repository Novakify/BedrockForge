// Vibrant Visuals final pass: VV lighting + this pack's own grade (see _shared/ColorPostVV.fragment.hlsli)
float3 vvAces(float3 x) { const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14; return saturate((x * (a * x + b)) / (x * (c * x + d) + e)); }
float3 packGradeVV(float3 scene, float2 uv, float2 pix, float2 dims) { float3 c = vvAces(scene * 0.75); c = desat(c, 0.35); c = pow(max(c, 0.0), 1.0 / 2.2); return dreadGrain(c, pix); }
// @include ../../_shared/ColorPostVV.fragment.hlsli
