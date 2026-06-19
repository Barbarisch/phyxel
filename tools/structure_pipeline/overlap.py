"""
Structure Pipeline — voxel overlap detection.

A generation pipeline must never emit two voxels claiming the same space. This checks a
``.voxel`` body (C/S/M lines, mixed resolution) by mapping every voxel to the microcube
grid (the finest common resolution: 1 cube = 9³ microcubes, 1 subcube = 3³) and reporting
any microcube cell occupied more than once.

Reusable by asset generators (gen_door.py) and for auditing any template.
"""

from __future__ import annotations

from typing import Dict, List, Tuple

Cell = Tuple[int, int, int]


def _cube_cells(x: int, y: int, z: int):
    bx, by, bz = x * 9, y * 9, z * 9
    for i in range(9):
        for j in range(9):
            for k in range(9):
                yield (bx + i, by + j, bz + k)


def _subcube_cells(px, py, pz, sx, sy, sz):
    bx, by, bz = px * 9 + sx * 3, py * 9 + sy * 3, pz * 9 + sz * 3
    for i in range(3):
        for j in range(3):
            for k in range(3):
                yield (bx + i, by + j, bz + k)


def _microcube_cell(px, py, pz, sx, sy, sz, mx, my, mz) -> Cell:
    return (px * 9 + sx * 3 + mx, py * 9 + sy * 3 + my, pz * 9 + sz * 3 + mz)


def cells_for_line(line: str):
    """Yield the microcube cells a C/S/M voxel line occupies. Empty for comments/blanks."""
    parts = line.split()
    if not parts or parts[0] not in ("C", "S", "M"):
        return
    t = parts[0]
    nums = [int(p) for p in parts[1:] if p.lstrip("-").isdigit()]
    if t == "C" and len(nums) >= 3:
        yield from _cube_cells(*nums[:3])
    elif t == "S" and len(nums) >= 6:
        yield from _subcube_cells(*nums[:6])
    elif t == "M" and len(nums) >= 9:
        yield _microcube_cell(*nums[:9])


def find_overlaps(voxel_lines: List[str]) -> List[Tuple[Cell, List[int]]]:
    """Return [(microcube_cell, [line_indices]), ...] for every multiply-occupied cell."""
    occupants: Dict[Cell, List[int]] = {}
    for idx, line in enumerate(voxel_lines):
        for cell in cells_for_line(line):
            occupants.setdefault(cell, []).append(idx)
    return [(cell, idxs) for cell, idxs in occupants.items() if len(idxs) > 1]


def assert_no_overlap(voxel_lines: List[str], name: str = "template") -> None:
    """Raise ValueError if any voxels overlap, with a short diagnostic."""
    overlaps = find_overlaps(voxel_lines)
    if overlaps:
        sample = overlaps[:3]
        detail = "; ".join(
            f"cell {cell} ← lines {[voxel_lines[i].strip() for i in idxs]}" for cell, idxs in sample
        )
        raise ValueError(
            f"{name}: {len(overlaps)} overlapping microcube cell(s). e.g. {detail}"
        )
