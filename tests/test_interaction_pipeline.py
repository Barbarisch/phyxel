"""Unit tests for the interaction pipeline's deterministic components.

These tests exercise the pieces that do NOT require a running engine:
  - detectors.run_detectors over synthetic SweepReport fixtures
  - tuner._heuristic_tune mapping logic
  - sweep helpers (_samples_for_clip, _slugify)
  - cli profile-delta math (offset_keys application)

The engine_lifecycle module and live HTTP endpoints are exercised by the
integration test (separate file) and the chat skill.
"""
from __future__ import annotations

import json
from pathlib import Path

import pytest

from tools.interaction_pipeline import detectors as det_mod
from tools.interaction_pipeline import sweep as sweep_mod
from tools.interaction_pipeline import tuner as tuner_mod
from tools.interaction_pipeline.detectors import (
    DetectionResult,
    Finding,
    FindingKind,
    Severity,
    run_detectors,
    write_engine_fix_queue,
)
from tools.interaction_pipeline.sweep import FrameRecord, SweepReport
from tools.interaction_pipeline.tuner import tune


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

def _frame(clip, t, *, cx=5.0, cy=2.0, cz=0.0,
           feet_l_sd=0.01, feet_r_sd=0.01,
           seat=(5.0, 1.0, 0.0),
           extra_bones=None,
           state="standing",
           world_pos=None) -> FrameRecord:
    bones = list(extra_bones or [])
    # Default world_pos tracks centroid so existing tests continue to model the
    # case where the engine character anchor IS sliding (the bug scenario).
    # Tests that want to differentiate pose-only motion can pass world_pos
    # explicitly (e.g. a fixed anchor while centroid drifts).
    wp = world_pos if world_pos is not None else (cx, cy, cz)
    telemetry = {
        "centroid": {"x": cx, "y": cy, "z": cz},
        "world_pos": {"x": wp[0], "y": wp[1], "z": wp[2]},
        "facing_yaw": 0.0,
        "state": state,
        "seat_anchor": {"x": seat[0], "y": seat[1], "z": seat[2]} if seat else None,
        "feet": {
            "left":  {"y": cy - 1.0, "signed_distance": feet_l_sd},
            "right": {"y": cy - 1.0, "signed_distance": feet_r_sd},
        },
        "bones": bones,
    }
    return FrameRecord(
        clip=clip, t=t, keyframe_index=0,
        is_clip_boundary=(t in (0.0, 1.0)),
        telemetry=telemetry,
    )


def _sweep(frames) -> SweepReport:
    # number the keyframes
    for i, f in enumerate(frames):
        f.keyframe_index = i
    return SweepReport(
        schema_version="sweep.v1",
        run_id="test",
        asset_path="/tmp/x.voxel",
        asset_stem="x",
        interaction="sit",
        clips=["stand_to_sit", "sitting_idle", "sit_to_stand"],
        samples_per_clip=2,
        started_at="", finished_at="", duration_s=0.0,
        engine_pid=None, engine_uptime_s=None,
        frames=frames,
        report_dir="", screenshot_dir="",
    )


# ---------------------------------------------------------------------------
# sweep helpers
# ---------------------------------------------------------------------------

def test_samples_for_clip_endpoints_inclusive():
    s = sweep_mod._samples_for_clip(6)
    assert s[0] == 0.0
    assert s[-1] == 1.0
    assert len(s) == 6


def test_samples_for_clip_single():
    assert sweep_mod._samples_for_clip(1) == [0.5]


def test_slugify_strips_unsafe_chars():
    assert sweep_mod._slugify("my asset/v2.voxel") == "my_asset_v2_voxel"
    assert sweep_mod._slugify("chair_wood-01") == "chair_wood-01"


# ---------------------------------------------------------------------------
# Detector: POSITION_SNAP_AT_CLIP_BOUNDARY
# ---------------------------------------------------------------------------

def test_detect_position_snap_fires_on_boundary_jump():
    frames = [
        _frame("stand_to_sit", 0.0, cy=2.0),
        _frame("stand_to_sit", 1.0, cy=1.0),
        _frame("sitting_idle", 0.0, cy=1.5),  # +0.5 jump
        _frame("sitting_idle", 1.0, cy=1.5),
        _frame("sit_to_stand", 0.0, cy=1.5),
        _frame("sit_to_stand", 1.0, cy=2.0),
    ]
    result = run_detectors(_sweep(frames))
    engine_ids = [f.id for f in result.engine_findings]
    assert "POSITION_SNAP_AT_CLIP_BOUNDARY" in engine_ids


def test_detect_position_snap_silent_on_smooth_motion():
    frames = [
        _frame("stand_to_sit", 0.0, cy=2.0),
        _frame("stand_to_sit", 1.0, cy=1.5),
        _frame("sitting_idle", 0.0, cy=1.52),  # 0.02 — under threshold
        _frame("sitting_idle", 1.0, cy=1.5),
        _frame("sit_to_stand", 0.0, cy=1.5),
        _frame("sit_to_stand", 1.0, cy=2.0),
    ]
    result = run_detectors(_sweep(frames))
    engine_ids = [f.id for f in result.engine_findings]
    assert "POSITION_SNAP_AT_CLIP_BOUNDARY" not in engine_ids


# ---------------------------------------------------------------------------
# Detector: POSE_FEET_DESYNC requires persistence
# ---------------------------------------------------------------------------

def test_detect_feet_desync_persistent():
    # 3 consecutive frames with bad feet → fires.
    frames = [
        _frame("stand_to_sit", 0.0, feet_l_sd=0.5, feet_r_sd=0.5),
        _frame("stand_to_sit", 1.0, feet_l_sd=0.5, feet_r_sd=0.5),
        _frame("sitting_idle", 0.0, feet_l_sd=0.5, feet_r_sd=0.5),
        _frame("sitting_idle", 1.0),
        _frame("sit_to_stand", 0.0),
        _frame("sit_to_stand", 1.0),
    ]
    result = run_detectors(_sweep(frames))
    assert any(f.id == "POSE_FEET_DESYNC" for f in result.engine_findings)


def test_detect_feet_desync_transient_silenced():
    # Single transient bad-feet frame should not fire (persistent_frames=2).
    frames = [
        _frame("stand_to_sit", 0.0),
        _frame("stand_to_sit", 1.0, feet_l_sd=0.5),  # one frame only
        _frame("sitting_idle", 0.0),
        _frame("sitting_idle", 1.0),
        _frame("sit_to_stand", 0.0),
        _frame("sit_to_stand", 1.0),
    ]
    result = run_detectors(_sweep(frames))
    assert not any(f.id == "POSE_FEET_DESYNC" for f in result.engine_findings)


# ---------------------------------------------------------------------------
# Detector: INITIAL_TELEPORT
# ---------------------------------------------------------------------------

def test_detect_initial_teleport_fires_when_first_frame_too_low():
    frames = [
        _frame("stand_to_sit", 0.0, cy=1.05, seat=(5.0, 1.0, 0.0)),  # only 0.05 above seat
        _frame("stand_to_sit", 1.0, cy=1.0),
        _frame("sitting_idle", 0.0, cy=1.0),
        _frame("sitting_idle", 1.0, cy=1.0),
        _frame("sit_to_stand", 0.0, cy=1.0),
        _frame("sit_to_stand", 1.0, cy=2.0),
    ]
    result = run_detectors(_sweep(frames))
    assert any(f.id == "INITIAL_TELEPORT" for f in result.engine_findings)


def test_detect_initial_teleport_silent_when_standing_high():
    frames = [
        _frame("stand_to_sit", 0.0, cy=2.0, seat=(5.0, 1.0, 0.0)),  # 1.0 above seat
        _frame("stand_to_sit", 1.0, cy=1.0),
        _frame("sitting_idle", 0.0, cy=1.0),
        _frame("sitting_idle", 1.0, cy=1.0),
        _frame("sit_to_stand", 0.0, cy=1.0),
        _frame("sit_to_stand", 1.0, cy=2.0),
    ]
    result = run_detectors(_sweep(frames))
    assert not any(f.id == "INITIAL_TELEPORT" for f in result.engine_findings)


# ---------------------------------------------------------------------------
# Detector: OFFSET_TOO_LOW / OFFSET_TOO_HIGH (profile kind)
# ---------------------------------------------------------------------------

def test_detect_offset_too_low_fires_when_hips_penetrates():
    hips = {
        "name": "Hips",
        "center": {"x": 5.0, "y": 0.9, "z": 0.0},
        "half_extents": {"x": 0.1, "y": 0.1, "z": 0.1},
        "overlap_class": "desired_contact",
        "signed_distance": -0.15,  # 15cm penetration — exceeds 0.05 tolerance
    }
    frames = [
        _frame("stand_to_sit", 0.0),
        _frame("stand_to_sit", 1.0),
        _frame("sitting_idle", 0.0, extra_bones=[hips]),
        _frame("sitting_idle", 1.0, extra_bones=[hips]),
        _frame("sit_to_stand", 0.0),
        _frame("sit_to_stand", 1.0),
    ]
    result = run_detectors(_sweep(frames))
    profile_ids = [f.id for f in result.profile_findings]
    assert "OFFSET_TOO_LOW" in profile_ids


def test_detection_partitions_profile_vs_engine():
    # Mixed: snap + offset_too_low.
    hips_pen = {
        "name": "Hips", "center": {"x": 5, "y": 0.9, "z": 0},
        "half_extents": {"x": 0.1, "y": 0.1, "z": 0.1},
        "overlap_class": "desired_contact", "signed_distance": -0.10,
    }
    frames = [
        _frame("stand_to_sit", 0.0, cy=2.0),
        _frame("stand_to_sit", 1.0, cy=1.0),
        _frame("sitting_idle", 0.0, cy=1.5, extra_bones=[hips_pen]),  # snap
        _frame("sitting_idle", 1.0, cy=1.5, extra_bones=[hips_pen]),
        _frame("sit_to_stand", 0.0, cy=1.5),
        _frame("sit_to_stand", 1.0, cy=2.0),
    ]
    result = run_detectors(_sweep(frames))
    assert all(f.kind == FindingKind.PROFILE for f in result.profile_findings)
    assert all(f.kind == FindingKind.ENGINE_BUG for f in result.engine_findings)
    assert result.profile_findings
    assert result.engine_findings


# ---------------------------------------------------------------------------
# Detector: POSE_HORIZONTAL_SLIDING (the bug the user spotted manually)
# ---------------------------------------------------------------------------

def test_detect_horizontal_sliding_fires_on_monotonic_drift_in_stand_up():
    # Reproduces the field bug: character translates +Z monotonically through
    # sit_to_stand while the engine reports state='standing_up'.
    frames = [
        _frame("stand_to_sit", 0.0, cx=13.34, cy=17.18, cz=13.94, state="sitting_down"),
        _frame("stand_to_sit", 1.0, cx=13.35, cy=16.86, cz=13.64, state="sitting_down"),
        _frame("sitting_idle", 0.0, cx=13.34, cy=16.89, cz=13.71, state="sitting_idle"),
        _frame("sitting_idle", 1.0, cx=13.34, cy=16.89, cz=13.71, state="sitting_idle"),
        _frame("sit_to_stand", 0.0, cx=13.34, cy=16.86, cz=13.74, state="standing_up"),
        _frame("sit_to_stand", 0.33, cx=13.34, cy=16.83, cz=13.97, state="standing_up"),
        _frame("sit_to_stand", 0.67, cx=13.31, cy=17.13, cz=14.29, state="standing_up"),
        _frame("sit_to_stand", 1.0, cx=13.34, cy=17.17, cz=14.44, state="standing_up"),
    ]
    result = run_detectors(_sweep(frames))
    slide_findings = [f for f in result.engine_findings if f.id == "POSE_HORIZONTAL_SLIDING"]
    # sit_to_stand has ~0.7m monotonic +Z drift — must fire.
    assert any(f.evidence.get("clip") == "sit_to_stand" for f in slide_findings), \
        f"expected sit_to_stand sliding, got {[(f.id, f.evidence.get('clip')) for f in slide_findings]}"


def test_detect_horizontal_sliding_silent_when_planted():
    # Character stays put — no drift detected.
    frames = [
        _frame("stand_to_sit", 0.0, cx=10.0, cy=2.0, cz=5.0, state="sitting_down"),
        _frame("stand_to_sit", 1.0, cx=10.0, cy=1.5, cz=5.0, state="sitting_down"),
        _frame("sitting_idle", 0.0, cx=10.0, cy=1.5, cz=5.0, state="sitting_idle"),
        _frame("sitting_idle", 1.0, cx=10.0, cy=1.5, cz=5.0, state="sitting_idle"),
        _frame("sit_to_stand", 0.0, cx=10.0, cy=1.5, cz=5.0, state="standing_up"),
        _frame("sit_to_stand", 1.0, cx=10.0, cy=2.0, cz=5.0, state="standing_up"),
    ]
    result = run_detectors(_sweep(frames))
    assert not any(f.id == "POSE_HORIZONTAL_SLIDING" for f in result.engine_findings)


def test_detect_horizontal_sliding_idle_has_tighter_threshold():
    # 0.05m drift during sitting_idle should fire (idle threshold 0.03)…
    frames = [
        _frame("stand_to_sit", 0.0, cx=10.0, cz=5.0, state="sitting_down"),
        _frame("stand_to_sit", 1.0, cx=10.0, cz=5.0, state="sitting_down"),
        _frame("sitting_idle", 0.0, cx=10.0,  cz=5.00, state="sitting_idle"),
        _frame("sitting_idle", 0.5, cx=10.05, cz=5.05, state="sitting_idle"),
        _frame("sitting_idle", 1.0, cx=10.10, cz=5.10, state="sitting_idle"),
        _frame("sit_to_stand", 0.0, cx=10.10, cz=5.10, state="standing_up"),
        _frame("sit_to_stand", 1.0, cx=10.10, cz=5.10, state="standing_up"),
    ]
    result = run_detectors(_sweep(frames))
    slide_findings = [f for f in result.engine_findings if f.id == "POSE_HORIZONTAL_SLIDING"]
    assert any(f.evidence.get("clip") == "sitting_idle" for f in slide_findings)


def test_detect_horizontal_sliding_ignores_pose_drift_when_world_pos_constant():
    # After the engine fix: world_pos is held fixed at the seat anchor across all
    # sit states, but the .anim pose itself moves the model (e.g. leaning forward
    # to push off the chair). Centroid drifts, but world_pos does NOT — this is
    # legitimate animation motion, not a slide. Detector must stay silent.
    anchor = (13.333, 16.193, 13.933)
    frames = [
        _frame("stand_to_sit", 0.0, cx=13.34, cy=17.18, cz=13.94, state="sitting_down", world_pos=anchor),
        _frame("stand_to_sit", 1.0, cx=13.35, cy=16.86, cz=13.64, state="sitting_down", world_pos=anchor),
        _frame("sitting_idle", 0.0, cx=13.34, cy=16.89, cz=14.11, state="sitting_idle", world_pos=anchor),
        _frame("sitting_idle", 1.0, cx=13.34, cy=16.89, cz=14.11, state="sitting_idle", world_pos=anchor),
        _frame("sit_to_stand", 0.0, cx=13.34, cy=16.86, cz=14.14, state="standing_up", world_pos=anchor),
        _frame("sit_to_stand", 0.33, cx=13.34, cy=16.83, cz=14.23, state="standing_up", world_pos=anchor),
        _frame("sit_to_stand", 0.67, cx=13.31, cy=17.13, cz=14.42, state="standing_up", world_pos=anchor),
        _frame("sit_to_stand", 1.0, cx=13.34, cy=17.17, cz=14.44, state="standing_up", world_pos=anchor),
    ]
    result = run_detectors(_sweep(frames))
    assert not any(f.id == "POSE_HORIZONTAL_SLIDING" for f in result.engine_findings), \
        "centroid drift with constant world_pos is legitimate pose motion, not a slide"


# ---------------------------------------------------------------------------
# engine_fix_queue writing
# ---------------------------------------------------------------------------

def test_write_engine_fix_queue_appends(tmp_path: Path):
    det = DetectionResult(
        profile_findings=[],
        engine_findings=[Finding(
            id="POSITION_SNAP_AT_CLIP_BOUNDARY",
            kind=FindingKind.ENGINE_BUG,
            severity=Severity.ERROR,
            message="m", evidence={}, catalog_ref="POSITION_SNAP_AT_CLIP_BOUNDARY",
        )],
    )
    queue = tmp_path / "queue.json"
    write_engine_fix_queue(det, queue, sweep_report_path=tmp_path / "r.json")
    write_engine_fix_queue(det, queue, sweep_report_path=tmp_path / "r.json")
    data = json.loads(queue.read_text(encoding="utf-8"))
    assert len(data["entries"]) == 2
    # Catalog enrichment should have happened
    assert data["entries"][0]["catalog"], "catalog entry should be attached"


# ---------------------------------------------------------------------------
# Tuner: heuristic backend
# ---------------------------------------------------------------------------

def test_heuristic_tuner_raises_y_on_offset_too_low():
    det = DetectionResult(
        profile_findings=[Finding(
            id="OFFSET_TOO_LOW",
            kind=FindingKind.PROFILE,
            severity=Severity.WARN,
            message="m",
            evidence={"samples": [{"signed_distance": -0.18}]},
        )],
        engine_findings=[],
    )
    sr = _sweep([_frame("sitting_idle", 0.5)])
    out = tune(sr, det, prefer_backend="heuristic")
    assert out.backend == "heuristic"
    assert all(d.target in ("sit_down", "sitting_idle", "sit_stand_up") for d in out.profile_deltas)
    assert all(d.axis == "y" and d.delta > 0 for d in out.profile_deltas)
    assert out.recommend_continue is True


def test_heuristic_tuner_silent_on_no_findings():
    det = DetectionResult(profile_findings=[], engine_findings=[])
    sr = _sweep([_frame("sitting_idle", 0.5)])
    out = tune(sr, det, prefer_backend="heuristic")
    assert out.profile_deltas == []
    assert out.recommend_continue is False


def test_heuristic_tuner_defers_on_engine_bugs():
    det = DetectionResult(
        profile_findings=[Finding(
            id="OFFSET_TOO_LOW",
            kind=FindingKind.PROFILE,
            severity=Severity.WARN,
            message="m",
            evidence={"samples": [{"signed_distance": -0.1}]},
        )],
        engine_findings=[Finding(
            id="POSITION_SNAP_AT_CLIP_BOUNDARY",
            kind=FindingKind.ENGINE_BUG,
            severity=Severity.ERROR,
            message="m",
        )],
    )
    sr = _sweep([_frame("sitting_idle", 0.5)])
    out = tune(sr, det, prefer_backend="heuristic")
    # Engine findings present → must not recommend continuing.
    assert out.recommend_continue is False


def test_heuristic_tuner_clamps_at_0_2():
    det = DetectionResult(
        profile_findings=[Finding(
            id="OFFSET_TOO_LOW",
            kind=FindingKind.PROFILE,
            severity=Severity.WARN,
            message="m",
            evidence={"samples": [{"signed_distance": -10.0}]},  # absurdly deep
        )],
        engine_findings=[],
    )
    sr = _sweep([_frame("sitting_idle", 0.5)])
    out = tune(sr, det, prefer_backend="heuristic")
    for d in out.profile_deltas:
        assert abs(d.delta) <= 0.2


# ---------------------------------------------------------------------------
# Tuner: response parsing
# ---------------------------------------------------------------------------

def test_extract_json_object_strips_markdown_fence():
    txt = "```json\n{\"a\": 1}\n```"
    out = tuner_mod._extract_json_object(txt)
    assert out == {"a": 1}


def test_extract_json_object_handles_prose_wrap():
    txt = "Sure! Here is the JSON: {\"profile_deltas\": [], \"confidence\": 0.5, \"recommend_continue\": false, \"rationale\": \"ok\"}"
    out = tuner_mod._extract_json_object(txt)
    assert out["confidence"] == 0.5


def test_parse_llm_response_clamps_deltas():
    parsed = {
        "profile_deltas": [
            {"target": "sit_down", "axis": "y", "delta": 99.0, "reason": "test"},
            {"target": "sit_down", "axis": "facing_yaw", "delta": 999.0, "reason": "yaw"},
            {"target": "invalid", "axis": "y", "delta": 0.1, "reason": "x"},
            {"target": "sit_down", "axis": "w", "delta": 0.1, "reason": "x"},
        ],
        "confidence": 0.7,
        "recommend_continue": True,
        "rationale": "ok",
    }
    det = DetectionResult(profile_findings=[], engine_findings=[])
    out = tuner_mod._parse_llm_response(parsed, det, backend="test")
    # Two invalid entries dropped, two clamped.
    assert len(out.profile_deltas) == 2
    y_delta = next(d for d in out.profile_deltas if d.axis == "y")
    assert y_delta.delta == 0.3  # clamped from 99.0
    yaw_delta = next(d for d in out.profile_deltas if d.axis == "facing_yaw")
    assert yaw_delta.delta == 15.0  # clamped from 999.0
