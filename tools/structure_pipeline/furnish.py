"""
Structure Pipeline — deterministic ROOM FURNISHERS.

The LLM decides the PROGRAM (which rooms, where, doors/windows). This module FILLS each room
correctly by construction: the right furniture for the room type, placed against the right walls,
facing the room, clear of the doors, leaving a walkable path, lit, with clutter on the surfaces —
so the output passes every geometry check automatically. A 'library' becomes lined with bookshelves
and a 'master bedroom' gets a four-poster, wardrobe, dresser and nightstands, with no LLM guesswork.

`furnish_spec(spec)` returns a new BuildingSpec whose rooms are furnished (existing fixtures kept).
Each light fixture is also recorded so the realizer can drop a point light at it (glow is decorative
only; real illumination needs point lights — see geometry/MaterialTextureNeeds notes).
"""

from __future__ import annotations

import json
from typing import Dict, List, Optional, Tuple

from .spec import BuildingSpec
from .realize import build_shell, FIXTURE_TEMPLATES, _FACING_ROT, CLUTTER_TYPES
from .geometry import template_cube_footprint, LIGHT_FIXTURE_TYPES
from .playtest import (_cube_occupancy, _story_base_y, _FULL_CUBE, _bounds,
                       _swing_sides, _swing_block, classify_purpose)

Cell = Tuple[int, int]
# wall the BACK faces -> facing string (front points into the room)
_FACING_FOR_WALL = {(0, -1): "north", (0, 1): "south", (-1, 0): "east", (1, 0): "west"}
_SIDES = {"N": (0, -1), "S": (0, 1), "W": (-1, 0), "E": (1, 0)}


# --------------------------------------------------------------------------- room classification

def room_kind(purpose: str) -> str:
    """A finer room type than classify_purpose, for choosing a furnisher recipe."""
    p = (purpose or "").lower()
    table = [
        ("library", ("library", "study", "office", "den", "reading")),
        ("master_bedroom", ("master", "lord", "lady")),
        ("bedroom", ("bedroom", "bed chamber", "ireena", "ismark", "guest", "nursery", "chamber")),
        ("kitchen", ("kitchen", "scullery", "galley", "pantry")),
        ("dining", ("dining", "banquet", "mess")),
        ("drawing", ("drawing", "parlor", "parlour", "sitting", "lounge", "salon", "living")),
        ("entry", ("entry", "entrance", "foyer", "vestibule", "reception")),
        ("bath", ("bath", "privy", "washroom", "lavatory")),
        ("servant", ("servant", "quarter")),
        ("storeroom", ("cellar", "storeroom", "store", "vault", "larder")),
        ("chapel", ("chapel", "shrine", "altar", "temple", "nave")),
        ("hall", ("landing", "hallway", "corridor", "passage", "great hall", "hall")),
    ]
    for kind, keys in table:
        if any(k in p for k in keys):
            return kind
    return "generic"


# --------------------------------------------------------------------------- room plan

class RoomPlan:
    """Tracks a room's free floor, walls and door clearances, and places fixtures that respect
    fixture-placement / wall-backing / circulation / access by construction."""

    def __init__(self, occ, base_y: int, room, portals, rooms_by_id, footprint):
        self.occ = occ
        self.y = base_y + 1
        self.room = room
        self.x0, self.z0, self.x1, self.z1 = _bounds(tuple(room.rect))
        fw, fd = footprint
        self.free = {(x, z) for x in range(self.x0, self.x1) for z in range(self.z0, self.z1)
                     if occ.get((x, self.y, z), 0) != _FULL_CUBE}
        self.taken: set = set()
        self.keep_clear: set = set()                 # door approaches
        for p in portals:
            if p.kind not in ("door", "arch") or room.id not in p.between:
                continue
            sides = _swing_sides(p, rooms_by_id, fw, fd)
            if not sides:
                continue
            p_lo = p.pos[1] if sides[0][0] == "x" else p.pos[0]
            for axis, rm, sign, coord in sides:
                if rm is not None and rm.id == room.id:
                    self.keep_clear |= _swing_block(axis, coord, sign, p_lo, 2)
        self.fixtures: List[dict] = []

    # --- queries ---
    def footprint(self, ftype: str, facing: str) -> Tuple[int, int]:
        tmpl = FIXTURE_TEMPLATES.get(ftype)
        return template_cube_footprint(tmpl, _FACING_ROT.get(facing, 0)) if tmpl else (1, 1)

    def _cells(self, x, z, fw, fd):
        return {(x + i, z + j) for i in range(fw) for j in range(fd)}

    def _ok(self, cells, ignore_clear=False):
        bad = self.taken | (set() if ignore_clear else self.keep_clear)
        return all(c in self.free and c not in bad for c in cells)

    def _wall_at(self, x, z, dx, dz):
        return self.occ.get((x + dx, self.y, z + dz), 0) == _FULL_CUBE

    # --- placement ---
    def add(self, ftype, x, z, facing, surface_of: Optional[int] = None):
        fw, fd = self.footprint(ftype, facing)
        f = {"type": ftype, "rect": [x, z, fw, fd], "facing": facing, "room": self.room.id}
        self.fixtures.append(f)
        if ftype not in CLUTTER_TYPES:                # clutter overlaps its surface; don't block
            self.taken |= self._cells(x, z, fw, fd)
        return f

    def against_wall(self, ftype, sides="NSWE", light=False) -> Optional[dict]:
        """Place a piece flush to a wall (back to the wall, front to the room), in the first run
        that fits, skipping door approaches."""
        for s in sides:
            dx, dz = _SIDES[s]
            facing = _FACING_FOR_WALL[(dx, dz)]
            fw, fd = self.footprint(ftype, facing)
            if s == "N":
                z = self.z0
                xs = range(self.x0, self.x1 - fw + 1)
                cands = [(x, z) for x in xs]
                back = (0, -1)
            elif s == "S":
                z = self.z1 - fd
                cands = [(x, z) for x in range(self.x0, self.x1 - fw + 1)]
                back = (0, 1)
            elif s == "W":
                x = self.x0
                cands = [(x, z) for z in range(self.z0, self.z1 - fd + 1)]
                back = (-1, 0)
            else:  # E
                x = self.x1 - fw
                cands = [(x, z) for z in range(self.z0, self.z1 - fd + 1)]
                back = (1, 0)
            for (cx, cz) in cands:
                cells = self._cells(cx, cz, fw, fd)
                if not self._ok(cells):
                    continue
                # the back edge must actually touch a wall
                if not any(self._wall_at(bx, bz, *back) for (bx, bz) in cells):
                    continue
                return self.add(ftype, cx, cz, facing, )
        return None

    def fill_wall(self, ftype, sides="NSWE", count=99) -> int:
        n = 0
        while n < count and self.against_wall(ftype, sides=sides):
            n += 1
        return n

    def center(self, ftype, facing="north") -> Optional[dict]:
        fw, fd = self.footprint(ftype, facing)
        cx = (self.x0 + self.x1) // 2 - fw // 2
        cz = (self.z0 + self.z1) // 2 - fd // 2
        # search outward from the centre for a fit
        for r in range(0, max(self.x1 - self.x0, self.z1 - self.z0)):
            for ox in range(-r, r + 1):
                for oz in range(-r, r + 1):
                    x, z = cx + ox, cz + oz
                    if self._ok(self._cells(x, z, fw, fd)):
                        return self.add(ftype, x, z, facing)
        return None

    def on_surface(self, ftype, surface: dict):
        """Place a clutter item on a surface fixture (same near-corner cell)."""
        x, z, fw, fd = surface["rect"]
        self.add(ftype, x + fw // 2, z + fd // 2, "north")

    def light(self, prefer_table: Optional[dict] = None):
        """Guarantee a light source: candelabra on a table if available, else a wall sconce."""
        if prefer_table is not None and self.on_surface_ok(prefer_table):
            self.on_surface("candelabra", prefer_table)
            return
        if not self.against_wall("sconce"):
            self.against_wall("torch")

    def on_surface_ok(self, surface):
        return surface is not None and FIXTURE_TEMPLATES.get(surface.get("type"))


# --------------------------------------------------------------------------- recipes

def _bed_for(plan: RoomPlan, kind: str) -> Optional[dict]:
    return plan.against_wall("four_poster" if kind == "master_bedroom" else "bed_single",
                             sides="NSWE")


def furnish_room(plan: RoomPlan, kind: str) -> None:
    if kind in ("library",):
        plan.fill_wall("bookshelf")
        desk = plan.against_wall("desk") or plan.center("desk")
        if desk:
            plan.add("armchair", desk["rect"][0], desk["rect"][1] + desk["rect"][3], "south")
            plan.on_surface("books", desk)
            plan.on_surface("candelabra", desk)
        plan.center("rug")
        if not any(f["type"] in LIGHT_FIXTURE_TYPES for f in plan.fixtures):
            plan.light(desk)

    elif kind == "master_bedroom":
        bed = _bed_for(plan, kind)
        plan.against_wall("wardrobe")
        plan.against_wall("dresser")
        ns = plan.against_wall("nightstand")
        plan.center("rug")
        plan.light(ns)

    elif kind in ("bedroom", "servant"):
        _bed_for(plan, kind)
        plan.against_wall("wardrobe" if kind == "bedroom" else "barrel")
        ns = plan.against_wall("nightstand")
        if kind == "bedroom":
            plan.center("rug")
        plan.light(ns)

    elif kind == "kitchen":
        plan.fill_wall("counter", count=3)
        plan.against_wall("fireplace")
        plan.against_wall("barrel")
        t = plan.center("table")
        if t:
            plan.on_surface("plate", t)
        plan.light(t)

    elif kind == "dining":
        t = plan.center("long_table")
        if t:
            tx, tz, tw, td = t["rect"]
            plan.add("armchair", tx, tz - 1, "north")
            plan.add("armchair", tx + tw - 1, tz + td, "south")
            plan.on_surface("candelabra", t)
        plan.against_wall("sideboard")
        plan.center("rug")
        if not any(f["type"] in LIGHT_FIXTURE_TYPES for f in plan.fixtures):
            plan.light(t)

    elif kind in ("drawing", "hall", "entry"):
        plan.against_wall("fireplace")
        plan.against_wall("sideboard")
        for _ in range(2):
            plan.against_wall("armchair")
        plan.center("rug")
        plan.light(None)

    elif kind == "chapel":
        plan.against_wall("altar")
        plan.fill_wall("pew", count=4)
        plan.light(None)

    elif kind == "storeroom":
        plan.fill_wall("barrel", count=4)
        plan.fill_wall("shelf")
        plan.light(None)

    elif kind == "bath":
        plan.against_wall("dresser")               # washstand stand-in (no tub asset yet)
        plan.light(None)

    else:  # generic
        t = plan.center("table")
        if t:
            plan.add("chair", t["rect"][0], t["rect"][1] - 1, "north")
            plan.on_surface("candlestick", t)
        plan.light(t)

    # final guarantee: every room must have a light source
    if not any(f["type"] in LIGHT_FIXTURE_TYPES for f in plan.fixtures):
        plan.light(None)


# --------------------------------------------------------------------------- driver

def furnish_spec(spec: BuildingSpec, overwrite: bool = True) -> BuildingSpec:
    """Return a furnished copy of `spec`. Rooms with no fixtures (or all rooms if overwrite) are
    filled by their recipe. Existing windows/doors/stairs are untouched."""
    data = spec_to_dict(spec)
    canvas = build_shell(spec)
    occ = _cube_occupancy(canvas)
    bases = _story_base_y(spec)
    for si, story in enumerate(spec.stories):
        rooms_by_id = {r.id: r for r in story.rooms}
        existing = {f.room for f in story.fixtures}
        new_fixtures = [] if overwrite else list(data["stories"][si]["fixtures"])
        for room in story.rooms:
            if not overwrite and room.id in existing:
                continue
            plan = RoomPlan(occ, bases[si], room, story.portals, rooms_by_id, spec.footprint)
            furnish_room(plan, room_kind(room.purpose))
            new_fixtures.extend(plan.fixtures)
        data["stories"][si]["fixtures"] = new_fixtures
    return BuildingSpec.from_dict(data)


def spec_to_dict(spec: BuildingSpec) -> dict:
    """Serialise a BuildingSpec back to the plain dict shape from_dict accepts."""
    return {
        "kind": spec.kind, "name": spec.name, "style": spec.style,
        "palette": spec.palette, "function": spec.function, "footprint": list(spec.footprint),
        "roof": spec.roof,
        "stories": [{
            "height": s.height,
            "rooms": [{"id": r.id, "rect": list(r.rect), "purpose": r.purpose,
                       "floor_mat": r.floor_mat} for r in s.rooms],
            "portals": [{"between": list(p.between), "pos": list(p.pos), "width": p.width,
                         "height": p.height, "kind": p.kind,
                         **({"door": {"lockable": p.door.lockable, "key": p.door.key,
                                      "swing": p.door.swing}} if p.door else {})} for p in s.portals],
            "stairs": [{"from_story": st.from_story, "to_story": st.to_story,
                        "rect": list(st.rect), "kind": st.kind} for st in s.stairs],
            "fixtures": [{"type": f.type, "rect": list(f.rect), "facing": f.facing,
                          "room": f.room} for f in s.fixtures],
        } for s in spec.stories],
    }


def main(argv=None) -> int:
    import argparse
    import sys
    from .playtest import full_validate
    from . import geometry as G
    ap = argparse.ArgumentParser(prog="structure_pipeline.furnish",
                                 description="Deterministically furnish a building's rooms by type.")
    ap.add_argument("spec", type=Path if False else str)
    ap.add_argument("--out")
    args = ap.parse_args(argv)
    spec = BuildingSpec.from_dict(json.loads(open(args.spec).read()))
    furnished = furnish_spec(spec)
    canvas = build_shell(furnished)
    rep = full_validate(furnished)
    g = G.geometry_report(furnished, canvas)
    nfx = sum(len(s.fixtures) for s in furnished.stories)
    print(f"[furnish] {nfx} fixtures; full_validate {rep.summary().splitlines()[0]}; "
          f"geometry {'PASS' if g.ok else str(len(g.errors)) + ' err'}", file=sys.stderr)
    for i in g.errors[:20]:
        print("   ", i, file=sys.stderr)
    if args.out:
        open(args.out, "w").write(json.dumps(spec_to_dict(furnished), indent=2))
    return 0 if g.ok else 1


if __name__ == "__main__":
    import sys
    from pathlib import Path
    sys.exit(main())
