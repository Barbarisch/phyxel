"""Generate a sidecar `<template>.metrics.json` for a .voxel asset.

Usage:
    python tools/characterize_asset.py resources/templates/chair_wood.voxel
    python tools/characterize_asset.py resources/templates/*.voxel       (PowerShell expands the glob)
    python tools/characterize_asset.py --all                              (walks resources/templates/)

The resulting JSON is consumed by tools/interaction_pipeline/asset_metrics.py and
by the engine's compatibility-gate (Phase 2.3). Re-run when the .voxel changes.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

# Allow running as a script from the repo root.
sys.path.insert(0, str(Path(__file__).resolve().parent))

from interaction_pipeline.asset_metrics import (  # type: ignore[import-not-found]
    AssetMetrics,
    characterize_asset,
)

TEMPLATES_DIR = Path(__file__).resolve().parents[1] / "resources" / "templates"


def _write_metrics(metrics: AssetMetrics, sidecar_path: Path) -> None:
    sidecar_path.write_text(
        json.dumps(metrics.to_dict(), indent=2, sort_keys=False) + "\n",
        encoding="utf-8",
    )


def _characterize_one(template_path: Path, *, verbose: bool) -> AssetMetrics:
    metrics = characterize_asset(template_path)
    sidecar = template_path.with_suffix(".metrics.json")
    _write_metrics(metrics, sidecar)
    if verbose:
        print(f"[characterize] {template_path.name} -> {sidecar.name}")
        for p in metrics.interaction_points:
            print(f"  point {p.point_id} ({p.kind}) @ local {p.local_position} yaw={p.facing_yaw}")
            for k, v in p.features.items():
                print(f"    {k}: {v}")
    return metrics


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("templates", nargs="*", type=Path,
                        help="One or more .voxel template paths.")
    parser.add_argument("--all", action="store_true",
                        help="Process every .voxel in resources/templates/.")
    parser.add_argument("--quiet", "-q", action="store_true")
    args = parser.parse_args(argv)

    targets: list[Path]
    if args.all:
        targets = sorted(TEMPLATES_DIR.glob("*.voxel"))
        if not targets:
            print(f"[characterize] no .voxel files found in {TEMPLATES_DIR}", file=sys.stderr)
            return 1
    else:
        if not args.templates:
            parser.error("provide one or more .voxel paths, or pass --all.")
        targets = [p.resolve() for p in args.templates]

    rc = 0
    for t in targets:
        if not t.is_file():
            print(f"[characterize] WARN: {t} does not exist", file=sys.stderr)
            rc = 2
            continue
        try:
            _characterize_one(t, verbose=not args.quiet)
        except Exception as e:  # noqa: BLE001  (best-effort tool)
            print(f"[characterize] ERROR on {t}: {e}", file=sys.stderr)
            rc = 3
    return rc


if __name__ == "__main__":
    sys.exit(main())
