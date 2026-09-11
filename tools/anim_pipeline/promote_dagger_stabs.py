"""Promote the three approved Mixamo Stabbing clips to the dagger family."""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from anim_format import parse, write  # noqa: E402


ANIM = Path("resources/animated_characters/humanoid.anim")
PROMOTIONS = (
    ("review_dagger1h_stab1", "dagger1h_light1", "light1", 0.50),
    ("review_dagger1h_stab2", "dagger1h_light2", "light2", 0.50),
    ("review_dagger1h_stab3", "dagger1h_light3", "light3", 0.50),
)


def main() -> None:
    anim = parse(ANIM)
    missing = [source for source, _, _, _ in PROMOTIONS if anim.clip(source) is None]
    if missing:
        raise SystemExit(f"Missing review clips: {', '.join(missing)}")

    for source, target, role, hit_fraction in PROMOTIONS:
        clip = anim.clip(source)
        clip.name = target
        anim.remove_clip(target)
        anim.remove_clip_meta(target)
        anim.remove_clip(source)
        anim.remove_clip_meta(source)
        anim.set_clip(clip)
        anim.set_clip_meta(target, {
            "type": "combat",
            "interruptible": False,
            "interruptAfter": 1.0,
            "hitFrameFraction": hit_fraction,
            "meleeFamily": "dagger_1h",
            "meleeRole": role,
        })

    write(anim, ANIM)
    print(f"Promoted {len(PROMOTIONS)} dagger clips in {ANIM}")


if __name__ == "__main__":
    main()
