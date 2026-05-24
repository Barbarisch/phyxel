"""Stage 1 validator tests: validate_asset_metrics + door feature extraction.

These exercise the real .voxel templates in resources/templates/ so we know
the gate accepts annotated assets and rejects broken ones.
"""
from __future__ import annotations

from pathlib import Path

import pytest

from tools.interaction_pipeline.asset_metrics import (
    AssetIssue,
    AssetMetrics,
    InteractionPointMetrics,
    SCHEMA_VERSION,
    asset_provenance,
    characterize_asset,
    extract_door_features,
    parse_voxel_template,
    validate_asset_metrics,
)

ROOT = Path(__file__).resolve().parents[1]
TEMPLATES = ROOT / "resources" / "templates"


# ---------------------------------------------------------------------------
# Real templates pass the gate
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("name", ["chair_wood", "test_chair"])
def test_seat_templates_pass_validation(name):
    metrics = characterize_asset(TEMPLATES / f"{name}.voxel")
    issues = [i for i in validate_asset_metrics(metrics) if i.severity == "error"]
    assert issues == [], f"{name}: unexpected errors {issues}"
    assert any(p.kind == "seat" for p in metrics.interaction_points)


@pytest.mark.parametrize("name", ["door_wood", "door_wood_wide", "door_metal"])
def test_door_templates_pass_validation(name):
    metrics = characterize_asset(TEMPLATES / f"{name}.voxel")
    issues = [i for i in validate_asset_metrics(metrics) if i.severity == "error"]
    assert issues == [], f"{name}: unexpected errors {issues}"
    door = next(p for p in metrics.interaction_points if p.kind == "door_handle")
    assert door.features.get("panel_width", 0.0) > 0.0
    assert door.features.get("panel_height", 0.0) > 0.0


# ---------------------------------------------------------------------------
# Validator catches breakage
# ---------------------------------------------------------------------------

def _seat_metrics():
    return AssetMetrics(
        schema_version=SCHEMA_VERSION,
        template_name="t",
        overall_min=(0.0, 0.0, 0.0),
        overall_max=(1.0, 1.0, 1.0),
        interaction_points=[
            InteractionPointMetrics(
                point_id="seat_0", kind="seat",
                local_position=(0.5, 0.5, 0.5),
                facing_yaw=0.0,
                features={"seat_top_y": 0.5, "seat_width_x": 0.5,
                          "seat_depth_z": 0.5, "seat_center": [0.5, 0.5, 0.5]},
            ),
        ],
    )


def test_gate_rejects_missing_required_seat_feature():
    m = _seat_metrics()
    del m.interaction_points[0].features["seat_top_y"]
    issues = validate_asset_metrics(m)
    assert any(i.rule_id == "FEATURES_INCOMPLETE" for i in issues)


def test_gate_rejects_non_positive_seat_dims():
    m = _seat_metrics()
    m.interaction_points[0].features["seat_width_x"] = 0.0
    issues = validate_asset_metrics(m)
    assert any(i.rule_id == "SEAT_DIMS_NON_POSITIVE" for i in issues)


def test_gate_warns_on_position_outside_aabb():
    m = _seat_metrics()
    m.interaction_points[0].local_position = (5.0, 0.5, 0.5)
    issues = validate_asset_metrics(m)
    warns = [i for i in issues if i.rule_id == "POSITION_OUTSIDE_AABB"]
    assert warns and warns[0].severity == "warn"


def test_gate_rejects_duplicate_point_ids():
    m = _seat_metrics()
    m.interaction_points.append(m.interaction_points[0])
    issues = validate_asset_metrics(m)
    assert any(i.rule_id == "POINT_ID_DUPLICATE" for i in issues)


def test_gate_rejects_no_points():
    m = AssetMetrics(SCHEMA_VERSION, "t", (0, 0, 0), (1, 1, 1), [])
    issues = validate_asset_metrics(m)
    assert any(i.rule_id == "NO_INTERACTION_POINTS" for i in issues)


def test_gate_rejects_inverted_aabb():
    m = _seat_metrics()
    m.overall_max = (-1.0, -1.0, -1.0)
    issues = validate_asset_metrics(m)
    assert any(i.rule_id == "AABB_INVERTED" for i in issues)


# ---------------------------------------------------------------------------
# Door feature extractor
# ---------------------------------------------------------------------------

def test_door_features_geometry():
    metrics = characterize_asset(TEMPLATES / "door_wood.voxel")
    door = next(p for p in metrics.interaction_points if p.kind == "door_handle")
    f = door.features
    # door_wood is 1m wide (X), 2m tall (Y), 1m deep (Z)
    assert pytest.approx(f["panel_width"], abs=0.01) == 1.0
    assert pytest.approx(f["panel_height"], abs=0.01) == 2.0
    # Hinge X opposite to handle X: handle is at x=0.5, panel spans x∈[0,1]
    hinge_x = f["hinge_xz"][0]
    assert hinge_x in (0.0, 1.0)
    assert abs(hinge_x - 0.5) > 0.4  # actually on an edge, not centre


# ---------------------------------------------------------------------------
# Provenance
# ---------------------------------------------------------------------------

def test_asset_provenance_round_trip():
    metrics = characterize_asset(TEMPLATES / "chair_wood.voxel")
    prov = asset_provenance(metrics, "seat_0")
    assert prov["template_name"] == "chair_wood"
    assert prov["kind"] == "seat"
    assert "seat_top_y" in prov["features"]
    assert prov["schema_version"] == SCHEMA_VERSION


def test_asset_provenance_missing_point_raises():
    metrics = characterize_asset(TEMPLATES / "chair_wood.voxel")
    with pytest.raises(KeyError):
        asset_provenance(metrics, "nope_0")
