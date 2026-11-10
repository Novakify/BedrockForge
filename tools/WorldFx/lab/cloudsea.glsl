// Cloud Sea - shared look code (lab build, GLSL ES 3.0). Everything here only uses what the game hands a shader:
// a view direction + camera position for the sky, and albedo / face normal / vertex AO / world position for blocks.
// Colours are linear until toDisplay().
uniform sampler2D NOISE;   // 256x256 tileable value noise (R,G,B,A independent)
uniform sampler2D PUFF;    // baked tileable cloud-top height (R, 0..1): domes on domes
uniform float TIME;
uniform vec3 CAM;

const vec3 SUN = vec3(0.17605, 0.11247, -0.97794);       // low sun, ~6.5 degrees up
const vec3 SUN_COL = vec3(1.00, 0.52, 0.22);              // direct light after a long path through air
const float SEA_Y = 12.0;                                 // cloud deck base (world y)
const float SEA_AMP = 24.0;
const vec2 MAP_C = vec2(0.0, -25.0);                      // towers keep clear of the map

float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return textureLod(NOISE, (i + f + 0.5) / 256.0, 0.0).r;
}
float vnoiseG(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return textureLod(NOISE, (i + f + 0.5) / 256.0, 0.0).g;
}
const mat2 ROT = mat2(0.8, 0.6, -0.6, 0.8);
float fbm(vec2 p, int oct) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < oct; i++) { s += a * vnoise(p); p = ROT * p * 2.03 + 17.7; a *= 0.5; }
    return s;
}

// ---------------------------------------------------------------- sky gradient
// One gradient, three colour families: slate violet overhead, a rose/lavender body, an orange band that is only
// hot near the sun. Opposite the sun the horizon carries the cool earth shadow with a pink belt above it.
vec3 skyBase(vec3 d) {
    float e = max(d.y, 0.0);
    float mu = dot(normalize(vec3(d.x, max(d.y, 0.0), d.z)), SUN);
    float sunSide = mu * 0.5 + 0.5;
    float towardSun = pow(sunSide, 5.0);
    vec3 zen = vec3(0.036, 0.042, 0.135);
    vec3 body = mix(vec3(0.085, 0.080, 0.215), vec3(0.235, 0.120, 0.235), pow(sunSide, 1.6));
    vec3 hot = vec3(1.02, 0.300, 0.040);
    vec3 warm = vec3(0.62, 0.185, 0.085);
    vec3 belt = vec3(0.290, 0.150, 0.225);
    vec3 shadow = vec3(0.250, 0.170, 0.245);   // the sun is still up: no earth shadow yet, just front-lit haze
    vec3 hor = mix(mix(shadow, belt, smoothstep(0.0, 0.08, e)), mix(warm, hot, towardSun), smoothstep(0.30, 0.92, sunSide));
    float kb = mix(0.10, 0.24, towardSun); // the warm band stands taller under the sun
    vec3 c = mix(hor, body, smoothstep(0.0, kb, e));
    c = mix(c, zen, smoothstep(0.06, 0.62, e));
    float ang = acos(clamp(dot(d, SUN), -1.0, 1.0));
    c += vec3(1.05, 0.50, 0.10) * exp(-ang * 6.0) * exp(-e * 8.0) * 0.75; // yellow core on the horizon under the sun
    return c;
}

// ---------------------------------------------------------------- high clouds (seen from below, lit from below)
float hcDens(vec2 p, int oct) {
    float cover = fbm(p * 0.30 + 3.1, 4);
    vec2 w = p * vec2(3.4, 4.6);
    w += (vec2(vnoiseG(p * 1.3), vnoiseG(p * 1.3 + 31.0)) - 0.5) * 1.6;
    float shape = fbm(w, oct);
    return cover * 1.05 + shape * 0.40;
}
vec4 highClouds(vec3 d, float mu) {
    if (d.y < 0.012) return vec4(0.0);
    vec2 sunXZ = normalize(SUN.xz);
    vec2 q = CAM.xz + d.xz * ((900.0 - CAM.y) / d.y);
    float fade = smoothstep(0.012, 0.085, d.y);
    vec2 p = q / 520.0 + TIME * vec2(0.0030, 0.0011);
    float dn = hcDens(p, 5);
    float fine = fbm(p * 21.0, 3);
    float near = smoothstep(0.05, 0.30, d.y);                                   // far cloud is too small to show its ragged edge
    float dens = smoothstep(0.765, 0.830, dn - (fine - 0.5) * 0.10 * near);
    float tau = smoothstep(0.765, 0.965, dn);                                   // how thick the cloud is here
    float dnS = hcDens(p + sunXZ * 0.040, 4);                                   // one tap toward the sun
    float shadeS = smoothstep(0.765, 0.93, dnS);
    // cirrus: long streaks that converge on the sun
    vec2 along = vec2(dot(q, sunXZ), dot(q, vec2(-sunXZ.y, sunXZ.x)));
    vec2 pc = vec2(along.x / 3400.0, along.y / 760.0) + TIME * vec2(0.0015, 0.0);
    pc += (vec2(fbm(pc * 1.7 + 9.0, 3), fbm(pc * 1.7 + 23.0, 3)) - 0.5) * 0.9;      // combed, then curled
    float wisp = fbm(pc * vec2(1.0, 2.2), 5);
    float cir = smoothstep(0.46, 0.80, wisp);
    cir *= cir * smoothstep(0.40, 0.66, fbm(q / 2300.0 + 40.0, 3));

    float sunSide = mu * 0.5 + 0.5;
    vec3 lit = mix(vec3(0.46, 0.130, 0.170), vec3(1.10, 0.300, 0.050), pow(sunSide, 2.0));   // dusty rose far off, orange near
    lit = mix(lit, vec3(1.55, 0.74, 0.20), pow(max(mu, 0.0), 9.0));                          // gold right by the sun
    vec3 shade = mix(vec3(0.062, 0.052, 0.120), vec3(0.120, 0.070, 0.135), sunSide);          // purple-grey bodies
    float ripple = fbm(ROT * p * vec2(9.0, 15.0), 3) - 0.5;                    // the rippled underside of altocumulus
    float back = exp(-2.6 * tau) * (1.0 - 0.65 * shadeS);
    float front = 0.92 - 0.46 * tau - 0.20 * shadeS;                            // thin parts glow, heavy parts go mauve
    float litF = clamp(mix(front, back, smoothstep(0.25, 0.80, sunSide)) * (1.0 + 0.7 * ripple * near), 0.0, 1.0);
    vec3 col = mix(shade, lit, litF);
    float a = dens * (0.50 + 0.50 * smoothstep(0.0, 0.5, tau)) * fade;
    vec3 cirCol = mix(vec3(0.42, 0.130, 0.20), vec3(1.0, 0.36, 0.10), pow(sunSide, 3.0));
    float ac = cir * 0.34 * fade * (1.0 - a);
    return vec4(col * a + cirCol * ac, a + ac);
}

// ---------------------------------------------------------------- the cloud sea (a height field below the map)
float seaH(vec2 w, float lod) {
    vec2 drift = TIME * vec2(0.9, 0.35);
    vec2 p = (w + drift) / 300.0;
    float big = textureLod(PUFF, ROT * p * 0.31 + 0.37, max(lod - 1.7, 0.0)).r;      // slow swells, ~150 blocks across
    float h = textureLod(PUFF, p, lod).r;                                            // puffs, ~45 blocks, with smaller ones riding on them
    float far = smoothstep(240.0, 520.0, length(w - MAP_C));
    float tw = smoothstep(0.56, 0.78, vnoise(w / 760.0 + 2.7)) * far;
    return h * (0.42 + 0.55 * big) + big * 0.55 + tw * (1.6 + 3.6 * big + 1.5 * h);
}
float seaY(vec2 w, float lod) { return SEA_Y + SEA_AMP * seaH(w, lod); }

// returns colour (linear) and coverage in a; sunMask says how much of the sun's glow gets through this pixel
vec4 cloudSea(vec3 ro, vec3 rd, out float tHit) {
    tHit = -1.0;
    float yTop = SEA_Y + SEA_AMP * 7.6;
    if (rd.y > 0.0 && ro.y > yTop) return vec4(0.0);
    float t = 0.0;
    if (ro.y > yTop) t = (ro.y - yTop) / -rd.y;
    float tMax = 3400.0;
    bool hit = false;
    float lod = 0.0;
    float tPrev = t;
    for (int i = 0; i < 80; i++) {
        vec3 p = ro + rd * t;
        lod = max(log2(t * 0.011), 0.0);
        float dh = p.y - seaY(p.xz, lod);
        if (dh < 0.004 * t + 0.03) { hit = true; break; }
        tPrev = t;
        t += max(dh * 0.40, 0.008 * t + 0.30);
        if (t > tMax || (rd.y > 0.0 && p.y > yTop)) break;
    }
    if (!hit) return vec4(0.0);
    for (int i = 0; i < 6; i++) {                                     // pin the surface down (no terracing)
        float tm = 0.5 * (tPrev + t);
        vec3 pm = ro + rd * tm;
        if (pm.y - seaY(pm.xz, lod) < 0.0) t = tm; else tPrev = tm;
    }
    tHit = t;
    vec3 p = ro + rd * t;
    float e = 2.2 + t * 0.006;
    float h0 = seaY(p.xz, lod);
    float nl = lod + 1.2;
    float hc0 = seaY(p.xz, nl);
    vec3 n = normalize(vec3(hc0 - seaY(p.xz + vec2(e, 0.0), nl), e, hc0 - seaY(p.xz + vec2(0.0, e), nl)));
    vec2 l2 = normalize(SUN.xz);
    float slope = SUN.y / length(SUN.xz);
    // how much sun reaches this spot: walk toward the sun and see how much cloud stands in the way
    float sh = 1.0;
    for (int i = 0; i < 5; i++) {
        float s = 7.0 * pow(2.3, float(i));
        float over = seaY(p.xz + l2 * s, lod + 1.0 + float(i) * 0.6) - (h0 + slope * s);
        sh *= 1.0 - smoothstep(-0.05 * s, 0.20 * s, over) * 0.78;
    }
    float ndl = dot(n, SUN);
    float grain = fbm(p.xz / 5.0 + 3.0, 4) + 0.5 * vnoiseG(p.xz / 1.3);               // cloud is never smooth
    grain = mix(0.75, grain, exp(-t / 260.0));
    float cav = clamp((h0 - seaY(p.xz, lod + 3.4)) / (SEA_AMP * 0.13) + 0.55, 0.0, 1.0);   // bumps bright, hollows dark
    float lit = sh * (0.30 + 0.70 * smoothstep(-0.35, 0.55, ndl)) * (0.72 + 0.38 * grain) * (0.34 + 0.66 * cav);
    float hRel = (h0 - SEA_Y) / SEA_AMP;
    float crease = smoothstep(0.05, 0.85, hRel);                       // down between the puffs it goes dark
    float mu = dot(rd, SUN);
    vec3 amb = mix(vec3(0.020, 0.023, 0.070), vec3(0.066, 0.062, 0.155), crease) * (0.60 + 0.40 * n.y) * (0.85 + 0.2 * grain);
    amb += vec3(0.22, 0.075, 0.040) * max(dot(normalize(n.xz + 1e-4), l2), 0.0) * (1.0 - n.y) * 0.5 * crease;  // glow of the bright horizon
    float phase = mix(0.60, 1.0, smoothstep(-1.0, 0.35, mu));         // cloud throws light forward: dimmer with the sun behind you
    vec3 direct = mix(vec3(0.95, 0.36, 0.20), vec3(1.00, 0.335, 0.105), smoothstep(-1.0, 0.35, mu)) * 1.02 * lit * phase;
    // light bleeding through thin crests when you look toward the sun
    float crest = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);
    vec3 through = vec3(1.5, 0.62, 0.18) * crest * pow(max(mu, 0.0), 5.0) * sh * 0.9;
    vec3 col = amb + direct + through;
    // distance: melt into the horizon colour for that direction
    float f = clamp((1.0 - exp(-t / 1000.0)) / (1.0 - exp(-3.4)), 0.0, 1.0);
    vec3 hz = skyBase(normalize(vec3(rd.x, max(rd.y, 0.0) + 0.030, rd.z)));
    col = mix(col, hz, f * f * (3.0 - 2.0 * f));
    return vec4(col, 1.0);
}

// ---------------------------------------------------------------- whole background for one view ray
// rgb = colour (linear), a = how much sun glow this pixel lets through (feeds the light shafts)
vec4 background(vec3 ro, vec3 rd) {
    float mu = dot(rd, SUN);
    vec3 col = skyBase(rd);
    float ang = acos(clamp(mu, -1.0, 1.0));
    float pass = 1.0;
    vec4 hc = highClouds(rd, mu);
    // sun: a small disc with a tight aureole; the wide glow is added by the shaft pass so things can shadow it
    float disc = smoothstep(0.0125, 0.0095, ang);
    vec3 sun = vec3(3.2, 2.3, 1.25) * disc + vec3(1.5, 0.78, 0.30) * exp(-ang * 38.0) * 0.8;
    col += sun * (1.0 - hc.a * 0.85);
    col = col * (1.0 - hc.a) + hc.rgb;
    float tHit;
    vec4 sea = cloudSea(ro, rd, tHit);
    if (sea.a > 0.0) { col = sea.rgb; pass = 0.0; }
    float glow = exp(-ang * 3.2);
    return vec4(col, clamp(glow * pass, 0.0, 1.0));
}

// ---------------------------------------------------------------- block lighting (what the terrain shader does)
// albedo linear, n = face normal, ao = the game's smooth-lighting term (0..1), wp = world position
vec3 blockLight(vec3 alb, vec3 n, float ao, vec3 wp) {
    // pure greens are the one colour that refuses to sit in a sunset: ease them toward olive (other colours untouched)
    float green = clamp((alb.g - max(alb.r, alb.b)) * 5.0, 0.0, 1.0);
    alb = mix(alb, dot(alb, vec3(0.30, 0.59, 0.11)) * vec3(1.22, 0.98, 0.42), green * 0.42);
    float ndl = max(dot(n, SUN), 0.0);
    vec3 direct = vec3(1.00, 0.60, 0.32) * 2.35 * (ndl + 0.035 * max(n.y, 0.0));   // rough tops still catch the grazing light
    // sky light by face direction: violet from above, a dim warm bounce off the cloud sea from below,
    // and the bright horizon itself lighting whatever faces it
    vec3 up = vec3(0.098, 0.100, 0.185);
    vec3 down = vec3(0.150, 0.092, 0.092);
    vec3 amb = mix(down, up, n.y * 0.5 + 0.5);
    vec2 hn = n.xz;
    float toSun = dot(hn, normalize(SUN.xz));
    amb += vec3(0.42, 0.17, 0.07) * max(toSun, 0.0) * (1.0 - abs(n.y));
    amb += vec3(0.060, 0.052, 0.082) * max(-toSun, 0.0) * (1.0 - abs(n.y));
    float aoA = ao * ao;
    vec3 c = alb * (direct * mix(0.55, 1.0, ao) + amb * aoA);
    return c;
}
// air between the camera and the block: far things drift toward the sky behind them
vec3 blockHaze(vec3 c, vec3 wp) {
    vec3 v = wp - CAM;
    float dist = length(v);
    vec3 d = v / max(dist, 0.001);
    float f = 1.0 - exp(-dist / 520.0);
    vec3 hz = skyBase(normalize(vec3(d.x, max(d.y, 0.0) * 0.5 + 0.03, d.z)));
    return mix(c, hz, f * 0.85);
}

vec3 toDisplay(vec3 c) {
    c = max(c, 0.0);
    vec3 k = max(c - 0.55, 0.0);
    c = min(c, 0.55) + k / (1.0 + k * 1.9);   // linear up to 0.55, then a soft shoulder (tops out near 1.08 -> clipped)
    return pow(c, vec3(1.0 / 2.2));
}
vec3 fromDisplay(vec3 c) { return pow(max(c, 0.0), vec3(2.2)); }
