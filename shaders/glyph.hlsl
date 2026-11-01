// Renderer Foundation - LyricsFlow world text (glyph quads). Original code for Renderer Foundation / Bedrock Forge.
// One instanced draw: 6 vertices per glyph, glyph data from Bedrock Forge (BF_CopyLyricFrame, src/BfLyrics.h).
// Look: SDF fill (+ vertical gradient), outline, soft glow; MSDF (median of rgb) and pixel bitmap (nearest) atlases.
// Output is premultiplied alpha (blend ONE / INV_SRC_ALPHA, same as panel.hlsl).
//
// Root constants (b0, 24 floats):
//   c0..c3  viewProj, column-major, camera-relative (input = glyph.pos + c4.xyz)
//   c4      xyz: BF origin minus camera position (world units), w: time (s)
//   c5      x: screen aspect (width / height), y: global opacity, z: debug (1 = show quad bounds), w: unused
// t0 = StructuredBuffer<Glyph>, t1..t16 = atlases (index = Glyph.atlas), t17 = S2 UI target, s0 = linear clamp, s1 = point clamp
// t18 = the screen behind, blurred (quarter size; glass), t19 = back buffer copy, t20 = blur temp (PSBlurH -> PSBlurV)
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
static const uint F_GLASS    = 256u; // with SOLID: frosted glass (t18 at this pixel, tinted by fill, * glowK)
static const uint F_BLUR     = 512u; // with IMAGE: soft-blurred image, radius glowW (fraction of the image), soft edges
static const uint kAtlases   = 16u;

StructuredBuffer<Glyph> gGlyphs  : register(t0);
Texture2D               gAtlas[16] : register(t1);
SamplerState            sLin     : register(s0);
SamplerState            sPoint   : register(s1);
Texture2D               gGlassBlur : register(t18);
Texture2D               gScene     : register(t19);
Texture2D               gBlurTmp   : register(t20);

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

float4 PSGlyph(V2P i) : SV_Target {
    Glyph  g     = gGlyphs[i.inst];
    float4 fillC = unpack(g.fill);
    if (g.flags & F_GRADIENT) fillC = lerp(fillC, unpack(g.fill2), saturate(i.grad));
    float  k     = g.alpha * misc.y;

    if ((g.flags & F_SOLID) && (g.flags & F_GLASS)) { // [BF] frosted glass: what is behind, blurred, then the tint
        float2 halfPx = 0.5 / max(fwidth(i.uv), 1e-6);
        float  r      = min(halfPx.x, halfPx.y) * saturate(g.outlineW);
        float2 q      = abs(i.uv - 0.5) * 2.0 * halfPx - (halfPx - r);
        float  d      = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
        float  a      = saturate(0.5 - d / max(1.0, g.glowW));
        float  sw, sh;
        gScene.GetDimensions(sw, sh);
        float3 b   = gGlassBlur.SampleLevel(sLin, i.pos.xy / float2(max(sw, 1.0), max(sh, 1.0)), 0).rgb * g.glowK;
        float3 rgb = lerp(b, fillC.rgb, fillC.a);
        return float4(rgb * a, a) * k;
    }
    if (g.flags & F_SOLID) { // outlineW = corner radius (fraction of the smaller half-extent), glowW = edge softness px
        float2 halfPx = 0.5 / max(fwidth(i.uv), 1e-6);
        float  r      = min(halfPx.x, halfPx.y) * saturate(g.outlineW);
        float2 q      = abs(i.uv - 0.5) * 2.0 * halfPx - (halfPx - r);
        float  d      = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
        float  a      = saturate(0.5 - d / max(1.0, g.glowW)) * fillC.a;
        return float4(fillC.rgb * a, a) * k;
    }

    uint const ai = min(g.atlas, kAtlases - 1);
    if (g.flags & F_IMAGE) { // cover art / icons / thumbnails: straight-alpha rgba, tinted
        Texture2D img   = gAtlas[NonUniformResourceIndex(ai)];
        float2    span  = g.uv.zw - g.uv.xy;
        float2    local = (i.uv - g.uv.xy) / (abs(span.x) > 1e-6 && abs(span.y) > 1e-6 ? span : float2(1, 1));
        float4    t;
        if (g.flags & F_BLUR) { // [BF] 32 taps on a golden-angle spiral, weighted to the middle; edges fade out
            float r = g.glowW;
            t       = 0;
            float wsum = 0;
            [unroll] for (int s = 0; s < 32; ++s) {
                float  rr = sqrt((s + 0.5) / 32.0);
                float  an = s * 2.39996323;
                float2 o  = float2(cos(an), sin(an)) * rr * r * span;
                float  w  = 1.0 - 0.6 * rr;
                t += img.SampleLevel(sLin, i.uv + o, 0) * w;
                wsum += w;
            }
            t /= wsum;
            float e = min(min(local.x, 1.0 - local.x), min(local.y, 1.0 - local.y));
            t.a *= pow(saturate(e / max(r * 1.6, 1e-3)), 1.5);
        } else {
            t = img.Sample(sLin, i.uv);
        }
        t *= fillC;
        if (g.outlineW > 0) { // [BF] rounded corners (outlineW = radius as a fraction of the smaller half-extent)
            float2 halfPx = 0.5 / max(fwidth(local), 1e-6);
            float  r      = min(halfPx.x, halfPx.y) * saturate(g.outlineW);
            float2 q      = abs(local - 0.5) * 2.0 * halfPx - (halfPx - r);
            float  d      = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
            t.a *= saturate(0.5 - d);
        }
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

// ---- [BF] glass blur: back buffer copy (t19) -> quarter size, horizontal (t20 target) -> vertical (t18 target).
// c4 (originTime) = xy: uv step between taps (one output texel along the pass direction), z: sigma in output texels.
// Bilinear pairs (two texels per fetch), like RF's panel blur; the horizontal pass also folds the 4x4 source block in.
float4 PSBlurH(VFull i) : SV_Target {
    float  sigma = max(originTime.z, 0.5);
    float2 stepv = originTime.xy;
    float  sw, sh;
    gScene.GetDimensions(sw, sh);
    float2 px  = float2(0, 1.0 / max(sh, 1.0)) * 1.0; // a second row: the 4x downscale reads 2 rows per tap
    int    n   = (int)min(ceil(sigma * 1.5), 16.0);
    float3 acc = (gScene.SampleLevel(sLin, i.uv - px, 0).rgb + gScene.SampleLevel(sLin, i.uv + px, 0).rgb) * 0.5;
    float  ws  = 1.0;
    for (int k = 0; k < n; ++k) {
        float a = 2.0 * k + 1.0, b = a + 1.0;
        float wa = exp(-a * a / (2.0 * sigma * sigma)), wb = exp(-b * b / (2.0 * sigma * sigma));
        float w = wa + wb, o = (a * wa + b * wb) / max(w, 1e-6);
        float2 off = stepv * o;
        acc += (gScene.SampleLevel(sLin, i.uv + off - px, 0).rgb + gScene.SampleLevel(sLin, i.uv + off + px, 0).rgb +
                gScene.SampleLevel(sLin, i.uv - off - px, 0).rgb + gScene.SampleLevel(sLin, i.uv - off + px, 0).rgb) * 0.5 * w;
        ws += 2.0 * w;
    }
    return float4(acc / ws, 1);
}
float4 PSBlurV(VFull i) : SV_Target {
    float  sigma = max(originTime.z, 0.5);
    float2 stepv = originTime.xy;
    int    n     = (int)min(ceil(sigma * 1.5), 16.0);
    float3 acc   = gBlurTmp.SampleLevel(sLin, i.uv, 0).rgb;
    float  ws    = 1.0;
    for (int k = 0; k < n; ++k) {
        float a = 2.0 * k + 1.0, b = a + 1.0;
        float wa = exp(-a * a / (2.0 * sigma * sigma)), wb = exp(-b * b / (2.0 * sigma * sigma));
        float w = wa + wb, o = (a * wa + b * wb) / max(w, 1e-6);
        acc += (gBlurTmp.SampleLevel(sLin, i.uv + stepv * o, 0).rgb + gBlurTmp.SampleLevel(sLin, i.uv - stepv * o, 0).rgb) * w;
        ws += 2.0 * w;
    }
    return float4(acc / ws, 1);
}
