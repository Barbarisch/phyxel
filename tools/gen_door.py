#!/usr/bin/env python3
"""
Generate paneled door templates for Phyxel's DoorManager.

Authors a thin door *leaf* as a single subcube-deep layer (parent cube z=0, subcube z=0)
in the X-Y plane, with a darker Log frame (stiles + rails + center mullion on wide doors),
lighter Wood panels, and a Gold microcube knob on the latch side. The hinge stile is at local
X=0 (DoorManager pivots there). registerDoor compresses the leaf along Z into a thin slab, so
the authored single subcube layer renders microcube-thin.

Run:  python tools/gen_door.py
Writes resources/templates/door_wood{,_wide}.voxel + matching .metrics.json.
"""

import json
import os
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # tools/ — for structure_pipeline
from structure_pipeline.overlap import assert_no_overlap  # noqa: E402

TEMPLATES = Path(__file__).resolve().parents[1] / "resources" / "templates"

FRAME  = "Log"    # darker bark — stiles / rails / mullion
PANEL  = "Wood"   # lighter planks — recessed-look panel fill
HANDLE = "Gold"   # knob


def build_door(width_cubes: int, height_cubes: int):
    """Return (voxel_lines, handle_local_xyz) for a door of the given cube size."""
    cols = 3 * width_cubes      # subcube columns (X)
    rows = 3 * height_cubes     # subcube rows (Y)
    mid_row = rows // 2         # horizontal mid rail
    mullion = cols // 2 if width_cubes > 1 else -1  # vertical center mullion (wide doors only)

    lines = []

    def sub(cx, ry, mat):
        px, sx = divmod(cx, 3)
        py, sy = divmod(ry, 3)
        lines.append(f"S {px} {py} 0 {sx} {sy} 0 {mat}")

    for cx in range(cols):
        for ry in range(rows):
            is_frame = (
                cx == 0 or cx == cols - 1 or          # stiles
                ry == 0 or ry == rows - 1 or          # top/bottom rails
                ry == mid_row or                       # mid rail
                (mullion >= 0 and cx == mullion)       # center mullion
            )
            sub(cx, ry, FRAME if is_frame else PANEL)

    # Knob: two Gold microcubes stacked on the latch side (away from the X=0 hinge), just below
    # the mid rail. Placed on the FRONT layer (subcube z=1) so the knob sits proud of the panel
    # (subcube z=0) and never shares a cell with it — no overlap.
    hcx = cols - 2                                  # latch-side panel column
    hry = mid_row - 1 if mid_row - 1 > 0 else mid_row + 1
    px, sx = divmod(hcx, 3)
    py, sy = divmod(hry, 3)
    for my in (0, 1):
        lines.append(f"M {px} {py} 0 {sx} {sy} 1 1 {my} 0 {HANDLE}")

    assert_no_overlap(lines, f"door {width_cubes}x{height_cubes}")
    handle_local = [round((hcx + 0.5) / 3, 3), round((hry + 0.5) / 3, 3), 1.0]
    return lines, handle_local


def write_door(name: str, width_cubes: int, height_cubes: int):
    lines, handle = build_door(width_cubes, height_cubes)
    header = [
        f"# {'Wide ' if width_cubes > 1 else ''}wooden door - "
        f"{width_cubes} wide (X), {height_cubes} tall (Y), thin (renders microcube-thin via DoorManager)",
        "# Hinge edge at local X=0, Z=0. Door extends along +X. Paneled: Log frame, Wood panels, Gold knob.",
        f"# interaction_point: handle_0 door_handle {handle[0]} {handle[1]} {handle[2]} 0.0 * 2.00 \"Open / Close\" 90.0",
        "# Format: S px py pz sx sy sz Material  |  M px py pz sx sy sz mx my mz Material",
        # NOTE: no movable '# part:' hinge directive — a door is plain static geometry.
        # DoorManager::registerDoor creates the single swinging kinematic leaf. A movable-part
        # directive here would make spawn_template auto-create a SECOND kinematic (the panel),
        # which stays closed while DoorManager's swings — the "ghost door" L-shape bug.
        "",
    ]
    voxel_path = TEMPLATES / f"{name}.voxel"
    voxel_path.write_text("\n".join(header + lines) + "\n", encoding="utf-8")

    metrics = {
        "schema_version": "asset_metrics.v1",
        "template_name": name,
        "overall_min": [0.0, 0.0, 0.0],
        "overall_max": [float(width_cubes), float(height_cubes), 1.0],
        "interaction_points": [{
            "point_id": "handle_0",
            "kind": "door_handle",
            "local_position": handle,
            "facing_yaw": 0.0,
            "features": {},
        }],
    }
    (TEMPLATES / f"{name}.metrics.json").write_text(json.dumps(metrics, indent=2), encoding="utf-8")

    n_sub = sum(1 for l in lines if l.startswith("S "))
    n_mic = sum(1 for l in lines if l.startswith("M "))
    print(f"{name}.voxel: {n_sub} subcubes + {n_mic} microcubes  (handle @ {handle})")


if __name__ == "__main__":
    write_door("door_wood", 1, 2)
    write_door("door_wood_wide", 2, 2)
