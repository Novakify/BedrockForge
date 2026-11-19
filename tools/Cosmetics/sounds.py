"""Original knife sounds for BF Cosmetics (by Novak) - synthesised from scratch, nothing sampled.

Calibrated against the Purxiy client's own sound design (its UI set, used as a reference with permission - the
knife sounds are not readable files in the loader, they live in its protected native core, which we do not open):
soft and warm rather than bright - peaks ~0.15-0.35, spectral centroid ~1-2.5 kHz, energy front-loaded (90 % in the
first 0.05-0.25 s) with quiet tails. Our steel follows that: struck modes kept low and short, a little body under
every hit, gentle levels.

Real references studied for what they are made of (not copied): a blade through air is band-limited noise whose
centre frequency rises then falls with the blade's speed, plus a faint whistle off the edge; steel-on-steel is a
short impulse ringing a few inharmonic modes (thin parts ring high and short, heavier parts lower and longer); a
draw from a sheath is a scrape (noise sweeping up) followed by the blade ringing.

    python3 tools/Cosmetics/sounds.py <pack>/sounds/bf_cosmetics      (needs ffmpeg for .ogg)
"""
from __future__ import annotations

import subprocess
import sys
import wave
from pathlib import Path

import numpy as np

SR = 44100


def t_(dur):
    return np.arange(int(SR * dur)) / SR


def bandnoise(dur, f_curve, q=4.0, seed=0):
    """noise shaped by a moving band (overlap-add STFT, Gaussian band of width f/q around f_curve(t)): unlike a
    2-pole filter, nothing leaks into the treble, so a whoosh stays airy instead of hissy"""
    r = np.random.default_rng(seed)
    n = int(SR * dur)
    N, H = 1024, 256
    x = r.standard_normal(n + N)
    out = np.zeros(n + N)
    win = np.hanning(N)
    freqs = np.fft.rfftfreq(N, 1 / SR)
    for start in range(0, n, H):
        t = start / SR
        fc = max(float(f_curve(np.array([t]))[0]), 40.0)
        bw = fc / q
        X = np.fft.rfft(x[start:start + N] * win)
        X *= np.exp(-0.5 * ((freqs - fc) / bw) ** 2)
        out[start:start + N] += np.fft.irfft(X, N) * win
    return out[:n] / (np.abs(out[:n]).max() + 1e-9)


def env(dur, attack, peak_at, release, shape=2.0):
    t = t_(dur)
    e = np.where(t < attack, (t / max(attack, 1e-4)) ** 0.7, 1.0)
    e *= np.where(t > peak_at, np.exp(-((t - peak_at) / release) ** shape * 3), 1.0)
    return e


def modes(dur, freqs, decays, amps, seed=0, detune=0.003):
    """steel ringing: a sum of exponentially decaying inharmonic partials, struck (random phase, slight detune)"""
    r = np.random.default_rng(seed)
    t = t_(dur)
    out = np.zeros_like(t)
    for f, d, a in zip(freqs, decays, amps):
        f *= 1 + r.uniform(-detune, detune)
        out += a * np.sin(2 * np.pi * f * t + r.uniform(0, 2 * np.pi)) * np.exp(-t / d)
    return out


def strike(dur, seed=0, hp=3000):
    """the hit itself: a few ms of bright noise"""
    return bandnoise(dur, lambda t: 4200 + 0 * t, q=1.5, seed=seed) * np.exp(-t_(dur) / 0.003)


def mix(*parts):
    L = max(len(p) for p in parts)
    out = np.zeros(L)
    for p in parts:
        out[:len(p)] += p
    return out


def pad(x, dur):
    out = np.zeros(int(SR * dur))
    out[:min(len(x), len(out))] = x[:len(out)]
    return out


def norm(x, peak=0.7):
    x = x - np.mean(x)
    fade = min(len(x), int(SR * 0.01))
    x[-fade:] *= np.linspace(1, 0, fade)
    return x / (np.abs(x).max() + 1e-9) * peak


# ------------------------------------------------------------------ the sounds
def swish(seed=1, dur=0.24, lo=520, hi=1900):
    peak = 0.09
    f = lambda t: lo + (hi - lo) * np.exp(-((t - peak) / 0.06) ** 2)
    air = bandnoise(dur, f, q=1.6, seed=seed) * env(dur, 0.03, peak, 0.07)
    whistle = bandnoise(dur, lambda t: f(t) * 1.6, q=14, seed=seed + 7) * env(dur, 0.05, peak, 0.05) * 0.35
    return norm(air + whistle * 0.7, 0.38)


def cut(seed=3):
    dur = 0.28
    s = swish(seed, 0.18, 650, 2300) * 0.7
    slice_ = pad(bandnoise(0.06, lambda t: 3600 - 16000 * t, q=2.5, seed=seed + 1) * np.exp(-t_(0.06) / 0.018), dur)
    thud = pad(np.sin(2 * np.pi * 120 * t_(0.08)) * np.exp(-t_(0.08) / 0.02) * 0.6, dur)
    out = mix(pad(s, dur), np.roll(slice_, int(SR * 0.05)) * 0.9, np.roll(thud, int(SR * 0.05)))
    return norm(out, 0.45)


def click(seed=5, pitch=1.0):
    dur = 0.16
    ring = modes(dur, [1960 * pitch, 3140 * pitch, 4620 * pitch, 6100 * pitch], [0.05, 0.03, 0.016, 0.008], [1, 0.55, 0.28, 0.12], seed)
    body = np.sin(2 * np.pi * 420 * pitch * t_(dur)) * np.exp(-t_(dur) / 0.012) * 0.35
    return norm(mix(ring, body, pad(strike(0.02, seed) * 0.4, dur)), 0.32)


def ring_spin(seed=7):
    """a karambit turning round the finger: a low whir with the ring ticking against the knuckle"""
    dur = 0.5
    whir = bandnoise(dur, lambda t: 380 + 260 * np.sin(2 * np.pi * 6 * t), q=3, seed=seed) * env(dur, 0.06, 0.3, 0.12) * 0.5
    ticks = np.zeros(int(SR * dur))
    for i, at in enumerate((0.05, 0.19, 0.33)):
        c = click(seed + i, 1.0 + 0.04 * i) * (0.5 - 0.1 * i)
        k = int(SR * at)
        ticks[k:k + len(c)] += c[:len(ticks) - k]
    return norm(whir + ticks, 0.32)


def clack(seed=11, pitch=1.0):
    """balisong handle meeting the tang pins: a lower, chunkier metal-on-metal hit with a little rattle"""
    dur = 0.14
    ring = modes(dur, [1240 * pitch, 2110 * pitch, 3380 * pitch, 5200 * pitch], [0.034, 0.024, 0.014, 0.008], [1, 0.7, 0.4, 0.18], seed)
    body = bandnoise(dur, lambda t: 650 + 0 * t, q=2, seed=seed) * np.exp(-t_(dur) / 0.012) * 0.7
    rattle = np.roll(ring, int(SR * 0.018)) * 0.25
    return norm(mix(ring, body, rattle, pad(strike(0.015, seed) * 0.5, dur)), 0.36)


def latch(seed=13):
    dur = 0.22
    a = pad(clack(seed, 1.15), dur)
    b = np.roll(pad(click(seed + 1, 0.8) * 0.6, dur), int(SR * 0.05))
    return norm(a + b, 0.38)


def draw(seed=17):
    """the draw: a short rising scrape, then the blade rings (long, bright, slightly beating)"""
    dur = 1.1
    scrape = pad(bandnoise(0.26, lambda t: 1300 + 5200 * t, q=3, seed=seed) * env(0.26, 0.02, 0.2, 0.05), dur) * 0.7
    ring = modes(dur, [1420, 1427, 2960, 4380, 6050], [0.6, 0.55, 0.32, 0.18, 0.08], [0.6, 0.6, 0.42, 0.22, 0.1], seed)
    ring = np.roll(ring, int(SR * 0.2)) * np.r_[np.zeros(int(SR * 0.2)), np.ones(int(SR * dur) - int(SR * 0.2))]
    return norm(scrape + ring * 0.8, 0.36)


def dagger_catch(seed=19):
    """the dagger caught after the toss: a heavier, lower ring and a soft palm thump"""
    dur = 0.8
    ring = modes(dur, [980, 2140, 3360, 4900], [0.45, 0.28, 0.16, 0.08], [0.8, 0.55, 0.3, 0.12], seed)
    thump = pad(np.sin(2 * np.pi * 95 * t_(0.09)) * np.exp(-t_(0.09) / 0.025), dur)
    return norm(ring * 0.7 + thump * 0.9 + pad(strike(0.02, seed) * 0.3, dur), 0.36)


SOUNDS = {
    "swish1": lambda: swish(1), "swish2": lambda: swish(2, 0.22, 600, 2100), "swish3": lambda: swish(4, 0.26, 480, 1700),
    "cut1": lambda: cut(3), "cut2": lambda: cut(6),
    "click1": lambda: click(5), "click2": lambda: click(8, 1.07),
    "ring_spin": lambda: ring_spin(7),
    "clack1": lambda: clack(11), "clack2": lambda: clack(12, 0.93), "clack3": lambda: clack(14, 1.08),
    "latch": lambda: latch(13),
    "draw": lambda: draw(17),
    "dagger_catch": lambda: dagger_catch(19),
}

# sound_definitions: event -> files (Bedrock picks one at random) + pitch/volume
EVENTS = {
    "bfk.swish": (["swish1", "swish2", "swish3"], 0.55),
    "bfk.cut": (["cut1", "cut2"], 0.7),
    "bfk.click": (["click1", "click2"], 0.5),
    "bfk.ring_spin": (["ring_spin"], 0.5),
    "bfk.clack": (["clack1", "clack2", "clack3"], 0.55),
    "bfk.latch": (["latch"], 0.6),
    "bfk.draw": (["draw"], 0.55),
    "bfk.dagger_catch": (["dagger_catch"], 0.6),
}


def write(out: Path):
    out.mkdir(parents=True, exist_ok=True)
    for name, fn in SOUNDS.items():
        x = fn()
        wav = out / f"{name}.wav"
        with wave.open(str(wav), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(SR)
            w.writeframes((np.clip(x, -1, 1) * 32767).astype(np.int16).tobytes())
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", str(wav), "-c:a", "libvorbis", "-q:a", "6", str(out / f"{name}.ogg")], check=True)
        wav.unlink()


QUIET = 0.45   # the Knives "Quiet sounds" setting plays the ".q" events (same files, under half the level)


def definitions(prefix="sounds/bf_cosmetics"):
    defs = {}
    for ev, (files, vol) in EVENTS.items():
        for name, v in ((ev, vol), (ev + ".q", vol * QUIET)):
            defs[name] = {"category": "player", "min_distance": 1.0, "max_distance": 12.0,
                          "sounds": [{"name": f"{prefix}/{f}", "volume": round(v, 3), "pitch": 1.0} for f in files]}
    return {"format_version": "1.14.0", "sound_definitions": defs}


if __name__ == "__main__":
    write(Path(sys.argv[1]))
