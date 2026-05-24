"""`sit` interaction kind.

A character is "sitting" when:

  - Hips are over the seat top in X/Z and within tolerance in Y;
  - Spine is upright (hips below shoulders);
  - Knees are forward of hips, thighs roughly horizontal;
  - Feet are below the hips and roughly under the knees.

This module owns:
  - `derive_initial_offsets`: given character + asset metrics, compute the
    per-clip Hips offsets so the very first run of an asset has a sensible
    starting profile (no manual tweaking needed).
  - `check_compatibility`: refuse mismatched pairs early
    (e.g., giant won't fit on a child seat).
  - Posture rule re-export: the existing `detect_seated_posture` in
    `detectors.py` is the authority. We re-export it here so callers can
    discover the rule via the kind interface.
"""
from __future__ import annotations

import math
from typing import Any, Mapping

from . import (
    CompatibilityIssue,
    InitialOffsets,
    register,
)

# Re-export the existing detector so kind-aware runners can find it.
from ..detectors import detect_seated_posture as posture_detector  # noqa: F401


# Margins for compatibility checks (metres).
_HIP_CLEARANCE = 0.05      # seat must exceed hip width by at least this.
_DEPTH_CLEARANCE = 0.10    # buttock-to-knee depth headroom.
_FOOT_DROP_MAX = 0.20      # how far feet can dangle below seat before "too tall".
_BACKREST_HEAD_MAX = 0.10  # backrest may exceed shoulder height by this; above that warns.


class _Sit:
    kind_id: str = "sit"
    required_clip_aliases: tuple[str, ...] = (
        "sit_down", "sitting_idle", "sit_stand_up",
    )
    required_character_keys: tuple[str, ...] = (
        "hip_height", "hip_width", "body_depth",
        "leg_length", "sitting_height", "eye_height",
    )
    required_asset_feature_keys: tuple[str, ...] = (
        "seat_top_y", "seat_width_x", "seat_depth_z", "seat_center",
    )

    # -----------------------------------------------------------------------
    def derive_initial_offsets(
        self,
        character_metrics: Mapping[str, Any],
        asset_features: Mapping[str, Any],
    ) -> InitialOffsets:
        """Produce sit_* offsets in the asset's local frame.

        Convention (matches the engine, see Application.cpp): the Hips are
        anchored to `seat + sit_*_offset` after subtracting the clip's Hips
        ref-pose translation. The cleanest initial guess is therefore to
        place the offset at the seat centre directly — that puts the Hips
        over the seat footprint at the seat surface. Per-clip refinements
        (e.g., the sit-down anim approaching from slightly back) are nudges
        the tuner will discover; we just need a sane starting point.
        """
        seat_center = asset_features.get("seat_center") or [0.0, 0.0, 0.0]
        cx, cy, cz = float(seat_center[0]), float(seat_center[1]), float(seat_center[2])

        # Slight forward bias on sit_down so the character arrives at the
        # seat front, then settles centred when sitting_idle plays.
        depth = float(asset_features.get("seat_depth_z") or 0.0)
        front_bias = max(0.0, depth * 0.15)

        return InitialOffsets(by_clip={
            "sit_down":     (cx, cy, cz + front_bias),
            "sitting_idle": (cx, cy, cz),
            "sit_stand_up": (cx, cy, cz + front_bias),
        })

    # -----------------------------------------------------------------------
    def check_compatibility(
        self,
        character_metrics: Mapping[str, Any],
        asset_features: Mapping[str, Any],
    ) -> list[CompatibilityIssue]:
        issues: list[CompatibilityIssue] = []
        # Pull (with sensible zero defaults so missing keys flag explicitly).
        hip_width = float(character_metrics.get("hip_width") or 0.0)
        body_depth = float(character_metrics.get("body_depth") or 0.0)
        leg_length = float(character_metrics.get("leg_length") or 0.0)
        eye_height = float(character_metrics.get("eye_height") or 0.0)
        sitting_height = float(character_metrics.get("sitting_height") or 0.0)

        seat_width = float(asset_features.get("seat_width_x") or 0.0)
        seat_depth = float(asset_features.get("seat_depth_z") or 0.0)
        seat_top_y = float(asset_features.get("seat_top_y") or 0.0)
        backrest_h = float(asset_features.get("backrest_height") or 0.0)

        # Width.
        if seat_width > 0.0 and hip_width > 0.0:
            need = hip_width + _HIP_CLEARANCE
            if seat_width < need:
                issues.append(CompatibilityIssue(
                    rule_id="SEAT_TOO_NARROW",
                    message=f"Seat width {seat_width:.3f}m too narrow for hip width {hip_width:.3f}m",
                    measured=seat_width, required=need, severity="error",
                ))
        # Depth: at least body-depth-ish so the character isn't perched on the edge.
        if seat_depth > 0.0 and body_depth > 0.0:
            need = body_depth + _DEPTH_CLEARANCE
            if seat_depth < need:
                issues.append(CompatibilityIssue(
                    rule_id="SEAT_TOO_SHALLOW",
                    message=f"Seat depth {seat_depth:.3f}m too shallow for body depth {body_depth:.3f}m",
                    measured=seat_depth, required=need, severity="warn",
                ))
        # Height: feet shouldn't dangle absurdly. Use seat_top_y as floor-to-seat.
        if seat_top_y > 0.0 and leg_length > 0.0:
            overhang = seat_top_y - leg_length
            if overhang > _FOOT_DROP_MAX:
                issues.append(CompatibilityIssue(
                    rule_id="SEAT_TOO_TALL",
                    message=f"Seat {seat_top_y:.3f}m above floor; legs only {leg_length:.3f}m "
                            f"(feet dangle {overhang:.3f}m, max {_FOOT_DROP_MAX:.2f}m)",
                    measured=overhang, required=_FOOT_DROP_MAX, severity="warn",
                ))
        # Backrest blocks the view: warn if backrest top extends well above eye level
        # measured from seat-relative-to-eye (eye is from floor, backrest is from seat).
        if backrest_h > 0.0 and eye_height > 0.0 and sitting_height > 0.0:
            # When seated, the eye is roughly at seat_top_y + (eye_height - hip_height)
            # approximations break down for very different morphologies; use sitting_height instead.
            seated_eye_above_seat = max(0.0, sitting_height - 0.1)
            if backrest_h > seated_eye_above_seat + _BACKREST_HEAD_MAX:
                issues.append(CompatibilityIssue(
                    rule_id="BACKREST_BLOCKS_VIEW",
                    message=f"Backrest {backrest_h:.3f}m exceeds seated eye height ~{seated_eye_above_seat:.3f}m",
                    measured=backrest_h, required=seated_eye_above_seat + _BACKREST_HEAD_MAX,
                    severity="warn",
                ))
        return issues


SIT_KIND: _Sit = _Sit()
register(SIT_KIND)


__all__ = ["SIT_KIND", "posture_detector"]
