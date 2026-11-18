// Vibrant Visuals final pass (ColorPostProcessing) for Bedrock Forge shaderpacks.
// VV draws the world with its own deferred PBR materials, then this pass turns the HDR scene into the picture.
// Kept 1:1 from vanilla 1.26 (decoded from DXIL 2026-10-28): pre-exposure undo, auto exposure (average luminance,
// key value modes 0/1/2), saturate, then the rasterized layer (UI/hand drawn outside the deferred path) composited
// premultiplied on top. REPLACED: vanilla tonemapper + VV colour grading -> the pack's own final grade
// (packGradeVV, defined by the pack before including this file) - so VV lighting/shadows/reflections stay and the
// colours/tonemap/vignette/grain are the shaderpack's.
cbuffer FragmentUniforms : register(b1) {
    float4 ColorGrading_Misc;
    float4 GenericTonemapperContrastAndScaleAndOffsetAndCrosstalk;
    float4 ExposureCompensation;                 // x = key mode, y = constant key, z > 0.5 = auto exposure
    float4 ColorGrading_Offset_Highlights;
    float4 ColorGrading_Gamma_Highlights;
    float4 ColorGrading_Contrast_Highlights;
    float4 ColorGrading_Saturation_Highlights;
    float4 ColorGrading_Contrast_Midtones;
    float4 ColorGrading_Contrast_Shadows;
    float4 ColorGrading_Gain_Highlights;
    float4 ColorGrading_Gain_Midtones;
    float4 ColorGrading_Gain_Shadows;
    float4 ColorGrading_Offset_Shadows;
    float4 ColorGrading_Offset_Midtones;
    float4 ColorGrading_Gamma_Shadows;
    float4 ColorGrading_Gamma_Midtones;
    float4 GenericTonemapperCrosstalkParams;
    float4 ColorGrading_Misc2;
    float4 TonemapParams0;                       // z > 0: scene is pre-exposed
    float4 ColorGrading_Saturation_Midtones;
    float4 ColorGrading_Saturation_Shadows;
    float4 ColorGrading_Temperature_Params;
    float4 LuminanceMinMaxAndWhitePointAndMinWhitePoint;
    float4 RasterizedColorEnabled;
};
SamplerState s_AverageLuminanceSampler           : register(s0);
SamplerState s_ColorTextureSampler               : register(s1);
SamplerState s_CustomExposureCompensationSampler : register(s2);
SamplerState s_PreExposureLuminanceSampler       : register(s3);
SamplerState s_RasterizedColorSampler            : register(s5);
Texture2D s_AverageLuminanceTexture           : register(t0);
Texture2D s_ColorTextureTexture               : register(t1);
Texture2D s_CustomExposureCompensationTexture : register(t2);
Texture2D s_PreExposureLuminanceTexture       : register(t3);
Texture2D s_RasterizedColorTexture            : register(t5);

#ifndef VV_DEHAZE
#define VV_DEHAZE 0.0      // per-pack: remove a grey veil of VV_DEHAZE x mid-grey (VV's fog/sky scattering reads milky)
#endif
#ifndef VV_WHITE_BALANCE
#define VV_WHITE_BALANCE float3(1.0, 1.0, 1.0)   // per-pack tint correction (VV's sky/fog scatter reads cyan)
#endif
#ifndef VV_SATURATION
#define VV_SATURATION 1.0   // per-pack: VV's PBR albedo reads more saturated than the pack's Fancy lighting
#endif
#ifndef VV_HURT_RATIO
#define VV_HURT_RATIO 1.8   // impact frame: red / max(green, blue) above this = the hit entity (black)
#endif
#ifndef VV_EXPOSURE_BIAS
#define VV_EXPOSURE_BIAS 1.0   // per-pack brightness trim on top of VV's auto exposure
#endif

float4 main(PSIn i) : SV_Target {
    float2 uv = i.texcoord0.xy;
    float3 c = s_ColorTextureTexture.Sample(s_ColorTextureSampler, uv).rgb;
    if (TonemapParams0.z > 0.0)
        c /= 0.18 / s_PreExposureLuminanceTexture.Sample(s_PreExposureLuminanceSampler, float2(0.5, 0.5)).r + 0.0001;
    float avg = 0.18;
    if (ExposureCompensation.z > 0.5)
        avg = min(max(s_AverageLuminanceTexture.Sample(s_AverageLuminanceSampler, float2(0.5, 0.5)).r,
                      LuminanceMinMaxAndWhitePointAndMinWhitePoint.x), LuminanceMinMaxAndWhitePointAndMinWhitePoint.y);
    int mode = (int)ExposureCompensation.x;
    float key = ExposureCompensation.y;
    if (mode == 1) key = 1.03 - 2.0 / (log2(avg + 1.0) * 0.30103 + 2.0);
    else if (mode > 1) {
        float lo = LuminanceMinMaxAndWhitePointAndMinWhitePoint.x, hi = LuminanceMinMaxAndWhitePointAndMinWhitePoint.y;
        float t = lo == hi ? 0.5 : (log2(avg) - log2(lo)) / (log2(hi) - log2(lo));
        key = s_CustomExposureCompensationTexture.Sample(s_CustomExposureCompensationSampler, float2(t, 0.5)).r;
    }
    float3 scene = max(c, 0.0) * (key / max(avg, 1e-5)) * VV_EXPOSURE_BIAS;
    float3 raw = scene;      // before the pack's VV corrections: the impact test reads the hurt overlay here
    scene *= VV_WHITE_BALANCE;
    { float l = dot(scene, float3(0.2126, 0.7152, 0.0722)); scene = max(lerp(l.xxx, scene, VV_SATURATION), 0.0); }
    if (VV_DEHAZE > 0.0)      // subtract a grey veil (black-point move); auto exposure can't undo it because it runs after
        scene = max(scene - VV_DEHAZE * 0.18 * VV_EXPOSURE_BIAS, 0.0);
    float2 dims; s_ColorTextureTexture.GetDimensions(dims.x, dims.y);
    float3 o = saturate(packGradeVV(scene, uv, uv * dims, dims));
#ifndef BF_NO_IMPACT_VV
    // Impact frame (BedrockForgeImpact BP): while the frame is on, the BP sets the player's VV colour-grading midtones
    // gain to the marker (4, 0.05, 4) - vanilla gains are always 1. The game blends grading overrides in over a few
    // frames (measured ~50% after 0.5 s), so test thresholds, not the exact value. Whole world white, the entity that
    // was hit (red hurt overlay) black. Red-dominant pixels = hurt overlay (red blocks go black too).
    if (ColorGrading_Gain_Midtones.x > 1.6 && ColorGrading_Gain_Midtones.z > 1.6 && ColorGrading_Gain_Midtones.y < 0.8) {
        // Hurt overlay: tools/HurtColorGen makes it opaque red; VV still blends it ~50% into the albedo, so the hit
        // entity is clearly red-dominant before grading. Strongly red natural blocks (red wool, redstone) match too.
        // Ratio on the linear scene: independent of the pack's VV_EXPOSURE_BIAS (a low bias, e.g. Insanity 0.12, pushed
        // the old h.r > 0.02 floor above the pig's red). Tiny floor only rejects black pixels.
        bool hurt = raw.r > 0.002 && raw.r > VV_HURT_RATIO * max(raw.g, raw.b);
        o = hurt ? float3(0.0, 0.0, 0.0) : float3(1.0, 1.0, 1.0);
    }
#endif
    if (RasterizedColorEnabled.x > 0.0) {
        float4 r = s_RasterizedColorTexture.Sample(s_RasterizedColorSampler, uv);
        o = (1.0 - r.a) * o + r.rgb;
    }
#ifdef BF_DEBUG_GRADING   // #define it in a pack Fragment.hlsl to draw the grading uniforms as bars
    if (uv.y < 0.06) {   // debug: 4 bars = Offset_Midtones x,y,z,w (x40), then Offset_Shadows, Offset_Highlights, Gain_Midtones
        int k = (int)(uv.x * 16.0);
        float4 a = k < 4 ? ColorGrading_Offset_Midtones : k < 8 ? ColorGrading_Offset_Shadows : k < 12 ? ColorGrading_Offset_Highlights : ColorGrading_Gain_Midtones;
        float v = a[k & 3]; v = k < 12 ? v * 40.0 : v * 0.5;
        o = v < 0.0 ? float3(saturate(-v), 0, 0) : float3(0, saturate(v), saturate(v - 1.0));
        if (frac(uv.x * 16.0) < 0.04) o = float3(1, 1, 1);
    }
#endif
    return float4(o, 1.0);
}
