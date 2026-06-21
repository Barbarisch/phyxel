"""Tests for the door library, deterministic selection, and door geometry checks."""

import unittest

from structure_pipeline import doors as D
from structure_pipeline import geometry as G
from structure_pipeline.spec import BuildingSpec
from structure_pipeline.realize import build_shell


class SelectionTests(unittest.TestCase):
    def test_situations(self):
        cases = {
            ("entrance hall", True, 4): "door_grand",
            ("courtyard gate", True, 3): "gate_timber",
            ("barn", False, 2): "gate_timber",
            ("wine cellar", True, 1): "door_iron",
            ("master bedroom", False, 2): "door_plank",   # situation beats the 2-wide opening
            ("master bedroom", True, 1): "door_wood",       # lockable bedroom -> panel (lockable)
            ("dining room", False, 2): "door_wood_wide",
            ("study", False, 1): "door_wood",
        }
        for (purpose, lockable, w), expected in cases.items():
            got = D.select_door(w, purpose, lockable, exterior="entrance" in purpose)
            self.assertEqual(got, expected, f"{purpose!r} w{w} lock={lockable}: {got} != {expected}")

    def test_catalog_consistency(self):
        for name, d in D.DOOR_CATALOG.items():
            self.assertEqual(d.name, name)
            self.assertIn(d.swing, ("kinematic", "free"))
            self.assertGreaterEqual(d.width, 1)


class GeneratedDoorTests(unittest.TestCase):
    def test_generated_doors_connected_and_sized(self):
        for name in D._GENERATED:
            self.assertTrue(G.connectivity_report(name).ok, f"{name} has floating parts")
            d = D.DOOR_CATALOG[name]
            m = G.measure_template(name)
            self.assertIsNotNone(m, f"{name} template missing — run `python -m structure_pipeline.doors`")
            # measured width/height within ~1 cube of the catalog (knobs/studs add a little)
            self.assertLessEqual(abs(m[0] - d.width), 1.2)
            self.assertLessEqual(abs(m[1] - d.height), 1.2)


def _spec_with_door(purpose, lockable, width, height=4):
    return BuildingSpec.from_dict({
        "kind": "building", "name": "t", "function": "house",
        "palette": {"wall": "StoneBricks", "floor": "Wood", "roof": "Wood"},
        "footprint": [8, 8],
        "stories": [{"height": height,
                     "rooms": [{"id": "r", "rect": [0, 0, 8, 8], "purpose": purpose}],
                     "portals": [{"between": ["exterior", "r"], "pos": [3, 0], "width": width,
                                  "height": 2, "kind": "door",
                                  "door": {"lockable": lockable, "key": "k", "swing": 90}}],
                     "stairs": [], "fixtures": []}],
        "roof": {"style": "flat", "mat": "Wood"},
    })


class DoorGeometryTests(unittest.TestCase):
    def test_opening_matches_selected_door(self):
        spec = _spec_with_door("entrance hall", True, 2, height=5)   # -> door_grand 2x3
        self.assertTrue(G.opening_fit_report(spec, build_shell(spec)).ok)
        self.assertTrue(G.door_selection_report(spec).ok)

    def test_lockable_gets_lockable_door(self):
        # a lockable bedroom selects door_wood (panel, lockable) — selection guarantees it
        self.assertEqual(D.select_door(1, "bedroom", lockable=True), "door_wood")
        self.assertTrue(D.DOOR_CATALOG["door_wood"].lockable)


if __name__ == "__main__":
    unittest.main()
