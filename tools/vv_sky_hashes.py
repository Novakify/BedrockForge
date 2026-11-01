#!/usr/bin/env python3
"""Vibrant Visuals sky hooks for Renderer Foundation: list the pixel-shader hashes RF tags (SkyPass.cpp kindOfHash).

  python3 tools/vv_sky_hashes.py <game>/data/renderer/materials

No lazurite needed: scans the DXBC containers inside each material.bin, takes the pass / platform / stage from the
length-prefixed strings in front of them and hashes the whole container with FNV-1a 64 (what RF hashes at
CreateGraphicsPipelineState). PSV0 gives the register bindings (e.g. DeferredShading's SceneDepth = t5 / s5).
Re-run after a game update and paste the new hashes into SkyPass.cpp."""
import os, re, struct, sys

def fnv(b):
    h = 0xcbf29ce484222325
    for x in b:
        h = ((h ^ x) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h

def strings(d):
    out, i, n = [], 0, len(d)
    while i + 4 < n:
        L = struct.unpack_from('<I', d, i)[0]
        if 3 <= L <= 80 and i + 4 + L <= n and re.fullmatch(rb'[A-Za-z_][A-Za-z0-9_.]*', d[i + 4:i + 4 + L]):
            out.append((i, d[i + 4:i + 4 + L].decode())); i += 4 + L
        else:
            i += 1
    return out

PASSES = ('ForwardPBRTransparent', 'ForwardPBRTransparentSkyProbe', 'DepthOnlyAlphaTest', 'MotionOnly', 'Opaque',
          'DoDeferredShading', 'Fallback')

def scan(path):
    d = open(path, 'rb').read()
    st = strings(d)
    for m in re.finditer(b'DXBC', d):
        o = m.start()
        size = struct.unpack_from('<I', d, o + 24)[0]
        if o + size > len(d):
            continue
        before = [s for p, s in st if p < o]
        plat = next((s for s in reversed(before) if s.startswith('Direct3D_')), '?')
        stage = next((s for s in reversed(before) if s in ('Vertex', 'Fragment', 'Compute')), '?')
        pas = next((s for s in reversed(before) if s in PASSES and s != 'Fallback'), '?')
        yield pas, plat, stage, fnv(d[o:o + size])

def main():
    root = sys.argv[1]
    for mat in ('Sky', 'Stars', 'SunMoon', 'Clouds', 'StarsForwardPBR', 'SunMoonForwardPBR', 'CloudsForwardPBR', 'DeferredShading'):
        p = os.path.join(root, mat + '.material.bin')
        if not os.path.exists(p):
            continue
        seen = set()
        for pas, plat, stage, h in scan(p):
            if stage != 'Fragment' or plat not in ('Direct3D_SM60', 'Direct3D_SM65') or h in seen:
                continue
            seen.add(h)
            print(f'0x{h:016x}ull  {mat}/{pas} {plat}')

if __name__ == '__main__':
    main()
