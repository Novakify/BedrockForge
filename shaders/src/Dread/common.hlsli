// Bedrock Forge "Dread" shaderpack - horror atmosphere for From The Fog-style play (original work, MIT).
// Darker nights, cold desaturated shadows, warm-but-weak torchlight, heavy grey ground fog, faint film grain.
float luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
float3 desat(float3 c, float keep) { float l = luma(c); return lerp(float3(l, l, l), c, keep); }
float dreadHash(float2 p) { float3 p3 = frac(p.xyx * float3(0.1031, 0.1030, 0.0973)); p3 += dot(p3, p3.yzx + 33.33); return frac((p3.x + p3.y) * p3.z); }
// film grain from the screen position (static per pixel; subtle)
float3 dreadGrain(float3 c, float2 px) { return c + (dreadHash(floor(px)) - 0.5) * 0.035 * (1.0 - luma(c) * 0.6); }

// Lightmap -> horror light: crush low light toward black, cold blue in the dark, keep torch warmth but weaker.
// uv.x = block light, uv.y = sky light (vanilla lightmap layout)
float3 dreadLight(float3 light, float2 uv) {
    float  L     = max(max(light.r, light.g), light.b);
    float  crush = pow(saturate(L), 1.65);
    float3 cold  = lerp(float3(0.42, 0.50, 0.70), float3(1.0, 1.0, 1.0), saturate(crush * 1.6));
    float  torch = smoothstep(0.55, 1.0, uv.x) * (1.0 - smoothstep(0.6, 1.0, uv.y) * 0.5);
    float3 warm  = lerp(float3(1.0, 1.0, 1.0), float3(1.08, 0.82, 0.58), torch);
    return light * (crush / max(L, 1e-3)) * cold * warm;
}

// Thicker fog: vanilla fog factor curved up + extra low-lying mist, tinted toward a dark desaturated grey.
float3 dreadFog(float3 c, float3 fogColor, float fog) {
    float  f  = saturate(pow(saturate(fog), 0.45) * 1.1 + 0.04);
    float3 fc = desat(fogColor, 0.35) * 0.78 + float3(0.02, 0.025, 0.03);
    return lerp(c, fc, f);
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
