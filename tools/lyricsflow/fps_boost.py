#!/usr/bin/env python3
"""LyricsFlow FPS boost for Bedrock (port of LyricsFlow 2.1 perf.FpsBoost: lower the costly game settings, keep a backup,
restore on demand). Bedrock reads options.txt at start and rewrites it on exit, so run this with the GAME CLOSED.
Only keys already present in the file are changed; everything else is left alone.

usage: fps_boost.py apply   <options.txt>    # backup -> options.txt.lyricsflow-backup, then boost
       fps_boost.py restore <options.txt>    # put the backed-up values of the boosted keys back
       fps_boost.py show    <options.txt>    # print the keys it would touch and their current values"""
import pathlib
import sys

# key -> boosted value. Java's FpsBoost lowers entity shadows, particles and biome blend; these are the Bedrock
# settings with the same cost (fancy leaves / bubbles / skies, clouds, particle range, VSync, frame cap).
BOOST = {
    "gfx_transparentleaves": "0",   # Fancy leaves off
    "gfx_fancyskies": "0",          # Beautiful skies off
    "gfx_renderclouds": "0",        # Clouds off
    "gfx_fancybubbles": "0",        # Fancy bubbles off
    "gfx_particleviewdistance": "0",
    "gfx_vsync": "0",               # VSync off (more FPS, may tear)
    "gfx_max_framerate": "0",       # no frame cap
    "gfx_ssao": "0",
    "gfx_upscaling": "0",
}


def read(p):
    return pathlib.Path(p).read_text(encoding='utf-8', errors='replace').splitlines()


def main():
    cmd, path = sys.argv[1], pathlib.Path(sys.argv[2])
    lines = read(path)
    keys = {l.split(':', 1)[0]: i for i, l in enumerate(lines) if ':' in l}
    backup = path.with_name(path.name + '.lyricsflow-backup')
    if cmd == 'show':
        for k in BOOST:
            print(f"{k:28s} {'(not in file)' if k not in keys else lines[keys[k]].split(':', 1)[1]}")
        return
    if cmd == 'apply':
        if not backup.exists():
            backup.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        changed = []
        for k, v in BOOST.items():
            if k in keys:
                old = lines[keys[k]].split(':', 1)[1]
                if old != v:
                    lines[keys[k]] = f"{k}:{v}"
                    changed.append(f"{k} {old} -> {v}")
        path.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        print('\n'.join(changed) or 'already boosted')
        return
    if cmd == 'restore':
        if not backup.exists():
            print('no backup, nothing to restore')
            return
        old = {l.split(':', 1)[0]: l for l in read(backup) if ':' in l}
        for k in BOOST:
            if k in keys and k in old:
                lines[keys[k]] = old[k]
        path.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        print('restored the boosted keys from', backup.name)
        return
    raise SystemExit('unknown command ' + cmd)


if __name__ == '__main__':
    main()
