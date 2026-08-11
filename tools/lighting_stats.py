#!/usr/bin/env python3
"""lighting_stats.py — deterministic luminance statistics for a rendered frame.

WHY THIS EXISTS. Lighting work is the easiest thing in the engine to fool yourself about: two
screenshots taken minutes apart "look brighter" and that is the whole evidence base. The standing
rule is to verify geometry and now light DETERMINISTICALLY rather than by eye
(docs feedback: deterministic-checks-not-visual). This reads a saved screenshot and reports numbers.

It needs no engine change — screenshots are already written as PNGs, so the readout lives entirely
on this side.

THE METRICS, and what each one is for:

  clipped        Fraction of pixels at or above 254/255 in all three channels. This is THE metric
                 for the "over-bright and flat" complaint. The scene renders to a linear HDR target
                 and (before the tonemap work) went to the display with no curve at all, so every
                 value over 1.0 collapsed onto the same white. Clipped pixels carry no shading
                 information — a high fraction IS the flatness, measured. A filmic tonemap should
                 drive this toward zero without darkening the midtones.

  crushed        Fraction of pixels at or below 1/255 in all three channels. The other end of the
                 same failure: detail lost to black. Watch this when making interiors darker, so
                 "dark" does not silently become "empty".

  p05/p50/p95    Luminance percentiles. p50 is the exposure anchor; p95-p05 is the usable dynamic
                 range actually reaching the display. A revamp that raises p95-p05 while holding p50
                 has added contrast without changing overall exposure — which is the goal.

  contrast       p95 / max(p05, eps). A single scalar for "how far apart are lit and shadowed".

  regions        Per-region means. This is how "are rooms over-bright relative to outdoors" becomes
                 a ratio instead of an opinion: define an interior rect and an exterior rect for a
                 fixed pose and track interior/exterior across the work.

Luminance is Rec.709 on values decoded to LINEAR light by default (--gamma srgb), because a mean
taken on sRGB-encoded bytes is not a mean of light and will mislead you about ratios. Pass
--gamma none to stay in encoded space when you specifically want display-referred numbers.

USAGE
  python tools/lighting_stats.py shot.png
  python tools/lighting_stats.py shot.png --regions regions.json
  python tools/lighting_stats.py before.png --compare after.png
  python tools/lighting_stats.py shot.png --json          # machine-readable, for evidence files

REGIONS FILE (fractions of width/height, so it survives a resolution change):
  { "interior": [0.30, 0.40, 0.60, 0.75], "sky": [0.0, 0.0, 1.0, 0.15] }
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

# Rec.709 luminance weights, applied to LINEAR light.
_LUMA = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)

# A pixel counts as clipped/crushed only when EVERY channel is pinned. A saturated red highlight is
# blown in one channel but still carries shading in the others, and calling it "clipped" would
# overstate the problem.
_CLIP_BYTE = 254
_CRUSH_BYTE = 1


def srgb_to_linear(x: np.ndarray) -> np.ndarray:
    """Standard sRGB EOTF. x in [0,1] encoded -> [0,1] linear."""
    return np.where(x <= 0.04045, x / 12.92, ((x + 0.055) / 1.055) ** 2.4)


def load_rgb(path: Path) -> np.ndarray:
    """Load as uint8 RGB, dropping alpha (screenshots are opaque)."""
    with Image.open(path) as im:
        return np.asarray(im.convert("RGB"), dtype=np.uint8)


def luminance(rgb8: np.ndarray, gamma: str) -> np.ndarray:
    f = rgb8.astype(np.float64) / 255.0
    if gamma == "srgb":
        f = srgb_to_linear(f)
    return f @ _LUMA


def crop_fraction(arr: np.ndarray, rect: list[float]) -> np.ndarray:
    """rect = [x0, y0, x1, y1] as fractions of width/height."""
    h, w = arr.shape[:2]
    x0, y0, x1, y1 = rect
    cx0, cx1 = int(round(x0 * w)), int(round(x1 * w))
    cy0, cy1 = int(round(y0 * h)), int(round(y1 * h))
    cx0, cy0 = max(0, cx0), max(0, cy0)
    cx1, cy1 = min(w, max(cx1, cx0 + 1)), min(h, max(cy1, cy0 + 1))
    return arr[cy0:cy1, cx0:cx1]


def analyse(path: Path, regions: dict[str, list[float]], gamma: str) -> dict:
    rgb8 = load_rgb(path)
    lum = luminance(rgb8, gamma)

    clipped = float(np.mean(np.all(rgb8 >= _CLIP_BYTE, axis=2)))
    crushed = float(np.mean(np.all(rgb8 <= _CRUSH_BYTE, axis=2)))
    p05, p50, p95 = (float(v) for v in np.percentile(lum, [5, 50, 95]))

    out = {
        "image": str(path),
        "size": [int(rgb8.shape[1]), int(rgb8.shape[0])],
        "gamma": gamma,
        "clipped": clipped,
        "crushed": crushed,
        "p05": p05,
        "p50": p50,
        "p95": p95,
        "mean": float(np.mean(lum)),
        "contrast": p95 / max(p05, 1e-6),
        "regions": {},
    }
    for name, rect in regions.items():
        sub8 = crop_fraction(rgb8, rect)
        subl = luminance(sub8, gamma)
        out["regions"][name] = {
            "rect": rect,
            "mean": float(np.mean(subl)),
            "p50": float(np.median(subl)),
            "clipped": float(np.mean(np.all(sub8 >= _CLIP_BYTE, axis=2))),
            "crushed": float(np.mean(np.all(sub8 <= _CRUSH_BYTE, axis=2))),
        }
    return out


def _fmt_pct(v: float) -> str:
    return f"{v * 100:6.2f}%"


def report(st: dict, file=sys.stdout) -> None:
    print(f"{st['image']}  ({st['size'][0]}x{st['size'][1]}, luminance in "
          f"{'LINEAR' if st['gamma'] == 'srgb' else 'ENCODED'} light)", file=file)
    print(f"  clipped (all channels >= {_CLIP_BYTE}) : {_fmt_pct(st['clipped'])}"
          "   <- flatness: these pixels carry no shading", file=file)
    print(f"  crushed (all channels <= {_CRUSH_BYTE})   : {_fmt_pct(st['crushed'])}", file=file)
    print(f"  luminance p05 / p50 / p95      : {st['p05']:.4f} / {st['p50']:.4f} / {st['p95']:.4f}",
          file=file)
    print(f"  contrast (p95/p05)             : {st['contrast']:.2f}", file=file)
    if st["regions"]:
        print("  regions:", file=file)
        for name, r in st["regions"].items():
            print(f"    {name:<14} mean {r['mean']:.4f}  p50 {r['p50']:.4f}  "
                  f"clipped {_fmt_pct(r['clipped'])}", file=file)


def report_compare(a: dict, b: dict, file=sys.stdout) -> None:
    print(f"BEFORE {a['image']}\nAFTER  {b['image']}\n", file=file)
    rows = [
        ("clipped", a["clipped"], b["clipped"], True),
        ("crushed", a["crushed"], b["crushed"], True),
        ("p05", a["p05"], b["p05"], False),
        ("p50", a["p50"], b["p50"], False),
        ("p95", a["p95"], b["p95"], False),
        ("contrast", a["contrast"], b["contrast"], False),
    ]
    print(f"  {'metric':<12} {'before':>10} {'after':>10} {'delta':>10}", file=file)
    for name, av, bv, pct in rows:
        if pct:
            print(f"  {name:<12} {av * 100:9.2f}% {bv * 100:9.2f}% {(bv - av) * 100:+9.2f}pp",
                  file=file)
        else:
            print(f"  {name:<12} {av:10.4f} {bv:10.4f} {bv - av:+10.4f}", file=file)
    shared = [k for k in a["regions"] if k in b["regions"]]
    if shared:
        print("\n  region means:", file=file)
        for k in shared:
            av, bv = a["regions"][k]["mean"], b["regions"][k]["mean"]
            print(f"    {k:<14} {av:10.4f} {bv:10.4f} {bv - av:+10.4f}", file=file)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", type=Path)
    ap.add_argument("--compare", type=Path, help="second image; prints a before/after delta table")
    ap.add_argument("--regions", type=Path,
                    help="JSON map of name -> [x0,y0,x1,y1] as fractions of width/height")
    ap.add_argument("--gamma", choices=["srgb", "none"], default="srgb",
                    help="decode to linear light before averaging (default srgb)")
    ap.add_argument("--json", action="store_true", help="emit JSON instead of a text report")
    args = ap.parse_args(argv)

    regions: dict[str, list[float]] = {}
    if args.regions:
        regions = json.loads(args.regions.read_text(encoding="utf-8"))

    a = analyse(args.image, regions, args.gamma)
    if args.compare:
        b = analyse(args.compare, regions, args.gamma)
        if args.json:
            json.dump({"before": a, "after": b}, sys.stdout, indent=2)
            print()
        else:
            report_compare(a, b)
        return 0

    if args.json:
        json.dump(a, sys.stdout, indent=2)
        print()
    else:
        report(a)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
