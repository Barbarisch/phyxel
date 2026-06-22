"""
Structure Pipeline — static validator (P0).

Validates a ``BuildingSpec`` with no engine and no voxels: geometry (bounds, overlap,
wall adjacency), function (entrance + reachability graph, stair story-linkage), and
scale (clearances against the character canon). Catches a bad spec before a single voxel
is placed.

Connectivity convention: only ``door``/``arch`` portals and stairs are passable; windows
are not. Every room on every story must be reachable from ``"exterior"``.

Portal ``pos`` is the **min corner of the opening along its wall** (corner semantics,
matching the rect min-corner convention used elsewhere in the pipeline).
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

from .scale import ScaleCanon, load_canon
from .spec import (
    BuildingSpec, Portal, Room, Stair, Story,
    BUILDING_FUNCTIONS, PORTAL_KINDS, STAIR_KINDS, PASSABLE_PORTAL_KINDS,
)

EXTERIOR = "exterior"


@dataclass
class Issue:
    severity: str   # "error" | "warning"
    code: str
    message: str
    where: str = ""

    def __str__(self) -> str:
        loc = f" [{self.where}]" if self.where else ""
        return f"{self.severity.upper()} {self.code}: {self.message}{loc}"


@dataclass
class ValidationReport:
    issues: List[Issue] = field(default_factory=list)

    def error(self, code: str, message: str, where: str = "") -> None:
        self.issues.append(Issue("error", code, message, where))

    def warn(self, code: str, message: str, where: str = "") -> None:
        self.issues.append(Issue("warning", code, message, where))

    @property
    def errors(self) -> List[Issue]:
        return [i for i in self.issues if i.severity == "error"]

    @property
    def warnings(self) -> List[Issue]:
        return [i for i in self.issues if i.severity == "warning"]

    @property
    def ok(self) -> bool:
        return not self.errors

    @property
    def codes(self) -> List[str]:
        return [i.code for i in self.issues]

    def summary(self) -> str:
        head = "OK" if self.ok else "INVALID"
        return (f"{head} — {len(self.errors)} error(s), {len(self.warnings)} warning(s)\n"
                + "\n".join(f"  {i}" for i in self.issues))


# --------------------------------------------------------------------------- geometry helpers

Rect = Tuple[int, int, int, int]  # (x, z, w, d)


def _bounds(r: Rect) -> Tuple[int, int, int, int]:
    x, z, w, d = r
    return x, z, x + w, z + d  # x_min, z_min, x_max, z_max


def _point_in_rect(px: float, pz: float, r: Rect) -> bool:
    x0, z0, x1, z1 = _bounds(r)
    return x0 <= px < x1 and z0 <= pz < z1


def _rect_in_footprint(r: Rect, w: int, d: int) -> bool:
    x0, z0, x1, z1 = _bounds(r)
    return x0 >= 0 and z0 >= 0 and x1 <= w and z1 <= d


def _rects_overlap(a: Rect, b: Rect) -> bool:
    ax0, az0, ax1, az1 = _bounds(a)
    bx0, bz0, bx1, bz1 = _bounds(b)
    return ax0 < bx1 and bx0 < ax1 and az0 < bz1 and bz0 < az1  # interior overlap (touching is OK)


def _shared_wall(a: Rect, b: Rect) -> Optional[Tuple[str, int, int, int]]:
    """If a and b are edge-adjacent, return (axis, coord, lo, hi).

    axis 'x': wall is the vertical line x=coord, running along z over [lo, hi].
    axis 'z': wall is the horizontal line z=coord, running along x over [lo, hi].
    Returns None if they only touch at a corner or not at all.
    """
    ax0, az0, ax1, az1 = _bounds(a)
    bx0, bz0, bx1, bz1 = _bounds(b)

    # Vertical shared wall (constant x): right of a meets left of b (or vice versa).
    if ax1 == bx0 or bx1 == ax0:
        coord = ax1 if ax1 == bx0 else ax0
        lo, hi = max(az0, bz0), min(az1, bz1)
        if hi - lo > 0:
            return ("x", coord, lo, hi)

    # Horizontal shared wall (constant z).
    if az1 == bz0 or bz1 == az0:
        coord = az1 if az1 == bz0 else az0
        lo, hi = max(ax0, bx0), min(ax1, bx1)
        if hi - lo > 0:
            return ("z", coord, lo, hi)

    return None


def _opening_on_wall(p: Portal, wall: Tuple[str, int, int, int]) -> bool:
    """True if portal p (corner pos + width) lies on the given wall segment."""
    axis, coord, lo, hi = wall
    px, pz = p.pos
    if axis == "x":
        return px == coord and lo <= pz and pz + p.width <= hi
    return pz == coord and lo <= px and px + p.width <= hi


def _on_perimeter(p: Portal, w: int, d: int) -> bool:
    px, pz = p.pos
    return px == 0 or px == w or pz == 0 or pz == d


# --------------------------------------------------------------------------- validator

def validate(spec: BuildingSpec, canon: Optional[ScaleCanon] = None) -> ValidationReport:
    """Run all static checks. ``canon`` defaults to the on-disk humanoid_normal canon."""
    if canon is None:
        canon = load_canon()
    rep = ValidationReport()

    # ---- building-level ----
    if spec.function not in BUILDING_FUNCTIONS:
        rep.warn("UNKNOWN_FUNCTION", f"function '{spec.function}' is not a known type", "building")

    fw, fd = spec.footprint
    if fw <= 0 or fd <= 0:
        rep.error("BAD_FOOTPRINT", f"footprint must be positive, got {spec.footprint}", "building")

    if not spec.stories:
        rep.error("NO_STORIES", "building has no stories", "building")
        return rep  # nothing more to check

    # Connectivity graph nodes/edges accumulate across stories.
    adj: Dict[Any, set] = {EXTERIOR: set()}
    has_entrance = False

    # Per-story room lookup, used by stair linkage afterwards.
    story_rooms: List[Dict[str, Room]] = []

    for si, story in enumerate(spec.stories):
        where = f"story {si}"
        rooms_by_id: Dict[str, Room] = {}

        # ---- ceiling / scale ----
        if story.height < canon.ceiling_min:
            rep.error("CEILING_TOO_LOW",
                      f"interior height {story.height} < min {canon.ceiling_min} "
                      f"(character {canon.character_height:.3f} cubes won't fit)", where)
        elif story.height < canon.ceiling_comfortable:
            rep.warn("CEILING_TIGHT",
                     f"interior height {story.height} below comfortable {canon.ceiling_comfortable}", where)

        if not story.rooms:
            rep.warn("STORY_EMPTY", "story has no rooms", where)

        # ---- rooms ----
        for room in story.rooms:
            rwhere = f"{where} room '{room.id}'"
            if not room.id:
                rep.error("ROOM_NO_ID", "room missing id", rwhere)
            if room.id in rooms_by_id:
                rep.error("DUPLICATE_ROOM_ID", f"duplicate room id '{room.id}'", rwhere)
            else:
                rooms_by_id[room.id] = room
                adj.setdefault((si, room.id), set())

            _, _, rw, rd = room.rect
            if rw <= 0 or rd <= 0:
                rep.error("ROOM_DEGENERATE", f"room rect must be positive, got {room.rect}", rwhere)
            elif not _rect_in_footprint(room.rect, fw, fd):
                rep.error("ROOM_OUT_OF_BOUNDS",
                          f"room rect {room.rect} exceeds footprint {spec.footprint}", rwhere)

        # pairwise overlap
        rlist = list(rooms_by_id.values())
        for i in range(len(rlist)):
            for j in range(i + 1, len(rlist)):
                if _rects_overlap(rlist[i].rect, rlist[j].rect):
                    rep.error("ROOM_OVERLAP",
                              f"rooms '{rlist[i].id}' and '{rlist[j].id}' overlap", where)

        story_rooms.append(rooms_by_id)

        # ---- portals ----
        for pi, portal in enumerate(story.portals):
            pwhere = f"{where} portal #{pi} {portal.between}"
            if portal.kind not in PORTAL_KINDS:
                rep.error("UNKNOWN_PORTAL_KIND", f"unknown portal kind '{portal.kind}'", pwhere)
                continue
            if portal.door is not None and portal.kind != "door":
                rep.warn("DOOR_ON_NON_DOOR", f"door data on a '{portal.kind}' portal is ignored", pwhere)

            passable = portal.kind in PASSABLE_PORTAL_KINDS
            if passable:
                if portal.width < canon.door_width_min:
                    rep.error("DOOR_TOO_NARROW",
                              f"width {portal.width} < min {canon.door_width_min}", pwhere)
                if portal.height < canon.door_clear_min:
                    rep.error("DOOR_TOO_SHORT",
                              f"height {portal.height} < min {canon.door_clear_min} "
                              f"(character won't fit)", pwhere)
            if portal.height > story.height:
                rep.error("PORTAL_TALLER_THAN_ROOM",
                          f"opening height {portal.height} > interior height {story.height}", pwhere)

            # endpoint resolution
            a, b = portal.between
            is_ext = EXTERIOR in (a, b)
            room_ids = [e for e in (a, b) if e != EXTERIOR]

            if is_ext and len(room_ids) == 0:
                rep.error("PORTAL_NO_ROOM", "portal connects exterior to exterior", pwhere)
                continue

            missing = [rid for rid in room_ids if rid not in rooms_by_id]
            if missing:
                rep.error("PORTAL_BAD_ROOM", f"unknown room(s) {missing}", pwhere)
                continue

            if is_ext:
                room = rooms_by_id[room_ids[0]]
                if not _on_perimeter(portal, fw, fd):
                    rep.warn("PORTAL_NOT_ON_PERIMETER",
                             f"exterior portal pos {list(portal.pos)} is not on the building perimeter", pwhere)
                if passable:
                    has_entrance = True
                    adj[EXTERIOR].add((si, room.id))
                    adj[(si, room.id)].add(EXTERIOR)
            else:
                ra, rb = rooms_by_id[room_ids[0]], rooms_by_id[room_ids[1]]
                wall = _shared_wall(ra.rect, rb.rect)
                geom_ok = True
                if wall is None:
                    rep.error("ROOMS_NOT_ADJACENT",
                              f"rooms '{ra.id}' and '{rb.id}' do not share a wall", pwhere)
                    geom_ok = False
                elif not _opening_on_wall(portal, wall):
                    rep.error("PORTAL_NOT_ON_WALL",
                              f"opening at {list(portal.pos)} w{portal.width} not on shared wall {wall}", pwhere)
                    geom_ok = False
                # Only a geometrically valid opening grants passage in the reachability graph.
                if passable and geom_ok:
                    adj[(si, ra.id)].add((si, rb.id))
                    adj[(si, rb.id)].add((si, ra.id))

    # ---- stairs (story linkage + connectivity edges) ----
    for si, story in enumerate(spec.stories):
        for sti, stair in enumerate(story.stairs):
            swhere = f"story {si} stair #{sti}"
            if stair.kind not in STAIR_KINDS:
                rep.warn("UNKNOWN_STAIR_KIND", f"unknown stair kind '{stair.kind}'", swhere)
            if not (0 <= stair.from_story < len(spec.stories)) or not (0 <= stair.to_story < len(spec.stories)):
                rep.error("STAIR_BAD_STORY",
                          f"stair links invalid stories {stair.from_story}->{stair.to_story}", swhere)
                continue
            if abs(stair.to_story - stair.from_story) != 1:
                rep.warn("STAIR_NON_CONSECUTIVE",
                         f"stair links non-adjacent stories {stair.from_story}->{stair.to_story}", swhere)
            if not _rect_in_footprint(stair.rect, fw, fd):
                rep.error("STAIR_OUT_OF_BOUNDS",
                          f"stair footprint {stair.rect} exceeds building {spec.footprint}", swhere)

            # connectivity: which room does the stair land in on each story?
            cx = stair.rect[0] + stair.rect[2] / 2.0
            cz = stair.rect[1] + stair.rect[3] / 2.0
            from_room = next((r.id for r in spec.stories[stair.from_story].rooms
                              if _point_in_rect(cx, cz, r.rect)), None)
            to_room = next((r.id for r in spec.stories[stair.to_story].rooms
                            if _point_in_rect(cx, cz, r.rect)), None)
            if from_room is None or to_room is None:
                rep.warn("STAIR_NOT_IN_ROOM",
                         "stair footprint center is not inside a room on both stories", swhere)
            else:
                na, nb = (stair.from_story, from_room), (stair.to_story, to_room)
                adj.setdefault(na, set()).add(nb)
                adj.setdefault(nb, set()).add(na)

    # ---- entrance + reachability ----
    if not has_entrance:
        rep.error("NO_ENTRANCE", "no passable exterior portal (door/arch) — building has no entrance", "building")

    visited = set()
    queue = deque([EXTERIOR])
    visited.add(EXTERIOR)
    while queue:
        node = queue.popleft()
        for nxt in adj.get(node, ()):
            if nxt not in visited:
                visited.add(nxt)
                queue.append(nxt)

    for si, rooms_by_id in enumerate(story_rooms):
        for rid in rooms_by_id:
            if (si, rid) not in visited:
                rep.error("ROOM_UNREACHABLE",
                          f"room '{rid}' on story {si} is not reachable from the entrance",
                          f"story {si}")

    return rep


# --------------------------------------------------------------------------- convenience

def validate_dict(d: Dict[str, Any], canon: Optional[ScaleCanon] = None) -> ValidationReport:
    return validate(BuildingSpec.from_dict(d), canon)


def validate_file(path: Path, canon: Optional[ScaleCanon] = None) -> ValidationReport:
    import json
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    return validate_dict(data, canon)
