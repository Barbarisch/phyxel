#!/usr/bin/env python3
"""One-shot runtime proof for terrain-aware build_settlement seating.

Falsifiable invariant: every building's seat Y must sit within STEP_TOL of the
terrain that RINGS its footprint (undisturbed ground just outside the walls). A
buried/floating house deviates far more — the race-condition build deviated ~7.

RED baseline (documented): on terrain that was still generating when scanned,
seatY ran ~7 cubes below the surrounding ground (buildings buried). GREEN here:
on fully-generated terrain, max |seatY - ring-median| must be <= STEP_TOL.
"""
import json, urllib.request, sys, statistics, time

B = "http://localhost:8090"
STEP_TOL = 2  # cubes; gentle-hill single-step grade tolerance


def post(u, d):
    req = urllib.request.Request(B + u, data=json.dumps(d).encode(),
                                 headers={"Content-Type": "application/json"})
    return json.load(urllib.request.urlopen(req, timeout=15))


def th(x, z):
    # retry: during build the game loop is saturated and GETs can time out (None != no-terrain)
    for _ in range(6):
        try:
            v = json.load(urllib.request.urlopen(
                f"{B}/api/world/terrain_height?x={x}&z={z}", timeout=8)).get("surface_y")
            if v is not None:
                return v
        except Exception:
            pass
        time.sleep(2)
    return None


def ring_terrain(cx, cz, fw, fd):
    """8 cells one step OUTSIDE the footprint edge midpoints/corners -> undisturbed ground."""
    x0, x1 = cx - fw // 2 - 1, cx + fw // 2 + 1
    z0, z1 = cz - fd // 2 - 1, cz + fd // 2 + 1
    pts = [(x0, cz), (x1, cz), (cx, z0), (cx, z1),
           (x0, z0), (x1, z0), (x0, z1), (x1, z1)]
    vals = [th(x, z) for x, z in pts]
    return [v for v in vals if v is not None]


def main():
    # --seat-flat reproduces the terrain-BLIND red baseline: terrain-mode plot selection is
    # unchanged, but every building is pinned to base Y (the `: oy` branch) instead of its local
    # ground -> buildings buried/floating on the hills -> the invariant must FAIL. Without the flag
    # the shipped terrain seating runs and the invariant must PASS. Same world, only `by` changes.
    seat_flat = "--seat-flat" in sys.argv
    expect_pass = not seat_flat
    print(f"MODE: {'RED (seat_flat -> terrain-blind, expect FAIL)' if seat_flat else 'GREEN (terrain seating, expect PASS)'}")
    res = post("/api/settlement/build", {
        "terrain": True, "seat_flat": seat_flat, "position": {"x": 2, "y": 16, "z": 2},
        "width": 124, "depth": 124, "plot_size": 10, "max_relief": 5,
        "street_width": 3, "setback": 1, "min_building": 6, "max_plots": 20,
        "typologies": ["croft", "longhouse", "hall_house"],
    })
    builds = res.get("queued_builds", [])
    print(f"queued {len(builds)} buildings; waiting for build queue to drain before sampling rings")
    # 20 structures x ~27k voxels saturate the game loop; let it drain so terrain GETs don't time out
    time.sleep(90)
    worst = 0.0
    rows = []
    for b in builds:
        pos = b["position"]
        fw, fd = b["footprint"]
        cx, cz = pos["x"] + fw // 2, pos["z"] + fd // 2
        seat = pos["y"]
        ring = ring_terrain(cx, cz, fw, fd)
        if not ring:
            rows.append((b["plot"], seat, None, None, "NO_RING"))
            continue
        med = statistics.median(ring)
        dev = abs(seat - med)
        worst = max(worst, dev)
        rows.append((b["plot"], seat, med, dev, "OK" if dev <= STEP_TOL else "BURIED/FLOAT"))
    for plot, seat, med, dev, mark in sorted(rows):
        print(f"  plot {plot:2}: seatY={seat:3}  ring_median={med}  dev={dev}  {mark}")
    print(f"\nMAX DEVIATION = {worst}  (tolerance {STEP_TOL})")
    measured = [r for r in rows if r[3] is not None]
    ok = bool(measured) and worst <= STEP_TOL and all(r[4] == "OK" for r in measured)
    print("SEATING INVARIANT:", "PASS" if ok else "FAIL")
    # the script's own exit code asserts the EXPECTED verdict for the mode (red must fail, green must pass)
    print("EXPECTED:", "PASS" if expect_pass else "FAIL", "->",
          "as expected" if ok == expect_pass else "UNEXPECTED")
    sys.exit(0 if ok == expect_pass else 1)


if __name__ == "__main__":
    main()
