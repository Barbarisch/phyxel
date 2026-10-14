"""A4 step 1: the seat-fit margins have ONE source, engine/include/scene/SeatFit.h; the Python
sit rules (tools/interaction_pipeline/interaction_kinds/sit.py) must mirror them exactly."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "engine/include/scene/SeatFit.h"
PY = ROOT / "tools/interaction_pipeline/interaction_kinds/sit.py"

PAIRS = {
    "kHipClearance": "_HIP_CLEARANCE",
    "kDepthClearance": "_DEPTH_CLEARANCE",
    "kFootDropMax": "_FOOT_DROP_MAX",
    "kBackrestHeadMax": "_BACKREST_HEAD_MAX",
    "kKneeRiseMax": "_KNEE_RISE_MAX",
}


def _cpp_consts() -> dict[str, float]:
    text = HEADER.read_text(encoding="utf-8")
    return {m.group(1): float(m.group(2)) for m in re.finditer(r"constexpr float (k\w+)\s*=\s*([0-9.]+)f", text)}


def _py_consts() -> dict[str, float]:
    text = PY.read_text(encoding="utf-8")
    return {m.group(1): float(m.group(2)) for m in re.finditer(r"^(_[A-Z_]+)\s*=\s*([0-9.]+)", text, re.M)}


def test_python_sit_margins_mirror_the_engine_header():
    cpp, py = _cpp_consts(), _py_consts()
    for ck, pk in PAIRS.items():
        assert ck in cpp, ck
        assert pk in py, pk
        assert abs(cpp[ck] - py[pk]) < 1e-9, f"{ck}={cpp[ck]} vs {pk}={py[pk]}"
