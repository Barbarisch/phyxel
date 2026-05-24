"""`climb` interaction kind — STUB for ladders, ropes, and vines.

Future work: detect rung spacing from the asset's vertical voxel stack,
derive grip cycle phase from rung pitch vs character arm-reach, and emit
a new FSM state that locks the character's XZ to the ladder rail while
driving Y motion from a `climb_loop` clip cycle.

Required Mixamo clips (already imported by `batch_import_mixamo.py`):
    - climb_ladder_start  (mount transition)
    - climb_loop          (vertical cycle)
    - climb_ladder        (full motion fallback)
"""
from __future__ import annotations

from typing import Any, Mapping

from . import CompatibilityIssue, InitialOffsets, register


class _Climb:
    kind_id: str = "climb"
    required_clip_aliases: tuple[str, ...] = (
        "climb_ladder_start",
        "climb_loop",
        "climb_ladder",
    )
    required_character_keys: tuple[str, ...] = (
        "arm_reach",
        "leg_length",
        "total_height",
    )
    # TBD: ladder rung spacing + rail axis come from asset features once the
    # detector lands.
    required_asset_feature_keys: tuple[str, ...] = ()

    def derive_initial_offsets(self, character_metrics: Mapping[str, Any],
                               asset_features: Mapping[str, Any]) -> InitialOffsets:
        # Real impl: x,z snap to rail centerline; y aligns the hip to the
        # nearest rung. For now leave to caller.
        return InitialOffsets(by_clip={})

    def check_compatibility(self, character_metrics: Mapping[str, Any],
                            asset_features: Mapping[str, Any]) -> list[CompatibilityIssue]:
        # Stub: never blocks. Real rung-pitch-vs-arm-reach check goes here.
        return []


CLIMB_KIND = _Climb()
register(CLIMB_KIND)


__all__ = ["CLIMB_KIND"]
