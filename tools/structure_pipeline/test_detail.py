"""
Structure Pipeline — detail canvas tests.

The exporter must be FAITHFUL (same filled cells, same materials) and OVERLAP-FREE
(no two voxels claim the same micro-cell). We verify both by round-tripping and by
running the pipeline's own overlap detector.

Run: python tools/structure_pipeline/test_detail.py
"""

import os
import sys
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_TOOLS = os.path.dirname(_HERE)
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

from structure_pipeline.detail import DetailCanvas, demo_ashlar_pillar  # noqa: E402
from structure_pipeline.overlap import find_overlaps                    # noqa: E402


def cells_from_voxels(voxels):
    """Expand exported C/S/M voxels back to {(gx,gy,gz): mat} micro-cells."""
    out = {}
    for v in voxels:
        if v[0] == "C":
            _, cx, cy, cz, mat = v
            bx, by, bz = cx * 9, cy * 9, cz * 9
            rng = [(bx + i, by + j, bz + k) for i in range(9) for j in range(9) for k in range(9)]
        elif v[0] == "S":
            _, cx, cy, cz, sx, sy, sz, mat = v
            bx, by, bz = cx * 9 + sx * 3, cy * 9 + sy * 3, cz * 9 + sz * 3
            rng = [(bx + i, by + j, bz + k) for i in range(3) for j in range(3) for k in range(3)]
        else:
            _, cx, cy, cz, sx, sy, sz, mx, my, mz, mat = v
            rng = [(cx * 9 + sx * 3 + mx, cy * 9 + sy * 3 + my, cz * 9 + sz * 3 + mz)]
        for cell in rng:
            assert cell not in out, f"overlap at {cell}"
            out[cell] = mat
    return out


class CoarseningTests(unittest.TestCase):
    def test_full_cube_box_is_cubes(self):
        c = DetailCanvas()
        c.fill_cube_box(0, 0, 0, 2, 2, 2, "Stone")
        r = c.report()
        self.assertEqual((r.cubes, r.subcubes, r.microcubes), (8, 0, 0))

    def test_single_subcube(self):
        c = DetailCanvas()
        c.add_subcube(0, 0, 0, 1, 1, 1, "Wood")
        r = c.report()
        self.assertEqual((r.cubes, r.subcubes, r.microcubes), (0, 1, 0))

    def test_single_micro(self):
        c = DetailCanvas()
        c.add_micro(0, 0, 0, 1, 1, 1, 1, 1, 1, "Gold")
        r = c.report()
        self.assertEqual((r.cubes, r.subcubes, r.microcubes), (0, 0, 1))

    def test_mixed_material_cube_drops_to_subcubes(self):
        c = DetailCanvas()
        c.add_cube(0, 0, 0, "Stone")
        c.add_subcube(0, 0, 0, 1, 1, 1, "Gold")   # one subcube re-materialed
        r = c.report()
        self.assertEqual((r.cubes, r.microcubes), (0, 0))
        self.assertEqual(r.subcubes, 27)           # full cube can't merge -> 27 subcubes

    def test_chamfer_introduces_micros(self):
        c = DetailCanvas()
        c.fill_cube_box(0, 0, 0, 2, 1, 2, "Stone")
        before = c.report()
        c.chamfer_edge(0, 0, 0, 18, 9, 18, "x", "+y+z", depth=3)
        after = c.report()
        self.assertEqual(before.microcubes, 0)
        self.assertGreater(after.microcubes, 0)    # the bevel is encoded as microcubes
        self.assertLess(after.total_voxels, after.micro_cells)  # still far cheaper than all-micro


class FidelityTests(unittest.TestCase):
    def _roundtrip(self, c: DetailCanvas):
        recon = cells_from_voxels(c.export_voxels())     # also asserts no overlap
        self.assertEqual(recon, c.cells)                 # faithful: exact same cells + materials

    def test_roundtrip_box(self):
        c = DetailCanvas()
        c.fill_cube_box(0, 0, 0, 3, 2, 1, "Stone")
        self._roundtrip(c)

    def test_roundtrip_pillar(self):
        self._roundtrip(demo_ashlar_pillar())

    def test_pillar_no_overlap_and_uses_all_resolutions(self):
        c = demo_ashlar_pillar()
        self.assertEqual(find_overlaps(c.to_voxel_lines()), [])   # exporter never overlaps
        r = c.report()
        self.assertGreater(r.cubes, 0)         # bulk
        self.assertGreater(r.subcubes, 0)      # molding band
        self.assertGreater(r.microcubes, 0)    # chamfers
        self.assertLess(r.total_voxels, r.micro_cells)  # efficient


if __name__ == "__main__":
    unittest.main(verbosity=2)
