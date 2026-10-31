// Renderer Foundation - LyricsFlow world text (glyph quads). Original code for Renderer Foundation / Bedrock Forge.
// One instanced draw: 6 vertices per glyph, glyph data from Bedrock Forge (BF_CopyLyricFrame, src/BfLyrics.h).
// Look: SDF fill (+ vertical gradient), outline, soft glow; MSDF (median of rgb) and pixel bitmap (nearest) atlases.
// Output is premultiplied alpha (blend ONE / INV_SRC_ALPHA, same as panel.hlsl).
//
// Root constants (b0, 24 floats):
//   c0..c3  viewProj, column-major, camera-relative (input = glyph.pos + c4.xyz)
//   c4      xyz: BF origin minus camera position (world units), w: time (s)
//   c5      x: screen aspect (width / height), y: global opacity, z: debug (1 = show quad bounds), w: screen height (px)
// t0 = StructuredBuffer<Glyph>, t1..t16 = atlases (index = Glyph.atlas), t17 = S2 UI target, s0 = linear clamp, s1 = point clamp
// t18 = copy of the frame (glass), t19 / t20 = quarter-res blur ping-pong (t19 holds the final blur)
//
// Glass (Bedrock Forge client UI + LyricsFlow panel, 2026-10-31): BF_GLYPH_GLASS surfaces show the frame behind them
// frosted (blur mix) and bent like a lens near the edge (refraction), with a bright rim and a coloured inner edge glow.
// The frame is copied once before the glyphs draw, so glass sees the world and the HUD but not other glyphs.
cbuffer Params : register(b0) {
    float4x4 viewProj;
    float4   originTime;
    float4   misc;
};

struct Glyph {
    float3 pos;   float pad0;
    float3 right; float pad1;
    float3 up;    float pad2;
    float4 uv;            // u0 v0(top) u1 v1(bottom)
    uint   fill, fill2, outline, glow; // RGBA8, R in the low byte
    float  alpha, outlineW, glowW, glowK;
    uint   atlas, flags;
    float  pxRange, pad3;
};

static const uint F_SCREEN   = 1u;
static const uint F_BITMAP   = 2u;
static const uint F_MSDF     = 4u;
static const uint F_GRADIENT = 16u;
static const uint F_SOLID    = 32u; // rounded rect, no atlas (top bar pill, icons, EQ bars)
static const uint F_VIEW     = 64u; // settings panel: camera-locked perspective, pos already / tan(fovY/2)
static const uint F_IMAGE    = 128u; // colour image: rgba * fill tint
static const uint F_GLASS    = 256u;  // with SOLID / PETAL: glass (see BfLyrics.h for the field meanings)
static const uint F_PETAL    = 512u;  // ring sector inside the quad (the client wheel's buttons)
static const uint F_BACKDROP = 1024u; // full-screen frosted copy of the frame with a clear circle in the middle
static const uint kAtlases   = 16u;

StructuredBuffer<Glyph> gGlyphs  : register(t0);
Texture2D               gAtlas[16] : register(t1);
SamplerState            sLin     : register(s0);
SamplerState            sPoint   : register(s1);
Texture2D               gScene   : register(t18);
Texture2D               gBlur    : register(t19);
Texture2D               gBlurB   : register(t20);

struct V2P {
    float4 pos  : SV_Position;
    float2 uv   : TEXCOORD0;
    float  grad : TEXCOORD1;  // 0 at the top of the quad, 1 at the bottom
    nointerpolation uint inst : TEXCOORD2;
};

float4 unpack(uint c) { return float4(c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF, c >> 24) / 255.0; }
float  median3(float3 v) { return max(min(v.r, v.g), min(max(v.r, v.g), v.b)); }

V2P VSGlyph(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
    static const float2 corner[6] = {float2(-1, 1), float2(1, 1), float2(-1, -1), float2(-1, -1), float2(1, 1), float2(1, -1)};
    Glyph  g = gGlyphs[iid];
    float2 c = corner[vid];
    float3 p = g.pos + g.right * c.x + g.up * c.y;
    V2P    o;
    if (g.flags & F_VIEW) // panel: exact perspective (BF ray-picks the mouse with the same mapping)
        o.pos = float4(p.x / max(misc.x, 1e-3), p.y, 0.0, max(p.z, 1e-3));
    else if (g.flags & F_SCREEN) // x 0..aspect, y 0..1 downwards
        o.pos = float4(p.x / max(misc.x, 1e-3) * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
    else
        o.pos = mul(viewProj, float4(p + originTime.xyz, 1.0));
    o.uv   = float2(c.x < 0 ? g.uv.x : g.uv.z, c.y > 0 ? g.uv.y : g.uv.w);
    o.grad = c.y > 0 ? 0.0 : 1.0;
    o.inst = iid;
    return o;
}


// ---------------------------------------------------------------- glass
float2 screenPx() { return float2(max(misc.x, 1e-3) * max(misc.w, 1.0), max(misc.w, 1.0)); }
float3 sceneAt(float2 px, float blur) {
    float2 uv = px / screenPx();
    return lerp(gScene.SampleLevel(sLin, uv, 0).rgb, gBlur.SampleLevel(sLin, uv, 0).rgb, blur);
}
float2 sdfNormal(float d) { // screen-space gradient of the signed distance (outward)
    float2 g = float2(ddx(d), ddy(d));
    float  l = length(g);
    return l > 1e-4 ? g / l : float2(0, -1);
}
// d = signed distance in screen px (+ outside), px = this pixel. Fields: fill = tint (a = amount), glowW = bevel px,
// glowK = refraction px, pxRange = blur mix 0..1, outline = rim light (a = strength), glow = inner edge glow (a = strength)
float4 glassShade(Glyph g, float d, float2 px, float4 tint, float k) {
    float2 n     = sdfNormal(d);
    float  bevel = max(g.glowW, 1.0);
    float  x     = saturate(-d / bevel);
    float  edge  = pow(1.0 - x, 2.2);
    float2 off   = -n * g.glowK * edge;               // lens: the rim pulls the world inward (magnifies + bends)
    float  blur  = saturate(g.pxRange);
    float3 c;
    c.r = sceneAt(px + off, blur).r;
    c.g = sceneAt(px + off * 1.07, blur).g;
    c.b = sceneAt(px + off * 1.14, blur).b;          // tiny chromatic split like real glass
    c   = lerp(c, tint.rgb, saturate(tint.a));
    float4 rimC  = unpack(g.outline);
    float  rim   = 1.0 - smoothstep(0.0, 1.6, abs(d + 0.9));
    float  light = 0.45 + 0.55 * max(dot(n, normalize(float2(-0.55, -0.85))), 0.0);
    c += rimC.rgb * rimC.a * (rim * light * 0.75 + edge * 0.10);
    float4 inC = unpack(g.glow);
    c += inC.rgb * inC.a * pow(saturate(1.0 + d / bevel), 3.0);
    float a = saturate(0.5 - d) * k;
    return float4(c * a, a);
}
float angDiff(float a, float b) { float d = a - b; return d - 6.2831853 * floor((d + 3.14159265) / 6.2831853); }

float4 PSGlyph(V2P i) : SV_Target {
    Glyph  g     = gGlyphs[i.inst];
    float4 fillC = unpack(g.fill);
    if (g.flags & F_GRADIENT) fillC = lerp(fillC, unpack(g.fill2), saturate(i.grad));
    float  k     = g.alpha * misc.y;

    if (g.flags & F_BACKDROP) { // fill = darkening colour (a = amount), pxRange = blur, glowW = clear radius px, glowK = feather px
        float2 halfPx = 0.5 / max(fwidth(i.uv), 1e-6);
        float  r      = length((i.uv - 0.5) * 2.0 * halfPx);
        float  mask   = g.glowK > 0.0 ? smoothstep(g.glowW, g.glowW + g.glowK, r) : 1.0;
        float3 c      = sceneAt(i.pos.xy, saturate(g.pxRange) * mask);
        c             = lerp(c, fillC.rgb, saturate(fillC.a) * (0.35 + 0.65 * mask));
        return float4(c * k, k);
    }
    if (g.flags & F_PETAL) { // ring sector: outlineW = inner / outer radius, fill2 = centre angle | half span << 16 (1/100 deg)
        float2 halfPx = 0.5 / max(fwidth(i.uv), 1e-6);
        float2 p      = (i.uv - 0.5) * 2.0 * halfPx;       // px from the quad centre, y down
        float  R2     = min(halfPx.x, halfPx.y) - 1.0, R1 = R2 * saturate(g.outlineW);
        float  ca     = float(g.fill2 & 0xFFFFu) * (3.14159265 / 18000.0);
        float  hs     = float(g.fill2 >> 16) * (3.14159265 / 18000.0);
        float  r      = length(p), an = atan2(p.y, p.x);
        float  rm = (R1 + R2) * 0.5, hw = (R2 - R1) * 0.5, rad = hw * 0.42;
        float2 q  = float2(abs(r - rm) - (hw - rad), (abs(angDiff(an, ca)) - hs) * r + rad);
        float  d  = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - rad;
        if (g.flags & F_GLASS) return glassShade(g, d, i.pos.xy, fillC, k);
        float a = saturate(0.5 - d) * fillC.a;
        return float4(fillC.rgb * a, a) * k;
    }
    if (g.flags & F_SOLID) { // outlineW = corner radius (fraction of the smaller half-extent), glowW = edge softness px
        float2 halfPx = 0.5 / max(fwidth(i.uv), 1e-6);
        float  r      = min(halfPx.x, halfPx.y) * saturate(g.outlineW);
        float2 q      = abs(i.uv - 0.5) * 2.0 * halfPx - (halfPx - r);
        float  d      = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
        if (g.flags & F_GLASS) return glassShade(g, d, i.pos.xy, fillC, k);
        // soft edge (glowW > 1.5 px: glows / shadows): fade to 0 exactly at the quad's edge, full at glowW px inside
        // (the old 0.5 - d / glowW left alpha 0.5 at the edge, a hard rectangle around every glow)
        float  a      = (g.glowW > 1.5 ? saturate(-d / g.glowW) : saturate(0.5 - d / max(1.0, g.glowW))) * fillC.a;
        return float4(fillC.rgb * a, a) * k;
    }

    uint const ai = min(g.atlas, kAtlases - 1);
    if (g.flags & F_IMAGE) { // cover art / icons / thumbnails: straight-alpha rgba, tinted
        float4 t = gAtlas[NonUniformResourceIndex(ai)].Sample(sLin, i.uv) * fillC;
        return float4(t.rgb * t.a, t.a) * k;
    }

    if (g.flags & F_BITMAP) { // pixel font: hard texels, no SDF maths
        float a = gAtlas[NonUniformResourceIndex(ai)].Sample(sPoint, i.uv).r;
        float4 o = float4(fillC.rgb, 1.0) * (fillC.a * a);
        if (misc.z > 0.5) o = max(o, float4(0.2, 0.0, 0.0, 0.2));
        return o * k;
    }

    Texture2D tex = gAtlas[NonUniformResourceIndex(ai)];
    float3    s   = tex.Sample(sLin, i.uv).rgb;
    float     v   = (g.flags & F_MSDF) ? median3(s) : s.r; // 0.5 = outline of the glyph
    // screen pixels per SDF unit (msdf-atlas-gen's screenPxRange)
    float tw, th;
    tex.GetDimensions(tw, th);
    float2 unitRange  = g.pxRange / float2(tw, th);
    float2 screenTex  = 1.0 / max(fwidth(i.uv), 1e-6);
    float  spr        = max(0.5 * dot(unitRange, screenTex), 1.0);
    float  sd         = (v - 0.5) * spr;                         // screen px, + inside
    float  fillA      = saturate(sd + 0.5);
    float  outA       = g.outlineW > 0 ? saturate(sd + g.outlineW * spr + 0.5) : fillA;
    float4 outC       = unpack(g.outline);
    float4 glowC      = unpack(g.glow);
    float  glowA      = g.glowK > 0 ? g.glowK * glowC.a * pow(saturate(1.0 + (v - 0.5) / max(g.glowW, 1e-3)), 2.0) : 0.0;

    // premultiplied: fill over outline over glow
    float3 rgb = fillC.rgb * (fillA * fillC.a) + outC.rgb * ((outA - fillA) * outC.a);
    float  a   = fillA * fillC.a + (outA - fillA) * outC.a;
    rgb += glowC.rgb * glowA * (1.0 - a);
    a += glowA * (1.0 - a);
    float4 o = float4(rgb, a);
    if (misc.z > 0.5) o = max(o, float4(0.2, 0.0, 0.0, 0.2));
    return o * k;
}

// ---- S2 "under the HUD" (staged, unverified): the game's UI was redirected into our own target (UiCapture);
// after the glyphs are drawn on the back buffer, the UI goes back on top with this full-screen pass.
Texture2D gUiTarget : register(t17);
struct VFull { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VFull VSComposite(uint id : SV_VertexID) {
    VFull  o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.uv      = uv;
    o.pos     = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float4 PSComposite(VFull i) : SV_Target { return gUiTarget.Sample(sPoint, i.uv); } // UI target is premultiplied-ish (see UiCapture alpha fix)

// ---- glass blur: frame copy (t18) -> quarter res (t19), then separable gaussian ping-pong t19 -> t20 -> t19
float4 PSDown(VFull i) : SV_Target {
    float w, h;
    gScene.GetDimensions(w, h);
    float2 t = 1.0 / float2(w, h);
    float3 c = gScene.SampleLevel(sLin, i.uv + t * float2(-1, -1), 0).rgb + gScene.SampleLevel(sLin, i.uv + t * float2(1, -1), 0).rgb
             + gScene.SampleLevel(sLin, i.uv + t * float2(-1, 1), 0).rgb + gScene.SampleLevel(sLin, i.uv + t * float2(1, 1), 0).rgb;
    return float4(c * 0.25, 1);
}
float3 gauss9(Texture2D src, float2 uv, float2 dir) {
    float w, h;
    src.GetDimensions(w, h);
    float2 t = dir * 2.0 / float2(w, h); // spread 2 texels per step: wide frost at quarter res
    float3 c = src.SampleLevel(sLin, uv, 0).rgb * 0.2270270270;
    c += (src.SampleLevel(sLin, uv + t * 1.3846153846, 0).rgb + src.SampleLevel(sLin, uv - t * 1.3846153846, 0).rgb) * 0.3162162162;
    c += (src.SampleLevel(sLin, uv + t * 3.2307692308, 0).rgb + src.SampleLevel(sLin, uv - t * 3.2307692308, 0).rgb) * 0.0702702703;
    return c;
}
float4 PSBlurH(VFull i) : SV_Target { return float4(gauss9(gBlur, i.uv, float2(1, 0)), 1); }
float4 PSBlurV(VFull i) : SV_Target { return float4(gauss9(gBlurB, i.uv, float2(0, 1)), 1); }
