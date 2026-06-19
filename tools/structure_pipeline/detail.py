"""
Structure Pipeline — multi-resolution detail canvas (smart voxel sizing).

The core idea: author/paint everything into ONE fine-resolution grid (the microcube
grid: 9x9x9 micro-cells per cube), then `export()` greedily coarsens each region to the
LARGEST voxel that is uniformly filled with a single material:

    uniform 9x9x9  -> 1 cube      (flat bulk is free)
    uniform 3x3x3  -> 1 subcube
    otherwise      -> microcubes  (only where geometry/material actually varies)

So resolution automatically tracks detail: bulk costs cubes, edges/bevels/mortar/ornament
cost microcubes, and you never pay for fine voxels you don't need. Detailers (chamfer,
courses, panel, ...) just paint into the canvas; the exporter handles efficiency.

Pure Python, stdlib only. Emits the engine's C/S/M .voxel lines.
"""

from __future__ import annotations

from collections import defaultdict
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

Cell = Tuple[int, int, int]   # micro-grid coordinate (9 per cube)
AIR = None

# Cell decomposition: micro coord -> (cube, subcube, micro) indices.
def split(g: int) -> Tuple[int, int, int]:
    c, r = divmod(g, 9)
    s, m = divmod(r, 3)
    return c, s, m


@dataclass
class ResolutionReport:
    cubes: int
    subcubes: int
    microcubes: int

    @property
    def total_voxels(self) -> int:
        return self.cubes + self.subcubes + self.microcubes

    @property
    def micro_cells(self) -> int:
        # what a naive all-microcube encoding would have cost
        return self.cubes * 729 + self.subcubes * 27 + self.microcubes

    def summary(self) -> str:
        naive = self.micro_cells
        saved = (1 - self.total_voxels / naive) * 100 if naive else 0
        return (f"{self.total_voxels} voxels  (C={self.cubes} S={self.subcubes} M={self.microcubes})"
                f"  vs {naive} all-micro  -> {saved:.1f}% fewer voxels")


class DetailCanvas:
    """A sparse microcube-resolution voxel grid that exports the optimal C/S/M mix."""

    def __init__(self) -> None:
        self.cells: Dict[Cell, str] = {}

    # ----- painting (micro space) -----
    def set_micro_cell(self, gx: int, gy: int, gz: int, mat: Optional[str]) -> None:
        if mat is AIR:
            self.cells.pop((gx, gy, gz), None)
        else:
            self.cells[(gx, gy, gz)] = mat

    def fill_micro_box(self, gx: int, gy: int, gz: int, gw: int, gh: int, gd: int,
                       mat: Optional[str]) -> None:
        """Fill (or carve, if mat is None) a box in micro-grid units."""
        for x in range(gx, gx + gw):
            for y in range(gy, gy + gh):
                for z in range(gz, gz + gd):
                    self.set_micro_cell(x, y, z, mat)

    # ----- painting (coarse helpers) -----
    def add_cube(self, cx: int, cy: int, cz: int, mat: str) -> None:
        self.fill_micro_box(cx * 9, cy * 9, cz * 9, 9, 9, 9, mat)

    def add_subcube(self, cx: int, cy: int, cz: int, sx: int, sy: int, sz: int, mat: str) -> None:
        self.fill_micro_box(cx * 9 + sx * 3, cy * 9 + sy * 3, cz * 9 + sz * 3, 3, 3, 3, mat)

    def add_micro(self, cx: int, cy: int, cz: int, sx: int, sy: int, sz: int,
                  mx: int, my: int, mz: int, mat: str) -> None:
        self.set_micro_cell(cx * 9 + sx * 3 + mx, cy * 9 + sy * 3 + my, cz * 9 + sz * 3 + mz, mat)

    def fill_cube_box(self, cx: int, cy: int, cz: int, w: int, h: int, d: int, mat: str) -> None:
        """Fill a w*h*d box of whole cubes (the cheap bulk path)."""
        self.fill_micro_box(cx * 9, cy * 9, cz * 9, w * 9, h * 9, d * 9, mat)

    # ----- detailers -----
    def chamfer_edge(self, gx: int, gy: int, gz: int, gw: int, gh: int, gd: int,
                     axis: str, corner: str, depth: int) -> None:
        """45-degree chamfer a box edge by carving a triangular wedge of micro-cells.

        `axis` ("x"|"y"|"z") = the edge's running direction. `corner` names which of the two
        perpendicular faces meet at the edge, e.g. "+y+z" (top-front), "-x+y" (top-left).
        `depth` = chamfer size in micro-cells.
        """
        sign = {"+x": (0, +1), "-x": (0, -1), "+y": (1, +1), "-y": (1, -1),
                "+z": (2, +1), "-z": (2, -1)}
        faces = [corner[i:i + 2] for i in range(0, len(corner), 2)]
        (a_ax, a_s), (b_ax, b_s) = sign[faces[0]], sign[faces[1]]
        lo = (gx, gy, gz)
        hi = (gx + gw - 1, gy + gh - 1, gz + gd - 1)
        for x in range(gx, gx + gw):
            for y in range(gy, gy + gh):
                for z in range(gz, gz + gd):
                    p = (x, y, z)
                    da = (p[a_ax] - lo[a_ax]) if a_s < 0 else (hi[a_ax] - p[a_ax])
                    db = (p[b_ax] - lo[b_ax]) if b_s < 0 else (hi[b_ax] - p[b_ax])
                    if da + db < depth:                     # past the diagonal cut -> air
                        self.set_micro_cell(x, y, z, AIR)

    def course_lines(self, gx: int, gy: int, gz: int, gw: int, gh: int, gd: int,
                     face: str, every: int = 9, mortar: Optional[str] = None) -> None:
        """Recess (or re-material) a 1-micro-deep horizontal mortar groove every `every`
        micro-rows on a vertical wall face. mortar=None carves the groove; a material name
        inlays it. `face` is the outward face: +z/-z/+x/-x."""
        front = face[0] == "+"
        ax = {"x": 0, "z": 2}[face[1]]
        depth_plane = (gx + gw - 1 if face == "+x" else gx) if ax == 0 else \
                      (gz + gd - 1 if face == "+z" else gz)
        for y in range(gy, gy + gh):
            if (y - gy) % every != 0 or y == gy:
                continue
            for x in range(gx, gx + gw):
                for z in range(gz, gz + gd):
                    if (ax == 0 and x == depth_plane) or (ax == 2 and z == depth_plane):
                        self.set_micro_cell(x, y, z, mortar)

    # ----- export (greedy coarsening) -----
    def export_voxels(self) -> List[tuple]:
        """Return the optimal voxel list as ('C',cx,cy,cz,mat) / ('S',...sx,sy,sz,mat) /
        ('M',...,mx,my,mz,mat) tuples."""
        bycube: Dict[Tuple[int, int, int], Dict[Tuple[int, ...], str]] = defaultdict(dict)
        for (gx, gy, gz), mat in self.cells.items():
            cx, sx, mx = split(gx)
            cy, sy, my = split(gy)
            cz, sz, mz = split(gz)
            bycube[(cx, cy, cz)][(sx, sy, sz, mx, my, mz)] = mat

        out: List[tuple] = []
        for (cx, cy, cz), cells in sorted(bycube.items()):
            if len(cells) == 729 and len(set(cells.values())) == 1:
                out.append(("C", cx, cy, cz, next(iter(cells.values()))))
                continue
            bysub: Dict[Tuple[int, int, int], Dict[Tuple[int, int, int], str]] = defaultdict(dict)
            for (sx, sy, sz, mx, my, mz), mat in cells.items():
                bysub[(sx, sy, sz)][(mx, my, mz)] = mat
            for sx in range(3):
                for sy in range(3):
                    for sz in range(3):
                        sub = bysub.get((sx, sy, sz))
                        if not sub:
                            continue
                        if len(sub) == 27 and len(set(sub.values())) == 1:
                            out.append(("S", cx, cy, cz, sx, sy, sz, next(iter(sub.values()))))
                        else:
                            for (mx, my, mz), mat in sorted(sub.items()):
                                out.append(("M", cx, cy, cz, sx, sy, sz, mx, my, mz, mat))
        return out

    def report(self) -> ResolutionReport:
        c = s = m = 0
        for v in self.export_voxels():
            c += v[0] == "C"
            s += v[0] == "S"
            m += v[0] == "M"
        return ResolutionReport(c, s, m)

    def to_voxel_lines(self) -> List[str]:
        lines = []
        for v in self.export_voxels():
            if v[0] == "C":
                lines.append(f"C {v[1]} {v[2]} {v[3]} {v[4]}")
            elif v[0] == "S":
                lines.append(f"S {v[1]} {v[2]} {v[3]} {v[4]} {v[5]} {v[6]} {v[7]}")
            else:
                lines.append(f"M {v[1]} {v[2]} {v[3]} {v[4]} {v[5]} {v[6]} {v[7]} {v[8]} {v[9]} {v[10]}")
        return lines


# --------------------------------------------------------------------------- demo assets

def demo_ashlar_pillar(stone: str = "Stone", trim: str = "StoneBricks") -> DetailCanvas:
    """A 2x2 footprint stone pillar: cube shaft (bulk) + chamfered base/capital (microcube
    bevels) + a subcube molding band — exercises all three resolutions on purpose."""
    c = DetailCanvas()
    W = 2  # cubes
    # Base plinth (y=0): solid cubes, top outer edges chamfered.
    c.fill_cube_box(0, 0, 0, W, 1, W, stone)
    g = W * 9
    for face_axis, corner in (("x", "+y-x"), ("x", "+y+x"), ("z", "+y-z"), ("z", "+y+z")):
        # chamfer the four top edges of the plinth
        c.chamfer_edge(0, 0, 0, g, 9, g, face_axis, corner, depth=3)
    # Shaft (y=1..3): solid cube bulk.
    c.fill_cube_box(0, 1, 0, W, 3, W, stone)
    # Molding band at the top of the shaft: a subcube ring of trim around the perimeter (y=3 top).
    for sx in range(W * 3):
        for sz in range(W * 3):
            if sx in (0, W * 3 - 1) or sz in (0, W * 3 - 1):  # perimeter subcubes
                cx, ssx = divmod(sx, 3)
                cz, ssz = divmod(sz, 3)
                c.add_subcube(cx, 3, cz, ssx, 2, ssz, trim)  # top subcube row of shaft cube y=3
    # Capital (y=4): solid cubes, bottom outer edges chamfered (inverted).
    c.fill_cube_box(0, 4, 0, W, 1, W, stone)
    for face_axis, corner in (("x", "-y-x"), ("x", "-y+x"), ("z", "-y-z"), ("z", "-y+z")):
        c.chamfer_edge(0, 4 * 9, 0, g, 9, g, face_axis, corner, depth=3)
    return c


DEMOS = {"pillar": demo_ashlar_pillar}


def main(argv=None) -> int:
    import argparse
    import sys
    from pathlib import Path

    ap = argparse.ArgumentParser(prog="structure_pipeline.detail",
                                 description="Generate a multi-resolution voxel asset.")
    ap.add_argument("demo", choices=sorted(DEMOS), nargs="?", default="pillar")
    ap.add_argument("--out", type=Path, help="write the .voxel here")
    ap.add_argument("--name", default=None, help="template name comment")
    args = ap.parse_args(argv)

    canvas = DEMOS[args.demo]()
    lines = canvas.to_voxel_lines()
    rep = canvas.report()
    print(f"[detail] {args.demo}: {rep.summary()}", file=sys.stderr)
    body = [f"# {args.name or args.demo} - multi-resolution (smart voxel sizing)",
            "# Format: C cx cy cz Mat | S .. sx sy sz Mat | M .. mx my mz Mat", ""] + lines
    text = "\n".join(body) + "\n"
    if args.out:
        args.out.write_text(text, encoding="utf-8")
        print(f"[detail] wrote {args.out}", file=sys.stderr)
    else:
        print(text)
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
