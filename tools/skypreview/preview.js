// Cloud Sea sky preview: renders shaders/cloud_sea_common.hlsli (translated to GLSL on the fly, so the game's source
// is the only source) in headless Chromium/WebGL2, for art review of the living match sky phases.
//   node preview.js <out.png> [w h] [phases comma list] [views comma list: sun,moon,up,down]
const fs = require('fs'), path = require('path');
const { chromium } = require('playwright');
const [out = '/tmp/sky.png', W = '640', H = '360', phArg = '0,0.5,1,1.5,2', viewArg = 'sun,moon,down'] = process.argv.slice(2);
const hlsl = fs.readFileSync(path.join(__dirname, '../../shaders/cloud_sea_common.hlsli'), 'utf8');

function toGlsl(s) {
    s = s.replace(/^Texture2D.*$/gm, '').replace(/^SamplerState.*$/gm, '');
    s = s.replace(/\[(loop|unroll|branch)\]\s*/g, '');
    s = s.replace(/static const /g, 'const ').replace(/static /g, '');
    s = s.replace(/float3x3/g, 'mat3').replace(/float4/g, 'vec4').replace(/float3/g, 'vec3').replace(/float2/g, 'vec2');
    s = s.replace(/\blerp\(/g, 'mix(').replace(/\bfrac\(/g, 'fract(').replace(/\bsaturate\(/g, 'sat(').replace(/\bpow\(/g, 'powx(');
    s = s.replace(/(\w+)\.SampleLevel\(gSmp,\s*/g, 'textureLod($1, ');
    return s;
}
const HEAD = `#version 300 es
precision highp float; precision highp int;
uniform sampler2D gNoise; uniform sampler2D gPuff;
float sat(float x){return clamp(x,0.0,1.0);} vec2 sat(vec2 x){return clamp(x,0.0,1.0);} vec3 sat(vec3 x){return clamp(x,0.0,1.0);}
float powx(float a,float b){return pow(a,b);} vec3 powx(vec3 a,float b){return pow(a,vec3(b));} vec3 powx(vec3 a,vec3 b){return pow(a,b);}
vec2 powx(vec2 a,float b){return pow(a,vec2(b));}
`;
const FS = HEAD + toGlsl(hlsl) + `
uniform vec3 F, R, U; uniform float TX, TY, PH, T, SEAY; uniform vec3 CAMP;
in vec2 v; out vec4 o;
void main(){
  CAM = CAMP; TIME = T; SEA_Y = SEAY; MAP_C = vec2(CAMP.x, CAMP.z - 25.0); csSetPhase(PH);
  vec3 rd = normalize(F + R * (v.x * TX) + U * (v.y * TY));
  vec3 c = csBackground(rd, true);
  o = vec4(toDisplay(c), 1.0);
}`;
const VS = `#version 300 es
in vec2 P; out vec2 v; void main(){ v = P; gl_Position = vec4(P, 0.0, 1.0); }`;

(async () => {
    const browser = await chromium.launch({ args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
    const page = await browser.newPage();
    page.on('console', m => console.log('page:', m.text()));
    const w = +W, h = +H, phases = phArg.split(',').map(Number), views = viewArg.split(',');
    const png = await page.evaluate(async ({ FS, VS, w, h, phases, views }) => {
        const cv = document.createElement('canvas'); cv.width = w * phases.length; cv.height = h * views.length;
        const gl = cv.getContext('webgl2', { preserveDrawingBuffer: true });
        gl.getExtension('EXT_color_buffer_float'); gl.getExtension('OES_texture_float_linear');
        const sh = (t, s) => { const x = gl.createShader(t); gl.shaderSource(x, s); gl.compileShader(x);
            if (!gl.getShaderParameter(x, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(x)); return x; };
        const p = gl.createProgram(); gl.attachShader(p, sh(gl.VERTEX_SHADER, VS)); gl.attachShader(p, sh(gl.FRAGMENT_SHADER, FS)); gl.linkProgram(p);
        if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(p));
        gl.useProgram(p);
        let seed = 1234567; const rnd = () => { seed = (seed * 1664525 + 1013904223) >>> 0; return seed / 4294967296; };
        const nz = new Uint8Array(256 * 256 * 4); for (let i = 0; i < nz.length; i++) nz[i] = Math.floor(rnd() * 256);
        gl.activeTexture(gl.TEXTURE0); const t0 = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D, t0);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, 256, 256, 0, gl.RGBA, gl.UNSIGNED_BYTE, nz);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
        const PN = 512;
        function domes(N, rMin, rMax, sd) {
            const pts = []; let s2 = sd; const r2 = () => { s2 = (s2 * 1664525 + 1013904223) >>> 0; return s2 / 4294967296; };
            for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) pts.push([(i + 0.15 + 0.7 * r2()) / N, (j + 0.15 + 0.7 * r2()) / N, (rMin + (rMax - rMin) * r2()) / N, 0.55 + 0.45 * r2()]);
            const o = new Float32Array(PN * PN);
            for (let y = 0; y < PN; y++) for (let x = 0; x < PN; x++) {
                const u = (x + 0.5) / PN, v2 = (y + 0.5) / PN, ci = Math.floor(u * N), cj = Math.floor(v2 * N); let m = 0;
                for (let dj = -1; dj <= 1; dj++) for (let di = -1; di <= 1; di++) {
                    const q0 = pts[((cj + dj + N) % N) * N + ((ci + di + N) % N)];
                    let dx = Math.abs(u - q0[0]), dy = Math.abs(v2 - q0[1]); if (dx > 0.5) dx = 1 - dx; if (dy > 0.5) dy = 1 - dy;
                    const q = (dx * dx + dy * dy) / (q0[2] * q0[2]); if (q < 1) { const hg = Math.pow(1 - q, 0.8) * q0[3]; if (hg > m) m = hg; }
                }
                o[y * PN + x] = m;
            }
            return o;
        }
        const d1 = domes(6, 0.62, 0.98, 11), d2 = domes(15, 0.60, 0.95, 23), d3 = domes(37, 0.55, 0.95, 37), d4 = domes(83, 0.5, 0.95, 51);
        const puff = new Float32Array(PN * PN);
        for (let i = 0; i < PN * PN; i++) puff[i] = d1[i] * 0.60 + d2[i] * 0.26 * (0.35 + 0.65 * d1[i]) + d3[i] * 0.10 * (0.3 + 0.7 * d2[i]) + d4[i] * 0.04;
        gl.activeTexture(gl.TEXTURE1); const t1 = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D, t1);
        { let lvl = 0, n = PN, cur = puff;
          while (true) { gl.texImage2D(gl.TEXTURE_2D, lvl, gl.R32F, n, n, 0, gl.RED, gl.FLOAT, cur); if (n === 1) break;
            const m = n >> 1, nx = new Float32Array(m * m);
            for (let y = 0; y < m; y++) for (let x = 0; x < m; x++) nx[y * m + x] = (cur[2 * y * n + 2 * x] + cur[2 * y * n + 2 * x + 1] + cur[(2 * y + 1) * n + 2 * x] + cur[(2 * y + 1) * n + 2 * x + 1]) / 4;
            cur = nx; n = m; lvl++; } }
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
        gl.uniform1i(gl.getUniformLocation(p, 'gNoise'), 0); gl.uniform1i(gl.getUniformLocation(p, 'gPuff'), 1);
        const b = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, b); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
        const loc = gl.getAttribLocation(p, 'P'); gl.enableVertexAttribArray(loc); gl.vertexAttribPointer(loc, 2, gl.FLOAT, false, 0, 0);
        const U = n => gl.getUniformLocation(p, n);
        const norm = a => { const l = Math.hypot(...a); return a.map(x => x / l); };
        const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
        const VIEWS = { sun: [0.177, -0.06, -0.984], moon: [-0.857, 0.30, 0.516], down: [0.5, -0.45, -0.7], up: [0.2, 0.75, 0.6], side: [-0.98, 0.02, -0.2] };
        const tx = Math.tan(80 * Math.PI / 360) , ty = tx * h / w;
        views.forEach((vn, vi) => phases.forEach((ph, pi) => {
            const f = norm(VIEWS[vn]), r = norm(cross(f, [0, 1, 0])), u = cross(r, f);
            gl.viewport(pi * w, (views.length - 1 - vi) * h, w, h);
            gl.uniform3fv(U('F'), f); gl.uniform3fv(U('R'), r); gl.uniform3fv(U('U'), u);
            gl.uniform1f(U('TX'), tx); gl.uniform1f(U('TY'), ty); gl.uniform1f(U('PH'), ph); gl.uniform1f(U('T'), 40.0);
            gl.uniform1f(U('SEAY'), 12.0); gl.uniform3fv(U('CAMP'), [0, 70, 0]);
            gl.drawArrays(gl.TRIANGLES, 0, 3);
        }));
        gl.finish();
        return cv.toDataURL('image/png');
    }, { FS, VS, w, h, phases, views });
    fs.writeFileSync(out, Buffer.from(png.split(',')[1], 'base64'));
    await browser.close();
    console.log('wrote', out);
})().catch(e => { console.error(e.message.slice(0, 3000)); process.exit(1); });
