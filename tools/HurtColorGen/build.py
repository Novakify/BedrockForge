#!/usr/bin/env python3
"""HurtColorGen - make the hurt flash a strong, detectable colour for Bedrock Forge impact frames.

Vanilla render controllers mostly leave `is_hurt_color` unset (engine default: half-transparent red). On Vibrant Visuals
that reads as a ~10% red shift after PBR lighting - too weak for the VV impact frame (shaders/src/_shared/
ColorPostVV.fragment.hlsli) to find the entity that was hit. This copies every vanilla render controller (read from the
game's own packed archives - Mojang files, never committed) with `is_hurt_color` = opaque red, into a resource pack.
Fancy impact frames key on a red OverlayColor too, so they keep working.

usage: build.py <instance dir> <resource pack dir> [--alpha 1.0]
"""
import argparse, glob, json, os, re, struct

def brarchive(p):
    b = open(p, 'rb').read(); n = struct.unpack_from('<I', b, 8)[0]; base = 16 + n * 256; out = {}
    for i in range(n):
        e = b[16 + i * 256:16 + (i + 1) * 256]; name = e[1:1 + e[0]].decode()
        off, ln = struct.unpack_from('<II', e, 248); out[name] = b[base + off:base + off + ln]
    return out

def vkey(p):
    m = re.search(r'vanilla_([\d.]+)', p); return tuple(int(x) for x in m.group(1).split('.')) if m else (0,)

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('instance'); ap.add_argument('rp'); ap.add_argument('--alpha', type=float, default=1.0)
    a = ap.parse_args()
    rp = os.path.join(a.instance, 'data', 'resource_packs'); files = {}
    for p in sorted(glob.glob(os.path.join(rp, 'vanilla*', '__brarchive', 'render_controllers.brarchive')), key=vkey):
        files.update(brarchive(p))   # newest version of each file wins
    out = os.path.join(a.rp, 'render_controllers'); os.makedirs(out, exist_ok=True); n = 0
    hurt = {'r': 1.0, 'g': 0.0, 'b': 0.0, 'a': a.alpha}
    for name, raw in files.items():
        d = json.loads(re.sub(rb'//[^\n]*', b'', raw))
        for rc in d.get('render_controllers', {}).values():
            if 'is_hurt_color' not in rc: rc['is_hurt_color'] = hurt; n += 1   # keep the 2 vanilla custom ones
        with open(os.path.join(out, name), 'w') as f: json.dump(d, f, indent=1)
    print('%d controllers -> opaque red hurt colour, %d files in %s' % (n, len(files), out))

if __name__ == '__main__': main()
