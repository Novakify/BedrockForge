// Glass Armor - pixel shader DRAFT (not built yet; lives in docs until the M0 probe answers the open questions in
// docs/GLASS_ARMOR.md). HLSL port of the approved preview docs/glass_armor/preview_v6.html.
//
// Runs in place of the game's armor pixel shader on the game's own armor draw: same vertex shader, same vertex
// buffers, same depth test -> exact vanilla pose and geometry. All this shader needs from the game's vertex outputs is
// the armor texture UV; the shape comes from the vanilla texture's alpha (tools/GlassArmor/gen_masks.py).
//
//  t0 scene   : copy of the render target taken right before the first armor draw of the frame (world + bodies)
//  t1 depth   : copy of the depth buffer at the same moment (keeps your own limbs clean under the glass)
//  t2 sdf     : <mat>_<layer>.sdf.f16, 512x256, texels, + inside the painted area
//  t3 face    : face_<layer>.u8, 64x32 RGBA8 = rect x, y, w, h (texels) of the cube face that texel belongs to
cbuffer GlassArmor : register(b0, space9) {
    float2 res;        // render target size (px)
    float  time;       // seconds
    float  glint;      // 0 off, 1 edge pulse, 2 edge flow (default when enchanted)
    float3 tint;       // material tint (linear)            leather: dye colour from the item
    float  lightLv;    // 0..1 block/sky light at the wearer
    float3 edge;       // neon outline colour (linear)
    float  swirl;      // fixed twist (rad): subtle .5 / medium .9 / strong 1.35
    float2 lightDir;   // screen-space direction of the main light (sun / nearest torch)
    float  ripple;     // .05 / .09 / .13
    float  lens;       // .6 / .8 / 1.0 (client menu lens at the rim)
    float2 zParams;    // linear depth = 1 / (z * zParams.x + zParams.y) (handles reversed Z)
    float  lineW;      // outline width (texels) .19
    float  uiScale;    // res.y / 760 (menu design units -> px)
};
Texture2D<float4> gScene : register(t0, space9);
Texture2D<float>  gDepth : register(t1, space9);
Texture2D<float>  gSdf   : register(t2, space9);
Texture2D<uint4>  gFace  : register(t3, space9);
SamplerState      sLin   : register(s0, space9);

float  linZ(float z) { return 1.0 / (z * zParams.x + zParams.y); }
float3 sceneAt(float2 px) { return gScene.SampleLevel(sLin, px / res, 0).rgb; }

float4 PSGlassArmor(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float2 t  = uv * float2(64, 32);                      // armor texel coordinates
    float  dT = gSdf.SampleLevel(sLin, uv, 0);            // texels to the painted edge
    clip(dT);                                             // vanilla alpha test, but with a smooth edge
    uint4  r  = gFace.Load(int3(int2(t), 0));
    float2 fs = float2(r.zw), p = t - float2(r.xy);       // face size + face-local position (texels)

    // edge distance in screen px + outward screen normal (same SDF trick as RF glassShade)
    float  dW = -dT;
    float2 gr = float2(ddx(dW), ddy(dW));
    float  lg = max(length(gr), 1e-4);
    float  d  = dW / lg;
    float2 n  = gr / lg;
    float  halfPx = min(fs.x, fs.y) * 0.5 / lg;
    float  S = uiScale;

    // outline + enchant (edge flow = light travelling round the outline)
    float aaW  = fwidth(dT) * 1.1;
    float line = 1.0 - smoothstep(lineW - aaW, lineW + aaW, dT);
    float ench = 0;
    if (glint > 0.5 && glint < 1.5) ench = 0.5 + 0.5 * sin(time * 2.4);
    if (glint > 1.5) { float per = (p.x + p.y) / (fs.x + fs.y); ench = pow(0.5 + 0.5 * sin((per * 2.0 - time * 0.55) * 6.2831), 6.0); }
    float3 edgeC = edge * (1.0 + ench * 1.2);

    // fixed twist around the face centre + gentle fixed wave (no animation)
    float  bevel = min(22.0 * S, halfPx * 0.85);
    float  e     = pow(1.0 - saturate(-d / max(bevel, 1.0)), 2.2);
    float2 rel   = p - fs * 0.5;
    float  rr    = length(rel) / max(min(fs.x, fs.y) * 0.5, 1e-3);
    float  ph    = frac(sin(dot(float2(r.xy), float2(12.9898, 78.233))) * 43758.5);
    float  ang   = swirl * smoothstep(1.15, 0.0, rr) * (ph > 0.5 ? 1.0 : -1.0);
    float  cs = cos(ang), sn = sin(ang);
    float2 fo = float2(cs * rel.x - sn * rel.y, sn * rel.x + cs * rel.y) - rel;
    fo += ripple * float2(sin(p.y * 0.9 + ph * 6.0), cos(p.x * 0.9 + ph * 4.0));
    float2x2 J  = float2x2(ddx(p).x, ddy(p).x, ddx(p).y, ddy(p).y);  // texels per screen px
    float    dj = J._11 * J._22 - J._12 * J._21;
    float2   off = abs(dj) > 1e-8 ? mul(float2x2(J._22, -J._12, -J._21, J._11) / dj, fo) : 0;
    off  = clamp(off, -42.0 * S, 42.0 * S);
    off += -n * 16.0 * lens * S * e;

    // your own body right under the glass stays clean: only bend what is further behind
    float self = linZ(pos.z);
    float gap  = min(linZ(gDepth.Load(int3(pos.xy, 0))), linZ(gDepth.SampleLevel(sLin, (pos.xy + off) / res, 0))) - self;
    float kw   = smoothstep(0.25, 0.9, gap);              // blocks: 1/4 .. ~1 block behind
    off *= kw;

    float3 c;
    c.r = sceneAt(pos.xy + off).r; c.g = sceneAt(pos.xy + off * 1.04).g; c.b = sceneAt(pos.xy + off * 1.08).b;
    c = lerp(c, lerp(float3(0.0065, 0.0032, 0.018), tint, 0.35), 0.10);
    float rim = 1.0 - smoothstep(0.0, 1.6, abs(d + 0.9));
    float lit = 0.45 + 0.55 * max(dot(n, lightDir), 0.0);
    c += 0.9 * (rim * lit * 0.75 + e * 0.08) * (0.35 + 0.65 * lightLv);
    float sat = max(max(edge.r, edge.g), edge.b) - min(min(edge.r, edge.g), edge.b);
    c += edgeC * (0.24 + 0.30 * ench) * lerp(0.45, 1.0, saturate(sat * 1.5)) * pow(saturate(1.0 + d / bevel), 3.0);
    c += edgeC * 0.015;
    c = lerp(c, edgeC * 1.25, line);
    return float4(c, 1.0);
}
