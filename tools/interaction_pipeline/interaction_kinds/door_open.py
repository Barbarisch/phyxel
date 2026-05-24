"""`door_open` interaction kind — STUB.

Future work: derive door-hinge swing axis from `interaction_point` of kind
`door_handle`, validate that the character's arm reach is enough to grab the
handle while standing in front of the door without intersecting the wall.
For now this kind only exists so the registry can list it and downstream
callers can detect that it is not yet implemented.
"""
from __future__ import annotations

from typing import Any, Mapping

from . import CompatibilityIssue, InitialOffsets, register


class _DoorOpen:
    kind_id: str = "door_open"
    required_clip_aliases: tuple[str, ...] = ("door_open_pull", "door_open_push")
    required_character_keys: tuple[str, ...] = ("arm_reach", "shoulder_width", "total_height")
    required_asset_feature_keys: tuple[str, ...] = ()  # TBD when door_handle features land

    def derive_initial_offsets(self, character_metrics: Mapping[str, Any],
                               asset_features: Mapping[str, Any]) -> InitialOffsets:
        return InitialOffsets(by_clip={})

    def check_compatibility(self, character_metrics: Mapping[str, Any],
                            asset_features: Mapping[str, Any]) -> list[CompatibilityIssue]:
        # Stub: never blocks. Real arm-reach vs handle-height check goes here.
        return []


DOOR_OPEN_KIND = _DoorOpen()
register(DOOR_OPEN_KIND)


__all__ = ["DOOR_OPEN_KIND"]
