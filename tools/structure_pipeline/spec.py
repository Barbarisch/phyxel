"""
Structure Pipeline — spec schema (P0).

The resolution-independent, *semantic* representation that the LLM authors and the
deterministic realizer consumes. Buildings, furniture, and items share a ``ScaledSpec``
base; P0 implements ``BuildingSpec`` in full. Every dimension is in the shared cube unit
(1 cube ~= 1 metre); the humanoid character (see ``scale.py``) is the ruler.

Pure data + (de)serialization only. Validation lives in ``validator.py``.

Coordinate convention (matches StructureGenerator local space):
  - A spec is authored in its own local frame; world placement/rotation happens at realize time.
  - X = width, Y = up, Z = depth.
  - Room/fixture/stair ``rect`` = ``[x, z, w, d]`` (an XZ footprint; min corner + size).
  - Portal ``pos`` = ``[x, z]`` — the min corner of the opening along its wall (the opening
    runs ``width`` cubes in the wall's direction from this corner).
"""

from __future__ import annotations

from dataclasses import dataclass, field, asdict
from typing import Any, Dict, List, Optional, Tuple

# Known building functions (warning-only if unknown — the realizer/room-program library is the
# authority; this is just a typo guard).
BUILDING_FUNCTIONS = {"house", "shop", "church", "tavern", "tower", "stadium", "generic"}
PORTAL_KINDS = {"door", "arch", "window"}
STAIR_KINDS = {"straight", "spiral"}
# Portal kinds a character can actually walk through (used by the connectivity graph).
PASSABLE_PORTAL_KINDS = {"door", "arch"}


def _as_int_tuple(value: Any, n: int) -> Tuple[int, ...]:
    """Coerce a list/tuple to an n-int tuple (lenient; validation reports bad shapes)."""
    if value is None:
        return tuple([0] * n)
    seq = list(value)
    seq = (seq + [0] * n)[:n]
    return tuple(int(round(float(v))) for v in seq)


@dataclass
class Door:
    """Functional door leaf wired to DoorManager at realize time."""
    lockable: bool = False
    key: str = ""            # required item id to unlock; empty = no key
    swing: float = 90.0      # open angle in degrees

    @staticmethod
    def from_dict(d: Optional[Dict[str, Any]]) -> Optional["Door"]:
        if not d:
            return None
        return Door(
            lockable=bool(d.get("lockable", False)),
            key=str(d.get("key", "")),
            swing=float(d.get("swing", 90.0)),
        )

    def to_dict(self) -> Dict[str, Any]:
        return {"lockable": self.lockable, "key": self.key, "swing": self.swing}


@dataclass
class Portal:
    """An opening between two spaces. Endpoints are room ids or the literal ``"exterior"``."""
    between: Tuple[str, str]
    pos: Tuple[int, int]          # [x, z] min corner of the opening along its wall
    width: int = 2
    height: int = 3
    kind: str = "door"            # door | arch | window
    door: Optional[Door] = None   # only meaningful when kind == "door"

    @staticmethod
    def from_dict(d: Dict[str, Any]) -> "Portal":
        between = d.get("between", ["exterior", ""])
        a, b = (list(between) + ["", ""])[:2]
        return Portal(
            between=(str(a), str(b)),
            pos=_as_int_tuple(d.get("pos"), 2),  # type: ignore[arg-type]
            width=int(d.get("width", 2)),
            height=int(d.get("height", 3)),
            kind=str(d.get("kind", "door")),
            door=Door.from_dict(d.get("door")),
        )

    def to_dict(self) -> Dict[str, Any]:
        out: Dict[str, Any] = {
            "between": list(self.between),
            "pos": list(self.pos),
            "width": self.width,
            "height": self.height,
            "kind": self.kind,
        }
        if self.door is not None:
            out["door"] = self.door.to_dict()
        return out


@dataclass
class Room:
    id: str
    rect: Tuple[int, int, int, int]   # [x, z, w, d]
    purpose: str = "generic"
    floor_mat: str = ""

    @staticmethod
    def from_dict(d: Dict[str, Any]) -> "Room":
        return Room(
            id=str(d.get("id", "")),
            rect=_as_int_tuple(d.get("rect"), 4),  # type: ignore[arg-type]
            purpose=str(d.get("purpose", "generic")),
            floor_mat=str(d.get("floor_mat", "")),
        )

    def to_dict(self) -> Dict[str, Any]:
        return {"id": self.id, "rect": list(self.rect),
                "purpose": self.purpose, "floor_mat": self.floor_mat}


@dataclass
class Stair:
    """Connects ``from_story`` to ``to_story`` (expected consecutive). Footprint = ``rect``."""
    from_story: int
    to_story: int
    rect: Tuple[int, int, int, int]   # [x, z, w, d] footprint on from_story
    kind: str = "straight"

    @staticmethod
    def from_dict(d: Dict[str, Any]) -> "Stair":
        return Stair(
            from_story=int(d.get("from_story", 0)),
            to_story=int(d.get("to_story", 1)),
            rect=_as_int_tuple(d.get("rect"), 4),  # type: ignore[arg-type]
            kind=str(d.get("kind", "straight")),
        )

    def to_dict(self) -> Dict[str, Any]:
        return {"from_story": self.from_story, "to_story": self.to_story,
                "rect": list(self.rect), "kind": self.kind}


@dataclass
class Fixture:
    """A furniture/feature instance inside a room (the building↔furniture seam)."""
    type: str                          # altar | counter | pew | bar | bed | table | chair | ...
    rect: Tuple[int, int, int, int]    # [x, z, w, d] footprint
    facing: str = "south"
    room: str = ""                     # owning room id (optional)

    @staticmethod
    def from_dict(d: Dict[str, Any]) -> "Fixture":
        return Fixture(
            type=str(d.get("type", "")),
            rect=_as_int_tuple(d.get("rect"), 4),  # type: ignore[arg-type]
            facing=str(d.get("facing", "south")),
            room=str(d.get("room", "")),
        )

    def to_dict(self) -> Dict[str, Any]:
        return {"type": self.type, "rect": list(self.rect),
                "facing": self.facing, "room": self.room}


@dataclass
class Story:
    height: int = 4                      # interior clear height in cubes (floor-to-ceiling)
    rooms: List[Room] = field(default_factory=list)
    portals: List[Portal] = field(default_factory=list)
    stairs: List[Stair] = field(default_factory=list)
    fixtures: List[Fixture] = field(default_factory=list)

    @staticmethod
    def from_dict(d: Dict[str, Any]) -> "Story":
        return Story(
            height=int(d.get("height", 4)),
            rooms=[Room.from_dict(r) for r in d.get("rooms", [])],
            portals=[Portal.from_dict(p) for p in d.get("portals", [])],
            stairs=[Stair.from_dict(s) for s in d.get("stairs", [])],
            fixtures=[Fixture.from_dict(f) for f in d.get("fixtures", [])],
        )

    def to_dict(self) -> Dict[str, Any]:
        return {
            "height": self.height,
            "rooms": [r.to_dict() for r in self.rooms],
            "portals": [p.to_dict() for p in self.portals],
            "stairs": [s.to_dict() for s in self.stairs],
            "fixtures": [f.to_dict() for f in self.fixtures],
        }


@dataclass
class ScaledSpec:
    """Common base for buildings, furniture, and items: name, style, palette, tier."""
    kind: str = "building"               # building | furniture | item
    name: str = ""
    style: str = ""
    palette: Dict[str, str] = field(default_factory=dict)


@dataclass
class BuildingSpec(ScaledSpec):
    function: str = "house"
    footprint: Tuple[int, int] = (8, 10)   # [w, d]
    stories: List[Story] = field(default_factory=list)
    roof: Dict[str, Any] = field(default_factory=dict)

    @staticmethod
    def from_dict(d: Dict[str, Any]) -> "BuildingSpec":
        return BuildingSpec(
            kind=str(d.get("kind", "building")),
            name=str(d.get("name", "")),
            style=str(d.get("style", "")),
            palette=dict(d.get("palette", {})),
            function=str(d.get("function", "house")),
            footprint=_as_int_tuple(d.get("footprint"), 2),  # type: ignore[arg-type]
            stories=[Story.from_dict(s) for s in d.get("stories", [])],
            roof=dict(d.get("roof", {})),
        )

    def to_dict(self) -> Dict[str, Any]:
        return {
            "kind": self.kind,
            "name": self.name,
            "style": self.style,
            "palette": dict(self.palette),
            "function": self.function,
            "footprint": list(self.footprint),
            "stories": [s.to_dict() for s in self.stories],
            "roof": dict(self.roof),
        }
