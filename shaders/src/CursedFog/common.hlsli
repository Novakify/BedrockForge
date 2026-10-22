// Cursed Fog for Bedrock - a line-by-line HLSL port of "Cursed Fog" V1.0.10 by LoLip_p (Iris/OptiFine shaderpack),
// licensed CC-BY-4.0 (https://creativecommons.org/licenses/by/4.0/). Changes: GLSL -> HLSL, Java's deferred
// gbuffers/composite/final chain folded into RenderDragon's forward materials, and the Bedrock substitutes listed in
// shaders/src/CursedFog/PORTING.md (no shadow map, no worldTime, no previous-frame buffers).
//
// Settings = the pack's defaults (settings.glsl, profile STANDARD).
#define RED_FILTER 1
#define FLICKERING_LIGHT 1
#define SHADING 1
#define ENABLE_SHADOW 1
#define LIGHT_ABSORPTION 0.50
#define SHADOW_TRANSPARENCY 0.05
#define degree_night_darkness 0.05
#define FOG_DISTANCE 3.0
#define GRAIN 1
#define GRAIN_STRENGTH 0.05
#define BLACK_STRIPES 1
#define BLACK_STRIPES_WIDTH 0.10
#define BLACK_STRIPES_SOFT 0.01
#define CRT_MASK 1
#define CRT_SCALE 2.0
#define R_DIRECTION 1
#define DIRECTION_STRENGTH 0.05
#define SATURATION 0.45
#define CONTRAST 0.95
#define BRIGHTNESS 0.00
#define CLOUDS 0

static const float3 CF_RED = float3(1.0, 0.2, 0.2);

float cfMod(float x, float y) { return x - y * floor(x / y); }

// ---- composite.fsh -------------------------------------------------------------------------------------------
float AdjustLightmapTorch(float torchLight, float frameTimeCounter) {
    const float K = 2.0f;
    const float P = 5.0f;
#if FLICKERING_LIGHT == 0
    float sinMod = 1;
#else
    float fastSin = sin(frameTimeCounter * 4);
    float slowSin = sin(frameTimeCounter * 2);
    float sinMod  = 0.925 + 0.075 * (fastSin + slowSin) / 2.0;
#endif
    return K * pow(max(torchLight * sinMod, 0.0), P);
}
float AdjustLightmapSky(float sky) {
    float sky_2 = sky * sky;
    return sky_2 * sky_2;
}
float2 AdjustLightmap(float2 Lightmap, float t) { return float2(AdjustLightmapTorch(Lightmap.x, t), AdjustLightmapSky(Lightmap.y)); }

float3 GetLightmapColor(float2 Lightmap, float state, float rainStrength, float t) {
    Lightmap         = AdjustLightmap(Lightmap, t);
    float3 SkyColor  = float3(0.1f, 0.05f, 0.09f);
#if ENABLE_SHADOW == 0
    SkyColor = float3(1, 1, 1) * 0.1f;
#endif
    float3 TorchLighting = Lightmap.x * float3(1.0f, 1.0f, 1.0f);
    float3 SkyLighting   = Lightmap.y * SkyColor * state * (1 - rainStrength);
    float  occ           = (Lightmap.y * LIGHT_ABSORPTION * state * (1 - rainStrength));
    float  torchMask     = smoothstep(-0.2, 0.8, Lightmap.x);
    TorchLighting -= occ * torchMask;
    return saturate(TorchLighting * float3(1.0f, 0.45f, 0.3f)) + SkyLighting;
}

// Java: Iris lightmap coords (gl_MultiTexCoord1 through the texture matrix) then the pack's remap; Bedrock gives level/15.
float2 cfLightmap(float2 bedrockLevels01) {
    float2 raw = (bedrockLevels01 * 15.0 + 0.5) / 16.0;
    return raw * 33.05f / 32.0f - 1.05f / 32.0f;
}

// composite.fsh, both branches. isEntity = colortex3 "normal" flag (entities / hand write 1, terrain 0).
// sunShadow = GetShadow() (the shadow map), lightBrightness = the gbuffer N.L term.
float3 cfComposite(float3 albedoTex, float2 Lightmap, float lightBrightness, float sunShadow, bool isEntity, float state,
                   float rainStrength, float t) {
    float3 Albedo           = pow(max(albedoTex, 0.0), 2.2f);
    float3 lightBrightnessV = float3(0.1f, 0.1f, 0.1f);
    float3 ShadowColor      = float3(1.0f, 1.0f, 1.0f);
    float3 LightmapColor    = GetLightmapColor(Lightmap, state, rainStrength, t);
#if SHADING == 1
    lightBrightness *= state;
    lightBrightness *= 1.0f - rainStrength;
    lightBrightness += (rainStrength - 0.5f) * (1.0f - state) * rainStrength;
    lightBrightnessV = float3(lightBrightness, lightBrightness, lightBrightness) * state;
    lightBrightnessV += float3(0.2, 0.2, 0.5) * (1.0f - state);
#endif
    lightBrightnessV *= AdjustLightmap(Lightmap, t).y;
    if (isEntity) {
#if ENABLE_SHADOW == 1
        float3 originalShadow         = sunShadow.xxx;
        float  shadow_transperency_coef = lerp(degree_night_darkness + 0.25f, SHADOW_TRANSPARENCY, state);
        ShadowColor = lerp(originalShadow * (shadow_transperency_coef / 0.75), originalShadow, state);
        float3 colorRainDayOrNight = lerp(float3(0.2, 0.2, 0.2), float3(1, 1, 1), state);
        ShadowColor = lerp(ShadowColor, colorRainDayOrNight, rainStrength);
        ShadowColor *= AdjustLightmap(Lightmap, t).y;
        ShadowColor += shadow_transperency_coef;
#endif
        LightmapColor *= lerp(5, 1, state);
        lightBrightnessV += SHADOW_TRANSPARENCY;
        lightBrightnessV += 0.025f;
        Albedo *= clamp(state, SHADOW_TRANSPARENCY, 1.0f);
        Albedo *= (LightmapColor + lightBrightnessV * ShadowColor);
        return Albedo;
    }
    float shadow_transperency_coef = lerp(degree_night_darkness - 0.025f, SHADOW_TRANSPARENCY, state);
#if ENABLE_SHADOW == 1
    {
        float3 originalShadow = sunShadow.xxx;
        ShadowColor = lerp(originalShadow * (shadow_transperency_coef / 0.75), originalShadow, state);
        float3 colorRainDayOrNight = lerp(float3(0.2, 0.2, 0.2), float3(1, 1, 1), state);
        ShadowColor = lerp(ShadowColor, colorRainDayOrNight, rainStrength);
        ShadowColor *= AdjustLightmap(Lightmap, t).y;
    }
#endif
    ShadowColor *= lerp(float3(1, 1, 1), lightBrightnessV, state);
    ShadowColor += shadow_transperency_coef;
    ShadowColor *= lerp(lightBrightnessV, float3(1, 1, 1), state);
    return Albedo * (LightmapColor + ShadowColor);
}

// ---- composite1.fsh (fog) --------------------------------------------------------------------------------------
float FogExp2(float viewDistance, float density) {
    float factor = viewDistance * (density / sqrt(log(2.0f)));
    return exp(-factor * factor);
}
float3 cfFog(float3 color, float viewLength, float far, float3 fogColor, float state, float rainStrength) {
    float distance  = viewLength / far;
    float fogFactor = 1 - saturate(FogExp2(distance, FOG_DISTANCE));
    float3 fog_color = fogColor;
    float average    = (fog_color.r + fog_color.g + fog_color.b) / 3.0;
    fog_color = lerp(average.xxx, fog_color, 0.35);
#if RED_FILTER == 1
    fog_color = lerp(fog_color, CF_RED, 0.3 * state * (1 - rainStrength));
#endif
    fog_color = lerp(fog_color, float3(0.5, 0.5, 0.5), 0.25 * state * rainStrength);
    return lerp(color, pow(max(fog_color, 0.0), 2.2f), saturate(fogFactor));
}

// ---- final.fsh (camera / VHS chain; chromatic aberration + interlace need the whole frame -> Renderer Foundation pass) --
float3 blackToDarkPurple(float3 color) {
    float  brightness = dot(color, float3(0.2126, 0.7152, 0.0722));
    float3 darkPurple = float3(0.02, 0.00, 0.04);
    return lerp(darkPurple, color, smoothstep(0.0, 0.05, brightness));
}
float3 toneMappingACES(float3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}
float3 rgb2yuv(float3 rgb) {
    float y = 0.299 * rgb.r + 0.587 * rgb.g + 0.114 * rgb.b;
    return float3(y, 0.493 * (rgb.b - y), 0.877 * (rgb.r - y));
}
float3 yuv2rgb(float3 yuv) {
    float y = yuv.x, u = yuv.y, v = yuv.z;
    return float3(y + 1.0 / 0.877 * v, y - 0.39393 * u - 0.58081 * v, y + 1.0 / 0.493 * u);
}
float RandomValue(inout uint state) {
    state = state * 747796405u + 2891336453u;
    uint result = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    result = (result >> 22u) ^ result;
    state *= result;
    return float(result) / 4294967295.0;
}
float RandomValueNormalDistribution(inout uint n) {
    float theta = 2.0 * 3.14 * RandomValue(n);
    float rho   = sqrt(-2.0 * log(max(RandomValue(n), 1e-9)));
    n += uint(rho * cos(theta));
    return rho * cos(theta);
}
float3 RandomDirection(inout uint n) {
    float x = RandomValueNormalDistribution(n);
    float y = RandomValueNormalDistribution(n);
    float z = RandomValueNormalDistribution(n);
    return normalize(float3(x, y, z));
}
// pix = SV_Position.xy (top-left origin, D3D) -> GL uv has a bottom-left origin.
float3 cfFinal(float3 linearColor, float2 pix, float2 viewSize, float frameTimeCounter) {
    float2 uv = float2(pix.x / viewSize.x, 1.0 - pix.y / viewSize.y);
    float3 c  = pow(max(linearColor, 0.0), 0.45f);
    c = blackToDarkPurple(c);
#if R_DIRECTION == 1
    int2 pixels      = int2(viewSize);
    int2 pixelCoordC = int2(uv * float2(pixels));
    pixelCoordC      = int2(floor(float2(pixelCoordC) / 4.0) * 4.0);
    uint pixelIndex  = uint(pixelCoordC.y * pixels.x + pixelCoordC.x);
    uint rnd         = pixelIndex + uint(frac(frameTimeCounter) * viewSize.y * viewSize.y);
    c    = rgb2yuv(c);
    c.yz += RandomDirection(rnd).xy * DIRECTION_STRENGTH;
    c    = yuv2rgb(c);
#endif
#if BLACK_STRIPES == 1
    float leftStripe  = smoothstep(BLACK_STRIPES_WIDTH, BLACK_STRIPES_WIDTH - BLACK_STRIPES_SOFT, uv.x);
    float rightStripe = smoothstep(1.0 - BLACK_STRIPES_WIDTH, 1.0 - (BLACK_STRIPES_WIDTH - BLACK_STRIPES_SOFT), uv.x);
    c = lerp(c, float3(0.05, 0.05, 0.05), max(leftStripe, rightStripe));
#endif
#if CRT_MASK == 1
    float2 p     = (uv * viewSize) / CRT_SCALE;
    float  maskX = sin(p.x * 3.14159) * 0.5 + 0.5;
    float  maskY = sin(p.y * 3.14159) * 0.5 + 0.5;
    c *= lerp(0.85, 1.0, maskX * maskY);
    c *= 1.0 - dot(uv - 0.5, uv - 0.5) * 0.5;
#endif
#if GRAIN == 1
    c += (frac(sin(dot(uv, float2(12.9898, 78.233 * frameTimeCounter))) * 43758.5453) - 0.5) * GRAIN_STRENGTH;
#endif
    float3 toned  = toneMappingACES(c);
    float average = (toned.r + toned.g + toned.b) / 3.0;
    toned = lerp(average.xxx, toned, SATURATION);
    toned += BRIGHTNESS;
    toned = ((toned - 0.5) * CONTRAST) + 0.5;
    return toned;
}

// Java worldTime smoothTransition(): 1 by day, 0 at night, 1000-tick ramps. Bedrock has no worldTime in these
// materials, so the day level is read from what the game itself darkens with the time: the sky-light column of the
// lightmap texture (terrain) or the fog colour (entities).
float cfStateFromSkyLight(float3 lightmapSky) { return smoothstep(0.30, 0.85, dot(lightmapSky, float3(0.2126, 0.7152, 0.0722))); }
float cfStateFromFog(float3 fogColor) { return smoothstep(0.12, 0.55, dot(fogColor, float3(0.2126, 0.7152, 0.0722))); }
