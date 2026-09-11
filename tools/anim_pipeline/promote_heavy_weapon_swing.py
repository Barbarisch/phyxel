"""Promote the approved Heavy Weapon Swing review clip."""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from anim_format import parse, write  # noqa: E402


ANIM = Path("resources/animated_characters/humanoid.anim")
SOURCE = "review_blunt2h_heavy_weapon_swing"
TARGET = "blunt2h_heavy"


def main() -> None:
    anim = parse(ANIM)
    clip = anim.clip(SOURCE)
    if clip is None:
        raise SystemExit(f"Missing review clip: {SOURCE}")
    clip.name = TARGET
    anim.remove_clip(TARGET)
    anim.remove_clip_meta(TARGET)
    anim.remove_clip(SOURCE)
    anim.remove_clip_meta(SOURCE)
    anim.set_clip(clip)
    anim.set_clip_meta(TARGET, {
        "type": "combat",
        "interruptible": False,
        "interruptAfter": 1.0,
        "hitFrameFraction": 0.50,
        "meleeFamily": "blunt_2h",
        "meleeRole": "heavy",
    })
    write(anim, ANIM)
    print(f"Promoted {SOURCE} -> {TARGET}")


if __name__ == "__main__":
    main()
