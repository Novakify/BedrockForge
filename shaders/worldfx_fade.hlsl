// Renderer Foundation - [Sky] crossfade between skies. When the Sky module (Bedrock Forge) switches Living Skies, Cloud
// Sea, Novak Glass or vanilla, the frame just before the switch is kept, and for a second the old sky dissolves into the
// new one instead of cutting. Sky pixels (depth at the far plane) take the old picture; the world takes a smaller share
// only while the camera has hardly moved since (a moved camera would ghost the old world over the new one).
// Drawn on the world's targets after the World FX pass, before the hand and the HUD. Original code for Renderer Foundation.
cbuffer C : register(b0) {
    float4 c0, c1, c2;
    float4 c3;   // x = old sky share (1 -> 0), y = old world share, w = flags: 1 BGRA
    float4 c4;   // xy = target size, z = old frame row pitch (bytes), w = depth row pitch (bytes)
    float4 c5, c6, c7, c8, c9, c10;
};
ByteAddressBuffer gOld   : register(t0);   // the frame before the switch (RGBA8 / BGRA8 rows)
ByteAddressBuffer gDepth : register(t1);   // this frame's depth (D24: low 24 bits)

float4 PSWorldFxFade(float4 pos : SV_Position) : SV_Target {
    int2  p = clamp(int2(pos.xy), int2(0, 0), int2(c4.xy) - 1);
    float d = float(gDepth.Load(uint(p.y) * uint(c4.w) + uint(p.x) * 4u) & 0xFFFFFFu) / 16777215.0;
    float a = d >= 0.99999 ? c3.x : c3.y;
    if (a <= 0.002) discard;
    uint   v = gOld.Load(uint(p.y) * uint(c4.z) + uint(p.x) * 4u);
    float3 c = float3(v & 255u, (v >> 8) & 255u, (v >> 16) & 255u) / 255.0;
    return float4(c3.w > 0.5 ? c.bgr : c, a);
}
