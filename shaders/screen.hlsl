// Renderer Foundation - LyricsForge video screen: the last pass that puts a captured window (optionally cleaned up and
// upscaled by Anime4K, shaders/anime4k.hlsl) onto the fixed 1920x1080 screen texture the glyph pass samples (t21).
// Original code for Renderer Foundation / Bedrock Forge.
//
// b0: outSize = 1920 x 1080, pt = 1 / outSize, rect = where the picture sits on the screen (uv x0, y0, w, h):
//     letterboxed so any aspect fits a 16:9 screen with black bars, never stretched.
// t0 = the picture (any size), s0 point, s1 linear clamp.
cbuffer Fit : register(b0) {
    float2 outSize;
    float2 pt;
    float4 rect;
};
Texture2D    gSrc : register(t0);
SamplerState sPt  : register(s0);
SamplerState sLin : register(s1);

float4 VSFit(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
}

// Catmull-Rom in 9 bilinear taps (the usual trick: the middle two taps of each axis merge into one linear fetch).
// Sharper than bilinear when a small video is blown up and Anime4K is off.
float3 catmullRom(float2 uv, float2 size) {
    float2 sp = uv * size;
    float2 tc = floor(sp - 0.5) + 0.5;
    float2 f  = sp - tc;
    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);
    float2 w12 = w1 + w2;
    float2 o12 = w2 / w12;
    float2 t0  = (tc - 1.0) / size;
    float2 t3  = (tc + 2.0) / size;
    float2 t12 = (tc + o12) / size;
    float3 c = 0;
    c += gSrc.SampleLevel(sLin, float2(t0.x, t0.y), 0).rgb * w0.x * w0.y;
    c += gSrc.SampleLevel(sLin, float2(t12.x, t0.y), 0).rgb * w12.x * w0.y;
    c += gSrc.SampleLevel(sLin, float2(t3.x, t0.y), 0).rgb * w3.x * w0.y;
    c += gSrc.SampleLevel(sLin, float2(t0.x, t12.y), 0).rgb * w0.x * w12.y;
    c += gSrc.SampleLevel(sLin, float2(t12.x, t12.y), 0).rgb * w12.x * w12.y;
    c += gSrc.SampleLevel(sLin, float2(t3.x, t12.y), 0).rgb * w3.x * w12.y;
    c += gSrc.SampleLevel(sLin, float2(t0.x, t3.y), 0).rgb * w0.x * w3.y;
    c += gSrc.SampleLevel(sLin, float2(t12.x, t3.y), 0).rgb * w12.x * w3.y;
    c += gSrc.SampleLevel(sLin, float2(t3.x, t3.y), 0).rgb * w3.x * w3.y;
    return saturate(c);
}

float4 PSFit(float4 pos : SV_Position) : SV_Target {
    float2 uv  = pos.xy * pt;
    float2 loc = (uv - rect.xy) / max(rect.zw, 1e-6);
    if (any(loc < 0.0) || any(loc > 1.0)) return float4(0, 0, 0, 1); // the bars
    float w, h;
    gSrc.GetDimensions(w, h);
    float2 size  = float2(w, h);
    float2 scale = rect.zw * outSize / size; // screen px per picture px
    float3 c;
    if (min(scale.x, scale.y) >= 1.0) {
        c = catmullRom(loc, size);
    } else { // shrinking (a 1440p / 4K window): 4 linear taps a screen pixel apart, so it doesn't shimmer
        float2 d = 0.25 / (scale * size);
        c  = gSrc.SampleLevel(sLin, loc + float2(-d.x, -d.y), 0).rgb;
        c += gSrc.SampleLevel(sLin, loc + float2(d.x, -d.y), 0).rgb;
        c += gSrc.SampleLevel(sLin, loc + float2(-d.x, d.y), 0).rgb;
        c += gSrc.SampleLevel(sLin, loc + float2(d.x, d.y), 0).rgb;
        c *= 0.25;
    }
    return float4(saturate(c), 1);
}
