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

# ============================================================================
# sword_1h flagship moveset (combat Phase B)
# Chain-continuity rule: every link starts AND ends at SWORD_GUARD (the hub);
# strikes land by ~40%, the 60-100% tail is recovery — which is exactly what
# a buffered chain input cancels. The 0.2s crossfade smooths hub re-entry.
# ============================================================================

# Lower-body involvement: Hips rotation pivots the whole body (legs swivel
# like a real back-foot pivot), and HipsOffset dips drop the weight into the
# cut — the engine's foot IK pins the feet so dips read as knee bends.
SWORD_GUARD = {                           # bladed stance: hilt at waist, blade up-forward
    "RightArm": (10.0, 0.0, -40.0),
    "RightForeArm": (5.0, 0.0, -55.0),
    "RightHand": (0.0, 0.0, -8.0),
    "LeftArm": (5.0, 0.0, 18.0),          # lead arm slightly raised
    "Spine": (1.0, -4.0, 0.0),            # subtle bladed twist
    "Spine1": (1.0, -4.0, 0.0),
    "Spine2": (1.0, -3.0, 0.0),
    "Hips": (0.0, -3.0, 0.0),
    "HipsOffset": (0.0, -0.02, 0.0),      # slight ready crouch
}
SWORD_COCK_R = {                          # wind up to the right shoulder
    "RightArm": (-25.0, 0.0, -25.0),
    "RightForeArm": (0.0, 0.0, -48.0),
    "RightHand": (0.0, 0.0, -8.0),
    "LeftArm": (8.0, 0.0, 22.0),
    "Spine": (1.0, -11.0, 0.0),
    "Spine1": (1.0, -11.0, 0.0),
    "Spine2": (1.0, -9.0, 0.0),
    "Head": (0.0, -6.0, 0.0),
    "Hips": (0.0, -8.0, 0.0),             # hips lead the windup
    "HipsOffset": (0.0, -0.03, 0.0),
}
SWORD_SLASH_L = {                         # horizontal cut, follow-through across left
    "RightArm": (40.0, 0.0, -70.0),
    "RightForeArm": (0.0, 0.0, -10.0),
    "RightHand": (0.0, 0.0, -5.0),
    "LeftArm": (0.0, 0.0, 10.0),
    "Spine": (3.0, 9.0, 0.0),
    "Spine1": (3.0, 9.0, 0.0),
    "Spine2": (3.0, 7.0, 0.0),
    "Head": (2.0, 5.0, 0.0),
    "Hips": (0.0, 8.0, 0.0),              # hips rotate through the cut
    "HipsOffset": (0.0, -0.06, 0.0),      # weight drops into it
}
SWORD_COCK_L = {                          # pull across the body for the backhand
    "RightArm": (38.0, 0.0, -38.0),
    "RightForeArm": (8.0, 0.0, -52.0),
    "RightHand": (0.0, 0.0, -8.0),
    "LeftArm": (0.0, 0.0, 12.0),
    "Spine": (2.0, 9.0, 0.0),
    "Spine1": (2.0, 9.0, 0.0),
    "Spine2": (2.0, 7.0, 0.0),
    "Head": (0.0, 5.0, 0.0),
    "Hips": (0.0, 7.0, 0.0),
    "HipsOffset": (0.0, -0.03, 0.0),
}
SWORD_BACKHAND_R = {                      # backhand cut out to the right
    "RightArm": (-32.0, 0.0, -62.0),
    "RightForeArm": (0.0, 0.0, -12.0),
    "RightHand": (0.0, 0.0, -5.0),
    "LeftArm": (6.0, 0.0, 20.0),
    "Spine": (2.0, -10.0, 0.0),
    "Spine1": (2.0, -10.0, 0.0),
    "Spine2": (2.0, -8.0, 0.0),
    "Head": (0.0, -5.0, 0.0),
    "Hips": (0.0, -8.0, 0.0),
    "HipsOffset": (0.0, -0.06, 0.0),
}
SWORD_HIGH_COCK = {                       # blade raised overhead-back
    "RightArm": (-8.0, 0.0, -130.0),
    "RightForeArm": (0.0, 0.0, -32.0),
    "RightHand": (0.0, 0.0, -8.0),
    "LeftArm": (5.0, 0.0, 28.0),
    "Spine": (-4.0, -6.0, 0.0),
    "Spine1": (-4.0, -6.0, 0.0),
    "Spine2": (-3.0, -5.0, 0.0),
    "Head": (-5.0, 0.0, 0.0),
    "Hips": (0.0, -5.0, 0.0),
    "HipsOffset": (0.0, -0.01, 0.0),      # tall at the top of the coil
}
SWORD_CHOP = {                            # overhead cut landing forward-low
    "RightArm": (6.0, 0.0, -52.0),
    "RightForeArm": (0.0, 0.0, -10.0),
    "RightHand": (0.0, 0.0, 6.0),
    "LeftArm": (0.0, 0.0, 8.0),
    "Spine": (8.0, 2.0, 0.0),
    "Spine1": (8.0, 2.0, 0.0),
    "Spine2": (7.0, 2.0, 0.0),
    "Head": (5.0, 0.0, 0.0),
    "Hips": (2.0, 2.0, 0.0),
    "HipsOffset": (0.0, -0.09, 0.0),      # deep drop under the chop
}
SWORD_HEAVY_COCK = {                      # deeper overhead wind with full body coil
    "RightArm": (-15.0, 0.0, -142.0),
    "RightForeArm": (0.0, 0.0, -38.0),
    "RightHand": (0.0, 0.0, -8.0),
    "LeftArm": (8.0, 0.0, 35.0),
    "Spine": (-5.0, -11.0, 0.0),
    "Spine1": (-5.0, -11.0, 0.0),
    "Spine2": (-4.0, -9.0, 0.0),
    "Head": (-7.0, -4.0, 0.0),
    "Hips": (0.0, -9.0, 0.0),
    "HipsOffset": (0.0, -0.02, 0.0),
}
SWORD_HEAVY_IMPACT = {                    # committed full-weight chop
    "RightArm": (10.0, 0.0, -45.0),
    "RightForeArm": (0.0, 0.0, -8.0),
    "RightHand": (0.0, 0.0, 8.0),
    "LeftArm": (-3.0, 0.0, 5.0),
    "Spine": (9.0, 3.0, 0.0),
    "Spine1": (9.0, 3.0, 0.0),
    "Spine2": (8.0, 3.0, 0.0),
    "Head": (6.0, 0.0, 0.0),
    "Hips": (3.0, 3.0, 0.0),
    "HipsOffset": (0.0, -0.12, 0.0),      # full commitment: sink into the blow
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
    "sword_guard": SWORD_GUARD,
    "sword_cock_r": SWORD_COCK_R,
    "sword_slash_l": SWORD_SLASH_L,
    "sword_cock_l": SWORD_COCK_L,
    "sword_backhand_r": SWORD_BACKHAND_R,
    "sword_high_cock": SWORD_HIGH_COCK,
    "sword_chop": SWORD_CHOP,
    "sword_heavy_cock": SWORD_HEAVY_COCK,
    "sword_heavy_impact": SWORD_HEAVY_IMPACT,
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

    # ---- sword_1h flagship moveset (combat Phase B) ----
    # All links start/end at sword_guard; the recovery tail (last ~35%) is the
    # chain window. Block freezes at the guard pose (blockHold 1.0).
    # 7th element = legs_from (mocap lower body): real footwork + weight
    # transfer from the melee mocap clips, time-mapped under the authored arms.
    ("sword1h_guard", 0.5, [
        (0.00, "rest"),
        (0.30, "sword_guard"),
        (0.50, "sword_guard"),
    ], 0.30, "slash_1h", "block", ("body_block", 0.7, 0.9)),

    ("sword1h_light1", 1.0, [
        (0.00, "sword_guard"),
        (0.18, "sword_cock_r"),
        (0.36, "sword_slash_l"),  # the cut
        (0.52, "sword_slash_l"),  # impact hold
        (1.00, "sword_guard"),    # recovery (chain window lives here)
    ], 0.40, "slash_1h", "light1", ("melee_attack_horizontal", 0.55, 1.75)),

    ("sword1h_light2", 1.0, [
        (0.00, "sword_guard"),
        (0.18, "sword_cock_l"),
        (0.36, "sword_backhand_r"),
        (0.52, "sword_backhand_r"),
        (1.00, "sword_guard"),
    ], 0.40, "slash_1h", "light2", ("melee_attack_horizontal", 0.9, 1.9)),

    ("sword1h_light3", 1.15, [
        (0.00, "sword_guard"),
        (0.24, "sword_high_cock"),
        (0.44, "sword_chop"),
        (0.62, "sword_chop"),
        (1.15, "sword_guard"),
    ], 0.42, "slash_1h", "light3", ("melee_attack_down", 0.5, 1.85)),

    ("sword1h_heavy", 1.9, [
        (0.00, "sword_guard"),
        (0.38, "sword_high_cock"),
        (0.62, "sword_heavy_cock"),   # the souls tell: a beat at full coil
        (0.88, "sword_heavy_cock"),
        (1.05, "sword_heavy_impact"),
        (1.35, "sword_heavy_impact"),
        (1.90, "sword_guard"),
    ], 0.57, "slash_1h", "heavy", ("melee_attack_down", 0.15, 2.1)),
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
    for spec in CLIPS:
        name, duration, keys, hit_frame, family, role = spec[:6]
        legs_from = spec[6] if len(spec) > 6 else None
        clip = build_clip(stance, name, duration, keys, POSES, legs_from=legs_from)
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
