#!/usr/bin/env python3
"""voxgrid — the grid-agnostic voxel emitter for the 1/8 m foundation (docs/VoxelFoundationPlan.md §7a).

Generators draw on a sparse canvas of integer cells at `cells_per_m` (8 = world voxel, 1/8 m; 64 = item voxel,
1/64 m) and emit a `.voxel` template in the grid format the engine already defines for fine items:

    # grid: N
    B x y z sx sy sz Mat     an axis-aligned box of sx*sy*sz cells, min corner (x, y, z)
    V x y z Mat              one cell (a 1x1x1 box)

All coordinates are cells, rebased so the lowest cell of the template is at 0 on every axis. Boxes come from a
deterministic greedy merge (runs along z, then grown along y, then x) of same-material cells; they only make the
file smaller, the loader may split them again. Same canvas -> byte-identical output.

This is the grid-8 / grid-64 counterpart of forge_core.emit() (which writes today's C/S/M levels). The engine
loader today accepts only `# grid: 27/81`; the voxfound lab reads grid 8 (StorageLab), the live engine takes it
when the block store becomes the default (F7).
"""

WORLD_CELLS_PER_M = 8      # world voxel: 1/8 m
ITEM_CELLS_PER_M = 64      # item voxel: 1/64 m (an 8^3 split of a world voxel)


class GridVoxels:
    """Sparse cell canvas: (x, y, z) integer cells -> material name."""

    __slots__ = ("cells_per_m", "v")

    def __init__(self, cells_per_m=WORLD_CELLS_PER_M):
        self.cells_per_m = cells_per_m
        self.v = {}

    def set(self, x, y, z, mat, overwrite=True):
        k = (x, y, z)
        if overwrite or k not in self.v:
            self.v[k] = mat

    def __len__(self):
        return len(self.v)


def greedy_boxes(cells):
    """cells: dict (x,y,z)->mat. Returns [(x, y, z, sx, sy, sz, mat)] covering every cell exactly once."""
    left = dict(cells)
    out = []
    for key in sorted(cells, key=lambda k: (k[1], k[0], k[2])):
        if key not in left:
            continue
        mat = left[key]
        x, y, z = key
        sz = 1
        while left.get((x, y, z + sz)) == mat:
            sz += 1

        def row_free(xx, yy):
            return all(left.get((xx, yy, z + k)) == mat for k in range(sz))

        sy = 1
        while row_free(x, y + sy):
            sy += 1

        def slab_free(xx):
            return all(row_free(xx, y + j) for j in range(sy))

        sx = 1
        while slab_free(x + sx):
            sx += 1
        for i in range(sx):
            for j in range(sy):
                for k in range(sz):
                    del left[(x + i, y + j, z + k)]
        out.append((x, y, z, sx, sy, sz, mat))
    return out


def emit_grid(gv):
    """Returns (lines, (boxes, cells), bounds_cells). lines start with '# grid: N'."""
    if not gv.v:
        return [f"# grid: {gv.cells_per_m}"], (0, 0), (0, 0, 0)
    ox = min(k[0] for k in gv.v)
    oy = min(k[1] for k in gv.v)
    oz = min(k[2] for k in gv.v)
    rebased = {(x - ox, y - oy, z - oz): m for (x, y, z), m in gv.v.items()}
    bounds = tuple(max(k[i] for k in rebased) + 1 for i in range(3))
    lines = [f"# grid: {gv.cells_per_m}"]
    boxes = greedy_boxes(rebased)
    for (x, y, z, sx, sy, sz, mat) in boxes:
        if sx == sy == sz == 1:
            lines.append(f"V {x} {y} {z} {mat}")
        else:
            lines.append(f"B {x} {y} {z} {sx} {sy} {sz} {mat}")
    return lines, (len(boxes), len(rebased)), bounds


def parse_grid(lines):
    """Inverse of emit_grid for tests: returns (cells_per_m, {(x,y,z): mat})."""
    n, cells = None, {}
    for ln in lines:
        ln = ln.strip()
        if ln.startswith("# grid:"):
            n = int(ln.split(":", 1)[1])
            continue
        if not ln or ln.startswith("#"):
            continue
        t = ln.split()
        if t[0] == "V":
            cells[(int(t[1]), int(t[2]), int(t[3]))] = t[4]
        elif t[0] == "B":
            x, y, z, sx, sy, sz = map(int, t[1:7])
            for i in range(sx):
                for j in range(sy):
                    for k in range(sz):
                        cells[(x + i, y + j, z + k)] = t[7]
    return n, cells
