"""`carry` interaction kind — STUB for picked-up-and-held objects.

Distinct from `pickup` (which is the lift action). `carry` is the held
state: a grip-bone attach offset, weight class affecting locomotion, and
carry-variant clips for idle/walk/run.

Required Mixamo clips (already imported by `batch_import_mixamo.py`):
    - carry_idle
    - pickup
    - put_down
"""
from __future__ import annotations

from typing import Any, Mapping

from . import CompatibilityIssue, InitialOffsets, register


class _Carry:
    kind_id: str = "carry"
    required_clip_aliases: tuple[str, ...] = ("carry_idle", "pickup", "put_down")
    required_character_keys: tuple[str, ...] = ("arm_reach", "shoulder_width", "total_height")
    required_asset_feature_keys: tuple[str, ...] = ()

    def derive_initial_offsets(self, character_metrics: Mapping[str, Any],
                               asset_features: Mapping[str, Any]) -> InitialOffsets:
        return InitialOffsets(by_clip={})

    def check_compatibility(self, character_metrics: Mapping[str, Any],
                            asset_features: Mapping[str, Any]) -> list[CompatibilityIssue]:
        return []


CARRY_KIND = _Carry()
register(CARRY_KIND)


__all__ = ["CARRY_KIND"]
