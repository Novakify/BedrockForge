#!/usr/bin/env python3
"""VVPackGen - builds a per-shaderpack Vibrant Visuals resource pack (atmospherics + fog + lighting).

Vanilla VV JSON is read straight from the game's packed archives (Mojang files, never committed):
  <instance>/data/resource_packs/vanilla_1.21.90/__brarchive/{atmospherics,lighting}.brarchive
  fogs: newest of vanilla_*/__brarchive/fogs.brarchive
and transformed with tools/VVPackGen/profiles.json. Every vanilla identifier is overridden, so the
per-biome VV settings vanilla ships since 1.21.90 can't leak the vanilla haze back in.

usage: build.py <instance_dir> <out_dev_resource_packs_dir> <Pack> [<Pack>...]
"""
import colorsys, glob, json, os, re, struct, sys, uuid

def brarchive(p):
    b = open(p, 'rb').read(); n = struct.unpack_from('<I', b, 8)[0]; base = 16 + n * 256; out = {}
    for i in range(n):
        e = b[16 + i * 256:16 + (i + 1) * 256]; name = e[1:1 + e[0]].decode()
        off, ln = struct.unpack_from('<II', e, 248); out[name] = json.loads(b[base + off:base + off + ln])
    return out

def vkey(p):
    m = re.search(r'vanilla_([\d.]+)', p); return tuple(int(x) for x in m.group(1).split('.')) if m else (0,)

def kf(v, f):  # apply f to a scalar or every keyframe value
    return {k: f(x) for k, x in v.items()} if isinstance(v, dict) else f(v)

def recolor(c, sat, val):
    h, s, v = colorsys.rgb_to_hsv(*(x / 255 for x in c[:3]))
    return [round(x * 255) for x in colorsys.hsv_to_rgb(h, min(s * sat, 1), min(v * val, 1))]

def atmos(d, a):
    s = d['minecraft:atmosphere_settings']
    for k, m in (('rayleigh_strength', 'rayleigh'), ('sun_mie_strength', 'sun_mie'), ('moon_mie_strength', 'sun_mie'), ('sun_glare_shape', 'glare')):
        if k in s and m in a: s[k] = kf(s[k], lambda x, m=m: round(x * a[m], 4))
    for k, m in (('sky_zenith_color', 'zenith'), ('sky_horizon_color', 'horizon')):
        if k in s and m in a: s[k] = kf(s[k], lambda c, m=m: recolor(c, a[m]['sat'], a[m]['val']))
    return d

def vol_layer(p):
    d = {'max_density': p['max_density']}
    if p.get('uniform'): d['uniform'] = True
    else: d.update(zero_density_height=float(p['zero_density_height']), max_density_height=float(p['max_density_height']))
    return d

def fog(d, f):
    s = d['minecraft:fog_settings']; v = s.get('volumetric', {}); air = f['air']; t = f.get('biome_tint', 0.0)
    sc, ab = list(air['scattering']), list(air['absorption'])
    va = v.get('media_coefficients', {}).get('air')
    if va and t > 0 and (max(va['absorption']) > 0.01 or max(va['scattering']) > 0.08):  # tinted biome (swamp, pale garden...)
        k = air['max_density'] / max(v['density']['air'].get('max_density', 0.05), 1e-3)
        sc = [round((1 - t) * a + t * b * k, 4) for a, b in zip(sc, va['scattering'])]
        ab = [round((1 - t) * a + t * b * k, 4) for a, b in zip(ab, va['absorption'])]
    w = f['weather']  # density only
    s['volumetric'] = {
        'density': {'air': vol_layer(air), 'weather': vol_layer(w)},
        'media_coefficients': {'air': {'scattering': sc, 'absorption': ab}},   # only air/water/cloud are valid here
        'henyey_greenstein_g': {'air': {'henyey_greenstein_g': air['g']}}}
    if d.get('format_version', '0') < '1.21.90': d['format_version'] = '1.21.90'
    return d

def lighting(d, l):
    s = d['minecraft:lighting_settings']; dl = s.get('directional_lights', {})
    o = dl.get('orbital', dl)
    if 'orbital' in dl and 'orbital_offset_degrees' in l: dl['orbital']['orbital_offset_degrees'] = l['orbital_offset_degrees']
    if 'sun' in o and 'sun_scale' in l: o['sun']['illuminance'] = kf(o['sun']['illuminance'], lambda x: round(x * l['sun_scale'], 4))
    if 'moon' in o:
        if 'moon_color' in l: o['moon']['color'] = kf(o['moon']['color'], lambda c: list(l['moon_color']))
        if 'moon_illuminance' in l: o['moon']['illuminance'] = kf(o['moon']['illuminance'], lambda x: l['moon_illuminance'] if x > 0 else x)
    if 'sky_intensity' in l: s['sky'] = {'intensity': l['sky_intensity']}
    if 'ambient' in l: s['ambient'] = {'color': s.get('ambient', {}).get('color', '#FFFFFF'), 'illuminance': l['ambient']}
    return d

NETHER_END = {'hell', 'the_end', 'crimson_forest', 'warped_forest', 'soulsand_valley', 'basalt_deltas'}

def main():
    inst, out, packs = sys.argv[1], sys.argv[2], sys.argv[3:]
    prof = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'profiles.json')))
    rp = os.path.join(inst, 'data', 'resource_packs'); v90 = os.path.join(rp, 'vanilla_1.21.90', '__brarchive')
    A = brarchive(os.path.join(v90, 'atmospherics.brarchive')); L = brarchive(os.path.join(v90, 'lighting.brarchive'))
    F = {}
    for p in sorted(glob.glob(os.path.join(rp, 'vanilla_*', '__brarchive', 'fogs.brarchive')), key=vkey):
        F.update(brarchive(p))
    B = {}
    for p in sorted(glob.glob(os.path.join(rp, 'vanilla*', '__brarchive', 'biomes.brarchive')), key=vkey):
        for k, v in brarchive(p).items():
            if k.endswith('.client_biome.json'): B[k.split(':')[-1]] = v
    # only biomes the game ships client_biome files for (legacy biomes_client.json names log 'no biome with that name')
    for name in packs:
        P = prof[name]; d = os.path.join(out, P['rp_dir'])
        u = P.get('uuid') or [str(uuid.uuid5(uuid.NAMESPACE_URL, 'bedrockforge/vv/' + name + s)) for s in ('/h', '/m')]
        files = {}
        for n, j in A.items():
            if n.split('_')[0] not in ('hell', 'end', 'basalt', 'crimson', 'soulsand', 'warped'): files['atmospherics/' + n] = atmos(json.loads(json.dumps(j)), P['atmos'])
        for n, j in L.items():
            if n not in ('end_lighting.json', 'nether_lighting.json'): files['lighting/' + n] = lighting(json.loads(json.dumps(j)), P['lighting'])
        ns = P.get('namespace', 'bfvv_' + name.lower())
        def fid(vid): return ns + ':' + vid.split(':', 1)[1]   # packs may not register minecraft:* fogs
        fogids = {}
        for n, j in F.items():
            vid = j['minecraft:fog_settings']['description']['identifier']
            if 'volumetric' in j['minecraft:fog_settings'] or n == 'default_fog_setting.json':
                if vid == 'minecraft:fog_the_end': continue
                j = fog(json.loads(json.dumps(j)), P['fog']); j['minecraft:fog_settings']['description']['identifier'] = fid(vid)
                files['fogs/' + n] = j; fogids[vid] = fid(vid)
        # client biomes: point every overworld biome at our fogs (keep vanilla's other components)
        for bname, b in B.items():
            c = json.loads(json.dumps(b)); comp = c['minecraft:client_biome']['components']
            if c['minecraft:client_biome']['description']['identifier'].split(':')[-1] in NETHER_END: continue
            vf = comp.get('minecraft:fog_appearance', {}).get('fog_identifier', 'minecraft:fog_default')
            comp['minecraft:fog_appearance'] = {'fog_identifier': fogids.get(vf, fogids['minecraft:fog_default'])}
            files['biomes/' + bname] = c
        files['shadows/global.json'] = {'format_version': '1.21.80', 'minecraft:shadow_settings': {'shadow_style': P.get('shadow_style', 'soft_shadows')}}
        files['manifest.json'] = {'format_version': 2, 'header': {'name': P['name'], 'description': 'Vibrant Visuals look for the %s shaderpack (Bedrock Forge, tools/VVPackGen)' % name,
            'uuid': u[0], 'version': [1, 0, 0], 'min_engine_version': [1, 21, 90]},
            'modules': [{'type': 'resources', 'uuid': u[1], 'version': [1, 0, 0]}], 'capabilities': ['pbr'], 'metadata': {'authors': ['Bedrock Forge'], 'license': 'MIT'}}
        for rel, j in files.items():
            p = os.path.join(d, rel); os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, 'w') as fh: json.dump(j, fh, indent=1)
        print('%s -> %s (%d files)' % (name, d, len(files)))

if __name__ == '__main__': main()
