"""Cuts reference slices from a DJ mix: the steadiest 90-second stretches (one track playing on
its own, not a transition), about one per 10 minutes and five at most, at least 4 minutes apart,
skipping the intro, the outro and quiet breakdowns. Usage: slice_mix.py <mix.mp3> <output folder>"""

import os
import subprocess
import sys

import numpy as np

RATE = 11025
SLICE = 90
GAP = 240
EDGE = 180

mix, outdir = sys.argv[1], sys.argv[2]
name = os.path.splitext(os.path.basename(mix))[0]

raw = subprocess.run(["ffmpeg", "-v", "error", "-i", mix, "-ac", "1", "-ar", str(RATE), "-f", "f32le", "-"],
                     capture_output=True, check=True).stdout
audio = np.frombuffer(raw, dtype=np.float32)
seconds = len(audio) // RATE

# One log-spectrum per second in 24 log-spaced bands.
frames = audio[: seconds * RATE].reshape(seconds, RATE)
spectra = np.abs(np.fft.rfft(frames * np.hanning(RATE), axis=1)) ** 2
freqs = np.fft.rfftfreq(RATE, 1 / RATE)
edges = np.geomspace(30, RATE / 2, 25)
bands = np.stack([spectra[:, (freqs >= lo) & (freqs < hi)].sum(axis=1) for lo, hi in zip(edges[:-1], edges[1:])], axis=1)
features = 10 * np.log10(bands + 1e-9)
level = 10 * np.log10(frames.astype(np.float64).__pow__(2).mean(axis=1) + 1e-12)

# How much each 90-second window changes inside: low means one steady track.
starts = np.arange(EDGE, max(EDGE + 1, seconds - EDGE - SLICE))
cumulative = np.cumsum(np.vstack([np.zeros(features.shape[1]), features]), axis=0)
cumulative_sq = np.cumsum(np.vstack([np.zeros(features.shape[1]), features ** 2]), axis=0)
mean = (cumulative[starts + SLICE] - cumulative[starts]) / SLICE
var = (cumulative_sq[starts + SLICE] - cumulative_sq[starts]) / SLICE - mean ** 2
change = var.mean(axis=1)

# Quiet windows (breakdowns, silence) aren't references.
window_level = np.array([level[s:s + SLICE].mean() for s in starts])
change[window_level < np.median(window_level) - 6] = np.inf

wanted = max(1, min(5, round(seconds / 600)))
chosen = []
for i in np.argsort(change):
    if not np.isfinite(change[i]):
        break
    s = int(starts[i])
    if all(abs(s - c) >= GAP for c in chosen):
        chosen.append(s)
    if len(chosen) == wanted:
        break

# Compressed mixes are cut as they are; uncompressed ones become FLAC, which is lossless and smaller.
extension = os.path.splitext(mix)[1].lower()
codec = ["-c:a", "flac"] if extension in (".wav", ".aif", ".aiff") else ["-c", "copy"]
extension = ".flac" if codec[1] == "flac" else extension

os.makedirs(outdir, exist_ok=True)
for n, s in enumerate(sorted(chosen), 1):
    out = os.path.join(outdir, f"{name} - {n:02d} ({s // 60}m{s % 60:02d}s){extension}")
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-ss", str(s), "-t", str(SLICE), "-i", mix, *codec, out], check=True)

print(f"{name}: {seconds // 60} min, {len(chosen)} slices at " + ", ".join(f"{s // 60}:{s % 60:02d}" for s in sorted(chosen)))
