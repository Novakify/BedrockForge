// Renderer Foundation - Spatial GUI panel (M1). Original code for Renderer Foundation.
// Root constants (b0, 32 floats):
//   c0  = 4 panel corners in clip space (TL, TR, BL, BR), float4 each
//   c4  = x: world zoom (>= 1, "FOV lerp"), y: panel opacity, z: world dim, w: alpha mode (0 raw, 1 boost)
//   c5  = x: frame line strength (0 = no frame, Java look), y: 1 = drop the dim background layer
// t0 = world (back buffer copy, UI already redirected away), t1 = UI target (premultiplied-ish, transparent clear)
cbuffer Params : register(b0) {
    float4 corner[4];
    float4 misc;
    float4 pad[3];
};
Texture2D    tWorld : register(t0);
Texture2D    tUi    : register(t1);
SamplerState sLin   : register(s0);

struct V2P { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

// full-screen triangle
V2P VSFull(uint id : SV_VertexID) {
    V2P o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.uv  = uv;
    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

float4 PSWorld(V2P i) : SV_Target {
    float2 uv = (i.uv - 0.5) / max(misc.x, 1.0) + 0.5;
    float3 c  = tWorld.SampleLevel(sLin, uv, 0).rgb;
    return float4(c * (1.0 - misc.z), 1);
}

// two triangles over the 4 corners; clip-space w carries perspective, so uv interpolates correctly
V2P VSPanel(uint id : SV_VertexID) {
    static const uint   idx[6] = {0, 1, 2, 2, 1, 3};
    static const float2 uvs[4] = {float2(0, 0), float2(1, 0), float2(0, 1), float2(1, 1)};
    uint k = idx[id];
    V2P  o;
    o.pos = corner[k];
    o.uv  = uvs[k];
    return o;
}

float4 PSPanel(V2P i) : SV_Target {
    float4 u = tUi.SampleLevel(sLin, i.uv, 0);
    // c5.y: drop the screen's dim background layer (near-black, partly transparent) - only the GUI itself shows
    if (pad[0].y > 0.5 && max(u.r, max(u.g, u.b)) < 0.03 && u.a < 0.85) u = 0;
    float  a = misc.w > 0.5 ? max(u.a, max(u.r, max(u.g, u.b))) : u.a;
    // thin frame so the panel edge reads in dark scenes
    float2 e    = min(i.uv, 1.0 - i.uv);
    float  edge = (1.0 - smoothstep(0.0, 0.004, min(e.x, e.y))) * pad[0].x;
    float3 rgb  = lerp(u.rgb, float3(0.85, 0.85, 0.85) * max(a, 0.6), edge);
    a           = max(a, edge * 0.6);
    return float4(rgb, a) * misc.y; // premultiplied output (blend ONE / INV_SRC_ALPHA)
}
