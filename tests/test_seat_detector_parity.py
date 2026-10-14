"""A4: the C++ seated-pose tests (SeatSolveTest / SeatMatrixStressTest) port the seated detectors of
tools/interaction_pipeline/detectors.py with their calibrated thresholds. Pin the two copies equal
so a retune in one place cannot silently loosen the other (same shape as test_seat_fit_margins.py).
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DETECTORS = ROOT / "tools" / "interaction_pipeline" / "detectors.py"
CPP_FILES = [ROOT / "tests" / "scene" / "motion" / "SeatSolveTest.cpp",
             ROOT / "tests" / "scene" / "motion" / "SeatMatrixStressTest.cpp"]

# C++ constant -> detectors.py keyword default of detect_seated_posture
PAIRS = {
    "kSeatHalfExtent": "seat_half_extent",
    "kHipsYBelowTol": "hips_y_below_tol",
    "kHipsYAboveTol": "hips_y_above_tol",
    "kKneesForwardMin": "knees_forward_min",
    "kKneesYTol": "knees_y_tol",
    "kFeetBelowHipsMin": "feet_below_hips_min",
}


def _py_defaults() -> dict[str, float]:
    text = DETECTORS.read_text(encoding="utf-8")
    body = text[text.index("def detect_seated_posture("):]
    body = body[: body.index(")")]
    return {m.group(1): float(m.group(2)) for m in re.finditer(r"(\w+): float = ([0-9.]+)", body)}


def _cpp_consts(path: Path) -> dict[str, float]:
    # handles both `constexpr float kA = 0.1f;` and `constexpr float kA = 0.1f, kB = 0.2f;`
    text = path.read_text(encoding="utf-8")
    out: dict[str, float] = {}
    for decl in re.finditer(r"constexpr float ([^;]+);", text):
        for m in re.finditer(r"(k\w+)\s*=\s*([0-9.]+)f", decl.group(1)):
            out[m.group(1)] = float(m.group(2))
    return out


def test_cpp_seated_detector_thresholds_match_detectors_py():
    py = _py_defaults()
    for path in CPP_FILES:
        cpp = _cpp_consts(path)
        for c_name, py_name in PAIRS.items():
            if c_name not in cpp:
                continue   # the stress test ports a subset
            assert py_name in py, py_name
            assert abs(cpp[c_name] - py[py_name]) < 1e-9, f"{path.name}: {c_name}={cpp[c_name]} vs {py_name}={py[py_name]}"


def test_position_snap_tolerance_matches_detectors_py():
    text = DETECTORS.read_text(encoding="utf-8")
    m = re.search(r"centroid_jump_threshold: float = ([0-9.]+)", text)
    assert m, "detectors.py must state its clip-boundary snap tolerance (centroid_jump_threshold)"
    snap = float(m.group(1))
    for path in CPP_FILES:
        cpp = _cpp_consts(path)
        assert abs(cpp["kPositionSnapTol"] - snap) < 1e-9, path.name


def test_every_cpp_file_ports_the_core_thresholds():
    for path in CPP_FILES:
        cpp = _cpp_consts(path)
        for name in ("kHipsYBelowTol", "kHipsYAboveTol", "kSeatHalfExtent", "kKneesYTol", "kPositionSnapTol"):
            assert name in cpp, f"{path.name} lacks {name}"
