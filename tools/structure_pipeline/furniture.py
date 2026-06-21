"""
Structure Pipeline — detailed multi-resolution furniture generator.

The old furniture templates (chair_wood=16, bed_single=47, table_wood=17 voxels) are crude
subcube-only blocks with ZERO microcube detail — they look bad and violate the "exploit
subcubes/microcubes, never default to full cubes" rule. This module paints furniture into a
DetailCanvas at microcube resolution (turned/tapered legs, beveled edges, slatted chair backs,
a real mattress + pillow + headboard) and the canvas exports the optimal C/S/M mix.

Scale is anchored to the engine's character canon (1 cube = 1 m, 9 micro-cells = 1 cube):
seat top ~0.667, chair back ~1.5, table top ~0.87, mattress top ~0.5. The chair preserves the
`interaction_point` the seating system needs so sit_character still works.

CLI: `python -m structure_pipeline.furniture --all` writes the templates into resources/templates/.
"""

from __future__ import annotations

from pathlib import Path
from typing import List, Optional

from .detail import DetailCanvas, AIR

_REPO = Path(__file__).resolve().parents[2]
TEMPLATES_DIR = _REPO / "resources" / "templates"

OAK = "Wood"      # planks: slabs, panels, seats, tops
LOG = "Log"       # bark posts/legs — reads as turned/round timber
LINEN = "Sandstone"   # mattress (soft layered look) — bedding-material gap noted in gaps doc
PILLOW = "Sand"   # lighter pillow


# --------------------------------------------------------------------------- helpers

def _bevel_top(c: DetailCanvas, gx: int, gy: int, gz: int, gw: int, gd: int, depth: int = 1) -> None:
    """Chamfer the four top edges of a slab at micro rows [gy, gy] (1-cell-thick relief)."""
    for axis, corner in (("x", "+y+z"), ("x", "+y-z"), ("z", "+y+x"), ("z", "+y-x")):
        c.chamfer_edge(gx, gy, gz, gw, 1, gd, axis, corner, depth)


# --------------------------------------------------------------------------- pieces

def make_chair(seat=OAK, frame=OAK) -> DetailCanvas:
    """Dining/hall chair: 4 legs, beveled seat at 0.667 (matches the sit calibration), and a
    sensible ~1.1-cube back with two slats. Footprint 0.667 cube (6 micro). Clean Wood throughout
    (Log bark reads as heavy timber at this size)."""
    c = DetailCanvas()
    W = 6                      # footprint micro (0.667 cube)
    seat_y = 6                 # 0.667 cube — keep for the seating interaction_point
    back_top = 10              # ~1.11 cube — a chair back, not a throne
    legs = [(0, 0), (W - 2, 0), (0, W - 2), (W - 2, W - 2)]
    for lx, lz in legs:
        c.fill_micro_box(lx, 0, lz, 2, seat_y, 2, frame)
    c.fill_micro_box(0, 2, 0, W, 1, 1, frame)                     # low stretcher rails
    c.fill_micro_box(0, 2, W - 1, W, 1, 1, frame)
    c.fill_micro_box(0, seat_y, 0, W, 1, W, seat)                 # seat slab + beveled edges
    _bevel_top(c, 0, seat_y, 0, W, W, 1)
    # backrest: two posts at the rear (z=0) + a top rail + two vertical slats
    c.fill_micro_box(0, seat_y, 0, 2, back_top - seat_y, 1, frame)
    c.fill_micro_box(W - 2, seat_y, 0, 2, back_top - seat_y, 1, frame)
    c.fill_micro_box(0, back_top - 1, 0, W, 1, 1, seat)           # top rail
    for sx in (2, 3):
        c.fill_micro_box(sx, seat_y + 1, 0, 1, back_top - seat_y - 2, 1, seat)
    return c


def make_table(top=OAK, frame=OAK) -> DetailCanvas:
    """Dining table: 4 legs + apron rails + a beveled overhanging top at ~0.87 cube.
    2 cubes long (X 0..17), 1 cube deep (Z 0..8)."""
    c = DetailCanvas()
    L, D = 18, 9
    top_y = 8                  # 0.87 cube
    for lx in (1, L - 3):
        for lz in (1, D - 3):
            c.fill_micro_box(lx, 0, lz, 2, top_y - 1, 2, frame)    # legs to just under the top
    # apron rails just under the top
    c.fill_micro_box(1, top_y - 2, 1, L - 2, 1, 1, top)
    c.fill_micro_box(1, top_y - 2, D - 2, L - 2, 1, 1, top)
    c.fill_micro_box(1, top_y - 2, 1, 1, 1, D - 2, top)
    c.fill_micro_box(L - 2, top_y - 2, 1, 1, 1, D - 2, top)
    # overhanging top slab (1 micro) with beveled edges all round
    c.fill_micro_box(0, top_y, 0, L, 1, D, top)
    _bevel_top(c, 0, top_y, 0, L, D, 1)
    return c


def make_bed(frame=OAK, wood=OAK, mattress=LINEN, pillow=PILLOW) -> DetailCanvas:
    """Single bed: tall headboard posts + panel, low foot posts, side rails, a mattress with a
    pillow and a folded blanket. 1 cube wide (Z 0..8), 2 cubes long (X 0..17). Head at X=0."""
    c = DetailCanvas()
    L, Dd = 18, 9
    rail_y, matt_y = 3, 4
    # posts: tall at the head (x=0), low at the foot (x=L-2)
    c.fill_micro_box(0, 0, 0, 2, 9, 2, frame)
    c.fill_micro_box(0, 0, Dd - 2, 2, 9, 2, frame)
    c.fill_micro_box(L - 2, 0, 0, 2, 5, 2, frame)
    c.fill_micro_box(L - 2, 0, Dd - 2, 2, 5, 2, frame)
    # headboard panel between the head posts
    c.fill_micro_box(0, rail_y, 0, 1, 6, Dd, wood)
    # side + foot rails
    c.fill_micro_box(0, rail_y, 0, L, 1, 1, wood)
    c.fill_micro_box(0, rail_y, Dd - 1, L, 1, 1, wood)
    c.fill_micro_box(L - 2, rail_y, 0, 2, 1, Dd, wood)
    # mattress (inset, 2 micro thick)
    c.fill_micro_box(1, matt_y, 1, L - 3, 2, Dd - 2, mattress)
    _bevel_top(c, 1, matt_y + 1, 1, L - 3, Dd - 2, 1)
    # pillow at the head end
    c.fill_micro_box(2, matt_y + 2, 1, 3, 1, Dd - 2, pillow)
    # folded blanket over the lower half
    c.fill_micro_box(9, matt_y + 2, 1, L - 11, 1, Dd - 2, wood)
    return c


# --------------------------------------------------------------------------- emit

# name -> (builder, display, extra header lines incl. seating interaction points)
PIECES = {
    "chair_wood": (make_chair, "Wooden Chair",
                   ["# facing:       +Z (seat faces +Z, backrest at -Z)",
                    "# seat_height:  0.667",
                    "interaction_point: seat_0 seat 0.333 0.667 0.333 0.0 *"]),
    "table_wood": (make_table, "Wooden Table", ["# facing:       +Z"]),
    "bed_single": (make_bed, "Single Bed",
                   ["# facing:       +Z (foot of bed faces +Z)", "# seat_height:  0.556  (mattress top)"]),
}


def write_piece(name: str) -> Path:
    builder, display, extra = PIECES[name]
    canvas = builder()
    rep = canvas.report()
    header = [
        "# ==========================================================",
        "# ASSET METADATA",
        f"# name:         {name}",
        f"# display_name: {display}",
        "# category:     furniture",
        "# materials:    Wood, Log, Sandstone, Sand",
        *extra,
        f"# primitives:   {rep.cubes} C + {rep.subcubes} S + {rep.microcubes} M = {rep.total_voxels}",
        "# method:       structure_pipeline.furniture (multi-resolution DetailCanvas)",
        "# ==========================================================",
        "# Format: C cx cy cz Mat | S .. sx sy sz Mat | M .. mx my mz Mat",
        "",
    ]
    body = header + canvas.to_voxel_lines()
    path = TEMPLATES_DIR / f"{name}.voxel"
    path.write_text("\n".join(body) + "\n", encoding="utf-8")
    return path


def main(argv=None) -> int:
    import argparse
    import sys
    ap = argparse.ArgumentParser(prog="structure_pipeline.furniture",
                                 description="Generate detailed multi-resolution furniture templates.")
    ap.add_argument("pieces", nargs="*", default=None,
                    help="piece names (default: all): " + " ".join(PIECES))
    ap.add_argument("--all", action="store_true", help="write every piece")
    args = ap.parse_args(argv)
    names = list(PIECES) if (args.all or not args.pieces) else args.pieces
    for n in names:
        if n not in PIECES:
            print(f"unknown piece '{n}' (have: {', '.join(PIECES)})", file=sys.stderr)
            continue
        p = write_piece(n)
        from .detail import DetailCanvas  # noqa
        print(f"[furniture] wrote {p}  ({PIECES[n][0]().report().summary()})", file=sys.stderr)
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
