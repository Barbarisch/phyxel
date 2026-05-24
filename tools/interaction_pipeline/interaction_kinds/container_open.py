"""`container_open` interaction kind — STUB for chests, barrels, drawers.

Future work: derive lid-hinge axis + open-pose hand position from a
`container_lid` interaction point. Validate that the character can reach
the lid handle from a standing pose and that the open lid doesn't clip the
character's face.
"""
from __future__ import annotations

from typing import Any, Mapping

from . import CompatibilityIssue, InitialOffsets, register


class _ContainerOpen:
    kind_id: str = "container_open"
    required_clip_aliases: tuple[str, ...] = ("container_open", "container_close")
    required_character_keys: tuple[str, ...] = ("arm_reach", "hip_height")
    required_asset_feature_keys: tuple[str, ...] = ()

    def derive_initial_offsets(self, character_metrics: Mapping[str, Any],
                               asset_features: Mapping[str, Any]) -> InitialOffsets:
        return InitialOffsets(by_clip={})

    def check_compatibility(self, character_metrics: Mapping[str, Any],
                            asset_features: Mapping[str, Any]) -> list[CompatibilityIssue]:
        return []


CONTAINER_OPEN_KIND = _ContainerOpen()
register(CONTAINER_OPEN_KIND)


__all__ = ["CONTAINER_OPEN_KIND"]
