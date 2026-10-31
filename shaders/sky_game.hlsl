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
