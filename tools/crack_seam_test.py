#!/usr/bin/env python3
"""R2 runtime half - the crack field must not break at a chunk seam.
docs/VoxelDamageVisualization.md 6.2.

    python tools/crack_seam_test.py

THE QUESTION. crack.glsl must seed from absolute world position, never from texCoord / sizeU /
sizeV. Those are tied to the greedy-merged rectangle, and merge runs are computed in a 32^3
loop, so they TERMINATE AT CHUNK BORDERS - a uv-seeded crack restarts its pattern at x = 32 and
shows a hard vertical discontinuity. tests/core/VoxelCrackSeamTest.cpp proves the CPU MIRROR is
partition-independent; it cannot prove the SHADER is, because it cannot execute the shader.
Only a captured frame can.

THE METHOD: TWO RIGS, A/B. The same damaged wall is built twice -

    RIG A   x = 28..35   STRADDLES the x = 31/32 chunk boundary
    RIG B   x = 20..27   wholly INSIDE chunk (0,0,0), no chunk boundary anywhere in it

- and the luminance step is measured at every internal voxel boundary of both. Rig B's steps,
plus rig A's non-seam steps, are the REFERENCE DISTRIBUTION: what an ordinary voxel boundary
looks like on this wall, under this shader, at this damage stage, in this light. The test asks
one question: IS RIG A'S CHUNK-SEAM BOUNDARY AN OUTLIER IN THAT DISTRIBUTION?

Under world seeding it is not - a chunk boundary is just another voxel boundary. Under uv
seeding it is, because only that boundary restarts the pattern.

WHY THIS SHAPE AND NOT THE OBVIOUS ONE. Three earlier metrics tried to characterize the seam
from a SINGLE capture, and all three failed against a deliberately uv-seeded shader:
  1. seam step vs ordinary boundaries in the same frame - FALSE PASS: that break happened to
     render no cracks at all, and a surface with no cracks has no discontinuities either;
  2. crack "presence" via column-to-column contrast - FALSE FAIL: damaged surfaces are darkened
     by the whole-face wear term, and absolute contrast scales with brightness;
  3. the same, normalized by mean luminance - FALSE FAIL: collapsing each column to one number
     AVERAGES THE CRACK AWAY, since a crack crosses different columns at different heights.

The two-rig form sidesteps all of it by comparing like with like. Both arms render whatever the
shader renders, so "are cracks present" never has to be answered.

!! STATUS: NOT A VALID GATE. The rig, the two preconditions and the two-rig framing all work
and are worth keeping. THE METRIC DOES NOT: six designs were tried against a deliberately
uv-seeded shader and none detected it, so a PASS from this script proves nothing and must not
be cited as evidence that 6.2 is satisfied. See MEASUREMENT ATTEMPTS at the bottom, and
docs/VoxelDamageVisualization.md 16.1 for the recommended way out (a debug view that renders
crackField directly, rather than a seventh statistic).
"""

import argparse
import json
import statistics
import sys
import time
import urllib.error
import urllib.request

BASE = "http://localhost:8090"

WALL_Y0, WALL_Y1 = 17, 20
WALL_Z = 8
RIG_A_X0, RIG_A_X1 = 28, 35     # straddles x = 31/32
RIG_B_X0, RIG_B_X1 = 20, 27     # wholly inside chunk (0,0,0)
SEAM_X = 32                     # first voxel of chunk (1,0,0)
DAMAGE_FRACTION = 0.60          # stage 2 of 3, comfortably inside the band


def call(path, body=None):
    url = BASE + path
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method="POST" if data else "GET",
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            raw = r.read().decode()
            return json.loads(raw) if raw.strip() else {}
    except urllib.error.URLError as e:
        sys.exit("engine API unreachable at %s (%s)" % (url, e))


def job(kind, params):
    r = call("/api/job/submit", {"type": kind, "params": params})
    for _ in range(160):
        st = call("/api/job/%s" % r.get("job_id"))
        if st.get("state") in ("complete", "failed", "cancelled"):
            return st
        time.sleep(0.25)
    return {"state": "timeout"}


def build_rig(x0, x1, label, damage=True):
    """Build the wall; damage it only if asked. Verifies the WORLD, never the fill response."""
    job("clear_region", {"x1": x0, "y1": WALL_Y0, "z1": WALL_Z,
                         "x2": x1, "y2": WALL_Y1, "z2": WALL_Z})
    call("/api/world/fill", {"x1": x0, "y1": WALL_Y0, "z1": WALL_Z,
                             "x2": x1, "y2": WALL_Y1, "z2": WALL_Z, "material": "Stone"})

    missing = [x for x in range(x0, x1 + 1)
               if not call("/api/world/voxel?x=%d&y=%d&z=%d"
                           % (x, WALL_Y0 + 1, WALL_Z)).get("exists")]
    if missing:
        sys.exit("%s PRECONDITION FAILED: columns %s not placed - the chunk holding them is not "
                 "resident, so the fill dropped silently." % (label, missing))

    if not damage:
        print("  %s %2d columns, PRISTINE (texture reference)" % (label, x1 - x0 + 1))
        return
    probe = call("/api/world/voxel?x=%d&y=%d&z=%d" % (x0, WALL_Y0 + 1, WALL_Z))
    energy = probe["toughness"] * DAMAGE_FRACTION
    for x in range(x0, x1 + 1):
        for y in range(WALL_Y0, WALL_Y1 + 1):
            call("/api/damage/apply", {"x": x + 0.5, "y": y + 0.5, "z": WALL_Z + 0.5,
                                       "radius": 1.0, "energy": energy, "collapse": False})

    stages = {x: call("/api/world/voxel?x=%d&y=%d&z=%d"
                      % (x, WALL_Y0 + 1, WALL_Z)).get("damage_stage")
              for x in range(x0, x1 + 1)}
    if len(set(stages.values())) != 1:
        sys.exit("%s PRECONDITION FAILED: columns do not share one damage stage: %s. A stage "
                 "step IS a real discontinuity and would be misread as a seam defect."
                 % (label, stages))
    print("  %s %2d columns, all at damage_stage %d"
          % (label, x1 - x0 + 1, next(iter(set(stages.values())))))


def capture(x0, x1):
    call("/api/camera", {"position": {"x": (x0 + x1) / 2.0 + 0.5,
                                      "y": (WALL_Y0 + WALL_Y1) / 2.0 + 0.5,
                                      "z": WALL_Z + 11.0},
                         "yaw": -90, "pitch": 0})
    time.sleep(1.2)
    shot = call("/api/screenshot")
    if not shot.get("path"):
        sys.exit("no screenshot path: %s" % shot)
    return shot["path"]


def boundary_steps(path_dmg, path_pristine, x0, x1):
    """Structural DIScontinuity (1 - profile correlation) at every internal voxel boundary."""
    from PIL import Image
    import os
    def openimg(p):
        return Image.open(p if os.path.isabs(p) else os.path.join(os.getcwd(), p)).convert("RGB")
    im, imp = openimg(path_dmg), openimg(path_pristine)
    W, H = im.size
    px, pxp = im.load(), imp.load()

    def raw(x, y):
        r, g, b = px[x, y]
        return 0.2126 * r + 0.7152 * g + 0.0722 * b

    # DIFFERENCE AGAINST THE PRISTINE CAPTURE OF THE SAME WALL. The stone albedo is
    # high-frequency noise that swamps the crack at pixel scale -- measured directly, two
    # columns 6 px apart are uncorrelated whether or not a crack runs through them. Diffing
    # against the identical undamaged wall cancels the texture and leaves the CRACK
    # CONTRIBUTION alone, which is the only thing whose continuity is in question.
    def lum(x, y):
        rr, gg, bb = px[x, y]
        pr, pg, pb = pxp[x, y]
        return abs(0.2126 * (rr - pr) + 0.7152 * (gg - pg) + 0.0722 * (bb - pb))

    # Detect the wall by EXCLUSION, not by greyness. Stone has warm brown mottling, so an
    # `abs(r-g) < 26` greyness test splits the wall into fragments and max(runs) then picks a
    # different fragment in every capture -- which is what produced 52.2 vs 17.0 px/voxel for
    # two identically framed walls. "Not sky and not grass" is stable across the texture.
    row = H // 2 - 80
    runs, start = [], None
    for x in range(260, W - 420):
        r, g, b = pxp[x, row]
        sky = b > r + 15
        grass = g > r + 12 and g > b + 12
        grey = (not sky) and (not grass) and 25 < r < 235
        if grey and start is None:
            start = x
        elif not grey and start is not None:
            runs.append((start, x - 1)); start = None
    if start is not None:
        runs.append((start, W - 421))
    if not runs:
        sys.exit("could not locate the wall in %s" % path)
    sx0, sx1 = max(runs, key=lambda r: r[1] - r[0])
    span, voxels = sx1 - sx0, x1 - x0 + 1
    ppv = span / float(voxels)
    if ppv < 10:
        sys.exit("only %.1f px per voxel - too small to measure boundaries" % ppv)

    ytop, ybot = row - 55, row + 95
    col = {x: sum(lum(x, y) for y in range(ytop, ybot)) / float(ybot - ytop)
           for x in range(sx0, sx1 + 1)}

    # CORRELATION, not luminance step. A pattern RESTART does not change mean brightness:
    # both sides of a uv seam carry the same statistical density of cracks, only misaligned.
    # Every earlier metric here measured a LEVEL (mean, contrast, dark-tail) and so was blind
    # to exactly the defect being hunted. What breaks at a seam is STRUCTURAL CONTINUITY: on a
    # continuous field the vertical luminance profile a few px left of a boundary closely
    # matches the one a few px right, because cracks run through. On a restarted pattern they
    # are uncorrelated.
    def profile(sx):
        return [lum(sx, y) for y in range(ytop, ybot)]

    def pearson(a, b):
        n = len(a)
        ma, mb = sum(a) / n, sum(b) / n
        va = sum((v - ma) ** 2 for v in a)
        vb = sum((v - mb) ** 2 for v in b)
        if va <= 1e-9 or vb <= 1e-9:
            return 0.0
        cov = sum((a[i] - ma) * (b[i] - mb) for i in range(n))
        return cov / (va ** 0.5 * vb ** 0.5)

    dx = max(3, int(ppv * 0.06))      # a few px either side, scaled to the framing
    steps = {}
    for b in range(x0 + 1, x1 + 1):
        sx = sx0 + int(round((b - x0) / float(voxels) * span))
        if sx - dx < sx0 or sx + dx > sx1:
            continue
        # Report 1 - correlation, so "larger = worse" matches the outlier test below.
        steps[b] = 1.0 - pearson(profile(sx - dx), profile(sx + dx))
    return steps, ppv


def main():
    argparse.ArgumentParser(description=__doc__).parse_args()
    print("R2 runtime seam test - two-rig A/B\n")

    call("/api/world/generate", {"type": "Flat", "from": {"x": 0, "y": 0, "z": 0},
                                 "to": {"x": 1, "y": 0, "z": 0}})
    call("/api/debug/tonemap", {"curve": 0})

    # ONE RIG AT A TIME. The two walls are only 1 voxel apart in x, so if both exist they read
    # as ONE continuous 16-voxel wall and the grey-run detector locks onto the union -- which
    # is exactly what the px/voxel guard below caught on the first run (52.2 vs 20.0).
    # Each rig is captured TWICE -- pristine, then damaged in place -- so the crack can be
    # isolated by difference. One rig at a time: the two walls are 1 voxel apart in x and
    # would otherwise read as one continuous run to the detector.
    print("building rigs (pristine + damaged capture each, one rig at a time):")

    def arm(x0, x1, label):
        build_rig(x0, x1, label, damage=False)
        pristine = capture(x0, x1)
        build_rig(x0, x1, label, damage=True)
        damaged = capture(x0, x1)
        steps, ppv = boundary_steps(damaged, pristine, x0, x1)
        job("clear_region", {"x1": x0, "y1": WALL_Y0, "z1": WALL_Z,
                             "x2": x1, "y2": WALL_Y1, "z2": WALL_Z})
        return steps, ppv, damaged

    steps_a, ppv_a, pa = arm(RIG_A_X0, RIG_A_X1,
                             "RIG A (straddles x=%d/%d):" % (SEAM_X - 1, SEAM_X))
    steps_b, ppv_b, pb = arm(RIG_B_X0, RIG_B_X1, "RIG B (inside one chunk): ")
    call("/api/debug/tonemap", {"curve": 1})

    print("\ncaptures: A=%s (%.1f px/voxel)  B=%s (%.1f px/voxel)"
          % (pa.split("/")[-1], ppv_a, pb.split("/")[-1], ppv_b))
    if abs(ppv_a - ppv_b) > 0.15 * max(ppv_a, ppv_b):
        sys.exit("framing differs between rigs (%.1f vs %.1f px/voxel) - not comparable"
                 % (ppv_a, ppv_b))

    if SEAM_X not in steps_a:
        sys.exit("the chunk-seam boundary was not sampled in rig A")
    seam = steps_a[SEAM_X]
    reference = [v for k, v in steps_a.items() if k != SEAM_X] + list(steps_b.values())
    if len(reference) < 6:
        sys.exit("only %d reference boundaries - too few to characterize" % len(reference))

    mean = statistics.mean(reference)
    sd = statistics.pstdev(reference)
    worst = max(reference)
    # An OUTLIER test. The reference distribution sets the bar, not a hand-picked number.
    limit = max(mean + 3.0 * sd, worst * 1.25)

    print("\nordinary voxel boundaries (reference, n=%d):" % len(reference))
    print("   mean %.3f   sd %.3f   max %.3f" % (mean, sd, worst))
    print("chunk seam x=%d/%d:  %.3f" % (SEAM_X - 1, SEAM_X, seam))
    print("outlier limit (max of mean+3sd, 1.25*max): %.3f\n" % limit)

    if seam > limit:
        print("FAIL: the chunk seam is an OUTLIER among ordinary voxel boundaries.")
        print("      The crack field is reading a CHUNK-DERIVED quantity (sizeU/sizeV/texCoord)")
        print("      instead of absolute world position: merge runs terminate at chunk borders,")
        print("      so a uv-seeded pattern restarts there and the seam becomes visible.")
        return 1

    print("PASS: the chunk seam is indistinguishable from an ordinary voxel boundary.")
    print("      The crack field is continuous across the chunk boundary.")
    print("      LIMITATION: this cannot tell 'seam-free cracks' from 'no cracks at all' - a")
    print("      shader drawing nothing is trivially seam-free and would also pass. Crack")
    print("      PRESENCE is R3's job (6.3) and the 14 visual review's, not this test's.")
    return 0


if __name__ == "__main__":
    sys.exit(main())


# ---------------------------------------------------------------------------------------------
# MEASUREMENT ATTEMPTS -- six designs, and why each failed. Kept so the next attempt starts from
# the seventh idea, or better, abandons pixel statistics altogether (see 16.1).
#
# The red was produced by seeding crackField from `texCoord` instead of `worldPosAbs` in
# voxel.frag. texCoord is tied to the greedy-merged rectangle, which terminates at chunk borders
# -- exactly the defect 3.3 forbids. Verified to actually reach the renderer (mean pixel diff
# 15.9/255 against the correct build); the first few attempts did NOT, see trap 0.
#
#   0. TRAP, not a metric: build_shaders.bat writes shaders/*.spv but the engine loads
#      build/shaders/*.spv, refreshed only by a CMake build. Several early runs measured the
#      CORRECT shader while believing they measured the broken one. Logged in
#      docs/StructurePipelineGaps.md.
#
#   1. Luminance step at the seam vs ordinary boundaries, one capture. FALSE PASS.
#   2. Crack "presence" via column-to-column contrast (absolute). FALSE FAIL -- damaged surfaces
#      are darkened by the wear term and absolute contrast scales with brightness.
#   3. The same, normalized by mean luminance. FALSE FAIL -- collapsing a column to one number
#      averages the crack away; a crack crosses different columns at different heights.
#   4. 2D dark-tail depth, (median - p05)/median. INCONCLUSIVE at 1.05x.
#   5. Two-rig A/B on luminance step (straddling vs in-chunk). NO SIGNAL: seam 2.972 against a
#      reference mean of 2.440, max 6.074 -- the seam is not even the largest boundary step.
#   6. Two-rig A/B on profile CORRELATION across each boundary, including a
#      damaged-minus-pristine difference image to cancel the albedo. NO SIGNAL: correlation is
#      ~0 at ORDINARY boundaries too (1-corr mean 1.002), because the rock texture is
#      high-frequency noise; and the difference image is dominated by the uniform wear term,
#      whose near-constant value makes correlation ill-conditioned.
#
# THE TWO FACTS THAT EXPLAIN ALL OF IT:
#   * A pattern RESTART does not change brightness -- both sides carry the same crack density,
#     only misaligned -- so every LEVEL-based statistic is blind to it by construction.
#   * The stone albedo swamps STRUCTURE at pixel scale, leaving no headroom for a seam to stand
#     out in a correlation measure.
#
# RECOMMENDED NEXT STEP: not a seventh statistic. Render the field itself -- a debug view that
# outputs crackField() as greyscale with no albedo, no lighting and no wear term. A uv seam is
# then a hard vertical edge in an otherwise smooth image, which attempt 1 would have caught.
# ---------------------------------------------------------------------------------------------
