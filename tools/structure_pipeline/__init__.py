"""
Structure Pipeline (P0) — spec authoring, scale canon, and static validation for
functional building / furniture / item generation.

See docs/StructureGenerationPipeline.md for the full design. P0 is pure Python (no engine):
define + validate a resolution-independent, character-scaled BuildingSpec before any voxels
are placed. Later phases add the deterministic C++ realizer and the LLM spec author.
"""

from .spec import (
    ScaledSpec, BuildingSpec, Story, Room, Portal, Door, Stair, Fixture,
    BUILDING_FUNCTIONS, PORTAL_KINDS, STAIR_KINDS,
)
from .scale import ScaleCanon, load_canon, DEFAULT_CANON_PATH
from .validator import (
    validate, validate_dict, validate_file, ValidationReport, Issue,
)

__all__ = [
    "ScaledSpec", "BuildingSpec", "Story", "Room", "Portal", "Door", "Stair", "Fixture",
    "BUILDING_FUNCTIONS", "PORTAL_KINDS", "STAIR_KINDS",
    "ScaleCanon", "load_canon", "DEFAULT_CANON_PATH",
    "validate", "validate_dict", "validate_file", "ValidationReport", "Issue",
]
