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

_REPO = Path(__file__).resolve().parents[2]
TEMPLATES_DIR = _REPO / "resources" / "templates"
ENGINE = "http://localhost:8090"
DOOR_LEAF_H = 2

# fixture type -> subcube furniture template (matches the engine-side map)
FIXTURE_TEMPLATES = {
    "table": "table_wood", "chair": "chair_wood", "stool": "stool", "bed": "bed_single",
    "counter": "tavern_bar", "bar": "tavern_bar", "altar": "altar",
    "pew": "bench_wood", "bench": "bench_wood", "barrel": "barrel",
    "bookshelf": "bookshelf", "shelf": "bookshelf",
}
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
    for story in spec.stories:
        h = story.height
        c.fill_cube_box(0, baseY, 0, W, 1, D, floor)            # floor slab (cubes)

        rooms = {r.id: tuple(r.rect) for r in story.rooms}
        wallset = set()
        for y in range(baseY + 1, baseY + h + 1):
            for x in range(W):
                wallset.add((x, y, 0)); wallset.add((x, y, D - 1))
            for z in range(D):
                wallset.add((0, y, z)); wallset.add((W - 1, y, z))
        ids = list(rooms)
        for i in range(len(ids)):
            for j in range(i + 1, len(ids)):
                sw = _shared_wall(rooms[ids[i]], rooms[ids[j]])
                if not sw:
                    continue
                ax, coord, lo, hi = sw
                for y in range(baseY + 1, baseY + h + 1):
                    for t in range(lo, hi):
                        wallset.add((coord, y, t) if ax == "x" else (t, y, coord))
        for (x, y, z) in wallset:
            c.add_cube(x, y, z, wall)

        # openings: exterior windows/doors get framed reveals; interior + arches are carved.
        for p in story.portals:
            res = _resolve_portal(p, rooms, W, D)
            if not res:
                continue
            ax, coord, normal = res
            ph = DOOR_LEAF_H if p.kind == "door" else min(p.height, h)
            px, pz = p.pos
            if ax == "z":
                ox, oy, oz, ow, oh, od = px, baseY + 1, coord, p.width, ph, 1
            else:
                ox, oy, oz, ow, oh, od = coord, baseY + 1, pz, 1, ph, p.width
            if normal and p.kind != "arch":
                frame_on_face(c, normal, ox, oy, oz, ow, oh, od, trim,
                              sill=(p.kind == "window"))
            else:
                c.fill_micro_box(ox * 9, oy * 9, oz * 9, ow * 9, oh * 9, od * 9, AIR)

        # Stairs (multi-story): a subcube staircase up + a carved hole in the floor above.
        for st in story.stairs:
            sx, sz, sw, sd = st.rect
            subcube_stairs(c, sx, baseY + 1, sz, climb=h + 1, width=max(1, sw), mat=floor)
            nbY = baseY + h + 1
            for x in range(sx, sx + sw):
                for z in range(sz, sz + sd):
                    c.fill_micro_box(x * 9, nbY * 9, z * 9, 9, 9, 9, AIR)

        topY = baseY + h + 1
        baseY = topY

    roof_style = (spec.roof or {}).get("style", "flat")
    if roof_style == "pitched" and W >= 2 and D >= 2:
        pitched_roof(c, 0, topY, 0, W, D, roof, gable=wall, pitch=2)
    else:
        # Flat roof slab + a beveled stone-style coping along its outer top edges.
        c.fill_cube_box(0, topY, 0, W, 1, D, roof)
        c.chamfer_edge(0, topY * 9, 0, W * 9, 9, 9, "x", "+y-z", 3)
        c.chamfer_edge(0, topY * 9, (D - 1) * 9, W * 9, 9, 9, "x", "+y+z", 3)
        c.chamfer_edge(0, topY * 9, 0, 9, 9, D * 9, "z", "+y-x", 3)
        c.chamfer_edge((W - 1) * 9, topY * 9, 0, 9, 9, D * 9, "z", "+y+x", 3)
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
        for p in story.portals:
            if p.kind != "door":
                continue
            res = _resolve_portal(p, rooms, W, D)
            if not res:
                continue
            ax, coord, _ = res
            pxl, pzl = p.pos
            if ax == "z":
                hinge, rot = (pxl, baseY + 1, coord), 0
            else:
                hinge, rot = (coord, baseY + 1, pzl), 90
            hw = {"x": px + hinge[0], "y": py + hinge[1], "z": pz + hinge[2]}
            tmpl = "door_wood_wide" if p.width >= 2 else "door_wood"
            sp = _spawn(engine, tmpl, hw["x"], hw["y"], hw["z"], rot)
            oid = sp.get("object_id")
            if oid:
                _post(engine, "/api/door/register",
                      {"placed_object_id": oid, "template_name": tmpl, "hinge": hw,
                       "base_rotation": rot, "thickness": 5})
                if p.door and p.door.lockable:
                    _post(engine, "/api/door/lock",
                          {"placed_object_id": oid, "locked": True, "key_item_id": p.door.key})
                out["doors"].append({"id": oid, "locked": bool(p.door and p.door.lockable)})
        for f in story.fixtures:
            tmpl = FIXTURE_TEMPLATES.get(f.type)
            if not tmpl:
                continue
            rot = _FACING_ROT.get(f.facing, 0)
            sp = _spawn(engine, tmpl, px + f.rect[0], py + baseY + 1, pz + f.rect[1], rot)
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
    args = ap.parse_args(argv)

    spec = BuildingSpec.from_dict(json.loads(args.spec.read_text(encoding="utf-8")))
    name = args.name or args.spec.stem
    path, canvas = write_template(spec, name)
    print(f"[realize] wrote {path}  ({canvas.report().summary()})", file=sys.stderr)

    if args.build:
        x, y, z = (int(v) for v in args.position.split(","))
        result = drive_engine(spec, name, (x, y, z), args.engine)
        print(f"[realize] built: {len(result['doors'])} doors, {len(result['fixtures'])} fixtures",
              file=sys.stderr)
        print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
