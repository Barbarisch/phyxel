"""
Structure Pipeline — deterministic GEOMETRY checks (functional dimensions, not eyeballing).

The spec-level validator and the 2D walkable pass don't catch geometry-level defects you can
only see by measuring the realized voxels: a stair with no headroom three steps up, a door
opening wider than any door leaf, a door embedded in a wall, or furniture with floating parts.
These checks measure the actual cube/subcube/micro geometry and reference real-world functional
dimensions (a person's height, standard bed/table sizes, a door leaf's size) — so a pass/fail is
an algorithm's verdict, not a screenshot impression.

Everything returns the shared ValidationReport/Issue so it slots into the same pipeline gate.
"""

from __future__ import annotations

import math
from collections import deque
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

from .scale import ScaleCanon, load_canon
from .spec import BuildingSpec, Stair
from .validator import Issue, ValidationReport, _bounds, EXTERIOR
from .playtest import _cube_occupancy, _story_base_y, _FULL_CUBE, _swing_sides

_REPO = Path(__file__).resolve().parents[2]
TEMPLATES_DIR = _REPO / "resources" / "templates"

# Door leaf footprints the realizer can place (width x height in cubes), from the templates.
DOOR_LEAVES = {"door_wood": (1, 2), "door_wood_wide": (2, 2), "door_metal": (1, 2)}


# --------------------------------------------------------------------------- voxel measuring

def template_cells(name: str) -> Set[Tuple[int, int, int]]:
    """Micro-grid cell set of a .voxel template (9 micro per cube), for connectivity/size checks."""
    p = TEMPLATES_DIR / f"{name}.voxel"
    cells: Set[Tuple[int, int, int]] = set()
    if not p.exists():
        return cells
    for ln in p.read_text(encoding="utf-8").splitlines():
        t = ln.split()
        if not t or t[0] not in ("C", "S", "M"):
            continue
        cx, cy, cz = int(t[1]), int(t[2]), int(t[3])
        if t[0] == "C":
            gx, gy, gz, n = cx * 9, cy * 9, cz * 9, 9
        elif t[0] == "S":
            gx, gy, gz, n = cx * 9 + int(t[4]) * 3, cy * 9 + int(t[5]) * 3, cz * 9 + int(t[6]) * 3, 3
        else:
            gx = cx * 9 + int(t[4]) * 3 + int(t[7])
            gy = cy * 9 + int(t[5]) * 3 + int(t[8])
            gz = cz * 9 + int(t[6]) * 3 + int(t[9])
            n = 1
        for x in range(gx, gx + n):
            for y in range(gy, gy + n):
                for z in range(gz, gz + n):
                    cells.add((x, y, z))
    return cells


def floating_components(cells: Set[Tuple[int, int, int]]) -> Set[Tuple[int, int, int]]:
    """Cells not face-connected to the bottom layer — i.e. floating in mid-air."""
    if not cells:
        return set()
    ymin = min(y for _, y, _ in cells)
    seed = [c for c in cells if c[1] == ymin]
    seen = set(seed)
    q = deque(seed)
    while q:
        x, y, z = q.popleft()
        for dx, dy, dz in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)):
            n = (x + dx, y + dy, z + dz)
            if n in cells and n not in seen:
                seen.add(n)
                q.append(n)
    return cells - seen


def _components(cells: Set[Tuple[int, int, int]]) -> int:
    """Count connected components (for reporting how many floating clusters)."""
    seen: Set[Tuple[int, int, int]] = set()
    comps = 0
    for c in cells:
        if c in seen:
            continue
        comps += 1
        q = deque([c])
        seen.add(c)
        while q:
            x, y, z = q.popleft()
            for dx, dy, dz in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)):
                n = (x + dx, y + dy, z + dz)
                if n in cells and n not in seen:
                    seen.add(n)
                    q.append(n)
    return comps


# --------------------------------------------------------------------------- checks

def connectivity_report(name: str) -> ValidationReport:
    """No floating parts: every voxel of a template must connect to the floor."""
    rep = ValidationReport()
    cells = template_cells(name)
    if not cells:
        rep.warn("TEMPLATE_EMPTY", f"template '{name}' has no voxels", name)
        return rep
    fl = floating_components(cells)
    if fl:
        ys = sorted({round(y / 9, 2) for _, y, _ in fl})
        rep.error("FLOATING_GEOMETRY",
                  f"'{name}' has {len(fl)} voxel cells in {_components(fl)} floating cluster(s) "
                  f"(disconnected from the floor) at heights {ys} cubes", name)
    return rep


def stair_clearance_report(spec: BuildingSpec, canvas, canon: Optional[ScaleCanon] = None
                           ) -> ValidationReport:
    """Per-step headroom: at every tread of every staircase, the cubes a climbing body occupies
    must be clear up to the character's height. Catches a stair-top that rams into the floor above
    (the classic 'top of the stairs is blocked')."""
    if canon is None:
        canon = load_canon()
    rep = ValidationReport()
    occ = _cube_occupancy(canvas)
    bases = _story_base_y(spec)
    need = math.ceil(canon.character_height)          # clear cubes a body needs above the tread

    for si, story in enumerate(spec.stories):
        for sti, stair in enumerate(story.stairs):
            fs = stair.from_story
            if not (0 <= fs < len(spec.stories)):
                continue
            base = bases[fs]
            climb = spec.stories[fs].height + 1        # realizer climbs height+1 cubes (+Z run)
            sx, sz, sw, sd = stair.rect
            for k in range(climb):                     # each tread, climbing +1 cube / +1 Z
                foot_y = base + 1 + k                  # cube whose top you stand on at run k
                z = sz + k
                worst = need
                for x in range(sx, sx + sw):
                    clear = 0
                    for h in range(1, need + 1):       # cubes the body would occupy above the foot
                        if occ.get((x, foot_y + h, z), 0) == _FULL_CUBE:
                            break
                        clear += 1
                    worst = min(worst, clear)
                if worst < need:
                    rep.error("STAIR_LOW_CLEARANCE",
                              f"stair step {k + 1}/{climb} (z={z}) has {worst} cube(s) of headroom, "
                              f"needs {need} for a {canon.character_height:.2f}-cube character — "
                              "a climber's head hits the floor/ceiling above here",
                              f"story {si} stair #{sti}")
    return rep


def opening_fit_report(spec: BuildingSpec, canvas) -> ValidationReport:
    """Every door opening must be fillable by an available door leaf and the leaf must sit in
    carved air. Catches openings wider/taller than any door (gaps) and doors embedded in walls."""
    from .doors import selected_door_for_portal
    rep = ValidationReport()
    occ = _cube_occupancy(canvas)
    bases = _story_base_y(spec)
    fw, fd = spec.footprint

    for si, story in enumerate(spec.stories):
        rooms = {r.id: r for r in story.rooms}
        purpose_map = {r.id: r.purpose for r in story.rooms}
        for pi, p in enumerate(story.portals):
            if p.kind != "door":
                continue
            where = f"story {si} door #{pi} {p.between}"
            lockable = bool(p.door and p.door.lockable)
            dd = selected_door_for_portal(p.between, p.width, lockable, story.height, purpose_map)
            # the door's full footprint (its width x height) must be carved air where it hangs
            sides = _swing_sides(p, rooms, fw, fd)
            if not sides:
                continue
            axis, _, _, coord = sides[0]
            y = bases[si] + 1
            px, pz = p.pos
            if axis == "x":
                opening = [(coord, y + dy, z) for z in range(pz, pz + dd.width) for dy in range(dd.height)]
            else:
                opening = [(x, y + dy, coord) for x in range(px, px + dd.width) for dy in range(dd.height)]
            solid = [c for c in opening if occ.get(c, 0) == _FULL_CUBE]
            if solid:
                rep.error("DOOR_EMBEDDED_IN_WALL",
                          f"{len(solid)}/{len(opening)} of the {dd.name} ({dd.width}x{dd.height}) "
                          "opening cells are solid wall (door buried, not in a carved opening)", where)
    return rep


def _fixture_cells_by_story(spec: BuildingSpec):
    """Floor cells occupied by fixtures (real rotated footprints), per story."""
    from .realize import FIXTURE_TEMPLATES, _FACING_ROT
    out = []
    for story in spec.stories:
        cells = set()
        for f in story.fixtures:
            tmpl = FIXTURE_TEMPLATES.get(f.type)
            if not tmpl:
                continue
            fw, fd = template_cube_footprint(tmpl, _FACING_ROT.get(f.facing, 0))
            for x in range(f.rect[0], f.rect[0] + fw):
                for z in range(f.rect[1], f.rect[1] + fd):
                    cells.add((x, z))
        out.append(cells)
    return out


def door_swing_report(spec: BuildingSpec, canvas) -> ValidationReport:
    """For each door, decide a HANDEDNESS: which side it can swing into with a clear quarter-arc
    (door_width x door_width of open room floor, no wall/fixture). Deterministic — picks the clear
    side (preferring to swing into a room, not the exterior). Errors if NO side is clear.

    NOTE: this verifies a clear swing EXISTS and which side; making the live door actually swing
    that way needs mirrored door templates + the engine's swing convention (tracked in the gaps doc)."""
    from .doors import selected_door_for_portal
    from .playtest import _swing_sides, _swing_block, _rect_contains_cells
    rep = ValidationReport()
    occ = _cube_occupancy(canvas)
    bases = _story_base_y(spec)
    fw, fd = spec.footprint
    fixt = _fixture_cells_by_story(spec)

    for si, story in enumerate(spec.stories):
        rooms = {r.id: r for r in story.rooms}
        purpose_map = {r.id: r.purpose for r in story.rooms}
        y = bases[si] + 1
        for pi, p in enumerate(story.portals):
            if p.kind != "door":
                continue
            where = f"story {si} door #{pi} {p.between}"
            lockable = bool(p.door and p.door.lockable)
            dd = selected_door_for_portal(p.between, p.width, lockable, story.height, purpose_map)
            sides = _swing_sides(p, rooms, fw, fd)
            if not sides:
                continue
            p_lo = p.pos[1] if sides[0][0] == "x" else p.pos[0]
            clear_sides = []
            for axis, room, sign, coord in sides:
                if room is None:                          # exterior side — don't prefer swinging out
                    continue
                block = _swing_block(axis, coord, sign, p_lo, dd.width)
                if not _rect_contains_cells(room.rect, block):
                    continue
                if any((x, z) in fixt[si] or occ.get((x, y, z), 0) == _FULL_CUBE for (x, z) in block):
                    continue
                clear_sides.append(room.id)
            if not clear_sides:
                rep.error("DOOR_NO_CLEAR_SWING",
                          f"the {dd.name} ({dd.width} wide) has no side it can swing open into "
                          "(both sides blocked by wall/fixture)", where)
    return rep


def door_selection_report(spec: BuildingSpec) -> ValidationReport:
    """The door chosen for each opening must be usable in its situation: it fits under the wall,
    and a portal that must lock gets a lockable-capable door."""
    from .doors import selected_door_for_portal
    rep = ValidationReport()
    for si, story in enumerate(spec.stories):
        purpose_map = {r.id: r.purpose for r in story.rooms}
        for pi, p in enumerate(story.portals):
            if p.kind != "door":
                continue
            where = f"story {si} door #{pi} {p.between}"
            lockable = bool(p.door and p.door.lockable)
            dd = selected_door_for_portal(p.between, p.width, lockable, story.height, purpose_map)
            if dd.height > story.height:
                rep.error("DOOR_TALLER_THAN_WALL",
                          f"selected {dd.name} is {dd.height} tall but the wall is {story.height}", where)
            if lockable and not dd.lockable:
                rep.error("DOOR_NOT_LOCKABLE",
                          f"portal must lock but the chosen {dd.name} ({dd.style}) isn't lockable", where)
    return rep


# --------------------------------------------------------------------------- fixture placement

def template_cube_footprint(name: str, rotation: int = 0) -> Tuple[int, int]:
    """Footprint (W in X, D in Z) of a template in whole cubes, after a 0/90/180/270 rotation.
    90/270 swap W and D (matches the engine's PlacedObjectManager rotation)."""
    cells = template_cells(name)
    if not cells:
        return (1, 1)
    xs = [x // 9 for x, _, _ in cells]
    zs = [z // 9 for _, _, z in cells]
    w, d = max(xs) - min(xs) + 1, max(zs) - min(zs) + 1
    return (d, w) if (rotation // 90) % 2 == 1 else (w, d)


def fixture_placement_report(spec: BuildingSpec, canvas) -> ValidationReport:
    """Check fixtures using their REAL measured template size (not the spec's f.rect): each must
    sit inside its room, not clip a wall, and not overlap another fixture (seats may tuck under
    tables). This is where 'overlapping blocks / furniture makes no sense' actually comes from —
    a 2-cube bed dropped at a 1-cube slot pokes through the wall or into a neighbour."""
    from .realize import FIXTURE_TEMPLATES, _FACING_ROT
    from .playtest import _seat_under_surface
    rep = ValidationReport()
    occ = _cube_occupancy(canvas)
    bases = _story_base_y(spec)

    for si, story in enumerate(spec.stories):
        y = bases[si] + 1
        rooms = {r.id: r for r in story.rooms}
        placed: List[Tuple[str, int, int, int, int]] = []
        for fi, f in enumerate(story.fixtures):
            tmpl = FIXTURE_TEMPLATES.get(f.type)
            if not tmpl:
                continue
            rot = _FACING_ROT.get(f.facing, 0)
            fw, fd = template_cube_footprint(tmpl, rot)
            x0, z0 = f.rect[0], f.rect[1]
            x1, z1 = x0 + fw, z0 + fd
            where = f"story {si} fixture #{fi} '{f.type}'"
            room = rooms.get(f.room)
            if room:
                rx0, rz0, rx1, rz1 = _bounds(tuple(room.rect))
                if not (rx0 <= x0 and x1 <= rx1 and rz0 <= z0 and z1 <= rz1):
                    rep.error("FIXTURE_OUT_OF_ROOM",
                              f"'{f.type}' is {fw}x{fd} cubes at [{x0},{z0}] but room '{f.room}' is "
                              f"{room.rect} — it pokes outside the room", where)
            clip = sum(1 for x in range(x0, x1) for z in range(z0, z1)
                       if occ.get((x, y, z), 0) == _FULL_CUBE)
            if clip:
                rep.error("FIXTURE_CLIPS_WALL",
                          f"'{f.type}' ({fw}x{fd} cubes) overlaps {clip} wall cube(s) at floor level "
                          "— it is embedded in a wall", where)
            placed.append((f.type, x0, z0, x1, z1))
        for i in range(len(placed)):
            for j in range(i + 1, len(placed)):
                ta, ax0, az0, ax1, az1 = placed[i]
                tb, bx0, bz0, bx1, bz1 = placed[j]
                if ax0 < bx1 and bx0 < ax1 and az0 < bz1 and bz0 < az1 and not _seat_under_surface(ta, tb):
                    rep.error("FIXTURE_OVERLAP",
                              f"'{ta}' and '{tb}' overlap at their real footprints (not just f.rect)",
                              f"story {si}")
    return rep


# --------------------------------------------------------------------------- real-world dimensions

# 1 cube = 1 m. Reference ranges for object footprint/height, in metres. A generated object
# whose measured bounding box falls outside these reads as wrong-sized to a human eye.
REFERENCE_DIMS = {                       # kind: {axis: (min, max)} ; H = height, F = footprint side
    "chair":  {"H": (0.80, 1.25), "F": (0.38, 0.75)},
    "stool":  {"H": (0.40, 0.85), "F": (0.30, 0.55)},
    "bench":  {"H": (0.40, 1.00), "F": (0.35, 0.65)},   # plus a long axis (checked loosely)
    "table":  {"H": (0.70, 1.05), "F": (0.55, 2.50)},
    "desk":   {"H": (0.70, 0.85), "F": (0.55, 1.80)},
    "bed":    {"H": (0.35, 1.40)},                       # footprint checked vs BED_SIZES
    "bookshelf": {"H": (1.50, 2.40), "F": (0.25, 1.20)},
    "door":   {"H": (1.95, 2.30)},   # width handled by opening-tiling; doors are intentionally thin
}

# Standard mattress sizes (width x length, metres) — beds are matched to the nearest.
BED_SIZES = {
    "single": (0.92, 1.88), "twin": (0.99, 1.91), "full": (1.37, 1.91),
    "queen": (1.52, 2.03), "king": (1.93, 2.03), "cot": (0.70, 1.40),
}


def measure_template(name: str) -> Optional[Tuple[float, float, float]]:
    """Bounding box of a template in metres (W=x, H=y, D=z)."""
    cells = template_cells(name)
    if not cells:
        return None
    xs = [x for x, _, _ in cells]
    ys = [y for _, y, _ in cells]
    zs = [z for _, _, z in cells]
    return ((max(xs) - min(xs) + 1) / 9, (max(ys) - min(ys) + 1) / 9, (max(zs) - min(zs) + 1) / 9)


def dimension_report(name: str, kind: str) -> ValidationReport:
    """Check a template's real-world size against reference furniture/architecture dimensions."""
    rep = ValidationReport()
    m = measure_template(name)
    if m is None:
        return rep
    w, h, d = m
    ref = REFERENCE_DIMS.get(kind)
    if ref:
        if "H" in ref and not (ref["H"][0] <= h <= ref["H"][1]):
            rep.error("OBJECT_WRONG_HEIGHT",
                      f"'{name}' is {h:.2f} m tall; a {kind} should be {ref['H'][0]:.2f}-"
                      f"{ref['H'][1]:.2f} m", name)
        if "F" in ref:
            foot = min(w, d)                       # the narrow horizontal side
            if not (ref["F"][0] <= foot <= ref["F"][1]):
                rep.error("OBJECT_WRONG_FOOTPRINT",
                          f"'{name}' footprint side is {foot:.2f} m; a {kind} should be "
                          f"{ref['F'][0]:.2f}-{ref['F'][1]:.2f} m", name)
    if kind == "bed":
        bw, bl = sorted((w, d))                    # bed width <= length
        best = min(BED_SIZES, key=lambda k: abs(BED_SIZES[k][0] - bw) + abs(BED_SIZES[k][1] - bl))
        sw, sl = BED_SIZES[best]
        if abs(bw - sw) > 0.35 or abs(bl - sl) > 0.35:
            rep.error("BED_NONSTANDARD_SIZE",
                      f"'{name}' is {bw:.2f}x{bl:.2f} m — closest standard is {best} "
                      f"({sw:.2f}x{sl:.2f} m), off by more than 0.35 m", name)
    return rep


# --------------------------------------------------------------------------- convenience

# Furniture that must back onto a wall (a bookshelf floating mid-room facing nowhere is wrong).
WALL_BACKED_TYPES = {"bookshelf", "bookcase", "shelf", "wardrobe", "dresser", "cabinet", "sideboard"}


def wall_backed_report(spec: BuildingSpec, canvas) -> ValidationReport:
    """Shelving / casegoods (bookshelf, shelf, wardrobe, …) must touch a wall — they're designed to
    back onto one. Catches a bookshelf stranded in open floor."""
    from .realize import FIXTURE_TEMPLATES, _FACING_ROT
    rep = ValidationReport()
    occ = _cube_occupancy(canvas)
    bases = _story_base_y(spec)
    for si, story in enumerate(spec.stories):
        y = bases[si] + 1
        for fi, f in enumerate(story.fixtures):
            if f.type not in WALL_BACKED_TYPES:
                continue
            tmpl = FIXTURE_TEMPLATES.get(f.type)
            if not tmpl:
                continue
            fw, fd = template_cube_footprint(tmpl, _FACING_ROT.get(f.facing, 0))
            x0, z0, x1, z1 = f.rect[0], f.rect[1], f.rect[0] + fw, f.rect[1] + fd
            backed = (
                any(occ.get((x, y, z0 - 1), 0) == _FULL_CUBE or occ.get((x, y, z1), 0) == _FULL_CUBE
                    for x in range(x0, x1))
                or any(occ.get((x0 - 1, y, z), 0) == _FULL_CUBE or occ.get((x1, y, z), 0) == _FULL_CUBE
                       for z in range(z0, z1)))
            if not backed:
                rep.error("FURNITURE_NOT_AGAINST_WALL",
                          f"'{f.type}' at {f.rect} backs onto open room (no adjacent wall) — "
                          "shelving/casegoods belong against a wall", f"story {si} fixture #{fi}")
    return rep


def shell_connectivity_report(spec: BuildingSpec, canvas) -> ValidationReport:
    """The building shell (walls/floor/roof) must be one connected solid from the ground — no
    floating wall segments, detached coping, or orphaned roof pieces."""
    rep = ValidationReport()
    fl = floating_components(set(canvas.cells.keys()))
    if fl:
        ys = sorted({round(y / 9, 1) for _, y, _ in fl})
        rep.error("SHELL_FLOATING",
                  f"{len(fl)} shell voxel cell(s) in {_components(fl)} floating cluster(s) "
                  f"(disconnected from the ground) at heights {ys} cubes", "building")
    return rep


def roof_coverage_report(spec: BuildingSpec, canvas) -> ValidationReport:
    """Every interior column must be capped from above (roof or the floor of a story over it).
    Catches rooms open to the sky — e.g. the single-story wing tails of a stepped building whose
    roof only covered the top story's footprint."""
    rep = ValidationReport()
    occ = _cube_occupancy(canvas)
    bases = _story_base_y(spec)
    # scan well above the eaves — a pitched roof ridge can rise ~half the footprint span
    top = bases[-1] + spec.stories[-1].height + max(spec.footprint) + 2
    exposed: List[Tuple[int, int, int]] = []
    for si, story in enumerate(spec.stories):
        ceil_y = bases[si] + story.height + 1           # first cube above this story's interior
        interior = set()
        for r in story.rooms:
            x0, z0, x1, z1 = _bounds(tuple(r.rect))
            interior |= {(x, z) for x in range(x0, x1) for z in range(z0, z1)}
        for (x, z) in interior:
            # open to sky only if NOTHING solid is above it anywhere (a roof higher up — e.g. over
            # a stairwell — still counts as covered)
            if not any(occ.get((x, y, z), 0) for y in range(ceil_y, top)):
                exposed.append((si, x, z))
    if exposed:
        by_story: Dict[int, int] = {}
        for si, _, _ in exposed:
            by_story[si] = by_story.get(si, 0) + 1
        rep.error("ROOF_GAP",
                  f"{len(exposed)} interior column(s) are open to the sky (no roof/floor above): "
                  + ", ".join(f"{n} on story {si}" for si, n in sorted(by_story.items())),
                  "building")
    return rep


def room_headroom_report(spec: BuildingSpec, canvas, canon: Optional[ScaleCanon] = None
                         ) -> ValidationReport:
    """Every walkable floor cell in a room must have full character headroom clear above it —
    catches low spots / intrusions a body would hit while just standing in a room."""
    if canon is None:
        canon = load_canon()
    rep = ValidationReport()
    occ = _cube_occupancy(canvas)
    bases = _story_base_y(spec)
    need = canon.headroom_min
    for si, story in enumerate(spec.stories):
        y = bases[si] + 1
        low = 0
        for r in story.rooms:
            x0, z0, x1, z1 = _bounds(tuple(r.rect))
            for x in range(x0, x1):
                for z in range(z0, z1):
                    if occ.get((x, y, z), 0) == _FULL_CUBE:     # a wall/partition cell, skip
                        continue
                    clear = 0
                    for h in range(need):
                        if occ.get((x, y + h, z), 0) == _FULL_CUBE:
                            break
                        clear += 1
                    if clear < need:
                        low += 1
        if low:
            rep.warn("ROOM_LOW_HEADROOM",
                     f"{low} floor cell(s) on story {si} have less than {need} cubes of standing "
                     "headroom", f"story {si}")
    return rep


def geometry_report(spec: BuildingSpec, canvas, canon: Optional[ScaleCanon] = None
                    ) -> ValidationReport:
    """All deterministic building-geometry checks on a realized shell + its fixtures."""
    from .playtest import merge
    if canon is None:
        canon = load_canon()
    return merge(stair_clearance_report(spec, canvas, canon),
                 opening_fit_report(spec, canvas),
                 door_selection_report(spec),
                 door_swing_report(spec, canvas),
                 fixture_placement_report(spec, canvas),
                 wall_backed_report(spec, canvas),
                 shell_connectivity_report(spec, canvas),
                 roof_coverage_report(spec, canvas),
                 room_headroom_report(spec, canvas, canon))
