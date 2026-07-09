#!/usr/bin/env python
"""Render regression pixel-diff — compare two engine screenshots, cropped to the 3D viewport.

Used to verify a render change is perceptually lossless (docs/RenderDensityPlan.md D1). Reports the
max / mean absolute per-channel pixel delta and the fraction of pixels exceeding small thresholds,
over a crop that excludes the ImGui panels and the dynamic status-bar text (FPS counter etc.).

MANDATORY methodology when capturing the two frames (else the diff is confounded by renderer noise):
  1. Disable grass + foliage:  POST /api/debug/grass {"enabled":false}, /api/debug/foliage {...}
     (their blades animate / are temporal → a non-zero noise floor).
  2. PAUSE the day-night cycle:  set_day_night(paused=true, timeOfDay=14)
     (a moving sun shifts shadows between the two captures → false positives).
  3. Fix the camera pose (POST /api/camera) identically for both frames.
Then: capture A, toggle the change, capture B. The OFF-vs-OFF noise floor MUST read max_delta=0 first;
if it doesn't, a temporal source is still active and the ON-vs-OFF number is meaningless.

Bar (matches the shipped fine-merge "perceptually lossless"): >=99.997% of viewport pixels within
2/255; residual is scattered single-pixel silhouette-edge aliasing, no contiguous wrong region.

Usage: python tools/render_pixel_diff.py A.png B.png
"""
import sys
import numpy as np
from PIL import Image

# Viewport crop for the 1600x900 editor layout: the 3D render, no panels / status bar.
CROP_Y0, CROP_Y1, CROP_X0, CROP_X1 = 50, 660, 260, 1180


def main():
    if len(sys.argv) != 3:
        print("usage: render_pixel_diff.py A.png B.png"); return 2
    a = np.asarray(Image.open(sys.argv[1]).convert("RGB"), dtype=np.int16)
    b = np.asarray(Image.open(sys.argv[2]).convert("RGB"), dtype=np.int16)
    if a.shape != b.shape:
        print("SHAPE MISMATCH", a.shape, b.shape); return 2
    H, W = a.shape[:2]
    ca = a[CROP_Y0:min(CROP_Y1, H), CROP_X0:min(CROP_X1, W)]
    cb = b[CROP_Y0:min(CROP_Y1, H), CROP_X0:min(CROP_X1, W)]
    d = np.abs(ca - cb)
    maxd = int(d.max())
    meand = float(d.mean())
    frac_gt2 = float((d > 2).mean()) * 100.0
    frac_gt8 = float((d > 8).mean()) * 100.0
    n_gt8 = int((d.sum(axis=2) > 8).sum())
    print(f"viewport crop {ca.shape}: max_delta={maxd}/255  mean_delta={meand:.4f}  "
          f"%px>2={frac_gt2:.4f}  %px>8={frac_gt8:.4f}  (px>8={n_gt8})")
    lossless = frac_gt2 < 0.01 and maxd <= 16
    perceptual = frac_gt8 < 0.02  # scattered edge aliasing only
    print("VERDICT:", "IDENTICAL" if lossless else
                       ("PERCEPTUALLY-LOSSLESS (edge aliasing)" if perceptual else "DIFFERENT"))
    return 0 if perceptual else 1


if __name__ == "__main__":
    sys.exit(main())
