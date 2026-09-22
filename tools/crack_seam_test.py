#!/usr/bin/env python3
"""R2 runtime half — the crack field must not break at a chunk seam.
docs/VoxelDamageVisualization.md §6.2.

!! STATUS: NOT A VALID GATE YET. The rig and its two PRECONDITIONS work and are worth
keeping. The PIXEL METRIC does not yet separate a world-seeded crack from a uv-seeded one,
so a PASS from this script currently proves NOTHING. Do not cite it as evidence that §6.2
is satisfied. Three metrics were tried against a deliberately uv-seeded shader and all
three failed to detect it -- see MEASUREMENT ATTEMPTS at the bottom of this file.

    python tools/crack_seam_test.py            # build rig, capture, measure
    python tools/crack_seam_test.py --keep     # leave the rig standing for a human look

WHAT THIS CATCHES THAT THE UNIT TEST CANNOT. `tests/core/VoxelCrackSeamTest.cpp` mirrors the
GLSL in C++ and proves the MIRROR is partition-independent. It cannot prove the SHADER is,
because it cannot execute the shader. The real mistake — a crack seeded from `texCoord` or
`sizeU` instead of world position — lives entirely on the GPU: UV space is tied to the
greedy-merged rectangle, and merge runs are computed in a 32^3 loop, so they TERMINATE AT
CHUNK BORDERS. A uv-seeded crack restarts its pattern at x = 32 and shows a hard vertical
discontinuity. Only a captured frame can see that.

RIG (§6.2). A Stone wall spanning x = 28..35, straddling the x = 31/32 chunk boundary.

  * BOTH chunks must be resident. This rig CANNOT fit in one chunk — that is the point — so
    the keys doc's "keep the rig inside ONE chunk" is replaced by an explicit residency
    assertion. In a fresh world only chunk (0,0,0) exists and every fill into (1,0,0) drops
    SILENTLY, leaving a half-built rig that looks fine and measures nothing.
  * Both sides must carry the SAME quantized damage stage. The stage is per-voxel and folded
    into the merge key, so a damage gradient across the seam produces a REAL, CORRECT
    luminance step that this test would otherwise report as a seam defect. Verified through
    /api/world/voxel, not by assuming equal energies land in the same band.

TWO CONTROLS, and the first one was missing in the first version of this test.

  * PRESENCE (control A). The damaged wall must measurably differ from a PRISTINE wall of the
    same material in the same capture -- i.e. cracks must actually be on screen. Without this
    the test is VACUOUS, and provably so: the first red attempt seeded the field from
    `texCoord * 8.0`, which made the pattern sub-pixel and rendered NO cracks at all, and the
    test happily reported "continuous across the chunk boundary". A shader that draws nothing
    has no discontinuities either. Presence must be asserted before continuity means anything.
  * ORDINARY BOUNDARIES (control B). The seam step is compared against the step at ordinary
    voxel boundaries in the same capture (x = 30/31, x = 33/34), so the threshold comes from
    the wall's own texture variation rather than a number picked by hand.

EXPECTED RESULT: GREEN ON ARRIVAL. The shipped shader is already world-seeded. To see this
test fail, deliberately seed `crackField` from `texCoord` in shaders/crack.glsl, rebuild
shaders, and re-run — that is how the red was demonstrated (recorded in §13 as retroactive).
"""

import argparse
import json
import sys
import time
import urllib.error
import urllib.request

BASE = "http://localhost:8090"

WALL_X0, WALL_X1 = 28, 35      # straddles the x = 31/32 chunk boundary
WALL_Y0, WALL_Y1 = 17, 20
WALL_Z = 8
SEAM_X = 32                    # first voxel of chunk (1,0,0)
CONTROL_SEAMS = [31, 34]       # ordinary voxel boundaries, same wall, same lighting
PRISTINE_X = 35                # left UNDAMAGED in frame -- control A (cracks are rendered)
DAMAGE_FRACTION = 0.60         # mid-range: stage 2 of 3, well inside the band


def call(path, body=None):
    url = BASE + path
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data,
                                 method="POST" if data else "GET",
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            raw = r.read().decode()
            return json.loads(raw) if raw.strip() else {}
    except urllib.error.URLError as e:
        sys.exit("engine API unreachable at %s (%s)" % (url, e))


def job(kind, params):
    r = call("/api/job/submit", {"type": kind, "params": params})
    for _ in range(120):
        st = call("/api/job/%s" % r.get("job_id"))
        if st.get("state") in ("complete", "failed", "cancelled"):
            return st
        time.sleep(0.25)
    return {"state": "timeout"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true", help="leave the rig standing")
    args = ap.parse_args()

    print("R2 runtime seam test — crack continuity across x = %d/%d\n" % (SEAM_X - 1, SEAM_X))

    # Both chunks, explicitly. A Flat world generated only for (0,0,0) silently drops every
    # fill into (1,0,0).
    call("/api/world/generate", {"type": "Flat",
                                 "from": {"x": 0, "y": 0, "z": 0},
                                 "to":   {"x": 1, "y": 0, "z": 0}})

    job("clear_region", {"x1": WALL_X0, "y1": WALL_Y0, "z1": WALL_Z,
                         "x2": WALL_X1, "y2": WALL_Y1, "z2": WALL_Z})
    call("/api/world/fill", {"x1": WALL_X0, "y1": WALL_Y0, "z1": WALL_Z,
                             "x2": WALL_X1, "y2": WALL_Y1, "z2": WALL_Z,
                             "material": "Stone"})

    # --- PRECONDITION 1: residency. Verify the WORLD, not the fill response. ---------------
    missing = [x for x in range(WALL_X0, WALL_X1 + 1)
               if not call("/api/world/voxel?x=%d&y=%d&z=%d" % (x, WALL_Y0 + 1, WALL_Z)).get("exists")]
    if missing:
        sys.exit("PRECONDITION FAILED: wall columns %s were not placed. The chunk holding them "
                 "is almost certainly not resident, so the fill dropped silently." % missing)
    print("precondition 1 OK: all %d columns present across both chunks"
          % (WALL_X1 - WALL_X0 + 1))

    # --- Damage every column identically, EXCEPT the pristine reference --------------------
    # The last column stays undamaged and in frame: it is control A, the proof that cracks are
    # actually being rendered. Continuity is meaningless without it.
    probe = call("/api/world/voxel?x=%d&y=%d&z=%d" % (WALL_X0, WALL_Y0 + 1, WALL_Z))
    energy = probe["toughness"] * DAMAGE_FRACTION
    for x in range(WALL_X0, PRISTINE_X):
        for y in range(WALL_Y0, WALL_Y1 + 1):
            call("/api/damage/apply", {"x": x + 0.5, "y": y + 0.5, "z": WALL_Z + 0.5,
                                       "radius": 1.0, "energy": energy, "collapse": False})

    # --- PRECONDITION 2: identical quantized stage on both sides ---------------------------
    stages = {}
    for x in range(WALL_X0, PRISTINE_X):
        v = call("/api/world/voxel?x=%d&y=%d&z=%d" % (x, WALL_Y0 + 1, WALL_Z))
        stages[x] = v.get("damage_stage")
    distinct = set(stages.values())
    if len(distinct) != 1:
        sys.exit("PRECONDITION FAILED: columns do not share one damage stage: %s\n"
                 "A stage step across the seam is a REAL discontinuity and would be reported "
                 "as a seam defect. Equalize before measuring." % stages)
    print("precondition 2 OK: every column at damage_stage %d" % distinct.pop())

    call("/api/debug/tonemap", {"curve": 0})
    call("/api/camera", {"position": {"x": (WALL_X0 + WALL_X1) / 2.0 + 0.5,
                                      "y": (WALL_Y0 + WALL_Y1) / 2.0 + 0.5,
                                      "z": WALL_Z + 11.0},
                         "yaw": -90, "pitch": 0})
    time.sleep(1.0)
    shot = call("/api/screenshot")          # GET, not POST
    path = shot.get("path")
    if not path:
        sys.exit("no screenshot path in response: %s" % shot)
    print("captured %s" % path)

    rc = measure(path)
    if not args.keep:
        call("/api/debug/tonemap", {"curve": 1})
    return rc


def measure(path):
    try:
        from PIL import Image
    except ImportError:
        sys.exit("Pillow required for the measurement (pip install pillow)")
    import os
    full = path if os.path.isabs(path) else os.path.join(os.getcwd(), path)
    im = Image.open(full).convert("RGB")
    W, H = im.size
    px = im.load()

    def lum(x, y):
        r, g, b = px[x, y]
        return 0.2126 * r + 0.7152 * g + 0.0722 * b

    # Locate the wall: scan the middle row of the viewport for the contiguous run of
    # stone-grey pixels. Detecting it beats hardcoding pixel bounds, which silently rot the
    # moment the camera or viewport changes.
    row = H // 2 - 80
    runs, start = [], None
    for x in range(260, W - 420):          # inside the viewport, clear of the side panels
        r, g, b = px[x, row]
        grey = abs(r - g) < 26 and abs(g - b) < 26 and 40 < r < 210
        if grey and start is None:
            start = x
        elif not grey and start is not None:
            runs.append((start, x - 1)); start = None
    if start is not None:
        runs.append((start, W - 421))
    if not runs:
        sys.exit("could not locate the wall in the capture")
    x0, x1 = max(runs, key=lambda r: r[1] - r[0])
    span = x1 - x0
    voxels = WALL_X1 - WALL_X0 + 1
    if span < voxels * 8:
        sys.exit("wall spans only %d px for %d voxels -- too small to measure" % (span, voxels))
    print("wall located at screen x %d..%d (%.1f px per voxel)" % (x0, x1, span / float(voxels)))

    # Mean luminance per screen column over the wall's vertical extent.
    ytop, ybot = row - 55, row + 95
    col = {x: sum(lum(x, y) for y in range(ytop, ybot)) / float(ybot - ytop)
           for x in range(x0, x1 + 1)}

    def screen_x(world_x):
        return x0 + int(round((world_x - WALL_X0) / float(voxels) * span))

    def discontinuity(world_boundary):
        """Luminance step across a voxel boundary, measured over a +/-3 px window so it is
        the STEP at the boundary rather than ordinary texture noise."""
        sx = screen_x(world_boundary)
        left = [col[x] for x in range(sx - 7, sx - 1) if x in col]
        right = [col[x] for x in range(sx + 1, sx + 7) if x in col]
        if not left or not right:
            return None
        return abs(sum(left) / len(left) - sum(right) / len(right))

    # --- CONTROL A: are cracks actually being drawn? ---------------------------------------
    # Compare texture DETAIL (mean absolute column-to-column change) on damaged vs pristine
    # columns. A crack network raises local contrast; plain stone does not. Mean luminance
    # alone is the wrong statistic -- the whole-face wear term shifts it even with no cracks.
    def detail(wx_lo, wx_hi):
        """How much DARK-LINE structure is on this patch, relative to its own brightness.

        A crack is a thin dark line. Two earlier metrics failed here and both failures are
        instructive: (1) mean luminance moves with the whole-face wear term even when no
        crack is drawn; (2) mean column-to-column change AVERAGES THE CRACK AWAY -- a crack
        crosses different columns at different heights, so collapsing each column to one
        number destroys exactly the signal being looked for, leaving the stone texture's own
        high-frequency detail to dominate.

        So: measure in 2D, and ask how far the DARKEST pixels sit below the median. A cracked
        surface has a long dark tail; plain stone does not. Normalized by the median so the
        darkening term cannot fake it."""
        a, b = screen_x(wx_lo), screen_x(wx_hi)
        vals = [lum(x, y) for x in range(a + 3, b - 3) if a + 3 <= x < b - 3
                for y in range(ytop, ybot, 2)]
        if len(vals) < 200:
            return 0.0
        vals.sort()
        median = vals[len(vals) // 2]
        p05 = vals[max(0, int(len(vals) * 0.05))]
        return (median - p05) / max(median, 1e-6)

    damaged_detail = detail(WALL_X0, PRISTINE_X)
    pristine_detail = detail(PRISTINE_X, WALL_X1 + 1)
    print("control A -- crack presence:")
    print("   damaged columns  detail %.3f" % damaged_detail)
    print("   pristine column  detail %.3f" % pristine_detail)
    if damaged_detail < pristine_detail * 1.25:
        print()
        print("FAIL (control A): the damaged wall shows no more surface detail than the pristine")
        print("      one -- NO CRACKS ARE BEING RENDERED. Any continuity result below would be")
        print("      vacuous: a shader that draws nothing has no discontinuities either.")
        return 1
    print("   -> cracks are on screen (%.2fx pristine detail)\n" % (damaged_detail / max(pristine_detail, 1e-6)))

    seam = discontinuity(SEAM_X)
    controls = [(b, discontinuity(b)) for b in CONTROL_SEAMS]
    controls = [(b, d) for b, d in controls if d is not None]
    if seam is None or not controls:
        sys.exit("could not sample the seam or its controls")

    worst_control = max(d for _, d in controls)
    print()
    print("%-28s %s" % ("boundary", "luminance step"))
    print("%-28s %.3f" % ("x = %d/%d  CHUNK SEAM" % (SEAM_X - 1, SEAM_X), seam))
    for b, d in controls:
        print("%-28s %.3f   (control: ordinary voxel boundary)" % ("x = %d/%d" % (b - 1, b), d))
    print()

    # The control IS the threshold. A uv-seeded crack restarts its pattern at the seam and
    # produces a step far outside the ordinary boundary-to-boundary variation.
    tol = max(1.5 * worst_control, worst_control + 1.0)
    if seam > tol:
        print("FAIL: the chunk seam shows a luminance step of %.3f, against a worst control of "
              "%.3f (tolerance %.3f)." % (seam, worst_control, tol))
        print("      The crack field is reading something CHUNK-DERIVED -- sizeU/sizeV/texCoord --")
        print("      instead of absolute world position. Merge runs terminate at chunk borders,")
        print("      so a uv-seeded pattern restarts there and the seam becomes visible.")
        return 1
    print("PASS: seam step %.3f is within the ordinary voxel-boundary variation (worst control "
          "%.3f, tolerance %.3f)." % (seam, worst_control, tol))
    print("      The crack field is continuous across the chunk boundary.")
    return 0


if __name__ == "__main__":
    sys.exit(main())


# ---------------------------------------------------------------------------------------------
# MEASUREMENT ATTEMPTS -- what was tried, and why each failed. Kept so the next attempt starts
# from the fourth idea rather than the first.
#
# The red was produced by deliberately seeding crackField from `texCoord` instead of
# `worldPosAbs` in voxel.frag, rebuilding shaders, and restarting. texCoord is tied to the
# greedy-merged rectangle, which terminates at chunk borders -- the exact defect §3.3 forbids.
#
#   1. LUMINANCE STEP AT THE SEAM, vs the step at ordinary voxel boundaries.
#      FAILED (false PASS). The first break used `texCoord * 8.0`, which made the pattern
#      sub-pixel so NO cracks rendered at all -- and a surface with no cracks has no
#      discontinuities either. This is what proved the test needed a PRESENCE control before
#      any continuity claim, which is now precondition "control A".
#
#   2. PRESENCE via mean column-to-column luminance change (absolute).
#      FAILED (false FAIL). Damaged surfaces are darkened by the whole-face wear term, and
#      absolute contrast scales with brightness, so a cracked wall measured LESS detailed than
#      pristine stone.
#
#   3. PRESENCE via the same, normalized by mean luminance.
#      FAILED (false FAIL). The deeper problem is that collapsing each column to one number
#      AVERAGES THE CRACK AWAY: a crack crosses different columns at different heights, so the
#      column mean smooths it out and the stone texture's own high-frequency detail dominates.
#
#   4. PRESENCE via 2D dark-tail depth, (median - p05) / median.
#      INCONCLUSIVE. Damaged 0.393 vs pristine 0.374 -- only 1.05x, below any usable threshold.
#      A Voronoi crack at this density is genuinely close to the stone albedo's own dark tail.
#
# WHERE THE NEXT ATTEMPT SHOULD START. Stop trying to characterize "is there a crack" from one
# capture. Use an A/B of two RIGS instead: the same damaged wall built (a) straddling x = 31/32
# and (b) wholly inside one chunk. Under world seeding both have the same discontinuity profile;
# under uv seeding only (a) has a step at the seam. That makes an IN-CHUNK WALL the control for
# "what a normal wall's discontinuity profile looks like", which is a far stronger reference
# than either the stone texture or a pristine column, and it sidesteps the presence question
# entirely -- both arms render whatever the shader renders.
# ---------------------------------------------------------------------------------------------
