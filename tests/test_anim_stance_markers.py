"""A3 transition graph: locomotion clips carry gait phase markers (stanceL/stanceR, normalized
time when each foot plants) so walk↔run switches can enter at the same gait phase.
RED 2026-09-30: humanoid.anim has no stance markers."""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "anim_pipeline"))

from anim_format import parse  # noqa: E402
import anim_lint  # noqa: E402
import clip_meta_schema as cms  # noqa: E402

HUMANOID = ROOT / "resources/animated_characters/humanoid.anim"
LOCOMOTION = ["walk", "run", "fast_run", "left_strafe_walk", "right_strafe_walk", "left_strafe",
              "right_strafe", "walking_backward"]


def _phase_dist(a: float, b: float) -> float:
    d = (a - b) % 1.0
    return min(d, 1.0 - d)


@pytest.fixture(scope="module")
def humanoid():
    return parse(HUMANOID)


def test_stance_markers_find_both_feet_half_a_cycle_apart_on_walk_and_run(humanoid):
    for name in ("walk", "run"):
        clip = humanoid.clip(name)
        m = anim_lint.stance_markers(humanoid, clip)
        assert "L" in m and "R" in m, (name, m)
        assert 0.0 <= m["L"] < 1.0 and 0.0 <= m["R"] < 1.0
        assert abs(_phase_dist(m["L"], m["R"]) - 0.5) < 0.15, (name, m)


def test_shipped_humanoid_locomotion_clips_carry_stance_markers():
    meta = {clip: m for clip, m in cms.iter_header_meta(HUMANOID)}
    missing = [n for n in LOCOMOTION if "stanceL" not in meta.get(n, {}) or "stanceR" not in meta.get(n, {})]
    assert missing == [], f"run: python tools/anim_pipeline/anim_lint.py stance {HUMANOID} --write  (missing: {missing})"
    for n in LOCOMOTION:
        assert 0.0 <= float(meta[n]["stanceL"]) < 1.0
        assert 0.0 <= float(meta[n]["stanceR"]) < 1.0


def test_stance_keys_are_in_the_schema():
    assert cms.validate_meta({"stanceL": "0.12", "stanceR": "0.62"}) == []
    assert "ERROR" in [s for s, _ in cms.validate_meta({"stanceL": "left"})]
