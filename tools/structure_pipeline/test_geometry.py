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


class RoofTests(unittest.TestCase):
    def test_building_is_roofed(self):
        spec = _two_story()
        self.assertTrue(G.roof_coverage_report(spec, build_shell(spec)).ok)

    def test_uncovered_column_flagged(self):
        spec = _two_story()
        canvas = build_shell(spec)
        top = spec.stories[0].height + 1 + spec.stories[1].height + 1
        canvas.fill_micro_box(3 * 9, top * 9, 3 * 9, 9, 400, 9, None)   # carve the roof off a column
        self.assertIn("ROOF_GAP", {i.code for i in G.roof_coverage_report(spec, canvas).errors})


class FixturePlacementTests(unittest.TestCase):
    def _spec_with_fixture(self, rect):
        return BuildingSpec.from_dict({
            "kind": "building", "name": "t", "function": "house",
            "palette": {"wall": "StoneBricks", "floor": "Wood", "roof": "Wood"},
            "footprint": [8, 8],
            "stories": [{"height": 4,
                         "rooms": [{"id": "g", "rect": [0, 0, 8, 8], "purpose": "living"}],
                         "portals": [{"between": ["exterior", "g"], "pos": [3, 0], "width": 1,
                                      "height": 2, "kind": "door"}],
                         "stairs": [],
                         "fixtures": [{"type": "bed", "rect": rect, "facing": "north", "room": "g"}]}],
            "roof": {"style": "flat", "mat": "Wood"},
        })

    def test_clean_fixture_ok(self):
        spec = self._spec_with_fixture([3, 3, 2, 1])      # bed (2x1) well inside
        self.assertTrue(G.fixture_placement_report(spec, build_shell(spec)).ok)

    def test_fixture_in_wall_flagged(self):
        spec = self._spec_with_fixture([0, 3, 2, 1])      # bed starts on the x=0 wall
        codes = {i.code for i in G.fixture_placement_report(spec, build_shell(spec)).errors}
        self.assertTrue(codes & {"FIXTURE_CLIPS_WALL", "FIXTURE_OUT_OF_ROOM"})


class WallBackedTests(unittest.TestCase):
    def _library(self, rect):
        return BuildingSpec.from_dict({
            "kind": "building", "name": "t", "function": "house",
            "palette": {"wall": "StoneBricks", "floor": "Wood", "roof": "Wood"},
            "footprint": [8, 8],
            "stories": [{"height": 4,
                         "rooms": [{"id": "r", "rect": [0, 0, 8, 8], "purpose": "library"}],
                         "portals": [{"between": ["exterior", "r"], "pos": [3, 0], "width": 1,
                                      "height": 2, "kind": "door"}],
                         "stairs": [],
                         "fixtures": [{"type": "bookshelf", "rect": rect, "facing": "south", "room": "r"}]}],
            "roof": {"style": "flat", "mat": "Wood"},
        })

    def test_bookshelf_against_wall_ok(self):
        self.assertTrue(G.wall_backed_report(self._library([2, 1, 1, 1]), build_shell(self._library([2, 1, 1, 1]))).ok)

    def test_bookshelf_stranded_flagged(self):
        spec = self._library([4, 4, 1, 1])
        self.assertIn("FURNITURE_NOT_AGAINST_WALL", {i.code for i in G.wall_backed_report(spec, build_shell(spec)).errors})


class ClutterTests(unittest.TestCase):
    def _study(self, clutter_rect):
        return BuildingSpec.from_dict({
            "kind": "building", "name": "t", "function": "house",
            "palette": {"wall": "StoneBricks", "floor": "Wood", "roof": "Wood"},
            "footprint": [8, 8],
            "stories": [{"height": 4,
                         "rooms": [{"id": "r", "rect": [0, 0, 8, 8], "purpose": "study"}],
                         "portals": [{"between": ["exterior", "r"], "pos": [3, 0], "width": 1,
                                      "height": 2, "kind": "door"}],
                         "stairs": [],
                         "fixtures": [{"type": "desk", "rect": [2, 2, 2, 1], "facing": "south", "room": "r"},
                                      {"type": "candlestick", "rect": clutter_rect, "facing": "north", "room": "r"}]}],
            "roof": {"style": "flat", "mat": "Wood"},
        })

    def test_clutter_on_surface_ok(self):
        self.assertTrue(G.clutter_on_surface_report(self._study([2, 2, 1, 1])).ok)

    def test_clutter_on_floor_flagged(self):
        codes = {i.code for i in G.clutter_on_surface_report(self._study([6, 6, 1, 1])).errors}
        self.assertIn("CLUTTER_NOT_ON_SURFACE", codes)


class CirculationTests(unittest.TestCase):
    def _two_rooms(self, fixtures):
        return BuildingSpec.from_dict({
            "kind": "building", "name": "t", "function": "house",
            "palette": {"wall": "StoneBricks", "floor": "Wood", "roof": "Wood"},
            "footprint": [6, 8],
            "stories": [{"height": 4,
                         "rooms": [{"id": "a", "rect": [0, 0, 6, 4], "purpose": "room"},
                                   {"id": "b", "rect": [0, 4, 6, 4], "purpose": "room"}],
                         "portals": [{"between": ["exterior", "a"], "pos": [2, 0], "width": 1, "height": 2, "kind": "door"},
                                     {"between": ["a", "b"], "pos": [2, 4], "width": 1, "height": 2, "kind": "door"}],
                         "stairs": [], "fixtures": fixtures}],
            "roof": {"style": "flat", "mat": "Wood"},
        })

    def test_clear_room_ok(self):
        self.assertTrue(G.circulation_report(self._two_rooms([]), build_shell(self._two_rooms([]))).ok)

    def test_furniture_wall_blocks(self):
        wall = [{"type": "bookshelf", "rect": [x, 2, 1, 1], "facing": "north", "room": "a"} for x in range(6)]
        spec = self._two_rooms(wall)
        self.assertIn("CIRCULATION_BLOCKED", {i.code for i in G.circulation_report(spec, build_shell(spec)).errors})


class LightTests(unittest.TestCase):
    def _room(self, portals, fixtures):
        return BuildingSpec.from_dict({
            "kind": "building", "name": "t", "function": "house",
            "palette": {"wall": "StoneBricks", "floor": "Wood", "roof": "Wood"},
            "footprint": [6, 6],
            "stories": [{"height": 4, "rooms": [{"id": "r", "rect": [0, 0, 6, 6], "purpose": "cellar"}],
                         "portals": portals, "stairs": [], "fixtures": fixtures}],
            "roof": {"style": "flat", "mat": "Wood"},
        })

    _arch = [{"between": ["exterior", "r"], "pos": [2, 0], "width": 1, "height": 2, "kind": "arch"}]

    def test_windowless_no_fixture_is_dark(self):
        spec = self._room([{"between": ["r", "exterior"], "pos": [0, 2], "width": 1, "height": 2, "kind": "arch"}], [])
        # interior arch to exterior counts as daylight; use an interior-only spec instead:
        dark = self._room([], [])
        self.assertIn("ROOM_NO_LIGHT", {i.code for i in G.light_per_room_report(dark).errors})

    def test_window_lights_room(self):
        spec = self._room([{"between": ["r", "exterior"], "pos": [0, 2], "width": 2, "height": 2, "kind": "window"}], [])
        self.assertTrue(G.light_per_room_report(spec).ok)

    def test_fixture_lights_room(self):
        spec = self._room([], [{"type": "fireplace", "rect": [1, 0, 1, 1], "facing": "south", "room": "r"}])
        self.assertTrue(G.light_per_room_report(spec).ok)


class FurnitureAccessTests(unittest.TestCase):
    def _room(self, facing):
        return BuildingSpec.from_dict({
            "kind": "building", "name": "t", "function": "house",
            "palette": {"wall": "StoneBricks", "floor": "Wood", "roof": "Wood"},
            "footprint": [8, 8],
            "stories": [{"height": 4, "rooms": [{"id": "r", "rect": [0, 0, 8, 8], "purpose": "room"}],
                         "portals": [{"between": ["exterior", "r"], "pos": [3, 0], "width": 1, "height": 2, "kind": "door"}],
                         "stairs": [],
                         "fixtures": [{"type": "wardrobe", "rect": [1, 1, 1, 1], "facing": facing, "room": "r"}]}],
            "roof": {"style": "flat", "mat": "Wood"},
        })

    def test_faces_room_ok(self):
        self.assertTrue(G.furniture_access_report(self._room("east"), build_shell(self._room("east"))).ok)

    def test_faces_wall_flagged(self):
        spec = self._room("west")     # front (-x) is the x=0 wall
        self.assertIn("FURNITURE_FACES_WALL", {i.code for i in G.furniture_access_report(spec, build_shell(spec)).errors})


class StairToDoorTests(unittest.TestCase):
    def test_door_at_stair_flagged(self):
        spec = BuildingSpec.from_dict({
            "kind": "building", "name": "t", "function": "house",
            "palette": {"wall": "StoneBricks", "floor": "Wood", "roof": "Wood"},
            "footprint": [8, 8],
            "stories": [
                {"height": 4,
                 "rooms": [{"id": "a", "rect": [0, 0, 8, 4], "purpose": "hall"},
                           {"id": "b", "rect": [0, 4, 8, 4], "purpose": "hall"}],
                 "portals": [{"between": ["exterior", "a"], "pos": [1, 0], "width": 1, "height": 2, "kind": "door"},
                             {"between": ["a", "b"], "pos": [3, 4], "width": 1, "height": 2, "kind": "door"}],
                 "stairs": [{"from_story": 0, "to_story": 1, "rect": [3, 4, 2, 4], "kind": "straight"}],
                 "fixtures": []},
                {"height": 4, "rooms": [{"id": "u", "rect": [0, 0, 8, 8], "purpose": "bedroom"}],
                 "portals": [], "stairs": [], "fixtures": []}],
            "roof": {"style": "flat", "mat": "Wood"}})
        self.assertIn("STAIR_AT_DOORWAY", {i.code for i in G.stair_to_door_clearance_report(spec).errors})


class ConnectivityTests(unittest.TestCase):
    def test_generated_furniture_connected(self):
        for name in ("chair_wood", "table_wood", "bed_single", "bookshelf", "wall_shelf", "book_stack",
                     "wardrobe", "dresser", "desk", "counter", "fireplace", "candlestick", "goblet",
                     "bottle", "plate", "candelabra", "sconce", "torch", "chandelier"):
            self.assertTrue(G.connectivity_report(name).ok, f"{name} has floating parts")

    def test_shell_is_connected(self):
        spec = _two_story()
        self.assertTrue(G.shell_connectivity_report(spec, build_shell(spec)).ok)


if __name__ == "__main__":
    unittest.main()
