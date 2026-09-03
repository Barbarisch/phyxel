#!/usr/bin/env python3
"""gen_hit_sound.py — synthesize the furniture-activation thunk (resources/sounds/hit.wav).

VoxelInteractionSystem.cpp (furniture activation) has referenced hit.wav since the
dynamic-furniture work, but the file never existed — every activation logged a load
error and played silence (the SoundSystemV2.md §1 dead reference).

A hand knocking a wooden object loose is one soft impact: a mid-frequency TAP
(~180 Hz damped sine, faster decay than the axe KNOCK — furniture is lighter than
a trunk) with a small woody click on top and no rumble tail. Deterministic; mono
16-bit 44.1 kHz, ~0.15 s. Same recipe family as gen_chop_sound.py.
"""
import os
import struct
import wave

import numpy as np

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "resources", "sounds", "hit.wav")

SR = 44100
DUR = 0.15
N = int(SR * DUR)
t = np.arange(N) / SR
rng = np.random.default_rng(11)

# 1. woody click: 4 ms of high-passed noise, very fast decay
click = rng.standard_normal(N)
click = np.diff(click, prepend=0.0)
click *= np.exp(-t / 0.003)
click *= 0.5

# 2. body TAP: damped sine ~180 Hz with slight downward bend, quick decay
f0 = 200.0 * np.exp(-t / 0.05) + 150.0
phase = 2 * np.pi * np.cumsum(f0) / SR
tap = np.sin(phase) * np.exp(-t / 0.035)
tap += 0.25 * np.sin(2.0 * phase) * np.exp(-t / 0.02)

sig = click + tap
sig[: int(0.001 * SR)] *= np.linspace(0, 1, int(0.001 * SR))  # de-click attack
sig /= np.max(np.abs(sig))
sig *= 0.8

with wave.open(OUT, "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(SR)
    w.writeframes(struct.pack("<" + "h" * N, *(np.clip(sig, -1, 1) * 32767).astype(np.int16)))
print("wrote", OUT)
