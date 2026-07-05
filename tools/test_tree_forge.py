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


def test_round_trunk_shifts_cube_to_subcube():
    """round_trunk (ISOLATED, same seed/attractors) must trade cubes for subcubes — the real
    signal, which the raw forest-vs-hero counts hide because hero also adds attractors."""
    _, (nc0, ns0, _), _ = tf.emit(tf.build_tree("oak", 22, 2, round_trunk=False)[0])
    _, (nc1, ns1, _), _ = tf.emit(tf.build_tree("oak", 22, 2, round_trunk=True)[0])
    assert nc1 < nc0 and ns1 > ns0, f"round_trunk should shift C->S: {nc0}C/{ns0}S -> {nc1}C/{ns1}S"


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


def _main():
    tests = [(n, f) for n, f in sorted(globals().items()) if n.startswith("test_")]
    failed = 0
    for name, fn in tests:
        try:
            fn(); print(f"PASS {name}")
        except AssertionError as e:
            failed += 1; print(f"FAIL {name}: {e}")
    print(f"\n{len(tests) - failed}/{len(tests)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(_main())
