"""Promote the approved Great Sword Pack review clips to slash_2h clips."""

from copy import deepcopy
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from anim_format import parse, write  # noqa: E402


ANIM = Path("resources/animated_characters/humanoid.anim")

PROMOTIONS = {
    "review_slash2h_idle": ("slash2h_idle", "idle", 0.50),
    "review_slash2h_slash1": ("slash2h_light1", "light1", 0.55),
    "review_slash2h_slash2": ("slash2h_light2", "light2", 0.55),
    "review_slash2h_slash3": ("slash2h_light3", "light3", 0.55),
    "review_slash2h_slash4": ("slash2h_light4", "light4", 0.55),
    "review_slash2h_slash5": ("slash2h_light5", "light5", 0.55),
    "review_slash2h_attack": ("slash2h_heavy", "heavy", 0.58),
    "review_slash2h_block1": ("slash2h_block1", "block1", 0.50),
    "review_slash2h_block2": ("slash2h_block2", "block", 0.50),
    "review_slash2h_block3": ("slash2h_block3", "block3", 0.50),
}


def main() -> None:
    anim = parse(ANIM)
    missing = [name for name in PROMOTIONS if anim.clip(name) is None]
    if missing:
        raise SystemExit(f"Missing review clips: {', '.join(missing)}")

    for review_name, (production_name, role, hit_fraction) in PROMOTIONS.items():
        promoted = deepcopy(anim.clip(review_name))
        promoted.name = production_name
        anim.set_clip(promoted)
        anim.set_clip_meta(production_name, {
            "type": "combat",
            "interruptible": False,
            "interruptAfter": 1.0,
            "hitFrameFraction": hit_fraction,
            "meleeFamily": "slash_2h",
            "meleeRole": role,
        })
        anim.remove_clip(review_name)
        anim.remove_clip_meta(review_name)

    write(anim, ANIM)
    print(f"Promoted {len(PROMOTIONS)} approved slash_2h clips in {ANIM}")


if __name__ == "__main__":
    main()
