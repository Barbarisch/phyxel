"""
Structure Pipeline P0 — unit tests (stdlib unittest, no deps).

Run from anywhere:
    python tools/structure_pipeline/test_structure_pipeline.py
or:
    python -m unittest tools.structure_pipeline.test_structure_pipeline
"""

import os
import sys
import unittest
from pathlib import Path

# Make the package importable when run as a plain script.
_HERE = os.path.dirname(os.path.abspath(__file__))      # tools/structure_pipeline
_TOOLS = os.path.dirname(_HERE)                          # tools
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

from structure_pipeline import (  # noqa: E402
    BuildingSpec, load_canon, validate, validate_dict, validate_file,
)

EXAMPLES = Path(_HERE) / "examples"


def _spec(**story0):
    """Minimal single-story building dict with sensible defaults, overridable per call."""
    story = {"height": 4, "rooms": [], "portals": [], "stairs": [], "fixtures": []}
    story.update(story0)
    return {
        "function": "house",
        "footprint": [8, 8],
        "stories": [story],
        "roof": {},
    }


class CanonTests(unittest.TestCase):
    def test_canon_loads_and_derives(self):
        canon = load_canon()
        self.assertAlmostEqual(canon.character_height, 1.751, places=2)
        # Derived integer clearances anchored to the measured height.
        self.assertEqual(canon.ceiling_min, 2)
        self.assertEqual(canon.door_clear_min, 2)
        self.assertEqual(canon.ceiling_comfortable, 3)
        self.assertEqual(canon.door_width_min, 1)


class ExampleFixtureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.canon = load_canon()

    def test_good_house_is_clean(self):
        rep = validate_file(EXAMPLES / "house_good.json", self.canon)
        self.assertTrue(rep.ok, rep.summary())
        self.assertEqual(rep.errors, [], rep.summary())
        self.assertEqual(rep.warnings, [], rep.summary())

    def test_broken_house_reports_expected_errors(self):
        rep = validate_file(EXAMPLES / "house_broken.json", self.canon)
        self.assertFalse(rep.ok)
        expected = {
            "CEILING_TOO_LOW", "ROOM_OVERLAP", "ROOM_OUT_OF_BOUNDS",
            "DOOR_TOO_SHORT", "ROOMS_NOT_ADJACENT", "ROOM_UNREACHABLE",
        }
        self.assertTrue(expected.issubset(set(rep.codes)),
                        f"missing {expected - set(rep.codes)}\n{rep.summary()}")


class ValidatorRuleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.canon = load_canon()

    def _validate(self, d):
        return validate_dict(d, self.canon)

    def test_no_entrance(self):
        d = _spec(
            rooms=[{"id": "r", "rect": [0, 0, 6, 6]}],
            portals=[{"between": ["r", "exterior"], "pos": [0, 2],
                      "width": 2, "height": 2, "kind": "window"}],
        )
        d["footprint"] = [6, 6]
        rep = self._validate(d)
        self.assertIn("NO_ENTRANCE", rep.codes)
        self.assertIn("ROOM_UNREACHABLE", rep.codes)

    def test_portal_taller_than_room(self):
        d = _spec(
            rooms=[{"id": "r", "rect": [0, 0, 6, 6]}],
            portals=[{"between": ["exterior", "r"], "pos": [2, 0],
                      "width": 2, "height": 5, "kind": "door"}],
        )
        d["footprint"] = [6, 6]
        rep = self._validate(d)
        self.assertIn("PORTAL_TALLER_THAN_ROOM", rep.codes)

    def test_door_too_short(self):
        d = _spec(
            rooms=[{"id": "r", "rect": [0, 0, 6, 6]}],
            portals=[{"between": ["exterior", "r"], "pos": [2, 0],
                      "width": 2, "height": 1, "kind": "door"}],
        )
        d["footprint"] = [6, 6]
        rep = self._validate(d)
        self.assertIn("DOOR_TOO_SHORT", rep.codes)

    def test_unknown_function_warns_only(self):
        d = _spec(
            rooms=[{"id": "r", "rect": [0, 0, 8, 8]}],
            portals=[{"between": ["exterior", "r"], "pos": [3, 0],
                      "width": 2, "height": 3, "kind": "door"}],
        )
        d["function"] = "wizard_lair"
        rep = self._validate(d)
        self.assertIn("UNKNOWN_FUNCTION", rep.codes)
        self.assertTrue(rep.ok)  # warning only

    def test_simple_house_clean(self):
        d = _spec(
            rooms=[{"id": "r", "rect": [0, 0, 8, 8]}],
            portals=[{"between": ["exterior", "r"], "pos": [3, 0],
                      "width": 2, "height": 3, "kind": "door"}],
        )
        rep = self._validate(d)
        self.assertTrue(rep.ok, rep.summary())
        self.assertEqual(rep.warnings, [], rep.summary())


class MultiStoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.canon = load_canon()

    def _two_story(self, with_stair):
        story0 = {
            "height": 4,
            "rooms": [{"id": "ground", "rect": [0, 0, 8, 8]}],
            "portals": [{"between": ["exterior", "ground"], "pos": [3, 0],
                         "width": 2, "height": 3, "kind": "door"}],
            "stairs": ([{"from_story": 0, "to_story": 1, "rect": [5, 5, 2, 2], "kind": "straight"}]
                       if with_stair else []),
            "fixtures": [],
        }
        story1 = {
            "height": 4,
            "rooms": [{"id": "upper", "rect": [0, 0, 8, 8]}],
            "portals": [], "stairs": [], "fixtures": [],
        }
        return {"function": "house", "footprint": [8, 8],
                "stories": [story0, story1], "roof": {}}

    def test_stair_makes_upper_reachable(self):
        rep = validate_dict(self._two_story(with_stair=True), self.canon)
        self.assertTrue(rep.ok, rep.summary())

    def test_missing_stair_leaves_upper_unreachable(self):
        rep = validate_dict(self._two_story(with_stair=False), self.canon)
        self.assertIn("ROOM_UNREACHABLE", rep.codes)


class SerializationTests(unittest.TestCase):
    def test_roundtrip_preserves_spec(self):
        import json
        d = json.loads((EXAMPLES / "house_good.json").read_text(encoding="utf-8"))
        once = BuildingSpec.from_dict(d).to_dict()
        twice = BuildingSpec.from_dict(once).to_dict()
        self.assertEqual(once, twice)
        # Door data survives the round-trip.
        portal0 = once["stories"][0]["portals"][0]
        self.assertEqual(portal0["door"]["key"], "cottage_key")
        self.assertTrue(portal0["door"]["lockable"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
