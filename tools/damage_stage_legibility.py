#!/usr/bin/env python3
"""P4 legibility axis - can damage still be SEEN at 3 / 7 / 15 stages, and at what damage level?
docs/VoxelDamageVisualization.md §2.

    python tools/damage_stage_legibility.py

Companion to tools/damage_stage_ab.py (the cost axis). Cost alone cannot choose the stage count:
the cheapest option is always the coarsest, and the question is how coarse we can go before
damage stops reading.

RIG. A uniformly damaged block beside an UNDAMAGED CONTROL in the same frame. The control is
what makes this a measurement rather than an impression: "the damaged wall got darker" means
nothing without the undamaged wall in the same capture under the same light.

THE DAMAGE LEVEL IS THE WHOLE EXPERIMENT, and the first version of this rig got it wrong. It
damaged to 0.95 of toughness -- MAXIMUM -- and measured 18.87 / 19.62 / 19.99% for 15 / 7 / 3
stages: three numbers agreeing to within capture noise, because at max damage every arm
saturates to its OWN top stage and the shader normalises stage/stagesVisible, so
15/15 == 7/7 == 3/3 == 1.0. The arms are identical BY CONSTRUCTION at that input and no stage
count can win. That run is recorded in 6.4 as a rig defect, not as evidence coarsening is free.

The cost of coarsening lives at LOW damage. A voxel at ratio 0.10 is stage 1-2 of 15 (faintly
cracked) but rounds to stage 0 of 3 -- rendered PRISTINE. Coarsening does not blur damage, it
DELETES the early increments. So this ladder sweeps damage ratio as well as distance, and prints
the resulting visible stage beside every row, because that is what explains the numbers.

PREDICTION, written before the corrected run, refining 6.4:
  (a) At LOW damage (ratio 0.12) the 3-stage arm quantizes to visible stage 0 and is therefore
      INDISTINGUISHABLE from the control at every distance (< 1% difference), while 15 stages
      still reads. This is the real price of coarsening and 6.4 did not state it.
  (b) At MID damage (ratio 0.45) every arm renders a non-zero stage, and legibility then falls
      off monotonically with distance for each.
FALSIFIER for (a): if 3 stages shows a >= 5% difference at ratio 0.12, the quantizer is not
rounding the way DamageStage.h says and the bit packing needs re-reading.

4u is omitted deliberately: two 8-wide blocks plus their gap span ~18 world units, which does
not fit in one frame at 4u. Close range is not the open question anyway (section 14 signed off
there); the far end is.
"""

import argparse
import json
import sys
import time
import urllib.error
import urllib.request

BASE = "http://localhost:8090"
DMG_X0, DMG_X1 = 8, 15          # damaged block
CTL_X0, CTL_X1 = 18, 25         # undamaged control, same material, same frame
Y0, Y1 = 17, 24
Z = 8
STAGE_COUNTS = [15, 7, 3]
DISTANCES = [12, 16, 48, 96]
# Fraction of the material's toughness. LOW is the discriminating case; MID is the sanity case
# where every arm must show something. MAX is deliberately absent -- see the module docstring.
# ratio 0.0 is the NOISE FLOOR and is not optional: the two blocks sit at different screen
# positions under the same sun, so their shading is not identical and the metric does NOT read
# 0% for two pristine blocks. Without this row a 2.5% reading looks like faint cracking when it
# may be the floor -- which is exactly how the 3-stage arm was nearly mis-reported.
RATIOS = [("none", 0.0), ("low", 0.12), ("mid", 0.45)]
# Small enough that a hit centred on one voxel cannot reach a neighbour's centre (they are 1.0
# apart). With radius 1.0 the block is NOT uniform -- see the precondition check in main().
DMG_RADIUS = 0.4


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
        sys.exit("engine API unreachable (%s)" % e)


def measure(path):
    """Mean luminance difference between the damaged block and the control, as a fraction of the
    control's own luminance. Scale-free, so distances are comparable."""
    import os
    from PIL import Image
    full = path if os.path.isabs(path) else os.path.join(os.getcwd(), path)
    im = Image.open(full).convert("RGB")
    W, H = im.size
    px = im.load()

    def lum(x, y):
        r, g, b = px[x, y]
        return 0.2126 * r + 0.7152 * g + 0.0722 * b

    # Locate the two blocks: scan a row for runs that are neither sky nor grass.
    row = H // 2 - 90
    runs, start = [], None
    for x in range(260, W - 420):
        r, g, b = px[x, row]
        solid = (not (b > r + 15)) and (not (g > r + 12 and g > b + 12)) and 25 < r < 235
        if solid and start is None:
            start = x
        elif not solid and start is not None:
            if x - start > 12:
                runs.append((start, x - 1))
            start = None
    if start is not None and (W - 421) - start > 12:
        runs.append((start, W - 421))
    if len(runs) < 2:
        return None
    runs.sort(key=lambda r: r[1] - r[0], reverse=True)
    a, b = sorted(runs[:2], key=lambda r: r[0])       # left = damaged, right = control

    def mean_of(run):
        x0, x1 = run
        vals = [lum(x, y) for x in range(x0 + 2, x1 - 1)
                for y in range(row - 40, row + 60, 2)]
        return sum(vals) / float(len(vals)) if vals else 0.0

    dmg, ctl = mean_of(a), mean_of(b)
    if ctl <= 1e-6:
        return None
    return abs(ctl - dmg) / ctl * 100.0


def reset_blocks():
    """Clear THEN fill. fill does NOT overwrite, so a wall left standing from an earlier arm
    survives carrying ITS damage -- the first version of this script silently measured the
    gradient wall left by the cost A/B and duly reported the 'undamaged control' at stage 2.
    Damage also ACCUMULATES, so each ratio needs fresh voxels, not another hit on the old ones."""
    j = call("/api/job/submit", {"type": "clear_region",
                                 "params": {"x1": DMG_X0 - 2, "y1": Y0, "z1": Z,
                                            "x2": CTL_X1 + 2, "y2": Y1 + 6, "z2": Z}})
    for _ in range(80):
        if call("/api/job/%s" % j.get("job_id")).get("state") in ("complete", "failed"):
            break
        time.sleep(0.25)
    for (a, b) in ((DMG_X0, DMG_X1), (CTL_X0, CTL_X1)):
        call("/api/world/fill", {"x1": a, "y1": Y0, "z1": Z,
                                 "x2": b, "y2": Y1, "z2": Z, "material": "Stone"})
    time.sleep(3.0)


def main():
    argparse.ArgumentParser(description=__doc__).parse_args()
    print("P4 legibility ladder")

    call("/api/world/generate", {"type": "Flat", "from": {"x": 0, "y": 0, "z": 0},
                                 "to": {"x": 1, "y": 0, "z": 0}})
    reset_blocks()

    probe = call("/api/world/voxel?x=%d&y=%d&z=%d" % (DMG_X0, Y0 + 1, Z))
    if not probe.get("exists"):
        sys.exit("PRECONDITION FAILED: damaged block not placed")
    toughness = probe["toughness"]
    ctl0 = call("/api/world/voxel?x=%d&y=%d&z=%d" % (CTL_X0, Y0 + 1, Z)).get("damage_stage")
    if ctl0:
        sys.exit("PRECONDITION FAILED: control block starts at stage %s, not 0 - the clear did "
                 "not take, and this run would be measuring a stale wall." % ctl0)

    call("/api/debug/tonemap", {"curve": 0})
    cx = (DMG_X0 + CTL_X1) / 2.0 + 0.5
    cy = (Y0 + Y1) / 2.0 + 0.5

    floor = {}
    for label, ratio in RATIOS:
        reset_blocks()
        for x in range(DMG_X0, DMG_X1 + 1) if ratio > 0.0 else ():
            for y in range(Y0, Y1 + 1):
                call("/api/damage/apply", {"x": x + 0.5, "y": y + 0.5, "z": Z + 0.5,
                                           "radius": DMG_RADIUS, "energy": toughness * ratio,
                                           "collapse": False})
        time.sleep(1.5)

        # PRECONDITION: the block must be UNIFORM, or "ratio" is a label rather than a fact and
        # the visible-stage column describes only the voxel probed. The first corrected run used
        # radius 1.0, which reaches neighbouring voxel centres: interior voxels took ~7 splash
        # hits while corners took 1, so the corner read stage 0 of 3 while the interior rendered
        # stage 1 -- which is exactly why that run measured 2.34% for a supposedly uncracked arm.
        if ratio == 0.0:
            print("")
            print("--- NOISE FLOOR: both blocks pristine, no damage applied ---")
            print("%-8s %-10s %s" % ("stages", "vis.stage",
                  "  ".join("%7s" % ("%du" % d) for d in DISTANCES)))
            for d in DISTANCES:
                call("/api/camera", {"position": {"x": cx, "y": cy, "z": Z + d},
                                     "yaw": -90, "pitch": 0})
                time.sleep(1.0)
                floor[d] = measure(call("/api/screenshot").get("path", ""))
            print("%-8s %-10s %s" % ("n/a", "0", "  ".join(
                ("%6.2f%%" % floor[d]) if floor[d] is not None else "     --"
                for d in DISTANCES)))
            continue

        corner = call("/api/world/voxel?x=%d&y=%d&z=%d"
                      % (DMG_X0, Y0, Z)).get("damage01", 0.0)
        centre = call("/api/world/voxel?x=%d&y=%d&z=%d"
                      % ((DMG_X0 + DMG_X1) // 2, (Y0 + Y1) // 2, Z)).get("damage01", 0.0)
        if abs(corner - centre) > 0.02:
            sys.exit("PRECONDITION FAILED at ratio %.2f: block is not uniform (corner damage01 "
                     "%.3f vs centre %.3f). Splash from DMG_RADIUS=%.2f is reaching neighbours; "
                     "shrink it." % (ratio, corner, centre, DMG_RADIUS))
        print("")
        print("uniform block confirmed: damage01 %.3f at corner, %.3f at centre "
              "(requested ratio %.2f)" % (corner, centre, ratio))

        print("")
        print("--- %s damage: ratio %.2f of toughness ---" % (label.upper(), ratio))
        print("%-8s %-10s %s" % ("stages", "vis.stage",
              "  ".join("%7s" % ("%du" % d) for d in DISTANCES)))
        for n in STAGE_COUNTS:
            applied = call("/api/debug/damage_stages", {"stages": n}).get("stages")
            time.sleep(1.2)
            # The visible stage EXPLAINS the row. A ~0% row beside stage 0 is the quantizer
            # working as designed; a ~0% row beside a NON-zero stage would be a rendering bug,
            # and without this column those two are indistinguishable.
            vis = call("/api/world/voxel?x=%d&y=%d&z=%d"
                       % (DMG_X0, Y0 + 1, Z)).get("damage_stage")
            row = []
            for d in DISTANCES:
                call("/api/camera", {"position": {"x": cx, "y": cy, "z": Z + d},
                                     "yaw": -90, "pitch": 0})
                time.sleep(1.0)
                row.append(measure(call("/api/screenshot").get("path", "")))
            def cell(d, v):
                if v is None:
                    return "     --"
                f = floor.get(d)
                # Mark anything not clearly above the noise floor. "=" means the arm is
                # INDISTINGUISHABLE from two pristine blocks at that distance -- the damage is
                # not faint, it is absent.
                return ("%6.2f%%%s" % (v, "=" if f is not None and v <= f * 1.25 else " "))
            print("%-8d %-10s %s" % (applied, "%s of %d" % (vis, applied),
                  "  ".join(cell(d, v) for d, v in zip(DISTANCES, row))))

    call("/api/debug/damage_stages", {"stages": 3})
    call("/api/debug/tonemap", {"curve": 1})
    print("")
    print("(% = mean luminance difference from the undamaged control, as a fraction of it)")
    print("restored: 3 stages, shipped tonemap")
    return 0


if __name__ == "__main__":
    sys.exit(main())
