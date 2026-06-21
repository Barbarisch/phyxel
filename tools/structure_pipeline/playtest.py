"""
Structure Pipeline — functional / ergonomic validation pass ("playtest").

The static :mod:`validator` proves a spec is *topologically* legal (rooms tile without
overlap, every room is reachable on the portal graph). This pass asks the next question:
can a real **1.751-cube humanoid actually USE the building**?

It layers three tiers of increasing fidelity:

* **Tier A — analytic ergonomics** (this module, `functional_report`): pure spec+canon math.
  Stairs climbable, doors have swing clearance, rooms are big enough to feel real, fixtures
  fit and don't block the way. Fast, deterministic, and wired into the LLM repair loop so the
  generator self-corrects functional failures the same way it fixes overlaps.
* **Tier B — built-geometry walkable grid** (`walkable_report`): flood-fill a capsule-footprint
  walkability grid derived from the *realized* voxels (+ fixture obstacles) and assert the
  entrance reaches every room/stair. The empirical backstop for whatever Tier A models loosely.
* **Tier C — runtime playtest** (`runtime_playtest`): drive a live engine — spawn the humanoid,
  pathfind to each room, climb the stairs, toggle each door. The gold standard; slow/flaky.

Severity policy (per project decision): genuine *blockers* (unclimbable stairs, a door that
can't open, a room too small to occupy, an unreachable body-path) are **errors** that feed the
repair loop; softer ergonomics (tight-but-passable landing, sliver aspect ratio) are warnings.
"""

from __future__ import annotations

from typing import Dict, List, Optional, Set, Tuple

from .scale import ScaleCanon, load_canon
from .spec import BuildingSpec, Portal, Room, Stair, Story
from .validator import (
    Issue, ValidationReport, EXTERIOR,
    _bounds, _shared_wall, _on_perimeter,
)

Rect = Tuple[int, int, int, int]
Cell = Tuple[int, int]


# --------------------------------------------------------------------------- room ergonomics

# category -> (min floor area in cubes, min short dimension in cubes)
ROOM_MINIMUMS: Dict[str, Tuple[int, int]] = {
    "corridor": (2, 1),   # circulation: long & thin is fine
    "closet":   (2, 1),   # storage/pantry/vault
    "bath":     (2, 1),
    "utility":  (4, 1),
    "bedroom":  (6, 2),
    "kitchen":  (6, 2),
    "study":    (6, 2),
    "hall":     (9, 2),    # living/dining/great/drawing/foyer/nave — public rooms
    "_default": (6, 2),
}

_PURPOSE_KEYWORDS = [
    ("corridor", ("corridor", "hallway", "landing", "passage", "stairwell", "vestibule")),
    ("closet",   ("closet", "pantry", "storage", "store-room", "storeroom", "cellar",
                  "vault", "wardrobe", "larder")),
    ("bath",     ("bath", "privy", "latrine", "washroom", "lavatory")),
    ("bedroom",  ("bed", "sleep", "dorm", "chamber")),
    ("kitchen",  ("kitchen", "scullery", "galley")),
    ("study",    ("study", "library", "office", "den", "workshop", "smithy")),
    ("hall",     ("hall", "living", "dining", "great", "drawing", "parlor", "parlour",
                  "foyer", "entry", "entrance", "nave", "common", "ballroom", "throne",
                  "lounge", "salon", "shop", "storefront", "taproom", "main")),
]


def classify_purpose(purpose: str) -> str:
    """Map a free-text room purpose to an ergonomics category."""
    p = (purpose or "").lower()
    for cat, keys in _PURPOSE_KEYWORDS:
        if any(k in p for k in keys):
            return cat
    return "_default"


# Fixtures you sit at vs. the surfaces you pull up to — a seat tucked under a table legitimately
# overlaps it in plan view, so that pair is exempt from the overlap check.
_SEAT_TYPES = {"chair", "stool", "bench", "pew"}
_SURFACE_TYPES = {"table", "counter", "bar", "desk"}


def _seat_under_surface(ta: str, tb: str) -> bool:
    a, b = (ta or "").lower(), (tb or "").lower()
    return (a in _SEAT_TYPES and b in _SURFACE_TYPES) or (b in _SEAT_TYPES and a in _SURFACE_TYPES)


def _bounds_overlap(a: Tuple[int, int, int, int], b: Tuple[int, int, int, int]) -> bool:
    ax0, az0, ax1, az1 = a
    bx0, bz0, bx1, bz1 = b
    return ax0 < bx1 and bx0 < ax1 and az0 < bz1 and bz0 < az1


# --------------------------------------------------------------------------- swing geometry

def _swing_sides(portal: Portal, rooms: Dict[str, Room], fw: int, fd: int
                 ) -> Optional[List[Tuple[str, Optional[Room], str, int]]]:
    """Resolve a door opening to its wall and the room(s) on each side.

    Returns a list of (axis, room_or_None, side_sign, coord) entries — one per side of the
    opening — or None if the wall can't be resolved. ``side_sign`` is the direction the leaf
    would swing INTO that side ('-'/'+' along ``axis``); ``coord`` is the wall grid line.
    """
    a, b = portal.between
    if EXTERIOR in (a, b):
        px, pz = portal.pos
        if px == 0:
            axis, coord, room_side = "x", 0, "+"
        elif px == fw:
            axis, coord, room_side = "x", fw, "-"
        elif pz == 0:
            axis, coord, room_side = "z", 0, "+"
        elif pz == fd:
            axis, coord, room_side = "z", fd, "-"
        else:
            return None
        rid = next((e for e in (a, b) if e != EXTERIOR), None)
        room = rooms.get(rid) if rid else None
        return [(axis, room, room_side, coord)]

    ra, rb = rooms.get(a), rooms.get(b)
    if not ra or not rb:
        return None
    wall = _shared_wall(ra.rect, rb.rect)
    if wall is None:
        return None
    axis, coord, _, _ = wall
    # which room lies on the '-' side (smaller coord) of the wall line?
    ax0, az0, ax1, az1 = _bounds(ra.rect)
    if axis == "x":
        a_is_minus = ax1 == coord
    else:
        a_is_minus = az1 == coord
    minus_room, plus_room = (ra, rb) if a_is_minus else (rb, ra)
    return [(axis, minus_room, "-", coord), (axis, plus_room, "+", coord)]


def _swing_block(axis: str, coord: int, side_sign: str, p_lo: int, width: int) -> Set[Cell]:
    """The width×width floor cells the leaf sweeps on one side of the opening."""
    cells: Set[Cell] = set()
    along = range(p_lo, p_lo + width)
    if side_sign == "-":
        perp = range(coord - width, coord)
    else:
        perp = range(coord, coord + width)
    for q in perp:
        for r in along:
            cells.add((q, r) if axis == "x" else (r, q))
    return cells


def _rect_contains_cells(rect: Rect, cells: Set[Cell]) -> bool:
    x0, z0, x1, z1 = _bounds(rect)
    return all(x0 <= x < x1 and z0 <= z < z1 for (x, z) in cells)


# --------------------------------------------------------------------------- Tier A

def functional_report(spec: BuildingSpec, canon: Optional[ScaleCanon] = None) -> ValidationReport:
    """Tier A: analytic ergonomic checks on the spec. Returns a ValidationReport."""
    if canon is None:
        canon = load_canon()
    rep = ValidationReport()
    fw, fd = spec.footprint

    for si, story in enumerate(spec.stories):
        where = f"story {si}"
        rooms = {r.id: r for r in story.rooms if r.id}

        # fixtures: in-room, mutual overlap (seats may tuck under surfaces), obstacle map.
        # Clutter sits ON surfaces (floor+2) and flat rugs are walkable — neither is an obstacle
        # or overlaps floor furniture, so they're kept out of the obstacle map / overlap check.
        from .realize import CLUTTER_TYPES as _CLUTTER, FLAT_TYPES as _FLAT
        nonblock = _CLUTTER | _FLAT
        fixt_cells: Set[Cell] = set()
        fbounds: List[Tuple[int, Tuple[int, int, int, int]]] = []
        for fi, f in enumerate(story.fixtures):
            fb = _bounds(tuple(f.rect))
            fx0, fz0, fx1, fz1 = fb
            room = rooms.get(f.room)
            if room and not _rect_contains_cells(room.rect, {(fx0, fz0), (fx1 - 1, fz1 - 1)}):
                rep.error("FIXTURE_OUT_OF_ROOM",
                          f"fixture '{f.type}' {list(f.rect)} is not inside room '{f.room}'",
                          f"{where} fixture #{fi}")
            if f.type in nonblock:
                continue
            fbounds.append((fi, fb))
            for x in range(fx0, fx1):
                for z in range(fz0, fz1):
                    fixt_cells.add((x, z))
        for ii in range(len(fbounds)):
            for jj in range(ii + 1, len(fbounds)):
                (ai, ab), (bi, bb) = fbounds[ii], fbounds[jj]
                if _bounds_overlap(ab, bb) and not _seat_under_surface(
                        story.fixtures[ai].type, story.fixtures[bi].type):
                    rep.error("FIXTURE_OVERLAP",
                              f"fixtures '{story.fixtures[ai].type}' and "
                              f"'{story.fixtures[bi].type}' overlap", f"{where} fixture #{bi}")

        # ---- room ergonomics: big enough to feel real ----
        for room in story.rooms:
            _, _, rw, rd = room.rect
            if rw <= 0 or rd <= 0:
                continue  # validator already errors on degenerate rects
            cat = classify_purpose(room.purpose)
            min_area, min_short = ROOM_MINIMUMS[cat]
            area, short = rw * rd, min(rw, rd)
            rwhere = f"{where} room '{room.id}'"
            if short < min_short:
                rep.error("ROOM_TOO_NARROW",
                          f"{cat} room '{room.id}' is {rw}x{rd}; min short side {min_short} "
                          f"(a person can't move/turn in a {short}-wide {cat})", rwhere)
            if area < min_area:
                rep.error("ROOM_TOO_SMALL",
                          f"{cat} room '{room.id}' floor area {area} < min {min_area} "
                          f"for a usable {cat}", rwhere)
            # sliver: passable but cramped (soft)
            elif short >= min_short and max(rw, rd) >= 6 * short and cat != "corridor":
                rep.warn("ROOM_SLIVER",
                         f"room '{room.id}' is very long & thin ({rw}x{rd}) for a {cat}", rwhere)

        # ---- door swing clearance ----
        for pi, portal in enumerate(story.portals):
            if portal.kind != "door":
                continue  # arches/windows have no leaf
            pwhere = f"{where} door #{pi} {portal.between}"
            sides = _swing_sides(portal, rooms, fw, fd)
            if not sides:
                continue  # geometry errors are the validator's job
            p_lo = portal.pos[1] if sides[0][0] == "x" else portal.pos[0]
            operable = False
            blocked_by_fixture = False
            for axis, room, side_sign, coord in sides:
                if room is None:
                    continue  # exterior side: nothing to swing into
                block = _swing_block(axis, coord, side_sign, p_lo, portal.width)
                if not _rect_contains_cells(room.rect, block):
                    continue  # room too shallow on this side for the leaf
                if block & fixt_cells:
                    blocked_by_fixture = True
                    continue
                operable = True
                break
            if not operable:
                if blocked_by_fixture:
                    rep.error("DOOR_SWING_FIXTURE",
                              "door leaf swing is blocked by a fixture on every side", pwhere)
                else:
                    rep.error("DOOR_NO_SWING_CLEARANCE",
                              f"no side of this door has {portal.width}x{portal.width} clear floor "
                              "for the leaf to open", pwhere)

    # ---- stairs climbable ----
    for si, story in enumerate(spec.stories):
        for sti, stair in enumerate(story.stairs):
            swhere = f"story {si} stair #{sti}"
            if not (0 <= stair.from_story < len(spec.stories)):
                continue  # validator errors on bad story linkage
            climb = spec.stories[stair.from_story].height + 1  # realizer climbs height+1 cubes
            _, _, sw, sd = stair.rect          # run is along +Z (sd); width along +X (sw)
            run = sd
            if run < climb:
                slope = climb / run if run else float("inf")
                rep.error("STAIR_TOO_STEEP",
                          f"stair climbs {climb} cubes but has only {run} cubes of run "
                          f"(slope {slope:.1f}:1 > {canon.stair_max_rise_per_run:.0f}:1 — "
                          "a body can't walk it; deepen the stair along Z)", swhere)
            if sw < canon.capsule_footprint:
                rep.error("STAIR_TOO_NARROW",
                          f"stair is {sw} wide; needs >= {canon.capsule_footprint}", swhere)
            # landing: the room above must extend past the floor-hole so there's floor to step onto
            to_rooms = {r.id: r for r in spec.stories[stair.to_story].rooms} \
                if 0 <= stair.to_story < len(spec.stories) else {}
            sx0, sz0, sx1, sz1 = _bounds(tuple(stair.rect))
            top_room = next((r for r in to_rooms.values()
                             if _bounds(r.rect)[0] <= sx0 and _bounds(r.rect)[2] >= sx1
                             and _bounds(r.rect)[1] <= sz0 and _bounds(r.rect)[3] >= sz1), None)
            if top_room is not None:
                _, _, tx1, tz1 = _bounds(top_room.rect)
                # need at least one standable cube past the hole (room bigger than the stair footprint)
                if tx1 <= sx1 and tz1 <= sz1 and top_room.rect[2] * top_room.rect[3] <= sw * sd:
                    rep.warn("STAIR_TIGHT_LANDING",
                             f"room above the stair is barely larger than the stairwell — "
                             "tight or missing landing", swhere)
    return rep


# --------------------------------------------------------------------------- Tier B

_FULL_CUBE = 729  # micro-cells in a fully solid cube


def _cube_occupancy(canvas) -> Dict[Tuple[int, int, int], int]:
    """Collapse the micro grid to per-cube filled-cell counts (729 = solid wall/floor)."""
    occ: Dict[Tuple[int, int, int], int] = {}
    for (gx, gy, gz) in canvas.cells:
        key = (gx // 9, gy // 9, gz // 9)
        occ[key] = occ.get(key, 0) + 1
    return occ


def _story_base_y(spec: BuildingSpec) -> List[int]:
    ys, b = [], 0
    for s in spec.stories:
        ys.append(b)
        b += s.height + 1  # floor + interior height (matches the realizer)
    return ys


def _entrance_cells(spec: BuildingSpec) -> Set[Cell]:
    """The (x,z) doorway cells of each passable exterior portal on story 0."""
    fw, fd = spec.footprint
    cells: Set[Cell] = set()
    for p in spec.stories[0].portals:
        if p.kind not in ("door", "arch") or EXTERIOR not in p.between:
            continue
        px, pz = p.pos
        if px == 0 or px == fw:
            wx = 0 if px == 0 else fw - 1
            cells.update((wx, z) for z in range(pz, pz + p.width))
        elif pz == 0 or pz == fd:
            wz = 0 if pz == 0 else fd - 1
            cells.update((x, wz) for x in range(px, px + p.width))
    return cells


def _stair_climbable(spec: BuildingSpec, stair: Stair) -> bool:
    if not (0 <= stair.from_story < len(spec.stories)):
        return False
    climb = spec.stories[stair.from_story].height + 1
    return stair.rect[3] >= climb and stair.rect[2] >= 1


def walkable_report(spec: BuildingSpec, canvas, canon: Optional[ScaleCanon] = None
                    ) -> ValidationReport:
    """Tier B: per-floor 2D flood through doorways (fixtures are obstacles), with climbable
    stairs as explicit floor-to-floor connectors. Asserts a body can actually reach every room
    from the entrance — catches sealed rooms, a fixture across the only doorway, a wing walled
    off by a staircase, and floors orphaned by an unclimbable/disconnected stair.

    Vertical traversal isn't flooded through voxels (a 45-degree stair cube reads as solid at
    cube resolution); instead a stair links floors when Tier A says it's climbable and its base
    is reachable. Same-floor circulation IS flooded over the realized walls/doorways."""
    if canon is None:
        canon = load_canon()
    rep = ValidationReport()
    occ = _cube_occupancy(canvas)
    bases = _story_base_y(spec)

    occupied: List[Set[Cell]] = []     # union of room footprints, per story
    fixt: List[Set[Cell]] = []         # fixture-blocked cells, per story
    stairfoot: List[Set[Cell]] = [set() for _ in spec.stories]  # stair connector cells, per story
    for si, story in enumerate(spec.stories):
        occ_s: Set[Cell] = set()
        for r in story.rooms:
            x0, z0, x1, z1 = _bounds(tuple(r.rect))
            occ_s.update((x, z) for x in range(x0, x1) for z in range(z0, z1))
        occupied.append(occ_s)
        fx: Set[Cell] = set()
        for f in story.fixtures:
            fx0, fz0, fx1, fz1 = _bounds(tuple(f.rect))
            fx.update((x, z) for x in range(fx0, fx1) for z in range(fz0, fz1))
        fixt.append(fx)
    for story in spec.stories:
        for st in story.stairs:
            sx0, sz0, sx1, sz1 = _bounds(tuple(st.rect))
            cells = {(x, z) for x in range(sx0, sx1) for z in range(sz0, sz1)}
            for s in (st.from_story, st.to_story):
                if 0 <= s < len(stairfoot):
                    stairfoot[s] |= cells

    def walkable(si: int, x: int, z: int) -> bool:
        if (x, z) not in occupied[si]:
            return False
        if (x, z) in stairfoot[si]:        # stair connectors are always traversable
            return True
        if occ.get((x, bases[si] + 1, z), 0) == _FULL_CUBE:  # a wall (carved doorways aren't full)
            return False
        return (x, z) not in fixt[si]

    if not _entrance_cells(spec):
        rep.error("NO_REACHABLE_ENTRANCE", "no passable exterior door to enter by", "building")
        return rep

    reachable: List[Set[Cell]] = [set() for _ in spec.stories]
    pending: Dict[int, Set[Cell]] = {0: set(_entrance_cells(spec))}
    for si, story in enumerate(spec.stories):
        seeds = {c for c in pending.get(si, set()) if walkable(si, *c)}
        vis = set(seeds)
        stack = list(seeds)
        while stack:
            x, z = stack.pop()
            for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nb = (x + dx, z + dz)
                if nb not in vis and walkable(si, *nb):
                    vis.add(nb)
                    stack.append(nb)
        reachable[si] = vis
        for st in story.stairs:           # climb to the next floor if the base is reached
            if st.from_story != si or not _stair_climbable(spec, st):
                continue
            sx0, sz0, sx1, sz1 = _bounds(tuple(st.rect))
            foot = {(x, z) for x in range(sx0, sx1) for z in range(sz0, sz1)}
            if foot & vis:
                pending.setdefault(st.to_story, set()).update(foot)

    neigh = ((1, 0), (-1, 0), (0, 1), (0, -1))
    for si, story in enumerate(spec.stories):
        for room in story.rooms:
            x0, z0, x1, z1 = _bounds(tuple(room.rect))
            cells = {(x, z) for x in range(x0, x1) for z in range(z0, z1) if walkable(si, x, z)}
            if not cells:
                continue
            reached = {c for c in cells if c in reachable[si]}
            # Genuinely "in" the room means standing on an interior cell that has a reached
            # in-room neighbour — not merely on a door threshold shared with the wall line.
            inside = any((c[0] + dx, c[1] + dz) in reached for c in reached for dx, dz in neigh)
            if len(cells) == 1:
                inside = bool(reached)
            if not inside:
                rep.error("ROOM_UNREACHABLE_BODY",
                          f"a character can't physically walk into room '{room.id}' from the "
                          "entrance (blocked doorway, fixture, or disconnected stairs)",
                          f"story {si}")
    return rep


# --------------------------------------------------------------------------- merge / convenience

def merge(*reports: ValidationReport) -> ValidationReport:
    out = ValidationReport()
    for r in reports:
        out.issues.extend(r.issues)
    return out


def full_validate(spec: BuildingSpec, canon: Optional[ScaleCanon] = None) -> ValidationReport:
    """Topological validation + Tier A ergonomics + Tier B walkable-grid, merged.

    This is what the LLM repair loop runs, so the generator self-corrects topological,
    ergonomic AND body-traversal failures (a fixture in a doorway, a sealed wing)."""
    from .validator import validate
    if canon is None:
        canon = load_canon()
    reports = [validate(spec, canon), functional_report(spec, canon)]
    try:  # Tier B + geometry need the realized voxels; skip if the spec is too broken to build
        from .realize import build_shell
        from .geometry import geometry_report
        canvas = build_shell(spec)
        reports.append(walkable_report(spec, canvas, canon))
        reports.append(geometry_report(spec, canvas, canon))   # stair headroom + door-opening fit
    except Exception:
        pass  # topological / Tier-A errors already describe the breakage
    return merge(*reports)


def full_validate_dict(d, canon: Optional[ScaleCanon] = None) -> ValidationReport:
    return full_validate(BuildingSpec.from_dict(d), canon)


# --------------------------------------------------------------------------- Tier C (runtime)

ENGINE = "http://localhost:8090"


def _rt_get(engine: str, path: str) -> dict:
    import json as _j
    import urllib.request
    try:
        with urllib.request.urlopen(engine + path, timeout=15) as r:
            return _j.loads(r.read().decode())
    except Exception as e:
        return {"error": str(e)}


def _rt_post(engine: str, path: str, body: dict) -> dict:
    import json as _j
    import urllib.request
    req = urllib.request.Request(engine + path, data=_j.dumps(body).encode(),
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=15) as r:
            return _j.loads(r.read().decode())
    except Exception as e:
        return {"error": str(e)}


def runtime_playtest(spec: BuildingSpec, position, engine: str = ENGINE) -> dict:
    """Tier C: drive a LIVE engine to confirm the built structure works. Operates every
    registered door (open+close) and asks the engine's NavGrid A* to path from the entrance
    to each ground-floor room. Returns a result dict ({'ok', 'doors', 'navigation', 'notes'}).

    The NavGrid is 2D, so this verifies same-floor navigation + door operation at runtime;
    vertical/stair traversal stays with the Tier B analysis. Requires the engine running with
    the structure already built (realize --build) AND its physics/navgrid reflecting it."""
    px, py, pz = position
    out = {"ok": True, "doors": [], "navigation": [], "notes": []}

    doors = _rt_get(engine, "/api/doors")
    if "error" in doors:
        out["notes"].append(f"could not list doors: {doors['error']}")
        out["ok"] = False
    for d in doors.get("doors", []):
        oid = d.get("placed_object_id") or d.get("id")
        op = _rt_post(engine, "/api/door/open", {"placed_object_id": oid})
        cl = _rt_post(engine, "/api/door/close", {"placed_object_id": oid})
        ok = "error" not in op and "error" not in cl
        out["doors"].append({"id": oid, "operable": ok})
        out["ok"] = out["ok"] and ok

    # Navigation is ADVISORY: the engine's NavGrid isn't rebuilt after runtime world edits /
    # structure spawns (see docs/StructurePipelineGaps.md), so it may not reflect a freshly
    # built building. Tier B is the authoritative navigability check; this is a live cross-check.
    ent = sorted(_entrance_cells(spec))
    if ent:
        ex, ez = ent[0]
        ewx, ewz = px + ex, pz + ez
        for room in spec.stories[0].rooms:
            rx = px + room.rect[0] + room.rect[2] // 2
            rz = pz + room.rect[1] + room.rect[3] // 2
            res = _rt_get(engine, f"/api/navgrid/path?x1={ewx}&z1={ewz}&x2={rx}&z2={rz}")
            path = res.get("path") or res.get("waypoints") or []
            out["navigation"].append({"room": room.id, "reachable": bool(path), "len": len(path)})
    nav_ok = sum(n["reachable"] for n in out["navigation"])
    if out["navigation"] and nav_ok == 0:
        out["notes"].append("navgrid returned no paths — the engine NavGrid likely doesn't "
                            "reflect the runtime-built structure (advisory; trust Tier B)")
    return out
