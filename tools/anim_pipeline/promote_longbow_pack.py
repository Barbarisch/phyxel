"""Promote the approved Longbow Aiming Pack review clips."""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from anim_format import parse, write  # noqa: E402


ANIM = Path("resources/animated_characters/humanoid.anim")
PROMOTIONS = (
    ("review_bow_idle", "bow_idle", "idle"),
    ("review_bow_draw", "bow_draw", "draw"),
    ("review_bow_aim_overdraw", "bow_aim_overdraw", "aim"),
    ("review_bow_release_recoil", "bow_release_recoil", "release"),
    ("review_bow_aim_walk_forward", "bow_aim_walk_forward", "walk_forward"),
    ("review_bow_aim_walk_back", "bow_aim_walk_back", "walk_back"),
    ("review_bow_aim_walk_left", "bow_aim_walk_left", "walk_left"),
    ("review_bow_aim_walk_right", "bow_aim_walk_right", "walk_right"),
)


def main() -> None:
    anim = parse(ANIM)
    missing = [source for source, _, _ in PROMOTIONS if anim.clip(source) is None]
    if missing:
        raise SystemExit(f"Missing review clips: {', '.join(missing)}")

    for source, target, role in PROMOTIONS:
        clip = anim.clip(source)
        clip.name = target
        anim.remove_clip(target)
        anim.remove_clip_meta(target)
        anim.remove_clip(source)
        anim.remove_clip_meta(source)
        anim.set_clip(clip)
        anim.set_clip_meta(target, {
            "type": "combat",
            "interruptible": role in {"idle", "aim"} or role.startswith("walk_"),
            "interruptAfter": 0.0 if role in {"idle", "aim"} or role.startswith("walk_") else 1.0,
            "weaponFamily": "bow",
            "weaponRole": role,
        })

    write(anim, ANIM)
    print(f"Promoted {len(PROMOTIONS)} bow clips in {ANIM}")


if __name__ == "__main__":
    main()
