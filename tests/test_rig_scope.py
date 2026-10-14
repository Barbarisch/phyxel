"""A2 rig scope (docs/AnimationSystemV3Plan.md §4 A2, owner decision A2-2).

  * the committed rig_scope.json equals a fresh scan (it cannot go stale silently)
  * every `retired` rig lives under resources/animated_characters/legacy/ — RED until A2
    moves the nine unreferenced files there
  * no gameplay data references a retired rig (W axis: nothing owns them)
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "anim_pipeline"))

import rig_scope  # type: ignore  # noqa: E402

RIG_DIR = ROOT / "resources" / "animated_characters"


def test_committed_scope_matches_fresh_scan():
    committed = json.loads((RIG_DIR / "rig_scope.json").read_text(encoding="utf-8"))
    assert committed == rig_scope.scan(), "rig_scope.json is stale — run tools/anim_pipeline/rig_scope.py"


def test_no_unreferenced_rig_is_shipped_at_top_level():
    scope = json.loads((RIG_DIR / "rig_scope.json").read_text(encoding="utf-8"))
    assert scope["unreferenced_top_level"] == [], (
        "unreferenced rigs still shipped at top level — retire them (git mv to legacy/): "
        f"{scope['unreferenced_top_level']}")


def test_retired_rigs_are_not_referenced_by_gameplay_data():
    """W axis: nothing owns a retired rig. Scan the gameplay sources for legacy/ stems that are
    not generic words (the legacy folder holds 'character*.anim' from the engine's first days,
    whose stems match everything and are excluded)."""
    scope = json.loads((RIG_DIR / "rig_scope.json").read_text(encoding="utf-8"))
    generic = {"character", "character_box", "character_complete"}
    stems = [r for r in scope["retired"] if r not in generic]
    fresh = rig_scope.scan_stems(stems)
    referenced = [r for r, tiers in fresh.items() if "gameplay" in tiers]
    assert not referenced, f"retired rigs still referenced by gameplay data: {referenced}"


def test_gameplay_rigs_exist_at_top_level():
    scope = json.loads((RIG_DIR / "rig_scope.json").read_text(encoding="utf-8"))
    missing = [r for r in scope["gameplay"] if not (RIG_DIR / f"{r}.anim").exists()]
    assert not missing, f"gameplay-referenced rigs not found: {missing}"
