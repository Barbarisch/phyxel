#!/usr/bin/env python3
"""Edge-sharpness tests for gen_tree.py (ProceduralTreeExpansionPlan Increment A).

Falsifiable metrics computed on the raw sub-space voxel set (Tree.sub, after
prune_floaters — the exact data that gets emitted), NOT on a rendered image:

  rim_fill        For each leaf-bearing row: the fraction of lattice cells in the
                  outermost 1-sub annulus (dist in (r_max-1, r_max] around the
                  row's leaf centroid) that are filled. A crisp rasterized disc
                  fills its outer ring completely (~1.0). The dithered
                  SOLID..FUZZ shell keeps outer cells probabilistically, so the
                  ring at the true max radius is sparse. Reported as the MINIMUM
                  over rows (a per-tier guarantee): a single bubbly whorl on an
                  otherwise-crisp tree must fail, so averaging would hide it.

  taper_violations  Fraction of row pairs ABOVE the widest row where
                  r_max(y+1) > r_max(y) + 1.0 — a conical crown narrows going
                  up; per-layer jitter + dither makes the measured radius jump
                  upward repeatedly.

CRISP thresholds (an archetype claiming a sharp silhouette must satisfy BOTH):
  rim_fill >= 0.85   and   taper_violations <= 0.10

RED (2026-07-03, pre-implementation): the sharpest tree the current generator
can produce — spruce at fullness 1.0 — measures far below these thresholds
(see test_red_current_generator_cannot_be_crisp). Implementing edge="crisp" +
pine/fir turns test_crisp_archetypes_pass green without touching the red facts.

Run:  python tools/test_tree_sharpness.py            (standalone, exit 1 on fail)
      python -m pytest tools/test_tree_sharpness.py  (if pytest available)
"""

import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_tree  # noqa: E402

CRISP_RIM_FILL = 0.85
CRISP_TAPER_VIOL = 0.10


def build_tree(ttype, height=None, radius=None, fullness=0.85, seed=0, edge="fuzzy"):
    """Replicates generate_one()'s generation path without any file output."""
    spec = gen_tree.ARCHETYPES[ttype]
    h = height or spec["height"]
    rng = random.Random(f"{ttype}:{h}:{radius}:{fullness}:{seed}")
    t = gen_tree.Tree()
    gen_tree.GENERATORS[ttype](t, rng, h, radius, fullness, spec["log"], spec["leaf"],
                               edge="crisp" if ttype in gen_tree.CRISP_TYPES else edge)
    gen_tree.prune_floaters(t)
    return t


def leaf_rows(t, min_cells=4):
    """{row_y: [(sx, sz), ...]} for rows with enough leaf cells to measure."""
    rows = {}
    for (sx, sy, sz), (_, is_log) in t.sub.items():
        if not is_log:
            rows.setdefault(sy, []).append((sx, sz))
    return {y: c for y, c in rows.items() if len(c) >= min_cells}


def sharpness_metrics(t):
    """Returns (rim_fill_mean, taper_violation_fraction). See module docstring."""
    rows = leaf_rows(t)
    if not rows:
        return 0.0, 1.0

    # Fill counts LEAVES ONLY (logs excluded). Counting trunk logs as "filled"
    # would let a bare-trunk pole with a foliage skirt score as crisp — the
    # occupancy-laundering hole the solution-auditor flagged. Leaf-only means the
    # crisp silhouette must be real foliage, and the separate bare-pole test
    # guarantees that foliage is continuous over the trunk for cone archetypes.
    occupied_by_row = {}
    for (sx, sy, sz), (_, is_log) in t.sub.items():
        if not is_log:
            occupied_by_row.setdefault(sy, set()).add((sx, sz))

    fills, r_by_row = [], {}
    for y, cells in rows.items():
        # Center from the occupied BOUNDING BOX, not the mass centroid. The rim is
        # always solid + symmetric, so the bbox spans the full disc and its center
        # is the true disc center — immune to interior density-thinning (which
        # shifts a mass centroid and would fake holes into the edge annulus).
        occ = occupied_by_row[y]
        cx = (min(sx for sx, _ in occ) + max(sx for sx, _ in occ)) / 2.0
        cz = (min(sz for _, sz in occ) + max(sz for _, sz in occ)) / 2.0
        filled = occ
        r_max = max(math.hypot(sx - cx, sz - cz) for (sx, sz) in cells)
        r_by_row[y] = r_max
        if r_max < 2.5:
            continue  # small tiers: too few cells to dither meaningfully, skip
        lo, hit, tot = r_max - 1.0, 0, 0
        for sx in range(int(cx - r_max) - 1, int(cx + r_max) + 2):
            for sz in range(int(cz - r_max) - 1, int(cz + r_max) + 2):
                d = math.hypot(sx - cx, sz - cz)
                if lo < d <= r_max:
                    tot += 1
                    if (sx, sz) in filled:
                        hit += 1
        if tot:
            fills.append(hit / tot)

    # MIN across rows, not mean: one fuzzy tier must sink the whole tree. (A mean
    # lets a single fully-dithered whorl hide behind many crisp ones — that hole
    # was found by the solution-auditor and is what this per-row floor closes.)
    # No qualifying rows = every tier is < 2.5 sub radius (a sapling): there is no
    # edge large enough to dither, so it is trivially crisp (1.0), not a failure.
    rim_fill = min(fills) if fills else 1.0

    ys = sorted(r_by_row)
    widest = max(ys, key=lambda y: r_by_row[y])
    above = [y for y in ys if y >= widest]
    pairs = list(zip(above, above[1:]))
    viol = sum(1 for a, b in pairs if r_by_row[b] > r_by_row[a] + 1.0)
    taper_viol = viol / len(pairs) if pairs else 0.0
    return rim_fill, taper_viol


def is_crisp(t):
    rim, viol = sharpness_metrics(t)
    return rim >= CRISP_RIM_FILL and viol <= CRISP_TAPER_VIOL, rim, viol


def max_bare_gap_in_crown(t):
    """Longest run of LEAF-FREE rows strictly inside the foliage span (lowest to
    highest leaf row). For a CONTINUOUS cone (fir, crisp spruce) this must be ~0:
    a tall trunk-only gap between a spire cap and a lower foliage skirt is the
    'bare pole' defect (the solution-auditor's finding) that the crisp-edge metric
    alone misses, because bare-trunk rows have radius ~1 and get size-skipped.
    NOT applied to pine — its inter-whorl gaps are intentional morphology."""
    leaf_ys = sorted({sy for (_, sy, _), (_, is_log) in t.sub.items() if not is_log})
    if len(leaf_ys) < 2:
        return 0
    present = set(leaf_ys)
    worst = run = 0
    for y in range(leaf_ys[0], leaf_ys[-1] + 1):
        run = 0 if y in present else run + 1
        worst = max(worst, run)
    return worst


def connected_and_nonempty(t):
    """prune_floaters already ran; the tree must still have a trunk and leaves."""
    logs = sum(1 for v in t.sub.values() if v[1])
    leaves = sum(1 for v in t.sub.values() if not v[1])
    return logs > 0 and leaves > 0


# ------------------------------------------------------------------ RED facts

def test_red_current_generator_cannot_be_crisp():
    """The sharpest configurations the pre-crisp generator offers all fail the
    crisp thresholds — this documents the problem being solved. These stay
    true after the fix (fuzzy archetypes remain fuzzy by design), proving the
    metric actually separates the two looks."""
    for ttype, fullness in (("spruce", 1.0), ("spruce", 0.85), ("oak", 1.0)):
        t = build_tree(ttype, fullness=fullness, seed=8)
        ok, rim, viol = is_crisp(t)
        print(f"  [red] {ttype} fullness={fullness}: rim_fill={rim:.3f} "
              f"taper_viol={viol:.3f} crisp={ok}")
        assert not ok, (f"{ttype}@{fullness} unexpectedly passes crisp thresholds "
                        f"(rim={rim:.3f}, viol={viol:.3f}) — metric is too weak")


# ------------------------------------------------------------- GREEN + stress

CRISP_TYPES = ("pine", "fir")


def test_crisp_archetypes_pass():
    """RED until pine/fir exist; GREEN after. Library-scale sizes, a few seeds."""
    for ttype in CRISP_TYPES:
        assert ttype in gen_tree.ARCHETYPES, f"archetype '{ttype}' not implemented yet"
        for h in (8, 12, 18):
            for seed in (0, 3, 7):
                t = build_tree(ttype, height=h, seed=seed)
                ok, rim, viol = is_crisp(t)
                print(f"  [green] {ttype} h={h} s={seed}: rim_fill={rim:.3f} "
                      f"taper_viol={viol:.3f}")
                assert ok, f"{ttype} h={h} seed={seed}: rim={rim:.3f} viol={viol:.3f}"
                assert connected_and_nonempty(t)


def test_spruce_edge_crisp():
    """spruce honors edge=crisp: same archetype, hard silhouette (no jitter/dither)."""
    for h, seed in ((6, 7), (9, 8), (13, 9)):
        t = build_tree("spruce", height=h, seed=seed, edge="crisp")
        ok, rim, viol = is_crisp(t)
        print(f"  [green] spruce(crisp) h={h} s={seed}: rim_fill={rim:.3f} "
              f"taper_viol={viol:.3f}")
        assert ok, f"spruce crisp h={h}: rim={rim:.3f} viol={viol:.3f}"
        assert connected_and_nonempty(t)


def test_stress_height_axis():
    """Scaling axis: height 4 -> 40 (Scots pine mature 25-35 m; 1 cube = 1 m per
    DimensionReference.md; 40 = beyond-max margin). Crisp invariant + connectivity
    must hold at EVERY height, not just the library sizes."""
    for ttype in CRISP_TYPES:
        if ttype not in gen_tree.ARCHETYPES:
            return  # red phase: covered by test_crisp_archetypes_pass
        for h in range(4, 41):
            t = build_tree(ttype, height=h, seed=1)
            ok, rim, viol = is_crisp(t)
            assert ok, f"{ttype} h={h}: rim={rim:.3f} viol={viol:.3f}"
            assert connected_and_nonempty(t), f"{ttype} h={h}: lost trunk or leaves"


def _leaf_count(t):
    return sum(1 for v in t.sub.values() if not v[1])


def test_stress_fullness_axis():
    """Fullness thins the crown INTERIOR, never the silhouette edge. Two things
    must BOTH hold across the sweep, or the axis is meaningless:
      (a) crisp per-row invariant holds at every fullness (edge never erodes);
      (b) fullness is actually LIVE — a sparse crown has strictly fewer leaves
          than a dense one. Without (b) this test is vacuous (the exact hole the
          solution-auditor flagged: a dead parameter swept for show)."""
    for ttype in CRISP_TYPES:
        if ttype not in gen_tree.ARCHETYPES:
            return
        counts = {}
        for f10 in range(3, 11):
            t = build_tree(ttype, height=14, fullness=f10 / 10.0, seed=2)
            ok, rim, viol = is_crisp(t)
            assert ok, f"{ttype} fullness={f10/10}: rim={rim:.3f} viol={viol:.3f}"
            counts[f10 / 10.0] = _leaf_count(t)
        # (b) live parameter: dense crown must out-populate the sparse one.
        assert counts[1.0] > counts[0.3], (
            f"{ttype}: fullness is a DEAD parameter — leaf count 0.3={counts[0.3]} "
            f"vs 1.0={counts[1.0]} (must strictly increase or the axis is vacuous)")
        print(f"  [green] {ttype} fullness live: leaves 0.3->{counts[0.3]} "
              f"1.0->{counts[1.0]}, crisp held")


def test_continuous_cones_have_no_bare_pole():
    """fir + crisp spruce must be CONTINUOUS: foliage unbroken from crown base to
    spire (max bare-trunk gap <= 1 sub) at every library-ish height. The pre-fix
    fir failed this — its narrow upper cone collapsed into the plus-trunk, leaving
    a bare pole between a tiny top cap and the foliage skirt."""
    for ttype in ("fir",):
        for h in (10, 14, 18, 24):
            t = build_tree(ttype, height=h, seed=44)
            gap = max_bare_gap_in_crown(t)
            print(f"  [green] {ttype} h={h}: max bare-trunk gap in crown = {gap} subs")
            assert gap <= 1, f"{ttype} h={h}: bare-pole gap of {gap} subs inside crown"
    for h in (9, 13):
        t = build_tree("spruce", height=h, seed=46, edge="crisp")
        gap = max_bare_gap_in_crown(t)
        print(f"  [green] spruce(crisp) h={h}: max bare-trunk gap in crown = {gap} subs")
        assert gap <= 1, f"crisp spruce h={h}: bare-pole gap of {gap} subs"


def test_bare_pole_guard_is_falsifiable():
    """Prove the continuity check has teeth: sabotage fir so a mid vertical band
    of the cone emits nothing, and assert max_bare_gap_in_crown flags it. Without
    this, a green continuity test could just mean the metric never fires."""
    orig = gen_tree.crisp_disc

    def holed(t, cy, r, mat, cx=1, cz=1, rng=None, fullness=1.0):
        if 18 <= cy <= 26:          # punch a bare band into the cone
            return
        return orig(t, cy, r, mat, cx, cz, rng, fullness)

    gen_tree.crisp_disc = holed
    try:
        t = build_tree("fir", height=18, seed=44)
    finally:
        gen_tree.crisp_disc = orig
    gap = max_bare_gap_in_crown(t)
    print(f"  [guard] fir with punched band: max bare gap = {gap} subs")
    assert gap > 1, f"continuity check missed an injected bare band (gap={gap})"


def test_per_row_metric_catches_single_fuzzy_tier():
    """The metric REGRESSION guard. Monkeypatch crisp_disc so one whorl of a pine
    is fully dithered (50% dropout across the whole disc, not just the rim), then
    assert is_crisp() reports FALSE. This is the exact exploit the solution-
    auditor demonstrated slipping past the old mean-based rim_fill (it scored
    0.923 PASS); the per-row minimum must now reject it."""
    import random as _random
    orig = gen_tree.crisp_disc
    state = {"n": 0}

    def sabotaged(t, cy, r, mat, cx=1, cz=1, rng=None, fullness=1.0):
        state["n"] += 1
        if state["n"] == 2 and r >= 2.5:  # dither the 2nd sizeable tier
            rr = _random.Random(1)
            ir = int(r) + 1
            for sx in range(cx - ir, cx + ir + 1):
                for sz in range(cz - ir, cz + ir + 1):
                    if math.hypot(sx - cx, sz - cz) <= r and rr.random() < 0.5:
                        t.leaf(sx, cy, sz, mat)
            return
        return orig(t, cy, r, mat, cx, cz, rng, fullness)

    gen_tree.crisp_disc = sabotaged
    try:
        t = build_tree("pine", height=16, seed=3)
    finally:
        gen_tree.crisp_disc = orig
    ok, rim, viol = is_crisp(t)
    print(f"  [guard] pine with one fuzzy tier: rim_fill(min)={rim:.3f} crisp={ok}")
    assert not ok, (f"per-row metric FAILED to catch a bubbly tier "
                    f"(rim_fill min={rim:.3f} >= 0.85) — the averaging hole is back")


def _main():
    tests = [(n, f) for n, f in sorted(globals().items()) if n.startswith("test_")]
    failed = 0
    for name, fn in tests:
        try:
            fn()
            print(f"PASS {name}")
        except AssertionError as e:
            failed += 1
            print(f"FAIL {name}: {e}")
    print(f"\n{len(tests) - failed}/{len(tests)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(_main())
