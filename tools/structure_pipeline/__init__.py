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
from .overlap import find_overlaps, assert_no_overlap
from .author import (
    author_spec, AuthorResult, build_system_prompt, build_user_prompt,
    extract_json, anthropic_llm, LLMFn, DEFAULT_MODEL,
)

__all__ = [
    "ScaledSpec", "BuildingSpec", "Story", "Room", "Portal", "Door", "Stair", "Fixture",
    "BUILDING_FUNCTIONS", "PORTAL_KINDS", "STAIR_KINDS",
    "ScaleCanon", "load_canon", "DEFAULT_CANON_PATH",
    "validate", "validate_dict", "validate_file", "ValidationReport", "Issue",
    "find_overlaps", "assert_no_overlap",
    "author_spec", "AuthorResult", "build_system_prompt", "build_user_prompt",
    "extract_json", "anthropic_llm", "LLMFn", "DEFAULT_MODEL",
]
