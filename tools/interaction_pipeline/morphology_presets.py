"""Morphology presets — synthetic body proportions for matrix testing.

A *morphology preset* is a `CharacterAppearance`-compatible JSON dict the
engine can apply via `setAppearance()` + `rebuildWithAppearance()`. Each
preset captures a target body shape (height, bulk, limb proportions) without
needing a separate `.anim` file.

Use these to fan out the (character × asset) matrix in the interaction
pipeline: load a chair, spawn each preset in turn via the
`spawn_for_test` engine endpoint, characterise the resulting metrics, run
`can_interact`, then sit and capture posture telemetry.

SOURCE OF TRUTH: resources/appearance_presets.json — shared with the engine's
C++ AppearancePresetRegistry (races resolve presets through it at spawn).
This module loads that file; do not hardcode proportion values here, edit the
JSON instead so Python and C++ can never drift.

Fields mirror `engine/include/scene/CharacterAppearance.h::CharacterAppearance`.
Only the proportion scales are set here; colors are left at defaults.
"""
from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping

_REPO_ROOT = Path(__file__).resolve().parents[2]
_PRESETS_JSON = _REPO_ROOT / "resources" / "appearance_presets.json"


@dataclass(frozen=True)
class MorphologyPreset:
    preset_id: str
    description: str
    height_scale: float = 1.0       # overall vertical scale
    bulk_scale: float = 1.0         # overall width/thickness
    head_scale: float = 1.0
    arm_length_scale: float = 1.0
    leg_length_scale: float = 1.0
    torso_length_scale: float = 1.0
    shoulder_width_scale: float = 1.0

    def to_appearance_json(self) -> dict:
        """Produce a JSON dict that maps onto `CharacterAppearance::fromJson`."""
        return {
            "morphology": "Humanoid",
            # The preset id flows into CharacterAppearance.presetId so the
            # engine's sit/interact code can pick the per-character override
            # on the resolved InteractionProfile.
            "presetId":            self.preset_id,
            "heightScale":         self.height_scale,
            "bulkScale":           self.bulk_scale,
            "headScale":           self.head_scale,
            "armLengthScale":      self.arm_length_scale,
            "legLengthScale":      self.leg_length_scale,
            "torsoLengthScale":    self.torso_length_scale,
            "shoulderWidthScale":  self.shoulder_width_scale,
        }


def _load_builtins() -> Mapping[str, MorphologyPreset]:
    with open(_PRESETS_JSON, encoding="utf-8") as f:
        doc = json.load(f)
    presets: dict[str, MorphologyPreset] = {}
    for entry in doc["presets"]:
        pid = entry["presetId"]
        presets[pid] = MorphologyPreset(
            preset_id=pid,
            description=entry.get("description", ""),
            height_scale=entry.get("heightScale", 1.0),
            bulk_scale=entry.get("bulkScale", 1.0),
            head_scale=entry.get("headScale", 1.0),
            arm_length_scale=entry.get("armLengthScale", 1.0),
            leg_length_scale=entry.get("legLengthScale", 1.0),
            torso_length_scale=entry.get("torsoLengthScale", 1.0),
            shoulder_width_scale=entry.get("shoulderWidthScale", 1.0),
        )
    return presets


_BUILT_INS: Mapping[str, MorphologyPreset] = _load_builtins()

# Named constants kept for existing imports (test matrices reference these).
STANDARD   = _BUILT_INS["standard"]
GIANT      = _BUILT_INS["giant"]
DWARF      = _BUILT_INS["dwarf"]
CHILD      = _BUILT_INS["child"]
HALFLING   = _BUILT_INS["halfling"]
GNOME      = _BUILT_INS["gnome"]
ELF        = _BUILT_INS["elf"]
TIEFLING   = _BUILT_INS["tiefling"]
DRAGONBORN = _BUILT_INS["dragonborn"]
HALF_ORC   = _BUILT_INS["half_orc"]
GOLIATH    = _BUILT_INS["goliath"]
GOBLIN     = _BUILT_INS["goblin"]
OGRE       = _BUILT_INS["ogre"]


def get(preset_id: str) -> MorphologyPreset:
    if preset_id not in _BUILT_INS:
        raise KeyError(
            f"unknown morphology preset {preset_id!r}; "
            f"known: {sorted(_BUILT_INS)}"
        )
    return _BUILT_INS[preset_id]


def all_presets() -> Mapping[str, MorphologyPreset]:
    return dict(_BUILT_INS)


__all__ = [
    "MorphologyPreset",
    "STANDARD", "GIANT", "DWARF", "CHILD",
    "HALFLING", "GNOME", "ELF", "TIEFLING",
    "DRAGONBORN", "HALF_ORC", "GOLIATH",
    "GOBLIN", "OGRE",
    "get", "all_presets",
]
