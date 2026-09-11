"""Promote approved mining/digging clips and remove the duplicate candidate."""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from anim_format import parse, write  # noqa: E402


ANIM = Path("resources/animated_characters/humanoid.anim")
PROMOTIONS = (
    ("review_mining_wall_strike", "mining_wall_strike", "mining_wall", 0.52),
    ("review_digging_ground", "digging_ground", "digging_ground", 0.50),
)
DUPLICATES = ("review_mining_downward_swing",)


def main() -> None:
    anim = parse(ANIM)
    missing = [source for source, _, _, _ in PROMOTIONS if anim.clip(source) is None]
    if missing:
        raise SystemExit(f"Missing review clips: {', '.join(missing)}")

    for source, target, role, impact_fraction in PROMOTIONS:
        clip = anim.clip(source)
        clip.name = target
        anim.remove_clip(target)
        anim.remove_clip_meta(target)
        anim.remove_clip(source)
        anim.remove_clip_meta(source)
        anim.set_clip(clip)
        anim.set_clip_meta(target, {
            "type": "gathering",
            "interruptible": False,
            "interruptAfter": 1.0,
            "gatheringRole": role,
            "impactFrameFraction": impact_fraction,
        })

    for duplicate in DUPLICATES:
        anim.remove_clip(duplicate)
        anim.remove_clip_meta(duplicate)

    write(anim, ANIM)
    print(f"Promoted {len(PROMOTIONS)} gathering clips; removed {len(DUPLICATES)} duplicate")


if __name__ == "__main__":
    main()
