# World FX lab: runs RF's real worldfx_pass.hlsl offline (dxc -> SPIR-V -> wgpu on lavapipe) over a synthetic scene
# (colour + D24 depth made by a little numpy ray tracer), so effects can be judged in seconds instead of a CI build.
# usage: from lab import Lab; L = Lab(w, h); img = L.render(cam, match)   (see shots.py)
import os, subprocess, struct, math
os.environ.setdefault("VK_ICD_FILENAMES", "/opt/lavapipe/lvp_icd.json")
import numpy as np, wgpu
from PIL import Image

RF = "/home/claude/rf3"
HERE = os.path.dirname(os.path.abspath(__file__))

def compile_shaders():
    for prof, ent, out in (("ps_6_0", "PSWorldFx", "ps.spv"), ("vs_6_0", "VSWorldFx", "vs.spv")):
        r = subprocess.run(["/opt/dxc/bin/dxc", "-spirv", "-T", prof, "-E", ent, "-O3", "-fvk-b-shift", "0", "0", "-fvk-t-shift", "10", "0",
                            "-fvk-s-shift", "20", "0", "-fspv-target-env=vulkan1.1", "-Fo", f"{HERE}/{out}", f"{RF}/shaders/worldfx_pass.hlsl"],
                           capture_output=True, text=True)
        if r.returncode: raise SystemExit(r.stderr[-3000:])

def lcg_bytes(n, seed=1234567):
    out = np.empty(n, np.uint8); s = seed
    a = np.uint64(1664525); c = np.uint64(1013904223)
    # vectorised LCG via closed form is fiddly: do it in chunks with python ints (n = 262144, fine)
    for i in range(n):
        s = (s * 1664525 + 1013904223) & 0xFFFFFFFF
        out[i] = s >> 24
    return out

def make_puff(PN=1024):
    def domes(N, rMin, rMax, sd):
        s = sd; pts = np.zeros((N, N, 4))
        for j in range(N):
            for i in range(N):
                v = []
                for _ in range(4):
                    s = (s * 1664525 + 1013904223) & 0xFFFFFFFF; v.append(s / 4294967296.0)
                pts[j, i] = ((i + 0.15 + 0.7 * v[0]) / N, (j + 0.15 + 0.7 * v[1]) / N, (rMin + (rMax - rMin) * v[2]) / N, 0.55 + 0.45 * v[3])
        u = (np.arange(PN) + 0.5) / PN
        U, V = np.meshgrid(u, u)
        ci = np.floor(U * N).astype(int); cj = np.floor(V * N).astype(int)
        m = np.zeros((PN, PN))
        for dj in (-1, 0, 1):
            for di in (-1, 0, 1):
                p = pts[(cj + dj) % N, (ci + di) % N]
                dx = np.abs(U - p[..., 0]); dy = np.abs(V - p[..., 1])
                dx = np.where(dx > 0.5, 1 - dx, dx); dy = np.where(dy > 0.5, 1 - dy, dy)
                q = (dx * dx + dy * dy) / (p[..., 2] ** 2)
                m = np.maximum(m, np.where(q < 1, np.power(np.clip(1 - q, 0, 1), 0.8) * p[..., 3], 0))
        return m
    d1, d2, d3, d4 = domes(6, .62, .98, 11), domes(15, .60, .95, 23), domes(37, .55, .95, 37), domes(83, .5, .95, 51)
    return (d1 * 0.60 + d2 * 0.26 * (0.35 + 0.65 * d1) + d3 * 0.10 * (0.3 + 0.7 * d2) + d4 * 0.04).astype(np.float32)

class Cam:
    def __init__(self, pos, yaw=0.0, pitch=0.0, fov=110.0):   # yaw: 0 looks +z, pitch > 0 looks down (Minecraft)
        self.pos = np.array(pos, float); self.yaw = yaw; self.pitch = pitch; self.fov = fov
    @staticmethod
    def look(pos, at, fov=62.0):
        d = np.array(at, float) - np.array(pos, float); h = math.hypot(d[0], d[2])
        return Cam(pos, yaw=math.degrees(math.atan2(-d[0], d[2])), pitch=math.degrees(math.atan2(-d[1], h)), fov=fov)
    def axes(self):
        y, p = math.radians(self.yaw), math.radians(self.pitch)
        f = np.array([-math.sin(y) * math.cos(p), -math.sin(p), math.cos(y) * math.cos(p)])
        r = np.cross(f, [0, 1, 0]); r /= np.linalg.norm(r)      # (left-handed like the game: right = f x up)
        u = np.cross(r, f)
        return f, r, u

class Scene:
    """blocks: list of (x0,y0,z0,x1,y1,z1,(r,g,b)) boxes in world space; lit flat like Bedrock (face shade)"""
    def __init__(self, boxes): self.boxes = boxes
    def trace(self, cam, W, H):
        f, r, u = cam.axes()
        ty = math.tan(math.radians(cam.fov) / 2); tx = ty * W / H     # fov is vertical here
        xs = (np.arange(W) + 0.5) / W * 2 - 1; ys = 1 - (np.arange(H) + 0.5) / H * 2
        X, Y = np.meshgrid(xs, ys)
        d = f[None, None] + r[None, None] * (X * tx)[..., None] + u[None, None] * (Y * ty)[..., None]   # z along forward = 1
        best = np.full((H, W), 1e30); col = np.zeros((H, W, 3))
        o = cam.pos
        for (x0, y0, z0, x1, y1, z1, c) in self.boxes:
            lo = np.array([x0, y0, z0]); hi = np.array([x1, y1, z1])
            with np.errstate(divide='ignore', invalid='ignore'):
                inv = 1.0 / np.where(np.abs(d) > 1e-9, d, 1e-9)
            t0 = (lo - o) * inv; t1 = (hi - o) * inv
            tmin = np.minimum(t0, t1); tmax = np.maximum(t0, t1)
            tn = tmin.max(-1); tf = tmax.min(-1)
            hit = (tn < tf) & (tn > 0.02) & (tn < best)
            ax = tmin.argmax(-1)
            P = o + d * tn[..., None]
            shade = np.choose(ax, [0.6, 1.0, 0.8]); shade = np.where((ax == 1) & (d[..., 1] > 0), 0.5, shade)
            # a 16-texel pixel pattern so blocks read as blocks
            uv = np.where(ax[..., None] == 0, P[..., [2, 1]], np.where(ax[..., None] == 1, P[..., [0, 2]], P[..., [0, 1]]))
            tex = np.floor(uv * 16).astype(np.int64)
            n = ((tex[..., 0] * 73856093) ^ (tex[..., 1] * 19349663)) & 1023
            g = 0.86 + 0.24 * (n / 1023.0)
            blk = np.floor(uv).astype(np.int64); edge = (np.abs(uv - np.round(uv)) < 0.03).any(-1)
            cc = np.array(c)[None, None] * (shade * g * np.where(edge, 0.9, 1.0))[..., None]
            best = np.where(hit, tn, best); col = np.where(hit[..., None], cc, col)
        sky = best > 1e29
        depth = np.where(sky, 1.0, 1.0 - 0.025 / np.maximum(best, 0.026))
        d24 = np.minimum((depth * 16777215.0).round(), 16777215).astype(np.uint32)
        d24 = np.where(sky, 16777215, d24).astype(np.uint32)
        skyc = np.array([0.45, 0.6, 0.9])
        col = np.where(sky[..., None], skyc, col)
        rgba = (np.clip(col, 0, 1) * 255).astype(np.uint32)
        scene = rgba[..., 0] | (rgba[..., 1] << 8) | (rgba[..., 2] << 16) | (255 << 24)
        return scene.astype(np.uint32), d24, (tx, ty)

class Match:
    """the M cbuffer (341 float4), same order as worldfx_pass.hlsl"""
    N = 341
    def __init__(self):
        self.a = np.zeros((self.N, 4), np.float32)
        o = 0
        def take(n):
            nonlocal o; s = slice(o, o + n); o += n; return s
        self.m0 = take(1); self.tPos = take(16); self.tCol = take(16); self.eA = take(8); self.eB = take(8); self.eC = take(8)
        self.m1 = take(1); self.tAge = take(16); self.trMin = take(1); self.trMax = take(1); self.gA = take(48); self.gB = take(48)
        self.bVis = take(16); self.fallen = take(24); self.m2 = take(1); self.fCol = take(24); self.pInfo = take(8); self.pPts = take(96)
        assert o == self.N
        self.a[self.m0] = (0, 0, 1, 1); self.a[self.m1] = (0, 1, 0, 1)
        self.a[self.bVis, 3] = -100; self.a[self.tAge, 0] = -1
        self.ne = 0; self.nt = 0; self.ng = 0
    def team(self, pos, col, yaw=0.0, broke=-1.0, threat=0.0, elim=0.0, mine=1.0, vis=True):
        k = self.nt; self.nt += 1
        self.a[self.tPos.start + k] = (*pos, 1.0 if broke < 0 else 0.0); self.a[self.tCol.start + k] = (*col, 0)
        self.a[self.tAge.start + k] = (broke, threat, elim, mine)
        if vis: self.a[self.bVis.start + k] = (*pos, yaw)
        self.a[self.m0.start, 0] = self.nt
    def event(self, pos, kind, age, col=(0.3, 0.62, 1.0), seed=4242.0, floorDy=0.0, scene=0.0, who=0.0):
        k = self.ne; self.ne += 1
        self.a[self.eA.start + k] = (*pos, age); self.a[self.eB.start + k] = (*col, kind); self.a[self.eC.start + k] = (seed, floorDy, who, scene)
        self.a[self.m0.start, 1] = self.ne
        if kind >= 3: self.a[self.m1.start, 2] = 1
    def trail(self, pos, age, col=(0.8, 0.8, 0.8), link=0.0):
        k = self.ng; self.ng += 1
        self.a[self.gA.start + k] = (*pos, age); self.a[self.gB.start + k] = (*col, link)
        self.a[self.m1.start, 0] = self.ng
        lo = self.a[self.gA.start:self.gA.start + self.ng, :3].min(0); hi = self.a[self.gA.start:self.gA.start + self.ng, :3].max(0) + 1
        self.a[self.trMin.start, :3] = lo; self.a[self.trMax.start, :3] = hi

class Lab:
    def __init__(self, W=960, H=540, recompile=True):
        if recompile: compile_shaders()
        self.W, self.H = W, H
        ad = wgpu.gpu.request_adapter_sync(power_preference="high-performance")
        self.dev = dev = ad.request_device_sync(required_limits={"max_storage_buffer_binding_size": 64 << 20} if False else {})
        self.vs = dev.create_shader_module(code=open(f"{HERE}/vs.spv", "rb").read())
        self.ps = dev.create_shader_module(code=open(f"{HERE}/ps.spv", "rb").read())
        cache = f"{HERE}/tex.npz"
        if os.path.exists(cache): z = np.load(cache); noise, puff = z["noise"], z["puff"]
        else:
            noise = lcg_bytes(256 * 256 * 4).reshape(256, 256, 4); puff = make_puff(); np.savez(cache, noise=noise, puff=puff)
        self.tn = dev.create_texture(size=(256, 256, 1), format="rgba8unorm", usage=wgpu.TextureUsage.COPY_DST | wgpu.TextureUsage.TEXTURE_BINDING)
        dev.queue.write_texture({"texture": self.tn}, noise.tobytes(), {"bytes_per_row": 1024}, (256, 256, 1))
        levels = 11
        self.tp = dev.create_texture(size=(1024, 1024, 1), format="r16float", mip_level_count=levels,
                                     usage=wgpu.TextureUsage.COPY_DST | wgpu.TextureUsage.TEXTURE_BINDING)
        cur = puff; n = 1024
        for lv in range(levels):
            dev.queue.write_texture({"texture": self.tp, "mip_level": lv}, cur.astype(np.float16).tobytes(), {"bytes_per_row": n * 2}, (n, n, 1))
            if n > 1: cur = cur.reshape(n // 2, 2, n // 2, 2).mean((1, 3)); n //= 2
        # a test skin (64 x 1024, slot 0): our own drawing - hair, a face, a striped hoodie, jeans, sneakers
        sk = np.zeros((1024, 64, 4), np.uint8)
        def rect(u, v, w, h, c): sk[v:v + h, u:u + w] = (*c, 255)
        rng = np.random.RandomState(4)
        def box(u, v, w, h, d, c):
            for (uu, vv, ww, hh) in ((u + d, v, w, d), (u + d + w, v, w, d), (u, v + d, d, h), (u + d, v + d, w, h), (u + d + w, v + d, d, h), (u + 2 * d + w, v + d, w, h)): rect(uu, vv, ww, hh, c)
        skin, hair, hood, hood2, jean, shoe = (226, 172, 132), (58, 36, 24), (230, 120, 40), (250, 235, 215), (48, 70, 130), (236, 236, 240)
        box(0, 0, 8, 8, 8, skin); rect(8, 0, 8, 8, hair); rect(0, 8, 8, 3, hair); rect(16, 8, 8, 3, hair); rect(24, 8, 8, 8, hair); rect(8, 8, 8, 2, hair)
        rect(9, 12, 2, 1, (255, 255, 255)); rect(13, 12, 2, 1, (255, 255, 255)); rect(10, 12, 1, 1, (40, 60, 120)); rect(13, 12, 1, 1, (40, 60, 120)); rect(11, 14, 2, 1, (150, 90, 70))
        for (u, v, w) in ((16, 16, 8), (40, 16, 4), (32, 48, 4)):
            box(u, v, w, 12, 4, hood)
            for yy in (v + 4 + 3, v + 4 + 7): sk[yy, u:u + 2 * w + 8] = (*hood2, 255)
        rect(44, 29, 4, 3, skin); rect(36, 61, 4, 3, skin); rect(40, 29, 4, 3, skin); rect(48, 29, 8, 3, skin); rect(32, 61, 4, 3, skin); rect(40, 61, 8, 3, skin)
        for (u, v) in ((0, 16), (16, 48)):
            box(u, v, 4, 12, 4, jean); rect(u, v + 13, 16, 3, shoe)
        nz = rng.randint(-10, 10, (1024, 64, 1)); sk[..., :3] = np.clip(sk[..., :3].astype(int) + nz, 0, 255)
        self.ts = dev.create_texture(size=(64, 1024, 1), format="rgba8unorm", usage=wgpu.TextureUsage.COPY_DST | wgpu.TextureUsage.TEXTURE_BINDING)
        dev.queue.write_texture({"texture": self.ts}, sk.tobytes(), {"bytes_per_row": 256}, (64, 1024, 1))
        self.smp = dev.create_sampler(address_mode_u="repeat", address_mode_v="repeat", mag_filter="linear", min_filter="linear", mipmap_filter="linear")
        B = wgpu.BufferUsage
        self.cb = dev.create_buffer(size=160, usage=B.UNIFORM | B.COPY_DST)
        self.mb = dev.create_buffer(size=Match.N * 16, usage=B.UNIFORM | B.COPY_DST)
        self.sb = dev.create_buffer(size=W * H * 4, usage=B.STORAGE | B.COPY_DST)
        self.db = dev.create_buffer(size=W * H * 4, usage=B.STORAGE | B.COPY_DST)
        self.rt = dev.create_texture(size=(W, H, 1), format="rgba8unorm", usage=wgpu.TextureUsage.RENDER_ATTACHMENT | wgpu.TextureUsage.COPY_SRC)
        F = wgpu.ShaderStage.FRAGMENT
        bgl = dev.create_bind_group_layout(entries=[
            {"binding": 0, "visibility": F, "buffer": {"type": "uniform"}},
            {"binding": 1, "visibility": F, "buffer": {"type": "uniform"}},
            {"binding": 10, "visibility": F, "buffer": {"type": "read-only-storage"}},
            {"binding": 11, "visibility": F, "buffer": {"type": "read-only-storage"}},
            {"binding": 12, "visibility": F, "texture": {"sample_type": "float"}},
            {"binding": 13, "visibility": F, "texture": {"sample_type": "float"}},
            {"binding": 14, "visibility": F, "texture": {"sample_type": "float"}},
            {"binding": 20, "visibility": F, "sampler": {"type": "filtering"}}])
        self.bg = dev.create_bind_group(layout=bgl, entries=[
            {"binding": 0, "resource": {"buffer": self.cb}}, {"binding": 1, "resource": {"buffer": self.mb}},
            {"binding": 10, "resource": {"buffer": self.sb}}, {"binding": 11, "resource": {"buffer": self.db}},
            {"binding": 12, "resource": self.tn.create_view()}, {"binding": 13, "resource": self.tp.create_view()},
            {"binding": 14, "resource": self.ts.create_view()},
            {"binding": 20, "resource": self.smp}])
        self.pipe = dev.create_render_pipeline(layout=dev.create_pipeline_layout(bind_group_layouts=[bgl]),
            vertex={"module": self.vs, "entry_point": "VSWorldFx"},
            fragment={"module": self.ps, "entry_point": "PSWorldFx", "targets": [{"format": "rgba8unorm"}]},
            primitive={"topology": "triangle-list"})
    def render(self, cam, scene, match=None, time=10.0, phase=0.0, deck=None, quality=1.0, ink=0.0, anchored=1.0, skytime=None):
        W, H = self.W, self.H
        sc, d24, (tx, ty) = scene.trace(cam, W, H)
        f, r, u = cam.axes()
        c = np.zeros(40, np.float32)
        c[0:3] = f; c[3] = tx; c[4:7] = r; c[7] = ty; c[8:11] = u; c[11] = time
        c[12] = 0.025; c[15] = 0.0
        c[16] = W; c[17] = H; c[18] = W * 4; c[19] = W * 4
        c[20:23] = (0.17605, 0.11247, -0.97794); c[23] = 0.7
        c[24] = 0.55; c[25] = 1.0; c[26] = 0; c[27] = phase
        c[28:31] = cam.pos; c[31] = (cam.pos[1] - 56.0) if deck is None else deck
        c[32] = 0.0; c[33] = 0.0; c[34] = time if skytime is None else skytime; c[35] = anchored
        c[36] = 1.0; c[37] = 1.0; c[38] = quality; c[39] = ink
        q = self.dev.queue
        q.write_buffer(self.cb, 0, c.tobytes())
        q.write_buffer(self.mb, 0, (match or Match()).a.tobytes())
        q.write_buffer(self.sb, 0, sc.tobytes()); q.write_buffer(self.db, 0, d24.tobytes())
        enc = self.dev.create_command_encoder()
        rp = enc.begin_render_pass(color_attachments=[{"view": self.rt.create_view(), "load_op": "clear", "store_op": "store", "clear_value": (0, 0, 0, 1)}])
        rp.set_pipeline(self.pipe); rp.set_bind_group(0, self.bg); rp.draw(3, 1, 0, 0); rp.end()
        q.submit([enc.finish()])
        data = q.read_texture({"texture": self.rt}, {"bytes_per_row": W * 4}, (W, H, 1))
        return Image.fromarray(np.frombuffer(data, np.uint8).reshape(H, W, 4)[..., :3])

def flat_world(y=64, extra=()):
    g = [(-200, y - 1, -200, 200, y, 200, (0.42, 0.40, 0.44))]
    return Scene(g + list(extra))
def island_world():
    b = []
    def isl(cx, cy, cz, r, col=(0.78, 0.72, 0.6)):
        for k in range(4):
            rr = r - k * 1.6
            if rr > 0.5: b.append((cx - rr, cy - 1 - 2 * k, cz - rr, cx + rr, cy - 2 * k, cz + rr, tuple(np.array(col) * (1 - 0.12 * k))))
    isl(0, 64, 0, 9); isl(40, 64, 0, 7); isl(-40, 64, 0, 7); isl(0, 64, 40, 7); isl(0, 64, -40, 7); isl(28, 66, 28, 5, (0.6, 0.6, 0.62))
    b += [(-2, 64, 3, -1, 67, 4, (0.45, 0.3, 0.18)), (-3.5, 67, 1.5, 0.5, 69, 5.5, (0.25, 0.5, 0.2))]
    return Scene(b)

def sheet(imgs, cols, path, scale=1.0):
    w, h = imgs[0].size; rows = (len(imgs) + cols - 1) // cols
    S = Image.new("RGB", (w * cols, h * rows))
    for i, im in enumerate(imgs): S.paste(im, ((i % cols) * w, (i // cols) * h))
    if scale != 1.0: S = S.resize((int(S.width * scale), int(S.height * scale)), Image.LANCZOS)
    S.save(path, quality=92)
