#!/usr/bin/env python3
"""gen_debris_sounds.py — synthesize the GPU-debris impact and settle sounds
(DebrisInteractionPlan Phase 6b: resources/sounds/sfx/debris/).

Debris is broken voxel material - mostly stone - so an impact is a short, bright rock
CLACK: a few inharmonic damped resonances (a small block rings at 1-3 kHz, a large one
lower) excited by a noise transient, no tail. A settle is the same family, quieter and
softer: the piece rocking to rest. Variation pools (4 impacts, 2 settles) with per-play
pitch/volume jitter in sounds.json keep a collapsing wall from machine-gunning one sample.

Procedural because no FREESOUND_API_KEY was available for tools/fetch_cc0_sounds.py on
2026-10-07; a CC0 recording can replace these later through that tool (same event names).
Deterministic (fixed seeds); mono 16-bit 44.1 kHz. Recipe family: gen_hit_sound.py.

Usage: python tools/gen_debris_sounds.py
"""
import os
import struct
import wave

import numpy as np

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(REPO, "resources", "sounds", "sfx", "debris")
SR = 44100


def write_wav(path, x):
    x = np.clip(x, -1.0, 1.0)
    pcm = (x * 32767.0).astype(np.int16)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())


def clack(seed, dur, base_hz, modes, decay, click_gain, noise_ms, peak):
    rng = np.random.default_rng(seed)
    n = int(SR * dur)
    t = np.arange(n) / SR
    y = np.zeros(n)
    # Inharmonic rock modes (ratios of a stiff block, jittered per variant).
    for k, ratio in enumerate(modes):
        f = base_hz * ratio * (1.0 + rng.uniform(-0.04, 0.04))
        amp = (0.8 ** k) * rng.uniform(0.7, 1.0)
        d = decay * (1.0 + 0.6 * k)
        y += amp * np.sin(2 * np.pi * f * t + rng.uniform(0, 2 * np.pi)) * np.exp(-d * t)
    # Noise transient: the grit of the contact.
    nn = int(SR * noise_ms / 1000.0)
    burst = rng.standard_normal(nn) * np.exp(-np.linspace(0, 6, nn))
    burst = np.convolve(burst, np.ones(3) / 3.0, mode="same")   # take the fizz off
    y[:nn] += click_gain * burst
    # 2 ms attack ramp (no DC click), normalise to the requested peak.
    a = int(SR * 0.002)
    y[:a] *= np.linspace(0, 1, a)
    y *= peak / max(np.max(np.abs(y)), 1e-9)
    return y


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    modes = [1.0, 2.32, 3.87, 5.41]
    impacts = [
        ("impact_1.wav", dict(seed=11, dur=0.16, base_hz=1150, decay=38, click_gain=0.9, noise_ms=9, peak=0.9)),
        ("impact_2.wav", dict(seed=12, dur=0.18, base_hz=860,  decay=32, click_gain=0.8, noise_ms=11, peak=0.9)),
        ("impact_3.wav", dict(seed=13, dur=0.14, base_hz=1480, decay=44, click_gain=1.0, noise_ms=8, peak=0.85)),
        ("impact_4.wav", dict(seed=14, dur=0.20, base_hz=690,  decay=28, click_gain=0.7, noise_ms=13, peak=0.9)),
    ]
    settles = [
        ("settle_1.wav", dict(seed=21, dur=0.10, base_hz=1700, decay=60, click_gain=0.5, noise_ms=5, peak=0.45)),
        ("settle_2.wav", dict(seed=22, dur=0.11, base_hz=1350, decay=55, click_gain=0.4, noise_ms=6, peak=0.4)),
    ]
    for name, kw in impacts + settles:
        write_wav(os.path.join(OUT_DIR, name), clack(modes=modes, **kw))
        print("wrote", os.path.join("sfx", "debris", name))


if __name__ == "__main__":
    main()
