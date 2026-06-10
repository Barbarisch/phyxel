"""Generate melee combat clips for humanoid.anim via the pose DSL.

Strategy: REUSE the quality Mixamo mocap that already exists (boxing,
elbow_punch, attack, melee_attack_horizontal, melee_attack_down, body_block —
real weight transfer the DSL can't match) and author only the missing weapon
gestures:

  melee_stab_1h     ~0.9s  — dagger/rapier: cock back -> lunge thrust
  melee_chop_2h      1.3s  — greatsword/maul: overhead -> committed down chop
  melee_sweep_2h     1.4s  — two-hand horizontal sweep right -> left
  melee_thrust_spear 1.0s  — staggered two-hand grip -> drive forward
  melee_parry        0.45s — quick one-hand high guard snap

Weapon -> family mapping lives in resources/rpg_items/anim/melee_anim_families.json
(rule-driven from weapon.damageType + weapon.properties, like the spell system).

Axis conventions (measured via probe_axes.py; deltas on the idle@0 stance,
RIGHT side; the model fronts +Z at yaw 0):
  Arm/ForeArm:  -Z = swing forward/up, +X = across the body, +Y = twist.
  Spine1/2:     +X = bow forward, -Y = right-handed WINDUP (right shoulder
                back), +Y = follow-through. (Z roll unprobed — avoid.)

Usage:
  python tools/anim_pipeline/generate_melee.py            # work copy + lint
  python tools/anim_pipeline/generate_melee.py --splice   # also into humanoid.anim
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from anim_format import parse, write  # noqa: E402
from anim_lint import lint_clip  # noqa: E402
from pose_dsl import BaseStance, build_clip, mirror_pose, merge_poses  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
SOURCE_ANIM = REPO / "resources" / "animated_characters" / "humanoid.anim"
WORK_ANIM = REPO / "resources" / "animated_characters" / "humanoid_casts.anim"
CALIBRATION = Path(__file__).parent / "calibration.json"


# ---------------------------------------------------------------------------
# Poses (deltas on idle@0, Euler degrees, intrinsic XYZ)
# ---------------------------------------------------------------------------

REST = {}

# --- one-hand stab (dagger / rapier) -----------------------------------------
STAB_BACK = {
    "RightArm": (-12.0, 0.0, 8.0),       # upper arm drawn back past the hip
    "RightForeArm": (0.0, 0.0, -65.0),   # blade cocked at the ribs
    "RightHand": (0.0, 0.0, -5.0),
    "LeftArm": (0.0, 0.0, 25.0),         # off-hand forward for balance
    # Torso twist distributed across all three spine bones (real mocap does
    # this — dumping it on Spine1 alone reads as a twitchy snap and trips the
    # per-bone velocity envelope).
    "Spine": (1.0, -10.0, 0.0),
    "Spine1": (1.0, -10.0, 0.0),
    "Spine2": (0.0, -9.0, 0.0),
}
STAB_LUNGE = {
    "RightArm": (5.0, 0.0, -85.0),       # full extension at chest height
    "RightForeArm": (0.0, 0.0, -5.0),
    "RightHand": (0.0, 0.0, -5.0),
    "LeftArm": (0.0, 0.0, -20.0),        # off-hand whips back
    "Spine": (5.0, 8.0, 0.0),            # follow through + lean into it
    "Spine1": (5.0, 8.0, 0.0),
    "Spine2": (5.0, 7.0, 0.0),
    "Head": (4.0, 0.0, 0.0),
}

# --- two-handed overhead chop ------------------------------------------------
_CHOP_UP_R = {
    "RightArm": (-8.0, 0.0, -150.0),     # both hands high overhead
    "RightForeArm": (0.0, 0.0, -25.0),
    "RightHand": (0.0, 0.0, -5.0),
}
CHOP_UP = merge_poses(_CHOP_UP_R, mirror_pose(_CHOP_UP_R), {
    "Spine": (-5.0, 0.0, 0.0),           # arch back under the lifted blade
    "Spine1": (-5.0, 0.0, 0.0),
    "Spine2": (-4.0, 0.0, 0.0),
    "Head": (-8.0, 0.0, 0.0),
})
_CHOP_DOWN_R = {
    "RightArm": (10.0, 0.0, -45.0),      # hands driven to waist height in front
    "RightForeArm": (0.0, 0.0, -12.0),
    "RightHand": (0.0, 0.0, 8.0),
}
CHOP_DOWN = merge_poses(_CHOP_DOWN_R, mirror_pose(_CHOP_DOWN_R), {
    "Spine": (10.0, 0.0, 0.0),           # whole torso commits into the chop
    "Spine1": (10.0, 0.0, 0.0),
    "Spine2": (10.0, 0.0, 0.0),
    "Head": (8.0, 0.0, 0.0),
})

# --- two-handed horizontal sweep ----------------------------------------------
SWEEP_RIGHT = {                           # hands gathered at the right hip
    "RightArm": (-25.0, 0.0, -15.0),     # right arm back-out to the right
    "RightForeArm": (0.0, 0.0, -40.0),
    "LeftArm": (45.0, 0.0, 25.0),        # left arm reaches across to the grip
    "LeftForeArm": (10.0, 0.0, 30.0),
    "Spine": (2.0, -14.0, 0.0),          # deep windup twist, distributed
    "Spine1": (2.0, -14.0, 0.0),
    "Spine2": (2.0, -13.0, 0.0),
    "Head": (0.0, -10.0, 0.0),
}
SWEEP_LEFT = {                            # follow-through across to the left
    "RightArm": (50.0, 0.0, -65.0),      # right arm sweeps across the chest
    "RightForeArm": (0.0, 0.0, -10.0),
    "LeftArm": (-20.0, 0.0, 30.0),       # left arm opens out to the left
    "LeftForeArm": (0.0, 0.0, 10.0),
    "Spine": (3.0, 12.0, 0.0),
    "Spine1": (3.0, 12.0, 0.0),
    "Spine2": (4.0, 12.0, 0.0),
    "Head": (0.0, 8.0, 0.0),
}

# --- spear thrust (staggered two-hand grip) -----------------------------------
SPEAR_GUARD = {
    "RightArm": (-5.0, 0.0, -20.0),      # rear hand low near the hip
    "RightForeArm": (0.0, 0.0, -50.0),
    "LeftArm": (5.0, 0.0, 50.0),         # lead hand forward on the shaft
    "LeftForeArm": (0.0, 0.0, 15.0),
    "Spine": (2.0, -8.0, 0.0),           # right side coiled
    "Spine1": (2.0, -8.0, 0.0),
    "Spine2": (1.0, -7.0, 0.0),
}
SPEAR_THRUST = {
    "RightArm": (5.0, 0.0, -60.0),       # rear hand drives, stays behind the lead
    "RightForeArm": (0.0, 0.0, -8.0),
    "LeftArm": (5.0, 0.0, 70.0),         # lead hand guides at full reach
    "LeftForeArm": (0.0, 0.0, 5.0),
    "Spine": (5.0, 6.0, 0.0),            # lunge + uncoil
    "Spine1": (5.0, 6.0, 0.0),
    "Spine2": (6.0, 5.0, 0.0),
    "Head": (3.0, 0.0, 0.0),
}

# --- parry (one-hand high guard) -----------------------------------------------
PARRY_GUARD = {
    "RightArm": (18.0, 0.0, -60.0),      # blade raised across the front
    "RightForeArm": (35.0, 0.0, -55.0),
    "RightHand": (0.0, 0.0, -8.0),
    "Spine1": (4.0, -6.0, 0.0),          # slight brace
    "Head": (4.0, 0.0, 0.0),
}

POSES = {
    "rest": REST,
    "stab_back": STAB_BACK,
    "stab_lunge": STAB_LUNGE,
    "chop_up": CHOP_UP,
    "chop_down": CHOP_DOWN,
    "sweep_right": SWEEP_RIGHT,
    "sweep_left": SWEEP_LEFT,
    "spear_guard": SPEAR_GUARD,
    "spear_thrust": SPEAR_THRUST,
    "parry_guard": PARRY_GUARD,
}


# ---------------------------------------------------------------------------
# Authored clip specs: (name, duration, keys, hitFrame, family, role)
# ---------------------------------------------------------------------------

CLIPS = [
    ("melee_stab_1h", 0.9, [
        (0.00, "rest"),
        (0.25, "stab_back"),
        (0.34, "stab_back"),     # beat before the lunge
        (0.50, "stab_lunge"),
        (0.64, "stab_lunge"),    # hold at full extension
        (0.90, "rest"),
    ], 0.52, "stab_1h", "primary"),

    ("melee_chop_2h", 1.3, [
        (0.00, "rest"),
        (0.45, "chop_up"),
        (0.62, "chop_up"),       # heave at the top
        (0.78, "chop_down"),     # fast committed chop
        (1.00, "chop_down"),
        (1.30, "rest"),
    ], 0.58, "two_handed", "primary"),

    ("melee_sweep_2h", 1.4, [
        (0.00, "rest"),
        (0.45, "sweep_right"),
        (0.60, "sweep_right"),   # coiled
        (0.83, "sweep_left"),    # the sweep itself
        (1.05, "sweep_left"),
        (1.40, "rest"),
    ], 0.54, "two_handed", "secondary"),

    ("melee_thrust_spear", 1.0, [
        (0.00, "rest"),
        (0.30, "spear_guard"),
        (0.42, "spear_guard"),
        (0.55, "spear_thrust"),
        (0.70, "spear_thrust"),
        (1.00, "rest"),
    ], 0.57, "spear", "primary"),

    ("melee_parry", 0.45, [
        (0.00, "rest"),
        (0.12, "parry_guard"),   # fast snap up
        (0.33, "parry_guard"),
        (0.45, "rest"),
    ], 0.27, "stab_1h", "block"),
]

# Reused Mixamo mocap clips: tag with family/role + hit frames via clip_meta
# only (no key data is touched). Hit fractions eyeballed from storyboards.
REUSED_META = [
    # (clip, hitFrame, family, role)
    ("boxing",                  0.30, "unarmed",    "primary"),
    ("elbow_punch",             0.40, "unarmed",    "secondary"),
    ("attack",                  0.40, "slash_1h",   "quick"),
    ("melee_attack_horizontal", 0.42, "slash_1h",   "primary"),
    ("melee_attack_down",       0.42, "slash_1h",   "secondary"),
    ("body_block",              0.10, "unarmed",    "block"),
]


def main(argv=None):
    ap = argparse.ArgumentParser(description="Generate melee combat clips")
    ap.add_argument("--splice", action="store_true",
                    help="also splice into the production humanoid.anim")
    ap.add_argument("--out", default=str(WORK_ANIM))
    args = ap.parse_args(argv)

    af = parse(SOURCE_ANIM)
    stance = BaseStance(af, ref_clip="idle", ref_time=0.0)

    calibration = None
    if CALIBRATION.exists():
        calibration = json.loads(CALIBRATION.read_text(encoding="utf-8"))

    failed = False
    for name, duration, keys, hit_frame, family, role in CLIPS:
        clip = build_clip(stance, name, duration, keys, POSES)
        af.set_clip(clip)
        af.set_clip_meta(name, {
            "type": "combat", "interruptible": False, "interruptAfter": 1.0,
            "hitFrameFraction": hit_frame,
            "meleeFamily": family, "meleeRole": role,
        })
        findings = lint_clip(af, clip, calibration)
        errors = [f for f in findings if f[0] == "ERROR"]
        warns = [f for f in findings if f[0] == "WARN"]
        status = "FAIL" if errors else ("WARN" if warns else "PASS")
        print(f"[{status}] {name} ({duration:.2f}s)")
        for sev, msg in findings[:10]:
            print(f"    {sev}: {msg}")
        if errors:
            failed = True

    for name, hit_frame, family, role in REUSED_META:
        if af.clip(name) is None:
            print(f"  (reused clip '{name}' not found — meta skipped)")
            continue
        meta = af.clip_meta(name) or {"type": "combat"}
        meta.update({"hitFrameFraction": hit_frame,
                     "meleeFamily": family, "meleeRole": role})
        af.set_clip_meta(name, meta)
        print(f"[META] {name}: family={family} role={role} hit={hit_frame}")

    write(af, args.out)
    print(f"wrote {args.out}")

    if args.splice:
        if failed:
            print("NOT splicing into humanoid.anim: lint errors present")
            return 1
        write(af, SOURCE_ANIM)
        print(f"spliced into {SOURCE_ANIM}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
