#!/usr/bin/env python3
"""
Generate paneled door templates for Phyxel's DoorManager.

Authors a thin door *leaf* as a single subcube-deep layer (parent cube z=0, subcube z=0)
in the X-Y plane, with a darker Log frame (stiles + rails + center mullion on wide doors),
lighter Wood panels, and a Gold microcube knob on the latch side. The hinge stile is at local
X=0 (DoorManager pivots there). registerDoor compresses the leaf along Z into a thin slab, so
the authored single subcube layer renders microcube-thin.

Run:  python tools/gen_door.py [--grid 8]
Writes resources/templates/architecture/door_wood{,_wide}.voxel + matching .metrics.json (the library
taxonomy folder since 2026-08-07; this script used to write to the template root).

--grid 8 (docs/VoxelFoundationPlan.md 7a.3 step 4): the same door DESIGNED IN METRES on the 1/8 m grid,
not the thirds rescaled -- frame members are one cell (0.125 m, near a real ~0.11 m stile; today's are
1/3 m subcubes), a one-cell knob proud of the leaf. Output resources/templates_g8/architecture as
'# grid: 8' B/V lines (tools/voxgrid.py), no .metrics.json (the characterizer reads C/S/M only).
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


def build_door_grid(width_m: int, height_m: int, n: int):
    """The door in metres on n cells per metre. Returns (grid_lines, handle_local_xyz_metres)."""
    from voxgrid import GridVoxels, emit_grid
    W, H = width_m * n, height_m * n
    mid = H // 2                                    # mid rail at half height, as legacy
    mullion = W // 2 if width_m > 1 else -1         # wide doors only
    gv = GridVoxels(n)
    for x in range(W):
        for y in range(H):
            frame = x in (0, W - 1) or y in (0, H - 1, mid) or x == mullion
            gv.set(x, y, 0, FRAME if frame else PANEL)
    # knob: the latch-side panel column next to the stile, one cell under the mid rail, one cell proud
    kx, ky = W - 2, mid - 1
    gv.set(kx, ky, 1, HANDLE)
    lines, _, _ = emit_grid(gv)
    return lines, [round((kx + 0.5) / n, 4), round((ky + 0.5) / n, 4), round(1.5 / n, 4)]


def default_out_dir(grid: int = 3):
    return TEMPLATES / "architecture" if grid in (3, 9) else (TEMPLATES.parent / f"templates_g{grid}" / "architecture")


def write_door(name: str, width_cubes: int, height_cubes: int, out_dir=None, grid: int = 3):
    out = Path(out_dir) if out_dir is not None else default_out_dir(grid)
    out.mkdir(parents=True, exist_ok=True)
    if grid not in (3, 9):
        glines, h = build_door_grid(width_cubes, height_cubes, grid)
        text = [glines[0],
                f"# {'Wide ' if width_cubes > 1 else ''}wooden door - {width_cubes} m wide (X), {height_cubes} m "
                f"tall (Y), one 1/{grid} m cell deep. Hinge edge at local X=0, Z=0. Log frame, Wood panels, Gold knob.",
                f"# interaction_point: handle_0 door_handle {h[0]} {h[1]} {h[2]} 0.0 * 2.00 \"Open / Close\" 90.0",
                f"# method: tools/gen_door.py --grid {grid} (designed in metres)",
                ""] + glines[1:]
        (out / f"{name}.voxel").write_text("\n".join(text) + "\n", encoding="utf-8", newline="\n")
        print(f"{name}.voxel (grid {grid}): {len(glines) - 1} boxes  (handle @ {h}) -> {out}")
        return
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
    voxel_path = out / f"{name}.voxel"
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
    (out / f"{name}.metrics.json").write_text(json.dumps(metrics, indent=2), encoding="utf-8")

    n_sub = sum(1 for l in lines if l.startswith("S "))
    n_mic = sum(1 for l in lines if l.startswith("M "))
    print(f"{name}.voxel: {n_sub} subcubes + {n_mic} microcubes  (handle @ {handle})")


if __name__ == "__main__":
    _grid = int(sys.argv[sys.argv.index("--grid") + 1]) if "--grid" in sys.argv else 3
    write_door("door_wood", 1, 2, grid=_grid)
    write_door("door_wood_wide", 2, 2, grid=_grid)
