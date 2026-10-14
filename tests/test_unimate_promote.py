"""Red tests for tools/anim_pipeline/unimate_promote.py (docs/UniMateIntegrationPlan.md M1b).

Fixture: a temp copy of humanoid.anim with a synthetic generated clip `unimate_test_r0`
(channels cloned from the shipped `wave`) plus its two provenance header lines, and a ledger
built from the same data — independent of whatever real unimate_* clips the working tree holds
(those are stripped from the copy first).

  (a) one `pending` entry -> refuse, target byte-identical
  (b) `reject` removes exactly that clip + its clip_meta + unimate_prompt lines, nothing else
  (c) `accept` with promote_to colliding with a shipped name refuses without replace_shipped
  (d) `accept` with a fresh promote_to renames the clip and moves provenance with it
  (e) an unledgered unimate_* clip blocks the run
"""
from __future__ import annotations

import copy
import shutil
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "anim_pipeline"))

from anim_format import AnimFile, parse, write, semantically_equal  # type: ignore  # noqa: E402
import unimate_ledger as L  # type: ignore  # noqa: E402
import unimate_promote as P  # type: ignore  # noqa: E402

HUMANOID = ROOT / "resources" / "animated_characters" / "humanoid.anim"
CLIP = "unimate_test_r0"


@pytest.fixture
def rig(tmp_path):
    dst = tmp_path / "humanoid.anim"
    shutil.copy(HUMANOID, dst)
    af = parse(dst)
    for c in [c for c in af.clips if c.name.startswith("unimate_")]:
        af.remove_clip(c.name)
        af.remove_clip_meta(c.name)
    af.header_comments = [h for h in af.header_comments if not h.startswith(P.PROMPT_HEADER)]
    synth = copy.deepcopy(af.clip("wave"))
    synth.name = CLIP
    af.set_clip(synth)
    af.set_clip_meta(CLIP, {"source": "unimate", "ckpt": 100000, "seed": 10, "cfg": 3.0, "rep": 0})
    af.header_comments.append(f"{P.PROMPT_HEADER} {CLIP} a person waves")
    write(af, dst)
    return dst


@pytest.fixture
def ledger(tmp_path):
    path = tmp_path / "unimate_review.json"
    data = L.load(path)
    entry = L.entry_from_import_report(
        {"provenance": {"prompt": "a person waves", "seed": 10, "cfg": 3.0, "ckpt": 100000, "rep": 0},
         "source": "x.glb", "speed": None, "speed_source": "none", "lint_errors": 0, "lint": []},
        "humanoid.anim")
    L.upsert_pending(data, CLIP, entry)
    L.save(data, path)
    return path


def _run(rig, ledger):
    af = parse(rig)
    data = L.load(ledger)
    p = P.plan(af, data, rig.name)
    if not p["blocked"]:
        P.apply(af, data, p["actions"])
        write(af, rig)
        L.save(data, ledger)
    return p


def _others_equal(before: AnimFile, after: AnimFile, exclude: set):
    for c in before.clips:
        if c.name in exclude:
            continue
        a = AnimFile(bones=before.bones, boxes=before.boxes, clips=[c])
        b = AnimFile(bones=after.bones, boxes=after.boxes, clips=[after.clip(c.name)])
        assert after.clip(c.name) is not None, c.name
        assert semantically_equal(a, b) == [], c.name


def test_a_pending_refuses_and_leaves_file_byte_identical(rig, ledger):
    before = rig.read_bytes()
    p = _run(rig, ledger)
    assert p["blocked"] and "pending" in p["blocked"][0]
    assert rig.read_bytes() == before


def test_b_reject_removes_exactly_that_clip_and_its_two_header_lines(rig, ledger):
    before = parse(rig)
    data = L.load(ledger); L.set_verdict(data, CLIP, "reject", notes="feet drift"); L.save(data, ledger)
    p = _run(rig, ledger)
    assert not p["blocked"]
    after = parse(rig)
    assert after.clip(CLIP) is None
    assert after.clip_meta(CLIP) is None
    assert not any(h.startswith(f"{P.PROMPT_HEADER} {CLIP} ") for h in after.header_comments)
    assert len(after.clips) == len(before.clips) - 1
    assert len(after.header_comments) == len(before.header_comments) - 2
    _others_equal(before, after, {CLIP})
    assert L.load(ledger)["clips"][CLIP]["applied"] == "removed"


def test_c_accept_onto_shipped_name_refuses_without_replace_shipped(rig, ledger):
    before = rig.read_bytes()
    data = L.load(ledger); L.set_verdict(data, CLIP, "accept", promote_to="wave"); L.save(data, ledger)
    p = _run(rig, ledger)
    assert p["blocked"] and "replace a shipped clip" in p["blocked"][0]
    assert rig.read_bytes() == before


def test_d_accept_with_fresh_name_renames_and_moves_provenance(rig, ledger):
    before = parse(rig)
    data = L.load(ledger); L.set_verdict(data, CLIP, "accept", promote_to="wave_variant_b"); L.save(data, ledger)
    p = _run(rig, ledger)
    assert not p["blocked"]
    after = parse(rig)
    assert after.clip(CLIP) is None and after.clip("wave_variant_b") is not None
    meta = after.clip_meta("wave_variant_b")
    assert meta["source"] == "unimate" and meta["promoted_from"] == CLIP
    assert any(h == f"{P.PROMPT_HEADER} wave_variant_b a person waves" for h in after.header_comments)
    # the renamed clip's keys are the original's keys, untouched
    renamed = copy.deepcopy(after.clip("wave_variant_b")); renamed.name = CLIP
    assert semantically_equal(AnimFile(bones=before.bones, boxes=before.boxes, clips=[before.clip(CLIP)]),
                              AnimFile(bones=after.bones, boxes=after.boxes, clips=[renamed])) == []
    _others_equal(before, after, {CLIP})
    assert L.load(ledger)["clips"][CLIP]["applied"] == "promoted:wave_variant_b"


def test_e_unledgered_generated_clip_blocks(rig, ledger):
    af = parse(rig)
    stray = copy.deepcopy(af.clip("wave")); stray.name = "unimate_stray_r9"; af.set_clip(stray); write(af, rig)
    data = L.load(ledger); L.set_verdict(data, CLIP, "accept"); L.save(data, ledger)
    before = rig.read_bytes()
    p = _run(rig, ledger)
    assert any("no ledger entry" in b for b in p["blocked"])
    assert rig.read_bytes() == before


def test_ledger_round_trip_is_stable(ledger):
    data = L.load(ledger)
    L.save(data, ledger)
    once = ledger.read_bytes()
    L.save(L.load(ledger), ledger)
    assert ledger.read_bytes() == once
