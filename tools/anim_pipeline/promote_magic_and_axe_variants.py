"""Promote approved magic and axe variants; remove the rejected magic idle."""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from anim_format import parse, write  # noqa: E402

ANIM = Path("resources/animated_characters/humanoid.anim")

MAGIC = (
    ("review_cast_main_1h_spell1", "cast_1h_spell1", "one_hand", "cast"),
    ("review_cast_main_1h_attack1", "cast_1h_attack1", "one_hand", "attack1"),
    ("review_cast_main_1h_attack2", "cast_1h_attack2", "one_hand", "attack2"),
    ("review_cast_main_1h_attack3", "cast_1h_attack3", "one_hand", "attack3"),
    ("review_cast_main_2h_spell1", "cast_2h_spell1", "two_hand", "cast"),
    ("review_cast_main_2h_area1", "cast_2h_area1", "area", "attack1"),
    ("review_cast_main_2h_area2", "cast_2h_area2", "area", "attack2"),
    ("review_cast_main_2h_attack1", "cast_2h_attack1", "two_hand", "attack1"),
    ("review_cast_main_2h_attack2", "cast_2h_attack2", "two_hand", "attack2"),
    ("review_cast_main_2h_attack3", "cast_2h_attack3", "two_hand", "attack3"),
    ("review_cast_main_2h_attack4", "cast_2h_attack4", "two_hand", "attack4"),
    ("review_cast_main_2h_attack5", "cast_2h_attack5", "two_hand", "attack5"),
)

AXE = (
    ("review_axe1h_backhand", "axe1h_uppercut", "axe_1h", "uppercut"),
    ("review_axe1h_spin_high", "axe1h_spin_high", "axe_1h", "spin_high"),
    ("review_axe1h_spin_low", "axe1h_spin_low", "axe_1h", "spin_low"),
    ("review_axe2h_combo1", "axe2h_mocap_combo1", "axe_2h", "source_combo"),
    ("review_axe1h_combo2", "axe1h_mocap_combo2", "axe_1h", "source_combo2"),
    ("review_axe1h_combo3", "axe1h_mocap_combo3", "axe_1h", "source_combo3"),
)


def promote(anim, source, target, meta):
    clip = anim.clip(source)
    if clip is None:
        raise SystemExit(f"Missing approved review clip: {source}")
    clip.name = target
    anim.remove_clip(target)
    anim.remove_clip_meta(target)
    anim.remove_clip(source)
    anim.remove_clip_meta(source)
    anim.set_clip(clip)
    anim.set_clip_meta(target, meta)


def main() -> None:
    anim = parse(ANIM)
    for source, target, family, role in MAGIC:
        promote(anim, source, target, {
            "type": "cast_variant", "interruptible": False,
            "interruptAfter": 1.0, "castFamily": family,
            "castRole": role, "releaseFrame": 0.70,
        })
    for source, target, family, role in AXE:
        promote(anim, source, target, {
            "type": "combat_source", "interruptible": False,
            "interruptAfter": 1.0, "meleeFamily": family,
            "meleeRole": role,
        })
    anim.remove_clip("review_cast_lite_idle2")
    anim.remove_clip_meta("review_cast_lite_idle2")
    write(anim, ANIM)
    print(f"Promoted {len(MAGIC)} magic and {len(AXE)} axe variants; removed magic idle")


if __name__ == "__main__":
    main()
