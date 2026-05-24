"""Thorough tune-all orchestrator.

For every asset that has interaction points declared in its `.metrics.json`
sidecar, this script:

  1. Runs the character × asset MATRIX with `apply_overrides=True` to seed
     base + per-morphology overrides for every preset (standard, giant,
     dwarf, child). Persists via `POST /api/interaction/profile`.

  2. For interaction kinds that have a full tuner (currently only "sit"),
     refines each morphology with `cli.run_pipeline(morphology=...)`:
         a. Spawns the morphology in IE mode
         b. Sweeps + detects + tunes for up to N iterations
         c. Writes the converged deltas back as a per-character override
            (or as the base profile when morphology == "standard")

  3. Tracks state in `tools/interaction_pipeline/tune_all_state.json`
     so re-runs skip cells already completed (unless --force).

  4. Emits a summary report at the end listing converged / failed / skipped
     cells and any assets missing interaction-point annotations.

Usage:
    python -m tools.tune_all_interactions
    python -m tools.tune_all_interactions --only chair_wood door_wood
    python -m tools.tune_all_interactions --force
    python -m tools.tune_all_interactions --no-refine    # matrix only
"""
from __future__ import annotations

import argparse
import datetime as _dt
import json
import sys
import traceback
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.interaction_pipeline.cli import run_pipeline  # noqa: E402
from tools.interaction_pipeline.matrix import run_matrix, write_matrix_result  # noqa: E402
from tools.interaction_pipeline.engine_lifecycle import EngineSession, Mode, PROJECT_ROOT  # noqa: E402

TEMPLATES_DIR = ROOT / "resources" / "templates"
REPORTS_DIR = ROOT / "tools" / "interaction_pipeline" / "reports" / "tune_all"
STATE_PATH = ROOT / "tools" / "interaction_pipeline" / "tune_all_state.json"

# Map asset-point `kind` → tuner kind id.
POINT_KIND_TO_INTERACTION = {
    "seat":         "sit",
    "door_handle":  "door_open",
    "door":         "door_open",
    "lid":          "container_open",
    "drawer":       "container_open",
    "container":    "container_open",
    "grasp":        "pickup",
    "handle":       "pickup",
}

# Which interaction kinds have a full sweep+detect+tune pipeline (refine step).
# Anything not listed gets matrix-only coverage.
TUNABLE_KINDS = {"sit"}

MORPHOLOGIES = ("standard", "giant", "dwarf", "child")


# ---------------------------------------------------------------------------
# State persistence
# ---------------------------------------------------------------------------

def load_state() -> dict[str, Any]:
    if STATE_PATH.exists():
        try:
            return json.loads(STATE_PATH.read_text(encoding="utf-8"))
        except Exception:
            pass
    return {"version": 1, "cells": {}}


def save_state(state: dict[str, Any]) -> None:
    STATE_PATH.parent.mkdir(parents=True, exist_ok=True)
    STATE_PATH.write_text(json.dumps(state, indent=2), encoding="utf-8")


def cell_key(template: str, point_id: str, kind: str, morphology: str | None = None) -> str:
    return f"{template}/{point_id}/{kind}" + (f"/{morphology}" if morphology else "")


# ---------------------------------------------------------------------------
# Discovery
# ---------------------------------------------------------------------------

def discover_cells() -> tuple[list[dict[str, Any]], list[str]]:
    """Return (cells, missing_annotations).

    cells is a flat list of dicts:
        {asset_path, template, point_id, point_kind, interaction_kind, features}

    missing_annotations is a list of asset stems that have a sidecar but no
    interaction_points — these are candidates for `characterize_asset`.
    """
    cells: list[dict[str, Any]] = []
    missing: list[str] = []
    for sidecar in sorted(TEMPLATES_DIR.glob("*.metrics.json")):
        data = json.loads(sidecar.read_text(encoding="utf-8"))
        points = data.get("interaction_points") or []
        asset_path = sidecar.with_suffix("").with_suffix(".voxel")
        if not asset_path.exists():
            continue
        if not points:
            missing.append(data.get("template_name", sidecar.stem))
            continue
        for p in points:
            pkind = (p.get("kind") or "").lower()
            ikind = POINT_KIND_TO_INTERACTION.get(pkind)
            if not ikind:
                missing.append(f"{data['template_name']} ({pkind}: no kind handler)")
                continue
            cells.append({
                "asset_path": asset_path,
                "template": data["template_name"],
                "point_id": p["point_id"],
                "point_kind": pkind,
                "interaction_kind": ikind,
            })
    return cells, missing


# ---------------------------------------------------------------------------
# Cell processing
# ---------------------------------------------------------------------------

def process_cell(
    cell: dict[str, Any],
    state: dict[str, Any],
    *,
    refine: bool,
    max_iterations: int,
    samples: int,
    force: bool,
    verbose: bool,
) -> dict[str, Any]:
    """Run matrix + (optionally) per-morphology refine for one cell."""
    template = cell["template"]
    point_id = cell["point_id"]
    ikind = cell["interaction_kind"]
    asset_path = cell["asset_path"]
    result: dict[str, Any] = {
        "template": template,
        "point_id": point_id,
        "kind": ikind,
        "asset_path": str(asset_path),
        "matrix": None,
        "refine": {},  # morphology -> {converged, iterations, error}
        "started_at": _dt.datetime.now().isoformat(timespec="seconds"),
    }

    matrix_key = cell_key(template, point_id, ikind)
    matrix_done = state["cells"].get(matrix_key, {}).get("matrix_ok") is True

    # Open ONE engine session per asset and share it across the matrix +
    # every refine iteration / morphology. This avoids the start/stop churn
    # that previously launched the engine dozens of times per asset.
    print(f"[engine-up] {template} ({asset_path.name})")
    session = EngineSession(
        Mode.INTERACTION_EDITOR,
        target=str(asset_path),
        on_crash="abort",
        verbose=verbose,
    )
    session.__enter__()
    try:
        # -------- Step 1: matrix --------
        if matrix_done and not force:
            print(f"[skip-matrix] {matrix_key} already complete")
            result["matrix"] = {"skipped": True}
        else:
            print(f"[matrix] {matrix_key}")
            try:
                mr = run_matrix(
                    asset_path,
                    kind=ikind,
                    character_ids=MORPHOLOGIES,
                    point_id=point_id,
                    apply_overrides=True,
                    capture_screenshots=False,
                    session=session,
                    verbose=verbose,
                )
                out = REPORTS_DIR / template / f"matrix_{point_id}_{ikind}.json"
                write_matrix_result(mr, out)
                result["matrix"] = {
                    "ok": True,
                    "result_path": str(out),
                    "cells": [{"cid": c.character_id, "can_interact": c.can_interact,
                               "issues": len(c.compatibility_issues),
                               "notes": c.notes} for c in mr.cells],
                }
                state["cells"].setdefault(matrix_key, {})["matrix_ok"] = True
                save_state(state)
            except Exception as e:
                print(f"[matrix-FAIL] {matrix_key}: {e!r}")
                traceback.print_exc()
                result["matrix"] = {"ok": False, "error": repr(e)}
                return result

        # -------- Step 2: refine per morphology --------
        if not refine or ikind not in TUNABLE_KINDS:
            if ikind not in TUNABLE_KINDS:
                result["refine_note"] = f"kind '{ikind}' has matrix-only support"
            return result

        for morph in MORPHOLOGIES:
            m_key = cell_key(template, point_id, ikind, morph)
            m_state = state["cells"].get(m_key, {})
            if m_state.get("refine_ok") and not force:
                print(f"[skip-refine] {m_key}")
                result["refine"][morph] = {"skipped": True}
                continue

            print(f"[refine] {m_key}")
            try:
                pr = run_pipeline(
                    asset_path,
                    point_id=point_id,
                    max_iterations=max_iterations,
                    samples_per_clip=samples,
                    take_screenshots=True,
                    tuner_backend="heuristic",
                    apply_profile_deltas=True,
                    morphology=(None if morph == "standard" else morph),
                    template_name_override=template,
                    session=session,
                    verbose=verbose,
                )
                result["refine"][morph] = {
                    "ok": True,
                    "converged": pr.converged,
                    "iterations": len(pr.iterations),
                    "notes": pr.notes,
                }
                state["cells"][m_key] = {"refine_ok": True, "converged": pr.converged}
                save_state(state)
            except Exception as e:
                print(f"[refine-FAIL] {m_key}: {e!r}")
                traceback.print_exc()
                result["refine"][morph] = {"ok": False, "error": repr(e)}
    finally:
        print(f"[engine-down] {template}")
        try:
            session.__exit__(None, None, None)
        except Exception:
            pass

    return result


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    p.add_argument("--only", nargs="*", default=None,
                   help="Restrict to these template names")
    p.add_argument("--skip", nargs="*", default=None,
                   help="Skip these template names")
    p.add_argument("--no-refine", action="store_true",
                   help="Matrix only — skip the iterative refinement step")
    p.add_argument("--max-iterations", type=int, default=3,
                   help="Iterations per morphology in the refine step")
    p.add_argument("--samples", type=int, default=8,
                   help="Sweep samples per clip during refinement")
    p.add_argument("--force", action="store_true",
                   help="Ignore resume state and re-do every cell")
    p.add_argument("--quiet", action="store_true")
    args = p.parse_args(argv)

    REPORTS_DIR.mkdir(parents=True, exist_ok=True)
    state = load_state() if not args.force else {"version": 1, "cells": {}}

    cells, missing = discover_cells()
    if args.only:
        cells = [c for c in cells if c["template"] in args.only]
    if args.skip:
        cells = [c for c in cells if c["template"] not in args.skip]

    print(f"=== tune-all: {len(cells)} cells to process ===")
    for c in cells:
        print(f"  - {c['template']}/{c['point_id']} ({c['interaction_kind']})")
    if missing:
        print(f"\n=== NOT TUNABLE ({len(missing)} assets lack interaction points) ===")
        for m in missing:
            print(f"  - {m}")
        print("  (Run `python tools/characterize_asset.py <asset>.voxel`"
              " after adding `# interaction_point:` headers to enable.)")

    results: list[dict[str, Any]] = []
    for i, cell in enumerate(cells, 1):
        print(f"\n========== [{i}/{len(cells)}] {cell['template']}/{cell['point_id']} ==========")
        r = process_cell(
            cell,
            state,
            refine=not args.no_refine,
            max_iterations=args.max_iterations,
            samples=args.samples,
            force=args.force,
            verbose=not args.quiet,
        )
        results.append(r)

    # ----- summary -----
    summary = {
        "started": results[0]["started_at"] if results else None,
        "finished": _dt.datetime.now().isoformat(timespec="seconds"),
        "cells": results,
        "missing_annotations": missing,
    }
    out = REPORTS_DIR / f"tune_all_summary_{_dt.datetime.now().strftime('%Y%m%d_%H%M%S')}.json"
    out.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(f"\nSummary -> {out}")

    matrix_ok = sum(1 for r in results if r.get("matrix", {}).get("ok") or r.get("matrix", {}).get("skipped"))
    refine_total = sum(len(r.get("refine", {})) for r in results)
    refine_ok = sum(1 for r in results for v in r.get("refine", {}).values()
                    if v.get("ok") or v.get("skipped"))
    refine_conv = sum(1 for r in results for v in r.get("refine", {}).values()
                      if v.get("converged"))
    print(f"\nMatrix: {matrix_ok}/{len(results)} cells")
    print(f"Refine: {refine_ok}/{refine_total} morphologies succeeded ({refine_conv} converged)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
