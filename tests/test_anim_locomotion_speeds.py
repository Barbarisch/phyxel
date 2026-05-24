"""Regression guard: locomotion clips in humanoid.anim must keep authored Speed.

The engine in ``AnimatedVoxelCharacter::Update`` reads ``clips[i].speed`` to
drive forward/strafe distance per second. If the Speed line is missing the
engine silently falls back to hardcoded constants (Walk=2.0, Run=5.0/8.0),
which feels noticeably wrong. A previous Mixamo batch-re-import dropped the
Speed lines on ``walk`` and ``run`` — this test catches that class of
regression at commit time.
"""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from anim_editor import parse_anim_file  # type: ignore  # noqa: E402


# Clips that drive root-motion translation. Each MUST have a Speed line or
# the engine will pick its hardcoded fallback and movement will feel off.
LOCOMOTION_CLIPS = {
    "walk", "run", "unarmed_walk", "crouched_walking", "walking_backward",
    "fast_run",
    "left_strafe_walk", "right_strafe_walk", "left_strafe", "right_strafe",
}


def test_humanoid_locomotion_clips_have_speed():
    m = parse_anim_file(str(ROOT / "resources" / "animated_characters" / "humanoid.anim"))
    present = {c.name for c in m.clips}
    missing_speed = []
    for clip in m.clips:
        if clip.name in LOCOMOTION_CLIPS and not clip.speed:
            missing_speed.append(clip.name)
    assert not missing_speed, (
        f"locomotion clips with no Speed line: {missing_speed}. "
        f"Engine will fall back to hardcoded constants and movement will feel wrong."
    )
    # Sanity floor on the two most-used clips so a zero-speed import is caught
    # even if someone writes `Speed 0`:
    by_name = {c.name: c for c in m.clips}
    if "walk" in by_name:
        assert by_name["walk"].speed and by_name["walk"].speed > 0.5, \
            f"walk Speed={by_name['walk'].speed} is implausibly low"
    if "run" in by_name:
        assert by_name["run"].speed and by_name["run"].speed > 1.5, \
            f"run Speed={by_name['run'].speed} is implausibly low"
