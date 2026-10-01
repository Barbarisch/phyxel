"""Unit tests for the standalone runtime smoke-test contract."""

import sys
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import smoke_test_standalone as smoke  # noqa: E402


def test_probe_accepts_rendering_standalone():
    errors = smoke.evaluate_probe(
        {"standalone": True, "game": "ReferenceGame"},
        {"screen": "menu"},
        {"visible_chunk_count": 1, "total_visible_faces": 12},
    )
    assert errors == []


def test_probe_rejects_editor_or_empty_render():
    errors = smoke.evaluate_probe(
        {"standalone": False},
        {"screen": "unknown"},
        {"visible_chunk_count": 0, "total_visible_faces": 0},
    )
    assert "did not identify a standalone game" in errors[0]
    assert any("screen state" in error for error in errors)
    assert any("visible chunks" in error for error in errors)
    assert any("world faces" in error for error in errors)
