"""Promote approved club combo takes while retaining them intact as sources."""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from anim_format import parse, write  # noqa: E402


ANIM = Path("resources/animated_characters/humanoid.anim")
PROMOTIONS = (
    ("review_blunt1h_club_combo", "blunt1h_mocap_combo", "blunt_1h"),
    ("review_blunt2h_club_combo", "blunt2h_mocap_combo", "blunt_2h"),
)


def main() -> None:
    anim = parse(ANIM)
    missing = [source for source, _, _ in PROMOTIONS if anim.clip(source) is None]
    if missing:
        raise SystemExit(f"Missing approved review clips: {', '.join(missing)}")

    for source, target, family in PROMOTIONS:
        clip = anim.clip(source)
        clip.name = target
        anim.remove_clip(target)
        anim.remove_clip_meta(target)
        anim.remove_clip(source)
        anim.remove_clip_meta(source)
        anim.set_clip(clip)
        anim.set_clip_meta(target, {
            "type": "combat_source",
            "interruptible": False,
            "interruptAfter": 1.0,
            "meleeFamily": family,
            "meleeRole": "source_combo",
        })

    write(anim, ANIM)
    print(f"Promoted {len(PROMOTIONS)} intact club combo sources")


if __name__ == "__main__":
    main()
