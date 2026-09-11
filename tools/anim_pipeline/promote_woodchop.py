"""Promote the approved wood-chopping clip and reject the combat-like candidate."""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from anim_format import parse, write  # noqa: E402


ANIM = Path("resources/animated_characters/humanoid.anim")
SOURCE = "review_woodchop_horizontal"
TARGET = "woodchop_horizontal"
REJECTED = "review_woodchop_backhand"


def main() -> None:
    anim = parse(ANIM)
    clip = anim.clip(SOURCE)
    if clip is None:
        raise SystemExit(f"Missing approved review clip: {SOURCE}")

    clip.name = TARGET
    anim.remove_clip(TARGET)
    anim.remove_clip_meta(TARGET)
    anim.remove_clip(SOURCE)
    anim.remove_clip_meta(SOURCE)
    anim.set_clip(clip)
    anim.set_clip_meta(TARGET, {
        "type": "gathering",
        "interruptible": False,
        "interruptAfter": 1.0,
        "gatheringRole": "woodchop",
        "impactFrameFraction": 0.50,
    })

    anim.remove_clip(REJECTED)
    anim.remove_clip_meta(REJECTED)
    write(anim, ANIM)
    print(f"Promoted {SOURCE} -> {TARGET}; removed {REJECTED}")


if __name__ == "__main__":
    main()
