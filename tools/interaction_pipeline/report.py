"""Phase 4 — multi-view screenshots + markdown report per matrix cell.

Given a `MatrixResult` (in memory or on disk), capture a small set of
diagnostic screenshots per cell and emit a markdown report. The default
capture plan is three keyposes (sit-down end, sitting-idle midpoint,
sit-to-stand start) × three orbit views (south/east/iso) per cell, which
is enough to spot the common posture failures (feet dangling, hips
clipping the seat, knees through the backrest) without exploding into a
full animation sweep.

Designed to be called from inside an already-open ``EngineSession`` —
the matrix runner reuses the session it opened so we don't pay the
engine startup cost twice.
"""
from __future__ import annotations

import datetime as _dt
import json
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Optional, Sequence

import httpx

from .engine_lifecycle import PROJECT_ROOT


# Keyposes worth capturing for the sit kind. Each tuple is (clip, t, label).
# We pick the visually-most-distinctive moments: just after the character
# settles into the chair, the midpoint of the idle (where any drift shows
# up), and the start of the stand transition (where push-up posture matters).
SIT_KEYPOSES: list[tuple[str, float, str]] = [
    ("stand_to_sit", 1.0, "seated"),
    ("sitting_idle", 0.5, "idle"),
    ("sit_to_stand", 0.0, "rising"),
]

# Orbit views to capture per keypose. Front + side + iso is enough for the
# common posture failure modes; `top` is rarely useful for seated characters.
DEFAULT_VIEWS: list[str] = ["south", "east", "iso"]


# ---------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------

@dataclass
class CellShot:
    """A single captured frame: one keypose × one view."""
    character_id: str
    clip: str
    t: float
    label: str
    view: str
    path: str  # relative-to-report path


@dataclass
class ReportResult:
    schema_version: str
    matrix_path: Optional[str]
    report_dir: str
    markdown_path: str
    shots: list[CellShot] = field(default_factory=list)


# ---------------------------------------------------------------------------
# Capture
# ---------------------------------------------------------------------------

def _slugify(s: str) -> str:
    return "".join(c if c.isalnum() or c in "-_" else "_" for c in s)


def capture_cell_shots(
    *,
    base_url: str,
    character_id: str,
    out_dir: Path,
    keyposes: Sequence[tuple[str, float, str]] = SIT_KEYPOSES,
    views: Sequence[str] = DEFAULT_VIEWS,
    orbit_radius: float = 2.5,
    seek_settle_s: float = 0.10,
    verbose: bool = True,
) -> list[CellShot]:
    """Capture screenshots for one matrix cell.

    Assumes the engine is already in IE mode with the asset loaded *and*
    that `spawn_for_test` has already been called to apply this cell's
    morphology preset. The caller (matrix runner) handles that lifecycle.
    """
    out_dir.mkdir(parents=True, exist_ok=True)
    shots: list[CellShot] = []

    def _log(msg: str) -> None:
        if verbose:
            print(f"[report]   {msg}", flush=True)

    with httpx.Client(timeout=15.0) as c:
        for clip, t, label in keyposes:
            # Seek so the pose is correct, then settle a frame so the GPU
            # has uploaded the new skinning data.
            try:
                c.post(f"{base_url}/api/interaction/ie/seek",
                       json={"clip_name": clip, "normalized_time": float(t)})
            except httpx.HTTPError as e:
                _log(f"seek failed clip={clip} t={t}: {e!r}")
                continue
            if seek_settle_s > 0:
                time.sleep(seek_settle_s)

            # Anchor screenshots around the seat (better framing than the
            # character centroid, which can be off-screen mid-fall).
            try:
                tel = c.get(
                    f"{base_url}/api/interaction/ie/telemetry",
                    params={"clip": clip, "t": f"{t:.4f}", "include_bones": "0"},
                ).json()
            except httpx.HTTPError as e:
                _log(f"telemetry failed clip={clip} t={t}: {e!r}")
                continue

            anchor = tel.get("seat_anchor") or tel.get("centroid")
            if not anchor:
                _log(f"no anchor for clip={clip} t={t} — skipping")
                continue

            try:
                resp = c.post(
                    f"{base_url}/api/orbit-screenshots",
                    json={
                        "x": float(anchor["x"]),
                        "y": float(anchor["y"]),
                        "z": float(anchor["z"]),
                        "radius": orbit_radius,
                        "views": list(views),
                    },
                ).json()
            except httpx.HTTPError as e:
                _log(f"orbit_screenshots failed clip={clip} t={t}: {e!r}")
                continue

            for s in resp.get("screenshots", []):
                src = Path(s["path"])
                if not src.is_absolute():
                    src = (PROJECT_ROOT / src).resolve()
                if not src.is_file():
                    _log(f"missing screenshot {src}")
                    continue
                dst = out_dir / (
                    f"{_slugify(character_id)}_{_slugify(label)}_{_slugify(s['view'])}.png"
                )
                try:
                    dst.write_bytes(src.read_bytes())
                except OSError as e:
                    _log(f"copy failed {src} -> {dst}: {e!r}")
                    continue
                shots.append(CellShot(
                    character_id=character_id,
                    clip=clip,
                    t=float(t),
                    label=label,
                    view=str(s["view"]),
                    path=str(dst.relative_to(out_dir.parent)),
                ))

    _log(f"{character_id}: captured {len(shots)} shots")
    return shots


# ---------------------------------------------------------------------------
# Markdown emit
# ---------------------------------------------------------------------------

_SEVERITY_GLYPH = {"error": "BLOCK", "warn": "warn", "info": "info"}


def _format_metric(value: Any) -> str:
    if isinstance(value, (int, float)):
        return f"{float(value):.3f}"
    return str(value)


def write_markdown_report(
    matrix_result: dict[str, Any],
    shots: list[CellShot],
    out_path: Path,
) -> Path:
    """Render the matrix result + captured shots as a markdown report.

    Layout: one section per cell with metrics summary, issues table, and
    a grid of keypose × view thumbnails. Image paths are stored relative
    to ``out_path``'s parent so the report stays portable.
    """
    out_path = Path(out_path)
    lines: list[str] = []

    title = (
        f"# Interaction matrix report: "
        f"{matrix_result.get('template_name', '?')} / {matrix_result.get('kind', '?')}"
    )
    lines.append(title)
    lines.append("")
    lines.append(f"- **Schema**: `{matrix_result.get('schema_version', '?')}`")
    lines.append(f"- **Asset**: `{matrix_result.get('asset_path', '?')}`")
    lines.append(f"- **Interaction point**: `{matrix_result.get('point_id', '?')}`")
    lines.append(f"- **Run window**: {matrix_result.get('started_at', '?')} "
                 f"to {matrix_result.get('finished_at', '?')}")
    lines.append(f"- **Report generated**: {_dt.datetime.now().isoformat(timespec='seconds')}")
    lines.append("")

    # Quick summary table across all cells.
    cells = matrix_result.get("cells", [])
    lines.append("## Summary")
    lines.append("")
    lines.append("| Character | H (m) | hipW (m) | legL (m) | Issues | Can interact? |")
    lines.append("|-----------|-------|----------|----------|--------|---------------|")
    for cell in cells:
        m = cell.get("character_metrics", {})
        n_issues = len(cell.get("compatibility_issues", []))
        ok = "yes" if cell.get("can_interact") else "**NO**"
        lines.append(
            f"| {cell.get('character_id', '?')} "
            f"| {_format_metric(m.get('total_height'))} "
            f"| {_format_metric(m.get('hip_width'))} "
            f"| {_format_metric(m.get('leg_length'))} "
            f"| {n_issues} "
            f"| {ok} |"
        )
    lines.append("")

    # Index shots by (character, label, view) so the per-cell renderer is O(1).
    shot_index: dict[tuple[str, str, str], CellShot] = {
        (s.character_id, s.label, s.view): s for s in shots
    }
    labels = [label for _, _, label in SIT_KEYPOSES]
    views_present = sorted({s.view for s in shots}) or list(DEFAULT_VIEWS)

    # Per-cell detail
    for cell in cells:
        cid = cell.get("character_id", "?")
        lines.append(f"## {cid}")
        lines.append("")

        # Preset summary (height/bulk/etc. give context for the diagnostics below)
        preset = cell.get("preset", {})
        lines.append("**Preset.** "
                     f"height={preset.get('height_scale', '?')}, "
                     f"bulk={preset.get('bulk_scale', '?')}, "
                     f"leg={preset.get('leg_length_scale', '?')}, "
                     f"torso={preset.get('torso_length_scale', '?')}, "
                     f"shoulder={preset.get('shoulder_width_scale', '?')}")
        lines.append("")

        # Metrics
        m = cell.get("character_metrics", {})
        lines.append("**Metrics.** "
                     f"total_height={_format_metric(m.get('total_height'))} m, "
                     f"hip_height={_format_metric(m.get('hip_height'))} m, "
                     f"leg_length={_format_metric(m.get('leg_length'))} m, "
                     f"hip_width={_format_metric(m.get('hip_width'))} m, "
                     f"shoulder_width={_format_metric(m.get('shoulder_width'))} m, "
                     f"sitting_height={_format_metric(m.get('sitting_height'))} m")
        lines.append("")

        # Derived offsets
        offsets = cell.get("initial_offsets", {})
        if offsets:
            lines.append("**Derived offsets.**")
            lines.append("")
            lines.append("| Clip | Offset (x, y, z) |")
            lines.append("|------|------------------|")
            for clip in ("sit_down", "sitting_idle", "sit_stand_up"):
                if clip in offsets:
                    o = offsets[clip]
                    lines.append(f"| `{clip}` | ({o[0]:.3f}, {o[1]:.3f}, {o[2]:.3f}) |")
            lines.append("")

        # Issues
        issues = cell.get("compatibility_issues", [])
        if issues:
            lines.append("**Compatibility issues.**")
            lines.append("")
            lines.append("| Severity | Rule | Message |")
            lines.append("|----------|------|---------|")
            for i in issues:
                sev = _SEVERITY_GLYPH.get(i.get("severity", ""), i.get("severity", ""))
                lines.append(
                    f"| {sev} | `{i.get('rule_id', '?')}` | {i.get('message', '')} |"
                )
            lines.append("")
        else:
            lines.append("**No compatibility issues detected.**")
            lines.append("")

        notes = cell.get("notes", [])
        if notes:
            lines.append("**Notes.** " + "; ".join(notes))
            lines.append("")

        # Screenshot grid: rows = keyposes, columns = views.
        if any((cid, label, view) in shot_index
               for _, _, label in SIT_KEYPOSES for view in views_present):
            lines.append("**Screenshots.**")
            lines.append("")
            header_views = " | ".join(views_present)
            lines.append(f"| Keypose | {header_views} |")
            lines.append("|---------|" + "|".join(["---"] * len(views_present)) + "|")
            for label in labels:
                row = [label]
                for view in views_present:
                    shot = shot_index.get((cid, label, view))
                    if shot:
                        row.append(f"![{cid} {label} {view}]({shot.path})")
                    else:
                        row.append("_(missing)_")
                lines.append("| " + " | ".join(row) + " |")
            lines.append("")
        else:
            lines.append("_No screenshots captured for this cell._")
            lines.append("")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(lines), encoding="utf-8")
    return out_path
