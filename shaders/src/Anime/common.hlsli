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
