#!/usr/bin/env python3
"""Batch-evaluate UniMate stage-5 GLBs with the importer's dry run and tabulate.

For every ``*.glb`` in a directory: run ``unimate_import.import_clip(dry_run=True)``
against the target rig and collect the per-clip gates (bone match, root-travel vs
stance-feet speed, residual, anim_lint findings). Writes ``<dir>/eval.json`` and prints
a markdown table — the M1 verdict table for docs/UniMateIntegrationPlan.md.

Usage:
    python tools/anim_pipeline/unimate_eval.py <glb_dir> [--captions samples/captions.json]
        [--target resources/animated_characters/humanoid.anim] [--label objaverse]
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import unimate_import as ui  # noqa: E402


def evaluate_dir(glb_dir: Path, target: Path, captions: dict, label: str) -> list[dict]:
    rows = []
    for glb in sorted(glb_dir.glob("*.glb")):
        stem = glb.stem                      # e.g. phyxel_humanoid-walk-rep_0-0
        prompt = captions.get(stem + ".npy", "")
        parts = stem.split("-")
        tag = parts[1] if len(parts) > 1 else stem
        rep = next((p.replace("rep_", "") for p in parts if p.startswith("rep_")), "0")
        name = f"unimate_{tag}_r{rep}"
        try:
            r = ui.import_clip(glb, target, name, prompt=prompt, dry_run=True)
            st = r.get("stance_estimate", {})
            rows.append({
                "label": label, "clip": name, "prompt": prompt, "glb": glb.name,
                "duration_s": r["duration_s"], "match": r["match_ratio"],
                "root_speed": (r["root_travel"] or {}).get("speed"),
                "feet_speed": st.get("est_speed"), "residual": st.get("residual"),
                "stance_samples": st.get("stance_samples"),
                "root_vs_feet": r.get("root_vs_feet_mismatch"),
                "speed": r["speed"], "speed_source": r["speed_source"],
                "lint_errors": r["lint_errors"],
                "lint_warns": sum(1 for f in r["lint"] if f["severity"] == "WARN"),
                "lint": r["lint"],
            })
        except ui.ImportRefused as exc:
            rows.append({"label": label, "clip": name, "prompt": prompt, "glb": glb.name,
                         "refused": str(exc)})
    return rows


def fmt(v, nd=3):
    if v is None:
        return "-"
    if isinstance(v, float):
        return f"{v:.{nd}f}"
    return str(v)


def markdown(rows: list[dict]) -> str:
    head = ("| clip | prompt | dur s | match | root u/s | feet u/s | root/feet off | residual | "
            "Speed (src) | lint E/W |\n|---|---|---|---|---|---|---|---|---|---|")
    lines = [head]
    for r in rows:
        if "refused" in r:
            lines.append(f"| {r['clip']} | {r['prompt']} | REFUSED: {r['refused']} |||||||")
            continue
        off = f"{r['root_vs_feet']:.0%}" if r.get("root_vs_feet") is not None else "-"
        lines.append(
            f"| {r['clip']} | {r['prompt']} | {fmt(r['duration_s'], 2)} | {r['match']:.0%} | "
            f"{fmt(r['root_speed'])} | {fmt(r['feet_speed'])} | {off} | {fmt(r['residual'])} | "
            f"{fmt(r['speed'])} ({r['speed_source']}) | {r['lint_errors']}/{r['lint_warns']} |")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("glb_dir", type=Path)
    ap.add_argument("--captions", type=Path, help="captions.json from the sampler")
    ap.add_argument("--target", type=Path, default=ui.DEFAULT_TARGET)
    ap.add_argument("--label", default="")
    ap.add_argument("--out", type=Path, help="JSON output (default <glb_dir>/eval.json)")
    a = ap.parse_args()
    captions = json.loads(a.captions.read_text(encoding="utf-8")) if a.captions else {}
    rows = evaluate_dir(a.glb_dir, a.target, captions, a.label)
    out = a.out or a.glb_dir / "eval.json"
    out.write_text(json.dumps(rows, indent=2), encoding="utf-8")
    print(markdown(rows))
    print(f"\n{len(rows)} clips -> {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
