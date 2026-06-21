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
KNOB = "Gold"     # drawer/door knobs

# Book-spine colours from the current palette (a dedicated cloth/leather-binding material is a gap —
# see docs/MaterialTextureNeeds.md). Cycled to give a shelf varied spines.
BOOK_SPINES = ("Bricks", "Sandstone", "Log", "Gold", "Metal", "Leaf", "Wood")


def _fill_books(c: DetailCanvas, x0: int, y0: int, z0: int, w: int, d: int) -> None:
    """A row of upright book spines of varied colour/height, resting on a shelf at y0."""
    for i, bx in enumerate(range(x0, x0 + w)):
        h = 3 + (i * 2 + 1) % 4                          # 3..6 micro tall, varied
        c.fill_micro_box(bx, y0, z0, 1, h, max(1, d - 1), BOOK_SPINES[i % len(BOOK_SPINES)])


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
            c.fill_micro_box(lx, 0, lz, 2, top_y, 2, frame)        # legs up to the underside of the top
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


def make_bookshelf(frame=OAK, back=LOG) -> DetailCanvas:
    """Tall floor-standing bookcase (~1.8 m): back panel, sides, top/bottom, three shelves, each
    compartment filled with varied book spines. Backs onto a wall."""
    c = DetailCanvas()
    W, H, D = 9, 16, 4                                   # 1 cube wide, ~1.78 m tall, 0.44 m deep
    c.fill_micro_box(0, 0, 0, W, H, 1, back)             # back panel (z=0)
    c.fill_micro_box(0, 0, 0, 1, H, D, frame)            # left side
    c.fill_micro_box(W - 1, 0, 0, 1, H, D, frame)        # right side
    c.fill_micro_box(0, 0, 0, W, 1, D, frame)            # bottom
    c.fill_micro_box(0, H - 1, 0, W, 1, D, frame)        # top
    shelf_ys = [4, 8, 12]
    for sy in shelf_ys:
        c.fill_micro_box(1, sy, 1, W - 2, 1, D - 1, frame)
    for ly in [1] + [sy + 1 for sy in shelf_ys]:         # books on the bottom + above each shelf
        _fill_books(c, 1, ly, 1, W - 2, D - 1)
    return c


def make_shelf(frame=OAK, back=LOG) -> DetailCanvas:
    """A low open floor shelf / console (~0.9 m): back, sides, one mid shelf, books on top + the
    shelf. Backs onto a wall. (A high WALL-MOUNTED shelf needs height placement — see gaps doc.)"""
    c = DetailCanvas()
    W, H, D = 9, 8, 4
    c.fill_micro_box(0, 0, 0, W, H, 1, back)             # back
    c.fill_micro_box(0, 0, 0, 1, H, D, frame)            # sides
    c.fill_micro_box(W - 1, 0, 0, 1, H, D, frame)
    c.fill_micro_box(0, 0, 0, W, 1, D, frame)            # bottom
    c.fill_micro_box(0, 4, 1, W - 2, 1, D - 1, frame)    # mid shelf
    c.fill_micro_box(0, H - 1, 0, W, 1, D, frame)        # top surface
    _fill_books(c, 1, 1, 1, W - 2, D - 1)                # books in the lower compartment
    _fill_books(c, 1, H, 1, 4, D - 1)                    # a few on top
    return c


def make_wardrobe(frame=LOG, panel=OAK) -> DetailCanvas:
    """Tall wardrobe (~2 m): carcass + two panelled doors with knobs. Backs onto a wall."""
    c = DetailCanvas()
    W, H, D = 9, 18, 5
    c.fill_micro_box(0, 0, 0, W, H, 1, panel)            # back
    c.fill_micro_box(0, 0, 0, 1, H, D, frame)            # sides
    c.fill_micro_box(W - 1, 0, 0, 1, H, D, frame)
    c.fill_micro_box(0, 0, 0, W, 1, D, frame)            # bottom
    c.fill_micro_box(0, H - 1, 0, W, 1, D, frame)        # cornice
    for dx in (1, 5):                                    # two doors, each 4 wide
        c.fill_micro_box(dx, 1, D - 1, 3, H - 2, 1, panel)
        c.fill_micro_box(dx, 1, D - 1, 3, 1, 1, frame)   # door rails/stiles
        c.fill_micro_box(dx, H - 2, D - 1, 3, 1, 1, frame)
        c.fill_micro_box(dx, 1, D - 1, 1, H - 2, 1, frame)
        c.fill_micro_box(dx + 2, 1, D - 1, 1, H - 2, 1, frame)
    c.set_micro_cell(3, H // 2, D - 1, KNOB)             # door knobs at the meeting stile
    c.set_micro_cell(5, H // 2, D - 1, KNOB)
    return c


def make_dresser(frame=LOG, panel=OAK, top=OAK) -> DetailCanvas:
    """Chest of drawers (~0.8 m) with three drawer fronts + knobs and a top surface. Backs to a wall."""
    c = DetailCanvas()
    W, H, D = 9, 7, 5
    c.fill_micro_box(0, 0, 0, W, H - 1, 1, panel)        # back
    c.fill_micro_box(0, 0, 0, 1, H - 1, D, frame)        # sides
    c.fill_micro_box(W - 1, 0, 0, 1, H - 1, D, frame)
    c.fill_micro_box(0, H - 1, 0, W, 1, D, top)          # top surface (clutter can sit here)
    for i, dy in enumerate((1, 3, 5)):                   # three drawer fronts
        c.fill_micro_box(1, dy - 1, D - 1, W - 2, 1, 1, frame)   # rail between drawers
        c.set_micro_cell(W // 2, dy, D - 1, KNOB)
    return c


def make_desk(top=OAK, frame=LOG) -> DetailCanvas:
    """Writing desk (~0.75 m top): a top slab, two legs one side, a drawer pedestal the other.
    Free-standing; its top is a surface for clutter/books."""
    c = DetailCanvas()
    L, H, D = 14, 7, 8
    top_y = H - 1
    # pedestal (drawers) on the left
    c.fill_micro_box(0, 0, 0, 4, top_y, D, frame)
    for dy in (1, 4):
        c.set_micro_cell(2, dy, D - 1, KNOB)
    # two legs on the right
    c.fill_micro_box(L - 2, 0, 0, 2, top_y, 2, frame)
    c.fill_micro_box(L - 2, 0, D - 2, 2, top_y, 2, frame)
    c.fill_micro_box(L - 2, top_y - 2, 0, 2, 1, D, frame)         # rail tying the legs
    c.fill_micro_box(0, top_y, 0, L, 1, D, top)                   # top slab
    _bevel_top(c, 0, top_y, 0, L, D, 1)
    return c


def make_fireplace(stone="Stone", frame=LOG, ember="glow") -> DetailCanvas:
    """Stone fireplace (~1.5 m): surround + firebox opening + a mantel shelf + glowing embers and
    a log. Backs onto a wall (the chimney breast)."""
    c = DetailCanvas()
    W, H, D = 13, 14, 5
    c.fill_micro_box(0, 0, 0, W, H, 1, stone)            # chimney-breast back
    c.fill_micro_box(0, 0, 0, 3, H - 3, D, stone)        # left jamb
    c.fill_micro_box(W - 3, 0, 0, 3, H - 3, D, stone)    # right jamb
    c.fill_micro_box(0, H - 5, 0, W, 2, D, stone)        # lintel over the opening
    c.fill_micro_box(0, H - 3, 0, W, 1, D + 1, stone)    # mantel shelf (oversails)
    # firebox floor + embers + a log
    c.fill_micro_box(3, 0, 1, W - 6, 1, D - 1, stone)
    c.fill_micro_box(4, 1, 2, W - 8, 1, 2, ember)        # glowing embers
    c.fill_micro_box(4, 2, 2, W - 8, 1, 1, frame)        # a log on the fire
    return c


def make_book_stack(frame=OAK) -> DetailCanvas:
    """A small stack of a few books lying flat — a tabletop/desk decoration."""
    c = DetailCanvas()
    W, D = 5, 7
    for i, by in enumerate(range(0, 4)):                 # 4 stacked books, slight offset
        off = i % 2
        c.fill_micro_box(off, by, 0, W - off, 1, D, BOOK_SPINES[i % len(BOOK_SPINES)])
    return c


# ----- surface clutter (small props that sit ON a table/desk/shelf) -----

def make_candlestick(metal=KNOB, candle=PILLOW, flame="glow") -> DetailCanvas:
    """A candlestick: base, stem, candle, glowing flame (~0.4 m)."""
    c = DetailCanvas()
    c.fill_micro_box(0, 0, 0, 3, 1, 3, metal)   # base
    c.set_micro_cell(1, 1, 1, metal)            # stem
    c.set_micro_cell(1, 2, 1, candle)           # candle
    c.set_micro_cell(1, 3, 1, flame)            # flame
    return c


def make_goblet(metal=KNOB) -> DetailCanvas:
    """A goblet: foot, stem, bowl (~0.3 m)."""
    c = DetailCanvas()
    c.fill_micro_box(0, 0, 0, 3, 1, 3, metal)   # foot
    c.set_micro_cell(1, 1, 1, metal)            # stem
    c.fill_micro_box(0, 2, 0, 3, 1, 3, metal)   # bowl
    return c


def make_bottle(glass="Glass", cork=OAK) -> DetailCanvas:
    """A bottle/jug: body, neck, stopper (~0.5 m)."""
    c = DetailCanvas()
    c.fill_micro_box(0, 0, 0, 2, 3, 2, glass)   # body
    c.set_micro_cell(0, 3, 0, glass)            # neck
    c.set_micro_cell(0, 4, 0, cork)             # cork
    return c


def make_plate(mat=LINEN) -> DetailCanvas:
    """A shallow plate/bowl with a rim (~0.3 m)."""
    c = DetailCanvas()
    c.fill_micro_box(0, 0, 0, 3, 1, 3, mat)
    for x in (0, 2):
        for z in range(3):
            c.set_micro_cell(x, 1, z, mat)
    for z in (0, 2):
        c.set_micro_cell(1, 1, z, mat)
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
    "bookshelf":  (make_bookshelf, "Bookshelf",
                   ["# facing:       +Z (front faces +Z; back panel at Z=0 against a wall)"]),
    "wall_shelf": (make_shelf, "Shelf",
                   ["# facing:       +Z (back panel at Z=0 against a wall)"]),
    "book_stack": (make_book_stack, "Book Stack", ["# facing:       +Z (tabletop decoration)"]),
    "wardrobe":   (make_wardrobe, "Wardrobe", ["# facing:       +Z (doors face +Z; back to a wall)"]),
    "dresser":    (make_dresser, "Dresser", ["# facing:       +Z (drawers face +Z; back to a wall)"]),
    "desk":       (make_desk, "Writing Desk", ["# facing:       +Z"]),
    "fireplace":  (make_fireplace, "Fireplace", ["# facing:       +Z (opening faces +Z; chimney to a wall)"]),
    "candlestick": (make_candlestick, "Candlestick", ["# clutter:      surface prop (sits on a table/desk/shelf)"]),
    "goblet":     (make_goblet, "Goblet", ["# clutter:      surface prop"]),
    "bottle":     (make_bottle, "Bottle", ["# clutter:      surface prop"]),
    "plate":      (make_plate, "Plate", ["# clutter:      surface prop"]),
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
        "# materials:    Wood, Log, Sandstone, Sand, Bricks, Gold, Metal, Leaf",
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
        from .geometry import template_cells, floating_components
        fl = floating_components(template_cells(n))
        flag = f"  !! {len(fl)} FLOATING cells" if fl else "  connected OK"
        print(f"[furniture] wrote {p}  ({PIECES[n][0]().report().summary()}){flag}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
