"""Preset clipping regression gate.

Runs the bone-box interpenetration metric (tools/anim_pipeline/clip_metric.py)
for every shipped appearance preset against humanoid.anim's walk clip and
asserts the overlap DELTA over the standard baseline stays within the
calibrated band. Calibration 2026-07-22 (worst shipped preset: dwarf at
+52.4pt — 145% bulk on short legs; visually acceptable on chunky voxel
bodies): threshold +55pt. A preset edit or new preset that clips WORSE than
today's dwarf fails here instead of shipping bone soup.

Run directly: python tests/test_preset_clipping.py  (from the repo root)
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools" / "anim_pipeline"))

from clip_metric import clip_metric, load_preset, DEFAULT_APPEARANCE  # noqa: E402

ANIM = REPO / "resources" / "animated_characters" / "humanoid.anim"
MAX_DELTA_PT = 55.0


def test_all_presets_within_clipping_band():
    doc = json.loads((REPO / "resources" / "appearance_presets.json").read_text(encoding="utf-8"))
    base = clip_metric(ANIM, DEFAULT_APPEARANCE, "walk")["overlap_pct"]
    failures = []
    for entry in doc["presets"]:
        pid = entry["presetId"]
        pct = clip_metric(ANIM, load_preset(pid), "walk")["overlap_pct"]
        delta = pct - base
        print(f"{pid:12s} overlap={pct:6.2f}%  delta=+{max(delta, 0):.2f}pt")
        if delta > MAX_DELTA_PT:
            failures.append((pid, round(delta, 1)))
    assert not failures, f"presets exceed the calibrated clipping band: {failures}"


if __name__ == "__main__":
    test_all_presets_within_clipping_band()
    print("OK: all presets within the clipping band")
