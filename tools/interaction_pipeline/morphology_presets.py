"""Morphology presets — synthetic body proportions for matrix testing.

A *morphology preset* is a `CharacterAppearance`-compatible JSON dict the
engine can apply via `setAppearance()` + `rebuildWithAppearance()`. Each
preset captures a target body shape (height, bulk, limb proportions) without
needing a separate `.anim` file.

Use these to fan out the (character × asset) matrix in the interaction
pipeline: load a chair, spawn each preset in turn via the
`spawn_for_test` engine endpoint, characterise the resulting metrics, run
`can_interact`, then sit and capture posture telemetry.

Fields mirror `engine/include/scene/CharacterAppearance.h::CharacterAppearance`.
Only the proportion scales are set here; colors are left at defaults.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Mapping


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


# ---------------------------------------------------------------------------
# Built-in presets
# ---------------------------------------------------------------------------

# Numbers chosen to span the morphology space we care about for furniture
# interaction. Keep extremes inside what the rig handles without bone clipping:
# the rig has been tested up to ~1.3x and down to ~0.6x.

STANDARD = MorphologyPreset(
    preset_id="standard",
    description="Default humanoid proportions (1.0 across the board).",
)

GIANT = MorphologyPreset(
    preset_id="giant",
    description="~7ft tall, heavier build. Tests SEAT_TOO_NARROW / SEAT_TOO_SHALLOW.",
    height_scale=1.25,
    bulk_scale=1.30,
    leg_length_scale=1.25,
    torso_length_scale=1.20,
    shoulder_width_scale=1.20,
    arm_length_scale=1.20,
)

DWARF = MorphologyPreset(
    preset_id="dwarf",
    description="~60% height with 150% bulk. Tests SEAT_TOO_TALL.",
    height_scale=0.60,
    bulk_scale=1.50,
    leg_length_scale=0.55,
    torso_length_scale=0.70,
    shoulder_width_scale=1.10,
    head_scale=1.10,
)

CHILD = MorphologyPreset(
    preset_id="child",
    description="Small, thin character. Easy fit for everything.",
    height_scale=0.70,
    bulk_scale=0.75,
    leg_length_scale=0.65,
    torso_length_scale=0.80,
    head_scale=1.15,
)

# ---------------------------------------------------------------------------
# D&D race presets (Phase K)
#
# Sized against the canonical "Standard" rig at 1.0. We aim for *relative*
# silhouette differences — the engine sit-code reads the preset_id off
# CharacterAppearance and resolves per-character interaction overrides.
#
# All scales kept inside the rig-tested band (0.6 .. 1.3) to avoid bone
# clipping. Dwarf reuses the existing testing preset.
# ---------------------------------------------------------------------------

HALFLING = MorphologyPreset(
    preset_id="halfling",
    description="~3ft tall, slight build, slightly larger head.",
    height_scale=0.65,
    bulk_scale=0.80,
    leg_length_scale=0.60,
    torso_length_scale=0.75,
    arm_length_scale=0.70,
    shoulder_width_scale=0.85,
    head_scale=1.10,
)

GNOME = MorphologyPreset(
    preset_id="gnome",
    description="~3.5ft, slim, large head.",
    height_scale=0.70,
    bulk_scale=0.75,
    leg_length_scale=0.65,
    torso_length_scale=0.80,
    arm_length_scale=0.75,
    shoulder_width_scale=0.85,
    head_scale=1.15,
)

ELF = MorphologyPreset(
    preset_id="elf",
    description="Tall and slender; long limbs, narrow frame.",
    height_scale=1.05,
    bulk_scale=0.85,
    leg_length_scale=1.10,
    torso_length_scale=1.00,
    arm_length_scale=1.10,
    shoulder_width_scale=0.95,
)

TIEFLING = MorphologyPreset(
    preset_id="tiefling",
    description="Slightly above human height; balanced proportions.",
    height_scale=1.05,
    bulk_scale=1.00,
    leg_length_scale=1.05,
    torso_length_scale=1.05,
    arm_length_scale=1.05,
    shoulder_width_scale=1.00,
)

DRAGONBORN = MorphologyPreset(
    preset_id="dragonborn",
    description="~6.5ft, broad shoulders, heavy torso.",
    height_scale=1.15,
    bulk_scale=1.20,
    leg_length_scale=1.10,
    torso_length_scale=1.15,
    arm_length_scale=1.15,
    shoulder_width_scale=1.20,
)

HALF_ORC = MorphologyPreset(
    preset_id="half_orc",
    description="~6.5ft, heavy bulk and very broad shoulders.",
    height_scale=1.15,
    bulk_scale=1.25,
    leg_length_scale=1.10,
    torso_length_scale=1.15,
    arm_length_scale=1.15,
    shoulder_width_scale=1.25,
)

GOLIATH = MorphologyPreset(
    preset_id="goliath",
    description="~7-8ft, the heaviest of the playable races.",
    height_scale=1.28,
    bulk_scale=1.30,
    leg_length_scale=1.28,
    torso_length_scale=1.22,
    arm_length_scale=1.22,
    shoulder_width_scale=1.25,
)


_BUILT_INS: Mapping[str, MorphologyPreset] = {
    p.preset_id: p for p in (
        STANDARD, GIANT, DWARF, CHILD,
        HALFLING, GNOME, ELF, TIEFLING,
        DRAGONBORN, HALF_ORC, GOLIATH,
    )
}


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
    "get", "all_presets",
]
