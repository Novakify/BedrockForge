// Renderer Foundation - Vibrant Visuals exposure probe (by Novak). Original code for Renderer Foundation.
// VV's frame is pre-exposed: every lit pixel is multiplied by 0.18 / (last frame's average luminance) + 1e-4 before it
// goes into the HDR target (read off the game's DeferredShading shader). The game binds that 1x1 luminance texture at
// t0 for its own sun / moon / star draws - so on the game's own root signature, at that draw, one pixel copies it into
// a 1x1 target of ours. RF's sky and match layer read it there and expose themselves exactly like the game does.
Texture2D<float4> gAvgLum : register(t0);
float4 VSVvProbe(uint id : SV_VertexID) : SV_Position { return float4(id == 2 ? 3.0 : -1.0, id == 1 ? 3.0 : -1.0, 0.5, 1.0); }
float4 PSVvProbe(float4 pos : SV_Position) : SV_Target { return gAvgLum.Load(int3(0, 0, 0)); }
