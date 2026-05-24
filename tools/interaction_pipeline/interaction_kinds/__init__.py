"""Interaction-kind plugins.

An "interaction kind" answers four questions about a (character, asset)
pair:

  1. What clips does the kind need to exist before it can be tested?
  2. What initial profile offsets follow from the character + asset metrics?
     (e.g., centre the Hips on the seat footprint at the seat surface Y.)
  3. Is this character physically compatible with this asset?
     (e.g., a 7-foot giant doesn't fit a child-sized chair.)
  4. What posture rules confirm the runtime motion is correct?
     (e.g., for `sit`: hips on seat, torso upright, knees forward, feet under knees.)

Each kind lives in its own module so we can grow `door_open`, `pickup`,
`container_open`, etc. without touching `sit`. The shared `InteractionKind`
Protocol below is the contract; the `KIND_REGISTRY` exposes built-ins by id.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Callable, Mapping, Protocol


# ---------------------------------------------------------------------------
# Result types
# ---------------------------------------------------------------------------

@dataclass
class CompatibilityIssue:
    """A reason a (character, asset) pair is incompatible."""
    rule_id: str
    message: str
    measured: float
    required: float
    severity: str = "error"     # "error" blocks interaction; "warn" only flags

    def to_dict(self) -> dict[str, Any]:
        return {
            "rule_id": self.rule_id,
            "message": self.message,
            "measured": self.measured,
            "required": self.required,
            "severity": self.severity,
        }


@dataclass
class InitialOffsets:
    """Per-clip offset triples (x, y, z) in the asset's local frame.

    Keys are clip aliases (`sit_down`, `sitting_idle`, `sit_stand_up`, ...).
    Values are passed straight into the profile JSON `sit_down_offset`,
    `sitting_idle_offset`, etc. fields.
    """
    by_clip: dict[str, tuple[float, float, float]]


# ---------------------------------------------------------------------------
# Protocol
# ---------------------------------------------------------------------------

class InteractionKind(Protocol):
    """Contract every kind module must satisfy."""

    kind_id: str
    required_clip_aliases: tuple[str, ...]
    required_character_keys: tuple[str, ...]
    required_asset_feature_keys: tuple[str, ...]

    def derive_initial_offsets(
        self,
        character_metrics: Mapping[str, Any],
        asset_features: Mapping[str, Any],
    ) -> InitialOffsets:
        ...

    def check_compatibility(
        self,
        character_metrics: Mapping[str, Any],
        asset_features: Mapping[str, Any],
    ) -> list[CompatibilityIssue]:
        ...


# ---------------------------------------------------------------------------
# Registry
# ---------------------------------------------------------------------------

_REGISTRY: dict[str, InteractionKind] = {}


def register(kind: InteractionKind) -> InteractionKind:
    _REGISTRY[kind.kind_id] = kind
    return kind


def get(kind_id: str) -> InteractionKind:
    if kind_id not in _REGISTRY:
        raise KeyError(f"unknown interaction kind {kind_id!r}; "
                       f"known kinds: {sorted(_REGISTRY)}")
    return _REGISTRY[kind_id]


def all_kinds() -> Mapping[str, InteractionKind]:
    return dict(_REGISTRY)


# Import side-effect: register built-in kinds.
from . import sit as _sit  # noqa: E402,F401  (registration on import)
from . import door_open as _door  # noqa: E402,F401
from . import pickup as _pickup  # noqa: E402,F401
from . import container_open as _container  # noqa: E402,F401
from . import climb as _climb  # noqa: E402,F401
from . import control as _control  # noqa: E402,F401
from . import pose as _pose  # noqa: E402,F401
from . import carry as _carry  # noqa: E402,F401


__all__ = [
    "CompatibilityIssue",
    "InitialOffsets",
    "InteractionKind",
    "register",
    "get",
    "all_kinds",
]
