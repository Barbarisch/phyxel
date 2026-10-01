"""A4 item 1: `asset_metrics.v2` — seat sidecars gain backrest_angle_deg, armrests[] and approach,
derived from the voxels by tools/interaction_pipeline/asset_metrics.py. Additive over v1: every v1
field is unchanged. RED 2026-09-30: no sidecar carries the v2 fields."""
from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "interaction_pipeline"))

import asset_metrics as am  # noqa: E402

FURN = ROOT / "resources/templates/furniture"


def box(x0, y0, z0, x1, y1, z1, mat="Wood"):
    return am.VoxelBox(mn=(x0, y0, z0), mx=(x1, y1, z1), material=mat)


def synthetic_armchair(angle_deg: float = 15.0):
    """A seat slab 0.6x0.6 at y=0.45, a raked backrest behind it (+Z is the front), two armrests."""
    import math
    boxes = [box(0.0, 0.40, 0.0, 0.6, 0.45, 0.6)]                    # seat slab, top 0.45
    slope = math.tan(math.radians(angle_deg))
    for i in range(8):                                                  # backrest: 8 courses of 0.10 from 0.55, leaning back (-Z)
        y0 = 0.55 + i * 0.10                                            # (starts above the seat-top tolerance band)
        yc = y0 + 0.05 - 0.45                                           # height of the course centre above the seat
        z_front = 0.0 - slope * yc                                      # front face moves to -Z with height
        boxes.append(box(0.0, y0, z_front - 0.05, 0.6, y0 + 0.10, z_front))
    boxes.append(box(-0.08, 0.45, 0.05, 0.0, 0.70, 0.55))               # left armrest, top 0.70
    boxes.append(box(0.6, 0.45, 0.05, 0.68, 0.70, 0.55))                # right armrest
    boxes.append(box(0.0, 0.0, 0.0, 0.6, 0.40, 0.6))                    # legs/base to the floor
    point = am.InteractionPointDecl(point_id="seat_0", kind="seat", local_position=(0.3, 0.45, 0.3), facing_yaw=0.0)
    return boxes, point


def test_synthetic_armchair_yields_angle_two_armrests_and_an_approach_point():
    boxes, point = synthetic_armchair(15.0)
    f = am.extract_seat_features(boxes, point)
    assert abs(f.backrest_angle_deg - 15.0) < 2.0, f.backrest_angle_deg
    assert len(f.armrests) == 2, f.armrests
    sides = {a["side"] for a in f.armrests}
    assert sides == {"left", "right"}
    for a in f.armrests:
        assert abs(a["top_y"] - 0.70) < 1e-6
    left = next(a for a in f.armrests if a["side"] == "left")
    assert abs(left["inner_x"] - 0.0) < 1e-6
    assert f.approach is not None
    ax, ay, az = f.approach
    assert abs(ax - 0.3) < 1e-6 and abs(ay - 0.0) < 1e-6 and abs(az - (0.6 + 0.45)) < 1e-6


def test_upright_backrest_reads_zero_angle_and_a_bench_has_no_backrest_or_arms():
    boxes, point = synthetic_armchair(0.0)
    f = am.extract_seat_features(boxes, point)
    assert abs(f.backrest_angle_deg) < 0.5
    bench = [box(0.0, 0.40, 0.0, 1.4, 0.45, 0.45), box(0.0, 0.0, 0.0, 1.4, 0.40, 0.45)]
    fb = am.extract_seat_features(bench, am.InteractionPointDecl("seat_0", "seat", (0.7, 0.45, 0.22), 0.0))
    assert not fb.backrest_present and fb.backrest_angle_deg == 0.0 and fb.armrests == []
    assert fb.approach is not None


def test_shipped_seat_sidecars_are_v2_and_keep_their_v1_fields():
    seats = ["chair_wood", "bench_wood", "stool", "stool_low", "bar_stool", "bench_great"]
    for stem in seats:
        side = json.loads((FURN / f"{stem}.metrics.json").read_text(encoding="utf-8"))
        assert side["schema_version"] == "asset_metrics.v2", f"{stem}: run python tools/characterize_asset.py --all"
        feats = [p["features"] for p in side["interaction_points"] if p["kind"] == "seat"]
        assert feats, stem
        for f in feats:
            for k in ("seat_top_y", "seat_width_x", "seat_depth_z", "seat_center", "front_edge_z",
                      "backrest_height", "backrest_present", "backrest_angle_deg", "armrests", "approach"):
                assert k in f, f"{stem}: {k}"
        # the characterizer regenerates the same v1 numbers from the voxels (one source)
        fresh = am.characterize_asset(FURN / f"{stem}.voxel").to_dict()
        fresh_feats = [p["features"] for p in fresh["interaction_points"] if p["kind"] == "seat"]
        for a, b in zip(feats, fresh_feats):
            for k in ("seat_top_y", "seat_width_x", "seat_depth_z", "backrest_height"):
                assert abs(a[k] - b[k]) < 1e-6, f"{stem}: {k} {a[k]} vs {b[k]}"


def test_chair_wood_backrest_is_upright():
    side = json.loads((FURN / "chair_wood.metrics.json").read_text(encoding="utf-8"))
    f = side["interaction_points"][0]["features"]
    assert f["backrest_present"] and abs(f["backrest_angle_deg"]) < 3.0
