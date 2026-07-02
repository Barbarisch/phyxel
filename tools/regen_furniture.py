#!/usr/bin/env python3
"""Regenerate furniture .voxel templates whose dims drift from the grounded canon
(resources/object_dimensions.json), at the correct proportions, AND write the matching
.metrics.json from the SAME emitted voxel bounds (so the sidecar can't drift from the geometry).

Deterministic, no LLM. Microcube resolution: 1 cube = 9 micro = 1 m. A global micro index p maps to
(cube=p//9, sub=(p%9)//3, mic=p%3). Run from the repo root.

Targets (canon, metres):
  chest  -> coffer  1.2 w x 0.55 d x 0.7 h  (object_dimensions 'chest')
  hearth -> fireplace 1.5 w x 1.2 h x 0.6 d (object_dimensions 'hearth')
"""
import json
import os

TEMPLATES = "resources/templates"


def decomp(p):
    """global micro index -> (cube, sub, mic)"""
    return p // 9, (p % 9) // 3, p % 3


class Model:
    def __init__(self):
        self.cells = {}  # (px,py,pz) -> material  (micro grid)

    def m(self, px, py, pz, mat):
        self.cells[(px, py, pz)] = mat

    def box_shell(self, x0, x1, y0, y1, z0, z1, mat, faces=("x", "z", "ymin", "ymax")):
        for x in range(x0, x1 + 1):
            for y in range(y0, y1 + 1):
                for z in range(z0, z1 + 1):
                    on = False
                    if "x" in faces and (x == x0 or x == x1):
                        on = True
                    if "z" in faces and (z == z0 or z == z1):
                        on = True
                    if "ymin" in faces and y == y0:
                        on = True
                    if "ymax" in faces and y == y1:
                        on = True
                    if on:
                        self.cells[(x, y, z)] = mat

    def fill(self, x0, x1, y0, y1, z0, z1, mat):
        for x in range(x0, x1 + 1):
            for y in range(y0, y1 + 1):
                for z in range(z0, z1 + 1):
                    self.cells[(x, y, z)] = mat

    def clear(self, x0, x1, y0, y1, z0, z1):
        for x in range(x0, x1 + 1):
            for y in range(y0, y1 + 1):
                for z in range(z0, z1 + 1):
                    self.cells.pop((x, y, z), None)

    def bounds(self):
        xs = [c[0] for c in self.cells]
        ys = [c[1] for c in self.cells]
        zs = [c[2] for c in self.cells]
        return (min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs))

    def mirror_z(self):
        """Flip the model along Z so its 'front' (feature) face lands on the HIGH-z side. The placer
        convention is +Z front (FurniturePlacer: rot 0 -> front +z): a wall-backed piece at rot 0 on
        a min-z wall has its local z=HIGH face pointing INTO the room. Assets authored feature-on-z=0
        therefore face the WALL (the bug). mirror_z makes feature -> z-high so they face the room."""
        if not self.cells:
            return
        maxz = max(c[2] for c in self.cells)
        self.cells = {(x, y, maxz - z): mat for (x, y, z), mat in self.cells.items()}

    def emit_lines(self, order):
        """order: list of (predicate, comment) -> grouped output for parts."""
        out = []
        used = set()
        for pred, comment in order:
            if comment:
                out.append(comment)
            for (px, py, pz), mat in sorted(self.cells.items()):
                if (px, py, pz) in used or not pred(px, py, pz):
                    continue
                cx, sx, mx = decomp(px)
                cy, sy, my = decomp(py)
                cz, sz, mz = decomp(pz)
                out.append(f"M {cx} {cy} {cz} {sx} {sy} {sz} {mx} {my} {mz} {mat}")
                used.add((px, py, pz))
        # any ungrouped cells
        for (px, py, pz), mat in sorted(self.cells.items()):
            if (px, py, pz) in used:
                continue
            cx, sx, mx = decomp(px)
            cy, sy, my = decomp(py)
            cz, sz, mz = decomp(pz)
            out.append(f"M {cx} {cy} {cz} {sx} {sy} {sz} {mx} {my} {mz} {mat}")
        return out


def write(name, header, body_lines, model, anchors=None):
    (mnx, mny, mnz), (mxx, mxy, mxz) = model.bounds()
    # overall extent in metres: a micro at index p occupies [p/9, (p+1)/9]
    omin = [mnx / 9.0, mny / 9.0, mnz / 9.0]
    omax = [(mxx + 1) / 9.0, (mxy + 1) / 9.0, (mxz + 1) / 9.0]
    vox_path = os.path.join(TEMPLATES, name + ".voxel")
    with open(vox_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(header.rstrip() + "\n\n")
        f.write("\n".join(body_lines) + "\n")
    met_path = os.path.join(TEMPLATES, name + ".metrics.json")
    with open(met_path, "w", encoding="utf-8", newline="\n") as f:
        json.dump({
            "schema_version": "asset_metrics.v1",
            "template_name": name,
            "overall_min": omin,
            "overall_max": omax,
            "interaction_points": anchors or [],
        }, f, indent=2)
    w, h, d = omax[0] - omin[0], omax[1] - omin[1], omax[2] - omin[2]
    print(f"{name}: {len(model.cells)} micro, bounds {w:.3f}w x {h:.3f}h x {d:.3f}d m -> {vox_path}")


def seat_anchor(cx_micro, top_micro, cz_micro, depth_micro=4):
    """A 'seat' interaction point at the seat-slab top centre (micro indices -> metres)."""
    cx, cy, cz = (cx_micro + 0.5) / 9.0, top_micro / 9.0, (cz_micro + 0.5) / 9.0
    return [{
        "point_id": "seat_0", "kind": "seat",
        "local_position": [round(cx, 3), round(cy, 3), round(cz, 3)],
        "facing_yaw": 0.0,
        "features": {
            "seat_top_y": round(cy, 3),
            "seat_center": [round(cx, 3), round(cy, 3), round(cz, 3)],
            "backrest_present": False,
        },
    }]


def gen_chest():
    # coffer: 11 micro wide (1.22), 6 tall (0.67), 5 deep (0.56). facing +Z (clasp front).
    W, H, D = 11, 6, 5
    body_top = 3   # body occupies y 0..3, lid y 4..5
    m = Model()
    # body shell (hollow): walls + bottom, open top (lid sits over it)
    m.box_shell(0, W - 1, 0, body_top, 0, D - 1, "WoodWalnut", faces=("x", "z", "ymin"))
    # iron straps on the front face (z=0)
    for sx in (2, W - 3):
        for y in range(0, body_top + 1):
            m.m(sx, y, 0, "Metal")
    # lid: full top cap y 4..5 + a front clasp
    lid = Model()
    for x in range(0, W):
        for z in range(0, D):
            for y in range(body_top + 1, H):
                lid.m(x, y, z, "WoodWalnut")
    lid.m(W // 2, body_top + 1, 0, "Metal")  # clasp, front centre
    for k, v in lid.cells.items():
        m.cells[k] = v
    header = (
        "# ==========================================================\n"
        "# ASSET METADATA\n"
        "# name:         chest_closed\n"
        "# display_name: Chest (Closed)\n"
        "# description:  A wide wooden storage coffer with iron straps, lid closed.\n"
        "# category:     furniture\n"
        "# subcategory:  container\n"
        "# tags:         chest, loot, storage, house, treasure, common\n"
        "# materials:    Wood, Metal\n"
        "# facing:       +Z (clasp faces viewer)\n"
        "# bounds:       1.22W x 0.67H x 0.56D m (grounded to object_dimensions 'chest' 1.2x0.55x0.7)\n"
        "# method:       tools/regen_furniture.py (deterministic, canon-proportioned)\n"
        "# ==========================================================\n"
        "\n# Front = +Z."
    )
    is_lid = lambda px, py, pz: py > body_top
    order = [
        (lambda px, py, pz: py <= body_top, "# part: base"),
        (is_lid, "# part: lid hinge=back_top axis=x"),
    ]
    write("chest_closed", header, m.emit_lines(order), m)


def gen_fireplace():
    # hearth: 14 wide (1.56), 11 tall (1.22), 5 deep (0.56). facing +Z (opening front).
    W, H, D = 14, 11, 5
    m = Model()
    # hearth base slab (brick masonry — a fireplace/chimney is brick, not quarried stone)
    m.fill(0, W - 1, 0, 1, 0, D - 1, "Bricks")
    # back wall / chimney breast at z=0 (the side that backs ONTO the room wall)
    m.fill(0, W - 1, 0, H - 1, 0, 0, "Bricks")
    # side pillars
    m.fill(0, 2, 2, H - 1, 0, D - 1, "Bricks")
    m.fill(W - 3, W - 1, 2, H - 1, 0, D - 1, "Bricks")
    # lintel / mantel across the top
    m.fill(0, W - 1, H - 3, H - 1, 0, D - 1, "Bricks")
    # carve the firebox opening toward +Z (the OPENING faces the room; chimney breast to the wall).
    # Keep z=0 solid (the back); open z=1..D-1 so the firebox faces +Z (matches the header + the placer
    # convention rot 0 -> front +z, so a wall-backed fireplace opens INTO the room, not at the wall).
    m.clear(3, W - 4, 2, H - 4, 1, D - 1)
    # fire: a log bed + glowing embers on the hearth floor inside the firebox (near the back, z=1)
    m.fill(4, W - 5, 2, 2, 1, D - 2, "Log")
    m.fill(5, W - 6, 2, 2, 1, 1, "glow")
    header = (
        "# ==========================================================\n"
        "# ASSET METADATA\n"
        "# name:         fireplace\n"
        "# display_name: Fireplace\n"
        "# description:  A brick hearth with a fire opening and chimney breast.\n"
        "# category:     furniture\n"
        "# materials:    Bricks, Log, glow\n"
        "# facing:       +Z (opening faces +Z; chimney breast to the wall)\n"
        "# bounds:       1.56W x 1.22H x 0.56D m (grounded to object_dimensions 'hearth' 1.5x1.2x0.6)\n"
        "# method:       tools/regen_furniture.py (deterministic, canon-proportioned)\n"
        "# =========================================================="
    )
    write("fireplace", header, m.emit_lines([]), m)


def gen_bar():
    # tavern_bar (front counter): top ~1.11 m (42" bar, grounded barstoolcomforts/Mor Furniture; nearest
    # micro to the 40-42" band), depth 0.67 m (24-26" bar w/ overhang), length 3.0 m. facing +Z (patron).
    L = 27                          # length (x), 3.0 m — variable; length not conformance-checked
    BODY_TOP = 8                    # cabinet y 0..8 (top face 1.0 m); cap at y9 -> top 1.111 m
    m = Model()
    # front panel (patron side, z=0..1) + end panels + a back kick, leaving the cabinet hollow.
    m.fill(0, L - 1, 0, BODY_TOP, 1, 1, "WoodWalnut")            # front face (set back 1 micro for a toe kick)
    m.fill(0, 0, 0, BODY_TOP, 1, 5, "WoodWalnut")                # left end
    m.fill(L - 1, L - 1, 0, BODY_TOP, 1, 5, "WoodWalnut")        # right end
    m.fill(0, L - 1, 0, 0, 5, 5, "WoodWalnut")                   # back kick rail (bartender side)
    m.fill(0, L - 1, BODY_TOP + 1, BODY_TOP + 1, 0, 5, "WoodWalnut")  # top cap (overhangs to z=0 for legroom)
    m.fill(0, L - 1, BODY_TOP, BODY_TOP, 0, 0, "Log")      # dark front rail under the cap edge
    header = (
        "# ==========================================================\n"
        "# ASSET METADATA\n"
        "# name:         tavern_bar\n"
        "# display_name: Tavern Bar (Counter)\n"
        "# description:  A long wooden serving counter — toe-kick front, overhanging top, dark front rail.\n"
        "# category:     furniture\n"
        "# subcategory:  counter\n"
        "# tags:         bar, counter, tavern, inn, serving\n"
        "# materials:    Wood, Log\n"
        "# facing:       +Z (patron side; bartender works behind, +z)\n"
        "# bounds:       3.0W x 1.11H x 0.67D m (grounded object_dimensions 'tavern_bar' 42\" counter)\n"
        "# method:       tools/regen_furniture.py (deterministic, canon-proportioned)\n"
        "# =========================================================="
    )
    m.mirror_z()   # front (patron side) -> +Z so a wall-backed bar faces the room (placer +Z convention)
    write("tavern_bar", header, m.emit_lines([]), m)


def gen_back_bar():
    # back_bar (shelving behind the counter, against the wall): ~1.78 m tall, 0.33 m deep, 3.0 m long.
    # Three shelves of bottles (Glass) — the "shelves behind and above the bar".
    L, H, D = 27, 16, 3
    m = Model()
    m.fill(0, L - 1, 0, H - 1, D - 1, D - 1, "WoodWalnut")       # back panel (against the wall)
    m.fill(0, 0, 0, H - 1, 0, D - 1, "WoodWalnut")               # left end
    m.fill(L - 1, L - 1, 0, H - 1, 0, D - 1, "WoodWalnut")       # right end
    m.fill(0, L - 1, 0, 0, 0, D - 1, "WoodWalnut")               # base
    shelves = (4, 9, 14)
    for sy in shelves:
        m.fill(0, L - 1, sy, sy, 0, D - 1, "WoodWalnut")         # shelf board
    # bottles: pairs of Glass micro standing on each shelf (1 above the board), spaced along the run.
    for sy in shelves:
        for bx in range(2, L - 2, 3):
            m.m(bx, sy + 1, 1, "Glass")
            m.m(bx, sy + 2, 1, "Glass")                    # ~0.22 m bottles
    header = (
        "# ==========================================================\n"
        "# ASSET METADATA\n"
        "# name:         back_bar\n"
        "# display_name: Back Bar (Shelving)\n"
        "# description:  Wall shelving behind a tavern bar — three shelves lined with glass bottles.\n"
        "# category:     furniture\n"
        "# subcategory:  shelving\n"
        "# tags:         bar, shelf, bottles, tavern, inn\n"
        "# materials:    Wood, Glass\n"
        "# facing:       +Z (open shelf face toward the bartender / room)\n"
        "# bounds:       3.0W x 1.78H x 0.33D m (grounded object_dimensions 'back_bar')\n"
        "# method:       tools/regen_furniture.py (deterministic, canon-proportioned)\n"
        "# =========================================================="
    )
    m.mirror_z()   # open shelf face -> +Z (back panel to the wall) so it faces the room
    write("back_bar", header, m.emit_lines([]), m)


def gen_bar_stool():
    # bar_stool: seat top ~0.78 m (30" bar stool, grounded; 10-12\" below a 42\" counter -> ~13\" gap at
    # the micro grid), seat 0.44 m, four legs + a footrest ring. Backless. facing +Z.
    W = 4                           # seat 4x4 micro (0.444 m, within 12-18\" seat)
    SEAT_Y = 6                      # seat slab at y6 -> top face 7/9 = 0.778 m
    m = Model()
    m.fill(0, W - 1, SEAT_Y, SEAT_Y, 0, W - 1, "WoodWalnut")     # seat slab
    legs = [(0, 0), (W - 1, 0), (0, W - 1), (W - 1, W - 1)]
    for lx, lz in legs:
        m.fill(lx, lx, 0, SEAT_Y - 1, lz, lz, "WoodWalnut")      # leg
    # footrest ring at y2 connecting the legs (a square rail)
    ry = 2
    m.fill(0, W - 1, ry, ry, 0, 0, "Log")
    m.fill(0, W - 1, ry, ry, W - 1, W - 1, "Log")
    m.fill(0, 0, ry, ry, 0, W - 1, "Log")
    m.fill(W - 1, W - 1, ry, ry, 0, W - 1, "Log")
    # seat centre in micro-index space: seat spans 0..W-1 -> centre (W-1)/2; anchor at the slab top.
    anchors = seat_anchor((W - 1) / 2.0, SEAT_Y + 1, (W - 1) / 2.0)
    header = (
        "# ==========================================================\n"
        "# ASSET METADATA\n"
        "# name:         bar_stool\n"
        "# display_name: Bar Stool\n"
        "# description:  A tall backless wooden bar stool with four legs and a footrest ring.\n"
        "# category:     furniture\n"
        "# subcategory:  seating\n"
        "# tags:         stool, bar, seat, tavern, inn\n"
        "# materials:    Wood, Log\n"
        "# facing:       +Z\n"
        "# seat_height:  0.778\n"
        "# bounds:       0.44W x 0.78H x 0.44D m (grounded object_dimensions 'bar_stool' 30\" bar stool)\n"
        "# method:       tools/regen_furniture.py (deterministic, canon-proportioned)\n"
        "# =========================================================="
    )
    write("bar_stool", header, m.emit_lines([]), m, anchors)


def gen_candle_stand():
    # candle_stand: a floor candelabra ~1.3 m tall (floor-standing -> the placer can place it against a
    # wall). Metal base + pole, a top tray of candles (Wood) each with a glow flame. Emissive 'glow'.
    m = Model()
    m.fill(0, 2, 0, 0, 0, 2, "Metal")              # splayed base (3x3 foot)
    m.fill(1, 1, 1, 8, 1, 1, "Metal")              # central pole
    m.fill(0, 2, 9, 9, 0, 2, "Metal")              # top tray
    for cx, cz in [(0, 0), (2, 0), (0, 2), (2, 2), (1, 1)]:
        m.m(cx, 10, cz, "WoodWalnut")                    # candle
        m.m(cx, 11, cz, "glow")                    # flame (emissive)
    header = (
        "# ==========================================================\n"
        "# ASSET METADATA\n"
        "# name:         candle_stand\n"
        "# display_name: Candle Stand (Candelabra)\n"
        "# description:  A tall floor candelabra — metal pole + tray of candles with glowing flames.\n"
        "# category:     furniture\n"
        "# subcategory:  lighting\n"
        "# tags:         light, candle, candelabra, tavern, hall, floor\n"
        "# materials:    Metal, Wood, glow\n"
        "# facing:       +Z\n"
        "# bounds:       0.33W x 1.33H x 0.33D m (grounded object_dimensions 'candle_stand')\n"
        "# method:       tools/regen_furniture.py (deterministic, canon-proportioned)\n"
        "# =========================================================="
    )
    write("candle_stand", header, m.emit_lines([]), m)


def gen_wall_lantern():
    # wall_lantern: a wall sconce lantern (Metal frame + Glass panes + a glow core). The MOUNT height
    # (~1.68 m) is a placement concern; the asset is just the lantern body (~0.44 m tall).
    m = Model()
    m.fill(0, 2, 0, 0, 0, 1, "Metal")              # bottom plate
    m.fill(0, 2, 3, 3, 0, 1, "Metal")              # top cap (roof)
    for cx in (0, 2):                               # corner posts
        for cz in (0, 1):
            m.fill(cx, cx, 1, 2, cz, cz, "Metal")
    m.fill(0, 2, 1, 2, 0, 0, "Glass")              # front glass pane
    m.fill(1, 1, 1, 2, 1, 1, "glow")               # glowing core (the flame)
    header = (
        "# ==========================================================\n"
        "# ASSET METADATA\n"
        "# name:         wall_lantern\n"
        "# display_name: Wall Lantern (Sconce)\n"
        "# description:  A wall-mounted lantern — metal frame, glass panes, glowing core.\n"
        "# category:     furniture\n"
        "# subcategory:  lighting\n"
        "# tags:         light, lantern, sconce, wall, tavern, inn\n"
        "# materials:    Metal, Glass, glow\n"
        "# facing:       +Z (glass face to the room; mounts on a wall ~1.68 m up)\n"
        "# bounds:       0.33W x 0.44H x 0.22D m (grounded object_dimensions 'wall_lantern')\n"
        "# method:       tools/regen_furniture.py (deterministic, canon-proportioned)\n"
        "# =========================================================="
    )
    write("wall_lantern", header, m.emit_lines([]), m)


def gen_chandelier():
    # chandelier: a hanging ring of candles (~0.56 m diameter) with a chain up to the ceiling. Metal
    # ring + Wood candles + glow flames. Hangs ~2.1 m above the floor (a placement concern).
    R = 5                                           # 5x5 ring -> ~0.56 m diameter
    m = Model()
    m.box_shell(0, R - 1, 0, 0, 0, R - 1, "Metal", faces=("x", "z"))  # ring rim at y0
    for (cx, cz) in [(0, 2), (2, 0), (4, 2), (2, 4), (0, 0), (4, 4), (0, 4), (4, 0)]:
        m.m(cx, 1, cz, "WoodWalnut")                      # candle on the rim
        m.m(cx, 2, cz, "glow")                      # flame
    m.fill(2, 2, 1, 5, 2, 2, "Metal")              # central chain up to the ceiling
    header = (
        "# ==========================================================\n"
        "# ASSET METADATA\n"
        "# name:         chandelier\n"
        "# display_name: Chandelier (Candle Ring)\n"
        "# description:  A hanging iron candle-ring chandelier with glowing flames and a chain.\n"
        "# category:     furniture\n"
        "# subcategory:  lighting\n"
        "# tags:         light, chandelier, hanging, tavern, hall, feast\n"
        "# materials:    Metal, Wood, glow\n"
        "# facing:       +Z (hangs from the ceiling; chain up)\n"
        "# bounds:       0.56 diameter x 0.67H m (grounded object_dimensions 'chandelier')\n"
        "# method:       tools/regen_furniture.py (deterministic, canon-proportioned)\n"
        "# =========================================================="
    )
    write("chandelier", header, m.emit_lines([]), m)


def gen_mug():
    # mug / tankard: a pint pewter/wood tankard ~0.125 m tall, ~0.095 m dia. At the 1/9 m micro floor
    # that is ~1 micro -> a single small block (table clutter; reads as a cup at player scale).
    m = Model()
    m.m(0, 0, 0, "WoodWalnut")                            # wooden tankard (treen) — 0.111 m cube
    header = (
        "# ==========================================================\n"
        "# ASSET METADATA\n"
        "# name:         mug\n"
        "# display_name: Mug / Tankard\n"
        "# description:  A wooden drinking tankard — table clutter (microcube-floor sized).\n"
        "# category:     furniture\n"
        "# subcategory:  tableware\n"
        "# tags:         mug, tankard, cup, drink, tavern, clutter\n"
        "# materials:    Wood\n"
        "# facing:       +Z\n"
        "# bounds:       0.11W x 0.11H x 0.11D m (grounded object_dimensions 'mug' ~pint tankard)\n"
        "# method:       tools/regen_furniture.py (deterministic, canon-proportioned)\n"
        "# =========================================================="
    )
    write("mug", header, m.emit_lines([]), m)


def gen_bottle():
    # bottle: a standard 750 ml wine/ale bottle ~0.30 m tall, ~0.075 m dia. Micro: 1 wide x 3 tall
    # (Glass body + a Wood cork/stopper on top). Table / back-bar clutter.
    m = Model()
    m.m(0, 0, 0, "Glass")
    m.m(0, 1, 0, "Glass")
    m.m(0, 2, 0, "WoodWalnut")                            # cork / stopper
    header = (
        "# ==========================================================\n"
        "# ASSET METADATA\n"
        "# name:         bottle\n"
        "# display_name: Bottle\n"
        "# description:  A glass bottle with a cork — table / back-bar clutter.\n"
        "# category:     furniture\n"
        "# subcategory:  tableware\n"
        "# tags:         bottle, glass, drink, tavern, clutter\n"
        "# materials:    Glass, Wood\n"
        "# facing:       +Z\n"
        "# bounds:       0.11W x 0.33H x 0.11D m (grounded object_dimensions 'bottle' 750 ml = 30 cm)\n"
        "# method:       tools/regen_furniture.py (deterministic, canon-proportioned)\n"
        "# =========================================================="
    )
    write("bottle", header, m.emit_lines([]), m)


def _table(name, L, D, display, tags, desc):
    # a slab-top table: top surface at 7 micro (0.778 m ~ 0.75 m dining height), 4 corner legs.
    m = Model()
    m.fill(0, L - 1, 6, 6, 0, D - 1, "WoodWalnut")        # top slab (y6 -> top face 0.778 m)
    for lx in (1, L - 2):
        for lz in (1, D - 2):
            m.fill(lx, lx, 0, 5, lz, lz, "WoodWalnut")    # leg
    header = (
        "# ==========================================================\n"
        "# ASSET METADATA\n"
        f"# name:         {name}\n"
        f"# display_name: {display}\n"
        f"# description:  {desc}\n"
        "# category:     furniture\n"
        "# subcategory:  table\n"
        f"# tags:         {tags}\n"
        "# materials:    Wood\n"
        "# facing:       +Z\n"
        f"# bounds:       {L/9:.2f}W x 0.78H x {D/9:.2f}D m (grounded object_dimensions 'table_dining' 0.75 top / 0.84 deep)\n"
        "# method:       tools/regen_furniture.py (deterministic, canon-proportioned)\n"
        "# =========================================================="
    )
    write(name, header, m.emit_lines([]), m, [{"point_id": "surface", "kind": "surface",
        "local_position": [round((L/2)/9, 3), round(7/9, 3), round((D/2)/9, 3)], "facing_yaw": 0.0,
        "features": {"surface_y": round(7/9, 3)}}])


def gen_table_wood():  # 4-person dining table: ~1.44 x 0.78 x 0.89 m
    _table("table_wood", 13, 8, "Wooden Table", "table, dining, house, tavern, common",
           "A four-person wooden dining table with corner legs.")


def gen_tavern_table():  # long communal table: ~1.89 x 0.78 x 0.78 m
    _table("tavern_table", 17, 7, "Tavern Table (Long)", "table, tavern, communal, feast",
           "A long communal tavern table for shared seating.")


def gen_counter():
    # kitchen work surface: top ~0.89 m (0.9 m canon), depth 0.56 m (~0.6), length ~1.56 m.
    L = 14
    m = Model()
    m.fill(0, L - 1, 0, 6, 1, 1, "WoodWalnut")            # front panel (toe kick at z0)
    m.fill(0, 0, 0, 6, 1, 4, "WoodWalnut")                # left end
    m.fill(L - 1, L - 1, 0, 6, 1, 4, "WoodWalnut")        # right end
    m.fill(0, L - 1, 7, 7, 0, 4, "WoodWalnut")            # work top (overhang to z0)
    header = (
        "# ==========================================================\n"
        "# ASSET METADATA\n"
        "# name:         counter\n"
        "# display_name: Kitchen Counter\n"
        "# description:  A kitchen work surface / dresser board with a plank front and overhanging top.\n"
        "# category:     furniture\n"
        "# subcategory:  counter\n"
        "# tags:         counter, kitchen, work, dresser\n"
        "# materials:    Wood\n"
        "# facing:       +Z\n"
        "# bounds:       1.56W x 0.89H x 0.56D m (grounded object_dimensions 'counter_kitchen' 0.9 top / 0.6 deep)\n"
        "# method:       tools/regen_furniture.py (deterministic, canon-proportioned)\n"
        "# =========================================================="
    )
    m.mirror_z()   # work-front (toe-kick + overhang) -> +Z so a wall-backed counter faces the room
    write("counter", header, m.emit_lines([]), m)


def gen_barrel():
    # storage cask: ~0.89 m tall, ~0.56 m dia (octagon-ish staves + iron hoops). Wood + Metal.
    H, R = 8, 5
    corners = {(0, 0), (0, R - 1), (R - 1, 0), (R - 1, R - 1)}
    m = Model()
    for y in range(H):
        for x in range(R):
            for z in range(R):
                if (x, z) in corners:
                    continue                         # bevel the corners -> rounder barrel
                m.cells[(x, y, z)] = "WoodWalnut"
    for hoop in (1, H - 2):                          # iron hoops near top + bottom
        for x in range(R):
            for z in range(R):
                if (x, z) in corners:
                    continue
                if x in (0, R - 1) or z in (0, R - 1):
                    m.cells[(x, hoop, z)] = "Metal"
    header = (
        "# ==========================================================\n"
        "# ASSET METADATA\n"
        "# name:         barrel\n"
        "# display_name: Barrel (Cask)\n"
        "# description:  A wooden storage cask with iron hoops — ale/wine/stores.\n"
        "# category:     furniture\n"
        "# subcategory:  storage\n"
        "# tags:         barrel, cask, storage, tavern, cellar, common\n"
        "# materials:    Wood, Metal\n"
        "# facing:       +Z\n"
        "# bounds:       0.56W x 0.89H x 0.56D m (grounded object_dimensions 'barrel' ~31 gal cask)\n"
        "# method:       tools/regen_furniture.py (deterministic, canon-proportioned)\n"
        "# =========================================================="
    )
    write("barrel", header, m.emit_lines([]), m)


def gen_bench_wood():
    # backless bench / settle: seat top ~0.44 m (0.45 canon), depth ~0.44 m, length ~1.44 m.
    L, D, SEAT_Y = 13, 4, 3
    m = Model()
    m.fill(0, L - 1, SEAT_Y, SEAT_Y, 0, D - 1, "WoodWalnut")   # seat slab (top face 0.444 m)
    for lx in (0, L - 1):
        for lz in (0, D - 1):
            m.fill(lx, lx, 0, SEAT_Y - 1, lz, lz, "WoodWalnut")
    header = (
        "# ==========================================================\n"
        "# ASSET METADATA\n"
        "# name:         bench_wood\n"
        "# display_name: Bench\n"
        "# description:  A long backless wooden bench / settle seat.\n"
        "# category:     furniture\n"
        "# subcategory:  seating\n"
        "# tags:         bench, settle, seat, tavern, hall, common\n"
        "# materials:    Wood\n"
        "# facing:       +Z\n"
        "# bounds:       1.44W x 0.44H x 0.44D m (grounded object_dimensions 'bench' seat 0.45)\n"
        "# method:       tools/regen_furniture.py (deterministic, canon-proportioned)\n"
        "# =========================================================="
    )
    write("bench_wood", header, m.emit_lines([]), m, seat_anchor((L - 1) / 2.0, SEAT_Y + 1, (D - 1) / 2.0))


def gen_forge_hearth():
    # forge_hearth: a Stone hearth on the back wall, work_top ~0.8 m (anvil/working height), with a
    # firepot recess of glowing coals + a tuyere and a chimney/hood rising up the back. facing +Z
    # (the smith works the open front; the chimney breast is to the wall, +z). Canon 'forge_hearth'
    # width 1.0 / depth 0.8 (work_top 0.8). The chimney rises in Y only (footprint unchanged).
    W, D = 9, 7                       # 1.0 m wide x 0.78 m deep
    TOP = 6                           # body y0..6 -> top face 7/9 = 0.778 m (~0.8)
    m = Model()
    m.fill(0, W - 1, 0, TOP, 0, D - 1, "Stone")          # solid hearth block
    # firepot recess in the top, centred toward the front; coals + a back tuyere (air inlet)
    m.clear(3, W - 4, TOP - 1, TOP, 2, 4)
    m.fill(3, W - 4, TOP - 1, TOP - 1, 2, 4, "glow")     # glowing coals in the firepot
    m.m(W // 2, TOP - 1, 4, "Metal")                      # tuyere (bellows air inlet, at the back)
    # chimney: a hood lip then a flue rising up the BACK (z = D-1), kept within the WxD footprint
    m.fill(1, W - 2, TOP + 1, TOP + 2, 4, D - 1, "Stone") # hood lip over the fire, to the back
    m.fill(3, W - 4, TOP + 3, 17, D - 2, D - 1, "Stone")  # flue up the back wall (vent)
    header = (
        "# ==========================================================\n"
        "# ASSET METADATA\n"
        "# name:         forge_hearth\n"
        "# display_name: Forge Hearth\n"
        "# description:  A stone blacksmith's forge -- firepot of glowing coals, tuyere, and a back chimney/hood.\n"
        "# category:     furniture\n"
        "# subcategory:  fixture\n"
        "# tags:         forge, hearth, smithy, blacksmith, fire, vented\n"
        "# materials:    Stone, Metal, glow\n"
        "# facing:       +Z (open working front to the room; chimney breast + flue to the wall, +z)\n"
        "# bounds:       1.0W x ~2.0H x 0.78D m (grounded object_dimensions 'forge_hearth' work_top 0.8 / width 1.0 / depth 0.8)\n"
        "# method:       tools/regen_furniture.py (deterministic, canon-proportioned)\n"
        "# =========================================================="
    )
    m.mirror_z()   # open working front -> +Z (chimney/flue to the wall) so the forge faces the room
    write("forge_hearth", header, m.emit_lines([]), m)


def gen_anvil():
    # anvil on a stump: face at work_top ~0.8 m, overall length ~0.55 m (horn-heel), base ~0.22 m.
    # Long axis = Z (length); X = the narrow base width (canon width 0.25, tol 0.06). facing +Z.
    LZ = 5                            # length 5 micro = 0.556 m (canon length 0.55)
    BX = 2                            # base width 2 micro = 0.222 m (canon width 0.25 +/-0.06)
    m = Model()
    m.fill(0, BX - 1, 0, 4, 1, 3, "Log")                  # the stump (under the anvil waist)
    m.fill(0, BX - 1, 5, 6, 0, LZ - 1, "Metal")           # the anvil body (face top y6 -> 0.778 m)
    m.clear(0, BX - 1, 6, 6, 0, 0)                         # round off the horn tip (front, z0)
    anchors = [{
        "point_id": "work_0", "kind": "work_surface",
        "local_position": [round((BX / 2.0) / 9, 3), round(7 / 9, 3), round((LZ / 2.0) / 9, 3)],
        "facing_yaw": 0.0, "features": {"work_top_y": round(7 / 9, 3)},
    }]
    header = (
        "# ==========================================================\n"
        "# ASSET METADATA\n"
        "# name:         anvil\n"
        "# display_name: Anvil\n"
        "# description:  A blacksmith's anvil on a wooden stump -- face at working (knuckle) height.\n"
        "# category:     furniture\n"
        "# subcategory:  fixture\n"
        "# tags:         anvil, smithy, blacksmith, forge, iron\n"
        "# materials:    Metal, Log\n"
        "# facing:       +Z (horn to the front)\n"
        "# work_top:     0.778\n"
        "# bounds:       0.22W x 0.78H x 0.56D m (grounded object_dimensions 'anvil' length 0.55 / width 0.25 / work_top 0.8)\n"
        "# method:       tools/regen_furniture.py (deterministic, canon-proportioned)\n"
        "# =========================================================="
    )
    write("anvil", header, m.emit_lines([]), m, anchors)


def gen_bellows():
    # great double-lung bellows: ~1.5 m long, ~0.33 m wide, ~0.33 m tall. Long axis = Z (length).
    # Wide chamber at the back (z max), tapering to a Metal nozzle at the front (z0, toward the forge).
    m = Model()
    m.fill(0, 2, 0, 2, 8, 13, "WoodWalnut")     # back chamber (full 3x3 cross-section), z8..13
    m.fill(1, 1, 0, 2, 4, 7, "WoodWalnut")      # narrowing middle, z4..7
    m.fill(1, 1, 1, 1, 0, 3, "Metal")     # nozzle pipe to the front (z0..3)
    header = (
        "# ==========================================================\n"
        "# ASSET METADATA\n"
        "# name:         bellows\n"
        "# display_name: Bellows\n"
        "# description:  A great double-lung blacksmith's bellows -- wide chamber tapering to an iron nozzle.\n"
        "# category:     furniture\n"
        "# subcategory:  fixture\n"
        "# tags:         bellows, smithy, blacksmith, forge, air\n"
        "# materials:    Wood, Metal\n"
        "# facing:       +Z (nozzle to the front / the forge)\n"
        "# bounds:       0.33W x 0.33H x 1.56D m (grounded object_dimensions 'bellows' length 1.5 / width 0.3 / height 0.35)\n"
        "# method:       tools/regen_furniture.py (deterministic, canon-proportioned)\n"
        "# =========================================================="
    )
    write("bellows", header, m.emit_lines([]), m)


def gen_tool_rack():
    # tool_rack: a wall-mounted board (~1.0 m wide, thin) hung with hammers + tongs. The MOUNT height
    # (~1.5 m) is a placement concern; the asset is the board + tools. facing +Z (tools face the room).
    W = 9                             # 1.0 m wide (canon width 1.0)
    m = Model()
    m.fill(0, W - 1, 0, 5, 0, 0, "WoodWalnut")          # back board (against the wall, z0)
    m.fill(0, W - 1, 5, 5, 0, 1, "WoodWalnut")          # top peg rail (a little depth, z1)
    for tx in (1, 3, 5, 7):                        # hanging tools (hammers / tongs)
        m.m(tx, 4, 1, "Metal")
        m.m(tx, 3, 1, "Metal")
        m.m(tx, 2, 1, "Metal")
    header = (
        "# ==========================================================\n"
        "# ASSET METADATA\n"
        "# name:         tool_rack\n"
        "# display_name: Tool Rack\n"
        "# description:  A wall-mounted smith's tool rack -- a board hung with hammers and tongs.\n"
        "# category:     furniture\n"
        "# subcategory:  fixture\n"
        "# tags:         tool, rack, smithy, blacksmith, wall, hammers, tongs\n"
        "# materials:    Wood, Metal\n"
        "# facing:       +Z (tools face the room; mounts on a wall ~1.5 m up)\n"
        "# bounds:       1.0W x ~0.67H x 0.22D m (grounded object_dimensions 'tool_rack' width 1.0 / depth 0.15 / mount 1.5)\n"
        "# method:       tools/regen_furniture.py (deterministic, canon-proportioned)\n"
        "# =========================================================="
    )
    write("tool_rack", header, m.emit_lines([]), m)


def gen_oven_bread():
    # bread oven: a Stone masonry dome oven -- hearth floor at work_top ~0.8 m, a ~0.9 m baking chamber
    # in thick heat-retaining masonry, an arched mouth opening to the room + a flue up the back (VENTED
    # like the forge; gets a place_chimney stack through the roof). facing +Z (mouth to the room; the
    # flue / chimney breast backs onto the wall). Grounded object_dimensions 'oven_bread'
    # (interior 0.9 / width 1.6 / depth 1.4 / work_top 0.8; mouth ~0.63 of the dome height).
    W, D = 14, 12                     # 1.56 x 1.33 m masonry mass
    FLOOR = 6                         # solid base y0..6 -> hearth floor top at y7 = 0.778 m (work_top ~0.8)
    DOME = 11                         # dome shell ceiling (cavity y7..10 ~ 0.44 m interior, dome_height 0.45)
    m = Model()
    m.fill(0, W - 1, 0, DOME, 0, D - 1, "Stone")          # solid masonry mass
    # baking chamber: carve ~0.9 m interior above the hearth floor, thick walls all round (z 2..D-3)
    m.clear(3, W - 4, FLOOR + 1, DOME - 1, 2, D - 3)
    m.fill(3, W - 4, FLOOR + 1, FLOOR + 1, 2, D - 3, "glow")   # glowing coals on the hearth floor
    # mouth: an arched opening through the FRONT wall (z 0..1) at the hearth level (~0.28 m tall)
    m.clear(5, W - 6, FLOOR + 1, FLOOR + 3, 0, 1)
    # hood + flue: rise up the BACK (z D-2..D-1) within the footprint -- the vent
    m.fill(4, W - 5, DOME + 1, 20, D - 2, D - 1, "Stone")
    anchors = [{
        "point_id": "work_0", "kind": "work_surface",
        "local_position": [round((W / 2.0) / 9, 3), round(7 / 9, 3), round((D - 1) / 9, 3)],
        "facing_yaw": 0.0, "features": {"work_top_y": round(7 / 9, 3)},
    }]
    header = (
        "# ==========================================================\n"
        "# ASSET METADATA\n"
        "# name:         oven_bread\n"
        "# display_name: Bread Oven\n"
        "# description:  A stone masonry dome bread oven -- baking chamber of glowing coals, arched mouth, back flue.\n"
        "# category:     furniture\n"
        "# subcategory:  fixture\n"
        "# tags:         oven, bread, bakery, baker, bakehouse, fire, vented\n"
        "# materials:    Stone, glow\n"
        "# facing:       +Z (mouth opens to the room; flue / chimney breast to the wall, +z)\n"
        "# work_top:     0.778\n"
        "# bounds:       1.56W x ~2.3H x 1.33D m (grounded object_dimensions 'oven_bread' interior 0.9 / width 1.6 / depth 1.4 / work_top 0.8)\n"
        "# method:       tools/regen_furniture.py (deterministic, canon-proportioned)\n"
        "# =========================================================="
    )
    m.mirror_z()   # mouth -> +Z (faces the room); flue/chimney breast -> the wall (placer +Z convention)
    write("oven_bread", header, m.emit_lines([]), m, anchors)


def gen_chopping_block():
    # butcher's chopping block: a heavy end-grain Log top on Wood legs, work_top ~0.89 m (chopping/work
    # height, anthropometric). top ~0.56 x 0.56 m. The butcher's defining fixture (cf. the smith's anvil).
    W, D = 5, 5                       # 0.556 x 0.556 m top
    TOP = 7                           # legs y0..4, thick top y5..7 -> top face 8/9 = 0.889 m (work_top 0.89)
    m = Model()
    m.fill(0, W - 1, 5, TOP, 0, D - 1, "Log")             # thick end-grain block top
    for lx in (0, W - 1):
        for lz in (0, D - 1):
            m.fill(lx, lx, 0, 4, lz, lz, "WoodWalnut")          # leg
    anchors = [{
        "point_id": "work_0", "kind": "work_surface",
        "local_position": [round((W / 2.0) / 9, 3), round(8 / 9, 3), round((D / 2.0) / 9, 3)],
        "facing_yaw": 0.0, "features": {"work_top_y": round(8 / 9, 3)},
    }]
    header = (
        "# ==========================================================\n"
        "# ASSET METADATA\n"
        "# name:         chopping_block\n"
        "# display_name: Chopping Block\n"
        "# description:  A heavy end-grain wooden butcher's chopping block on legs -- the cutting surface.\n"
        "# category:     furniture\n"
        "# subcategory:  fixture\n"
        "# tags:         butcher, block, chopping, shambles, meat\n"
        "# materials:    Log, Wood\n"
        "# facing:       +Z\n"
        "# work_top:     0.889\n"
        "# bounds:       0.56W x 0.89H x 0.56D m (grounded object_dimensions 'chopping_block' work_top 0.89)\n"
        "# method:       tools/regen_furniture.py (deterministic, canon-proportioned)\n"
        "# =========================================================="
    )
    write("chopping_block", header, m.emit_lines([]), m, anchors)


def gen_meat_rail():
    # butcher's hanging meat rail: a FREESTANDING post rack (two Log posts + a top rail) hung with iron
    # (Metal) hooks. ~2.0 m tall (carcasses clear the floor), ~1.56 m span. The hanging MEAT itself awaits
    # a meat material (user's materials thread) -- the iron hooks are grounded, the meat is not faked.
    W = 14                            # 1.556 m span
    H = 18                            # 2.0 m tall
    m = Model()
    for px in (0, W - 1):
        m.fill(px, px, 0, H, 6, 6, "Log")                 # vertical posts (z mid)
    m.fill(0, W - 1, H - 1, H, 6, 6, "Log")               # top rail
    for hx in range(2, W - 1, 3):                          # iron hooks hanging below the rail
        m.m(hx, H - 2, 6, "Metal")
        m.m(hx, H - 3, 6, "Metal")
    header = (
        "# ==========================================================\n"
        "# ASSET METADATA\n"
        "# name:         meat_rail\n"
        "# display_name: Meat Rail\n"
        "# description:  A freestanding butcher's rail -- two posts + a top rail hung with iron hooks (meat awaits a material).\n"
        "# category:     furniture\n"
        "# subcategory:  fixture\n"
        "# tags:         butcher, meat, rail, hooks, shambles, hanging\n"
        "# materials:    Log, Metal\n"
        "# facing:       +Z\n"
        "# bounds:       1.56W x 2.0H x 0.11D m (grounded object_dimensions 'meat_rail')\n"
        "# method:       tools/regen_furniture.py (deterministic, canon-proportioned)\n"
        "# =========================================================="
    )
    write("meat_rail", header, m.emit_lines([]), m)


def gen_hanging_sign():
    # hanging_sign: a projecting pictorial trade sign on a wrought-iron bracket, hung over a shop
    # entrance. Authored BROADSIDE-to-street in the y-z plane (1 micro thick in x = 0.11 m board, read
    # walking along the street): the wall mount is at local z=0, a Metal bracket arm projects +Z, and a
    # Wood board (Log-framed) hangs from the arm's reach. NO mirror_z: the board (the decal/approach face)
    # is already at z-high = +Z, matching the placer's rot-0 front=+z convention. The board's image is the
    # DECAL system (backlog) -- this is the BOARD + BRACKET only, never a faked picture (placer F4).
    # Grounded (object_dimensions 'hanging_sign'): board ~0.78 x 0.56 m (area 0.42 m2 << 12 sq ft / 1.11
    # m2 cap); projection 0.78 m (< 48 in / 1.22 m cap). The >= 8 ft (2.44 m) ground clearance is a
    # PLACEMENT/mount-height concern (task 30), not the asset geometry.
    Z0, Z1 = 1, 6                     # board spans z 1..6 = 6 micro = 0.667 m (broadside width)
    BY0, BY1 = 0, 4                   # board spans y 0..4 = 5 micro = 0.556 m tall; bottom at local y0
    ARM_Y = 6                         # bracket arm at y6; arm reaches wall (z0) -> board (z6)
    m = Model()
    # board: Wood field with a Log frame top + bottom (a framed sign board), thin in x (x=0)
    m.fill(0, 0, BY0, BY1, Z0, Z1, "WoodWalnut")
    m.fill(0, 0, BY0, BY0, Z0, Z1, "Log")     # bottom rail
    m.fill(0, 0, BY1, BY1, Z0, Z1, "Log")     # top rail
    # wrought-iron bracket: an arm from the wall (z0) out over the board, + a short wall foot, + two
    # hanger links dropping from the arm to the board's top corners.
    m.fill(0, 0, ARM_Y, ARM_Y, 0, Z1, "Metal")   # the projecting arm (z0..6)
    m.m(0, ARM_Y - 1, 0, "Metal")                 # wall foot (the bolted bracket base at z0)
    m.m(0, BY1 + 1, Z0, "Metal")                  # hanger link, near corner
    m.m(0, BY1 + 1, Z1, "Metal")                  # hanger link, far corner
    header = (
        "# ==========================================================\n"
        "# ASSET METADATA\n"
        "# name:         hanging_sign\n"
        "# display_name: Hanging Sign (Trade Sign)\n"
        "# description:  A projecting pictorial trade sign -- a framed wooden board on a wrought-iron bracket, hung over a shop entrance. (Board image = the decal system, backlog; this is board + bracket only.)\n"
        "# category:     signage\n"
        "# subcategory:  sign\n"
        "# tags:         sign, signage, shop, trade, hanging, bracket, street\n"
        "# materials:    Wood, Log, Metal\n"
        "# facing:       +Z (board/approach face to +Z; the wall mount is at -Z / local z=0)\n"
        "# bounds:       0.11W x ~0.78H x 0.78D m (grounded object_dimensions 'hanging_sign' board 0.8x0.6 / projection 0.8)\n"
        "# method:       tools/regen_furniture.py (deterministic, canon-proportioned)\n"
        "# =========================================================="
    )
    write("hanging_sign", header, m.emit_lines([]), m)


if __name__ == "__main__":
    gen_chest()
    gen_fireplace()
    gen_bar()
    gen_back_bar()
    gen_bar_stool()
    gen_candle_stand()
    gen_wall_lantern()
    gen_chandelier()
    gen_mug()
    gen_bottle()
    gen_table_wood()
    gen_tavern_table()
    gen_counter()
    gen_barrel()
    gen_bench_wood()
    gen_forge_hearth()
    gen_anvil()
    gen_bellows()
    gen_tool_rack()
    gen_oven_bread()
    gen_chopping_block()
    gen_meat_rail()
    gen_hanging_sign()
