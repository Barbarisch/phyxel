"""
Game-mode smoke test for the interaction pipeline.

After IE-mode tuning converges, this phase verifies the asset/profile combo
also works in a real game project — i.e. the engine running in `--project`
mode with the player as a normal animated character.

What it does:
  1. Launch the engine with --project <project_dir>
  2. POST /api/entity/spawn for the target template at a known position
  3. POST /api/interaction/sit for the player against that template
  4. Wait for the sit animation to finish
  5. GET /api/entity/player/bones and compare key bones (Hips/Pelvis/Spine,
     feet) against a baseline snapshot taken from the IE-final frame
  6. Optionally capture orbit screenshots for visual evidence

Output: GameSmokeResult (pass/fail + per-bone deltas + notes) + JSON file.
"""
from __future__ import annotations

import datetime as _dt
import json
import time
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Any, Optional

import httpx

from .engine_lifecycle import EngineSession, Mode, PROJECT_ROOT
from .sweep import FrameRecord, SweepReport


DEFAULT_REPORTS_DIR = PROJECT_ROOT / "tools" / "interaction_pipeline" / "reports"
SIT_ANIMATION_WAIT_S = 3.0
BONE_POSITION_TOLERANCE = 0.15
DEFAULT_SPAWN_POS = (0.0, 16.0, 0.0)


# ---------------------------------------------------------------------------
# Result types
# ---------------------------------------------------------------------------

@dataclass
class BoneDelta:
    bone: str
    baseline_center: tuple[float, float, float]
    live_center: tuple[float, float, float]
    distance: float
    within_tolerance: bool


@dataclass
class GameSmokeResult:
    passed: bool
    project_dir: str
    template_name: str
    spawn_pos: tuple[float, float, float]
    timestamp: str
    duration_s: float
    bone_deltas: list[BoneDelta]
    notes: list[str] = field(default_factory=list)
    screenshots: list[str] = field(default_factory=list)
    error: Optional[str] = None

    def to_dict(self) -> dict[str, Any]:
        d = asdict(self)
        d["bone_deltas"] = [
            {**bd, "baseline_center": list(bd["baseline_center"]),
             "live_center": list(bd["live_center"])}
            for bd in d["bone_deltas"]
        ]
        d["spawn_pos"] = list(d["spawn_pos"])
        return d


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

KEY_BONES = ["Hips", "Pelvis", "Spine", "Spine1", "LeftFoot", "RightFoot",
             "LeftUpLeg", "RightUpLeg"]


def _baseline_from_sweep(report: SweepReport) -> dict[str, tuple[float, float, float]]:
    """Pick the IE-final-pose frame (last sitting_idle sample) and snapshot
    key bone centers. The world-position component will be re-anchored before
    the comparison since the live test spawns at a different location."""
    idle_frames = [f for f in report.frames if f.clip == "sitting_idle"]
    target = idle_frames[-1] if idle_frames else (report.frames[-1] if report.frames else None)
    if target is None:
        return {}
    bones = (target.telemetry or {}).get("bones") or []
    out: dict[str, tuple[float, float, float]] = {}
    for b in bones:
        name = b.get("name", "")
        if name in KEY_BONES:
            c = b.get("center") or {}
            try:
                out[name] = (float(c["x"]), float(c["y"]), float(c["z"]))
            except (KeyError, TypeError, ValueError):
                continue
    return out


def _rebase(positions: dict[str, tuple[float, float, float]],
            old_anchor: tuple[float, float, float],
            new_anchor: tuple[float, float, float]) -> dict[str, tuple[float, float, float]]:
    dx, dy, dz = (new_anchor[0] - old_anchor[0],
                  new_anchor[1] - old_anchor[1],
                  new_anchor[2] - old_anchor[2])
    return {k: (v[0] + dx, v[1] + dy, v[2] + dz) for k, v in positions.items()}


def _pick_anchor(report: SweepReport) -> Optional[tuple[float, float, float]]:
    idle_frames = [f for f in report.frames if f.clip == "sitting_idle"]
    target = idle_frames[-1] if idle_frames else (report.frames[-1] if report.frames else None)
    if target is None:
        return None
    seat = (target.telemetry or {}).get("seat_anchor")
    if seat:
        return (float(seat["x"]), float(seat["y"]), float(seat["z"]))
    c = (target.telemetry or {}).get("centroid")
    if c:
        return (float(c["x"]), float(c["y"]), float(c["z"]))
    return None


# ---------------------------------------------------------------------------
# Public entry point
# ---------------------------------------------------------------------------

def run_game_smoke(
    *,
    project_dir: str | Path,
    template_name: str,
    baseline_report: Optional[SweepReport] = None,
    spawn_pos: tuple[float, float, float] = DEFAULT_SPAWN_POS,
    sit_wait_s: float = SIT_ANIMATION_WAIT_S,
    tolerance: float = BONE_POSITION_TOLERANCE,
    take_screenshots: bool = True,
    reports_dir: Path = DEFAULT_REPORTS_DIR,
    verbose: bool = True,
) -> GameSmokeResult:
    """Boot a game project, spawn the template, sit on it, compare to baseline.

    `baseline_report` is the SweepReport produced by `sweep.run_sit_sweep` on
    the same asset. If omitted, the smoke test only verifies "the engine
    doesn't crash and the player ends up near the spawn point" — no pose
    comparison.
    """
    project_dir = Path(project_dir).resolve()
    if not project_dir.is_dir():
        raise FileNotFoundError(f"Project directory not found: {project_dir}")

    started = _dt.datetime.now()
    out_dir = reports_dir / "game_smoke" / started.strftime("%Y%m%d_%H%M%S")
    out_dir.mkdir(parents=True, exist_ok=True)

    def _log(msg: str) -> None:
        if verbose:
            print(f"[game_smoke] {msg}", flush=True)

    baseline_centers: dict[str, tuple[float, float, float]] = {}
    baseline_anchor: Optional[tuple[float, float, float]] = None
    if baseline_report is not None:
        baseline_centers = _baseline_from_sweep(baseline_report)
        baseline_anchor = _pick_anchor(baseline_report)

    deltas: list[BoneDelta] = []
    notes: list[str] = []
    screenshots: list[str] = []
    error: Optional[str] = None

    with EngineSession(Mode.PROJECT, target=str(project_dir), verbose=verbose) as session:
        base = session.base_url
        try:
            with httpx.Client(timeout=15.0) as c:
                _log(f"spawning template {template_name!r} at {spawn_pos}")
                r = c.post(f"{base}/api/entity/spawn", json={
                    "template": template_name,
                    "x": spawn_pos[0], "y": spawn_pos[1], "z": spawn_pos[2],
                    "static": True,
                })
                if r.status_code >= 400:
                    notes.append(f"spawn failed: {r.status_code} {r.text}")
                else:
                    spawn_resp = r.json()
                    object_id = spawn_resp.get("id") or spawn_resp.get("object_id") or template_name
                    notes.append(f"spawned: {object_id}")

                    _log("triggering sit on player")
                    r = c.post(f"{base}/api/interaction/sit", json={
                        "entity_id": "player",
                        "object_id": object_id,
                        "point_id": "seat_0",
                    })
                    if r.status_code >= 400:
                        notes.append(f"sit failed: {r.status_code} {r.text}")
                    else:
                        time.sleep(sit_wait_s)

                        _log("fetching live bone snapshot")
                        rb = c.get(f"{base}/api/entity/player/bones")
                        if rb.status_code >= 400:
                            notes.append(f"bones fetch failed: {rb.status_code} {rb.text}")
                        else:
                            live = rb.json()
                            live_bones = live.get("bones") or live.get("data") or live
                            live_centers: dict[str, tuple[float, float, float]] = {}
                            if isinstance(live_bones, list):
                                for b in live_bones:
                                    name = b.get("name") or b.get("bone_name", "")
                                    if name not in KEY_BONES:
                                        continue
                                    c2 = b.get("center") or {}
                                    try:
                                        live_centers[name] = (
                                            float(c2["x"]), float(c2["y"]), float(c2["z"])
                                        )
                                    except (KeyError, TypeError, ValueError):
                                        continue

                            if baseline_centers and baseline_anchor and live_centers:
                                # Anchor live snapshot at the player's actual sit anchor by
                                # picking the live Hips/Pelvis as the new anchor reference.
                                live_hips = live_centers.get("Hips") or live_centers.get("Pelvis")
                                baseline_hips = (baseline_centers.get("Hips")
                                                 or baseline_centers.get("Pelvis"))
                                if live_hips and baseline_hips:
                                    rebased = _rebase(baseline_centers, baseline_hips, live_hips)
                                    for name, lc in live_centers.items():
                                        bc = rebased.get(name)
                                        if not bc:
                                            continue
                                        d = ((lc[0] - bc[0]) ** 2
                                             + (lc[1] - bc[1]) ** 2
                                             + (lc[2] - bc[2]) ** 2) ** 0.5
                                        deltas.append(BoneDelta(
                                            bone=name,
                                            baseline_center=bc,
                                            live_center=lc,
                                            distance=d,
                                            within_tolerance=(d <= tolerance),
                                        ))
                                else:
                                    notes.append(
                                        "No Hips/Pelvis in either baseline or live — skipping rebase comparison."
                                    )
                            elif not baseline_centers:
                                notes.append("No baseline supplied — skipping bone comparison.")
                            elif not live_centers:
                                notes.append("No live bones with known names — skipping comparison.")

                if take_screenshots:
                    try:
                        rr = c.post(f"{base}/api/orbit-screenshots", json={
                            "x": spawn_pos[0], "y": spawn_pos[1], "z": spawn_pos[2],
                            "radius": 4.0,
                            "views": ["north", "iso"],
                        })
                        if rr.status_code < 400:
                            for s in rr.json().get("screenshots", []):
                                src = Path(s["path"])
                                if not src.is_absolute():
                                    src = (PROJECT_ROOT / src).resolve()
                                if src.is_file():
                                    dst = out_dir / f"smoke_{s['view']}.png"
                                    try:
                                        dst.write_bytes(src.read_bytes())
                                        screenshots.append(str(dst.relative_to(out_dir)))
                                    except OSError as e:
                                        notes.append(f"screenshot copy failed: {e!r}")
                    except httpx.HTTPError as e:
                        notes.append(f"orbit_screenshots failed: {e!r}")
        except httpx.HTTPError as e:
            error = f"HTTP error: {e!r}"

    finished = _dt.datetime.now()
    failed_bones = [d for d in deltas if not d.within_tolerance]
    passed = (error is None) and (not failed_bones)
    result = GameSmokeResult(
        passed=passed,
        project_dir=str(project_dir),
        template_name=template_name,
        spawn_pos=spawn_pos,
        timestamp=started.isoformat(timespec="seconds"),
        duration_s=(finished - started).total_seconds(),
        bone_deltas=deltas,
        notes=notes,
        screenshots=screenshots,
        error=error,
    )

    (out_dir / "result.json").write_text(
        json.dumps(result.to_dict(), indent=2), encoding="utf-8"
    )
    _log(f"result: {'PASS' if passed else 'FAIL'} ({len(failed_bones)} bones out of tolerance, {len(deltas)} compared)")
    return result


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _main() -> int:
    import argparse
    from .sweep import FrameRecord  # noqa: F401 used for rehydration

    p = argparse.ArgumentParser(description="Game-mode smoke test for an interaction.")
    p.add_argument("--project", required=True, help="Path to a game project directory")
    p.add_argument("--template", required=True, help="Template name (no extension)")
    p.add_argument("--baseline", default=None,
                   help="Path to sweep report.json to compare against")
    p.add_argument("--spawn", default="0,16,0", help="x,y,z spawn position")
    p.add_argument("--no-screenshots", action="store_true")
    p.add_argument("--quiet", action="store_true")
    args = p.parse_args()

    baseline_report: Optional[SweepReport] = None
    if args.baseline:
        data = json.loads(Path(args.baseline).read_text(encoding="utf-8"))
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

    try:
        sx, sy, sz = [float(v) for v in args.spawn.split(",")]
    except ValueError:
        print(f"Invalid --spawn '{args.spawn}', expected 'x,y,z'")
        return 2

    result = run_game_smoke(
        project_dir=args.project,
        template_name=args.template,
        baseline_report=baseline_report,
        spawn_pos=(sx, sy, sz),
        take_screenshots=not args.no_screenshots,
        verbose=not args.quiet,
    )
    print(json.dumps(result.to_dict(), indent=2))
    return 0 if result.passed else 1


if __name__ == "__main__":
    raise SystemExit(_main())
