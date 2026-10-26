#!/usr/bin/env python3
"""Generates the deterministic test samples in tests/fixtures/samples (48 kHz, 16-bit).

Run from anywhere: python3 tests/fixtures/make_samples.py
"""

import math
import struct
import wave
from pathlib import Path

RATE = 48000
OUT = Path(__file__).parent / "samples"


def write(name, left, right=None):
    frames = bytearray()
    for i, l in enumerate(left):
        values = [l] if right is None else [l, right[i]]
        for v in values:
            frames += struct.pack("<h", max(-32768, min(32767, round(v * 32768))))
    with wave.open(str(OUT / name), "wb") as w:
        w.setnchannels(1 if right is None else 2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(bytes(frames))


def noise(seed):
    """Small LCG so the output never depends on Python's random module."""
    state = seed
    while True:
        state = (1103515245 * state + 12345) % 2**31
        yield state / 2**30 - 1.0


def seconds(s):
    return range(round(s * RATE))


def sine(freq, length, amp):
    return [amp * math.sin(2 * math.pi * freq * i / RATE) for i in seconds(length)]


def kick():
    out, phase = [], 0.0
    for i in seconds(0.3):
        t = i / RATE
        freq = 50 + 100 * math.exp(-t * 30)
        phase += 2 * math.pi * freq / RATE
        out.append(0.9 * math.exp(-t * 12) * math.sin(phase))
    return out


def snare():
    n = noise(7)
    return [math.exp(-(i / RATE) * 25) * (0.4 * next(n) + 0.3 * math.sin(2 * math.pi * 180 * i / RATE))
            for i in seconds(0.2)]


def hat():
    n = noise(3)
    return [0.5 * math.exp(-(i / RATE) * 80) * next(n) for i in seconds(0.05)]


def main():
    OUT.mkdir(exist_ok=True)
    write("kick.wav", kick())
    write("snare.wav", snare())
    write("hat.wav", hat())
    # A2 (MIDI 45) sine for pitch tests: 1 s at amplitude 0.5
    write("bass_a2.wav", sine(110.0, 1.0, 0.5))
    # 1 kHz beep, 100 ms at amplitude 0.5: peak -6.02 dBFS
    write("beep.wav", sine(1000.0, 0.1, 0.5))
    # Stereo 1 kHz, left at 0.5 and right at 0.25, 500 ms
    write("stereo_beep.wav", sine(1000.0, 0.5, 0.5), sine(1000.0, 0.5, 0.25))


if __name__ == "__main__":
    main()
