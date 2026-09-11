"""Split the approved Mixamo one-hand sword combo into gameplay clips.

The source clip is imported by tools/character_import.py as
``sword1h_mocap_combo``. Boundaries are placed at measured recovery valleys;
endpoint samples are synthesized so every clip begins and ends exactly at its
source pose even when the boundary falls between FBX frames.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from anim_format import Channel, Clip, parse, write  # noqa: E402
from anim_lint import lint_clip, sample_position, sample_rotation  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
ANIM = ROOT / "resources" / "animated_characters" / "humanoid.anim"
SOURCE = "sword1h_mocap_combo"

# name, source start, source end, impact time, role
SLICES = [
    ("sword1h_light1", 0.43, 1.37, 1.00, "light1"),
    ("sword1h_light2", 1.33, 2.40, 1.98, "light2"),
    ("sword1h_light3", 2.40, 3.30, 3.00, "light3"),
    ("sword1h_light4", 3.27, 4.53, 3.90, "light4"),
    ("sword1h_heavy",  3.27, 4.53, 3.90, "heavy"),
]


def slice_keys(keys, start, end, sampler):
    if not keys:
        return []
    result = [(0.0, sampler(keys, start))]
    result.extend((t - start, value) for t, value in keys if start < t < end)
    result.append((end - start, sampler(keys, end)))
    return result


def slice_clip(source: Clip, name: str, start: float, end: float) -> Clip:
    channels = []
    for src in source.channels:
        channel = Channel(bone_id=src.bone_id)
        channel.pos_keys = slice_keys(src.pos_keys, start, end, sample_position)
        channel.rot_keys = slice_keys(src.rot_keys, start, end, sample_rotation)
        channel.scale_keys = slice_keys(src.scale_keys, start, end, sample_position)
        channels.append(channel)
    return Clip(name=name, duration=end - start, speed=source.speed,
                root_motion=source.root_motion, channels=channels)


def main():
    af = parse(ANIM)
    source = af.clip(SOURCE)
    if source is None:
        raise SystemExit(f"missing source clip '{SOURCE}' in {ANIM}")

    # Rejected procedural experiments must not linger in the editor or be
    # selected by runtime metadata. A real guard can be added later once the
    # transition/stitching system owns its entry and exit poses.
    for obsolete in ("sword1h_prototype", "sword1h_guard"):
        af.remove_clip(obsolete)
        af.remove_clip_meta(obsolete)

    for name, start, end, impact, role in SLICES:
        clip = slice_clip(source, name, start, end)
        findings = lint_clip(af, clip)
        errors = [message for severity, message in findings if severity == "ERROR"]
        if errors:
            raise SystemExit(f"{name}: lint failed: {'; '.join(errors)}")
        af.set_clip(clip)
        af.set_clip_meta(name, {
            "type": "combat",
            "interruptible": False,
            "interruptAfter": 1.0,
            "hitFrameFraction": (impact - start) / (end - start),
            "meleeFamily": "slash_1h",
            "meleeRole": role,
        })
        print(f"[PASS] {name}: source {start:.2f}-{end:.2f}s, "
              f"duration {clip.duration:.2f}s, hit {(impact-start)/(end-start):.3f}")

    write(af, ANIM)
    print(f"wrote {ANIM}")


if __name__ == "__main__":
    main()
