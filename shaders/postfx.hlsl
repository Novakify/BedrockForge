// Renderer Foundation - full-screen post effects on the swapchain image (driven by Bedrock Forge shaderpacks).
// Compiled offline to DXIL by tools/gen_postfx.py -> src/PostFxShaders.h.
//
// Effects follow the Java packs they come from:
//   bloom  - Hysteria / Photon: 6 downsampled tiles, gaussian blurred, averaged and mixed into the scene
//   CA     - Cursed Fog final.fsh (r/b offset by distance from centre) or Insanity final.glsl (4 taps toward centre)
//   CAS    - AMD FidelityFX Contrast Adaptive Sharpening (Hysteria CAS_INTENSITY)
//   motion - Cursed Fog composite4.fsh MotionBlur(): 9 samples along the camera velocity. Bedrock gives no depth
//            buffer here, so the velocity comes from the camera rotation only (turning), not from walking.

cbuffer Params : register(b0) {
    float4 p0;   // xy = 1/target size, z = bloom mix, w = bloom tile count
    float4 p1;   // x = ca, y = ca style, z = cas, w = motion blur strength
    float4 p2;   // x = frame, y = aspect (w/h), z = tan(fovy/2), w = flags (1 = linearize sRGB)
    float4 m0;   // rows of the previous-from-current camera rotation (3x3)
    float4 m1;
    float4 m2;
};

Texture2D t0 : register(t0);
Texture2D t1 : register(t1);
Texture2D t2 : register(t2);
Texture2D t3 : register(t3);
Texture2D t4 : register(t4);
Texture2D t5 : register(t5);
Texture2D t6 : register(t6);
Texture2D t7 : register(t7);
SamplerState sLinear : register(s0);

struct V2P { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

V2P VSMain(uint id : SV_VertexID) {
    V2P o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.uv  = uv;
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

float3 toLinear(float3 c) { c = saturate(c); return lerp(c / 12.92, pow((c + 0.055) / 1.055, 2.4), step(0.04045, c)); }
float3 toSrgb(float3 c) {
    c = max(c, 0.0);
    return saturate(1.055 * pow(c, 1.0 / 2.4) - 0.055) * step(0.0031308, c) + (12.92 * c) * step(c, 0.0031308);
}
float3 lin(float3 c) { return p2.w > 0.5 ? toLinear(c) : c; }

// ---- bloom: downsample (13 taps, Jimenez 2014) ----
float4 PSDownFirst(V2P i) : SV_Target {
    float2 px = p0.xy;  // texel of the SOURCE
    float3 a = lin(t0.SampleLevel(sLinear, i.uv + px * float2(-2, -2), 0).rgb);
    float3 b = lin(t0.SampleLevel(sLinear, i.uv + px * float2( 0, -2), 0).rgb);
    float3 c = lin(t0.SampleLevel(sLinear, i.uv + px * float2( 2, -2), 0).rgb);
    float3 d = lin(t0.SampleLevel(sLinear, i.uv + px * float2(-2,  0), 0).rgb);
    float3 e = lin(t0.SampleLevel(sLinear, i.uv, 0).rgb);
    float3 f = lin(t0.SampleLevel(sLinear, i.uv + px * float2( 2,  0), 0).rgb);
    float3 g = lin(t0.SampleLevel(sLinear, i.uv + px * float2(-2,  2), 0).rgb);
    float3 h = lin(t0.SampleLevel(sLinear, i.uv + px * float2( 0,  2), 0).rgb);
    float3 k = lin(t0.SampleLevel(sLinear, i.uv + px * float2( 2,  2), 0).rgb);
    float3 l = lin(t0.SampleLevel(sLinear, i.uv + px * float2(-1, -1), 0).rgb);
    float3 m = lin(t0.SampleLevel(sLinear, i.uv + px * float2( 1, -1), 0).rgb);
    float3 n = lin(t0.SampleLevel(sLinear, i.uv + px * float2(-1,  1), 0).rgb);
    float3 o = lin(t0.SampleLevel(sLinear, i.uv + px * float2( 1,  1), 0).rgb);
    float3 r = e * 0.125 + (a + c + g + k) * 0.03125 + (b + d + f + h) * 0.0625 + (l + m + n + o) * 0.125;
    return float4(r, 1.0);
}
float4 PSDown(V2P i) : SV_Target {
    float2 px = p0.xy;
    float3 a = t0.SampleLevel(sLinear, i.uv + px * float2(-2, -2), 0).rgb;
    float3 b = t0.SampleLevel(sLinear, i.uv + px * float2( 0, -2), 0).rgb;
    float3 c = t0.SampleLevel(sLinear, i.uv + px * float2( 2, -2), 0).rgb;
    float3 d = t0.SampleLevel(sLinear, i.uv + px * float2(-2,  0), 0).rgb;
    float3 e = t0.SampleLevel(sLinear, i.uv, 0).rgb;
    float3 f = t0.SampleLevel(sLinear, i.uv + px * float2( 2,  0), 0).rgb;
    float3 g = t0.SampleLevel(sLinear, i.uv + px * float2(-2,  2), 0).rgb;
    float3 h = t0.SampleLevel(sLinear, i.uv + px * float2( 0,  2), 0).rgb;
    float3 k = t0.SampleLevel(sLinear, i.uv + px * float2( 2,  2), 0).rgb;
    float3 l = t0.SampleLevel(sLinear, i.uv + px * float2(-1, -1), 0).rgb;
    float3 m = t0.SampleLevel(sLinear, i.uv + px * float2( 1, -1), 0).rgb;
    float3 n = t0.SampleLevel(sLinear, i.uv + px * float2(-1,  1), 0).rgb;
    float3 o = t0.SampleLevel(sLinear, i.uv + px * float2( 1,  1), 0).rgb;
    float3 r = e * 0.125 + (a + c + g + k) * 0.03125 + (b + d + f + h) * 0.0625 + (l + m + n + o) * 0.125;
    return float4(r, 1.0);
}

// ---- bloom: separable gaussian (Hysteria gaussian0/1: 9 taps via linear sampling) ----
static const float gw[3] = {0.2270270270, 0.3162162162, 0.0702702703};
static const float go[3] = {0.0, 1.3846153846, 3.2307692308};
float4 PSBlur(V2P i) : SV_Target {
    float2 dir = p0.xy;  // texel * axis
    float3 c = t0.SampleLevel(sLinear, i.uv, 0).rgb * gw[0];
    [unroll] for (int k = 1; k < 3; ++k) {
        c += t0.SampleLevel(sLinear, i.uv + dir * go[k], 0).rgb * gw[k];
        c += t0.SampleLevel(sLinear, i.uv - dir * go[k], 0).rgb * gw[k];
    }
    return float4(c, 1.0);
}

// ---- composite ----
float3 scene(float2 uv) { return lin(t0.SampleLevel(sLinear, uv, 0).rgb); }

float3 sceneCA(float2 uv) {
    float ca = p1.x;
    if (ca <= 0.0) return scene(uv);
    if (p1.y < 0.5) {
        // Cursed Fog: centeredCoord = (uv - .5) * .925 + .5; red at uv + (ab,ab)*d, blue at uv - (ab,ab)*d (GL uv, y up)
        float d = length((uv - 0.5) * 0.925);
        float2 o = float2(ca, -ca) * d;   // D3D uv has y down
        return float3(scene(uv + o).r, scene(uv).g, scene(uv - o).b);
    }
    // Insanity: caScale = (1/aspect, 1); four taps pulled toward / pushed from the centre
    float2 s = float2(1.0 / p2.y, 1.0);
    float3 c = scene(uv) * float3(0.0, 1.0, 0.0);
    c += scene(lerp(uv, 0.5, s * -ca)).rgb * float3(1.0, 0.0, 0.0);
    c += scene(lerp(uv, 0.5, s * -ca * 0.5)).rgb * float3(0.5, 0.5, 0.0);
    c += scene(lerp(uv, 0.5, s * ca * 0.5)).rgb * float3(0.0, 0.5, 0.5);
    c += scene(lerp(uv, 0.5, s * ca)).rgb * float3(0.0, 0.0, 1.0);
    return c / float3(1.5, 2.0, 1.5);
}

// AMD FidelityFX CAS (reference single-pass form, no scaling)
float3 cas(float2 uv, float sharp) {
    float2 px = p0.xy;
    float3 a = scene(uv + px * float2(-1, -1)), b = scene(uv + px * float2(0, -1)), c = scene(uv + px * float2(1, -1));
    float3 d = scene(uv + px * float2(-1,  0)), e = scene(uv),                      f = scene(uv + px * float2(1,  0));
    float3 g = scene(uv + px * float2(-1,  1)), h = scene(uv + px * float2(0,  1)), k = scene(uv + px * float2(1,  1));
    float3 mn = min(min(min(d, e), min(f, b)), h);
    float3 mn2 = min(mn, min(min(a, c), min(g, k)));
    mn += mn2;
    float3 mx = max(max(max(d, e), max(f, b)), h);
    float3 mx2 = max(mx, max(max(a, c), max(g, k)));
    mx += mx2;
    float3 amp = saturate(min(mn, 2.0 - mx) / max(mx, 1e-5));
    amp = sqrt(amp);
    float peak = -1.0 / lerp(8.0, 5.0, saturate(sharp));
    float3 w = amp * peak;
    float3 rcpW = 1.0 / (1.0 + 4.0 * w);
    return saturate((b * w + d * w + f * w + h * w + e) * rcpW);
}

float ign(float2 p) { return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715)))); }

float4 PSComposite(V2P i) : SV_Target {
    float2 uv = i.uv;
    float3 col;
    float mb = p1.w;
    if (mb > 0.0) {
        // Cursed Fog MotionBlur(): currentPosition - previousPosition in NDC, velocity/(1+|v|)*strength*0.02,
        // start at uv - v*(3.5 + dither), 9 samples stepping +v
        float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
        float tany = p2.z, tanx = tany * p2.y;
        float3 d  = float3(ndc.x * tanx, ndc.y * tany, 1.0);
        float3 dp = float3(dot(m0.xyz, d), dot(m1.xyz, d), dot(m2.xyz, d));
        float2 prev = dp.z > 1e-3 ? float2(dp.x / dp.z / tanx, dp.y / dp.z / tany) : ndc;
        float2 v = ndc - prev;
        v = v / (1.0 + length(v)) * mb * 0.02;
        v.y = -v.y;  // NDC y up -> uv y down
        float dither = ign(i.pos.xy + p2.x * 5.588238);
        float2 c = uv - v * (3.5 + dither);
        float2 lim = 2.0 * p0.xy;
        col = 0.0;
        [loop] for (int k = 0; k < 9; ++k, c += v) col += sceneCA(clamp(c, lim, 1.0 - lim));
        col /= 9.0;
    } else if (p1.z > 0.0) {
        col = cas(uv, p1.z);
    } else {
        col = sceneCA(uv);
    }
    if (p0.z > 0.0) {
        // Hysteria get_bloom(): average of the tiles (radius 1 -> equal weights), scene = mix(scene, bloom, 0.12*I)
        int n = clamp(int(p0.w), 1, 6);
        float3 b = t2.SampleLevel(sLinear, uv, 0).rgb;
        if (n > 1) b += t3.SampleLevel(sLinear, uv, 0).rgb;
        if (n > 2) b += t4.SampleLevel(sLinear, uv, 0).rgb;
        if (n > 3) b += t5.SampleLevel(sLinear, uv, 0).rgb;
        if (n > 4) b += t6.SampleLevel(sLinear, uv, 0).rgb;
        if (n > 5) b += t7.SampleLevel(sLinear, uv, 0).rgb;
        b /= float(n);
        col = lerp(col, b, p0.z);
    }
    return float4(p2.w > 0.5 ? toSrgb(col) : col, 1.0);
}
