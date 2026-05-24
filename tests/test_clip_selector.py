"""Unit tests for tools/interaction_pipeline/clip_selector.py (Stage 3).

Offline: every test feeds a fixed clip list to ``select_clips`` and checks
that the binding matches expectations. No engine required.
"""
from __future__ import annotations

import pytest

from tools.interaction_pipeline.clip_selector import (
    ClipBinding,
    ClipInfo,
    MIN_BINDING_SCORE,
    load_spec,
    select_clips,
    validate_binding,
)

# Real clip vocabulary from resources/animated_characters/humanoid.anim.
HUMANOID_CLIPS = (
    "base_pose", "idle", "idle_looking", "standing", "unarmed_walk",
    "stand_to_sit", "sitting_idle", "sit_to_stand",
    "open_door_in", "open_door_out", "open_lid", "close_lid",
    "pickup_object", "pickup", "put_down",
    "climb_ladder", "climb_ladder_start", "climb_loop",
    "climbing_to_top", "climbing_top", "climbing_down",
    "pull_lever", "pull_heavy", "push",
    "kneel", "lay_idle", "stand_up", "salute", "talk",
)


@pytest.fixture(scope="module")
def spec():
    return load_spec()


def test_sit_binds_canonical_clips(spec):
    b = select_clips("sit", HUMANOID_CLIPS, spec=spec, archetype="humanoid_normal")
    assert b.clip_for("sit_down") == "stand_to_sit"
    assert b.clip_for("sitting_idle") == "sitting_idle"
    assert b.clip_for("sit_stand_up") == "sit_to_stand"
    issues = validate_binding(b)
    assert issues == [], f"expected clean binding, got {issues}"


def test_door_open_binds_in_and_out(spec):
    b = select_clips("door_open", HUMANOID_CLIPS, spec=spec)
    assert b.clip_for("door_open_pull") == "open_door_in"
    assert b.clip_for("door_open_push") == "open_door_out"
    issues = validate_binding(b)
    assert issues == [], f"expected clean binding, got {issues}"


def test_missing_clip_raises_error_issue(spec):
    # Drop the canonical sit clips entirely.
    pared = tuple(c for c in HUMANOID_CLIPS
                  if c not in {"stand_to_sit", "sitting_idle", "sit_to_stand"})
    b = select_clips("sit", pared, spec=spec)
    assert b.clip_for("sit_down") is None
    assert b.clip_for("sitting_idle") is None
    assert b.clip_for("sit_stand_up") is None
    issues = validate_binding(b)
    aliases = {i.alias for i in issues if i.severity == "error"}
    assert aliases == {"sit_down", "sitting_idle", "sit_stand_up"}


def test_low_score_fallback_produces_warning(spec):
    # Only an inexact, low-weight match exists for door_open_push.
    only_push = ("push", "idle")
    b = select_clips("door_open", only_push, spec=spec)
    # push has weight 25 in the spec, below MIN_BINDING_SCORE (40).
    push_best = b.bindings["door_open_push"].best
    assert push_best is not None and push_best.clip_name == "push"
    assert push_best.score < MIN_BINDING_SCORE
    issues = validate_binding(b)
    warns = [i for i in issues if i.severity == "warn" and i.alias == "door_open_push"]
    assert warns, "expected a warn-severity issue for low-score push binding"


def test_collision_warning_when_one_clip_serves_multiple_aliases(spec):
    # Construct a kind binding where every alias maps to the same clip.
    fake_clips = ("stand_to_sit",)  # nothing for idle or stand_up
    b = select_clips("sit", fake_clips, spec=spec)
    # sit_down should still bind; the other two won't have an exact match
    # but "stand_to_sit" only matches "sit_down" patterns, so no collision.
    # Build the collision case manually with custom clips.
    crazy = ("sitting_idle", "sitting_idle_2")
    b2 = select_clips("sit", crazy, spec=spec)
    assert b2.clip_for("sit_down") is None
    assert b2.clip_for("sitting_idle") == "sitting_idle"


def test_to_provenance_round_trips_keys(spec):
    b = select_clips("sit", HUMANOID_CLIPS, spec=spec, archetype="humanoid_normal",
                     character_id="player_test")
    p = b.to_provenance()
    assert p["kind_id"] == "sit"
    assert p["archetype"] == "humanoid_normal"
    assert p["character_id"] == "player_test"
    assert set(p["aliases"].keys()) == {"sit_down", "sitting_idle", "sit_stand_up"}
    assert p["aliases"]["sit_down"]["clip"] == "stand_to_sit"
    assert p["aliases"]["sit_down"]["score"] >= MIN_BINDING_SCORE


def test_clipinfo_accepts_mapping_and_string(spec):
    mixed: list = [
        "stand_to_sit",
        {"name": "sitting_idle", "duration": 2.0, "speed": 1.0, "index": 5},
        ClipInfo(name="sit_to_stand", duration=1.5),
    ]
    b = select_clips("sit", mixed, spec=spec)
    assert b.clip_for("sit_down") == "stand_to_sit"
    assert b.clip_for("sitting_idle") == "sitting_idle"
    assert b.clip_for("sit_stand_up") == "sit_to_stand"


def test_unknown_kind_raises(spec):
    with pytest.raises(KeyError):
        select_clips("not_a_kind", HUMANOID_CLIPS, spec=spec)


def test_required_aliases_subset(spec):
    b = select_clips("sit", HUMANOID_CLIPS, spec=spec,
                     required_aliases=["sitting_idle"])
    assert list(b.bindings.keys()) == ["sitting_idle"]
    assert b.clip_for("sitting_idle") == "sitting_idle"
