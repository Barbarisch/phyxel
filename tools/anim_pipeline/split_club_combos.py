"""Create review-only attack cuts from the approved club combo sources."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from anim_format import Channel, Clip, parse, write  # noqa: E402
from anim_lint import lint_clip, sample_position, sample_rotation  # noqa: E402


ROOT = Path(__file__).resolve().parents[2]
ANIM = ROOT / "resources" / "animated_characters" / "humanoid.anim"

# source, output, start, end, measured motion peak
SLICES = (
    ("blunt1h_mocap_combo", "review_blunt1h_light1", 0.00, 1.10, 0.67),
    ("blunt1h_mocap_combo", "review_blunt1h_light2", 1.03, 1.90, 1.47),
    ("blunt1h_mocap_combo", "review_blunt1h_light3", 1.83, 2.57, 2.27),
    ("blunt1h_mocap_combo", "review_blunt1h_light4", 2.50, 3.63, 2.80),
    ("blunt2h_mocap_combo", "review_blunt2h_light1", 0.93, 2.43, 1.93),
    ("blunt2h_mocap_combo", "review_blunt2h_light2", 2.37, 3.57, 2.93),
    ("blunt2h_mocap_combo", "review_blunt2h_light3", 3.50, 4.57, 4.20),
    ("blunt2h_mocap_combo", "review_blunt2h_light4", 4.50, 5.43, 4.87),
)


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


def main() -> None:
    anim = parse(ANIM)
    for source_name, output, start, end, impact in SLICES:
        source = anim.clip(source_name)
        if source is None:
            raise SystemExit(f"Missing source clip: {source_name}")
        clip = slice_clip(source, output, start, end)
        errors = [message for severity, message in lint_clip(anim, clip)
                  if severity == "ERROR"]
        if errors:
            raise SystemExit(f"{output}: lint failed: {'; '.join(errors)}")
        anim.set_clip(clip)
        anim.set_clip_meta(output, {
            "type": "combat_review",
            "interruptible": False,
            "interruptAfter": 1.0,
            "hitFrameFraction": (impact - start) / (end - start),
        })
        print(f"[PASS] {output}: {start:.2f}-{end:.2f}s")
    write(anim, ANIM)
    print(f"wrote {ANIM}")


if __name__ == "__main__":
    main()
