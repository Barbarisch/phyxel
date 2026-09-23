#!/usr/bin/env python3
"""P4 - stage-count A/B: cost and legibility at 3 / 7 / 15 visible damage stages.
docs/VoxelDamageVisualization.md 6.4.

    python tools/damage_stage_ab.py

WHY A RUNTIME KNOB AND NOT THREE BUILDS. The legibility half needs the SAME scene at the same
poses; three separate binaries cannot hold it fixed. POST /api/debug/damage_stages changes the
count and forces a full re-mesh, so all three arms measure one scene.

COST AXIS. Damage is folded into the greedy-merge key, so a damage GRADIENT breaks merge runs
once per distinct stage value: 15 stages produce up to 15 concentric bands, 3 produce 3.
PREDICTION (written before the run, 6.4): the damaged region's face count at 3 stages is at
least 2x lower than at 15. FALSIFIER: if it differs by less than ~1.3x, merge fragmentation is
not where the cost lives and 3.5's central cost argument is wrong.

LEGIBILITY AXIS. Captures at 4 / 16 / 48 / 96 units. PREDICTION: legibility falls off
monotonically with distance, and the 3-stage variant stays distinguishable from pristine
further out than 15, because wider bands survive minification.

THE RIG IS A GRADIENT, NOT A LADDER. tools/damage_ladder_rig.py gives every column one uniform
stage, which is what you want for judging stages against each other -- but it is the WRONG rig
here, because uniform blocks merge cleanly and hide exactly the fragmentation being measured. A
blast produces a continuous radial gradient, which is the case that actually costs faces.
"""

import argparse
import json
import statistics
import sys
import time
import urllib.error
import urllib.request

BASE = "http://localhost:8090"
WALL_X0, WALL_X1 = 6, 29
WALL_Y0, WALL_Y1 = 17, 26
WALL_Z = 8
STAGE_COUNTS = [15, 7, 3]
DISTANCES = [4, 16, 48, 96]


def call(path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(BASE + path, data=data,
                                 method="POST" if data else "GET",
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=120) as r:
            raw = r.read().decode()
            return json.loads(raw) if raw.strip() else {}
    except urllib.error.URLError as e:
        sys.exit("engine API unreachable at %s (%s)" % (url_of(path), e))


def url_of(path):
    return BASE + path


def faces():
    """Total visible faces reported by the renderer."""
    s = call("/api/world/chunks").get("stats", {})
    return s.get("totalVisibleFaces", 0)


def main():
    argparse.ArgumentParser(description=__doc__).parse_args()
    print("P4 stage-count A/B\n")

    call("/api/world/generate", {"type": "Flat", "from": {"x": 0, "y": 0, "z": 0},
                                 "to": {"x": 1, "y": 0, "z": 0}})
    call("/api/world/fill", {"x1": WALL_X0, "y1": WALL_Y0, "z1": WALL_Z,
                             "x2": WALL_X1, "y2": WALL_Y1, "z2": WALL_Z, "material": "Stone"})
    time.sleep(3.0)

    missing = [x for x in range(WALL_X0, WALL_X1 + 1, 4)
               if not call("/api/world/voxel?x=%d&y=%d&z=%d"
                           % (x, WALL_Y0 + 1, WALL_Z)).get("exists")]
    if missing:
        sys.exit("PRECONDITION FAILED: columns %s missing - the chunk is not resident and the "
                 "fill dropped silently." % missing)

    # CONTROL: the un-fragmented baseline. Every cost number below is meaningless without it,
    # since it is what the damaged scene is being compared against.
    baseline = faces()
    print("control: undamaged wall = %d visible faces\n" % baseline)

    # A RADIAL GRADIENT, centred on the wall: this is the case that fragments merge runs.
    cx = (WALL_X0 + WALL_X1) / 2.0
    cy = (WALL_Y0 + WALL_Y1) / 2.0
    tough = call("/api/world/voxel?x=%d&y=%d&z=%d"
                 % (WALL_X0 + 1, WALL_Y0 + 1, WALL_Z))["toughness"]
    for x in range(WALL_X0, WALL_X1 + 1):
        for y in range(WALL_Y0, WALL_Y1 + 1):
            d = (((x + 0.5) - cx) ** 2 + ((y + 0.5) - cy) ** 2) ** 0.5
            f = max(0.0, 1.0 - d / 14.0)          # 1.0 at centre -> 0 at the rim
            if f <= 0.02:
                continue
            call("/api/damage/apply", {"x": x + 0.5, "y": y + 0.5, "z": WALL_Z + 0.5,
                                       "radius": 1.0, "energy": tough * 0.95 * f,
                                       "collapse": False})
    time.sleep(1.5)

    print("%-8s %-14s %-12s %-16s" % ("stages", "visible faces", "vs control", "chunks remeshed"))
    results = {}
    for n in STAGE_COUNTS:
        r = call("/api/debug/damage_stages", {"stages": n})
        applied = r.get("stages")
        if applied != n:
            print("  (requested %d, engine applied %d)" % (n, applied))
        time.sleep(1.5)
        f = faces()
        results[applied] = f
        print("%-8d %-14d %-12s %-16s"
              % (applied, f, "+%d" % (f - baseline), r.get("chunks_remeshed")))

    print()
    if 15 in results and 3 in results:
        extra15 = results[15] - baseline
        extra3 = results[3] - baseline
        ratio = extra15 / float(extra3) if extra3 > 0 else float("inf")
        print("DAMAGE-INDUCED faces (total minus control):")
        print("   15 stages: %d" % extra15)
        print("    3 stages: %d" % extra3)
        print("   ratio 15:3 = %.2fx   (prediction: >= 2.0x; falsifier: < 1.3x)" % ratio)
        if ratio < 1.3:
            print("\n   *** PREDICTION FALSIFIED. Merge fragmentation is not where the cost")
            print("       lives, and 3.5's central cost argument needs revisiting.")
        elif ratio < 2.0:
            print("\n   Prediction NOT met (>= 2.0x) but above the falsifier: the effect is real")
            print("   and smaller than argued. Report the number, do not round it up.")
        else:
            print("\n   Prediction met.")

    # Restore the shipped count so the session is not left on a debug setting.
    call("/api/debug/damage_stages", {"stages": 3})
    print("\nrestored to the shipped stage count (3)")
    print("Legibility ladder is a separate pass: set a stage count, then capture at "
          + " / ".join(str(d) for d in DISTANCES) + " units.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
