// Bedrock Forge "Anime" shaderpack - shared helpers (original work, MIT).
float luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
float3 saturateColor(float3 c, float amount) { float l = luma(c); return lerp(float3(l, l, l), c, amount); }

// Cel lighting: quantise light into soft bands and tint shadows cool, like hand-painted anime backgrounds.
float3 toonLight(float3 light) {
    float L     = max(max(light.r, light.g), light.b);
    float bands = 4.0;
    float f     = L * bands;
    float q     = (floor(f) + smoothstep(0.80, 1.0, frac(f))) / bands;
    q           = max(q, L * 0.35);                       // keep caves readable
    float3 tint = lerp(float3(0.78, 0.86, 1.10), float3(1.0, 1.0, 1.0), saturate(q * 1.4));
    return light * (q / max(L, 1e-3)) * tint;
}

// Vanilla distance-dither fade (matches RenderDragon's Actor/RenderChunk alpha-test code): true = discard this pixel.
//   fwd = view forward (u_view row 2), cam = camera position, clip = clip-space position from the vertex stage,
//   p2 = DitherParams2[k] (x start, y end, z cell size), p = DitherParams (x,y = screen size)
float bfDitherHash(float2 q) { return frac(q.x * 0.5 + q.y * q.y * 0.75); }
bool bfDitherKill(float3 worldPos, float3 cam, float3 fwd, float4 clip, float4 p2, float4 p) {
    float3 n    = -normalize(fwd);
    float  f    = saturate((dot(n, worldPos - cam) - p2.x) / (p2.y - p2.x));
    float  fade = f * f * (3.0 - 2.0 * f);
    float2 sp   = (clip.xy / clip.w) * 0.5 + 0.5;
    float2 px   = floor(sp * p.xy / p2.z) * p2.z;
    float  t    = (bfDitherHash(floor(px * 0.25)) * 0.25 + bfDitherHash(floor(px * 0.5))) * 0.25 + 0.0078125
                + bfDitherHash(floor(px));
    return fade <= t;
}
