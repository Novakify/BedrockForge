// LyricsFlow port - live sky drawn INSIDE the game's frame (stage 2), right after Bedrock's own sky draw on the same
// command list. These pipelines run on the game's root signature, so they declare NO resources: every input comes
// through the vertex buffer RF uploads (the 16 x RGBA8 parameter row, the camera basis, the shader inputs).
struct GIn {
    float2 pos  : POSITION;     // clip xy of the fullscreen triangle
    float4 p0   : PARAM0;  float4 p1  : PARAM1;  float4 p2  : PARAM2;  float4 p3  : PARAM3;
    float4 p4   : PARAM4;  float4 p5  : PARAM5;  float4 p6  : PARAM6;  float4 p7  : PARAM7;
    float4 p8   : PARAM8;  float4 p9  : PARAM9;  float4 p10 : PARAM10; float4 p11 : PARAM11; float4 p12 : PARAM12;
    float3 f    : CAMF;    float3 r   : CAMR;    float3 u   : CAMU;
    float2 txy  : CAMT;         // tan(hfov/2), tan(vfov/2)
    float4 info : INFO;         // time, brightness, alpha, -
};
struct GOut {
    float4 pos : SV_Position;
    float3 dir : TEXCOORD0;
    nointerpolation float4 info : TEXCOORD1;
    nointerpolation float4 p[13] : TEXCOORD2;
};

GOut VSSkyGame(GIn i) {
    GOut o;
    o.pos  = float4(i.pos, 1.0, 1.0);
    o.dir  = i.f + i.r * (i.pos.x * i.txy.x) + i.u * (i.pos.y * i.txy.y);
    o.info = i.info;
    o.p[0] = i.p0; o.p[1] = i.p1; o.p[2] = i.p2; o.p[3] = i.p3; o.p[4] = i.p4; o.p[5] = i.p5; o.p[6] = i.p6;
    o.p[7] = i.p7; o.p[8] = i.p8; o.p[9] = i.p9; o.p[10] = i.p10; o.p[11] = i.p11; o.p[12] = i.p12;
    return o;
}

static float4 gP[13];
#define PX_FETCH(i) gP[i]
#include "live_sky.hlsl"

float4 PSSkyGame(GOut i) : SV_Target {
    [unroll] for (int k = 0; k < 13; ++k) gP[k] = i.p[k];
    float4 c = skyMain(i.dir, i.info.xyz);
    return c; // straight alpha, blended SRC_ALPHA / INV_SRC_ALPHA over Bedrock's own sky
}

// ---- stars / constellations / shooting stars / lightning (solid quads, unit directions)
struct QIn {
    float3 pos : POSITION;
    float4 col : COLOR0;        // RGBA8_UNORM, straight alpha
    float3 f : CAMF; float3 r : CAMR; float3 u : CAMU; float2 txy : CAMT;
};
struct QOut {
    float4 pos : SV_Position;
    float4 col : COLOR0;
};
QOut VSSkyQuadGame(QIn i) {
    QOut  o;
    float z = dot(i.pos, i.f);
    o.pos   = float4(dot(i.pos, i.r) / i.txy.x, dot(i.pos, i.u) / i.txy.y, z, z); // depth disabled; z = w keeps it far
    o.col   = i.col;
    return o;
}
float4 PSSkyQuadGame(QOut i) : SV_Target { return i.col; }

// ---- "My picture" in place of the sky: own root signature (b0 = 16 root constants, t0 = the panorama, s0 wrap)
cbuffer PicCB : register(b0) {
    float3 pF; float pTx;
    float3 pR; float pTy;
    float3 pU; float pAlpha;
    float  pBright; float3 pPad;
};
Texture2D<float4> gPic : register(t0);
SamplerState      sPicWrap : register(s0);
struct POut { float4 pos : SV_Position; float3 dir : TEXCOORD0; };
POut VSSkyPicGame(uint id : SV_VertexID) {
    float2 p = float2((id << 1) & 2, id & 2) * 2.0 - 1.0;
    POut o;
    o.pos = float4(p, 1.0, 1.0);
    o.dir = pF + pR * (p.x * pTx) + pU * (p.y * pTy);
    return o;
}
float4 PSSkyPicGame(POut i) : SV_Target { // SkyImage: u = az / 360, v = 0.5 - el / 180
    float3 d  = normalize(i.dir);
    float  az = degrees(atan2(-d.x, d.z));
    if (az < 0.0) az += 360.0;
    float  el = degrees(asin(clamp(d.y, -1.0, 1.0)));
    float3 c  = gPic.SampleLevel(sPicWrap, float2(az / 360.0, 0.5 - el / 180.0), 0).rgb * pBright;
    return float4(c, saturate(pAlpha));
}

// ---- weather particles (WorldSky rain / snow / blizzard / petals / embers / fireflies), drawn at Bedrock's cloud
// draw with the terrain's depth (test only). Sprites are drawn procedurally after SkyPainter.sprites():
// frame 0 petal, 1 snow, 2 glow, 3 streak (u = frame / 4 + local u).
struct WIn {
    float3 pos : POSITION;  // blocks, camera-relative
    float2 uv  : TEXCOORD0;
    float4 col : COLOR0;
    float4 m0 : VPROW0; float4 m1 : VPROW1; float4 m2 : VPROW2; float4 m3 : VPROW3; // game view-projection rows
};
struct WOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0; };
WOut VSWeatherGame(WIn i) {
    WOut   o;
    float4 p = float4(i.pos, 1.0);
    o.pos = float4(dot(i.m0, p), dot(i.m1, p), dot(i.m2, p), dot(i.m3, p));
    o.uv  = i.uv;
    o.col = i.col;
    return o;
}
float4 PSWeatherGame(WOut i) : SV_Target {
    float  f = floor(clamp(i.uv.x, 0.0, 0.9999) * 4.0);
    float2 q = float2(frac(i.uv.x * 4.0), i.uv.y);
    float2 c = q - 0.5;
    float  r = length(c);
    float  a;
    float3 tint = float3(1, 1, 1);
    if (f < 0.5) {          // petal: round top, point at the bottom, pale centre stripe
        float w = 0.34 * sin(3.14159265 * pow(saturate(1.0 - q.y), 1.4));
        a    = 1.0 - smoothstep(w - 0.03, w, abs(c.x));
        tint = lerp(float3(0.96, 0.72, 0.84), float3(1.0, 0.93, 0.96), 1.0 - smoothstep(0.0, 0.12, abs(c.x)));
    } else if (f < 1.5) {   // snow: soft bright blob
        a = pow(saturate(1.0 - r * 2.3), 0.6);
    } else if (f < 2.5) {   // glow: small core + wide halo
        a = saturate(exp(-r * r * 160.0) + 0.55 * exp(-r * r * 22.0));
    } else {                // streak: thin bar, brightest toward the bottom (head)
        a = (1.0 - smoothstep(0.09, 0.16, abs(c.x))) * smoothstep(0.05, 0.85, q.y) * (1.0 - smoothstep(0.95, 0.99, q.y));
    }
    float4 o = i.col;
    o.rgb *= tint;
    o.a   *= a;
    return o;
}
