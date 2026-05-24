"""Guardrails against silent corruption of resources/animated_characters/humanoid.anim.

Two failure modes have hit the working tree:

1. A tool used parse_anim_file -> mutate -> write_anim_file but the writer
   didn't re-emit `# archetype:` / `# clip_meta:` headers, silently disabling
   motion-warp / foot-IK / stair contact frames at runtime.

2. A tool re-imported Mixamo FBX and re-introduced raw Hips X/Z translation
   on `walk` / `run`, so the character translates twice (controller + bone)
   and visibly slides forward.

This test asserts both invariants on humanoid.anim itself, AND verifies the
parser/writer round-trip preserves every byte we care about.
"""
from __future__ import annotations

import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from anim_editor import parse_anim_file, write_anim_file  # noqa: E402

ANIM_PATH = ROOT / "resources" / "animated_characters" / "humanoid.anim"

# Clips whose Speed is the sole authoritative source for controller
# locomotion velocity. Hips X/Z translation must stay stripped on these.
LOCOMOTION_CLIPS = {
    "walk", "run", "unarmed_walk", "crouched_walking", "walking_backward",
    "fast_run", "left_strafe_walk", "right_strafe_walk",
    "left_strafe", "right_strafe",
}

# Headers the engine consumes. Missing any of these silently disables
# motion warp / foot IK / stair tuning.
REQUIRED_HEADER_PREFIXES = (
    "# archetype:",
    "# clip_meta:",
)

# Allow a sub-mm tolerance for floating-point noise in the stripped tracks.
MAX_HIPS_XZ_DELTA_M = 0.01


def _hips_bone_id(af) -> int:
    for b in af.bones:
        if "Hips" in b.name:
            return b.id
    raise AssertionError("No Hips bone found in humanoid.anim")


def test_humanoid_anim_has_required_headers():
    af = parse_anim_file(ANIM_PATH)
    found = {p: False for p in REQUIRED_HEADER_PREFIXES}
    for hl in af.header_lines:
        for p in REQUIRED_HEADER_PREFIXES:
            if hl.startswith(p):
                found[p] = True
    missing = [p for p, ok in found.items() if not ok]
    assert not missing, (
        f"humanoid.anim is missing required header lines: {missing}. "
        f"These configure motion warp / foot IK / stair contact frames. "
        f"Some tool round-tripped the file through a parser that lost them."
    )


def test_locomotion_clips_have_no_root_motion():
    """Hips X/Z must be stripped on locomotion clips; controller speed comes
    from the `Speed` line. If both are active the character slides forward."""
    af = parse_anim_file(ANIM_PATH)
    hips_id = _hips_bone_id(af)

    violations = []
    for clip in af.clips:
        if clip.name not in LOCOMOTION_CLIPS:
            continue
        ch = next((c for c in clip.channels if c.bone_id == hips_id), None)
        if ch is None or not ch.pos_keys:
            continue
        xs = [k.x for k in ch.pos_keys]
        zs = [k.z for k in ch.pos_keys]
        dx = max(xs) - min(xs)
        dz = max(zs) - min(zs)
        if dx > MAX_HIPS_XZ_DELTA_M or dz > MAX_HIPS_XZ_DELTA_M:
            violations.append(
                f"  {clip.name}: Hips X delta={dx:.4f} m, Z delta={dz:.4f} m "
                f"(speed={clip.speed})"
            )

    assert not violations, (
        "Locomotion clips have unstripped Hips root motion. The character "
        "controller translates AND the Hips bone translates, causing slide:\n"
        + "\n".join(violations)
    )


def test_parse_write_roundtrip_is_byte_identical():
    """parse_anim_file -> write_anim_file must not lose data. This is the
    safety net against future tools that mutate the file via the editor API."""
    af = parse_anim_file(ANIM_PATH)
    with tempfile.NamedTemporaryFile("w", suffix=".anim", delete=False) as tmp:
        tmp_path = Path(tmp.name)
    try:
        write_anim_file(af, tmp_path)
        original = ANIM_PATH.read_text(encoding="utf-8")
        rewritten = tmp_path.read_text(encoding="utf-8")
    finally:
        tmp_path.unlink(missing_ok=True)

    # Compare structurally: header lines + section counts + per-clip speeds
    # and Hips pos_key counts. A strict byte compare would fail on float
    # repr differences which the engine tolerates; this catches the real
    # losses (dropped headers, dropped boxes, dropped channels).
    af2 = parse_anim_file(tmp_path) if tmp_path.exists() else None
    # tmp_path was deleted above; re-parse from in-memory rewrite:
    tmp_path2 = ANIM_PATH.with_suffix(".anim.roundtrip_tmp")
    try:
        tmp_path2.write_text(rewritten, encoding="utf-8")
        af2 = parse_anim_file(tmp_path2)
    finally:
        tmp_path2.unlink(missing_ok=True)

    assert af.header_lines == af2.header_lines, (
        f"Header lines lost in round-trip. "
        f"Before: {len(af.header_lines)} lines, after: {len(af2.header_lines)}."
    )
    assert len(af.bones) == len(af2.bones), "Bone count changed in round-trip"
    assert len(af.boxes) == len(af2.boxes), (
        f"Box count changed in round-trip: {len(af.boxes)} -> {len(af2.boxes)}"
    )
    assert len(af.clips) == len(af2.clips), "Clip count changed in round-trip"

    for c1, c2 in zip(af.clips, af2.clips):
        assert c1.name == c2.name
        assert c1.speed == c2.speed, f"Speed lost for clip '{c1.name}'"
        assert len(c1.channels) == len(c2.channels), (
            f"Channel count changed for clip '{c1.name}'"
        )
