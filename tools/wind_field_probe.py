#!/usr/bin/env python3
"""Measure the grass wind gust field — WITHOUT the engine.

`windGustAt` (shaders/wind.glsl) is a pure function of position and time, so it can be evaluated
exactly on the CPU. That turns questions that are otherwise matters of opinion into numbers:

  1. DOES IT READ AS WAVES?  Wind on a real field appears as bands stretched CROSSWIND and narrow
     ALONG-wind, sweeping downwind. Isotropic noise scrolled in a direction gives travelling BLOBS,
     not waves. Measured as the ratio of correlation lengths:

         anisotropy = (crosswind correlation length) / (along-wind correlation length)

     1.0 = perfectly round blobs. Wave-like fronts want this well above 1.

  2. ARE THE FRONTS STRAIGHT LINES?  A pure anisotropic stretch of smooth noise has near-straight
     iso-contours — the "ruler dragged over the meadow" defect (user, 2026-09-02). The DOMAIN WARP
     (--warp-amp etc.) bends each front's edge; --dump-image renders the field so front shape is
     judged by eye on the CPU before touching the engine, and orientation_concentration below
     quantifies it (angular spread of the gust-band power spectrum; R=1.0 = ruled lines).

  3. IS IT JITTERY?  Sample a fixed point over time; jitter% = energy above 0.5 Hz.
     (The shader's old 4-tap box low-pass is GONE — it removed nothing; do not resurrect it here.)

This file is the CPU MIRROR of shaders/wind.glsl — keep gust_at() in exact sync with windGustAtEx,
including the g*g shaping and all default constants. It drifted once (missing squaring, stale fine
octave 2.3/0.35 vs shipped 1.8/0.15) and measured a field the engine no longer ran.

This is a measurement tool, not a test — it has no pass/fail. Use it to get a baseline before
changing the field, and to show the change did what was intended.

USAGE
  python tools/wind_field_probe.py                          # shipped defaults
  python tools/wind_field_probe.py --aniso 5 --warp-amp 0        # a straighter-front A/B
  python tools/wind_field_probe.py --dump-image field.png   # 512x512 u rendering of the field
"""
import argparse
import math
import numpy as np


# ── exact port of shaders/wind.glsl (windGustAtEx) ────────────────────────────────────────────
def hash21(px, py):
    """Integer avalanche hash of a lattice point — mirrors windHash21 (the old float
    fract-hash's correlated float32 failures were straight lattice-aligned creases)."""
    ux = (np.asarray(px, dtype=np.int64) + 0x8000).astype(np.uint32)
    uy = (np.asarray(py, dtype=np.int64) + 0x8000).astype(np.uint32)
    h = ux * np.uint32(0x9E3779B9) ^ uy * np.uint32(0x85EBCA6B)
    h ^= h >> np.uint32(16)
    h = h * np.uint32(0x7FEB352D)
    h ^= h >> np.uint32(15)
    h = h * np.uint32(0x846CA68B)
    h ^= h >> np.uint32(16)
    return (h & np.uint32(0xFFFFFF)).astype(np.float64) / 16777216.0


def value_noise(px, py):
    """GRADIENT (Perlin-style) noise, quintic fade — mirrors windValueNoise (which kept its
    name when the primitive changed; value noise's lattice creases were the straight lines)."""
    ix, iy = np.floor(px), np.floor(py)
    fx, fy = px - ix, py - iy
    ux = fx * fx * fx * (fx * (fx * 6.0 - 15.0) + 10.0)
    uy = fy * fy * fy * (fy * (fy * 6.0 - 15.0) + 10.0)
    def grad_dot(cx, cy, ox, oy):
        a = hash21(cx, cy) * 6.2831853
        return np.cos(a) * (fx - ox) + np.sin(a) * (fy - oy)
    a = grad_dot(ix,       iy,       0.0, 0.0)
    b = grad_dot(ix + 1.0, iy,       1.0, 0.0)
    c = grad_dot(ix,       iy + 1.0, 0.0, 1.0)
    d = grad_dot(ix + 1.0, iy + 1.0, 1.0, 1.0)
    g = (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uy
    return np.clip(0.5 + 0.70 * g, 0.0, 1.0)


def gust_at(px, pz, t, dirx, dirz, gust_scale, gust_speed,
            fine_freq=1.8, fine_weight=0.15, aniso=2.2,
            warp_amp=0.7, warp_cross_freq=1.3, warp_along_freq=0.35, fine_aniso=2.0,
            broad_freq=0.35, broad_weight=0.40, broad_aniso=2.0):
    """Mirror of windGustAtEx (weights: crest 0.45, fine fine_weight, broad broad_weight)."""
    qx = (px - dirx * (gust_speed * t)) * gust_scale
    qz = (pz - dirz * (gust_speed * t)) * gust_scale
    a = aniso if aniso > 0.01 else 1.0
    # into wind-aligned coords (along, cross/a)
    along = qx * dirx + qz * dirz
    cross = (-qx * dirz + qz * dirx) / a
    # DOMAIN WARP: meander the front line — offset the along-wind coordinate by a noise sampled
    # dominantly along the crosswind coordinate (the front's own length). Both coords already
    # include the scroll, so meanders travel with the field.
    meander = value_noise(cross * warp_cross_freq + 53.1,
                          along * warp_along_freq + 91.7) - 0.5
    along = along + meander * warp_amp
    # coarse octave: rotate back to keep the noise lattice off the wind axes
    cx = along * dirx - cross * dirz
    cz = along * dirz + cross * dirx
    coarse = value_noise(cx, cz)
    # fine octave at its OWN lower anisotropy (roughens edges instead of striping along them)
    fa = fine_aniso if fine_aniso > 0.01 else 1.0
    crossF = cross * (a / fa)
    fx_ = along * dirx - crossF * dirz
    fz_ = along * dirz + crossF * dirx
    fine = value_noise(fx_ * fine_freq + 17.7, fz_ * fine_freq + 17.7)
    # broad swell layer (kWindBroad*): ~3x larger features, low aniso, summed before the shaping
    # so gusts have BODY (wide bent regions) instead of thinning to a crest line
    bx = along * broad_freq
    bz = cross * (a / broad_aniso) * broad_freq
    qbx = bx * dirx - bz * dirz
    qbz = bx * dirz + bz * dirx
    broad = value_noise(qbx + 7.7, qbz + 7.7)
    g = 0.45 * coarse + fine_weight * fine + broad_weight * broad
    return g * g   # shaped: rests near zero, arrives in swells (see wind.glsl)


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


def orientation_concentration(field, res_u, band_lo_u=15.0, band_hi_u=60.0):
    """Front-edge straightness, spectrally. Straight parallel bands concentrate 2D-FFT energy
    along ONE orientation through the origin; wavy/ragged bands spread it angularly. Reported
    as the circular concentration R of the power-spectrum orientation (doubled-angle statistics,
    since orientation is mod pi): R = 1.0 -> perfectly straight ruled bands, lower -> wavier.
    Restricted to the gust-scale wavelength annulus [band_lo_u, band_hi_u] so the fine octave's
    grain does not pollute the front-shape measurement."""
    f = field - field.mean()
    n = f.shape[0]
    w1 = np.hanning(n)
    ps = np.abs(np.fft.fftshift(np.fft.fft2(f * np.outer(w1, w1)))) ** 2
    k = np.fft.fftshift(np.fft.fftfreq(n, d=res_u))
    kx, kz = np.meshgrid(k, k)
    kr = np.sqrt(kx * kx + kz * kz)
    mask = (kr > 1.0 / band_hi_u) & (kr < 1.0 / band_lo_u)
    if not mask.any():
        return float('nan')
    theta = np.arctan2(kz[mask], kx[mask])
    w = ps[mask]
    c = np.sum(w * np.cos(2.0 * theta)) / np.sum(w)
    s = np.sum(w * np.sin(2.0 * theta)) / np.sum(w)
    return float(np.hypot(c, s))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--gust-scale', type=float, default=0.02, help='1/world units (shipped derived default)')
    ap.add_argument('--gust-speed', type=float, default=10.0, help='world units/second (shipped derived default)')
    ap.add_argument('--dir-deg', type=float, default=15.0)
    ap.add_argument('--fine-freq', type=float, default=1.8)
    ap.add_argument('--fine-weight', type=float, default=0.15)
    ap.add_argument('--aniso', type=float, default=2.2, help='>1 stretches fronts crosswind')
    ap.add_argument('--warp-amp', type=float, default=0.7, help='meander amplitude, noise cells (0 = straight fronts)')
    ap.add_argument('--warp-cross-freq', type=float, default=1.3, help='meander frequency along the front')
    ap.add_argument('--warp-along-freq', type=float, default=0.35, help='front-to-front shape variation')
    ap.add_argument('--fine-aniso', type=float, default=2.0, help='fine octave crosswind stretch (< aniso roughens edges)')
    ap.add_argument('--dump-image', type=str, default=None, help='write a grayscale PNG of the field (512x512 u)')
    a = ap.parse_args()

    dx_, dz_ = math.cos(math.radians(a.dir_deg)), math.sin(math.radians(a.dir_deg))
    kw = dict(dirx=dx_, dirz=dz_, gust_scale=a.gust_scale, gust_speed=a.gust_speed,
              fine_freq=a.fine_freq, fine_weight=a.fine_weight, aniso=a.aniso,
              warp_amp=a.warp_amp, warp_cross_freq=a.warp_cross_freq,
              warp_along_freq=a.warp_along_freq, fine_aniso=a.fine_aniso)

    print(f"gustScale {a.gust_scale}  (coarse gust ~{1.0/a.gust_scale:.0f} u, "
          f"fine octave ~{1.0/(a.gust_scale*a.fine_freq):.0f} u)")
    print(f"gustSpeed {a.gust_speed} u/s   dir {a.dir_deg} deg   fineWeight {a.fine_weight}   "
          f"aniso {a.aniso}   warp amp/cf/af {a.warp_amp}/{a.warp_cross_freq}/{a.warp_along_freq}   "
          f"fineAniso {a.fine_aniso}")

    # ── 1. anisotropy: correlation length crosswind vs along-wind ────────────────────────────
    step, n = 0.5, 2048
    d = np.arange(n) * step
    along = gust_at(500 + d * dx_, 500 + d * dz_, 0.0, **kw)
    cross = gust_at(500 - d * dz_, 500 + d * dx_, 0.0, **kw)
    la, lc = correlation_length(along, step), correlation_length(cross, step)
    print(f"\ncorrelation length   along-wind {la:6.1f} u   crosswind {lc:6.1f} u"
          f"   ANISOTROPY {lc/la:5.2f}x")
    print("  (1.0 = round blobs travelling past you; waves want fronts much longer crosswind)")

    # ── 2. front straightness: angular concentration of the gust-band power spectrum ─────────
    res_u, nfft = 1.0, 512
    xs = np.arange(nfft) * res_u + 500.0
    gxf, gzf = np.meshgrid(xs, xs)
    f2d = gust_at(gxf.ravel(), gzf.ravel(), 0.0, **kw).reshape(nfft, nfft)
    oc = orientation_concentration(f2d, res_u)
    print(f"\nfront straightness   R = {oc:.3f}  (orientation concentration, 15-60 u band)")
    print("  (1.0 = perfectly straight parallel bands — the ruled-line defect; lower = wavier)")

    # ── 3. jitter: temporal spectrum at a fixed point ─────────────────────────────────────────
    fs, dur = 60.0, 40.0
    ts = np.arange(int(fs * dur)) / fs
    raw = np.array([float(gust_at(np.array([500.0]), np.array([500.0]), t, **kw)[0]) for t in ts])
    print(f"\nenergy above 0.5 Hz  {band_split(raw, fs)*100:5.1f}%   peak-to-peak {np.ptp(raw):5.2f}")

    # ── 4. does a front actually travel downwind at gustSpeed? ────────────────────────────────
    probe = np.arange(0, 400, 2.0)
    t0, t1 = 0.0, 3.0
    g0 = gust_at(500 + probe * dx_, 500 + probe * dz_, t0, **kw)
    g1 = gust_at(500 + probe * dx_, 500 + probe * dz_, t1, **kw)
    best, bestlag = -2.0, 0
    for lag in range(0, 120):
        if lag >= len(probe):
            break
        # g1(x) == g0(x - travel): the field moves DOWNWIND, so g1 lags g0 in +dir.
        c = np.corrcoef(g0[:len(g0) - lag], g1[lag:])[0, 1] if lag else np.corrcoef(g0, g1)[0, 1]
        if c > best:
            best, bestlag = c, lag
    print(f"\nfront travel over {t1-t0:.0f}s: {bestlag*2.0:5.1f} u "
          f"(expected {a.gust_speed*(t1-t0):.0f} u)   corr {best:.2f}")

    # ── 5. optional field image (front SHAPE, judged by eye) ─────────────────────────────────
    if a.dump_image:
        from PIL import Image
        size_u, res = 512, 1.0
        n = int(size_u / res)
        xs = np.arange(n) * res + 500.0
        gx, gz = np.meshgrid(xs, xs)
        f = gust_at(gx.ravel(), gz.ravel(), 0.0, **kw).reshape(n, n)
        img = (np.clip(f, 0, 1) * 255).astype(np.uint8)
        Image.fromarray(img, 'L').save(a.dump_image)
        print(f"\nfield image ({size_u}x{size_u} u, +X right / +Z down, wind {a.dir_deg} deg): {a.dump_image}")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
