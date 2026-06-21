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
    from .realize import door_leaves_for_width
    rep = ValidationReport()
    occ = _cube_occupancy(canvas)
    bases = _story_base_y(spec)
    fw, fd = spec.footprint
    leaf_h = DOOR_LEAVES["door_wood"][1]

    for si, story in enumerate(spec.stories):
        rooms = {r.id: r for r in story.rooms}
        for pi, p in enumerate(story.portals):
            if p.kind != "door":
                continue
            where = f"story {si} door #{pi} {p.between}"
            covered = sum(lw for _, _, lw in door_leaves_for_width(p.width))
            if covered < p.width:
                rep.error("DOOR_OPENING_UNFILLED",
                          f"opening is {p.width} wide but door leaves only cover {covered} — "
                          f"a {p.width - covered}-cube gap will be left open", where)
            # the opening must actually be carved (air) where the door hangs
            sides = _swing_sides(p, rooms, fw, fd)
            if not sides:
                continue
            axis, _, _, coord = sides[0]
            y = bases[si] + 1
            px, pz = p.pos
            if axis == "x":
                opening = [(coord, y + dy, z) for z in range(pz, pz + p.width) for dy in range(leaf_h)]
            else:
                opening = [(x, y + dy, coord) for x in range(px, px + p.width) for dy in range(leaf_h)]
            solid = [c for c in opening if occ.get(c, 0) == _FULL_CUBE]
            if solid:
                rep.error("DOOR_EMBEDDED_IN_WALL",
                          f"{len(solid)}/{len(opening)} of the door's opening cells are solid wall "
                          "(the leaf is buried, not in a carved opening)", where)
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

def geometry_report(spec: BuildingSpec, canvas, canon: Optional[ScaleCanon] = None
                    ) -> ValidationReport:
    """All building-geometry checks (stairs + door openings) on a realized shell."""
    from .playtest import merge
    if canon is None:
        canon = load_canon()
    return merge(stair_clearance_report(spec, canvas, canon),
                 opening_fit_report(spec, canvas))
