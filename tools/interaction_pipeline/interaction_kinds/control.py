"""`control` interaction kind — STUB for levers, buttons, wheels, valves.

Future work: model a two-stable-position control. The runtime drives a
KinematicAnimator pivot (existing Phase D primitive) between `off` and
`on` angles and publishes an InteractionEvent that other objects can
subscribe to (e.g. lever opens distant gate).

Required Mixamo clips (already imported by `batch_import_mixamo.py`):
    - pull_lever
    - work_device
"""
from __future__ import annotations

from typing import Any, Mapping

from . import CompatibilityIssue, InitialOffsets, register


class _Control:
    kind_id: str = "control"
    required_clip_aliases: tuple[str, ...] = ("pull_lever", "work_device")
    required_character_keys: tuple[str, ...] = ("arm_reach", "total_height")
    required_asset_feature_keys: tuple[str, ...] = ()

    def derive_initial_offsets(self, character_metrics: Mapping[str, Any],
                               asset_features: Mapping[str, Any]) -> InitialOffsets:
        return InitialOffsets(by_clip={})

    def check_compatibility(self, character_metrics: Mapping[str, Any],
                            asset_features: Mapping[str, Any]) -> list[CompatibilityIssue]:
        return []


CONTROL_KIND = _Control()
register(CONTROL_KIND)


__all__ = ["CONTROL_KIND"]
