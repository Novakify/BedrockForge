// Cloud Sea lab: renders a small Bed Wars style scene (voxel islands, vanilla textures) with the Cloud Sea look in
// headless Chromium/WebGL2. Same passes as the game: background -> blocks (colour+depth) -> one screen pass.
//   node lab.js <tiles.json> <out_dir> [w h] [shot,shot|all] [t] [orbit frames]
const fs = require('fs'), path = require('path');
const { chromium } = require('playwright');
const [tilesFile, outDir, W = '1280', H = '720', which = 'all', tArg = '0', orbitN = '0'] = process.argv.slice(2);
const glsl = fs.readFileSync(path.join(__dirname, 'cloudsea.glsl'), 'utf8');
const tiles = JSON.parse(fs.readFileSync(tilesFile, 'utf8'));
const HEAD = '#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\nprecision highp sampler2DArray;\n';

const bgVS = HEAD + `in vec2 P; uniform vec3 F, R, U; uniform float TX, TY; out vec3 dir;
void main(){ gl_Position = vec4(P, 0.9999, 1.0); dir = F + R * (P.x * TX) + U * (P.y * TY); }`;
const bgFS = HEAD + glsl + `in vec3 dir; out vec4 o;
void main(){ vec4 b = background(CAM, normalize(dir)); o = vec4(toDisplay(b.rgb), b.a); }`;
const blkVS = HEAD + `in vec3 POS; in vec4 DAT; in vec2 UV; uniform mat4 VP; out vec3 wp; out vec2 uv; flat out float layer; flat out float nid; out float ao;
void main(){ wp = POS; uv = UV; layer = DAT.x; nid = DAT.y; ao = DAT.z; gl_Position = VP * vec4(POS, 1.0); }`;
const blkFS = HEAD + glsl + `uniform sampler2DArray TILES; uniform float VANILLA;
in vec3 wp; in vec2 uv; flat in float layer; flat in float nid; in float ao; out vec4 o;
const vec3 NRM[6] = vec3[6](vec3(1,0,0), vec3(-1,0,0), vec3(0,1,0), vec3(0,-1,0), vec3(0,0,1), vec3(0,0,-1));
void main(){
  vec4 t = texture(TILES, vec3(uv, layer));
  if (t.a < 0.5) discard;
  vec3 n = NRM[int(nid)];
  vec3 alb = fromDisplay(t.rgb);
  vec3 c;
  if (VANILLA > 0.5) { // the game's own look: fixed face shades, no sun
    float sh = n.y > 0.5 ? 1.0 : (n.y < -0.5 ? 0.5 : (abs(n.x) > 0.5 ? 0.6 : 0.8));
    c = alb * sh * sh * mix(0.35, 1.0, ao) * 1.0;
    o = vec4(pow(c, vec3(1.0 / 2.2)), 0.0); return;
  }
  c = blockHaze(blockLight(alb, n, ao, wp), wp);
  o = vec4(toDisplay(c), 0.0);
}`;
const postVS = HEAD + `in vec2 P; out vec2 uv; void main(){ gl_Position = vec4(P, 0.0, 1.0); uv = P * 0.5 + 0.5; }`;
const postFS = HEAD + `uniform sampler2D SCENE, DEPTH; uniform vec3 SUNSCR; uniform vec2 RES; uniform float VANILLA;
in vec2 uv; out vec4 o;
float hash(vec2 p){ p = fract(p * vec2(0.1031, 0.1030)); p += dot(p, p.yx + 33.33); return fract((p.x + p.y) * p.x); }
void main(){
  vec4 s = texture(SCENE, uv);
  vec3 c = s.rgb;
  if (VANILLA > 0.5) { o = vec4(c, 1.0); return; }
  float d = texture(DEPTH, uv).r;
  float j = hash(gl_FragCoord.xy);
  vec2 asp = vec2(RES.x / RES.y, 1.0);
  // light shafts: walk toward the sun through a blurred copy of "how much glow gets through"
  if (SUNSCR.z > 0.0) {
    vec2 to = SUNSCR.xy - uv;
    float acc = 0.0, wsum = 0.0;
    const int N = 28;
    for (int i = 0; i < N; i++) {
      float f = (float(i) + j) / float(N);
      vec2 q = uv + to * f;
      float w = 1.0 - f * 0.35;
      float m = (q.x < 0.0 || q.x > 1.0 || q.y < 0.0 || q.y > 1.0) ? 0.30 : textureLod(SCENE, q, 4.6).a;
      acc += m * w; wsum += w;
    }
    acc /= wsum;
    float dist = length(to * asp);
    float fall = exp(-dist * 2.3);
    vec3 shaft = vec3(1.0, 0.47, 0.17) * acc * fall * 0.80 * SUNSCR.z;
    vec3 lin = pow(c, vec3(2.2));
    lin += shaft * (1.0 - lin * 0.55);
    // rim: a block edge with open sky right behind it, sunward, catches a thin line of light
    if (d < 0.99999) {
      vec2 dir = normalize(to * asp) / asp;
      float open = 0.0;
      for (int k = 1; k <= 2; k++) { vec2 q = uv + dir * (float(k) * 1.5 / RES.y); if (texture(DEPTH, q).r >= 0.99999) open += texture(SCENE, q).a; }
      lin += vec3(1.5, 0.74, 0.30) * min(open, 1.0) * exp(-dist * 2.4) * 0.55;
    }
    c = pow(lin, vec3(1.0 / 2.2));
  }
  c += (j - 0.5) / 255.0 + (hash(gl_FragCoord.yx + 7.0) - 0.5) / 255.0; // dither: no banding in the gradients
  o = vec4(c, 1.0);
}`;

function pageMain(a) {
  const { W, H, tiles, src, shots, T, orbitN } = a;
  const c = document.createElement('canvas'); c.width = W; c.height = H;
  const gl = c.getContext('webgl2', { antialias: false, preserveDrawingBuffer: true, premultipliedAlpha: false });
  if (!gl) return { err: 'no webgl2' };
  const sh = (type, s) => { const x = gl.createShader(type); gl.shaderSource(x, s); gl.compileShader(x);
    if (!gl.getShaderParameter(x, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(x) + '\n' + s.split('\n').slice(0, 0).join('\n')); return x; };
  const prog = (v, f) => { const p = gl.createProgram(); gl.attachShader(p, sh(gl.VERTEX_SHADER, v)); gl.attachShader(p, sh(gl.FRAGMENT_SHADER, f));
    gl.linkProgram(p); if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(p)); return p; };
  const U = (p, n) => gl.getUniformLocation(p, n);

  // ---- noise texture (tileable value noise, 4 channels)
  let seed = 1234567;
  const rnd = () => { seed = (seed * 1664525 + 1013904223) >>> 0; return seed / 4294967296; };
  const nz = new Uint8Array(256 * 256 * 4); for (let i = 0; i < nz.length; i++) nz[i] = Math.floor(rnd() * 256);
  const noise = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D, noise);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, 256, 256, 0, gl.RGBA, gl.UNSIGNED_BYTE, nz);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);

  // ---- baked cloud-top height: tileable domes at three sizes, small ones riding on the big ones
  const PN = 1024, puff = new Float32Array(PN * PN);
  function domes(N, rMin, rMax, sd) {
    const pts = []; let s2 = sd;
    const r2 = () => { s2 = (s2 * 1664525 + 1013904223) >>> 0; return s2 / 4294967296; };
    for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) pts.push([(i + 0.15 + 0.7 * r2()) / N, (j + 0.15 + 0.7 * r2()) / N, (rMin + (rMax - rMin) * r2()) / N, 0.55 + 0.45 * r2()]);
    const o = new Float32Array(PN * PN);
    for (let y = 0; y < PN; y++) for (let x = 0; x < PN; x++) {
      const u = (x + 0.5) / PN, v2 = (y + 0.5) / PN, ci = Math.floor(u * N), cj = Math.floor(v2 * N); let m = 0;
      for (let dj = -1; dj <= 1; dj++) for (let di = -1; di <= 1; di++) {
        const p = pts[((cj + dj + N) % N) * N + ((ci + di + N) % N)];
        let dx = Math.abs(u - p[0]), dy = Math.abs(v2 - p[1]); if (dx > 0.5) dx = 1 - dx; if (dy > 0.5) dy = 1 - dy;
        const q = (dx * dx + dy * dy) / (p[2] * p[2]); if (q < 1) { const hgt = Math.pow(1 - q, 0.8) * p[3]; if (hgt > m) m = hgt; }
      }
      o[y * PN + x] = m;
    }
    return o;
  }
  const d1 = domes(6, 0.62, 0.98, 11), d2 = domes(15, 0.60, 0.95, 23), d3 = domes(37, 0.55, 0.95, 37), d4 = domes(83, 0.5, 0.95, 51);
  for (let i = 0; i < PN * PN; i++) puff[i] = d1[i] * 0.60 + d2[i] * 0.26 * (0.35 + 0.65 * d1[i]) + d3[i] * 0.10 * (0.3 + 0.7 * d2[i]) + d4[i] * 0.04;
  const puffT = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D, puffT);
  { let lvl = 0, n = PN, cur = puff;
    while (true) { gl.texImage2D(gl.TEXTURE_2D, lvl, gl.R16F, n, n, 0, gl.RED, gl.FLOAT, cur); if (n === 1) break;
      const m = n >> 1, nx = new Float32Array(m * m);
      for (let y = 0; y < m; y++) for (let x = 0; x < m; x++) nx[y * m + x] = (cur[2 * y * n + 2 * x] + cur[2 * y * n + 2 * x + 1] + cur[(2 * y + 1) * n + 2 * x] + cur[(2 * y + 1) * n + 2 * x + 1]) / 4;
      cur = nx; n = m; lvl++; } }
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
  // ---- tiles
  const names = Object.keys(tiles), L = {}; names.forEach((n, i) => L[n] = i);
  const arr = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D_ARRAY, arr);
  gl.texStorage3D(gl.TEXTURE_2D_ARRAY, 1, gl.RGBA8, 16, 16, names.length);
  names.forEach((n, i) => gl.texSubImage3D(gl.TEXTURE_2D_ARRAY, 0, 0, 0, i, 16, 16, 1, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array(a.tilePx[n])));
  gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_MIN_FILTER, gl.NEAREST); gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_WRAP_S, gl.REPEAT); gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_WRAP_T, gl.REPEAT);

  // ---- world
  const SX = 200, SY = 64, SZ = 200, OX = -100, OY = 36, OZ = -140;
  const vox = new Uint8Array(SX * SY * SZ);
  const B = { grass: 1, dirt: 2, stone: 3, cobble: 4, log: 5, leaves: 6, planks: 7, ywool: 8, rwool: 9, yconc: 10, endstone: 11, sand: 12, brick: 13, mred: 14, mstem: 15, clay: 16, mossy: 17, spruce: 18, obsid: 19, andes: 20, gravel: 21, moss: 22 };
  const TEX = { 1: ['grass_side_carried', 'grass_carried', 'dirt'], 2: 'dirt', 3: 'stone', 4: 'cobblestone', 5: ['log_oak', 'log_oak_top', 'log_oak_top'], 6: 'leaves_oak_carried', 7: 'planks_oak', 8: 'wool_colored_yellow', 9: 'wool_colored_red', 10: 'concrete_yellow', 11: 'end_stone', 12: 'sandstone_normal', 13: 'stonebrick', 14: 'mushroom_block_skin_red', 15: 'mushroom_block_skin_stem', 16: 'hardened_clay_stained_white', 17: 'cobblestone_mossy', 18: 'planks_spruce', 19: 'obsidian', 20: 'stone_andesite', 21: 'gravel', 22: 'moss_block' };
  const idx = (x, y, z) => (x - OX) + SX * ((z - OZ) + SZ * (y - OY));
  const inb = (x, y, z) => x >= OX && x < OX + SX && y >= OY && y < OY + SY && z >= OZ && z < OZ + SZ;
  const set = (x, y, z, b) => { if (inb(x, y, z)) vox[idx(x, y, z)] = b; };
  const get = (x, y, z) => inb(x, y, z) ? vox[idx(x, y, z)] : 0;
  const h2 = (x, z, s) => { let n = Math.sin(x * 127.1 + z * 311.7 + s * 74.7) * 43758.5453; return n - Math.floor(n); };
  const sn = (x, z, s) => { const xi = Math.floor(x), zi = Math.floor(z), fx = x - xi, fz = z - zi, u = fx * fx * (3 - 2 * fx), v = fz * fz * (3 - 2 * fz);
    return (h2(xi, zi, s) * (1 - u) + h2(xi + 1, zi, s) * u) * (1 - v) + (h2(xi, zi + 1, s) * (1 - u) + h2(xi + 1, zi + 1, s) * u) * v; };
  function island(cx, cy, cz, r, seedI, depth) {
    for (let x = -r - 2; x <= r + 2; x++) for (let z = -r - 2; z <= r + 2; z++) {
      const rr = Math.hypot(x, z) / (r * (0.80 + 0.35 * sn((x + 50) * 0.21, (z + 50) * 0.21, seedI)));
      if (rr > 1) continue;
      const edge = 1 - rr;
      const bump = Math.round(sn(x * 0.16 + 9, z * 0.16 + 9, seedI + 3) * 2.2 * Math.min(1, edge * 3));
      const top = cy + bump;
      const dep = Math.max(1, Math.round(depth * Math.pow(edge, 0.62) * (0.55 + 0.9 * sn(x * 0.33, z * 0.33, seedI + 7)) + 1));
      for (let k = 0; k < dep; k++) {
        const y = top - k;
        let b = k === 0 ? B.grass : (k < 3 ? B.dirt : (h2(x + k, z - k, seedI) < 0.16 ? B.andes : (h2(x - k, z + k, seedI + 1) < 0.12 ? B.cobble : B.stone)));
        set(cx + x, y, cz + z, b);
      }
    }
  }
  const topY = (x, z) => { for (let y = OY + SY - 1; y >= OY; y--) { const b = get(x, y, z); if (b && b !== B.leaves && b !== B.log) return y; } return -1; };
  function tree(x, z, hgt) {
    const y0 = topY(x, z); if (y0 < 0) return;
    for (let k = 1; k <= hgt; k++) set(x, y0 + k, z, B.log);
    const ty = y0 + hgt;
    for (let dy = -2; dy <= 2; dy++) { const rad = dy <= -1 ? 2 : (dy === 2 ? 1 : (dy === 1 ? 1 : 2));
      for (let dx = -rad; dx <= rad; dx++) for (let dz = -rad; dz <= rad; dz++) {
        if (Math.abs(dx) === rad && Math.abs(dz) === rad && (rad === 1 || h2(x + dx, z + dz, dy + 5) < 0.55)) continue;
        if (!get(x + dx, ty + dy, z + dz)) set(x + dx, ty + dy, z + dz, B.leaves);
      } }
  }
  function mushroom(x, z, hgt, rad) {
    const y0 = topY(x, z); if (y0 < 0) return;
    for (let k = 1; k <= hgt; k++) set(x, y0 + k, z, B.mstem);
    for (let dx = -rad; dx <= rad; dx++) for (let dz = -rad; dz <= rad; dz++) {
      const corner = Math.abs(dx) === rad && Math.abs(dz) === rad; if (corner) continue;
      set(x + dx, y0 + hgt + 1, z + dz, B.mred);
      if (Math.abs(dx) === rad || Math.abs(dz) === rad) set(x + dx, y0 + hgt, z + dz, B.mred);
    }
  }
  function box(x0, y0, z0, x1, y1, z1, b, hollow) { for (let x = x0; x <= x1; x++) for (let y = y0; y <= y1; y++) for (let z = z0; z <= z1; z++) {
    if (hollow && x > x0 && x < x1 && z > z0 && z < z1 && y < y1) continue; set(x, y, z, b); } }
  // player's island
  island(0, 64, 0, 11, 1, 13);
  tree(-6, 4, 5); tree(7, 5, 4);
  // bed + a half-built defence
  set(3, 65, 2, B.rwool); set(4, 65, 2, B.rwool);
  box(2, 65, 0, 5, 65, 0, B.endstone); box(2, 65, 4, 5, 65, 4, B.endstone); set(1, 65, 1, B.endstone); set(1, 65, 2, B.endstone); set(1, 65, 3, B.endstone);
  set(6, 65, 1, B.endstone); set(6, 65, 3, B.endstone); box(2, 66, 1, 5, 66, 3, B.ywool); set(3, 66, 0, B.endstone);
  // generator hut
  box(-4, 65, -6, -1, 67, -3, B.spruce, true); box(-5, 68, -7, 0, 68, -2, B.planks); set(-4, 65, -3, 0); set(-4, 66, -3, 0);
  // bridge toward the middle
  for (let z = -11; z >= -34; z--) set(0, 64, z, z > -27 ? B.ywool : (h2(z, 1, 2) < 0.5 ? B.ywool : B.yconc));
  // middle island (sunward): mushrooms + a tower
  island(-4, 66, -62, 15, 4, 17);
  mushroom(-10, -58, 7, 3); mushroom(3, -66, 5, 2); tree(-2, -55, 5);
  box(4, 67, -60, 8, 74, -56, B.brick, true); box(3, 75, -61, 9, 75, -55, B.cobble); box(4, 76, -60, 8, 76, -56, B.brick, true);
  set(6, 70, -56, 0); set(6, 71, -56, 0); set(6, 72, -60, 0);
  // small diamond island between
  island(15, 62, -30, 5, 9, 7); tree(15, -30, 4); set(13, 63, -29, B.obsid);
  // side island (lit from the side when you face it)
  island(58, 64, -14, 10, 12, 12); tree(60, -18, 5); tree(53, -10, 4);
  box(56, 65, -14, 61, 67, -10, B.clay, true); box(55, 68, -15, 62, 68, -9, B.sand); set(58, 65, -10, 0); set(58, 66, -10, 0);
  // island behind (front-lit view)
  island(-8, 65, 52, 12, 20, 14); tree(-12, 50, 5); mushroom(-3, 56, 6, 3); tree(-5, 46, 4);
  // far islands for depth
  island(-70, 68, -96, 12, 30, 14); tree(-72, -98, 6); mushroom(-66, -92, 6, 2);
  island(64, 70, -118, 10, 33, 12); tree(64, -118, 5);
  island(-80, 62, 10, 9, 36, 11); tree(-80, 10, 5);

  // ---- mesh
  const FACES = [ // normal id, corners (ccw), uv
    [0, [1, 0, 0], [[1, 0, 1], [1, 0, 0], [1, 1, 0], [1, 1, 1]]], [1, [-1, 0, 0], [[0, 0, 0], [0, 0, 1], [0, 1, 1], [0, 1, 0]]],
    [2, [0, 1, 0], [[0, 1, 1], [1, 1, 1], [1, 1, 0], [0, 1, 0]]], [3, [0, -1, 0], [[0, 0, 0], [1, 0, 0], [1, 0, 1], [0, 0, 1]]],
    [4, [0, 0, 1], [[0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1]]], [5, [0, 0, -1], [[1, 0, 0], [0, 0, 0], [0, 1, 0], [1, 1, 0]]]];
  const UVQ = [[0, 1], [1, 1], [1, 0], [0, 0]];
  const solidAO = (x, y, z) => { const b = get(x, y, z); return b && b !== B.leaves ? 1 : (b === B.leaves ? 0.5 : 0); };
  const v = [];
  for (let y = OY; y < OY + SY; y++) for (let z = OZ; z < OZ + SZ; z++) for (let x = OX; x < OX + SX; x++) {
    const b = vox[idx(x, y, z)]; if (!b) continue;
    for (const [nid, n, cs] of FACES) {
      const nb = get(x + n[0], y + n[1], z + n[2]);
      if (nb && nb !== B.leaves) continue;
      if (nb === B.leaves && b === B.leaves && nid % 2 === 1) continue;
      const t = TEX[b]; const tn = typeof t === 'string' ? t : (nid === 2 ? t[1] : (nid === 3 ? t[2] : t[0]));
      const layer = L[tn];
      // tangent axes for AO
      const ax = [0, 1, 2].filter(i => n[i] === 0);
      const quad = cs.map((cn, ci) => {
        const s = [0, 0, 0], t2 = [0, 0, 0]; s[ax[0]] = cn[ax[0]] ? 1 : -1; t2[ax[1]] = cn[ax[1]] ? 1 : -1;
        const bx = x + n[0], by = y + n[1], bz = z + n[2];
        const s1 = solidAO(bx + s[0], by + s[1], bz + s[2]), s2 = solidAO(bx + t2[0], by + t2[1], bz + t2[2]);
        const co = solidAO(bx + s[0] + t2[0], by + s[1] + t2[1], bz + s[2] + t2[2]);
        const occ = (s1 >= 1 && s2 >= 1) ? 3 : (s1 + s2 + co);
        return [x + cn[0], y + cn[1], z + cn[2], UVQ[ci][0], UVQ[ci][1], layer, nid, 1 - occ * 0.2];
      });
      const flip = quad[0][7] + quad[2][7] < quad[1][7] + quad[3][7];
      const order = flip ? [1, 2, 3, 1, 3, 0] : [0, 1, 2, 0, 2, 3];
      for (const i of order) v.push(...quad[i]);
    }
  }
  const vb = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, vb); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(v), gl.STATIC_DRAW);
  const nVerts = v.length / 8;
  const qb = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, qb); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]), gl.STATIC_DRAW);

  const pBg = prog(src.bgVS, src.bgFS), pBlk = prog(src.blkVS, src.blkFS), pPost = prog(src.postVS, src.postFS);
  // ---- targets
  const scene = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D, scene);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, W, H, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  const depth = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D, depth);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.DEPTH_COMPONENT32F, W, H, 0, gl.DEPTH_COMPONENT, gl.FLOAT, null);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  const fbo = gl.createFramebuffer(); gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
  gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, scene, 0);
  gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.TEXTURE_2D, depth, 0);

  const SUN = [0.17605, 0.11247, -0.97794];
  const norm = a => { const l = Math.hypot(...a); return a.map(x => x / l); };
  const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
  const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  function render(shot, vanilla) {
    const eye = shot.eye.slice(); if (shot.stand) { const g = topY(Math.floor(eye[0]), Math.floor(eye[2])); if (g > 0) eye[1] = g + 1 + 1.62; }
    const F = norm([shot.at[0] - eye[0], shot.at[1] - eye[1], shot.at[2] - eye[2]]);
    const R = norm(cross(F, [0, 1, 0])), Uv = cross(R, F);
    const ty = Math.tan((shot.fov || 70) * Math.PI / 360), tx = ty * W / H, nr = 0.1, fr = 1500;
    // VP (column-major)
    const view = [R[0], Uv[0], -F[0], 0, R[1], Uv[1], -F[1], 0, R[2], Uv[2], -F[2], 0, -dot(R, eye), -dot(Uv, eye), dot(F, eye), 1];
    const proj = [1 / tx, 0, 0, 0, 0, 1 / ty, 0, 0, 0, 0, -(fr + nr) / (fr - nr), -1, 0, 0, -2 * fr * nr / (fr - nr), 0];
    const VP = new Float32Array(16);
    for (let cI = 0; cI < 4; cI++) for (let r = 0; r < 4; r++) { let s = 0; for (let k = 0; k < 4; k++) s += proj[k * 4 + r] * view[cI * 4 + k]; VP[cI * 4 + r] = s; }
    gl.bindFramebuffer(gl.FRAMEBUFFER, fbo); gl.viewport(0, 0, W, H);
    gl.clearColor(0, 0, 0, 0); gl.clearDepth(1); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.disable(gl.DEPTH_TEST); gl.disable(gl.CULL_FACE);
    const bindCommon = p => { gl.useProgram(p); gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, noise); gl.uniform1i(U(p, 'NOISE'), 0); gl.activeTexture(gl.TEXTURE3); gl.bindTexture(gl.TEXTURE_2D, puffT); gl.uniform1i(U(p, 'PUFF'), 3);
      gl.uniform1f(U(p, 'TIME'), shot.t !== undefined ? shot.t : T); gl.uniform3fv(U(p, 'CAM'), eye); };
    if (!vanilla) {
      bindCommon(pBg);
      gl.uniform3fv(U(pBg, 'F'), F); gl.uniform3fv(U(pBg, 'R'), R); gl.uniform3fv(U(pBg, 'U'), Uv); gl.uniform1f(U(pBg, 'TX'), tx); gl.uniform1f(U(pBg, 'TY'), ty);
      gl.bindBuffer(gl.ARRAY_BUFFER, qb); const l = gl.getAttribLocation(pBg, 'P'); gl.enableVertexAttribArray(l); gl.vertexAttribPointer(l, 2, gl.FLOAT, false, 0, 0);
      gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4); gl.disableVertexAttribArray(l);
    } else { gl.clearColor(0.47, 0.65, 1.0, 0); gl.clear(gl.COLOR_BUFFER_BIT); }
    gl.enable(gl.DEPTH_TEST); gl.enable(gl.CULL_FACE); gl.cullFace(gl.BACK);
    bindCommon(pBlk); gl.uniformMatrix4fv(U(pBlk, 'VP'), false, VP); gl.uniform1f(U(pBlk, 'VANILLA'), vanilla ? 1 : 0);
    gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D_ARRAY, arr); gl.uniform1i(U(pBlk, 'TILES'), 1);
    gl.bindBuffer(gl.ARRAY_BUFFER, vb);
    const aP = gl.getAttribLocation(pBlk, 'POS'), aU = gl.getAttribLocation(pBlk, 'UV'), aD = gl.getAttribLocation(pBlk, 'DAT');
    gl.enableVertexAttribArray(aP); gl.vertexAttribPointer(aP, 3, gl.FLOAT, false, 32, 0);
    gl.enableVertexAttribArray(aU); gl.vertexAttribPointer(aU, 2, gl.FLOAT, false, 32, 12);
    gl.enableVertexAttribArray(aD); gl.vertexAttribPointer(aD, 3, gl.FLOAT, false, 32, 20);
    gl.drawArrays(gl.TRIANGLES, 0, nVerts);
    gl.disableVertexAttribArray(aP); gl.disableVertexAttribArray(aU); gl.disableVertexAttribArray(aD);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null); gl.disable(gl.DEPTH_TEST); gl.disable(gl.CULL_FACE);
    gl.bindTexture(gl.TEXTURE_2D, scene); gl.generateMipmap(gl.TEXTURE_2D);
    gl.useProgram(pPost);
    gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, scene); gl.uniform1i(U(pPost, 'SCENE'), 0);
    gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, depth); gl.uniform1i(U(pPost, 'DEPTH'), 1);
    const sf = dot(SUN, F); let sx = 0.5, sy = 0.5, sv = 0;
    if (sf > 0.05) { sx = 0.5 + 0.5 * dot(SUN, R) / (sf * tx); sy = 0.5 + 0.5 * dot(SUN, Uv) / (sf * ty); sv = Math.min(1, (sf - 0.05) / 0.35); }
    gl.uniform3f(U(pPost, 'SUNSCR'), sx, sy, sv); gl.uniform2f(U(pPost, 'RES'), W, H); gl.uniform1f(U(pPost, 'VANILLA'), vanilla ? 1 : 0);
    gl.bindBuffer(gl.ARRAY_BUFFER, qb); const l2 = gl.getAttribLocation(pPost, 'P'); gl.enableVertexAttribArray(l2); gl.vertexAttribPointer(l2, 2, gl.FLOAT, false, 0, 0);
    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4); gl.finish();
    return c.toDataURL('image/png');
  }
  const out = {};
  for (const s of shots) { out[s.name] = render(s, !!s.vanilla); }
  if (orbitN > 0) for (let i = 0; i < orbitN; i++) {
    const u = i / orbitN, a0 = -0.55 + 1.5 * u;
    const eye = [0.5, 66.62, -11 - u * 15];
    const yaw = -0.9 + 1.8 * (0.5 - 0.5 * Math.cos(u * 6.283 * 0.5)) - 0.25, pitch = -0.10 + 0.10 * Math.sin(u * 6.283);
    const at = [eye[0] + Math.sin(yaw) * Math.cos(pitch), eye[1] + Math.sin(pitch), eye[2] - Math.cos(yaw) * Math.cos(pitch)];
    out['orbit_' + String(i).padStart(4, '0')] = render({ eye, at, fov: 80, t: T + i / 30 }, false);
  }
  return { out, nVerts };
}

const SHOTS = {
  hero:    { eye: [0.5, 66.62, -12.5], at: [-1.0, 68.0, -60], fov: 80 },                 // first steps onto the bridge, middle island ahead
  sunpeek: { eye: [-13.0, 66.62, -6.0], at: [3, 69, -100], fov: 80 },                   // toward the sun past the middle island
  sun:     { eye: [8.5, 0, -6.5], at: [26, 71.5, -100], fov: 80, stand: true },         // the sun itself in clear sky
  side:    { eye: [9.0, 0, -5.0], at: [58, 64, -16], fov: 75, stand: true },            // 90 degrees off the sun
  back:    { eye: [-1.0, 0, 9.0], at: [-8, 66, 52], fov: 80, stand: true },             // sun behind you: front-lit island
  down:    { eye: [9.0, 0, -6.0], at: [30, 40, -60], fov: 80, stand: true },            // over the edge at the cloud sea
  close:   { eye: [-1.5, 0, 6.5], at: [3.5, 65.8, 1.5], fov: 70, stand: true },         // blocks up close (bed defence)
  wide:    { eye: [40, 82, 46], at: [-6, 60, -50], fov: 70 },                           // spectator view of the whole map
  hero_vanilla: { eye: [0.5, 66.62, -12.5], at: [-1.0, 68.0, -60], fov: 80, vanilla: true },
};

(async () => {
  const browser = await chromium.launch({ args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
  const page = await browser.newPage();
  page.on('console', m => console.log('[page]', m.text()));
  // decode tiles to raw RGBA in the page
  const tilePx = await page.evaluate(async tiles => { const o = {};
    for (const [k, b64] of Object.entries(tiles)) { const im = new Image(); im.src = 'data:image/png;base64,' + b64; await im.decode();
      const c = document.createElement('canvas'); c.width = 16; c.height = 16; const x = c.getContext('2d'); x.drawImage(im, 0, 0); o[k] = Array.from(x.getImageData(0, 0, 16, 16).data); }
    return o; }, tiles);
  const names = which === 'all' ? Object.keys(SHOTS) : which.split(',').filter(n => SHOTS[n]);
  const shots = names.map(n => ({ name: n, ...SHOTS[n] }));
  const t0 = Date.now();
  const res = await page.evaluate(pageMain, { W: +W, H: +H, tiles, tilePx, src: { bgVS, bgFS, blkVS, blkFS, postVS, postFS }, shots, T: +tArg, orbitN: +orbitN });
  if (res.err) throw new Error(res.err);
  fs.mkdirSync(outDir, { recursive: true });
  for (const [k, url] of Object.entries(res.out)) fs.writeFileSync(path.join(outDir, k + '.png'), Buffer.from(url.split(',')[1], 'base64'));
  console.log('verts', res.nVerts, 'rendered', Object.keys(res.out).length, 'in', ((Date.now() - t0) / 1000).toFixed(1) + 's');
  await browser.close();
})().catch(e => { console.error(String(e.message || e).slice(0, 3000)); process.exit(1); });
