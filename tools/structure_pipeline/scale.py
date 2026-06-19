"""
Structure Pipeline — scale canon (P0).

The single source of truth for "how big is everything" is the engine's pre-measured
``resources/character_design_constraints.json``. The default ``humanoid_normal`` is
~1.751 cubes tall (1 cube ~= 1 metre). Every spec dimension is validated against the
canon so generated structures/furniture/items read correctly next to the character.

Derived targets (ceiling/door clearances) are computed from the measured character
height with documented formulas — never hardcoded magic numbers.
"""

from __future__ import annotations

import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, Optional

# repo root = .../tools/structure_pipeline/scale.py -> parents[2]
_REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CANON_PATH = _REPO_ROOT / "resources" / "character_design_constraints.json"


@dataclass
class ScaleCanon:
    """Measured character anchors + derived sizing targets, all in cubes."""
    archetype: str
    character_height: float

    # Measured seating/table anchors (cubes).
    seat_top_ideal: float
    seat_top_achievable: float
    seat_depth_min: float
    seat_width_min: float
    backrest_top_chair: float
    table_standing: float
    table_seated: float
    hip_height: float

    # ----- Derived clearances (integer cubes, since structural voxels are cube-aligned) -----
    @property
    def door_clear_min(self) -> int:
        """Minimum passable opening height: the character must fit, rounded up to whole cubes."""
        return max(2, math.ceil(self.character_height))

    @property
    def ceiling_min(self) -> int:
        """Hard floor for interior clear height — below this the character does not fit."""
        return max(2, math.ceil(self.character_height))

    @property
    def ceiling_comfortable(self) -> int:
        """Recommended interior clear height (head clearance for movement/jumping)."""
        return self.ceiling_min + 1

    @property
    def door_width_min(self) -> int:
        """Minimum passable opening width in cubes (shoulder clearance)."""
        return 1

    def describe(self) -> Dict[str, Any]:
        """Flat dict of the values used by the validator/prompt — handy for logging."""
        return {
            "archetype": self.archetype,
            "character_height": self.character_height,
            "door_clear_min": self.door_clear_min,
            "door_width_min": self.door_width_min,
            "ceiling_min": self.ceiling_min,
            "ceiling_comfortable": self.ceiling_comfortable,
            "seat_top_ideal": self.seat_top_ideal,
            "seat_top_achievable": self.seat_top_achievable,
            "table_standing": self.table_standing,
            "backrest_top_chair": self.backrest_top_chair,
        }


def load_canon(path: Optional[Path] = None, archetype: str = "humanoid_normal") -> ScaleCanon:
    """Load the scale canon for an archetype from the engine constraints file.

    Raises FileNotFoundError / KeyError with actionable messages if the canon is missing.
    """
    canon_path = Path(path) if path else DEFAULT_CANON_PATH
    if not canon_path.exists():
        raise FileNotFoundError(
            f"Scale canon not found at {canon_path}. It is the source of truth for sizing; "
            f"re-measure via get_bone_positions after .anim changes."
        )
    data = json.loads(canon_path.read_text(encoding="utf-8"))
    if archetype not in data:
        available = [k for k in data.keys() if k != "_comment"]
        raise KeyError(f"Archetype '{archetype}' not in canon. Available: {available}")

    block = data[archetype]
    meas = block.get("measurements", {})
    seating = block.get("seating", {})
    tables = block.get("tables", {})

    height = float(meas.get("character_height") or meas.get("head_top") or 1.751)
    return ScaleCanon(
        archetype=archetype,
        character_height=height,
        seat_top_ideal=float(seating.get("seat_top_ideal", 0.479)),
        seat_top_achievable=float(seating.get("seat_top_achievable", 0.667)),
        seat_depth_min=float(seating.get("seat_depth_min", 0.389)),
        seat_width_min=float(seating.get("seat_width_min", 0.500)),
        backrest_top_chair=float(seating.get("backrest_top_y_chair", 1.667)),
        table_standing=float(tables.get("standing_height_ideal", 1.0)),
        table_seated=float(tables.get("seated_height_ideal", 0.868)),
        hip_height=float(meas.get("hip_bottom", 0.868)),
    )
