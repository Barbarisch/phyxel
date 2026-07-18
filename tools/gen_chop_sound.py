#!/usr/bin/env python3
"""gen_chop_sound.py — synthesize the axe-hits-wood impact (resources/sounds/axe_chop.wav).

An axe biting a trunk is two events ~5 ms apart: a bright, very short CRACK
(the blade splitting fibers — band-limited noise burst, fast decay) on top of a
deep KNOCK (the trunk's body resonance — a ~90 Hz damped sine with a pitch drop,
plus a 180 Hz overtone). A touch of low rumble tail sells the mass of the tree.
Deterministic; mono 16-bit 44.1 kHz, ~0.28 s. Re-run any time; the engine loads
the wav fresh per playSound3D.
"""
import os
import struct
import wave

import numpy as np

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "resources", "sounds", "axe_chop.wav")

SR = 44100
DUR = 0.28
N = int(SR * DUR)
t = np.arange(N) / SR
rng = np.random.default_rng(7)

# 1. the CRACK: 8 ms of noise, band-passed by simple differencing (bright), sharp decay
crack = rng.standard_normal(N)
crack = np.diff(crack, prepend=0.0)                    # high-pass tilt
crack *= np.exp(-t / 0.006)
crack *= 0.9

# 2. the KNOCK: damped sine at ~90 Hz with a downward pitch bend + one overtone
f0 = 96.0 * np.exp(-t / 0.12) + 62.0                   # 158 Hz -> 62 Hz sweep feel
phase = 2 * np.pi * np.cumsum(f0) / SR
knock = np.sin(phase) * np.exp(-t / 0.075) * 1.0
knock += 0.35 * np.sin(2.1 * phase) * np.exp(-t / 0.045)

# 3. body rumble tail (low-passed noise, slow-ish decay, quiet)
rumble = rng.standard_normal(N)
for _ in range(3):                                     # crude low-pass by smoothing
    rumble = np.convolve(rumble, np.ones(9) / 9.0, mode="same")
rumble *= np.exp(-t / 0.12) * 0.25

sig = crack + knock + rumble
sig[: int(0.001 * SR)] *= np.linspace(0, 1, int(0.001 * SR))   # de-click attack
sig /= np.max(np.abs(sig))
sig *= 0.85

with wave.open(OUT, "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(SR)
    w.writeframes(struct.pack("<" + "h" * N, *(np.clip(sig, -1, 1) * 32767).astype(np.int16)))
print("wrote", OUT)
