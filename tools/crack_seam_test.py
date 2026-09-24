#!/usr/bin/env python3
"""R2 runtime half - the crack field must not break at a chunk seam.
docs/VoxelDamageVisualization.md 6.2.

    python tools/crack_seam_test.py

THE QUESTION. crack.glsl must seed from absolute world position, never from texCoord / sizeU /
sizeV. Those are tied to the greedy-merged rectangle, and merge runs terminate at chunk borders,
so a uv-seeded crack RESTARTS its pattern at x = 32. tests/core/VoxelCrackSeamTest.cpp proves
the CPU MIRROR is partition-independent; it cannot prove the SHADER is, because it cannot
execute the shader.

THE METHOD: measure the CRACK FIELD DIRECTLY, via debug view 19 (was 11 until the 2026-09-23 merge collision).

That view renders crackField() as greyscale with no albedo, no lighting and no wear term. It is
the whole reason this test works, and it exists because SIX pixel statistics on the shaded frame
failed to detect a deliberately uv-seeded shader:

  1. luminance step at the seam vs ordinary boundaries      FALSE PASS
  2. crack "presence" via column-to-column contrast         FALSE FAIL
  3. the same, normalized by mean luminance                 FALSE FAIL
  4. 2D dark-tail depth, (median - p05)/median              INCONCLUSIVE (1.05x)
  5. two-rig A/B on luminance step                          NO SIGNAL
  6. two-rig A/B on profile correlation, incl. a
     damaged-minus-pristine difference image                NO SIGNAL

Two facts explain all six, and both are properties of the observable rather than bugs in the
attempts:
  * A pattern RESTART does not change brightness. Both sides carry the same crack density, only
    misaligned - so every LEVEL-based statistic is blind to it by construction.
  * The stone albedo is high-frequency noise that swamps STRUCTURE at pixel scale: profile
    correlation read ~0 at ORDINARY boundaries, leaving no headroom for a seam to stand out.

Stripping albedo and lighting removes both problems at once. On the raw field a uv seam is a
hard vertical edge in an otherwise smooth image, and the very first metric works.

NO DAMAGE IS APPLIED. Debug view 19 renders the field at full strength on every voxel, so the
test needs only a wall - which also removes the stage-equality precondition the shaded version
needed (a damage gradient across the seam is a REAL discontinuity that would be misread).
"""

import argparse
import json
import statistics
import sys
import time
import urllib.error
import urllib.request

BASE = "http://localhost:8090"

WALL_X0, WALL_X1 = 26, 37      # straddles x = 31/32, 6 voxels either side
WALL_Y0, WALL_Y1 = 17, 21
WALL_Z = 8
SEAM_X = 32                    # first voxel of chunk (1,0,0)
CRACK_DEBUG_MODE = 19   # was 11 until the 2026-09-23 merge collision with main's G-18 probe


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


def main():
    argparse.ArgumentParser(description=__doc__).parse_args()
    print("R2 runtime seam test - crack field measured directly (debug view %d)\n"
          % CRACK_DEBUG_MODE)

    # Both chunks. A Flat world generated only for (0,0,0) drops every fill into (1,0,0)
    # SILENTLY, leaving a half-built rig that looks fine and measures nothing.
    call("/api/world/generate", {"type": "Flat", "from": {"x": 0, "y": 0, "z": 0},
                                 "to": {"x": 1, "y": 0, "z": 0}})
    call("/api/world/fill", {"x1": WALL_X0, "y1": WALL_Y0, "z1": WALL_Z,
                             "x2": WALL_X1, "y2": WALL_Y1, "z2": WALL_Z, "material": "Stone"})
    time.sleep(2.0)

    missing = [x for x in range(WALL_X0, WALL_X1 + 1)
               if not call("/api/world/voxel?x=%d&y=%d&z=%d"
                           % (x, WALL_Y0 + 1, WALL_Z)).get("exists")]
    if missing:
        sys.exit("PRECONDITION FAILED: columns %s not placed - the chunk holding them is not "
                 "resident, so the fill dropped silently." % missing)
    print("precondition OK: all %d columns present across both chunks"
          % (WALL_X1 - WALL_X0 + 1))

    r = call("/api/debug/shadow", {"mode": CRACK_DEBUG_MODE})
    if not r.get("success"):
        sys.exit("could not enable debug view %d: %s" % (CRACK_DEBUG_MODE, r))
    call("/api/camera", {"position": {"x": (WALL_X0 + WALL_X1) / 2.0 + 0.5,
                                      "y": (WALL_Y0 + WALL_Y1) / 2.0 + 0.5,
                                      "z": WALL_Z + 11.0},
                         "yaw": -90, "pitch": 0})
    time.sleep(1.5)
    shot = call("/api/screenshot")
    path = shot.get("path")
    if not path:
        sys.exit("no screenshot path: %s" % shot)
    print("captured %s" % path)

    rc = measure(path)
    call("/api/debug/shadow", {"mode": 0})
    return rc


def measure(path):
    from PIL import Image
    import os
    full = path if os.path.isabs(path) else os.path.join(os.getcwd(), path)
    im = Image.open(full).convert("RGB")
    W, H = im.size
    px = im.load()

    def lum(x, y):
        r, g, b = px[x, y]
        return 0.2126 * r + 0.7152 * g + 0.0722 * b

    # The field view is black/white only, so the wall is simply the region that is not the
    # flat editor background. Scan a row through its middle for the widest run with real
    # contrast around it.
    row = H // 2 - 120
    ytop, ybot = row - 90, row + 90
    cand = [x for x in range(260, W - 420)
            if max(lum(x, y) for y in range(row - 40, row + 40, 4)) > 120]
    if len(cand) < 200:
        sys.exit("could not locate the field-rendered wall - is debug view %d active?"
                 % CRACK_DEBUG_MODE)
    sx0, sx1 = min(cand), max(cand)
    span, voxels = sx1 - sx0, WALL_X1 - WALL_X0 + 1
    ppv = span / float(voxels)
    print("wall at screen x %d..%d (%.1f px per voxel)" % (sx0, sx1, ppv))

    # ---------------------------------------------------------------------------------
    # THE CORRECT DETECTOR: chunk-periodic REPETITION, not a seam discontinuity.
    #
    # 3.3 says a uv-seeded crack "would scale and repeat differently on either side of a
    # chunk seam". The REPEAT half is right; the DISCONTINUITY half is not, and chasing it
    # cost seven metric designs. Why: kCrackCell is 1/3, so every merge rectangle spans a
    # WHOLE NUMBER of Voronoi cells. Under uv seeding both rects get cell coordinates 0..N --
    # the same hash inputs -- so the right rect is an EXACT COPY of the left, and the junction
    # lands on a cell boundary in both. There is no step to find. What there IS, is the same
    # pattern twice.
    #
    # So: compare the two half-walls directly. Under world seeding they are different regions
    # of a continuous field and share nothing. Under uv seeding they are pixel-identical.
    mid = sx0 + span // 2
    half = min(mid - sx0, sx1 - mid) - 4
    left = [[lum(mid - half + i, y) for y in range(ytop, ybot)] for i in range(half)]
    right = [[lum(mid + i, y) for y in range(ytop, ybot)] for i in range(half)]
    n = float(half * (ybot - ytop))
    repeat_diff = sum(abs(left[i][j] - right[i][j])
                      for i in range(half) for j in range(ybot - ytop)) / n

    # Control: the same comparison between two strips WITHIN one rect, which are genuinely
    # different parts of the field under either seeding. This is what "different" looks like.
    q = half // 2
    ctrl_a = [[lum(sx0 + 4 + i, y) for y in range(ytop, ybot)] for i in range(q)]
    ctrl_b = [[lum(sx0 + 4 + q + i, y) for y in range(ytop, ybot)] for i in range(q)]
    nc = float(q * (ybot - ytop))
    control_diff = sum(abs(ctrl_a[i][j] - ctrl_b[i][j])
                       for i in range(q) for j in range(ybot - ytop)) / nc

    print("\nmean |difference| between image regions:")
    print("   chunk 0 half  vs  chunk 1 half : %7.3f   <- identical means CHUNK-PERIODIC" % repeat_diff)
    print("   two strips within one chunk    : %7.3f   <- control: what 'different' looks like"
          % control_diff)

    if repeat_diff < control_diff * 0.25:
        print("\nFAIL: the two chunks render the SAME crack pattern.")
        print("      The field is seeded from a CHUNK-DERIVED quantity (texCoord/sizeU/sizeV),")
        print("      so each greedy-merge rectangle restarts the lattice at 0 and every rect of")
        print("      equal size is an exact copy. Chunk identity is visible as a repeat.")
        return 1

    print("\nPASS: the two chunks render DIFFERENT regions of one continuous field")
    print("      (%.1fx the control's difference). The crack is seeded from absolute world"
          % (repeat_diff / max(control_diff, 1e-6)))
    print("      position, so chunk boundaries leave no trace.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
