// Sky vertex shader - exact rebuild of vanilla 1.26 Sky.Vertex (decoded from DXIL 2026-10-27) + "full sky" fold.
// Vanilla: worldPos = u_model * pos; clip = u_viewProj * worldPos; colour = lerp(SkyColor, FogColor, COLOR.x);
// TEXCOORD3 = worldPos (camera-relative). The sky mesh is a flat plane ABOVE the camera (local y = 0, its height comes
// from u_model's translation; COLOR.x 0 = zenith .. 1 = rim), so a pack's sky only covered the upper half.
// Fold (v2, in WORLD space - v1 folded local space, where y is 0, so it never ran and lost the vertical direction):
// keep each vertex's distance, move its elevation from +90 deg (COLOR.x 0) to -SKY_FOLD_BELOW deg (COLOR.x 1).
// Planes below the camera (world y <= 0) stay exactly vanilla.
cbuffer VertexUniforms : register(b0) {
    column_major float4x4 u_viewProj : packoffset(c0);
    column_major float4x4 u_model[4] : packoffset(c4);
    float4 FogColor                  : packoffset(c20);
    float4 SkyColor                  : packoffset(c21);
};
#ifndef SKY_FOLD_BELOW
#define SKY_FOLD_BELOW 60.0
#endif
VSOut main(VSIn i) {
    VSOut o = (VSOut)0;
    float3 w = mul(u_model[0], float4(i.position0, 1.0)).xyz;
    if (w.y > 0.0) {
        float  d = length(w);
        float  r = length(w.xz);
        float2 h = r > 1e-5 ? w.xz / r : float2(1.0, 0.0);
        float  e = radians(90.0) - saturate(i.color0.x) * radians(90.0 + SKY_FOLD_BELOW);
        w = d * float3(h.x * cos(e), sin(e), h.y * cos(e));
    }
    o.position  = mul(u_viewProj, float4(w, 1.0));
    o.color0    = lerp(SkyColor, FogColor, i.color0.x);
    o.texcoord0 = i.texcoord0;
    o.texcoord3 = w;
    return o;
}
