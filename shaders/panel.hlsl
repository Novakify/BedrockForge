// Renderer Foundation - Spatial GUI panel (M1). Original code for Renderer Foundation.
// Root constants (b0, 32 floats):
//   c0  = 4 panel corners in clip space (TL, TR, BL, BR), float4 each
//   c4  = x: world zoom (>= 1, "FOV lerp"), y: panel opacity, z: world dim, w: alpha mode (0 raw, 1 boost)
//   c5  = x: frame line strength (0 = no frame, Java look), y: 1 = drop the dim background layer, zw: drawn uv fraction
//   c6  = the HUD strip (hotbar, hearts) kept flat under the panel, uv rect x0 y0 x1 y1 (x1 <= x0: none)
//   c7  = x: world blur radius in pixels (0 = none), y: drop shadow alpha (0 = none), zw: drop shadow uv offset
//   c9  = PSBlur1D: x sigma (texels of the output), yz uv step between taps
//   c8  = the player on screen (uv rect x0 y0 x1 y1; x1 <= x0: none) - kept sharp in the world blur
// t0 = world (back buffer copy, UI already redirected away), t1 = UI target (premultiplied-ish, transparent clear)
cbuffer Params : register(b0) {
    float4 corner[4];
    float4 misc;
    float4 pad[5];
};
Texture2D    tWorld : register(t0);
Texture2D    tUi    : register(t1);
Texture2D    tBlur  : register(t2); // the world blurred (half size, PSBlur1D twice)
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

// Java blurWorldBackground (WorldBlurRenderer: the vanilla menu blur over the world): a separable Gaussian made at
// half size (PSBlur1D, two passes) and mixed in here; c7.x = how much of it shows, the player rect (c8) can stay sharp.
float4 PSWorld(V2P i) : SV_Target {
    float2 uv = (i.uv - 0.5) / max(misc.x, 1.0) + 0.5;
    float3 c  = tWorld.SampleLevel(sLin, uv, 0).rgb;
    float  m  = pad[2].x;
    if (m > 0.0) {
        float keep = 0.0;
        if (pad[3].z > pad[3].x) {
            float2 lo = pad[3].xy, hi = pad[3].zw, ctr = (lo + hi) * 0.5, half = (hi - lo) * 0.5;
            float2 q  = abs(uv - ctr) - half;                                  // < 0 inside the rect
            float  f  = max(0.02, 0.35 * min(half.x, half.y));                 // feather
            keep      = 1.0 - smoothstep(0.0, f, max(q.x, q.y));
        }
        float3 b = tBlur.SampleLevel(sLin, uv, 0).rgb;
        c = lerp(c, b, saturate(m * (1.0 - keep)));
    }
    return float4(c * (1.0 - misc.z), 1);
}

// one direction of the Gaussian (bilinear pairs: two texels per tap, so a wide sigma stays cheap and smooth)
float4 PSBlur1D(V2P i) : SV_Target {
    float  sigma = max(pad[4].x, 0.5);
    float2 stepv = pad[4].yz;
    int    n     = (int)min(ceil(sigma * 3.0 / 2.0), 24.0);
    float  w0    = 1.0;
    float3 acc   = tWorld.SampleLevel(sLin, i.uv, 0).rgb;
    float  wsum  = w0;
    for (int k = 0; k < n; ++k) {
        float  a  = 2.0 * k + 1.0, b = a + 1.0;                 // the texel pair a, a+1 (in output texels)
        float  wa = exp(-a * a / (2.0 * sigma * sigma)), wb = exp(-b * b / (2.0 * sigma * sigma));
        float  w  = wa + wb;
        float  o  = (a * wa + b * wb) / max(w, 1e-6);           // where one bilinear fetch gets both
        acc  += (tWorld.SampleLevel(sLin, i.uv + stepv * o, 0).rgb + tWorld.SampleLevel(sLin, i.uv - stepv * o, 0).rgb) * w;
        wsum += 2.0 * w;
    }
    return float4(acc / wsum, 1);
}

// two triangles over the 4 corners; clip-space w carries perspective, so uv interpolates correctly
V2P VSPanel(uint id : SV_VertexID) {
    static const uint   idx[6] = {0, 1, 2, 2, 1, 3};
    static const float2 uvs[4] = {float2(0, 0), float2(1, 0), float2(0, 1), float2(1, 1)};
    uint k = idx[id];
    V2P  o;
    o.pos = corner[k];
    o.uv  = 0.5 + (uvs[k] - 0.5) * max(pad[0].zw, 1e-3); // c5.zw: drawn fraction of the texture (centred)
    return o;
}

bool inHud(float2 uv) { return pad[1].z > pad[1].x && all(uv >= pad[1].xy) && all(uv <= pad[1].zw); }

float4 uiAt(float2 uv) {
    float4 u = tUi.SampleLevel(sLin, uv, 0);
    if (inHud(uv)) u = 0; // the HUD strip is drawn flat (PSHud), not on the panel
    // c5.y: drop the screen's dim background layer (near-black, partly transparent) - only the GUI itself shows
    if (pad[0].y > 0.5 && max(u.r, max(u.g, u.b)) < 0.03 && u.a < 0.85) u = 0;
    return u;
}

float4 PSPanel(V2P i) : SV_Target {
    float4 u = uiAt(i.uv);
    float  a = misc.w > 0.5 ? max(u.a, max(u.r, max(u.g, u.b))) : u.a;
    // Java dropShadow: the GUI's silhouette in black (dropShadowAlpha) a little right of and below it, under it
    if (pad[2].y > 0.0) {
        float sa = uiAt(i.uv - pad[2].zw).a * pad[2].y;
        float3 rgb = u.rgb;                      // premultiplied GUI over the shadow
        float  na  = a + sa * (1.0 - a);
        u = float4(rgb, na);
        a = na;
    }
    // thin frame so the panel edge reads in dark scenes
    float2 e    = min(i.uv, 1.0 - i.uv);
    float  edge = (1.0 - smoothstep(0.0, 0.004, min(e.x, e.y))) * pad[0].x;
    float3 rgb  = lerp(u.rgb, float3(0.85, 0.85, 0.85) * max(a, 0.6), edge);
    a           = max(a, edge * 0.6);
    return float4(rgb, a) * misc.y; // premultiplied output (blend ONE / INV_SRC_ALPHA)
}

// The HUD under the panel (Java: the hotbar and hearts stay on the screen): the strip of the UI target, 1:1, with the
// screen's dim background (drawn over the HUD) taken back out. The dim is read from the top-left corner of the target.
float4 PSHud(V2P i) : SV_Target {
    if (!inHud(i.uv)) return 0;
    float4 u = tUi.SampleLevel(sLin, i.uv, 0);
    float4 d = tUi.Load(int3(2, 2, 0));
    float  k = (max(d.r, max(d.g, d.b)) < 0.03 && d.a < 0.85) ? d.a : 0.0;
    float  a = saturate((u.a - k) / max(1.0 - k, 0.05));
    float3 c = u.rgb / max(1.0 - k, 0.05);
    return float4(c, a); // not faded with the panel: the HUD never went away
}
