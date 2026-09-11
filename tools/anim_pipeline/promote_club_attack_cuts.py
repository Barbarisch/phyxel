"""Promote approved one- and two-handed club attack cuts."""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from anim_format import parse, write  # noqa: E402


ANIM = Path("resources/animated_characters/humanoid.anim")
PROMOTIONS = tuple(
    (f"review_blunt1h_light{i}", f"blunt1h_light{i}", "blunt_1h", f"light{i}")
    for i in range(1, 5)
) + tuple(
    (f"review_blunt2h_light{i}", f"blunt2h_light{i}", "blunt_2h", f"light{i}")
    for i in range(1, 5)
)


def main() -> None:
    anim = parse(ANIM)
    missing = [source for source, _, _, _ in PROMOTIONS if anim.clip(source) is None]
    if missing:
        raise SystemExit(f"Missing approved review clips: {', '.join(missing)}")

    for source, target, family, role in PROMOTIONS:
        clip = anim.clip(source)
        clip.name = target
        review_meta = anim.clip_meta(source) or {}
        hit_fraction = review_meta.get("hitFrameFraction", "0.5")
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
            "meleeFamily": family,
            "meleeRole": role,
        })

    write(anim, ANIM)
    print(f"Promoted {len(PROMOTIONS)} club attack cuts")


if __name__ == "__main__":
    main()
