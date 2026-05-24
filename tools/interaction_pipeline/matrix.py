"""Character × asset matrix runner.

The single-asset pipeline (`run_pipeline` in `cli.py`) tunes one set of offsets
for whatever character happens to be loaded. The matrix runner fans that out:
for each (interaction kind, asset, character morphology) cell it

  1. rebuilds the active character with the morphology preset
     (`POST /api/character/spawn_for_test`),
  2. fetches the post-rebuild scalar metrics,
  3. runs the kind's pure-Python `derive_initial_offsets` +
     `check_compatibility` against the asset metrics sidecar,
  4. emits a structured cell record.

The output is `matrix_result.v1` JSON — the format the Phase 4 markdown
report consumes. The runner does not auto-tune per cell; it produces the
coverage map. The full tuner is opt-in via `--tune-per-cell` (TODO).

Why pure-Python compat instead of the engine gate?
- The engine gate (`/api/interaction/can_interact`) requires a *placed*
  instance, which only project mode has. The matrix runner is designed to
  work in IE mode where the asset is loaded as the editor target, so we
  evaluate the kind's compatibility rules directly. The C++ gate stays as
  defense-in-depth at runtime; both share the same thresholds via the
  copy in `Application.cpp::runSitCompatChecks`.
"""
from __future__ import annotations

import dataclasses
import datetime as _dt
import json
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Any, Optional, Sequence

import httpx

from .asset_metrics import AssetMetrics, InteractionPointMetrics
from .character_metrics import CharacterMetrics, fetch_character_metrics, spawn_for_test
from .engine_lifecycle import EngineSession, Mode, PROJECT_ROOT
from . import interaction_kinds
from . import morphology_presets


SCHEMA_VERSION = "matrix_result.v1"


# ---------------------------------------------------------------------------
# Result dataclasses
# ---------------------------------------------------------------------------

@dataclass
class MatrixCell:
    character_id: str
    preset: dict[str, Any]
    character_metrics: dict[str, Any]
    compatibility_issues: list[dict[str, Any]]
    initial_offsets: dict[str, list[float]]
    can_interact: bool
    notes: list[str] = field(default_factory=list)
    # Phase 4: relative paths of any keypose screenshots captured for this
    # cell. Empty when `capture_screenshots=False`. Stored on the cell so the
    # report writer can find images even when invoked separately from the run.
    screenshots: list[dict[str, Any]] = field(default_factory=list)


@dataclass
class MatrixResult:
    schema_version: str
    started_at: str
    finished_at: str
    asset_path: str
    template_name: str
    kind: str
    point_id: str
    asset_features: dict[str, Any]
    cells: list[MatrixCell]
    # Phase 4: directory holding any captured screenshots + the markdown
    # report. Empty/absent when `capture_screenshots=False`.
    report_dir: Optional[str] = None

    def to_dict(self) -> dict[str, Any]:
        return {
            **{k: v for k, v in asdict(self).items() if k != "cells"},
            "cells": [asdict(c) for c in self.cells],
        }


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

_OFFSET_KEY_BY_CLIP = {
    "sit_down":     "sit_down_offset",
    "sitting_idle": "sitting_idle_offset",
    "sit_stand_up": "sit_stand_up_offset",
}


def _seed_base_if_missing(
    base_url: str,
    *,
    archetype: str,
    template_name: str,
    point_id: str,
    kind_obj: Any,
    features: dict[str, Any],
) -> None:
    """Ensure a base profile exists before we write any per-character overrides.

    The engine refuses to accept an override write when no base profile exists
    (so resolveProfile always has a fallback). We seed the base with the
    STANDARD preset's derived offsets so it represents the canonical reference
    rig \u2014 callers can always re-tune the base later via the existing tuner.
    """
    with httpx.Client(timeout=5.0) as c:
        r = c.get(
            f"{base_url}/api/interaction/profile",
            params={"archetype": archetype, "template_name": template_name, "point_id": point_id},
        )
        if r.status_code == 200 and r.json().get("found"):
            return  # base already exists, nothing to do

    # Synthesise a sensible base from standard-rig metrics. We don't query the
    # engine here; the standard preset's reference metrics come from the
    # morphology presets table by construction (heightScale=1, etc.), which
    # match the un-rebuilt rig's intrinsic proportions closely enough for
    # seeding. Subsequent overrides shadow this base anyway.
    from .morphology_presets import STANDARD  # local import to avoid cycle
    # Use the rig's intrinsic measurements baseline (set when no preset has
    # been applied yet). We construct the kind's initial offsets from a
    # neutral metrics dict + the asset features.
    standard_metrics = {
        # These are the live measurements of the un-rebuilt humanoid rig
        # captured in Phase 1.1 verification; if the rig is replaced these
        # need updating, but for now they're a safe constant.
        "total_height": 1.689, "hip_height": 0.905, "eye_height": 1.569,
        "leg_length": 0.9, "arm_reach": 0.562, "shoulder_width": 0.378,
        "hip_width": 0.201, "body_depth": 0.20, "sitting_height": 0.892,
    }
    offsets = kind_obj.derive_initial_offsets(standard_metrics, features)
    payload = {
        "archetype": archetype,
        "template_name": template_name,
        "point_id": point_id,
        # No character_id \u2014 this is the base profile.
    }
    for clip, key in _OFFSET_KEY_BY_CLIP.items():
        if clip in offsets.by_clip:
            payload[key] = list(offsets.by_clip[clip])
    with httpx.Client(timeout=5.0) as c:
        r = c.post(f"{base_url}/api/interaction/profile", json=payload)
        r.raise_for_status()


def _post_override(
    base_url: str,
    *,
    archetype: str,
    template_name: str,
    point_id: str,
    character_id: str,
    offsets: dict[str, tuple[float, float, float]],
) -> None:
    """POST a per-character override containing the derived offsets."""
    payload = {
        "archetype": archetype,
        "template_name": template_name,
        "point_id": point_id,
        "character_id": character_id,
    }
    for clip, key in _OFFSET_KEY_BY_CLIP.items():
        if clip in offsets:
            payload[key] = list(offsets[clip])
    with httpx.Client(timeout=5.0) as c:
        r = c.post(f"{base_url}/api/interaction/profile", json=payload)
        r.raise_for_status()


def _find_sidecar(asset_path: Path) -> Path:
    """Locate `<asset>.metrics.json` next to the .voxel file."""
    sidecar = asset_path.with_suffix(".metrics.json")
    if not sidecar.exists():
        raise FileNotFoundError(
            f"Missing asset metrics sidecar: {sidecar}\n"
            f"Run: python -m tools.characterize_asset {asset_path}"
        )
    return sidecar


def _resolve_report_dir(explicit: Optional[Path], template_name: str) -> Path:
    """Where this matrix run's screenshots + report live.

    Defaults to ``reports/<template>/<timestamp>/`` so multiple runs on the
    same asset don't clobber each other's images.
    """
    if explicit is not None:
        return Path(explicit)
    ts = _dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    return PROJECT_ROOT / "tools" / "interaction_pipeline" / "reports" / template_name / f"run_{ts}"


def _find_interaction_point(
    asset_metrics: AssetMetrics, point_id: Optional[str], kind_id: str
) -> InteractionPointMetrics:
    """Resolve the point we'll test against.

    If `point_id` is given, exact match. Otherwise pick the first point whose
    `kind` matches `kind_id` — for chairs that's typically the only `seat`.
    """
    if point_id:
        for p in asset_metrics.interaction_points:
            if p.point_id == point_id:
                return p
        raise KeyError(
            f"point_id {point_id!r} not in {asset_metrics.template_name}'s sidecar; "
            f"available: {[p.point_id for p in asset_metrics.interaction_points]}"
        )
    # Interaction *kind_id* (the verb, e.g. "sit") maps to the asset point
    # *kind* (the noun describing the feature, e.g. "seat"). Keep this small
    # table next to the lookup so new kinds need only one change.
    point_kinds_for_interaction = {
        "sit":            ("seat",),
        "door_open":      ("door_handle", "door"),
        "pickup":         ("grasp", "handle"),
        "container_open": ("lid", "drawer", "container"),
    }
    accepted = point_kinds_for_interaction.get(kind_id, (kind_id,))
    candidates = [p for p in asset_metrics.interaction_points if p.kind in accepted]
    if not candidates:
        raise KeyError(
            f"no interaction points matching kinds {accepted!r} for interaction "
            f"{kind_id!r} in {asset_metrics.template_name}'s sidecar; "
            f"available: {[(p.point_id, p.kind) for p in asset_metrics.interaction_points]}"
        )
    return candidates[0]


def _metrics_to_dict(m: CharacterMetrics) -> dict[str, Any]:
    """Flatten to the dict shape the kind compat rules read from."""
    return {
        "total_height":    m.total_height,
        "hip_height":      m.hip_height,
        "eye_height":      m.eye_height,
        "leg_length":      m.leg_length,
        "arm_reach":       m.arm_reach,
        "shoulder_width":  m.shoulder_width,
        "hip_width":       m.hip_width,
        "body_depth":      m.body_depth,
        "sitting_height":  m.sitting_height,
    }


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------

def run_matrix(
    asset_path: str | Path,
    *,
    kind: str = "sit",
    character_ids: Sequence[str] = ("standard", "giant", "dwarf", "child"),
    point_id: Optional[str] = None,
    archetype: str = "humanoid_normal",
    apply_overrides: bool = False,
    capture_screenshots: bool = False,
    report_dir: Optional[Path] = None,
    session: Optional[EngineSession] = None,
    on_crash: str = "abort",
    verbose: bool = True,
) -> MatrixResult:
    """Run the character × asset compatibility matrix for one asset+kind.

    Spawns one engine session in INTERACTION_EDITOR mode with the asset
    loaded as the target, then iterates the character presets in sequence,
    rebuilding the active character morphology per cell. Each call to
    `spawn_for_test` is idempotent for the same preset; the engine also
    self-tests cross-preset rebuilds, but the matrix runner always rebuilds
    fresh per cell so drift across presets cannot leak between rows.
    """
    asset_path = Path(asset_path).resolve()
    sidecar = _find_sidecar(asset_path)
    asset_metrics = AssetMetrics.from_file(sidecar)

    kind_obj = interaction_kinds.get(kind)
    point = _find_interaction_point(asset_metrics, point_id, kind_obj.kind_id)

    started_at = _dt.datetime.now()

    def _log(msg: str) -> None:
        if verbose:
            print(f"[matrix] {msg}", flush=True)

    cells: list[MatrixCell] = []
    # Resolve the report directory once so all cells in this run share the
    # same `images/` folder (and so the caller can write the final markdown
    # report next to it).
    resolved_report_dir: Optional[Path] = None
    if capture_screenshots:
        resolved_report_dir = _resolve_report_dir(report_dir, Path(asset_path).stem)

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
        _log(f"asset={asset_metrics.template_name} kind={kind_obj.kind_id} "
             f"point={point.point_id} characters={list(character_ids)}")

        if apply_overrides:
            # Seed the base profile from `standard` (or the first usable cell)
            # before writing any per-character overrides — the engine refuses
            # overrides when no base exists yet. Done here, ahead of the loop,
            # so a separate "standard" cell in `character_ids` isn't required.
            _seed_base_if_missing(
                session.base_url,
                archetype=archetype,
                template_name=asset_metrics.template_name,
                point_id=point.point_id,
                kind_obj=kind_obj,
                features=point.features,
            )

        for cid in character_ids:
            try:
                preset = morphology_presets.get(cid)
            except KeyError as e:
                _log(f"skipping unknown preset {cid!r}: {e}")
                continue

            notes: list[str] = []
            appearance = preset.to_appearance_json()
            try:
                metrics = spawn_for_test(
                    appearance,
                    host="localhost",
                    port=session.plan.port,
                )
            except Exception as e:  # network / engine failure on one cell shouldn't kill the run
                notes.append(f"spawn_for_test failed: {e!r}")
                _log(f"  {cid}: spawn_for_test failed ({e!r})")
                # Fall back to the currently-loaded metrics so the cell still
                # has *something* — but mark as unreliable.
                try:
                    metrics = fetch_character_metrics(port=session.plan.port)
                except Exception:
                    continue

            metrics_dict = _metrics_to_dict(metrics)
            features = point.features

            issues = kind_obj.check_compatibility(metrics_dict, features)
            offsets = kind_obj.derive_initial_offsets(metrics_dict, features)

            has_blocking = any(i.severity == "error" for i in issues)
            _log(f"  {cid}: H={metrics.total_height:.3f} hipW={metrics.hip_width:.3f} "
                 f"legL={metrics.leg_length:.3f} issues={len(issues)} "
                 f"can_interact={not has_blocking}")

            # Optionally bake the derived offsets into the engine's profile
            # store as a per-character override. This is the persistence step
            # the standalone runtime sit code consumes via resolveProfile().
            if apply_overrides and not has_blocking:
                try:
                    _post_override(
                        session.base_url,
                        archetype=archetype,
                        template_name=asset_metrics.template_name,
                        point_id=point.point_id,
                        character_id=cid,
                        offsets=offsets.by_clip,
                    )
                    notes.append("baked override")
                except Exception as e:
                    notes.append(f"override write failed: {e!r}")
                    _log(f"  {cid}: override write failed ({e!r})")

            cell_shots: list[dict[str, Any]] = []
            if capture_screenshots and resolved_report_dir is not None:
                # Local import keeps the report dependency optional for
                # callers that just want the JSON matrix.
                from .report import capture_cell_shots
                shots_dir = resolved_report_dir / "images"
                try:
                    captured = capture_cell_shots(
                        base_url=session.base_url,
                        character_id=cid,
                        out_dir=shots_dir,
                        verbose=verbose,
                    )
                    cell_shots = [
                        {"clip": s.clip, "t": s.t, "label": s.label,
                         "view": s.view, "path": s.path}
                        for s in captured
                    ]
                except Exception as e:
                    notes.append(f"screenshot capture failed: {e!r}")
                    _log(f"  {cid}: screenshot capture failed ({e!r})")

            cells.append(MatrixCell(
                character_id=cid,
                preset=dataclasses.asdict(preset),
                character_metrics=metrics_dict,
                compatibility_issues=[i.to_dict() for i in issues],
                initial_offsets={k: list(v) for k, v in offsets.by_clip.items()},
                can_interact=not has_blocking,
                notes=notes,
                screenshots=cell_shots,
            ))
    finally:
        if _own_session:
            try:
                session.__exit__(None, None, None)
            except Exception:
                pass

    finished_at = _dt.datetime.now()
    return MatrixResult(
        schema_version=SCHEMA_VERSION,
        started_at=started_at.isoformat(timespec="seconds"),
        finished_at=finished_at.isoformat(timespec="seconds"),
        asset_path=str(asset_path),
        template_name=asset_metrics.template_name,
        kind=kind_obj.kind_id,
        point_id=point.point_id,
        asset_features=dict(point.features),
        cells=cells,
        report_dir=str(resolved_report_dir) if resolved_report_dir else None,
    )


def write_matrix_result(result: MatrixResult, out_path: Path) -> Path:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(result.to_dict(), indent=2), encoding="utf-8")
    return out_path


__all__ = [
    "SCHEMA_VERSION",
    "MatrixCell",
    "MatrixResult",
    "run_matrix",
    "write_matrix_result",
]
