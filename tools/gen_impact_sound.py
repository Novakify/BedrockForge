"""Original synthesized impact SFX for Bedrock Forge (no samples used).
Layers: sub thump (pitch-dropping sine) + transient crack (filtered noise) + short metallic ring + reverse-swell whoosh into the hit."""
import numpy as np, subprocess, sys, wave
SR = 44100
def env(n, a, d):  # attack/decay envelope in samples
    e = np.ones(n); a = max(1, a); e[:a] = np.linspace(0, 1, a); e[a:] = np.exp(-np.arange(n - a) / d); return e
def make(seed=1, length=0.9, pre=0.10):
    rng = np.random.default_rng(seed); n = int(SR * length); t = np.arange(n) / SR; p = int(SR * pre)
    out = np.zeros(n)
    # whoosh swell before the hit
    w = rng.standard_normal(p); w = np.convolve(w, np.ones(40) / 40, 'same'); out[:p] += w * np.linspace(0, 1, p) ** 3 * 0.35
    h = np.arange(n - p) / SR
    f = 120 * np.exp(-h * 9) + 38                          # sub thump: 158 Hz -> 38 Hz
    out[p:] += np.sin(2 * np.pi * np.cumsum(f) / SR) * env(n - p, 30, SR * 0.16) * 1.0
    c = rng.standard_normal(n - p); c = c - np.convolve(c, np.ones(6) / 6, 'same')   # crack: bright noise burst
    out[p:] += c * env(n - p, 5, SR * 0.018) * 0.9
    ring = sum(np.sin(2 * np.pi * fr * h + ph) for fr, ph in ((1830, 0), (2710, 1.3), (3940, 2.2))) / 3
    out[p:] += ring * env(n - p, 10, SR * 0.09) * 0.22
    out = np.tanh(out * 1.8)                                # punchy saturation
    out /= np.max(np.abs(out)) * 1.05
    return (out * 32767).astype(np.int16)
if __name__ == "__main__":
    dst = sys.argv[1]
    for i, seed in enumerate((1, 2, 3)):                    # 3 variations so repeated hits don't sound identical
        pcm = make(seed); wav = f"/tmp/bf_impact_{i}.wav"
        with wave.open(wav, "wb") as f: f.setnchannels(1); f.setsampwidth(2); f.setframerate(SR); f.writeframes(pcm.tobytes())
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", wav, "-c:a", "libvorbis", "-q:a", "6", f"{dst}/impact_{i}.ogg"], check=True)
