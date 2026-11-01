// FiguraBridge: render frames of the headless simulator (tests/figura/Sim.cpp) on the compiled Bedrock geometry.
// usage: THREE=/path/three.min.js node render_sim.js <compiled_dir> <sim.json> <out.png> [frameIndices comma] [cols]
// Applies each part bone's per-frame values exactly as the runtime meant them (decomposeForBedrock inverted, in
// Blockbench space): M = T(pivot + pos) * Rz Ry Rx * S * T(-pivot), Bedrock values converted back (x mirrored,
// rot (-x, -y, z)). Colours: primary 'd' texel + additive emissive 'e' texel (Stick Figures uses 1x1 textures).
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');
const [dir, simPath, out, pick, colsArg] = process.argv.slice(2);
const rig = JSON.parse(fs.readFileSync(path.join(dir, 'rig.json')));
const geoFile = fs.readdirSync(path.join(dir, 'rp/models/entity'))[0];
const geos = JSON.parse(fs.readFileSync(path.join(dir, 'rp/models/entity', geoFile)))['minecraft:geometry'];
const geo = geos[0];   // big avatars are split into chunks (same bone names): merge them back into one bone list
{ const seen = new Set(geo.bones.map(b => b.name)); for (const g of geos.slice(1)) for (const b of g.bones) if (!seen.has(b.name)) { seen.add(b.name); geo.bones.push(b); } }
const sim = JSON.parse(fs.readFileSync(simPath));
const frames = pick ? pick.split(',').map(Number).map(i => sim.frames[Math.min(i, sim.frames.length - 1)]) : sim.frames.slice(0, 8);
// texel colours
const { execSync } = require('child_process');
const texColors = rig.textureFiles.map(t => {
  try {
    const px = execSync(`python3 -c "from PIL import Image;im=Image.open('${path.join(dir, t.file)}').convert('RGBA');w,h=im.size;print(*im.getpixel((w//2,h//2)))"`).toString().trim().split(' ').map(Number);
    return px;
  } catch (e) { return [255, 0, 255, 255]; }
});
const job = { geo, rig: { parts: rig.parts, textureSets: rig.textureSets }, texColors, frames, cols: Number(colsArg || 4) };
const three = fs.readFileSync(process.env.THREE, 'utf8');
const pageJs = `
const T = THREE, D = Math.PI / 180;
function boneMats(geo, fr, rig) {
  const byName = {}; geo.bones.forEach(b => byName[b.name] = b);
  const world = {};
  function get(name) {
    if (world[name]) return world[name];
    const b = byName[name];
    const piv = b.pivot || [0, 0, 0];
    const p = new T.Vector3(-piv[0], piv[1], piv[2]);
    let rot = [0, 0, 0], pos = [0, 0, 0], scl = [1, 1, 1];
    const m = /^p(\\d+)$/.exec(name);
    if (m && fr.bones[m[1]]) { const v = fr.bones[m[1]]; rot = [-v[0], -v[1], v[2]]; pos = [-v[3], v[4], v[5]]; scl = [v[6], v[7], v[8]]; }
    const R = new T.Matrix4().makeRotationFromEuler(new T.Euler(rot[0] * D, rot[1] * D, rot[2] * D, 'ZYX'));
    const L = new T.Matrix4().makeTranslation(p.x + pos[0], p.y + pos[1], p.z + pos[2])
      .multiply(R).multiply(new T.Matrix4().makeScale(scl[0], scl[1], scl[2])).multiply(new T.Matrix4().makeTranslation(-p.x, -p.y, -p.z));
    const W = b.parent ? get(b.parent).clone().multiply(L) : L;
    world[name] = W; return W;
  }
  geo.bones.forEach(b => get(b.name));
  return world;
}
function partOfBone(name) { const m = /^p(\\d+)/.exec(name); return m ? Number(m[1]) : -1; }
function visibleOf(fr, part) { const v = fr.bones[part]; return v ? v[9] : 1; }
function colorOf(job, fr, part, set) {
  const v = fr.bones[part];
  const ts = job.rig.textureSets[set] || {};
  let d = v && v[10] >= 0 ? v[10] : ts.d, e = v && v[11] >= 0 ? v[11] : ts.e;
  const c = [0, 0, 0];
  if (d != null && d >= 0) { const t = job.texColors[d]; c[0] += t[0]; c[1] += t[1]; c[2] += t[2]; }
  if (e != null && e >= 0) { const t = job.texColors[e]; c[0] += t[0]; c[1] += t[1]; c[2] += t[2]; }
  return new T.Color(Math.min(255, c[0]) / 255, Math.min(255, c[1]) / 255, Math.min(255, c[2]) / 255);
}
window.run = async (job) => {
  const W = 320; const outImgs = [];
  const r = new T.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true }); r.setSize(W, W);
  for (const fr of job.frames) {
    const s = new T.Scene(); s.background = new T.Color(0x9fb8c8);
    s.add(new T.AmbientLight(0xffffff, 1.0)); const dl = new T.DirectionalLight(0xffffff, 0.6); dl.position.set(2, 3, 4); s.add(dl);
    const mats = boneMats(job.geo, fr, job.rig);
    const partSet = {}; job.rig.parts.forEach(p => partSet[p.i] = p.set);
    for (const b of job.geo.bones) {
      const pm = b.poly_mesh; const cubes = b.cubes || [];
      if (!pm && !cubes.length) continue;
      const part = partOfBone(b.name); if (part < 0) continue;
      // effective visibility: the runtime already ANDs ancestors
      if (!visibleOf(fr, part)) continue;
      const sm = /s(\\d+)$/.exec(b.name); const set = sm ? Number(sm[1]) : (partSet[part] ?? 0);
      const mat = new T.MeshLambertMaterial({ color: colorOf(job, fr, part, set), side: T.DoubleSide });
      const M = mats[b.name];
      if (pm) {
        const pos = [];
        for (const poly of pm.polys) {
          const vs = poly.map(c => pm.positions[c[0]]).map(q => new T.Vector3(-q[0], q[1], q[2]).applyMatrix4(M));
          const tri = (a, b2, c) => pos.push(a.x, a.y, a.z, b2.x, b2.y, b2.z, c.x, c.y, c.z);
          tri(vs[0], vs[1], vs[2]); if (vs.length > 3) tri(vs[0], vs[2], vs[3]);
        }
        const g = new T.BufferGeometry(); g.setAttribute('position', new T.Float32BufferAttribute(pos, 3)); g.computeVertexNormals();
        s.add(new T.Mesh(g, mat));
      }
      for (const c of cubes) {
        const inf = c.inflate || 0;
        const g = new T.BoxGeometry(c.size[0] + 2 * inf, c.size[1] + 2 * inf, c.size[2] + 2 * inf);
        const o = [-(c.origin[0] + c.size[0]) + c.size[0] / 2, c.origin[1] + c.size[1] / 2, c.origin[2] + c.size[2] / 2];
        g.translate(o[0], o[1], o[2]); g.applyMatrix4(M); s.add(new T.Mesh(g, mat));
      }
    }
    const cam = new T.PerspectiveCamera(35, 1, 0.1, 1000); cam.position.set(30, 30, 70); cam.lookAt(0, 16, 0);
    r.render(s, cam); outImgs.push({ label: fr.label, url: r.domElement.toDataURL('image/png') });
  }
  return outImgs;
};`;
(async () => {
  const b = await chromium.launch({ args: ['--use-gl=swiftshader', '--enable-unsafe-swiftshader'] });
  const p = await b.newPage();
  await p.setContent(`<script>${three}</script><script>${pageJs}</script>`);
  const res = await p.evaluate(j => window.run(j), job);
  const cols = job.cols, rows = Math.ceil(res.length / cols);
  await p.setViewportSize({ width: cols * 320, height: rows * 340 + 10 });
  await p.setContent(`<body style="margin:0;background:#111;font:12px monospace;color:#ddd"><div style="display:grid;grid-template-columns:repeat(${cols},320px)">` +
    res.map(x => `<div><img src="${x.url}" width=320><div style="padding:2px">${x.label}</div></div>`).join('') + `</div></body>`);
  await p.screenshot({ path: out, fullPage: true });
  await b.close();
  console.log(`rendered ${res.length} frames -> ${out}`);
})();
