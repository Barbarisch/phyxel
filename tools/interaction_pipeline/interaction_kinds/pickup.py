"""`pickup` interaction kind — STUB for liftable assets (rocks, crates, weapons).

Future work: use the asset's mass + size (from a `liftable` interaction point
or from the bounding box + material density) and the character's strength
to decide one-handed vs two-handed pickup, plus initial hand-grip offsets.
"""
from __future__ import annotations

from typing import Any, Mapping

from . import CompatibilityIssue, InitialOffsets, register


class _Pickup:
    kind_id: str = "pickup"
    required_clip_aliases: tuple[str, ...] = ("pickup_low", "pickup_idle", "pickup_putdown")
    required_character_keys: tuple[str, ...] = ("arm_reach", "total_height")
    required_asset_feature_keys: tuple[str, ...] = ()

    def derive_initial_offsets(self, character_metrics: Mapping[str, Any],
                               asset_features: Mapping[str, Any]) -> InitialOffsets:
        return InitialOffsets(by_clip={})

    def check_compatibility(self, character_metrics: Mapping[str, Any],
                            asset_features: Mapping[str, Any]) -> list[CompatibilityIssue]:
        return []


PICKUP_KIND = _Pickup()
register(PICKUP_KIND)


__all__ = ["PICKUP_KIND"]
