# World FX atmos lab: runs RF's real worldfx_atmos.hlsl (PSWorldFxAtmos) offline on lavapipe over lab.py's box scenes.
# cbuffer A's layout is read from the shader itself, so fields are set by name:  A.set("wg0", r, g, b, size)
import os, re, subprocess, sys
import numpy as np, wgpu
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import HERE, RF, Scene, Cam, flat_world, island_world, sheet, compile_shaders

SRC = f"{RF}/shaders/worldfx_atmos.hlsl"

def compile_atmos():
    r = subprocess.run(["/opt/dxc/bin/dxc", "-spirv", "-T", "ps_6_0", "-E", "PSWorldFxAtmos", "-O3", "-fvk-b-shift", "0", "0", "-fvk-t-shift", "10", "0",
                        "-fvk-s-shift", "20", "0", "-fspv-target-env=vulkan1.1", "-Fo", f"{HERE}/atmos.spv", SRC], capture_output=True, text=True)
    if r.returncode: raise SystemExit(r.stderr[-3000:])

class ABuf:
    def __init__(self):
        src = open(SRC).read()
        body = src[src.index("cbuffer A"):]
        body = body[body.index("{") + 1:body.index("};")]
        self.off = {}; o = 0
        for m in re.finditer(r"float4\s+(\w+)(?:\[(\d+)\])?\s*;", body):
            n = int(m.group(2) or 1); self.off[m.group(1)] = (o, n); o += n
        self.a = np.zeros((o, 4), np.float32)
        self.set("pl", 0, 0, 0, -100.0); self.set("ch0", 0, 0, 0, -1.0); self.set("wxL", 0, 0, 0, -1.0)
    def set(self, name, *v, i=0):
        o, n = self.off[name]; self.a[o + i, :len(v)] = v

class AtmosLab:
    def __init__(self, W=640, H=360, recompile=True):
        if recompile:
            compile_atmos()
            if not os.path.exists(f"{HERE}/vs.spv"): compile_shaders()
        self.W, self.H = W, H
        ad = wgpu.gpu.request_adapter_sync(power_preference="high-performance")
        self.dev = dev = ad.request_device_sync()
        self.vs = dev.create_shader_module(code=open(f"{HERE}/vs.spv", "rb").read())
        self.ps = dev.create_shader_module(code=open(f"{HERE}/atmos.spv", "rb").read())
        B = wgpu.BufferUsage
        self.cb = dev.create_buffer(size=176, usage=B.UNIFORM | B.COPY_DST)
        self.ab = dev.create_buffer(size=ABuf().a.nbytes, usage=B.UNIFORM | B.COPY_DST)
        self.sb = dev.create_buffer(size=W * H * 4, usage=B.STORAGE | B.COPY_DST)
        self.db = dev.create_buffer(size=W * H * 4, usage=B.STORAGE | B.COPY_DST)
        self.rt = dev.create_texture(size=(W, H, 1), format="rgba8unorm", usage=wgpu.TextureUsage.RENDER_ATTACHMENT | wgpu.TextureUsage.COPY_SRC | wgpu.TextureUsage.COPY_DST)
        F = wgpu.ShaderStage.FRAGMENT
        bgl = dev.create_bind_group_layout(entries=[
            {"binding": 0, "visibility": F, "buffer": {"type": "uniform"}},
            {"binding": 1, "visibility": F, "buffer": {"type": "uniform"}},
            {"binding": 10, "visibility": F, "buffer": {"type": "read-only-storage"}},
            {"binding": 11, "visibility": F, "buffer": {"type": "read-only-storage"}}])
        self.bg = dev.create_bind_group(layout=bgl, entries=[
            {"binding": 0, "resource": {"buffer": self.cb}}, {"binding": 1, "resource": {"buffer": self.ab}},
            {"binding": 10, "resource": {"buffer": self.sb}}, {"binding": 11, "resource": {"buffer": self.db}}])
        self.pipe = dev.create_render_pipeline(layout=dev.create_pipeline_layout(bind_group_layouts=[bgl]),
            vertex={"module": self.vs, "entry_point": "VSWorldFx"},
            fragment={"module": self.ps, "entry_point": "PSWorldFxAtmos", "targets": [{"format": "rgba8unorm"}]},
            primitive={"topology": "triangle-list"})
    def render(self, cam, scene, A, time=10.0, tod=0.1, sky=None):
        W, H = self.W, self.H
        sc, d24, (tx, ty) = scene.trace(cam, W, H)
        if sky is not None:   # a sky gradient instead of the flat blue (top, horizon)
            ys = np.linspace(1, 0, H)[:, None, None]
            g = (np.array(sky[0]) * ys + np.array(sky[1]) * (1 - ys)) * np.ones((1, W, 1))
            rgba = (np.clip(g, 0, 1) * 255).astype(np.uint32)
            skyp = rgba[..., 0] | (rgba[..., 1] << 8) | (rgba[..., 2] << 16) | (255 << 24)
            sc = np.where(d24 >= 16777215, skyp, sc).astype(np.uint32)
        f, r, u = cam.axes()
        c = np.zeros(44, np.float32)
        c[0:3] = f; c[3] = tx; c[4:7] = r; c[7] = ty; c[8:11] = u; c[11] = time
        c[12] = 0; c[13] = cam.pos[1] - 1.62; c[14] = 1.0; c[15] = 1
        c[16] = W; c[17] = H; c[18] = W * 4; c[19] = W * 4
        c[20:23] = np.mod(cam.pos, 4096.0); c[23] = tod
        q = self.dev.queue
        q.write_buffer(self.cb, 0, c.tobytes()); q.write_buffer(self.ab, 0, A.a.tobytes())
        q.write_buffer(self.sb, 0, sc.tobytes()); q.write_buffer(self.db, 0, d24.tobytes())
        enc = self.dev.create_command_encoder()
        rp = enc.begin_render_pass(color_attachments=[{"view": self.rt.create_view(), "load_op": "load", "store_op": "store"}])
        # the pass draws over the frame (discarding untouched pixels): put the frame under it first
        rp.end()
        img = np.frombuffer(sc.tobytes(), np.uint8).reshape(H, W, 4).copy()
        q.write_texture({"texture": self.rt}, img.tobytes(), {"bytes_per_row": W * 4}, (W, H, 1))
        enc2 = self.dev.create_command_encoder()
        rp = enc2.begin_render_pass(color_attachments=[{"view": self.rt.create_view(), "load_op": "load", "store_op": "store"}])
        rp.set_pipeline(self.pipe); rp.set_bind_group(0, self.bg); rp.draw(3, 1, 0, 0); rp.end()
        q.submit([enc.finish(), enc2.finish()])
        data = q.read_texture({"texture": self.rt}, {"bytes_per_row": W * 4}, (W, H, 1))
        return Image.fromarray(np.frombuffer(data, np.uint8).reshape(H, W, 4)[..., :3].copy())

def player_boxes(x, y, z, col=(0.85, 0.55, 0.3)):
    """a standing player (feet at x, y, z): legs, body, head"""
    return [(x - 0.25, y, z - 0.125, x + 0.25, y + 0.75, z + 0.125, (0.25, 0.3, 0.55)),
            (x - 0.25, y + 0.75, z - 0.125, x + 0.25, y + 1.5, z + 0.125, col),
            (x - 0.5, y + 0.75, z - 0.125, x - 0.25, y + 1.5, z + 0.125, col), (x + 0.25, y + 0.75, z - 0.125, x + 0.5, y + 1.5, z + 0.125, col),
            (x - 0.25, y + 1.5, z - 0.25, x + 0.25, y + 2.0, z + 0.25, (0.9, 0.72, 0.58))]
