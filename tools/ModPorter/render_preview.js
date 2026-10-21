// Headless renderer for Bedrock .geo.json + texture (verification only).
// usage: node render.js <RP dir> <out.png>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const rp = process.argv[2], out = process.argv[3];
const three = fs.readFileSync(path.join(__dirname, 'node_modules/three/build/three.min.js'), 'utf8');

const jobs = [];
for (const f of fs.readdirSync(path.join(rp, 'entity'))) {
  const d = JSON.parse(fs.readFileSync(path.join(rp, 'entity', f)))['minecraft:client_entity'].description;
  jobs.push({ id: d.identifier.split(':')[1], geo: d.geometry.default, tex: d.textures.default });
}
const geos = {};
for (const f of fs.readdirSync(path.join(rp, 'models/entity')))
  for (const g of JSON.parse(fs.readFileSync(path.join(rp, 'models/entity', f)))['minecraft:geometry'] || []) geos[g.description.identifier] = g;
// one entity per distinct geometry+texture pair, so the sheet shows every look once
const seen = new Set(), list = [];
for (const j of jobs) { const k = j.geo + '|' + j.tex; if (seen.has(k) || !geos[j.geo]) continue; seen.add(k);
  const t = path.join(rp, j.tex + '.png'); if (!fs.existsSync(t)) continue;
  list.push({ ...j, g: geos[j.geo], png: 'data:image/png;base64,' + fs.readFileSync(t).toString('base64') }); }

const page_js = `
const T = THREE;
function faceUV(geo, faceIndex, u, v, w, h, tw, th) {
  const uv = geo.attributes.uv; const i = faceIndex * 4;
  const u0 = u / tw, u1 = (u + w) / tw, v0 = 1 - v / th, v1 = 1 - (v + h) / th;
  uv.setXY(i, u0, v0); uv.setXY(i + 1, u1, v0); uv.setXY(i + 2, u0, v1); uv.setXY(i + 3, u1, v1);
}
// three BoxGeometry face order: +x, -x, +y, -y, +z, -z
function cubeMesh(c, tw, th, mat) {
  const [w, h, d] = c.size, inf = c.inflate || 0;
  const g = new T.BoxGeometry(Math.max(w + 2 * inf, 0.001), Math.max(h + 2 * inf, 0.001), Math.max(d + 2 * inf, 0.001));
  if (Array.isArray(c.uv)) {
    const [u, v] = c.uv;
    faceUV(g, 1, u, v + d, d, h, tw, th);             // east  (-x in three, Bedrock x is mirrored)
    faceUV(g, 5, u + d, v + d, w, h, tw, th);         // north (-z)
    faceUV(g, 0, u + d + w, v + d, d, h, tw, th);     // west  (+x)
    faceUV(g, 4, u + 2 * d + w, v + d, w, h, tw, th); // south (+z)
    faceUV(g, 2, u + d, v, w, d, tw, th);             // up
    faceUV(g, 3, u + d + w, v + d, w, -d, tw, th);    // down
  } else if (c.uv) {
    const map = { east: 1, north: 5, west: 0, south: 4, up: 2, down: 3 };
    for (const [k, fi] of Object.entries(map)) { const f = c.uv[k]; if (f) faceUV(g, fi, f.uv[0], f.uv[1], f.uv_size[0], f.uv_size[1], tw, th); }
  }
  const m = new T.Mesh(g, mat);
  m.position.set(-(c.origin[0] + w / 2), c.origin[1] + h / 2, c.origin[2] + d / 2);
  if (c.rotation) {
    const p = c.pivot || [0, 0, 0], piv = new T.Group();
    piv.position.set(-p[0], p[1], p[2]);
    piv.rotation.set(T.MathUtils.degToRad(-c.rotation[0]), T.MathUtils.degToRad(c.rotation[1]), T.MathUtils.degToRad(-c.rotation[2]), 'ZYX');
    m.position.sub(piv.position); piv.add(m); return piv;
  }
  return m;
}
function build(g, tex) {
  const tw = g.description.texture_width || 64, th = g.description.texture_height || 64;
  const mat = new T.MeshLambertMaterial({ map: tex, transparent: true, alphaTest: 0.1, side: T.DoubleSide });
  const root = new T.Group(), groups = {};
  for (const b of g.bones || []) {
    const grp = new T.Group(), p = b.pivot || [0, 0, 0];
    grp.userData = { b, p }; groups[b.name] = grp;
    for (const c of b.cubes || []) { const m = cubeMesh(c, tw, th, mat); m.position.x += p[0]; m.position.y -= p[1]; m.position.z -= p[2]; grp.add(m); }
  }
  for (const b of g.bones || []) {
    const grp = groups[b.name], p = b.pivot || [0, 0, 0], parent = b.parent && groups[b.parent];
    const pp = parent ? (parent.userData.p) : [0, 0, 0];
    grp.position.set(-(p[0] - pp[0]), p[1] - pp[1], p[2] - pp[2]);
    if (b.rotation) grp.rotation.set(T.MathUtils.degToRad(-b.rotation[0]), T.MathUtils.degToRad(b.rotation[1]), T.MathUtils.degToRad(-b.rotation[2]), 'ZYX');
    (parent || root).add(grp);
  }
  return root;
}
window.renderAll = async (list, W) => {
  const r = new T.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true }); r.setSize(W, W);
  const out = [];
  for (const j of list) {
    const s = new T.Scene(); s.background = new T.Color(0x20262c);
    s.add(new T.AmbientLight(0xffffff, 1.1)); const dl = new T.DirectionalLight(0xffffff, 1.4); dl.position.set(2, 3, 4); s.add(dl);
    const img = new Image(); img.src = j.png; await img.decode();
    const tex = new T.Texture(img); tex.magFilter = T.NearestFilter; tex.minFilter = T.NearestFilter; tex.needsUpdate = true; tex.colorSpace = T.SRGBColorSpace;
    const m = build(j.g, tex); s.add(m);
    const box = new T.Box3().setFromObject(m), size = box.getSize(new T.Vector3()), c = box.getCenter(new T.Vector3());
    const dist = Math.max(size.x, size.y, size.z) * 1.6 + 4;
    for (const [ax, az, lbl] of (window.VIEWS || [[0.55, -1, '']])) {
      const cam = new T.PerspectiveCamera(35, 1, 0.1, 1000); cam.position.set(c.x + dist * ax, c.y + dist * 0.15, c.z + dist * az); cam.lookAt(c);
      r.render(s, cam); out.push({ id: j.id + lbl, url: r.domElement.toDataURL('image/png'), bones: (j.g.bones || []).length });
    }
  }
  return out;
};`;

(async () => {
  const b = await chromium.launch({ args: ['--use-gl=swiftshader', '--enable-unsafe-swiftshader'] });
  const p = await b.newPage();
  await p.setContent(`<script>${three}</script><script>${process.env.VIEWS ? 'window.VIEWS=' + process.env.VIEWS + ';' : ''}${page_js}</script>`);
  const W = Number(process.env.SIZE || 260), res = await p.evaluate(([l, w]) => window.renderAll(l, w), [list, W]);
  const cols = 5, rows = Math.ceil(res.length / cols);
  await p.setViewportSize({ width: cols * W, height: rows * (W + 22) });
  await p.setContent(`<body style="margin:0;background:#111;display:grid;grid-template-columns:repeat(${cols},${W}px);font:12px monospace;color:#ccc">` +
    res.map(r => `<div><img src="${r.url}" width=${W} height=${W}><div style="padding:2px 4px">${r.id} (${r.bones} bones)</div></div>`).join('') + '</body>');
  await p.screenshot({ path: out, fullPage: true }); await b.close();
  console.log(`rendered ${res.length} distinct models -> ${out}`);
})();
