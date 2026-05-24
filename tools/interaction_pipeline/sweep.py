"""
Sweep orchestrator for the interaction pipeline.

Given a `.voxel` asset, drives the engine through a multi-clip, multi-time
sample of an interaction (sit/door/etc.) and produces a structured report:

  reports/<asset_stem>/<run_id>/
    report.json          rich per-frame telemetry + run metadata
    bones.csv            flat per-bone-per-frame table for quick analysis
    screenshots/         orbit screenshots per keyframe
    run.log              copy of phyxel.log slice for this run

The sweep is purely *observational* — it does not mutate the asset or any
profile. Phase C (detectors + tuner) decides whether anything needs to change
and either writes profile deltas or queues engine fix actions.
"""
from __future__ import annotations

import csv
import datetime as _dt
import json
import time
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Any, Optional

import httpx

from .engine_lifecycle import (
    DEFAULT_PORT,
    EngineSession,
    Mode,
    PROJECT_ROOT,
)


# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------

DEFAULT_REPORTS_DIR = PROJECT_ROOT / "tools" / "interaction_pipeline" / "reports"

# Sit interaction: 3 clips in the engine's animated character FSM.
# Each is sampled at N normalized times. Clip names match those defined in the
# .anim files for sitting characters (stand_to_sit / sitting_idle / sit_to_stand).
SIT_CLIPS = ["stand_to_sit", "sitting_idle", "sit_to_stand"]
DEFAULT_SAMPLES_PER_CLIP = 6  # 0.0, 0.2, 0.4, 0.6, 0.8, 1.0 — boundaries matter
DEFAULT_ORBIT_VIEWS = ["north", "east", "iso"]
DEFAULT_HTTP_TIMEOUT_S = 15.0
DEFAULT_SEEK_SETTLE_S = 0.15  # let the render pipeline catch up after a seek


# ---------------------------------------------------------------------------
# Data records
# ---------------------------------------------------------------------------

@dataclass
class FrameRecord:
    """One telemetry sample at a (clip, normalized_time) point."""
    clip: str
    t: float
    keyframe_index: int  # 0..N-1 within the run
    is_clip_boundary: bool
    telemetry: dict[str, Any]
    screenshots: dict[str, str] = field(default_factory=dict)  # view -> path


@dataclass
class SweepReport:
    schema_version: str
    run_id: str
    asset_path: str
    asset_stem: str
    interaction: str
    clips: list[str]
    samples_per_clip: int
    started_at: str
    finished_at: str
    duration_s: float
    engine_pid: Optional[int]
    engine_uptime_s: Optional[float]
    frames: list[FrameRecord]
    report_dir: str
    screenshot_dir: str
    notes: list[str] = field(default_factory=list)

    def to_json(self) -> dict[str, Any]:
        d = asdict(self)
        # Make sure FrameRecord dicts are JSON-safe
        return d


# ---------------------------------------------------------------------------
# HTTP helpers
# ---------------------------------------------------------------------------

class _EngineClient:
    """Thin wrapper around the engine HTTP API used by the sweep."""

    def __init__(self, base_url: str, timeout_s: float = DEFAULT_HTTP_TIMEOUT_S) -> None:
        self.base_url = base_url
        self._client = httpx.Client(timeout=timeout_s)

    def close(self) -> None:
        self._client.close()

    # ---- status / engine ----
    def engine_status(self) -> dict[str, Any]:
        r = self._client.get(f"{self.base_url}/api/engine/status")
        r.raise_for_status()
        return r.json()

    # ---- ie ----
    def ie_seek(self, clip: str, t: float) -> dict[str, Any]:
        r = self._client.post(
            f"{self.base_url}/api/interaction/ie/seek",
            json={"clip_name": clip, "normalized_time": float(t)},
        )
        r.raise_for_status()
        return r.json()

    def ie_resume(self) -> dict[str, Any]:
        r = self._client.post(f"{self.base_url}/api/interaction/ie/resume")
        r.raise_for_status()
        return r.json()

    def ie_telemetry(self, clip: Optional[str] = None,
                     t: Optional[float] = None,
                     include_bones: bool = True) -> dict[str, Any]:
        params: dict[str, Any] = {"include_bones": "1" if include_bones else "0"}
        if clip is not None:
            params["clip"] = clip
        if t is not None:
            params["t"] = f"{float(t):.4f}"
        r = self._client.get(
            f"{self.base_url}/api/interaction/ie/telemetry",
            params=params,
        )
        r.raise_for_status()
        return r.json()

    # ---- screenshots ----
    def orbit_screenshots(self, x: float, y: float, z: float,
                          radius: float = 4.0,
                          views: Optional[list[str]] = None) -> dict[str, Any]:
        body: dict[str, Any] = {"x": x, "y": y, "z": z, "radius": radius}
        if views:
            body["views"] = views
        r = self._client.post(
            f"{self.base_url}/api/orbit-screenshots",
            json=body,
        )
        r.raise_for_status()
        return r.json()


# ---------------------------------------------------------------------------
# Public sweep API
# ---------------------------------------------------------------------------

def _samples_for_clip(n: int) -> list[float]:
    """Return N normalized-time samples spanning [0, 1] inclusive."""
    if n <= 1:
        return [0.5]
    return [i / (n - 1) for i in range(n)]


def _slugify(s: str) -> str:
    return "".join(c if c.isalnum() or c in "-_" else "_" for c in s)


def run_sit_sweep(
    asset_path: str | Path,
    *,
    samples_per_clip: int = DEFAULT_SAMPLES_PER_CLIP,
    clips: Optional[list[str]] = None,
    orbit_views: Optional[list[str]] = None,
    orbit_radius: float = 4.0,
    take_screenshots: bool = True,
    reports_dir: Path = DEFAULT_REPORTS_DIR,
    session: Optional[EngineSession] = None,
    verbose: bool = True,
) -> SweepReport:
    """Run an end-to-end sit sweep on `asset_path`.

    If `session` is provided, it is used as-is (caller manages lifecycle).
    Otherwise a new EngineSession is opened in INTERACTION_EDITOR mode for the
    asset and closed at the end of the sweep.
    """
    asset_path = Path(asset_path).resolve()
    if not asset_path.is_file():
        raise FileNotFoundError(f"Asset not found: {asset_path}")

    clips = clips or SIT_CLIPS
    orbit_views = orbit_views or DEFAULT_ORBIT_VIEWS

    started_at = _dt.datetime.now()
    run_id = started_at.strftime("%Y%m%d_%H%M%S")
    asset_stem = asset_path.stem
    out_dir = reports_dir / _slugify(asset_stem) / run_id
    shot_dir = out_dir / "screenshots"
    out_dir.mkdir(parents=True, exist_ok=True)
    if take_screenshots:
        shot_dir.mkdir(parents=True, exist_ok=True)

    def _log(msg: str) -> None:
        if verbose:
            print(f"[sweep] {msg}", flush=True)

    own_session = session is None
    if own_session:
        session = EngineSession(
            Mode.INTERACTION_EDITOR,
            target=str(asset_path),
            verbose=verbose,
        )
        session.__enter__()
    assert session is not None

    frames: list[FrameRecord] = []
    notes: list[str] = []
    engine_pid: Optional[int] = None
    engine_uptime_s: Optional[float] = None

    try:
        client = _EngineClient(session.base_url)
        try:
            status = client.engine_status()
            engine_pid = status.get("pid")
            engine_uptime_s = status.get("uptime_s")
            if status.get("mode") != Mode.INTERACTION_EDITOR.value:
                notes.append(
                    f"Engine reported mode={status.get('mode')!r} (expected interaction_editor)"
                )

            keyframe_index = 0
            for clip in clips:
                ts = _samples_for_clip(samples_per_clip)
                for t in ts:
                    is_boundary = (t == 0.0 or t == 1.0)
                    _log(f"clip={clip} t={t:.2f} (boundary={is_boundary})")

                    # Seek so the live pose matches the sample point. Telemetry
                    # itself also accepts sampled clip+t, but seeking makes the
                    # accompanying screenshots show the correct pose.
                    try:
                        client.ie_seek(clip, t)
                    except httpx.HTTPError as e:
                        notes.append(f"seek failed at clip={clip} t={t}: {e!r}")

                    if DEFAULT_SEEK_SETTLE_S > 0:
                        time.sleep(DEFAULT_SEEK_SETTLE_S)

                    telemetry = client.ie_telemetry(clip=clip, t=t, include_bones=True)

                    # Screenshots use the seat anchor when available, else fall
                    # back to the character centroid.
                    shots: dict[str, str] = {}
                    if take_screenshots:
                        anchor = telemetry.get("seat_anchor") or telemetry.get("centroid")
                        if anchor:
                            try:
                                resp = client.orbit_screenshots(
                                    x=float(anchor["x"]),
                                    y=float(anchor["y"]),
                                    z=float(anchor["z"]),
                                    radius=orbit_radius,
                                    views=orbit_views,
                                )
                                for s in resp.get("screenshots", []):
                                    src = Path(s["path"])
                                    if not src.is_absolute():
                                        src = (PROJECT_ROOT / src).resolve()
                                    if src.is_file():
                                        dst = shot_dir / (
                                            f"kf{keyframe_index:03d}_{_slugify(clip)}"
                                            f"_t{int(t*100):03d}_{s['view']}.png"
                                        )
                                        try:
                                            dst.write_bytes(src.read_bytes())
                                            shots[s["view"]] = str(dst.relative_to(out_dir))
                                        except OSError as e:
                                            notes.append(f"screenshot copy failed: {e!r}")
                                    else:
                                        notes.append(f"screenshot missing: {src}")
                            except httpx.HTTPError as e:
                                notes.append(
                                    f"orbit_screenshots failed at clip={clip} t={t}: {e!r}"
                                )
                        else:
                            notes.append(
                                f"No anchor/centroid at clip={clip} t={t} — skipped screenshots"
                            )

                    frames.append(FrameRecord(
                        clip=clip,
                        t=float(t),
                        keyframe_index=keyframe_index,
                        is_clip_boundary=is_boundary,
                        telemetry=telemetry,
                        screenshots=shots,
                    ))
                    keyframe_index += 1

            # Always resume so the engine is in a normal state for the next op.
            try:
                client.ie_resume()
            except httpx.HTTPError as e:
                notes.append(f"resume failed at end: {e!r}")
        finally:
            client.close()
    finally:
        if own_session:
            session.__exit__(None, None, None)

    finished_at = _dt.datetime.now()
    report = SweepReport(
        schema_version="sweep.v1",
        run_id=run_id,
        asset_path=str(asset_path),
        asset_stem=asset_stem,
        interaction="sit",
        clips=clips,
        samples_per_clip=samples_per_clip,
        started_at=started_at.isoformat(timespec="seconds"),
        finished_at=finished_at.isoformat(timespec="seconds"),
        duration_s=(finished_at - started_at).total_seconds(),
        engine_pid=engine_pid,
        engine_uptime_s=engine_uptime_s,
        frames=frames,
        report_dir=str(out_dir),
        screenshot_dir=str(shot_dir),
        notes=notes,
    )

    _write_report(report, out_dir)
    _log(f"report written: {out_dir / 'report.json'}")
    return report


# ---------------------------------------------------------------------------
# Serialization
# ---------------------------------------------------------------------------

def _write_report(report: SweepReport, out_dir: Path) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "report.json").write_text(
        json.dumps(report.to_json(), indent=2),
        encoding="utf-8",
    )
    _write_bones_csv(report, out_dir / "bones.csv")


def _write_bones_csv(report: SweepReport, csv_path: Path) -> None:
    """Flatten per-bone telemetry to a single CSV row per (frame, bone)."""
    fields = [
        "keyframe_index", "clip", "t", "is_clip_boundary",
        "bone_name", "overlap_class", "signed_distance",
        "center_x", "center_y", "center_z",
        "half_x", "half_y", "half_z",
        "facing_yaw", "state",
    ]
    with csv_path.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(fields)
        for fr in report.frames:
            tele = fr.telemetry or {}
            facing = tele.get("facing_yaw")
            state = tele.get("state")
            for b in tele.get("bones", []) or []:
                c = b.get("center", {}) or {}
                h = b.get("half_extents", {}) or {}
                w.writerow([
                    fr.keyframe_index,
                    fr.clip,
                    f"{fr.t:.4f}",
                    int(bool(fr.is_clip_boundary)),
                    b.get("name", ""),
                    b.get("overlap_class", ""),
                    b.get("signed_distance", ""),
                    c.get("x", ""), c.get("y", ""), c.get("z", ""),
                    h.get("x", ""), h.get("y", ""), h.get("z", ""),
                    facing if facing is not None else "",
                    state if state is not None else "",
                ])


# ---------------------------------------------------------------------------
# CLI entry point (module is also runnable standalone for quick smoke tests)
# ---------------------------------------------------------------------------

def _main() -> int:
    import argparse

    p = argparse.ArgumentParser(
        description="Run a sit-interaction sweep against a .voxel asset.",
    )
    p.add_argument("asset", help="Path to the .voxel asset to sweep")
    p.add_argument("--samples", type=int, default=DEFAULT_SAMPLES_PER_CLIP,
                   help="Samples per clip (default 6 → t=0.0, 0.2, ..., 1.0)")
    p.add_argument("--no-screenshots", action="store_true",
                   help="Skip orbit screenshots (faster, telemetry only)")
    p.add_argument("--orbit-radius", type=float, default=4.0)
    p.add_argument("--views", nargs="+", default=DEFAULT_ORBIT_VIEWS,
                   help="Orbit views to capture (default: north east iso)")
    p.add_argument("--quiet", action="store_true")
    args = p.parse_args()

    report = run_sit_sweep(
        args.asset,
        samples_per_clip=args.samples,
        orbit_views=args.views,
        orbit_radius=args.orbit_radius,
        take_screenshots=not args.no_screenshots,
        verbose=not args.quiet,
    )
    print(f"Done. {len(report.frames)} frames captured in {report.duration_s:.1f}s")
    print(f"Report: {Path(report.report_dir) / 'report.json'}")
    if report.notes:
        print(f"Notes ({len(report.notes)}):")
        for n in report.notes:
            print(f"  - {n}")
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())
