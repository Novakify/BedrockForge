// Novak Glass particles (installed in the Novak Glass pack only; the Cloud Sea pack keeps vanilla particles).
// Warm sparks, flames and explosion fire turn neon - orange to hot magenta, gold to electric cyan - and glow a little
// brighter; block-break debris (the block atlas) keeps its colours with a cool glass sheen, like shards.
Texture2D    s_ParticleTextureTexture : register(t0);
SamplerState s_ParticleTextureSampler : register(s0);
float3 ngNeonWarmP(float3 d, out float w) {
    float mx = max(d.r, max(d.g, d.b)), mn = min(d.r, min(d.g, d.b));
    w = 0.0;
    if (mx <= 1e-5 || !(d.r >= d.g && d.g >= d.b)) return d;
    float sat = (mx - mn) / mx;
    float h   = 60.0 * (d.g - d.b) / max(d.r - d.b, 1e-5);
    w = smoothstep(-1.0, 4.0, h) * (1.0 - smoothstep(46.0, 53.0, h)) * smoothstep(0.15, 0.40, sat);   // (fire deep reds too: seen half orange)
    float3 neon = lerp(float3(1.00, 0.22, 0.78), float3(0.15, 0.88, 1.00), saturate((h - 16.0) / 28.0));
    float  L = dot(d, float3(0.2126, 0.7152, 0.0722));
    return lerp(d, neon * (L / dot(neon, float3(0.2126, 0.7152, 0.0722))), w);
}
float4 main(PSIn i) : SV_Target {
    float4 t = s_ParticleTextureTexture.Sample(s_ParticleTextureSampler, i.texcoord0) * i.color0;
#if defined(PASS_ALPHATEST)
    if (t.a < 0.5) discard;
#endif
    uint tw, th, lv0;
    s_ParticleTextureTexture.GetDimensions(0, tw, th, lv0);
    float3 c = t.rgb;
    [branch] if (tw <= 512) {
        float w;
        float3 lc = ngNeonWarmP(csLin(c), w);
        c = csEnc(lc * (1.0 + 0.6 * w));
    } else {
        c = lerp(c, c * float3(0.92, 1.00, 1.10) + 0.04, 0.35);   // shards
    }
    return float4(c, t.a);
}
