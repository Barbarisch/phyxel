"""Generate D&D spell-casting clips for humanoid.anim via the pose DSL.

Produces (durations map to D&D casting economy):
  cast_quick    ~0.8s  — cantrip / bonus-action: one-hand raise + flick
  cast_standard ~1.6s  — one-action spell: two-hand gather -> charge -> thrust
  cast_windup    0.8s  — enter channeling stance (long casts / rituals)
  cast_loop      1.2s  — looping channel hold (repeat N times for cast time)
  cast_release   0.7s  — channel stance -> thrust release

Each clip gets a clip_meta header with releaseFrame (normalized time when the
spell VFX/projectile should fire) and hitFrameFraction mirroring it (the field
the engine already parses for combat timing).

Axis conventions (measured via probe_axes.py + get_bone_positions, then
corrected by live user observation — the character FACES +Z, so the probe's
"+Z moved the forearm to -Z" means +Z swings the arm BACKWARD):
  RightArm / RightForeArm:  -Z = swing forward/up (sagittal — the casting axis)
                            +X = swing across the body (adduction)
                            +Y = twist along the bone
  Spine/Head: small angles only (axes unverified); verify visually.

Usage:
  python tools/anim_pipeline/generate_casts.py            # writes humanoid_casts.anim + lints
  python tools/anim_pipeline/generate_casts.py --splice   # also splice into humanoid.anim
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
# Poses (deltas on the idle@0 stance, Euler degrees, intrinsic XYZ)
# ---------------------------------------------------------------------------

REST = {}

# --- one-handed quick cast -------------------------------------------------
QUICK_RAISE = {
    "RightArm": (0.0, 0.0, -65.0),      # arm sweeps up-forward
    "RightForeArm": (5.0, 0.0, -35.0),  # elbow cocked
    "RightHand": (0.0, 0.0, -10.0),
    "Spine1": (2.0, 0.0, 0.0),
}
QUICK_FLICK = {
    "RightArm": (0.0, 0.0, -92.0),      # arm extended forward at shoulder height
    "RightForeArm": (0.0, 0.0, -6.0),   # elbow snaps straight
    "RightHand": (0.0, 0.0, 12.0),      # wrist flick
    "Spine1": (4.0, 0.0, 0.0),
}

# --- two-handed standard cast ------------------------------------------------
_GATHER_R = {
    "RightArm": (18.0, 0.0, -35.0),     # upper arms forward + pulled inward
    "RightForeArm": (15.0, 0.0, -85.0), # elbows folded: hands meet at chest
    "RightHand": (0.0, 0.0, -5.0),
}
GATHER = merge_poses(_GATHER_R, mirror_pose(_GATHER_R), {
    "Spine1": (5.0, 0.0, 0.0),          # slight bow over the gathered hands
    "Spine2": (4.0, 0.0, 0.0),
    "Head": (5.0, 0.0, 0.0),
})

_CHARGE_R = {
    "RightArm": (-20.0, 0.0, -115.0),   # arms high and spread, charging
    "RightForeArm": (0.0, 0.0, -25.0),
    "RightHand": (0.0, 0.0, -5.0),
}
CHARGE = merge_poses(_CHARGE_R, mirror_pose(_CHARGE_R), {
    "Spine1": (-5.0, 0.0, 0.0),         # chest open, leaning back slightly
    "Spine2": (-4.0, 0.0, 0.0),
    "Head": (-6.0, 0.0, 0.0),           # looking up at the charge
})

_THRUST_R = {
    "RightArm": (5.0, 0.0, -88.0),      # both palms thrust straight forward
    "RightForeArm": (0.0, 0.0, -8.0),
    "RightHand": (0.0, 0.0, 8.0),
}
THRUST = merge_poses(_THRUST_R, mirror_pose(_THRUST_R), {
    "Spine1": (6.0, 0.0, 0.0),          # weight into the thrust
    "Spine2": (5.0, 0.0, 0.0),
})

# --- channeling stance (windup / loop / release) -----------------------------
_CHANNEL_R = {
    "RightArm": (8.0, 0.0, -55.0),      # hands forward at mid height
    "RightForeArm": (5.0, 0.0, -45.0),
    "RightHand": (0.0, 0.0, -5.0),
}
CHANNEL = merge_poses(_CHANNEL_R, mirror_pose(_CHANNEL_R), {
    "Spine1": (3.0, 0.0, 0.0),
    "Head": (3.0, 0.0, 0.0),
})
# subtle sway variants for the loop (small deltas around CHANNEL)
CHANNEL_A = merge_poses(CHANNEL, {
    "RightArm": (10.0, 0.0, -60.0),
    "LeftArm": (-6.0, 0.0, 50.0),
    "Spine1": (4.0, 2.0, 0.0),
})
CHANNEL_B = merge_poses(CHANNEL, {
    "RightArm": (6.0, 0.0, -50.0),
    "LeftArm": (-10.0, 0.0, 60.0),
    "Spine1": (4.0, -2.0, 0.0),
})

# --- call_down family (sacred_flame, shatter, mass_healing_word, ...) -------
_SKY_R = {
    "RightArm": (-12.0, 0.0, -145.0),   # arms overhead, reaching to the sky
    "RightForeArm": (0.0, 0.0, -15.0),
    "RightHand": (0.0, 0.0, -5.0),
}
SKY_REACH = merge_poses(_SKY_R, mirror_pose(_SKY_R), {
    "Spine1": (-7.0, 0.0, 0.0),         # lean back, chest open
    "Spine2": (-5.0, 0.0, 0.0),
    "Head": (-10.0, 0.0, 0.0),          # looking up
})
CALL_STRIKE = {
    "RightArm": (0.0, 0.0, -68.0),      # right arm sweeps down to point at target
    "RightForeArm": (0.0, 0.0, -8.0),
    "RightHand": (0.0, 0.0, 10.0),
    "LeftArm": (-8.0, 0.0, 95.0),       # left stays half-raised
    "LeftForeArm": (0.0, 0.0, 20.0),
    "Spine1": (6.0, 0.0, 0.0),          # weight forward into the command
    "Spine2": (4.0, 0.0, 0.0),
    "Head": (4.0, 0.0, 0.0),
}

# --- touch family (cure_wounds, shocking_grasp, fly) -------------------------
TOUCH_REACH = {
    "RightArm": (5.0, 0.0, -78.0),      # one palm extended at chest height
    "RightForeArm": (0.0, 0.0, -12.0),
    "RightHand": (0.0, 0.0, -10.0),
    "LeftArm": (3.0, 0.0, 12.0),        # off-hand drawn slightly back
    "Spine1": (6.0, 0.0, 0.0),          # lean in toward the target
    "Spine2": (4.0, 0.0, 0.0),
    "Head": (3.0, 0.0, 0.0),
}

# --- ward family (shield, counterspell, misty_step) ---------------------------
_WARD_R = {
    "RightArm": (28.0, 0.0, -48.0),     # forearms snap crossed before the chest
    "RightForeArm": (22.0, 0.0, -72.0),
    "RightHand": (0.0, 0.0, -5.0),
}
WARD_CROSS = merge_poses(_WARD_R, mirror_pose(_WARD_R), {
    "Spine1": (4.0, 0.0, 0.0),          # braced
    "Head": (5.0, 0.0, 0.0),            # chin tucked
})

POSES = {
    "rest": REST,
    "quick_raise": QUICK_RAISE,
    "quick_flick": QUICK_FLICK,
    "gather": GATHER,
    "charge": CHARGE,
    "thrust": THRUST,
    "channel": CHANNEL,
    "channel_a": CHANNEL_A,
    "channel_b": CHANNEL_B,
    "sky_reach": SKY_REACH,
    "call_strike": CALL_STRIKE,
    "touch_reach": TOUCH_REACH,
    "ward_cross": WARD_CROSS,
}


# ---------------------------------------------------------------------------
# Clip specs: (name, duration, keys, releaseFrame or None, looping, family, role)
# family/role land in clip_meta so the engine can map SpellDefinition -> clip
# (see resources/spells/spell_anim_families.json).
# ---------------------------------------------------------------------------

CLIPS = [
    ("cast_quick", 0.8, [
        (0.00, "rest"),
        (0.25, "quick_raise"),
        (0.40, "quick_flick"),
        (0.55, "quick_flick"),   # hold the release pose briefly
        (0.80, "rest"),
    ], 0.50, False, "bolt", "cast"),

    ("cast_standard", 1.6, [
        (0.00, "rest"),
        (0.45, "gather"),
        (0.70, "gather"),        # hold: spell words
        (1.00, "charge"),
        (1.15, "thrust"),
        (1.35, "thrust"),        # hold the thrust
        (1.60, "rest"),
    ], 0.72, False, "thrust", "cast"),

    ("cast_windup", 0.8, [
        (0.00, "rest"),
        (0.45, "gather"),
        (0.80, "channel"),
    ], None, False, "ritual", "windup"),

    ("cast_loop", 1.2, [
        (0.00, "channel"),
        (0.30, "channel_a"),
        (0.60, "channel"),
        (0.90, "channel_b"),
        (1.20, "channel"),
    ], None, True, "ritual", "loop"),

    ("cast_release", 0.7, [
        (0.00, "channel"),
        (0.18, "charge"),
        (0.32, "thrust"),
        (0.50, "thrust"),
        (0.70, "rest"),
    ], 0.46, False, "ritual", "release"),

    ("cast_call_down", 2.0, [
        (0.00, "rest"),
        (0.40, "gather"),
        (0.90, "sky_reach"),
        (1.30, "sky_reach"),     # hold: beseeching the heavens
        (1.50, "call_strike"),
        (1.75, "call_strike"),   # hold the command
        (2.00, "rest"),
    ], 0.75, False, "call_down", "cast"),

    ("cast_touch", 1.2, [
        (0.00, "rest"),
        (0.35, "gather"),
        (0.55, "touch_reach"),
        (0.95, "touch_reach"),   # hold the contact
        (1.20, "rest"),
    ], 0.46, False, "touch", "cast"),

    ("cast_ward", 0.5, [
        (0.00, "rest"),
        (0.15, "ward_cross"),    # fast defensive snap
        (0.38, "ward_cross"),
        (0.50, "rest"),
    ], 0.40, False, "ward", "cast"),
]


def main(argv=None):
    ap = argparse.ArgumentParser(description="Generate spell-cast clips")
    ap.add_argument("--splice", action="store_true",
                    help="also splice the clips into the production humanoid.anim")
    ap.add_argument("--out", default=str(WORK_ANIM), help="work-copy output path")
    args = ap.parse_args(argv)

    af = parse(SOURCE_ANIM)
    stance = BaseStance(af, ref_clip="idle", ref_time=0.0)

    calibration = None
    if CALIBRATION.exists():
        calibration = json.loads(CALIBRATION.read_text(encoding="utf-8"))

    failed = False
    for name, duration, keys, release_frame, looping, family, role in CLIPS:
        clip = build_clip(stance, name, duration, keys, POSES)
        af.set_clip(clip)
        meta = {"type": "combat", "interruptible": False, "interruptAfter": 1.0,
                "castFamily": family, "castRole": role}
        if release_frame is not None:
            meta["releaseFrame"] = release_frame
            meta["hitFrameFraction"] = release_frame
        af.set_clip_meta(name, meta)

        findings = lint_clip(af, clip, calibration, looping=looping)
        errors = [f for f in findings if f[0] == "ERROR"]
        warns = [f for f in findings if f[0] == "WARN"]
        status = "FAIL" if errors else ("WARN" if warns else "PASS")
        print(f"[{status}] {name} ({duration:.2f}s, {len(clip.channels)} channels)")
        for sev, msg in findings[:10]:
            print(f"    {sev}: {msg}")
        if errors:
            failed = True

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
