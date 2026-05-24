"""`pose` interaction kind — STUB for static body poses (kneel/lie/cross-leg).

Pose reuses the same offset shape as `sit` — a single anchored idle clip
plus optional approach/exit transitions — but with different posture rules
(hips on floor vs hips on seat, etc.). The runtime can dispatch to the sit
state-machine with overridden offsets once the detector lands.

Required Mixamo clips (already imported by `batch_import_mixamo.py`):
    - kneel
    - lay_idle
    - stand_up
"""
from __future__ import annotations

from typing import Any, Mapping

from . import CompatibilityIssue, InitialOffsets, register


class _Pose:
    kind_id: str = "pose"
    required_clip_aliases: tuple[str, ...] = ("kneel", "lay_idle", "stand_up")
    required_character_keys: tuple[str, ...] = ("total_height", "leg_length")
    required_asset_feature_keys: tuple[str, ...] = ()

    def derive_initial_offsets(self, character_metrics: Mapping[str, Any],
                               asset_features: Mapping[str, Any]) -> InitialOffsets:
        return InitialOffsets(by_clip={})

    def check_compatibility(self, character_metrics: Mapping[str, Any],
                            asset_features: Mapping[str, Any]) -> list[CompatibilityIssue]:
        return []


POSE_KIND = _Pose()
register(POSE_KIND)


__all__ = ["POSE_KIND"]
