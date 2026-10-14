"""Foot-slide gate for in-place locomotion clips (tools/anim_pipeline/anim_lint.py).

Why: generated locomotion drafts (UniMate, docs/UniMateIntegrationPlan.md M1) can
look fine frame-by-frame while the feet skate once the controller adds the clip's
Speed. The linter reads the body velocity the clip was authored for straight off
the stance feet (a planted foot moves at -bodyVelocity in character space) and
flags (a) a Speed line that disagrees with it and (b) jitter/skate inside stance.

Red test: mocap `walk` with its Speed doubled MUST be flagged (100% mismatch).
Control: every shipped locomotion clip passes at the same thresholds — this
proves the metric measures the clip's real travel, not just "feet move", and
pins the direction convention (walk feet imply +Z body travel, strafes +/-X).
Measured 2026-09-29 at STANCE_HEIGHT 0.03: walk est 1.67 vs Speed 1.68, run
3.66/3.77, fast_run 5.41/5.66, left_strafe (+3.37, 0) / 3.62, walking_backward
(0, -1.03) / 1.20.
"""
from __future__ import annotations

import copy
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "anim_pipeline"))

from anim_format import parse  # type: ignore  # noqa: E402
import anim_lint  # type: ignore  # noqa: E402

ANIM_PATH = ROOT / "resources" / "animated_characters" / "humanoid.anim"
CONTROL_CLIPS = ["walk", "run", "fast_run", "unarmed_walk", "crouched_walking",
                 "left_strafe", "right_strafe", "left_strafe_walk", "right_strafe_walk",
                 "walking_backward"]
# Speed-bearing clips that are NOT cyclic locomotion: the gate must stay silent on
# them (their Speed is a root-travel offset or they decelerate by design).
EXEMPT_CLIPS = ["sword1h_light3", "jump_down", "crouch_to_stand", "run_to_stop",
                "female_start_walking", "death_front", "roll_forward", "bow_aim_walk_forward"]


def _af():
    return parse(ANIM_PATH)


def _errors(findings):
    return [msg for sev, msg in findings if sev == "ERROR"]


def test_control_shipped_locomotion_matches_its_speed_line():
    af = _af()
    for name in CONTROL_CLIPS:
        clip = af.clip(name)
        assert clip is not None and clip.speed, f"{name} missing or has no Speed"
        m = anim_lint.foot_slide_metrics(af, clip)
        assert "error" not in m, m
        assert m["stance_samples"] >= anim_lint.MIN_STANCE_SAMPLES, f"{name}: no stance phase"
        assert m["mismatch"] <= anim_lint.SPEED_MISMATCH_WARN, (
            f"{name}: stance feet imply {m['est_speed']:.3f} u/s but Speed is {clip.speed:.3f} "
            f"({m['mismatch']:.0%} off) — metric or clip is wrong"
        )
        assert m["residual"] / m["est_speed"] <= anim_lint.RESIDUAL_RATIO_WARN, (
            f"{name}: stance residual {m['residual']:.3f} u/s is {m['residual']/m['est_speed']:.0%} "
            f"of body speed"
        )
        assert not _errors(anim_lint.foot_slide_findings(af, clip))


def test_gate_is_silent_on_non_locomotion_speed_clips():
    af = _af()
    for name in EXEMPT_CLIPS:
        clip = af.clip(name)
        assert clip is not None and clip.speed, f"{name} missing or has no Speed"
        assert not anim_lint.is_locomotion_clip(af, clip), name
        assert anim_lint.foot_slide_findings(af, clip) == [], name
    for name in CONTROL_CLIPS:
        assert anim_lint.is_locomotion_clip(af, af.clip(name)), name
    # generated drafts are named unimate_<action>_r<k> and must be judged
    assert anim_lint.is_locomotion_clip(af, anim_lint.Clip(name="unimate_walk_r0", duration=2.0, speed=1.0))


def test_red_doubled_speed_is_flagged_as_skate():
    af = _af()
    walk = copy.deepcopy(af.clip("walk"))
    walk.speed *= 2.0
    findings = anim_lint.foot_slide_findings(af, walk)
    errs = _errors(findings)
    assert errs, f"doubled Speed produced no ERROR; findings={findings}"
    assert all("foot skates" in e for e in errs)


def test_direction_convention_is_measured_not_assumed():
    """walk travels +Z (model-space forward), strafes travel along X, backward -Z."""
    af = _af()
    vx, vz = anim_lint.foot_slide_metrics(af, af.clip("walk"))["body_vel"]
    assert vz > 1.0 and abs(vx) < 0.2, (vx, vz)
    vx, vz = anim_lint.foot_slide_metrics(af, af.clip("walking_backward"))["body_vel"]
    assert vz < -0.5 and abs(vx) < 0.2, (vx, vz)
    lx, lz = anim_lint.foot_slide_metrics(af, af.clip("left_strafe"))["body_vel"]
    rx, rz = anim_lint.foot_slide_metrics(af, af.clip("right_strafe"))["body_vel"]
    assert lx > 1.0 and rx < -1.0 and abs(lz) < 0.3 and abs(rz) < 0.3, (lx, lz, rx, rz)


def test_idle_implies_zero_body_velocity():
    af = _af()
    m = anim_lint.foot_slide_metrics(af, af.clip("idle"))
    assert m["est_speed"] < 0.05, m


def test_lint_clip_includes_slide_findings():
    af = _af()
    walk = copy.deepcopy(af.clip("walk"))
    walk.speed *= 2.0
    findings = anim_lint.lint_clip(af, walk)
    assert any("foot skates" in msg for _, msg in findings)
