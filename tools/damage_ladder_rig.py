#!/usr/bin/env python3
"""Build the damage-ladder review rig (docs/VoxelDamageVisualization.md 14.1).

One command, identical every time, so a visual review months apart is comparable.

    python tools/damage_ladder_rig.py                 # build it, shipped tonemap
    python tools/damage_ladder_rig.py --measure       # + neutral tonemap, print the stage table
    python tools/damage_ladder_rig.py --material Glass --distance 48

What it builds: a full-cube wall whose columns are pre-damaged to an even spread of display
stages, with PRISTINE CONTROL COLUMNS on both ends, so the whole progression is judgeable in
one frame. Judging whether stages are distinguishable FROM EACH OTHER -- not merely from
pristine -- is the thing a single damaged voxel cannot show you.

Rig constraints, each of which is a trap that has already cost someone time:
  * Flat world, and the wall sits ABOVE y=16. A Flat world's surface IS sea level, so a wall
    at y 8..11 is entirely buried: the mesher emits no face where the neighbour is solid, the
    rig renders nothing, and it looks exactly like a broken shader.
  * Wholly inside chunk (0,0,0). A rig straddling chunks drops the fills for non-resident
    chunks SILENTLY, so it looks built and is not.
  * FULL CUBES, never a generated building. Structure walls are sub-cube and carry no damage
    bits at all in V1, so a building rig would produce a null result that also looks like a
    broken shader.
  * Energies are a fraction of the MATERIAL'S OWN break toughness, read back from the engine,
    not hardcoded -- so the ladder is correct for any material and cannot silently rot when a
    material's break block is retuned.
"""

import argparse
import json
import sys
import time
import urllib.error
import urllib.request

BASE = "http://localhost:8090"

# Rig geometry. z is a single plane so every column is lit identically.
WALL_X0, WALL_X1 = 8, 15
WALL_Y0, WALL_Y1 = 17, 20
WALL_Z = 8

# Fractions of break toughness for the 6 interior columns (x = 9..13 damaged, 8 pristine).
# 0.95 rather than 1.0: at ratio >= 1.0 the voxel BREAKS instead of accumulating.
LADDER = [0.0, 0.20, 0.40, 0.60, 0.80, 0.95]


def call(path, body=None, method=None):
    url = BASE + path
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method or ("POST" if data else "GET"),
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            raw = r.read().decode()
            return json.loads(raw) if raw.strip() else {}
    except urllib.error.URLError as e:
        sys.exit("engine API unreachable at %s (%s). Is the engine running with --project?" % (url, e))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--material", default="Stone", help="wall material (default: Stone)")
    ap.add_argument("--measure", action="store_true",
                    help="neutral tonemap (curve 0) for measurement; omit for the SHIPPED look, "
                         "which is the one that decides a visual review")
    ap.add_argument("--distance", type=float, default=16.0,
                    help="camera distance in world units; 14.2's ladder is 4/16/48/96")
    args = ap.parse_args()

    print("Building the damage ladder rig: %s, camera at %g units" % (args.material, args.distance))

    # Terrain first, so the wall is not floating in void and the ground gives the eye a reference.
    call("/api/world/generate", {"type": "Flat",
                                 "from": {"x": 0, "y": 0, "z": 0},
                                 "to":   {"x": 0, "y": 0, "z": 0}})

    # CLEAR FIRST -- this is load-bearing, not tidiness. Damage lives on the voxel, and a fill
    # over an existing voxel does not reset it, so re-running the rig would ADD each column's
    # energy to whatever it already carried. The first version of this script did exactly that:
    # the 0.60/0.80/0.95 columns summed past toughness and BROKE, turning a pure-graze rig into
    # a demolition. The integrity check at the bottom is what caught it.
    clear = call("/api/job/submit", {"type": "clear_region",
                                     "params": {"x1": WALL_X0, "y1": WALL_Y0, "z1": WALL_Z,
                                                "x2": WALL_X1, "y2": WALL_Y1, "z2": WALL_Z}})
    for _ in range(60):
        st = call("/api/job/%s" % clear.get("job_id"))
        if st.get("state") in ("complete", "failed", "cancelled"):
            break
        time.sleep(0.25)

    call("/api/world/fill", {"x1": WALL_X0, "y1": WALL_Y0, "z1": WALL_Z,
                             "x2": WALL_X1, "y2": WALL_Y1, "z2": WALL_Z,
                             "material": args.material})

    # Read the material's own toughness back from the engine rather than hardcoding it: the
    # ladder must stay correct if a material's break block is retuned.
    probe = call("/api/world/voxel?x=%d&y=%d&z=%d" % (WALL_X0, WALL_Y0 + 1, WALL_Z))
    toughness = probe.get("toughness")
    if not toughness:
        sys.exit("no toughness in /api/world/voxel -- is this engine older than P0? got: %s" % probe)
    print("%s toughness = %.1f (read from the engine)\n" % (args.material, toughness))

    # Column x=8 stays pristine (LADDER[0] == 0.0), x=14/15 are the far controls.
    for i, frac in enumerate(LADDER):
        if frac <= 0.0:
            continue
        x = WALL_X0 + i
        energy = toughness * frac
        for y in range(WALL_Y0, WALL_Y1 + 1):
            call("/api/damage/apply", {"x": x + 0.5, "y": y + 0.5, "z": WALL_Z + 0.5,
                                       "radius": 1.0, "energy": energy, "collapse": False})

    call("/api/debug/tonemap", {"curve": 0 if args.measure else 1})
    call("/api/camera", {"position": {"x": (WALL_X0 + WALL_X1) / 2.0 + 0.5,
                                      "y": (WALL_Y0 + WALL_Y1) / 2.0 + 0.5,
                                      "z": WALL_Z + args.distance},
                         "yaw": -90, "pitch": 0})

    # Verify the WORLD, never the fill response: /api/world/fill is async and reports no count.
    print("%-6s %-10s %-8s %-6s %s" % ("x", "damage", "dmg01", "stage", "role"))
    intact = 0
    for i in range(0, WALL_X1 - WALL_X0 + 1):
        x = WALL_X0 + i
        v = call("/api/world/voxel?x=%d&y=%d&z=%d" % (x, WALL_Y0 + 1, WALL_Z))
        if v.get("exists"):
            intact += 1
        role = "control (pristine)" if (i >= len(LADDER) or LADDER[i] == 0.0) else "damaged"
        print("%-6d %-10.1f %-8.3f %-6s %s" % (x, v.get("damage_energy", 0.0),
                                               v.get("damage01", 0.0),
                                               v.get("damage_stage", "?"), role))

    span = WALL_X1 - WALL_X0 + 1
    print("\n%d/%d columns intact -- %s" % (
        intact, span,
        "all grazes stayed sub-threshold, as intended" if intact == span
        else "*** SOMETHING BROKE: an energy exceeded toughness, so this is not a pure-graze rig"))
    print("tonemap: %s" % ("curve 0 (MEASUREMENT view)" if args.measure
                           else "shipped (VERDICT view -- this is the one that decides)"))


if __name__ == "__main__":
    main()
