"""Tests for the functional / ergonomic validation pass (Tier A)."""

import unittest

from structure_pipeline.spec import BuildingSpec
from structure_pipeline.playtest import functional_report, classify_purpose


def _spec(stories, footprint=(8, 8)):
    return BuildingSpec.from_dict({
        "kind": "building", "name": "t", "function": "house",
        "palette": {"wall": "StoneBricks", "floor": "Wood", "roof": "Wood"},
        "footprint": list(footprint),
        "stories": stories,
        "roof": {"style": "flat", "mat": "Wood"},
    })


def _story(rooms, portals=None, stairs=None, fixtures=None, height=4):
    return {"height": height, "rooms": rooms, "portals": portals or [],
            "stairs": stairs or [], "fixtures": fixtures or []}


def _codes(rep):
    return {i.code for i in rep.issues}


class RoomErgonomicsTests(unittest.TestCase):
    def test_hall_too_small(self):
        s = _spec([_story([{"id": "h", "rect": [0, 0, 2, 2], "purpose": "great hall"}])])
        self.assertIn("ROOM_TOO_SMALL", _codes(functional_report(s)))

    def test_bedroom_too_narrow(self):
        s = _spec([_story([{"id": "b", "rect": [0, 0, 1, 5], "purpose": "bedroom"}])])
        self.assertIn("ROOM_TOO_NARROW", _codes(functional_report(s)))

    def test_corridor_thin_is_ok(self):
        s = _spec([_story([{"id": "c", "rect": [0, 0, 1, 8], "purpose": "hallway corridor"}])])
        self.assertNotIn("ROOM_TOO_NARROW", _codes(functional_report(s)))
        self.assertNotIn("ROOM_TOO_SMALL", _codes(functional_report(s)))

    def test_normal_bedroom_passes(self):
        s = _spec([_story([{"id": "b", "rect": [0, 0, 3, 3], "purpose": "bedroom"}])])
        self.assertEqual(functional_report(s).errors, [])

    def test_classify(self):
        self.assertEqual(classify_purpose("entry hall with staircase"), "hall")
        self.assertEqual(classify_purpose("Ireena's bedroom"), "bedroom")
        self.assertEqual(classify_purpose("upper corridor west"), "corridor")
        self.assertEqual(classify_purpose("burgomaster study and library"), "study")


class StairTests(unittest.TestCase):
    def _two_story(self, stair_rect):
        return _spec([
            _story([{"id": "ground", "rect": [0, 0, 8, 8], "purpose": "living"}],
                   stairs=[{"from_story": 0, "to_story": 1, "rect": stair_rect, "kind": "straight"}],
                   height=4),
            _story([{"id": "upper", "rect": [0, 0, 8, 8], "purpose": "bedroom"}], height=4),
        ])

    def test_steep_stair_errors(self):
        # climb = 4+1 = 5 cubes, only 2 cubes of run -> unwalkable
        self.assertIn("STAIR_TOO_STEEP", _codes(functional_report(self._two_story([5, 1, 2, 2]))))

    def test_climbable_stair_passes(self):
        # run 5 >= climb 5
        self.assertNotIn("STAIR_TOO_STEEP", _codes(functional_report(self._two_story([5, 1, 2, 5]))))


class DoorSwingTests(unittest.TestCase):
    def _hall(self, fixtures):
        return _spec([_story(
            [{"id": "hall", "rect": [0, 0, 6, 6], "purpose": "hall"}],
            portals=[{"between": ["exterior", "hall"], "pos": [3, 0], "width": 1,
                      "height": 2, "kind": "door"}],
            fixtures=fixtures)], footprint=(6, 6))

    def test_clear_door_passes(self):
        self.assertNotIn("DOOR_SWING_FIXTURE", _codes(functional_report(self._hall([]))))
        self.assertNotIn("DOOR_NO_SWING_CLEARANCE", _codes(functional_report(self._hall([]))))

    def test_fixture_blocks_door(self):
        blocked = self._hall([{"type": "bed", "rect": [3, 0, 1, 2], "room": "hall"}])
        self.assertIn("DOOR_SWING_FIXTURE", _codes(functional_report(blocked)))


class FixtureTests(unittest.TestCase):
    def test_seat_under_table_ok(self):
        s = _spec([_story([{"id": "r", "rect": [0, 0, 6, 6], "purpose": "dining"}], fixtures=[
            {"type": "table", "rect": [1, 1, 4, 2], "room": "r"},
            {"type": "chair", "rect": [1, 1, 1, 1], "room": "r"},
            {"type": "chair", "rect": [4, 1, 1, 1], "room": "r"},
        ])])
        self.assertNotIn("FIXTURE_OVERLAP", _codes(functional_report(s)))

    def test_two_beds_overlap(self):
        s = _spec([_story([{"id": "r", "rect": [0, 0, 6, 6], "purpose": "bedroom"}], fixtures=[
            {"type": "bed", "rect": [1, 1, 3, 2], "room": "r"},
            {"type": "bed", "rect": [2, 1, 3, 2], "room": "r"},
        ])])
        self.assertIn("FIXTURE_OVERLAP", _codes(functional_report(s)))

    def test_fixture_out_of_room(self):
        s = _spec([_story([{"id": "r", "rect": [0, 0, 4, 4], "purpose": "bedroom"}], fixtures=[
            {"type": "bed", "rect": [5, 5, 2, 2], "room": "r"},
        ])])
        self.assertIn("FIXTURE_OUT_OF_ROOM", _codes(functional_report(s)))


class WalkableTests(unittest.TestCase):
    def _build(self, d):
        from structure_pipeline.realize import build_shell
        from structure_pipeline.playtest import walkable_report
        spec = BuildingSpec.from_dict(d)
        return {i.code for i in walkable_report(spec, build_shell(spec)).issues}

    def _two_room(self, fixtures):
        # entry hall + back room, connected by one interior door
        return {
            "kind": "building", "name": "t", "function": "house",
            "palette": {"wall": "StoneBricks", "floor": "Wood", "roof": "Wood"},
            "footprint": [6, 8],
            "stories": [_story(
                [{"id": "front", "rect": [0, 0, 6, 4], "purpose": "hall"},
                 {"id": "back", "rect": [0, 4, 6, 4], "purpose": "bedroom"}],
                portals=[
                    {"between": ["exterior", "front"], "pos": [2, 0], "width": 1, "height": 2, "kind": "door"},
                    {"between": ["front", "back"], "pos": [2, 4], "width": 1, "height": 2, "kind": "door"},
                ],
                fixtures=fixtures)],
            "roof": {"style": "flat", "mat": "Wood"},
        }

    def test_clear_house_walkable(self):
        self.assertNotIn("ROOM_UNREACHABLE_BODY", self._build(self._two_room([])))

    def test_fixture_sealing_door(self):
        # a bed planted right behind the front<->back door seals the back room
        codes = self._build(self._two_room([{"type": "bed", "rect": [2, 5, 1, 2], "room": "back"}]))
        self.assertIn("ROOM_UNREACHABLE_BODY", codes)


class CleanSpecTests(unittest.TestCase):
    def test_known_good_specs_pass(self):
        import json
        from pathlib import Path
        ex = Path(__file__).parent / "examples"
        for name in ("burgomaster", "house_L", "house_2story"):
            p = ex / f"{name}.json"
            if not p.exists():
                continue
            rep = functional_report(BuildingSpec.from_dict(json.loads(p.read_text())))
            self.assertEqual(rep.errors, [], f"{name} should pass: {[str(i) for i in rep.errors]}")


if __name__ == "__main__":
    unittest.main()
