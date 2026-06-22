"""
Structure Pipeline — realize.py tests (offline; no engine).

Validates the building shell builder: faithful/overlap-free export, cube bulk preserved,
fine detail only at openings/coping.

Run: python tools/structure_pipeline/test_realize.py
"""

import json
import os
import sys
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_TOOLS = os.path.dirname(_HERE)
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

from structure_pipeline.spec import BuildingSpec               # noqa: E402
from structure_pipeline.realize import build_shell, _shared_wall, _resolve_portal  # noqa: E402
from structure_pipeline.overlap import find_overlaps            # noqa: E402

EXAMPLES = os.path.join(_HERE, "examples")


def _load(name):
    with open(os.path.join(EXAMPLES, name), encoding="utf-8") as f:
        return BuildingSpec.from_dict(json.load(f))


class GeometryTests(unittest.TestCase):
    def test_shared_wall_adjacent(self):
        # two rooms tiling along z=7
        sw = _shared_wall((0, 0, 10, 7), (0, 7, 10, 5))
        self.assertEqual(sw, ("z", 7, 0, 10))

    def test_shared_wall_none_when_apart(self):
        self.assertIsNone(_shared_wall((0, 0, 4, 4), (6, 0, 4, 4)))

    def test_resolve_exterior_normal(self):
        from types import SimpleNamespace
        p = SimpleNamespace(between=("exterior", "r"), pos=(3, 0), kind="door")
        self.assertEqual(_resolve_portal(p, {"r": (0, 0, 8, 8)}, 8, 8), ("z", 0, "-z"))


class ShellTests(unittest.TestCase):
    def _check(self, spec):
        canvas = build_shell(spec)
        lines = canvas.to_voxel_lines()
        self.assertEqual(find_overlaps(lines), [], "shell must not self-overlap")
        r = canvas.report()
        self.assertGreater(r.cubes, 0, "wall/floor/bulk should stay cubes")
        self.assertGreater(r.subcubes, 0, "frames/roof detail should use subcubes")
        # efficiency: vastly cheaper than naive all-micro
        self.assertLess(r.total_voxels, r.micro_cells * 0.2)

    def test_house_shell(self):
        self._check(_load("house_good.json"))

    def test_shop_shell(self):
        self._check(_load("shop_generated.json"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
