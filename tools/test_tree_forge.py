#!/usr/bin/env python3
"""Tests for tree_forge — the unified multi-resolution tree generator.

The load-bearing property of the whole design: thickness auto-picks resolution via hierarchical
emit. These tests pin that (and later, skeleton/rasterization structure) with falsifiable checks.
"""

import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tree_forge as tf  # noqa: E402


def _skel_params(preset="oak", h=15, seed=1, **ov):
    """Mirror build_tree's param assembly for the skeleton (no tier/roots), so the structural tests
    exercise the SAME parameters the real generator uses."""
    p = tf.default_params(h, seed)
    pr = dict(tf.PRESETS.get(preset, {}))
    for mk, base in tf._MULT_KEYS.items():
        if mk in pr:
            p[base] = p[base] * pr.pop(mk)
    p.update(pr)
    p.update(ov)
    return p


def test_solid_cube_compresses_to_one_cube():
    """A fully-filled uniform cube region must emit exactly ONE C line (not 729 M's)."""
    mv = tf.MicroVoxels()
    for i in range(tf.MICRO_PER_CUBE):
        for j in range(tf.MICRO_PER_CUBE):
            for k in range(tf.MICRO_PER_CUBE):
                mv.set(i, j, k, "Wood")
    _, (nc, ns, nm), _ = tf.emit(mv)
    assert (nc, ns, nm) == (1, 0, 0), f"solid cube should be 1C 0S 0M, got {nc}C {ns}S {nm}M"


def test_full_subcube_compresses_to_one_subcube():
    """A fully-filled uniform subcube (27 micros) inside an otherwise-empty cube -> one S."""
    mv = tf.MicroVoxels()
    # subcube (1,1,1) of cube 0: micro indices 3..5 on each axis
    for i in range(3, 6):
        for j in range(3, 6):
            for k in range(3, 6):
                mv.set(i, j, k, "Leaf")
    _, (nc, ns, nm), _ = tf.emit(mv)
    assert (nc, ns, nm) == (0, 1, 0), f"full subcube should be 0C 1S 0M, got {nc}C {ns}S {nm}M"


def test_thin_line_stays_microcubes():
    """A 1-micro-wide vertical line (a 'twig') must stay as individual M's — the fine detail the
    engine's finest resolution exists for."""
    mv = tf.MicroVoxels()
    for y in range(20):
        mv.set(0, y, 0, "Leaf")   # one micro per layer, never fills a subcube
    _, (nc, ns, nm), _ = tf.emit(mv)
    assert nc == 0 and ns == 0 and nm == 20, f"thin line should be 20 M, got {nc}C {ns}S {nm}M"


def test_mixed_thickness_uses_mixed_resolution():
    """A thick block AND a thin twig together -> the block compresses to a cube, the twig stays
    micro. This is the whole design in one assertion: thickness picks resolution automatically."""
    mv = tf.MicroVoxels()
    for i in range(tf.MICRO_PER_CUBE):          # solid cube at origin
        for j in range(tf.MICRO_PER_CUBE):
            for k in range(tf.MICRO_PER_CUBE):
                mv.set(i, j, k, "Wood")
    for y in range(9, 30):                      # thin twig rising out of the top
        mv.set(4, y, 4, "Leaf")
    _, (nc, ns, nm), _ = tf.emit(mv)
    assert nc == 1, f"thick block should compress to 1 cube, got {nc}"
    assert nm >= 15, f"thin twig should stay as micros, got {nm} M"


def test_negative_coords_bucket_correctly():
    """Micro coords can be negative (branches spread from a centered trunk). Cube bucketing must
    use floor-division so a solid cube at negative coords still compresses to one C."""
    mv = tf.MicroVoxels()
    for i in range(-9, 0):
        for j in range(-9, 0):
            for k in range(-9, 0):
                mv.set(i, j, k, "Wood")
    _, (nc, ns, nm), _ = tf.emit(mv)
    assert (nc, ns, nm) == (1, 0, 0), f"solid cube at negative coords should be 1C, got {nc}C {ns}S {nm}M"


def test_normal_tree_emits_all_three_resolutions():
    """The load-bearing DESIGN claim: a real generated tree uses cubes AND subcubes AND microcubes
    (thickness picks resolution) — not secretly all-M. Fails if multi-res collapses to one kind."""
    mv, _ = tf.build_tree("oak", 15, 1)
    _, (nc, ns, nm), _ = tf.emit(mv)
    assert nc > 0 and ns > 0 and nm > 0, f"expected a C+S+M mix, got {nc}C {ns}S {nm}M"


def _low_spread(nodes, cx, cz, ymax=2.0):
    return max((math.hypot(n["pos"][0] - cx, n["pos"][2] - cz)
                for n in nodes if n["pos"][1] < ymax), default=0.0)


def test_add_roots_extends_beyond_trunk():
    """Root flare must measurably fan the base OUTWARD past the trunk (not collapse inward)."""
    p = _skel_params("oak", 15, 1)
    rng = random.Random("roots")
    nodes = tf.grow_skeleton(rng, p)
    tf.assign_radii(nodes, p)
    cx, cz = nodes[0]["pos"][0], nodes[0]["pos"][2]
    before = _low_spread(nodes, cx, cz)
    tf.add_roots(nodes, rng, p)
    after = _low_spread(nodes, cx, cz)
    assert after > before * 1.8, f"roots should fan out past the trunk: {before:.2f} -> {after:.2f}"


def _stem_deviation(crook):
    p = _skel_params("oak", 15, 1, crook=crook)
    rng = random.Random("crook")
    nodes = tf.grow_skeleton(rng, p)
    cx, cz = nodes[0]["pos"][0], nodes[0]["pos"][2]
    base_y = p["crown_center"][1] - p["canopy_h"] - 0.5   # only the pre-crown trunk stem
    return max((math.hypot(n["pos"][0] - cx, n["pos"][2] - cz)
                for n in nodes if n["pos"][1] < base_y), default=0.0)


def test_crook_zero_is_straight_nonzero_bends():
    """crook=0 must give a ramrod-straight stem (deviation 0); crook>0 must bend it."""
    assert _stem_deviation(0.0) == 0.0, "crook=0 should be perfectly straight"
    assert _stem_deviation(0.65) > 0.3, "crook=0.65 should visibly bend the trunk"


def test_real_trunk_band_has_subcube_shell():
    """The trunk of a REAL tree (bottom 3 cubes, where the user sees it) must carry subcube/micro
    surface detail, not read as pure full-cube stairsteps. (Supersedes the old round_trunk test —
    thick wood now always rasterizes with a subcube shell; emit() keeps the interior as cubes.)"""
    mv, _ = tf.build_tree("oak", 22, 2)
    band = tf.MicroVoxels()
    band.v = {k: m for k, m in mv.v.items() if k[1] < 3 * tf.MICRO_PER_CUBE}
    _, (nc, ns, nm), _ = tf.emit(band)
    assert ns + nm > 0, f"trunk band is pure cubes ({nc}C {ns}S {nm}M) — surface shell missing"
    assert nc > 0, f"trunk band lost its cube interior ({nc}C {ns}S {nm}M) — perf regression"


def test_pine_taller_narrower_than_oak():
    """Profiles must differ STRUCTURALLY (not just materials): pine has a larger height/width
    aspect ratio than oak, every seed."""
    def aspect(preset):
        rs = []
        for s in range(3):
            _, _, (w, h, d) = tf.emit(tf.build_tree(preset, 20, s)[0])
            rs.append(h / max(w, d))
        return sum(rs) / len(rs)
    ao, ap = aspect("oak"), aspect("pine")
    assert ap > ao * 1.15, f"pine should be taller/narrower than oak: oak {ao:.2f} vs pine {ap:.2f}"


def test_determinism():
    """Same (preset,height,seed,tier) -> byte-identical emit."""
    a = tf.emit(tf.build_tree("oak", 15, 1)[0])
    b = tf.emit(tf.build_tree("oak", 15, 1)[0])
    assert a[0] == b[0] and a[1] == b[1], "same seed must be reproducible"


def test_thick_trunk_has_subvoxel_shell():
    """A THICK limb must not alias its curved surface to whole-cube steps: the interior may be
    cubes (cheap, merged) but the SURFACE band must be finer voxels. Rasterize a single fat
    vertical capsule (r=1.5 cubes) and demand a mixed emit: cube interior AND a sub/micro shell."""
    p = tf.default_params(15, 1)
    nodes = [{"pos": (0.0, 0.0, 0.0), "parent": -1, "radius": 1.5, "children": [1]},
             {"pos": (0.0, 10.0, 0.0), "parent": 0, "radius": 1.5, "children": []}]
    mv = tf.MicroVoxels()
    tf.rasterize_branches(nodes, mv, p)
    _, (nc, ns, nm), _ = tf.emit(mv)
    assert nc > 0, f"fat capsule should keep a cube interior, got {nc}C {ns}S {nm}M"
    assert ns + nm > 0, (f"fat capsule surface must be sub/micro (slopes, not 1-cube stairsteps), "
                         f"got {nc}C {ns}S {nm}M")


def test_cli_passes_tier_through():
    """BUG (a): the CLI parses --tier but must actually pass it to build_tree. Baking the same
    (preset,height,seed) at --tier forest vs --tier hero must produce DIFFERENT output (hero adds
    attractors + round trunk), and the provenance header must record the tier for regen."""
    import subprocess
    import tempfile
    script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tree_forge.py")
    with tempfile.TemporaryDirectory() as td:
        outs = {}
        for tier in ("forest", "hero"):
            out = os.path.join(td, f"t_{tier}.voxel")
            r = subprocess.run([sys.executable, script, "--preset", "oak", "--height", "10",
                                "--seed", "3", "--tier", tier, "--out", out],
                               capture_output=True, text=True)
            assert r.returncode == 0, f"CLI failed (--tier {tier}, --out off-repo-drive): {r.stderr.strip()}"
            with open(out, encoding="utf-8") as f:
                outs[tier] = f.read()
    assert outs["forest"] != outs["hero"], "--tier hero must change the baked tree (it was ignored)"
    assert "--tier hero" in outs["hero"], "provenance header must record the tier"


def test_batch_mode_bakes_manifest():
    """Roadmap step 2: --batch <manifest.json> must bake every entry to --outdir with per-entry
    provenance (incl. tier), and re-running must be byte-identical (reproducible library regen)."""
    import json
    import subprocess
    import tempfile
    script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tree_forge.py")
    manifest = [{"name": "t_a", "preset": "oak", "height": 8, "seed": 1},
                {"name": "t_b", "preset": "pine", "height": 8, "seed": 2, "tier": "hero"}]
    with tempfile.TemporaryDirectory() as td:
        mp = os.path.join(td, "lib.json")
        with open(mp, "w", encoding="utf-8") as f:
            json.dump(manifest, f)

        def bake_all():
            r = subprocess.run([sys.executable, script, "--batch", mp, "--outdir", td],
                               capture_output=True, text=True)
            assert r.returncode == 0, f"--batch failed: {r.stderr.strip() or r.stdout.strip()}"
            out = {}
            for e in manifest:
                p = os.path.join(td, e["name"] + ".voxel")
                assert os.path.exists(p), f"batch did not bake {e['name']}"
                with open(p, encoding="utf-8") as f:
                    out[e["name"]] = f.read()
            return out

        first = bake_all()
        assert "--tier hero" in first["t_b"], "batch entry tier missing from provenance header"
        assert "--preset oak" in first["t_a"], "provenance header missing generator flags"
        second = bake_all()
        assert first == second, "batch regen must be byte-identical (determinism)"


def test_no_foliage_on_roots():
    """BUG (b): root-spur tips taper below leaf_below_r and have no children (the density roll never
    applies), so leaf blobs grow at ground level around every trunk base. Foliage must never appear
    on/near the roots: no leaf micros below 2 cubes (canopy base is far higher on an h=15 oak)."""
    mv, _ = tf.build_tree("oak", 15, 1)
    ground_leaves = [k for k, m in mv.v.items() if m == "Leaf" and k[1] < 2 * tf.MICRO_PER_CUBE]
    assert not ground_leaves, f"{len(ground_leaves)} leaf micros near/below ground (root foliage)"


def test_trunk_base_sits_on_template_floor():
    """BUG (c): emit() rebases to the lowest voxel, and roots/base-cap dip below y=0, so the trunk
    ends up floating above the template floor when stamped on terrain. The generator-space ground
    plane (y=0) must BE the floor: no micros below it, and wood present in the bottom micro layers
    (ground contact is trunk/roots, not a rebased leaf blob)."""
    mv, _ = tf.build_tree("oak", 15, 1)
    below = [k for k in mv.v if k[1] < 0]
    assert not below, f"{len(below)} micros below the ground plane (tree will float when placed)"
    bottom_wood = [k for k, m in mv.v.items() if m == "Log" and k[1] < tf.MICRO_PER_CUBE]
    assert bottom_wood, "no wood in the bottom cube layer — nothing contacts the ground"


def _main():
    tests = [(n, f) for n, f in sorted(globals().items()) if n.startswith("test_")]
    failed = 0
    for name, fn in tests:
        try:
            fn(); print(f"PASS {name}")
        except AssertionError as e:
            failed += 1; print(f"FAIL {name}: {e}")
        except Exception as e:                    # an erroring test must not kill the run
            failed += 1; print(f"FAIL {name}: {type(e).__name__}: {e}")
    print(f"\n{len(tests) - failed}/{len(tests)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(_main())
