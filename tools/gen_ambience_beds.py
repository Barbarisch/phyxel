#!/usr/bin/env python3
"""gen_ambience_beds.py — synthesize looping biome ambience beds + scatter one-shots.

Deterministic (fixed seeds), loop-conditioned (equal-power tail->head crossfade at
zero-gain-drift), and SELF-VALIDATING: every bed asserts its loop seam is inaudible
(RMS continuity across the wrap point) before it is written — the falsifiable loop
test from docs/SoundSystemV2.md §4.6, not "sounded fine once".

Wind/rumble beds are the category procedural synthesis does well (the design doc's
acquisition research); birdsong/animal scatter is deliberately NOT generated here —
that comes from CC0 recordings via the future fetch pipeline (AI/synth birds read
as wrong instantly).

Output: resources/sounds/ambience/*.wav (12 s loops, mono 16-bit 44.1 kHz) and
resources/sounds/sfx/*.wav one-shots. Re-run any time; provenance rows live in
resources/sounds/SOURCES.json.
"""
import os
import struct
import wave

import numpy as np

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_AMB = os.path.join(REPO, "resources", "sounds", "ambience")
OUT_SFX = os.path.join(REPO, "resources", "sounds", "sfx")
SR = 44100


def lowpass(x, alpha):
    """One-pole IIR lowpass, alpha in (0,1] — smaller = darker.
    y[i] = y[i-1] + alpha*(x[i]-y[i-1]), vectorized via lfilter."""
    from scipy.signal import lfilter
    return lfilter([alpha], [1.0, -(1.0 - alpha)], x)


def lowpass_fast(x, passes, kernel):
    """Moving-average lowpass — passes x kernel-width smoothing. FFT-based:
    kernels here reach ~2 s (88k taps), where direct convolve is minutes."""
    from scipy.signal import fftconvolve
    k = np.ones(kernel) / kernel
    for _ in range(passes):
        x = fftconvolve(x, k, mode="same")
    return x


def slow_am(n, rng, rate_hz, depth):
    """Slow random amplitude modulation: smoothed noise LFO in [1-depth, 1]."""
    ctrl_n = max(int(rate_hz * n / SR * 8), 4)
    ctrl = rng.standard_normal(ctrl_n)
    ctrl = np.interp(np.linspace(0, ctrl_n - 1, n), np.arange(ctrl_n), ctrl)
    ctrl = lowpass_fast(ctrl, 2, int(SR / max(rate_hz * 4, 0.5)))
    ctrl = (ctrl - ctrl.min()) / max(ctrl.max() - ctrl.min(), 1e-9)
    return 1.0 - depth + depth * ctrl


def condition_loop(x, fade_sec=0.5):
    """Equal-power crossfade of the tail into the head so the loop wraps clean."""
    f = int(fade_sec * SR)
    t = np.linspace(0.0, 1.0, f)
    head_gain = np.sqrt(t)
    tail_gain = np.sqrt(1.0 - t)
    out = x[:-f].copy()
    out[:f] = out[:f] * head_gain + x[-f:] * tail_gain
    return out


def assert_loop_seam(x, name, window_sec=0.03, tolerance_db=3.0):
    """The falsifiable loop test: RMS just before the wrap vs just after must
    agree within tolerance, and the seam jump must look like a normal
    sample-to-sample step, not a click."""
    w = int(window_sec * SR)
    rms_tail = np.sqrt(np.mean(x[-w:] ** 2))
    rms_head = np.sqrt(np.mean(x[:w] ** 2))
    db = abs(20 * np.log10(max(rms_tail, 1e-9) / max(rms_head, 1e-9)))
    assert db < tolerance_db, f"{name}: loop seam RMS discontinuity {db:.2f} dB >= {tolerance_db} dB"
    seam_jump = abs(float(x[0]) - float(x[-1]))
    typical_jump = np.percentile(np.abs(np.diff(x)), 99)
    assert seam_jump <= typical_jump * 4, \
        f"{name}: seam sample jump {seam_jump:.4f} vs typical {typical_jump:.4f} — audible click"


def write_wav(path, x):
    x = np.clip(x, -1, 1)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(struct.pack("<" + "h" * len(x), *(x * 32767).astype(np.int16)))
    print("wrote", path, f"({len(x)/SR:.1f}s)")


def make_bed(name, seed, dur=12.0, base_alpha=0.02, shimmer=0.0, shimmer_rate=6.0,
             am_rate=0.15, am_depth=0.5, tone_hz=0.0, tone_gain=0.0, gain=0.22):
    """General wind-family bed: dark noise base + optional bright shimmer layer
    (leaf rustle) + slow AM (gusting) + optional faint tone (enchanted pad)."""
    rng = np.random.default_rng(seed)
    n = int(dur * SR) + SR  # +1 s trimmed by loop conditioning
    base = lowpass(rng.standard_normal(n), base_alpha)
    base /= max(np.abs(base).max(), 1e-9)
    sig = base
    if shimmer > 0:
        sh = rng.standard_normal(n)
        sh = sh - lowpass(sh, 0.15)                     # highpass-ish
        sh *= slow_am(n, rng, shimmer_rate, 0.8)        # fluttery
        sh /= max(np.abs(sh).max(), 1e-9)
        sig = sig + shimmer * sh
    sig *= slow_am(n, rng, am_rate, am_depth)           # gusts
    if tone_gain > 0:
        t = np.arange(n) / SR
        # two slightly detuned sines — a faint, slowly beating pad
        tone = np.sin(2 * np.pi * tone_hz * t) + np.sin(2 * np.pi * tone_hz * 1.007 * t)
        tone *= slow_am(n, rng, 0.08, 0.6)
        sig = sig + tone_gain * tone * 0.5
    sig /= max(np.abs(sig).max(), 1e-9)
    sig *= gain
    sig = condition_loop(sig)
    assert_loop_seam(sig, name)
    write_wav(os.path.join(OUT_AMB, name), sig)


def make_cave_bed():
    rng = np.random.default_rng(31)
    n = int(12.0 * SR) + SR
    rumble = lowpass(lowpass(rng.standard_normal(n), 0.004), 0.004)
    rumble /= max(np.abs(rumble).max(), 1e-9)
    rumble *= slow_am(n, rng, 0.06, 0.4)
    sig = rumble * 0.18
    sig = condition_loop(sig)
    assert_loop_seam(sig, "cave_rumble.wav")
    write_wav(os.path.join(OUT_AMB, "cave_rumble.wav"), sig)


def make_drip():
    """Cave drip one-shot: a tiny damped high sine 'plink' + resonant tail."""
    rng = np.random.default_rng(41)
    dur = 0.5
    n = int(dur * SR)
    t = np.arange(n) / SR
    f0 = 2400.0 * np.exp(-t / 0.02) + 900.0
    phase = 2 * np.pi * np.cumsum(f0) / SR
    plink = np.sin(phase) * np.exp(-t / 0.012)
    tail = np.sin(2 * np.pi * 610.0 * t) * np.exp(-t / 0.10) * 0.25
    sig = plink + tail + rng.standard_normal(n) * np.exp(-t / 0.004) * 0.05
    sig[: int(0.001 * SR)] *= np.linspace(0, 1, int(0.001 * SR))
    sig /= max(np.abs(sig).max(), 1e-9)
    write_wav(os.path.join(OUT_SFX, "cave_drip.wav"), sig * 0.7)


def make_gust():
    """Soft distant wind gust one-shot (open-biome scatter): 2 s swell."""
    rng = np.random.default_rng(43)
    dur = 2.0
    n = int(dur * SR)
    t = np.arange(n) / SR
    sig = lowpass(rng.standard_normal(n), 0.03)
    sig /= max(np.abs(sig).max(), 1e-9)
    sig *= np.sin(np.pi * t / dur) ** 2   # swell in and out
    write_wav(os.path.join(OUT_SFX, "wind_gust.wav"), sig * 0.35)


if __name__ == "__main__":
    os.makedirs(OUT_AMB, exist_ok=True)
    os.makedirs(OUT_SFX, exist_ok=True)

    # ⚠️ BED GENERATION RETIRED (2026-09-02). The synthesized wind beds failed
    # the ear test — the differentiated-noise shimmer chopped at 7-11 Hz reads
    # as rattling metal, not leaves (user verdict: "100 rattling loose metal
    # fan blades"). Beds now come from real CC0 field recordings via
    # tools/fetch_cc0_beds.py; make_bed()/make_cave_bed() are kept above only
    # as reference DSP, deliberately not invoked so a rerun cannot overwrite
    # the fetched beds. Only the short one-shots (transients synth handles
    # fine) are still generated here.
    make_drip()
    make_gust()
    print("one-shots regenerated (beds are fetched — see fetch_cc0_beds.py)")
