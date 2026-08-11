#!/usr/bin/env python3
"""lighting_lab.py — build the Lighting Lab test world and capture its fixed poses.

WHY THIS EXISTS. Lighting changes are judged on two questions the eye cannot answer reliably:
"are interiors correctly darker than outdoors" and "did the change move anything I did not intend".
This builds a deliberately tiny, one-variable-at-a-time world where each room isolates ONE lighting
mechanism, drives the camera to a FIXED pose list, and writes screenshots that
tools/lighting_stats.py turns into numbers.

Test-rig discipline (CLAUDE.md): small, inside a couple of chunks, one variable per room, a written
prediction per room, and controls at BOTH ends (a room that must be black and a room that must be
fully lit). Predictions are in ROOMS below so a surprising number is checkable against an
expectation rather than a vibe.

THE ROOMS (all 7x7 outer / 5x5 interior, floor y=16, walls y=17..21, roof y=22, spaced along +X):

  control_open   No roof.                  Predict: interior ~= exterior. Proves the rig sees light.
  sealed         Fully closed shell.       Predict: black (ambient floor only). Proves it sees dark.
  window         One 1x1 Glass pane.       Predict: dim, graded away from the pane. Glass must ADMIT
                                           skylight (it did not until the P2 light-opacity fix).
  door           One 1x2 wall opening.     Predict: light spills a few cells and falls off.
  hearth         Sealed + one `glow` cube.  Predict: warm radial falloff, dark corners.

  Plus an unobstructed patch of flat ground as the EXTERIOR reference, and (with --tavern) a
  generator-built tavern, which is the real-world case: structure generation builds at sub-voxel
  resolution, and sub-voxel geometry did not occlude the light bake before P2.

PROVENANCE: rooms here are hand-placed voxels via /api/world/fill and are labelled as such. The
tavern is produced by the ENGINE's generator (POST /api/structure/build), never hand-assembled.

USAGE
  python tools/lighting_lab.py --build                 # generate terrain + rooms (engine must run)
  python tools/lighting_lab.py --capture P2_after      # screenshot every pose, tagged
  python tools/lighting_lab.py --build --tavern --capture baseline
  python tools/lighting_lab.py --verify                # read the world back and check the shells

Captures land in docs/evidence/lab_<tag>_<pose>.png. Regions for lighting_stats.py are written to
docs/evidence/lab_regions.json.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

API = "http://localhost:8090"
EVIDENCE = Path("docs/evidence")

GROUND_Y = 16          # Flat generation fills solid below y=16, so y=16 is the top solid layer
FLOOR_Y = GROUND_Y      # rooms sit ON the ground: their floor IS the ground layer
WALL_LO = FLOOR_Y + 1   # 17
WALL_HI = FLOOR_Y + 5   # 21
ROOF_Y = FLOOR_Y + 6    # 22
ROOM_SPAN = 6           # outer footprint is ROOM_SPAN+1 = 7 cells
Z0 = 8                  # rooms share a Z band


class Room:
    def __init__(self, name: str, x0: int, predict: str):
        self.name = name
        self.x0 = x0
        self.x1 = x0 + ROOM_SPAN
        self.z0 = Z0
        self.z1 = Z0 + ROOM_SPAN
        self.predict = predict

    @property
    def centre(self) -> tuple[int, int, int]:
        return ((self.x0 + self.x1) // 2, WALL_LO + 1, (self.z0 + self.z1) // 2)


ROOMS = [
    Room("control_open", 4,
         "interior luminance ~= exterior; open sky reaches the floor (positive control)"),
    Room("sealed", 12,
         "interior black apart from the ambient floor; no path to any source (negative control)"),
    Room("window", 20,
         "dim and graded away from the pane; REQUIRES glass to pass skylight (P2)"),
    Room("door", 28,
         "a wedge of light through the opening, falling off ~1 level per cell"),
    # NOTE x=48, not 36. At x 36..42 the engine PERSISTENTLY refuses ~12 shell cells at the far
    # corner (place_voxel returns success:false, fill_region reports failed:N, no log line, survives
    # save+restart and repeated retries, cells are empty and get_objects_at is empty). That is a real
    # engine defect and it is NOT a lighting one, so the rig is routed around it rather than blocked
    # by it. --verify is what caught it; keep running --verify before trusting any capture.
    Room("hearth", 48,
         "warm radial falloff from the glow cube; corners noticeably darker"),
]

# Screen regions for tools/lighting_stats.py, as fractions of width/height. The interior rect is the
# middle of the frame (where the room wall/floor sits at every eye-level pose); the sky rect is the
# top band. interior/sky is the ratio that answers "are rooms over-bright".
REGIONS = {
    "interior": [0.30, 0.35, 0.70, 0.80],
    "sky": [0.0, 0.0, 1.0, 0.12],
    "lower": [0.20, 0.70, 0.80, 1.0],
}


def post(path: str, body: dict | None = None, timeout: float = 120.0) -> dict:
    data = json.dumps(body or {}).encode("utf-8")
    req = urllib.request.Request(f"{API}{path}", data=data,
                                 headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=timeout) as r:
        raw = r.read().decode("utf-8", "replace")
    try:
        return json.loads(raw)
    except json.JSONDecodeError:
        return {"raw": raw}


def get(path: str, params: dict | None = None, timeout: float = 60.0) -> dict:
    url = f"{API}{path}"
    if params:
        url += "?" + urllib.parse.urlencode(params)
    with urllib.request.urlopen(url, timeout=timeout) as r:
        raw = r.read().decode("utf-8", "replace")
    try:
        return json.loads(raw)
    except json.JSONDecodeError:
        return {"raw": raw}


def job(job_type: str, params: dict, timeout: float = 180.0) -> dict:
    """Run a world-mutating operation through the JobSystem worker, like the MCP server does.

    fill_region / clear_region / generate_world are NOT plain POST endpoints: they are submitted to
    POST /api/job/submit and polled at GET /api/job/<id>. Doing them on the main thread freezes the
    engine, and the older direct /api/world/fill route returns before the voxels exist — which is
    exactly why the rig verifies the WORLD afterwards instead of trusting a response.
    """
    initial = post("/api/job/submit", {"type": job_type, "params": params})
    job_id = initial.get("job_id")
    if job_id is None:
        return initial
    waited = 0.0
    while waited < timeout:
        time.sleep(0.35)
        waited += 0.35
        status = get(f"/api/job/{job_id}")
        if status.get("state") in ("complete", "failed", "cancelled"):
            res = status.get("result")
            return res if isinstance(res, dict) else status
    return {"error": f"job {job_type} did not finish within {timeout}s"}


def set_camera(cam: dict) -> dict:
    """POST /api/camera wants position NESTED, not flat x/y/z — a flat body is silently ignored."""
    body = {"position": {"x": cam["x"], "y": cam["y"], "z": cam["z"]},
            "yaw": cam["yaw"], "pitch": cam["pitch"]}
    if "mode" in cam:
        body["mode"] = cam["mode"]
    return post("/api/camera", body)


def require_engine() -> None:
    try:
        get("/api/camera", timeout=5)
    except (urllib.error.URLError, OSError) as e:
        sys.exit(f"engine not reachable at {API} ({e}). Launch it first (launch_engine).")


# ── Building ─────────────────────────────────────────────────────────────────────────────────────

def fill(x1, y1, z1, x2, y2, z2, material, hollow=False, replace=True) -> dict:
    return job("fill_region", {
        "x1": x1, "y1": y1, "z1": z1, "x2": x2, "y2": y2, "z2": z2,
        "material": material, "hollow": hollow, "replace": replace,
    })


def build_room(r: Room) -> None:
    """Walls + roof. The floor is the existing ground layer, so nothing is placed at FLOOR_Y."""
    # Four walls, one cell thick, as full slabs then hollowed by the openings below.
    fill(r.x0, WALL_LO, r.z0, r.x1, WALL_HI, r.z0, "StoneBricks")   # -Z wall
    fill(r.x0, WALL_LO, r.z1, r.x1, WALL_HI, r.z1, "StoneBricks")   # +Z wall
    fill(r.x0, WALL_LO, r.z0, r.x0, WALL_HI, r.z1, "StoneBricks")   # -X wall
    fill(r.x1, WALL_LO, r.z0, r.x1, WALL_HI, r.z1, "StoneBricks")   # +X wall

    if r.name != "control_open":
        fill(r.x0, ROOF_Y, r.z0, r.x1, ROOF_Y, r.z1, "Wood")

    cx = (r.x0 + r.x1) // 2
    if r.name == "window":
        # A single 1x1 pane at eye height in the -Z wall.
        fill(cx, WALL_LO + 2, r.z0, cx, WALL_LO + 2, r.z0, "Glass")
    elif r.name == "door":
        # A 1-wide, 2-tall hole in the -Z wall. clear_region, not a fill, so it is genuinely open.
        job("clear_region", {"x1": cx, "y1": WALL_LO, "z1": r.z0,
                             "x2": cx, "y2": WALL_LO + 1, "z2": r.z0})
    elif r.name == "hearth":
        fill(cx, WALL_LO, (r.z0 + r.z1) // 2, cx, WALL_LO, (r.z0 + r.z1) // 2, "glow")


def build(with_tavern: bool) -> None:
    require_engine()
    print("generating flat terrain (chunks x0..2, z0)...")
    print(json.dumps(job("generate_world", {
        "type": "Flat", "seed": 1,
        "from": {"x": 0, "y": 0, "z": 0}, "to": {"x": 2, "y": 0, "z": 0},
    })))
    # /api/world/fill is async and reports no placed count — never trust its response, verify the
    # world afterwards with --verify (CLAUDE.md test-rig rule).
    time.sleep(2.0)

    for r in ROOMS:
        print(f"  room {r.name:<13} x {r.x0}..{r.x1}  predict: {r.predict}")
        build_room(r)
        time.sleep(0.4)

    if with_tavern:
        print("building a tavern with the ENGINE's generator (POST /api/structure/build)...")
        # Explicit typology + footprint as an ARRAY, or schema v2 silently yields a hall_house.
        print(json.dumps(post("/api/structure/build", {
            "schema": "v2", "typology": "tavern", "footprint": [14, 18],
            "x": 44, "y": GROUND_Y, "z": 6, "seed": 7,
        }), indent=2)[:800])

    EVIDENCE.mkdir(parents=True, exist_ok=True)
    (EVIDENCE / "lab_regions.json").write_text(json.dumps(REGIONS, indent=2), encoding="utf-8")
    print(f"\nwrote {EVIDENCE / 'lab_regions.json'}")
    print("now run with --verify, then --capture <tag>")


# ── Verification: read the WORLD back, not the API response ──────────────────────────────────────

def verify() -> int:
    require_engine()
    bad = 0
    for r in ROOMS:
        res = get("/api/world/scan", {"x1": r.x0, "y1": WALL_LO, "z1": r.z0,
                                      "x2": r.x1, "y2": ROOF_Y, "z2": r.z1})
        voxels = res.get("voxels", res.get("results", []))
        # Count only PERIMETER cells as walls: the hearth room has a `glow` cube on its interior
        # floor, and counting everything in the wall height band scored that as a 121st wall.
        def perim(v) -> bool:
            return v.get("x") in (r.x0, r.x1) or v.get("z") in (r.z0, r.z1)
        roof = sum(1 for v in voxels if v.get("y") == ROOF_Y)
        walls = sum(1 for v in voxels if WALL_LO <= v.get("y", -1) <= WALL_HI and perim(v))
        expect_roof = 0 if r.name == "control_open" else (ROOM_SPAN + 1) ** 2
        # Perimeter of a (span+1)^2 ring, times wall height.
        expect_walls = ((ROOM_SPAN + 1) ** 2 - (ROOM_SPAN - 1) ** 2) * (WALL_HI - WALL_LO + 1)
        if r.name == "door":
            expect_walls -= 2
        ok = roof == expect_roof and walls == expect_walls
        bad += 0 if ok else 1
        print(f"  {r.name:<13} roof {roof:>3}/{expect_roof:<3} walls {walls:>3}/{expect_walls:<3}"
              f"  {'ok' if ok else 'MISMATCH'}")
    if bad:
        print(f"\n{bad} room(s) did not build as specified — fix the rig before trusting any capture.")
    return 1 if bad else 0


# ── Poses + capture ──────────────────────────────────────────────────────────────────────────────

def poses() -> list[tuple[str, dict]]:
    """Fixed poses. Every one carries full position AND yaw AND pitch: a pitch-only /api/camera call
    is stomped by the per-frame free-cam sync.

    YAW CONVENTION (this has cost real hours before — see the editor-camera-yaw reference): yaw is
    the XZ heading (cos yaw, sin yaw), so yaw 0 = +X, yaw 90 = +Z, yaw 180 = -X, yaw -90 = -Z.
    These poses stand near the -Z wall and look at the far +Z wall, which is yaw +90, NOT -90.
    If a capture shows only floor and sky, suspect the yaw before suspecting the scene."""
    out: list[tuple[str, dict]] = []
    for r in ROOMS:
        cx, cy, cz = r.centre
        # Stand inside, just off the -Z wall, looking at the far (+Z) wall.
        out.append((f"in_{r.name}", {"x": cx + 0.5, "y": cy + 0.5, "z": r.z0 + 1.5,
                                     "yaw": 90.0, "pitch": -5.0, "mode": "free"}))
    # One frame containing BOTH a lit exterior and a doorway interior — the pose that makes the
    # interior/exterior ratio meaningful. Outside the -Z wall, looking +Z at the doorway.
    d = next(r for r in ROOMS if r.name == "door")
    cx = (d.x0 + d.x1) // 2
    out.append(("door_from_outside", {"x": cx + 0.5, "y": WALL_LO + 1.5, "z": d.z0 - 7.0,
                                      "yaw": 90.0, "pitch": 0.0, "mode": "free"}))
    # Elevated three-quarter view of the whole row, from -X/-Z looking +X/+Z.
    out.append(("overview", {"x": -14.0, "y": 40.0, "z": -14.0,
                             "yaw": 40.0, "pitch": -30.0, "mode": "free"}))
    return out


def capture(tag: str) -> None:
    require_engine()
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    for name, cam in poses():
        set_camera(cam)
        time.sleep(1.2)          # let the frame settle (and any dirty-chunk remesh drain)
        # /api/screenshot is a GET and the ENGINE chooses the path — it takes no destination. Copy
        # the file it wrote to a stable, tagged name so before/after pairs are comparable.
        res = get("/api/screenshot")
        src = res.get("path") or res.get("file") or res.get("filename")
        out = EVIDENCE / f"lab_{tag}_{name}.png"
        if src and Path(src).exists():
            out.write_bytes(Path(src).read_bytes())
            print(f"  {name:<22} -> {out}")
        else:
            print(f"  {name:<22} !! no screenshot path in response: {res}")
    print(f"\nnow: python tools/lighting_stats.py {EVIDENCE}/lab_{tag}_door_from_outside.png "
          f"--regions {EVIDENCE}/lab_regions.json")


# ── Sky / atmosphere sweep ───────────────────────────────────────────────────────────────────────

# Times chosen to hit every distinct regime of the scattering model, not just "day and night":
# the sun's transmittance and the sky's colour change fastest within a couple of degrees of the
# horizon, so the sweep is dense there and sparse at midday where nothing much happens.
SKY_TIMES = [
    ("night_0200", 2.0), ("astro_dawn_0500", 5.0), ("sunrise_0600", 6.0),
    ("golden_0640", 6.67), ("morning_0900", 9.0), ("noon_1200", 12.0),
    ("afternoon_1600", 16.0), ("golden_1720", 17.33), ("sunset_1800", 18.0),
    ("dusk_1830", 18.5), ("night_2200", 22.0),
]

# Two headings: one looking along the sun's swing plane (where the disc and the warm band live) and
# one 90 degrees away (where the sky should be at its deepest blue). One frame cannot show both.
SKY_VIEWS = [
    ("sunward", {"x": 24.0, "y": 26.0, "z": -6.0, "yaw": 0.0, "pitch": 8.0, "mode": "free"}),
    ("crossward", {"x": 24.0, "y": 26.0, "z": -6.0, "yaw": 90.0, "pitch": 8.0, "mode": "free"}),
]


def sky_sweep(tag: str, moon_day: int | None = None) -> None:
    """Capture the sky at every time in SKY_TIMES from both headings.

    The day/night cycle is ENABLED and PAUSED for each shot: enabled so the sun and moon directions
    are driven by time of day at all, paused so the time cannot drift between setting it and reading
    the frame back. Leaving it running is how you get a contact sheet that cannot be reproduced.
    """
    require_engine()
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    for name, hour in SKY_TIMES:
        body = {"enabled": True, "paused": True, "timeOfDay": hour}
        if moon_day is not None:
            body["dayNumber"] = moon_day
        post("/api/daynight/set", body)
        for view_name, cam in SKY_VIEWS:
            set_camera(cam)
            time.sleep(1.0)
            res = get("/api/screenshot")
            src = res.get("path") or res.get("file")
            out = EVIDENCE / f"sky_{tag}_{name}_{view_name}.png"
            if src and Path(src).exists():
                out.write_bytes(Path(src).read_bytes())
                print(f"  {name:<18} {view_name:<10} -> {out.name}")
            else:
                print(f"  {name:<18} {view_name:<10} !! no screenshot: {res}")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", action="store_true", help="generate terrain and build the rooms")
    ap.add_argument("--tavern", action="store_true",
                    help="also build a generator-made tavern (the sub-voxel real-world case)")
    ap.add_argument("--verify", action="store_true", help="read the world back and check the shells")
    ap.add_argument("--capture", metavar="TAG", help="screenshot every fixed pose, tagged")
    ap.add_argument("--sky", metavar="TAG",
                    help="sweep the day/night cycle and capture the sky from two headings")
    ap.add_argument("--moon-day", type=int, metavar="N",
                    help="with --sky: set the day number, which selects the lunar phase "
                         "(0 = new, 14 = full in a 28-day cycle)")
    ap.add_argument("--poses", action="store_true", help="print the pose list and exit")
    args = ap.parse_args(argv)

    if args.poses:
        for n, c in poses():
            print(f"{n:<22} {json.dumps(c)}")
        return 0
    if not (args.build or args.verify or args.capture or args.sky):
        ap.print_help()
        return 2
    if args.build:
        build(args.tavern)
    rc = verify() if args.verify else 0
    if args.capture:
        capture(args.capture)
    if args.sky:
        sky_sweep(args.sky, args.moon_day)
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
