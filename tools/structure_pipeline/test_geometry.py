"""Tests for the deterministic geometry checks (functional dimensions)."""

import unittest

from structure_pipeline import geometry as G
from structure_pipeline.realize import build_shell, door_leaves_for_width
from structure_pipeline.spec import BuildingSpec


def _two_story(stair_rect=(3, 1, 2, 5)):
    return BuildingSpec.from_dict({
        "kind": "building", "name": "t", "function": "house",
        "palette": {"wall": "StoneBricks", "floor": "Wood", "roof": "Wood"},
        "footprint": [8, 8],
        "stories": [
            {"height": 4, "rooms": [{"id": "g", "rect": [0, 0, 8, 8], "purpose": "living"}],
             "portals": [{"between": ["exterior", "g"], "pos": [3, 0], "width": 1, "height": 2, "kind": "door"}],
             "stairs": [{"from_story": 0, "to_story": 1, "rect": list(stair_rect), "kind": "straight"}],
             "fixtures": []},
            {"height": 4, "rooms": [{"id": "u", "rect": [0, 0, 8, 8], "purpose": "bedroom"}],
             "portals": [], "stairs": [], "fixtures": []},
        ],
        "roof": {"style": "flat", "mat": "Wood"},
    })


class FloatingTests(unittest.TestCase):
    def test_detects_floating(self):
        cells = {(0, 0, 0), (0, 1, 0), (5, 9, 5)}      # last cell floats
        self.assertEqual(G.floating_components(cells), {(5, 9, 5)})

    def test_connected_stack(self):
        cells = {(0, y, 0) for y in range(6)}
        self.assertEqual(G.floating_components(cells), set())


class DoorTilingTests(unittest.TestCase):
    def test_tiling_covers_any_width(self):
        for w in range(1, 7):
            covered = sum(lw for _, _, lw in door_leaves_for_width(w))
            self.assertEqual(covered, w, f"width {w} not fully covered")

    def test_wide_uses_multiple_leaves(self):
        self.assertEqual(len(door_leaves_for_width(4)), 2)   # two 2-wide leaves


class StairClearanceTests(unittest.TestCase):
    def test_valid_stair_passes(self):
        spec = _two_story()
        rep = G.stair_clearance_report(spec, build_shell(spec))
        self.assertTrue(rep.ok, [str(i) for i in rep.errors])

    def test_blocked_stairwell_detected(self):
        # Refill the stairwell hole (simulate the floor-above sealing it) -> must be caught.
        spec = _two_story()
        canvas = build_shell(spec)
        floor_y = spec.stories[0].height + 1
        sx, sz, sw, sd = spec.stories[0].stairs[0].rect
        for x in range(sx, sx + sw):
            for z in range(sz, sz + sd):
                canvas.add_cube(x, floor_y, z, "Wood")
        rep = G.stair_clearance_report(spec, canvas)
        self.assertIn("STAIR_LOW_CLEARANCE", {i.code for i in rep.errors})


class DimensionTests(unittest.TestCase):
    def test_real_furniture_in_range(self):
        for name, kind in (("chair_wood", "chair"), ("table_wood", "table"), ("bed_single", "bed")):
            rep = G.dimension_report(name, kind)
            self.assertTrue(rep.ok, f"{name}: {[str(i) for i in rep.errors]}")

    def test_oversized_chair_flagged(self):
        # bed_single is ~2m long; calling it a 'chair' must trip the height/footprint check
        rep = G.dimension_report("bed_single", "chair")
        self.assertFalse(rep.ok)


class ConnectivityTests(unittest.TestCase):
    def test_generated_furniture_connected(self):
        for name in ("chair_wood", "table_wood", "bed_single"):
            self.assertTrue(G.connectivity_report(name).ok, f"{name} has floating parts")


if __name__ == "__main__":
    unittest.main()
