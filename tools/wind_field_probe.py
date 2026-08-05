#!/usr/bin/env python3
"""Measure the grass wind gust field — WITHOUT the engine.

`windGustAt` (shaders/wind.glsl) is a pure function of position and time, so it can be evaluated
exactly on the CPU. That turns two questions that are otherwise matters of opinion into numbers:

  1. DOES IT READ AS WAVES?  Wind on a real field appears as bands stretched CROSSWIND and narrow
     ALONG-wind, sweeping downwind. Isotropic noise scrolled in a direction gives travelling BLOBS,
     not waves. Measured as the ratio of correlation lengths:

         anisotropy = (crosswind correlation length) / (along-wind correlation length)

     1.0 = perfectly round blobs. Wave-like fronts want this well above 1.

  2. IS IT JITTERY?  Sample a fixed point over time and split the signal's energy into a LOW band
     (slow swells — the wanted motion) and a HIGH band (fast flicker — the jitter). Reported as

         jitter% = high-band energy / total energy

     A field that reads as smooth swells has most of its energy below ~0.5 Hz.

Both are reported for the RAW field and for the field after the shader's 4-tap box low-pass, so the
filter's actual contribution is visible rather than assumed.

This is a measurement tool, not a test — it has no pass/fail. Use it to get a baseline before
changing the field, and to show the change did what was intended.

USAGE
  python tools/wind_field_probe.py                        # shipped defaults
  python tools/wind_field_probe.py --gust-scale 0.02 --fine-weight 0.15
"""
import argparse
import math
import numpy as np


# ── exact port of shaders/wind.glsl ───────────────────────────────────────────────────────────
def hash21(px, py):
    px = np.modf(px * 127.1)[0]
    py = np.modf(py * 311.7)[0]
    px = np.where(px < 0, px + 1.0, px)
    py = np.where(py < 0, py + 1.0, py)
    d = px * (px + 34.23) + py * (py + 34.23)
    px, py = px + d, py + d
    r = np.modf(px * py)[0]
    return np.where(r < 0, r + 1.0, r)


def value_noise(px, py):
    ix, iy = np.floor(px), np.floor(py)
    fx, fy = px - ix, py - iy
    ux = fx * fx * (3.0 - 2.0 * fx)
    uy = fy * fy * (3.0 - 2.0 * fy)
    a = hash21(ix, iy)
    b = hash21(ix + 1.0, iy)
    c = hash21(ix, iy + 1.0)
    d = hash21(ix + 1.0, iy + 1.0)
    return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uy


def gust_at(px, pz, t, dirx, dirz, gust_scale, gust_speed,
            fine_freq=2.3, fine_weight=0.35, aniso=1.0):
    """aniso > 1 stretches the field CROSSWIND (proposed change; 1.0 = today's isotropic field)."""
    qx = (px - dirx * (gust_speed * t)) * gust_scale
    qz = (pz - dirz * (gust_speed * t)) * gust_scale
    if aniso != 1.0:
        # into wind-aligned coords, squash crosswind (longer correlation), then back
        along = qx * dirx + qz * dirz
        cross = -qx * dirz + qz * dirx
        cross /= aniso
        qx = along * dirx - cross * dirz
        qz = along * dirz + cross * dirx
    coarse = value_noise(qx, qz)
    fine = value_noise(qx * fine_freq + 17.7, qz * fine_freq + 17.7)
    return coarse * (1.0 - fine_weight) + fine * fine_weight


# ── metrics ───────────────────────────────────────────────────────────────────────────────────
def correlation_length(sig, step):
    """Lag (world units) at which autocorrelation first falls below 0.5."""
    s = sig - sig.mean()
    denom = np.sum(s * s)
    if denom <= 0:
        return float('nan')
    for k in range(1, len(s) // 2):
        if np.sum(s[:-k] * s[k:]) / denom < 0.5:
            return k * step
    return (len(s) // 2) * step


def band_split(sig, fs, cut=0.5):
    """Fraction of signal energy above `cut` Hz — the jitter proxy."""
    s = sig - sig.mean()
    spec = np.abs(np.fft.rfft(s)) ** 2
    freq = np.fft.rfftfreq(len(s), 1.0 / fs)
    tot = spec[1:].sum()
    return float(spec[1:][freq[1:] > cut].sum() / tot) if tot > 0 else 0.0


def box4(fn, t, spacing=0.33, n=4):
    """The shader's 4-tap ~1s box low-pass."""
    return sum(fn(t - i * spacing) for i in range(n)) / n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--gust-scale', type=float, default=0.044, help='1/world units (from /api/debug/wind)')
    ap.add_argument('--gust-speed', type=float, default=7.0, help='world units/second')
    ap.add_argument('--dir-deg', type=float, default=25.0)
    ap.add_argument('--fine-freq', type=float, default=2.3)
    ap.add_argument('--fine-weight', type=float, default=0.35)
    ap.add_argument('--aniso', type=float, default=1.0, help='>1 stretches fronts crosswind')
    a = ap.parse_args()

    dx, dz = math.cos(math.radians(a.dir_deg)), math.sin(math.radians(a.dir_deg))
    kw = dict(dirx=dx, dirz=dz, gust_scale=a.gust_scale, gust_speed=a.gust_speed,
              fine_freq=a.fine_freq, fine_weight=a.fine_weight, aniso=a.aniso)

    print(f"gustScale {a.gust_scale}  (coarse gust ~{1.0/a.gust_scale:.0f} u, "
          f"fine octave ~{1.0/(a.gust_scale*a.fine_freq):.0f} u)")
    print(f"gustSpeed {a.gust_speed} u/s   dir {a.dir_deg} deg   "
          f"fineWeight {a.fine_weight}   aniso {a.aniso}")

    # ── 1. anisotropy: correlation length crosswind vs along-wind ────────────────────────────
    step, n = 0.5, 2048
    d = np.arange(n) * step
    along = gust_at(500 + d * dx, 500 + d * dz, 0.0, **kw)
    cross = gust_at(500 - d * dz, 500 + d * dx, 0.0, **kw)
    la, lc = correlation_length(along, step), correlation_length(cross, step)
    print(f"\ncorrelation length   along-wind {la:6.1f} u   crosswind {lc:6.1f} u"
          f"   ANISOTROPY {lc/la:5.2f}x")
    print("  (1.0 = round blobs travelling past you; waves want fronts much longer crosswind)")

    # ── 2. jitter: temporal spectrum at a fixed point ─────────────────────────────────────────
    fs, dur = 60.0, 40.0
    ts = np.arange(int(fs * dur)) / fs
    at = lambda t: float(gust_at(np.array([500.0]), np.array([500.0]), t, **kw)[0])
    raw = np.array([at(t) for t in ts])
    filt = np.array([box4(at, t) for t in ts])
    print(f"\nenergy above 0.5 Hz  raw {band_split(raw, fs)*100:5.1f}%"
          f"   after 4-tap box {band_split(filt, fs)*100:5.1f}%")
    print(f"peak-to-peak         raw {np.ptp(raw):5.2f}   after {np.ptp(filt):5.2f}"
          f"   (the filter also EATS AMPLITUDE — swells get weaker, not just smoother)")

    # ── 3. does a front actually travel downwind at gustSpeed? ────────────────────────────────
    probe = np.arange(0, 400, 2.0)
    t0, t1 = 0.0, 3.0
    g0 = gust_at(500 + probe * dx, 500 + probe * dz, t0, **kw)
    g1 = gust_at(500 + probe * dx, 500 + probe * dz, t1, **kw)
    best, bestlag = -2.0, 0
    for lag in range(0, 120):
        if lag >= len(probe):
            break
        # g1(x) == g0(x - travel): the field moves DOWNWIND, so g1 lags g0 in +dir.
        # Comparing g0[lag:] against g1[:-lag] tests the opposite sign and finds spurious matches.
        c = np.corrcoef(g0[:len(g0) - lag], g1[lag:])[0, 1] if lag else np.corrcoef(g0, g1)[0, 1]
        if c > best:
            best, bestlag = c, lag
    print(f"\nfront travel over {t1-t0:.0f}s: {bestlag*2.0:5.1f} u "
          f"(expected {a.gust_speed*(t1-t0):.0f} u)   corr {best:.2f}")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
