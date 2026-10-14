"""A0 #9 (docs/AnimationSystemV3Plan.md §1.3): no debug spam in the character runtime.

AnimatedVoxelCharacter.cpp printed `DEBUG: Selected TargetAnim` to std::cout every 30
frames from a static counter shared by ALL characters, and logged at INFO level inside
the IK solver per call and per frame ("IK_geo", "StairIK", "StepIK" — cf. the old 1.1 GB
TerrainIK log). Runtime diagnostics go through LOG_TRACE/LOG_DEBUG, never std::cout and
never INFO on a per-frame path.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AVC = ROOT / "engine" / "src" / "scene" / "AnimatedVoxelCharacter.cpp"
PER_FRAME_INFO_TAGS = ("IK_geo", "StairIK", "StepIK")


def _code_lines():
    for n, line in enumerate(AVC.read_text(encoding="utf-8").splitlines(), 1):
        stripped = line.strip()
        if stripped.startswith("//"):
            continue
        yield n, line


def test_no_std_cout_in_character_runtime():
    hits = [f"{n}: {l.strip()}" for n, l in _code_lines() if "std::cout" in l]
    assert not hits, "std::cout in AnimatedVoxelCharacter.cpp:\n" + "\n".join(hits)


def test_no_info_level_logging_on_per_frame_ik_paths():
    pat = re.compile(r'LOG_INFO(?:_FMT)?\(\s*"(' + "|".join(PER_FRAME_INFO_TAGS) + r')"')
    hits = [f"{n}: {l.strip()}" for n, l in _code_lines() if pat.search(l)]
    assert not hits, "per-frame INFO logging in the IK path:\n" + "\n".join(hits)
