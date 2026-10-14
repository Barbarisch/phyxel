"""Gates on tools/anim_pipeline/unimate_import.py (docs/UniMateIntegrationPlan.md M1).

Fixture: a Mixamo FBX with a skinned mesh converted to GLB through FBX2glTF, the
same round trip a UniMate stage-5 export takes. Skipped when FBX2glTF is absent.

  * determinism  — importing the same GLB twice yields semantically equal clips
  * name gate    — a name without the unimate_ prefix is REFUSED without --force,
                   so a draft can never replace shipped mocap
  * provenance   — clip_meta source=unimate + a separate prompt header line land
                   in the target and survive a parse/write round trip
  * untouched    — every other clip and header line is byte-identical
"""
from __future__ import annotations

import functools
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "anim_pipeline"))

from anim_format import parse, write, semantically_equal  # type: ignore  # noqa: E402
import unimate_import as ui  # type: ignore  # noqa: E402

SRC_FBX = ROOT / "resources" / "mixamo_imports" / "Head Hit.fbx"
HUMANOID = ROOT / "resources" / "animated_characters" / "humanoid.anim"


@pytest.fixture(scope="module")
def glb(tmp_path_factory):
    exe = shutil.which("FBX2glTF")
    if exe is None or not SRC_FBX.exists():
        pytest.skip("FBX2glTF or the Mixamo fixture is not available")
    out = tmp_path_factory.mktemp("glb") / "head_hit"
    subprocess.run([exe, "-i", str(SRC_FBX), "-o", str(out), "--binary"],
                   check=True, capture_output=True)
    return out.with_suffix(".glb")


@pytest.fixture
def target(tmp_path, monkeypatch):
    dst = tmp_path / "humanoid.anim"
    shutil.copy(HUMANOID, dst)
    # never let a test write the REAL review ledger
    monkeypatch.setattr(ui.unimate_ledger, "DEFAULT_LEDGER", tmp_path / "unimate_review.json")
    monkeypatch.setattr(ui, "import_clip", functools.partial(ui.import_clip,
                                                              ledger_path=tmp_path / "unimate_review.json"))
    return dst


def test_refuses_unprefixed_name_without_force(glb, target):
    before = target.read_bytes()
    with pytest.raises(ui.ImportRefused, match="unimate_"):
        ui.import_clip(glb, target, "walk")
    assert target.read_bytes() == before


def test_import_is_deterministic_and_leaves_rest_untouched(glb, target):
    original = parse(target)
    r1 = ui.import_clip(glb, target, "unimate_probe_r0", prompt="a person is hit on the head",
                        seed=10, cfg=3.0, ckpt=100000, rep=0)
    assert r1["written"] and r1["match_ratio"] == 1.0, r1
    first = parse(target)
    r2 = ui.import_clip(glb, target, "unimate_probe_r0", prompt="a person is hit on the head",
                        seed=10, cfg=3.0, ckpt=100000, rep=0)
    assert r2["replaced_existing"]
    second = parse(target)
    assert semantically_equal(first, second) == []

    # every pre-existing clip is unchanged
    for c in original.clips:
        assert semantically_equal(
            _single(original, c.name), _single(second, c.name)) == [], c.name
    assert len(second.clips) == len(original.clips) + 1

    # provenance
    meta = second.clip_meta("unimate_probe_r0")
    assert meta and meta["source"] == "unimate" and int(meta["ckpt"]) == 100000
    assert any(h.startswith("# unimate_prompt: unimate_probe_r0 a person is hit")
               for h in second.header_comments)
    # and it survives a write/parse round trip byte-for-byte
    again = parse(target)
    write(again, target)
    assert semantically_equal(parse(target), second) == []


def test_in_place_source_gets_no_speed(glb, target):
    r = ui.import_clip(glb, target, "unimate_probe_r0", dry_run=True)
    assert r["written"] is False
    assert r["speed"] is None and r["speed_source"].startswith("none")
    assert r["root_xz_stripped"] is False


def _single(af, name):
    from anim_format import AnimFile
    return AnimFile(header_comments=[], bones=af.bones, boxes=af.boxes, clips=[af.clip(name)])
