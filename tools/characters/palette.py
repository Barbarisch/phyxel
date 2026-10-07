"""palette.py — per-voxel colours -> a small deterministic palette (roadmap R2 decisions 2 + 10).

A palette keeps the voxel look; raw texels speckle. k-means in CIE Lab with NO randomness: the
initial centres are the most frequent distinct colours (ties broken by Lab value), so the same
input always gives the same palette and the same assignment.
"""
from __future__ import annotations

import numpy as np


def _srgb_to_lab(rgb: np.ndarray) -> np.ndarray:
    c = rgb.astype(np.float64) / 255.0
    c = np.where(c > 0.04045, ((c + 0.055) / 1.055) ** 2.4, c / 12.92)
    m = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    xyz = c @ m.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[:, 1] - 16, 500 * (f[:, 0] - f[:, 1]), 200 * (f[:, 1] - f[:, 2])], axis=1)


def quantize(colors: np.ndarray, max_colors: int = 24, iters: int = 20):
    """Return (palette uint8 (K,3), index (N,)) with K <= max_colors."""
    colors = np.asarray(colors, dtype=np.uint8)
    uniq, inverse, counts = np.unique(colors, axis=0, return_inverse=True, return_counts=True)
    inverse = inverse.reshape(-1)
    if len(uniq) <= max_colors:
        return uniq, inverse
    lab_u = _srgb_to_lab(uniq)
    order = np.lexsort((lab_u[:, 2], lab_u[:, 1], lab_u[:, 0], -counts))   # frequency first
    centers = lab_u[order[:max_colors]].copy()
    w = counts.astype(np.float64)
    for _ in range(iters):
        d = ((lab_u[:, None, :] - centers[None, :, :]) ** 2).sum(axis=2)
        assign = d.argmin(axis=1)
        new = centers.copy()
        for k in range(len(centers)):
            sel = assign == k
            if sel.any():
                new[k] = (lab_u[sel] * w[sel, None]).sum(axis=0) / w[sel].sum()
        if np.allclose(new, centers):
            break
        centers = new
    d = ((lab_u[:, None, :] - centers[None, :, :]) ** 2).sum(axis=2)
    assign = d.argmin(axis=1)
    # palette entries are the count-weighted mean RGB of their members (stays in the source gamut)
    pal = np.zeros((len(centers), 3), dtype=np.uint8)
    for k in range(len(centers)):
        sel = assign == k
        if sel.any():
            pal[k] = np.round((uniq[sel].astype(np.float64) * w[sel, None]).sum(axis=0) / w[sel].sum())
    used = np.unique(assign)
    remap = {int(k): i for i, k in enumerate(used)}
    return pal[used], np.array([remap[int(assign[i])] for i in inverse], dtype=np.int64)
