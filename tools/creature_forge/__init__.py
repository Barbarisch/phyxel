"""creature_forge — compile anyCreature-style ACS creature specs into Phyxel .anim rigs.

Ported (MIT, github.com/Ariescar/anyCreature) front half: joint resolution,
chains/mirroring, swept superellipse volumes, the six part types, and the
keyframe animation compiler. The GLB back half is replaced by a voxelizer
that emits per-bone Box shapes via tools/anim_pipeline/anim_format.py.

Entry point: gen_creature.py (CLI) / emit.compile_spec (library).
"""
from __future__ import annotations

import sys
from pathlib import Path

# The forge builds on the anim_pipeline toolkit (anim_format, finalize_quadruped,
# anim_lint, gen_quadruped_walk helpers).
_ANIM_PIPELINE = Path(__file__).resolve().parents[1] / "anim_pipeline"
if str(_ANIM_PIPELINE) not in sys.path:
    sys.path.insert(0, str(_ANIM_PIPELINE))

__version__ = "1.0"
