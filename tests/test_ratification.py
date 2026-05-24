"""Stage 4 gate tests — ratification combining Stages 1–3 + provenance sidecar."""
from __future__ import annotations

from pathlib import Path

import pytest

from tools.interaction_pipeline.character_metrics import (
    CharacterMetrics,
    SCHEMA_VERSION as CM_SCHEMA,
)
from tools.interaction_pipeline.ratification import (
    KIND_TAG_TO_KIND_ID,
    RatificationReport,
    provenance_sidecar_path,
    ratify_inputs,
    read_provenance,
    write_provenance,
)

ROOT = Path(__file__).resolve().parents[1]
TEMPLATES = ROOT / "resources" / "templates"

HUMANOID_CLIPS = (
    "stand_to_sit", "sitting_idle", "sit_to_stand",
    "open_door_in", "open_door_out",
    "idle", "unarmed_walk",
)


def _good_metrics(**overrides):
    base = dict(
        schema_version=CM_SCHEMA,
        total_height=1.80, hip_height=0.95, eye_height=1.70,
        leg_length=0.90, arm_reach=0.78, shoulder_width=0.45,
        hip_width=0.32, body_depth=0.24, sitting_height=0.95,
    )
    base.update(overrides)
    return CharacterMetrics(**base)


def test_ratify_sit_passes_for_chair_wood():
    r = ratify_inputs(
        asset_path=TEMPLATES / "chair_wood.voxel",
        point_id="seat_0",
        character_metrics=_good_metrics(),
        available_clips=HUMANOID_CLIPS,
        morphology="standard",
    )
    assert r.ok, f"expected ratified, got errors: {r.errors}"
    assert r.clip_binding is not None
    assert r.clip_binding.clip_for("sit_down") == "stand_to_sit"


def test_ratify_door_passes_for_door_wood():
    r = ratify_inputs(
        asset_path=TEMPLATES / "door_wood.voxel",
        point_id="handle_0",
        character_metrics=_good_metrics(),
        available_clips=HUMANOID_CLIPS,
    )
    assert r.ok, f"expected ratified, got errors: {r.errors}"
    assert r.clip_binding.clip_for("door_open_pull") == "open_door_in"
    assert r.clip_binding.clip_for("door_open_push") == "open_door_out"


def test_ratify_fails_when_point_id_unknown():
    r = ratify_inputs(
        asset_path=TEMPLATES / "chair_wood.voxel",
        point_id="not_a_point",
        character_metrics=_good_metrics(),
        available_clips=HUMANOID_CLIPS,
    )
    assert not r.ok
    assert any("not_a_point" in e for e in r.errors)


def test_ratify_fails_when_clips_missing():
    r = ratify_inputs(
        asset_path=TEMPLATES / "chair_wood.voxel",
        point_id="seat_0",
        character_metrics=_good_metrics(),
        available_clips=["idle"],  # no sit clips at all
    )
    assert not r.ok
    # Every sit alias should be reported missing.
    assert any("sit_down" in e for e in r.errors)
    assert any("sitting_idle" in e for e in r.errors)
    assert any("sit_stand_up" in e for e in r.errors)


def test_ratify_fails_when_metrics_corrupt():
    r = ratify_inputs(
        asset_path=TEMPLATES / "chair_wood.voxel",
        point_id="seat_0",
        character_metrics=_good_metrics(hip_width=0.0),  # sit requires hip_width
        available_clips=HUMANOID_CLIPS,
    )
    assert not r.ok
    assert any("hip_width" in e for e in r.errors)


def test_ratify_door_with_only_sit_clips_fails():
    # Catches the original schema-corruption bug class: tuning a door against
    # sit clips because the kind/clip pairing was never verified.
    r = ratify_inputs(
        asset_path=TEMPLATES / "door_wood.voxel",
        point_id="handle_0",
        character_metrics=_good_metrics(),
        available_clips=["stand_to_sit", "sitting_idle", "sit_to_stand"],
    )
    assert not r.ok
    assert any("door_open_pull" in e for e in r.errors)
    assert any("door_open_push" in e for e in r.errors)


def test_provenance_round_trip(tmp_path: Path):
    r = ratify_inputs(
        asset_path=TEMPLATES / "chair_wood.voxel",
        point_id="seat_0",
        character_metrics=_good_metrics(),
        available_clips=HUMANOID_CLIPS,
        morphology="standard",
    )
    assert r.ok
    path = write_provenance(
        r,
        archetype="test_archetype",
        template_name="chair_wood",
        point_id="seat_0",
        morphology="standard",
        base_dir=tmp_path,
    )
    assert path.exists()
    back = read_provenance(
        archetype="test_archetype",
        template_name="chair_wood",
        point_id="seat_0",
        morphology="standard",
        base_dir=tmp_path,
    )
    assert back is not None
    assert back["asset"]["template_name"] == "chair_wood"
    assert back["character"]["morphology"] == "standard"
    assert back["clip_binding"]["aliases"]["sit_down"]["clip"] == "stand_to_sit"


def test_write_provenance_refuses_non_ratified(tmp_path: Path):
    bad = RatificationReport(
        asset_metrics=None, character_metrics=None, clip_binding=None,
        extra_errors=["bogus"],
    )
    with pytest.raises(RuntimeError):
        write_provenance(
            bad, archetype="x", template_name="y", point_id="z",
            base_dir=tmp_path,
        )


def test_kind_tag_mapping_covers_seat_and_door():
    assert KIND_TAG_TO_KIND_ID["seat"] == "sit"
    assert KIND_TAG_TO_KIND_ID["door_handle"] == "door_open"
