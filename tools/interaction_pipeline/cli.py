"""
Top-level CLI orchestrator for the interaction pipeline.

Glues together the per-phase modules:
  engine_lifecycle  →  sweep  →  detectors  →  tuner  →  (apply)  →  game_smoke

Usage:
    python -m tools.interaction_pipeline.cli ASSET [options]

The orchestrator is the same code that the /interaction-pipeline chat skill
runs — keeping a single Python implementation avoids the historical drift
between CLI scripts and ad-hoc skill notebooks.
"""
from __future__ import annotations

import argparse
import dataclasses
import datetime as _dt
import json
import sys
import time
from dataclasses import asdict
from pathlib import Path
from typing import Any, Optional

import httpx

from .character_metrics import fetch_character_metrics
from .clip_selector import fetch_available_clips
from .detectors import (
    DetectionResult,
    Finding,
    FindingKind,
    run_detectors,
    write_engine_fix_queue,
)
from .engine_lifecycle import EngineSession, Mode, PROJECT_ROOT
from .game_smoke import run_game_smoke
from .matrix import run_matrix, write_matrix_result
from .ratification import RatificationReport, ratify_inputs, write_provenance
from .report import write_markdown_report, CellShot
from .sweep import (
    DEFAULT_ORBIT_VIEWS,
    DEFAULT_SAMPLES_PER_CLIP,
    SweepReport,
    run_sit_sweep,
)
from .tuner import ProfileDelta, TunerOutput, tune


ENGINE_FIX_QUEUE = PROJECT_ROOT / "tools" / "interaction_pipeline" / "engine_fix_queue.json"
DEFAULT_REPORTS_DIR = PROJECT_ROOT / "tools" / "interaction_pipeline" / "reports"


# ---------------------------------------------------------------------------
# Profile application
# ---------------------------------------------------------------------------

def _apply_profile_deltas(
    base_url: str,
    *,
    archetype: str,
    template_name: str,
    point_id: str,
    deltas: list[ProfileDelta],
    character_id: Optional[str] = None,
) -> dict[str, Any]:
    """Read current profile, add deltas, write back. Returns the new profile.

    When ``character_id`` is provided, the write becomes a per-character
    override (the engine refuses overrides unless a base profile exists, so
    callers must seed the base — character_id=None — first).
    """
    if not deltas:
        return {}
    with httpx.Client(timeout=10.0) as c:
        params = {"archetype": archetype, "template_name": template_name, "point_id": point_id}
        if character_id:
            params["character_id"] = character_id
        r = c.get(f"{base_url}/api/interaction/profile", params=params)
        r.raise_for_status()
        current = r.json()

        # The profile schema is *_offset arrays per state.
        offset_keys = {
            "sit_down":     "sit_down_offset",
            "sitting_idle": "sitting_idle_offset",
            "sit_stand_up": "sit_stand_up_offset",
        }
        # Build new payload from current, modified by deltas
        new_profile = dict(current)
        for d in deltas:
            key = offset_keys.get(d.target)
            if not key:
                continue
            cur = list(new_profile.get(key) or [0.0, 0.0, 0.0])
            while len(cur) < 3:
                cur.append(0.0)
            axis_idx = {"x": 0, "y": 1, "z": 2}.get(d.axis)
            if axis_idx is not None:
                cur[axis_idx] = float(cur[axis_idx]) + float(d.delta)
                new_profile[key] = cur
            elif d.axis == "facing_yaw":
                new_profile["facing_yaw"] = float(new_profile.get("facing_yaw", 0.0)) + float(d.delta)

        new_profile["archetype"] = archetype
        new_profile["template_name"] = template_name
        new_profile["point_id"] = point_id
        if character_id:
            new_profile["character_id"] = character_id

        r = c.post(f"{base_url}/api/interaction/profile", json=new_profile)
        r.raise_for_status()
        return r.json()


# ---------------------------------------------------------------------------
# Pipeline orchestrator
# ---------------------------------------------------------------------------

@dataclasses.dataclass
class IterationRecord:
    iteration: int
    sweep_report: str
    detection: dict[str, Any]
    tuner: dict[str, Any]
    applied_profile: dict[str, Any] = dataclasses.field(default_factory=dict)


@dataclasses.dataclass
class PipelineRunResult:
    started_at: str
    finished_at: str
    asset_path: str
    archetype: str
    template_name: str
    point_id: str
    iterations: list[IterationRecord]
    engine_fix_queue: Optional[str]
    game_smoke_result: Optional[dict[str, Any]]
    converged: bool
    notes: list[str]

    def to_dict(self) -> dict[str, Any]:
        return {
            **{k: v for k, v in asdict(self).items() if k != "iterations"},
            "iterations": [asdict(it) for it in self.iterations],
        }


def run_pipeline(
    asset_path: str | Path,
    *,
    archetype: str = "humanoid_normal",
    point_id: str = "seat_0",
    max_iterations: int = 3,
    samples_per_clip: int = DEFAULT_SAMPLES_PER_CLIP,
    orbit_views: Optional[list[str]] = None,
    take_screenshots: bool = True,
    tuner_backend: Optional[str] = None,
    apply_profile_deltas: bool = True,
    project_dir: Optional[str | Path] = None,
    template_name_override: Optional[str] = None,
    morphology: Optional[str] = None,
    session: Optional["EngineSession"] = None,
    on_crash: str = "abort",
    verbose: bool = True,
) -> PipelineRunResult:
    """End-to-end automated tune loop for one interaction asset.

    When ``morphology`` is set (e.g. ``"giant"`` / ``"dwarf"`` / ``"child"``),
    each iteration spawns that morphology before the sweep and writes the
    converged deltas as a per-character override. The base profile (for the
    standard morphology) must already exist; the matrix runner or a prior
    morphology=None pass seeds it.

    Steps:
      For each iteration up to max_iterations:
        1. Open EngineSession in INTERACTION_EDITOR mode
        2. Run sit sweep
        3. Run detectors
        4. Run tuner
        5. Optionally apply profile_deltas via /api/interaction/profile
        6. Persist any engine_findings to engine_fix_queue.json
        7. Stop iterating if tuner reports recommend_continue=false OR there
           are no profile findings.
      Then (optionally) run a game-mode smoke test.
    """
    asset_path = Path(asset_path).resolve()
    template_name = template_name_override or asset_path.stem
    orbit_views = orbit_views or DEFAULT_ORBIT_VIEWS

    def _log(msg: str) -> None:
        if verbose:
            print(f"[pipeline] {msg}", flush=True)

    started_at = _dt.datetime.now()
    iterations: list[IterationRecord] = []
    notes: list[str] = []
    converged = False
    engine_fix_queue_path: Optional[Path] = None

    # Reuse a caller-supplied session if any, else open one for the duration
    # of this run and close it at the end. Reusing avoids the cost of
    # restarting the engine for every iteration / morphology in batched runs.
    _own_session = session is None
    if _own_session:
        session = EngineSession(
            Mode.INTERACTION_EDITOR,
            target=str(asset_path),
            on_crash=on_crash,
            verbose=verbose,
        )
        session.__enter__()
    assert session is not None

    try:
        for i in range(1, max_iterations + 1):
            _log(f"=== Iteration {i}/{max_iterations} ===")
            # Per-morphology mode: rebuild the active character to the
            # requested preset before running the sweep so the bones we
            # measure correspond to that morphology.
            if morphology:
                from .morphology_presets import get as get_preset
                from .character_metrics import spawn_for_test
                preset = get_preset(morphology)
                spawn_for_test(preset.to_appearance_json(),
                               host="localhost", port=session.plan.port)
                _log(f"spawned morphology={morphology}")

            # Stages 1-3 ratification gate. Tuner writes are blocked if any
            # stage reports an error: this is what prevents the regression
            # where doors were saved with sit-shaped offsets, and where
            # offsets were written against mismatched clips.
            ratify_apply = apply_profile_deltas
            ratification: Optional[RatificationReport] = None
            try:
                cm = fetch_character_metrics(host="localhost", port=session.plan.port)
                with httpx.Client(timeout=5.0) as _c:
                    clips = fetch_available_clips(_c, session.base_url)
                ratification = ratify_inputs(
                    asset_path=asset_path,
                    point_id=point_id,
                    character_metrics=cm,
                    available_clips=clips,
                    archetype=archetype,
                    morphology=morphology or "",
                )
            except Exception as e:
                notes.append(f"ratification skipped iter {i}: {e!r}")
                ratify_apply = False
            if ratification is not None:
                for w in ratification.warnings:
                    _log(f"ratify WARN: {w}")
                if not ratification.ok:
                    for err in ratification.errors:
                        _log(f"ratify ERROR: {err}")
                    notes.append(
                        f"iter {i}: ratification failed ({len(ratification.errors)} errors) "
                        f"— refusing to apply profile deltas."
                    )
                    ratify_apply = False

            sweep_report = run_sit_sweep(
                asset_path,
                samples_per_clip=samples_per_clip,
                orbit_views=orbit_views,
                take_screenshots=take_screenshots,
                session=session,
                verbose=verbose,
            )
            detection = run_detectors(sweep_report)
            _log(f"detector: {detection.profile_count if hasattr(detection,'profile_count') else len(detection.profile_findings)} profile, "
                 f"{len(detection.engine_findings)} engine")

            # Persist any engine bugs to the queue regardless of profile state.
            if detection.engine_findings:
                write_engine_fix_queue(
                    detection,
                    ENGINE_FIX_QUEUE,
                    sweep_report_path=Path(sweep_report.report_dir) / "report.json",
                    append=True,
                )
                engine_fix_queue_path = ENGINE_FIX_QUEUE

            tuner_out = tune(sweep_report, detection, prefer_backend=tuner_backend)
            _log(f"tuner ({tuner_out.backend}): {len(tuner_out.profile_deltas)} deltas, "
                 f"confidence={tuner_out.confidence:.2f}, continue={tuner_out.recommend_continue}")

            applied: dict[str, Any] = {}
            if ratify_apply and tuner_out.profile_deltas:
                try:
                    applied = _apply_profile_deltas(
                        session.base_url,
                        archetype=archetype,
                        template_name=template_name,
                        point_id=point_id,
                        deltas=tuner_out.profile_deltas,
                        character_id=morphology,
                    )
                    _log(f"applied profile deltas (cid={morphology or '-'}): {applied}")
                    # Persist provenance alongside the saved profile so this
                    # write can be traced back to its inputs.
                    if ratification is not None and ratification.ok:
                        try:
                            sidecar = write_provenance(
                                ratification,
                                archetype=archetype,
                                template_name=template_name,
                                point_id=point_id,
                                morphology=morphology or "",
                            )
                            _log(f"wrote provenance: {sidecar}")
                        except Exception as e:
                            notes.append(f"provenance write failed iter {i}: {e!r}")
                except httpx.HTTPError as e:
                    notes.append(f"profile apply failed iter {i}: {e!r}")
            elif not ratify_apply and tuner_out.profile_deltas:
                _log("skipping profile write — ratification gate blocked apply.")

            iterations.append(IterationRecord(
                iteration=i,
                sweep_report=str(Path(sweep_report.report_dir) / "report.json"),
                detection=detection.to_dict(),
                tuner=tuner_out.to_dict(),
                applied_profile=applied,
            ))

            # Decide whether to keep iterating.
            if not detection.profile_findings:
                _log("no profile findings — converged.")
                converged = True
                break
            if not tuner_out.recommend_continue:
                _log("tuner does not recommend continuing.")
                break
            if not tuner_out.profile_deltas:
                _log("tuner returned no deltas — stopping.")
                break
    finally:
        if _own_session:
            try:
                session.__exit__(None, None, None)
            except Exception:
                pass

    game_smoke_dict: Optional[dict[str, Any]] = None
    if project_dir is not None:
        _log("running game-mode smoke test...")
        baseline = iterations[-1] if iterations else None
        baseline_report: Optional[SweepReport] = None
        if baseline is not None:
            try:
                data = json.loads(Path(baseline.sweep_report).read_text(encoding="utf-8"))
                from .sweep import FrameRecord
                frames = [FrameRecord(**fr) for fr in data["frames"]]
                baseline_report = SweepReport(
                    schema_version=data.get("schema_version", "sweep.v1"),
                    run_id=data.get("run_id", ""),
                    asset_path=data.get("asset_path", ""),
                    asset_stem=data.get("asset_stem", ""),
                    interaction=data.get("interaction", "sit"),
                    clips=data.get("clips", []),
                    samples_per_clip=data.get("samples_per_clip", 0),
                    started_at=data.get("started_at", ""),
                    finished_at=data.get("finished_at", ""),
                    duration_s=data.get("duration_s", 0.0),
                    engine_pid=data.get("engine_pid"),
                    engine_uptime_s=data.get("engine_uptime_s"),
                    frames=frames,
                    report_dir=data.get("report_dir", ""),
                    screenshot_dir=data.get("screenshot_dir", ""),
                    notes=data.get("notes", []),
                )
            except (json.JSONDecodeError, OSError, KeyError, TypeError) as e:
                notes.append(f"could not rehydrate baseline for smoke: {e!r}")
        try:
            gs = run_game_smoke(
                project_dir=project_dir,
                template_name=template_name,
                baseline_report=baseline_report,
                take_screenshots=take_screenshots,
                verbose=verbose,
            )
            game_smoke_dict = gs.to_dict()
        except Exception as e:  # smoke failures shouldn't kill the report
            notes.append(f"game smoke crashed: {e!r}")

    finished_at = _dt.datetime.now()
    return PipelineRunResult(
        started_at=started_at.isoformat(timespec="seconds"),
        finished_at=finished_at.isoformat(timespec="seconds"),
        asset_path=str(asset_path),
        archetype=archetype,
        template_name=template_name,
        point_id=point_id,
        iterations=iterations,
        engine_fix_queue=str(engine_fix_queue_path) if engine_fix_queue_path else None,
        game_smoke_result=game_smoke_dict,
        converged=converged,
        notes=notes,
    )


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main(argv: Optional[list[str]] = None) -> int:
    p = argparse.ArgumentParser(
        prog="interaction_pipeline",
        description="Automated sit-interaction tuning pipeline for Phyxel assets.",
    )
    p.add_argument("asset", help="Path to the .voxel asset")
    p.add_argument("--archetype", default="humanoid_normal")
    p.add_argument("--point-id", default="seat_0")
    p.add_argument("--max-iterations", type=int, default=3)
    p.add_argument("--samples", type=int, default=DEFAULT_SAMPLES_PER_CLIP)
    p.add_argument("--no-screenshots", action="store_true")
    p.add_argument("--views", nargs="+", default=DEFAULT_ORBIT_VIEWS)
    p.add_argument("--tuner-backend", default=None,
                   choices=["anthropic", "openai", "heuristic"])
    p.add_argument("--no-apply", action="store_true",
                   help="Run sweep/detect/tune but don't apply profile deltas")
    p.add_argument("--project", default=None,
                   help="Game project dir for the final smoke test (skipped if omitted)")
    p.add_argument("--template-name", default=None,
                   help="Override template name (default: asset filename stem)")
    p.add_argument("--on-crash", default="abort",
                   choices=["abort", "restart-once", "restart-and-skip"])
    p.add_argument("--quiet", action="store_true")
    p.add_argument("--out", default=None,
                   help="Where to write pipeline_result.json (default: reports/<stem>/pipeline_<ts>.json)")

    # --- Matrix mode -----------------------------------------------------
    # When --characters is set we switch into the (character × asset)
    # coverage runner instead of the iterative tuner. The matrix runner
    # spawns each preset via /api/character/spawn_for_test, runs the
    # interaction kind's pure-Python compatibility + initial-offset rules,
    # and emits a matrix_result.v1 JSON. Tuning per cell is deferred.
    p.add_argument("--characters", default=None,
                   help="Comma-separated morphology preset IDs (e.g. standard,giant,dwarf,child). "
                        "When set, runs matrix mode instead of the tuning pipeline.")
    p.add_argument("--kind", default="sit",
                   help="Interaction kind to evaluate in matrix mode (default: sit)")
    p.add_argument("--apply-overrides", action="store_true",
                   help="In matrix mode, write per-character overrides to the engine "
                        "profile store (seeding the base from STANDARD if missing).")
    p.add_argument("--report", action="store_true",
                   help="In matrix mode, capture multi-view screenshots per cell and "
                        "emit a markdown report next to the matrix JSON.")
    args = p.parse_args(argv)

    started = _dt.datetime.now()

    # Matrix mode short-circuits the tuner pipeline.
    if args.characters:
        char_ids = [c.strip() for c in args.characters.split(",") if c.strip()]
        result_m = run_matrix(
            args.asset,
            kind=args.kind,
            character_ids=char_ids,
            point_id=(args.point_id if args.point_id != "seat_0" else None),
            archetype=args.archetype,
            apply_overrides=args.apply_overrides,
            capture_screenshots=args.report,
            on_crash=args.on_crash,
            verbose=not args.quiet,
        )
        if args.out:
            out_path = Path(args.out)
        else:
            out_path = (
                DEFAULT_REPORTS_DIR
                / Path(args.asset).stem
                / f"matrix_{started.strftime('%Y%m%d_%H%M%S')}.json"
            )
        write_matrix_result(result_m, out_path)
        print(f"\n[matrix] result written: {out_path}")

        # When --report is on, emit a markdown report next to the JSON. The
        # screenshots are already on disk under <report_dir>/images/ from
        # `capture_cell_shots`; we just rehydrate CellShot records from the
        # cell.screenshots fields and let the report writer link them.
        if args.report and result_m.report_dir:
            shots: list[CellShot] = []
            for cell in result_m.cells:
                for s in cell.screenshots:
                    shots.append(CellShot(
                        character_id=cell.character_id,
                        clip=s.get("clip", ""),
                        t=float(s.get("t", 0.0)),
                        label=s.get("label", ""),
                        view=s.get("view", ""),
                        path=s.get("path", ""),
                    ))
            md_path = Path(result_m.report_dir) / "report.md"
            write_markdown_report(result_m.to_dict(), shots, md_path)
            print(f"[matrix] report written: {md_path}")
        for c in result_m.cells:
            mark = "OK " if c.can_interact else "BLK"
            print(f"  [{mark}] {c.character_id:9} "
                  f"H={c.character_metrics['total_height']:.3f} "
                  f"issues={len(c.compatibility_issues)}")
        return 0
    result = run_pipeline(
        args.asset,
        archetype=args.archetype,
        point_id=args.point_id,
        max_iterations=args.max_iterations,
        samples_per_clip=args.samples,
        orbit_views=args.views,
        take_screenshots=not args.no_screenshots,
        tuner_backend=args.tuner_backend,
        apply_profile_deltas=not args.no_apply,
        project_dir=args.project,
        template_name_override=args.template_name,
        on_crash=args.on_crash,
        verbose=not args.quiet,
    )

    if args.out:
        out_path = Path(args.out)
    else:
        out_path = (
            DEFAULT_REPORTS_DIR
            / Path(args.asset).stem
            / f"pipeline_{started.strftime('%Y%m%d_%H%M%S')}.json"
        )
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(result.to_dict(), indent=2), encoding="utf-8")
    print(f"\n[pipeline] result written: {out_path}")
    print(f"[pipeline] converged={result.converged} iterations={len(result.iterations)}")
    if result.engine_fix_queue:
        print(f"[pipeline] engine fixes queued: {result.engine_fix_queue}")
    if result.notes:
        print("[pipeline] notes:")
        for n in result.notes:
            print(f"  - {n}")
    return 0 if result.converged or not result.iterations else 0


if __name__ == "__main__":
    sys.exit(main())
