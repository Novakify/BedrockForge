// GeckoBridge preview: render a Bedrock geometry posed by poses.json (written by preview.py), headless.
// usage: THREE=/path/three.min.js node preview.js poses.json out.png
// Conventions follow tools/ModPorter/render_preview.js (Blockbench's Bedrock mapping: x mirrored,
// rotations (-x, y, -z), Euler ZYX). Animation rotation adds to the bind rotation, position adds
// (x mirrored), scale multiplies - same as Bedrock's actor animation system.
const { chromium } = require('playwright');
const fs = require('fs');
const [posesPath, out] = process.argv.slice(2);
const job = JSON.parse(fs.readFileSync(posesPath));
const three = fs.readFileSync(process.env.THREE, 'utf8');
const png = f => 'data:image/png;base64,' + fs.readFileSync(f).toString('base64');
job.tex = png(job.texture); if (job.glow) job.glowTex = png(job.glow);

const page_js = `
const T = THREE, R = d => T.MathUtils.degToRad(d);
function faceUV(geo, fi, u, v, w, h, tw, th) {
  const uv = geo.attributes.uv, i = fi * 4, u0 = u / tw, u1 = (u + w) / tw, v0 = 1 - v / th, v1 = 1 - (v + h) / th;
  uv.setXY(i, u0, v0); uv.setXY(i + 1, u1, v0); uv.setXY(i + 2, u0, v1); uv.setXY(i + 3, u1, v1);
}
function cubeMesh(c, tw, th, mat) {
  const [w, h, d] = c.size, inf = c.inflate || 0;
  const g = new T.BoxGeometry(Math.max(w + 2 * inf, 0.001), Math.max(h + 2 * inf, 0.001), Math.max(d + 2 * inf, 0.001));
  if (Array.isArray(c.uv)) {
    const [u, v] = c.uv;
    faceUV(g, 1, u, v + d, d, h, tw, th); faceUV(g, 5, u + d, v + d, w, h, tw, th); faceUV(g, 0, u + d + w, v + d, d, h, tw, th);
    faceUV(g, 4, u + 2 * d + w, v + d, w, h, tw, th); faceUV(g, 2, u + d, v, w, d, tw, th); faceUV(g, 3, u + d + w, v + d, w, -d, tw, th);
  } else if (c.uv) {
    const map = { east: 1, north: 5, west: 0, south: 4, up: 2, down: 3 };
    for (const [k, fi] of Object.entries(map)) { const f = c.uv[k]; if (f) faceUV(g, fi, f.uv[0], f.uv[1], f.uv_size[0], f.uv_size[1], tw, th); }
  }
  const m = new T.Mesh(g, mat);
  m.position.set(-(c.origin[0] + w / 2), c.origin[1] + h / 2, c.origin[2] + d / 2);
  if (c.rotation) {
    const p = c.pivot || [0, 0, 0], piv = new T.Group();
    piv.position.set(-p[0], p[1], p[2]);
    piv.rotation.set(R(-c.rotation[0]), R(c.rotation[1]), R(-c.rotation[2]), 'ZYX');
    m.position.sub(piv.position); piv.add(m); return piv;
  }
  return m;
}
function build(g, mats, pose) {
  const tw = g.description.texture_width || 64, th = g.description.texture_height || 64;
  const root = new T.Group(), groups = {};
  for (const b of g.bones || []) {
    const grp = new T.Group(), p = b.pivot || [0, 0, 0]; groups[b.name] = grp; grp.userData.p = p;
    for (const mat of mats) for (const c of b.cubes || []) {
      const m = cubeMesh(c, tw, th, mat); m.position.x += p[0]; m.position.y -= p[1]; m.position.z -= p[2]; grp.add(m);
    }
  }
  for (const b of g.bones || []) {
    const grp = groups[b.name], p = b.pivot || [0, 0, 0], parent = b.parent && groups[b.parent];
    const pp = parent ? parent.userData.p : [0, 0, 0];
    const a = pose[b.name] || {}, rot = b.rotation || [0, 0, 0], ar = a.rot || [0, 0, 0], ap = a.pos || [0, 0, 0], as = a.scl || [1, 1, 1];
    grp.position.set(-(p[0] - pp[0]) - ap[0], p[1] - pp[1] + ap[1], p[2] - pp[2] + ap[2]);
    grp.rotation.set(R(-(rot[0] + ar[0])), R(rot[1] + ar[1]), R(-(rot[2] + ar[2])), 'ZYX');
    grp.scale.set(as[0], as[1], as[2]);
    (parent || root).add(grp);
  }
  return root;
}
async function texOf(src) {
  const img = new Image(); img.src = src; await img.decode();
  const t = new T.Texture(img); t.magFilter = T.NearestFilter; t.minFilter = T.NearestFilter; t.needsUpdate = true; t.colorSpace = T.SRGBColorSpace; return t;
}
window.renderAll = async (job, W) => {
  const r = new T.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true }); r.setSize(W, W);
  const mats = [new T.MeshLambertMaterial({ map: await texOf(job.tex), transparent: true, alphaTest: 0.1, side: T.DoubleSide })];
  if (job.glowTex) mats.push(new T.MeshBasicMaterial({ map: await texOf(job.glowTex), transparent: true, alphaTest: 0.1, side: T.DoubleSide,
                                                        polygonOffset: true, polygonOffsetFactor: -1, polygonOffsetUnits: -1 }));
  const out = [];
  for (const f of job.frames) {
    const s = new T.Scene(); s.background = new T.Color(job.dark ? 0x07090c : 0x20262c);
    s.add(new T.AmbientLight(0xffffff, job.dark ? 0.25 : 1.1)); const dl = new T.DirectionalLight(0xffffff, job.dark ? 0.3 : 1.4); dl.position.set(2, 3, 4); s.add(dl);
    const m = build(job.geo, mats, f.pose); s.add(m);
    const c = new T.Vector3(...(job.center || [0, 16, 0])), dist = job.dist || 70;
    const [ax, az] = job.view || [0.55, -1];
    const cam = new T.PerspectiveCamera(35, 1, 0.1, 1000); cam.position.set(c.x + dist * ax, c.y + dist * 0.15, c.z + dist * az); cam.lookAt(c);
    r.render(s, cam); out.push({ label: f.label, url: r.domElement.toDataURL('image/png') });
  }
  return out;
};`;

(async () => {
  const b = await chromium.launch({ args: ['--use-gl=swiftshader', '--enable-unsafe-swiftshader'] });
  const p = await b.newPage();
  await p.setContent(`<script>${three}</script><script>${page_js}</script>`);
  const W = Number(process.env.SIZE || 240), res = await p.evaluate(([j, w]) => window.renderAll(j, w), [job, W]);
  const cols = job.cols || res.length, rows = Math.ceil(res.length / cols);
  await p.setViewportSize({ width: cols * W, height: rows * (W + 20) + 24 });
  await p.setContent(`<body style="margin:0;background:#111;font:12px monospace;color:#ccc"><div style="padding:4px">${job.title || ''}</div>` +
    `<div style="display:grid;grid-template-columns:repeat(${cols},${W}px)">` +
    res.map(r => `<div><img src="${r.url}" width=${W} height=${W}><div style="padding:1px 4px">${r.label}</div></div>`).join('') + '</div></body>');
  await p.screenshot({ path: out, fullPage: true }); await b.close();
  console.log(`rendered ${res.length} frames -> ${out}`);
})();
