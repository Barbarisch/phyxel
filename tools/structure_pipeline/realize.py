"""
Structure Pipeline — detailed building realizer.

Builds a BuildingSpec's SHELL (walls, floor, roof) into a DetailCanvas with proper trim —
framed window/door reveals and a beveled roof coping — exports it as a single multi-resolution
.voxel template (cube bulk, sub/microcube detail only where it earns its keep), and orchestrates
the functional doors + subcube furniture over the engine HTTP API.

Two-phase by design (the engine caches templates at startup, no on-demand load):
  1. write the shell .voxel template          (offline)
  2. --build: spawn it + place doors/furniture (needs the engine launched AFTER step 1)

Walls stay full cubes; only the framed openings and roof coping spend subcubes/microcubes.
"""

from __future__ import annotations

import json
import urllib.request
from pathlib import Path
from typing import Dict, List, Optional, Tuple

from .spec import BuildingSpec
from .detail import DetailCanvas, frame_on_face, pitched_roof, subcube_stairs, AIR
from .doors import selected_door_for_portal

_REPO = Path(__file__).resolve().parents[2]
TEMPLATES_DIR = _REPO / "resources" / "templates"
ENGINE = "http://localhost:8090"
DOOR_LEAF_H = 2


def door_leaves_for_width(w: int):
    """Tile an opening `w` cubes wide with door leaves so there is NO gap (a width-4 grand door
    becomes two 2-wide leaves). Returns (template, offset_along_wall, leaf_width). door_wood is
    1 wide so any width is fully coverable."""
    leaves, off = [], 0
    while w - off >= 2:
        leaves.append(("door_wood_wide", off, 2)); off += 2
    if w - off == 1:
        leaves.append(("door_wood", off, 1)); off += 1
    return leaves

# fixture type -> subcube furniture template (matches the engine-side map)
FIXTURE_TEMPLATES = {
    "table": "table_wood", "chair": "chair_wood", "stool": "stool", "bed": "bed_single",
    "counter": "tavern_bar", "bar": "tavern_bar", "altar": "altar",
    "pew": "bench_wood", "bench": "bench_wood", "barrel": "barrel",
    "bookshelf": "bookshelf", "shelf": "wall_shelf", "bookcase": "bookshelf",
    "books": "book_stack", "book": "book_stack",
    "wardrobe": "wardrobe", "dresser": "dresser", "desk": "desk", "fireplace": "fireplace",
    # surface clutter (placed on top of tables/desks/shelves)
    "candlestick": "candlestick", "candle": "candlestick", "goblet": "goblet", "cup": "goblet",
    "bottle": "bottle", "jug": "bottle", "plate": "plate", "bowl": "plate",
}

# Small props that sit ON a surface (table/desk/shelf/mantel), not on the floor.
CLUTTER_TYPES = {"candlestick", "candle", "goblet", "cup", "bottle", "jug", "plate", "bowl",
                 "books", "book", "scroll", "inkwell", "vase"}
_FACING_ROT = {"north": 0, "east": 90, "south": 180, "west": 270}


# --------------------------------------------------------------------------- geometry helpers

def _shared_wall(a, b) -> Optional[Tuple[str, int, int, int]]:
    ax0, az0, ax1, az1 = a[0], a[1], a[0] + a[2], a[1] + a[3]
    bx0, bz0, bx1, bz1 = b[0], b[1], b[0] + b[2], b[1] + b[3]
    if ax1 == bx0 or bx1 == ax0:
        coord = ax1 if ax1 == bx0 else ax0
        lo, hi = max(az0, bz0), min(az1, bz1)
        if hi - lo > 0:
            return ("x", coord, lo, hi)
    if az1 == bz0 or bz1 == az0:
        coord = az1 if az1 == bz0 else az0
        lo, hi = max(ax0, bx0), min(ax1, bx1)
        if hi - lo > 0:
            return ("z", coord, lo, hi)
    return None


def _resolve_portal(portal, rooms: Dict[str, tuple], W: int, D: int):
    """Return (axis, coord, outward_normal or None). normal None for interior walls."""
    a, b = portal.between
    if "exterior" not in (a, b):
        ra, rb = rooms.get(a), rooms.get(b)
        if ra and rb:
            sw = _shared_wall(ra, rb)
            if sw:
                return (sw[0], sw[1], None)
        return None
    px, pz = portal.pos
    if px == 0:   return ("x", 0,     "-x")
    if px == W:   return ("x", W - 1, "+x")
    if pz == 0:   return ("z", 0,     "-z")
    if pz == D:   return ("z", D - 1, "+z")
    return None


# --------------------------------------------------------------------------- shell builder

def build_shell(spec: BuildingSpec, trim: Optional[str] = None) -> DetailCanvas:
    pal = spec.palette or {}
    wall = pal.get("wall", "StoneBricks")
    floor = pal.get("floor", "Wood")
    roof = pal.get("roof", "Wood")
    trim = trim or pal.get("trim", "Wood")
    W, D = spec.footprint
    c = DetailCanvas()

    baseY = 0
    topY = 0
    occupied: set = set()
    stair_holes: list = []   # (floor_y, sx, sz, sw, sd) carved AFTER all floors are placed
    story_layers: list = []  # (topY, occupied_set) per story, for stepped roofing
    for story in spec.stories:
        h = story.height
        rooms = {r.id: tuple(r.rect) for r in story.rooms}
        rooms_purpose = {r.id: r.purpose for r in story.rooms}

        # Footprint = UNION of the room rects (any L/T/U/wing shape, not just a rectangle).
        occupied = set()
        for (rx, rz, rw, rd) in rooms.values():
            for cx in range(rx, rx + rw):
                for cz in range(rz, rz + rd):
                    occupied.add((cx, cz))

        for (cx, cz) in occupied:                               # floor slab follows the outline
            c.add_cube(cx, baseY, cz, floor)

        # Perimeter walls = the OUTLINE of the occupied region (cells touching the exterior).
        wallset = {(cx, cz) for (cx, cz) in occupied
                   if any((cx + dx, cz + dz) not in occupied
                          for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1)))}
        # Interior partitions = shared room boundaries.
        ids = list(rooms)
        for i in range(len(ids)):
            for j in range(i + 1, len(ids)):
                sw = _shared_wall(rooms[ids[i]], rooms[ids[j]])
                if not sw:
                    continue
                ax, coord, lo, hi = sw
                for t in range(lo, hi):
                    wallset.add((coord, t) if ax == "x" else (t, coord))
        for (cx, cz) in wallset:
            for y in range(baseY + 1, baseY + h + 1):
                c.add_cube(cx, y, cz, wall)

        # openings: exterior windows/doors get framed reveals; interior + arches are carved.
        for p in story.portals:
            res = _resolve_portal(p, rooms, W, D)
            if not res:
                continue
            ax, coord, normal = res
            # Vertical placement: doors/arches start at the floor; windows sit on a sill ~1 cube
            # up (a floor-level "window" you can walk through looks wrong) and stay under the ceiling.
            if p.kind == "door":
                # The selected door type drives the opening size (variable openings).
                lockable = bool(p.door and p.door.lockable)
                dd = selected_door_for_portal(p.between, p.width, lockable, h, rooms_purpose)
                sill_h, ph, ow_door = 0, dd.height, dd.width
            elif p.kind == "window":
                sill_h = 1 if h >= 3 else 0          # need head + sill room; tiny rooms skip the sill
                ph, ow_door = min(p.height, h - sill_h), p.width
            else:  # arch
                sill_h, ph, ow_door = 0, min(p.height, h), p.width
            oy = baseY + 1 + sill_h
            px, pz = p.pos
            if ax == "z":
                ox, oz, ow, oh, od = px, coord, ow_door, ph, 1
            else:
                ox, oz, ow, oh, od = coord, pz, 1, ph, ow_door
            if normal and p.kind != "arch":
                frame_on_face(c, normal, ox, oy, oz, ow, oh, od, trim,
                              sill=(p.kind == "window"))
            else:
                c.fill_micro_box(ox * 9, oy * 9, oz * 9, ow * 9, oh * 9, od * 9, AIR)

        # Stairs (multi-story): a subcube staircase up. Defer the stairwell hole — if carved now,
        # the NEXT story's floor slab refills it (was the cause of the blocked stair-top).
        for st in story.stairs:
            sx, sz, sw, sd = st.rect
            subcube_stairs(c, sx, baseY + 1, sz, climb=h + 1, width=max(1, sw), mat=floor)
            stair_holes.append((baseY + h + 1, sx, sz, sw, sd))

        topY = baseY + h + 1
        story_layers.append((topY, set(occupied)))
        baseY = topY

    # Carve every stairwell hole now that all floor slabs exist, so they stay open (headroom).
    for nbY, sx, sz, sw, sd in stair_holes:
        for x in range(sx, sx + sw):
            for z in range(sz, sz + sd):
                c.fill_micro_box(x * 9, nbY * 9, z * 9, 9, 9, 9, AIR)

    # Roof: cap EVERY column at the top of the highest story that occupies it, so lower wings of a
    # stepped building get their own roof (not just the top story's footprint). The topmost level
    # may be a pitched gable if it's a true rectangle; everything else is flat with a beveled coping.
    roof_style = (spec.roof or {}).get("style", "flat")
    col_top: dict = {}
    for ty, occ_s in story_layers:                  # later (higher) stories overwrite -> highest wins
        for col in occ_s:
            col_top[col] = ty
    from collections import defaultdict
    levels: dict = defaultdict(set)
    for col, ty in col_top.items():
        levels[ty].add(col)
    top_level = max(levels)
    for ty, cols in sorted(levels.items()):
        xs = [p[0] for p in cols]
        zs = [p[1] for p in cols]
        bx0, bx1, bz0, bz1 = min(xs), max(xs) + 1, min(zs), max(zs) + 1
        is_rect = len(cols) == (bx1 - bx0) * (bz1 - bz0)
        if ty == top_level and roof_style == "pitched" and is_rect and (bx1 - bx0) >= 2 and (bz1 - bz0) >= 2:
            pitched_roof(c, bx0, ty, bz0, bx1 - bx0, bz1 - bz0, roof, gable=wall, pitch=2)
        else:
            for (cx, cz) in cols:
                c.add_cube(cx, ty, cz, roof)
            for (cx, cz) in cols:
                for dx, dz, axis, corner in ((1, 0, "z", "+y+x"), (-1, 0, "z", "+y-x"),
                                             (0, 1, "x", "+y+z"), (0, -1, "x", "+y-z")):
                    if (cx + dx, cz + dz) not in cols:
                        c.chamfer_edge(cx * 9, ty * 9, cz * 9, 9, 9, 9, axis, corner, 3)
    return c


def write_template(spec: BuildingSpec, name: str, trim: Optional[str] = None) -> Path:
    canvas = build_shell(spec, trim=trim)
    body = [f"# {name} - detailed building shell (multi-resolution)",
            "# Format: C cx cy cz Mat | S .. sx sy sz Mat | M .. mx my mz Mat", ""]
    body += canvas.to_voxel_lines()
    path = TEMPLATES_DIR / f"{name}.voxel"
    path.write_text("\n".join(body) + "\n", encoding="utf-8")
    return path, canvas


# --------------------------------------------------------------------------- engine orchestration

def _post(engine: str, path: str, body: dict) -> dict:
    req = urllib.request.Request(engine + path, data=json.dumps(body).encode(),
                                 headers={"Content-Type": "application/json"})
    try:
        return json.loads(urllib.request.urlopen(req, timeout=40).read().decode())
    except Exception as e:
        return {"error": str(e)}


def _spawn(engine: str, name: str, x: int, y: int, z: int, rotation: int = 0) -> dict:
    """spawn_template expects a NESTED position object, not flat x/y/z."""
    return _post(engine, "/api/world/template",
                 {"name": name, "position": {"x": x, "y": y, "z": z}, "rotation": rotation})


def drive_engine(spec: BuildingSpec, name: str, position, engine: str = ENGINE) -> dict:
    """Spawn the (already-loaded) shell template, then place functional doors + furniture."""
    px, py, pz = position
    W, D = spec.footprint
    out = {"shell": _spawn(engine, name, px, py, pz), "doors": [], "fixtures": []}

    baseY = 0
    for story in spec.stories:
        rooms = {r.id: tuple(r.rect) for r in story.rooms}
        rooms_purpose = {r.id: r.purpose for r in story.rooms}
        for p in story.portals:
            if p.kind != "door":
                continue
            res = _resolve_portal(p, rooms, W, D)
            if not res:
                continue
            ax, coord, _ = res
            pxl, pzl = p.pos
            # Select the situation-appropriate door (drives the opening size in build_shell too).
            lockable = bool(p.door and p.door.lockable)
            dd = selected_door_for_portal(p.between, p.width, lockable, story.height, rooms_purpose)
            if ax == "z":
                hinge, rot = (pxl, baseY + 1, coord), 0
            else:
                hinge, rot = (coord, baseY + 1, pzl), 90
            hw = {"x": px + hinge[0], "y": py + hinge[1], "z": pz + hinge[2]}
            sp = _spawn(engine, dd.name, hw["x"], hw["y"], hw["z"], rot)
            oid = sp.get("object_id")
            if oid:
                _post(engine, "/api/door/register",
                      {"placed_object_id": oid, "template_name": dd.name, "hinge": hw,
                       "base_rotation": rot, "thickness": 5})
                if lockable:
                    _post(engine, "/api/door/lock",
                          {"placed_object_id": oid, "locked": True, "key_item_id": p.door.key})
                out["doors"].append({"id": oid, "template": dd.name, "style": dd.style,
                                     "swing": dd.swing, "locked": lockable})
        for f in story.fixtures:
            tmpl = FIXTURE_TEMPLATES.get(f.type)
            if not tmpl:
                continue
            rot = _FACING_ROT.get(f.facing, 0)
            y_off = 2 if f.type in CLUTTER_TYPES else 1   # clutter sits on a surface, not the floor
            sp = _spawn(engine, tmpl, px + f.rect[0], py + baseY + y_off, pz + f.rect[1], rot)
            out["fixtures"].append({"id": sp.get("object_id"), "template": tmpl})
        baseY += story.height + 1
    return out


# --------------------------------------------------------------------------- CLI

def main(argv=None) -> int:
    import argparse
    import sys

    ap = argparse.ArgumentParser(prog="structure_pipeline.realize",
                                 description="Build a detailed building from a BuildingSpec.")
    ap.add_argument("spec", type=Path, help="BuildingSpec JSON")
    ap.add_argument("--name", default=None, help="template name (default: spec stem)")
    ap.add_argument("--build", action="store_true", help="also spawn it + place doors/furniture")
    ap.add_argument("--position", default="0,16,0", help="x,y,z world position for --build")
    ap.add_argument("--engine", default=ENGINE)
    ap.add_argument("--force", action="store_true",
                    help="build even if the functional/playtest pass reports errors")
    ap.add_argument("--playtest", action="store_true",
                    help="after --build, run the runtime (Tier C) playtest on the live engine")
    args = ap.parse_args(argv)

    spec = BuildingSpec.from_dict(json.loads(args.spec.read_text(encoding="utf-8")))
    name = args.name or args.spec.stem
    path, canvas = write_template(spec, name)
    print(f"[realize] wrote {path}  ({canvas.report().summary()})", file=sys.stderr)

    # Functional gate: topological + ergonomic (Tier A) + walkable (Tier B).
    from .playtest import full_validate
    report = full_validate(spec)
    if not report.ok:
        print(f"[realize] functional check: {report.summary()}", file=sys.stderr)
        if args.build and not args.force:
            print("[realize] refusing to --build a non-functional spec (use --force to override)",
                  file=sys.stderr)
            return 1
    else:
        print("[realize] functional check: OK (walkable, ergonomic)", file=sys.stderr)

    if args.build:
        x, y, z = (int(v) for v in args.position.split(","))
        result = drive_engine(spec, name, (x, y, z), args.engine)
        print(f"[realize] built: {len(result['doors'])} doors, {len(result['fixtures'])} fixtures",
              file=sys.stderr)
        print(json.dumps(result, indent=2))

        if args.playtest:
            from .playtest import runtime_playtest
            rt = runtime_playtest(spec, (x, y, z), args.engine)
            doors_ok = sum(d["operable"] for d in rt["doors"])
            nav_ok = sum(n["reachable"] for n in rt["navigation"])
            print(f"[realize] runtime playtest: {'OK' if rt['ok'] else 'ISSUES'} — "
                  f"{doors_ok}/{len(rt['doors'])} doors operable, "
                  f"{nav_ok}/{len(rt['navigation'])} rooms navigable", file=sys.stderr)
            for note in rt["notes"]:
                print(f"    note: {note}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
