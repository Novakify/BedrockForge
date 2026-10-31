// Renders LyricsFlow's original live_sky.fsh (resources/lyricsflow/shaders) in headless Chromium/WebGL2, fed with
// the parameter rows from sky_params (Sky::pack). Pixel-true previews + panel thumbnails without the game.
//   node tools/LyricsSky/render_sky.js <params.json> <out_dir> [w h yaw|auto pitch|auto vfov]  (auto = face the sun/moon) [preset,preset|all] [t]
const fs = require('fs'), path = require('path');
const { chromium } = require('playwright');

const [paramsFile, outDir, W = '640', H = '360', yaw = '0', pitch = '-20', vfov = '70', which = 'all', tArg] =
    process.argv.slice(2);
const repo = path.resolve(__dirname, '../..');
// SKY_FSH=<file> renders another fragment shader with the same inputs (gen_sky_shaders.py's check build).
let fsh = fs.readFileSync(process.env.SKY_FSH || path.join(repo, 'resources/lyricsflow/shaders/live_sky.fsh'), 'utf8');
fsh = fsh.replace(/#version 330/, '#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;');
const vsh = `#version 300 es
in vec2 P;
uniform vec3 F, R, U, INFO;
uniform float TX, TY;
out vec3 skyDir;
out vec3 skyInfo;
void main() {
  gl_Position = vec4(P, 0.0, 1.0);
  skyDir = F + R * (P.x * TX) + U * (P.y * TY);
  skyInfo = INFO;
}`;
const params = JSON.parse(fs.readFileSync(paramsFile, 'utf8'));
const ids = which === 'all' ? Object.keys(params) : which.split(',');

(async () => {
  const browser = await chromium.launch({ args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
  const page = await browser.newPage();
  page.on('console', m => console.log('[page]', m.text()));
  fs.mkdirSync(outDir, { recursive: true });
  for (const id of ids) {
    const url = await page.evaluate(({ vsh, fsh, px, W, H, yaw, pitch, vfov, t }) => {
      const c = document.createElement('canvas'); c.width = W; c.height = H;
      const gl = c.getContext('webgl2', { premultipliedAlpha: false, preserveDrawingBuffer: true });
      if (!gl) return 'ERR no webgl2';
      const sh = (type, src) => { const s = gl.createShader(type); gl.shaderSource(s, src); gl.compileShader(s);
        if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s)); return s; };
      const prog = gl.createProgram();
      try { gl.attachShader(prog, sh(gl.VERTEX_SHADER, vsh)); gl.attachShader(prog, sh(gl.FRAGMENT_SHADER, fsh)); }
      catch (e) { return 'ERR ' + e.message; }
      gl.bindAttribLocation(prog, 0, 'P'); gl.linkProgram(prog);
      if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) return 'ERR link ' + gl.getProgramInfoLog(prog);
      gl.useProgram(prog);
      const b = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, b);
      gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]), gl.STATIC_DRAW);
      gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
      const tex = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D, tex);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, 16, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array(px));
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      gl.uniform1i(gl.getUniformLocation(prog, 'Sampler0'), 0);
      const yr = yaw * Math.PI / 180, pr = pitch * Math.PI / 180; // Minecraft: pitch > 0 looks down
      const f = [-Math.sin(yr) * Math.cos(pr), -Math.sin(pr), Math.cos(yr) * Math.cos(pr)];
      const r = [-Math.cos(yr), 0, -Math.sin(yr)];
      const u = [r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]];
      const ty = Math.tan(vfov * Math.PI / 360), tx = ty * W / H;
      gl.uniform3fv(gl.getUniformLocation(prog, 'F'), f);
      gl.uniform3fv(gl.getUniformLocation(prog, 'R'), r); // Minecraft: facing +Z the right hand is -X
      gl.uniform3fv(gl.getUniformLocation(prog, 'U'), u);
      gl.uniform1f(gl.getUniformLocation(prog, 'TX'), tx);
      gl.uniform1f(gl.getUniformLocation(prog, 'TY'), ty);
      gl.uniform3fv(gl.getUniformLocation(prog, 'INFO'), [t, 1.0, 1.0]);
      gl.viewport(0, 0, W, H); gl.clearColor(0.03, 0.03, 0.04, 1); gl.clear(gl.COLOR_BUFFER_BIT);
      gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
      gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
      return c.toDataURL('image/png');
    }, { vsh, fsh, px: params[id].p, W: +W, H: +H, yaw: yaw === 'auto' ? (params[id].az >= 0 ? params[id].az : 0) : +yaw,
        pitch: pitch === 'auto' ? -Math.max(10, Math.min(35, params[id].el + 6)) : +pitch, vfov: +vfov, t: tArg ? +tArg : 2200 });
    if (url.startsWith('ERR')) { console.error(id, url); process.exitCode = 1; break; }
    fs.writeFileSync(path.join(outDir, id + '.png'), Buffer.from(url.split(',')[1], 'base64'));
    console.log('ok', id);
  }
  await browser.close();
})();
