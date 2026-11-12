// Renderer Foundation - IrisBridge import pass (original code). Bedrock's finished world colour (8-bit, display gamma)
// -> the pack's colortex0 (its own format, linear light), the way an Iris pack's gbuffers would have left it.
Texture2D<float4> gScene : register(t0);
cbuffer C : register(b0) { float4 c0; }; // x = 1: decode display gamma (2.2), y = exposure
struct V { float4 pos : SV_Position; };
V VSImport(uint id : SV_VertexID) {
    V o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float4 PSImport(V i) : SV_Target {
    float4 c = gScene.Load(int3(int2(i.pos.xy), 0));
    float3 l = c0.x > 0.5 ? pow(max(c.rgb, 0.0), 2.2) : c.rgb;
    return float4(l * c0.y, 1.0);
}
