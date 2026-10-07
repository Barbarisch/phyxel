"""Roadmap R2 (docs/CharacterAnimationRoadmap.md): Meshy -> voxel character pipeline, offline.

Red first (2026-10-01): tools/characters/{voxelize,palette,spec_complete,binder,character_add}
do not exist. Fixtures are the synthetic textured stand-ins from make_fixture_models.py; no test
here touches the Meshy API or spends credits."""
from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tools" / "anim_pipeline"))
FIX = ROOT / "tests" / "fixtures" / "characters"
# a frozen pre-completion bear spec: the live specs gain parts as imports complete them
NOJAW_BEAR = FIX / "bear_spec_nojaw.json"

from tools.characters import voxelize, palette, spec_complete, binder, character_add  # noqa: E402


# ---------------------------------------------------------------------------------------------
# decision 1 + 8: pitch and budget follow D&D size; the budget wins
# ---------------------------------------------------------------------------------------------
def test_size_table_matches_the_roadmap():
    t = voxelize.SIZE_TABLE
    assert list(t) == ["Tiny", "Small", "Medium", "Large", "Huge", "Gargantuan"]
    assert t["Medium"]["pitch"] == pytest.approx(1 / 18)
    assert t["Huge"]["pitch"] == pytest.approx(1 / 9)
    assert t["Gargantuan"]["pitch"] == pytest.approx(2 / 9)


def test_a_reference_crowd_fits_the_shared_character_buffer_with_headroom():
    used, capacity = voxelize.crowd_parts({"Medium": 40, "Large": 10, "Gargantuan": 2})
    assert capacity == 262144, "RenderCoordinator::kCharacterInstanceCapacity"
    assert used <= 0.75 * capacity, f"{used} parts for the reference crowd vs {capacity}"


def test_quadruped_voxelizes_at_the_standard_pitch_under_budget_and_shell_only():
    v = voxelize.voxelize(FIX / "quadruped.glb", "Medium", "creature")
    assert v.pitch == pytest.approx(1 / 18)
    assert 0 < len(v.centers) <= voxelize.SIZE_TABLE["Medium"]["budget"]
    lo, hi = voxelize.SIZE_TABLE["Medium"]["band"]
    assert lo <= v.extent_u <= hi
    assert v.centers[:, 1].min() >= -1e-6, "feet on the ground (y = 0)"
    # shell only: the body box's centre is empty
    d = np.linalg.norm(v.centers - np.array([0.0, 0.7, 0.0]), axis=1)
    assert d.min() > 0.1, "an interior voxel survived"


def test_the_budget_wins_over_the_pitch_preference():
    # a Large creature at the top of its band: 1/18 would overflow the budget -> steps to 1/9
    v = voxelize.voxelize(FIX / "quadruped.glb", "Large", "creature", height_u=4.8)
    assert len(v.centers) <= voxelize.SIZE_TABLE["Large"]["budget"]
    assert v.pitch > 1 / 18 + 1e-9 and v.steps, "the step down must be reported"


def test_height_outside_the_size_band_is_clamped_and_reported():
    v = voxelize.voxelize(FIX / "quadruped.glb", "Medium", "creature", height_u=9.0)
    assert v.extent_u == pytest.approx(voxelize.SIZE_TABLE["Medium"]["band"][1])
    assert any("clamped" in s for s in v.steps)


# ---------------------------------------------------------------------------------------------
# decision 2 + 10: colour from the texture, deterministic palette <= 24
# ---------------------------------------------------------------------------------------------
def test_every_voxel_takes_a_texture_colour_and_the_palette_is_small_and_deterministic():
    v = voxelize.voxelize(FIX / "quadruped.glb", "Medium", "creature")
    pal, idx = palette.quantize(v.colors, max_colors=24)
    assert 2 <= len(pal) <= 24
    assert len(idx) == len(v.centers)
    pal2, idx2 = palette.quantize(v.colors, max_colors=24)
    assert np.array_equal(pal, pal2) and np.array_equal(idx, idx2)
    # the head (z > 0.5) is orange in the texture, the body brown: both must survive
    head = v.colors[v.centers[:, 2] > 0.55]
    body = v.colors[np.abs(v.centers[:, 2]) < 0.2]
    assert head[:, 0].mean() > body[:, 0].mean() + 40, "orange head vs brown body lost"


# ---------------------------------------------------------------------------------------------
# decision 9: the species spec gets the parts its stat block needs
# ---------------------------------------------------------------------------------------------
def test_a_biting_species_spec_gains_a_jaw_and_an_already_complete_one_is_untouched():
    spec = json.loads((ROOT / "tools/creature_forge/specs/wolf.json").read_text(encoding="utf-8"))
    out, added = spec_complete.complete(spec, {"attack:bite"})
    assert "jaw" in added
    assert "Jaw" in out["joints"] and "jaw" in out["chains"]
    again, added2 = spec_complete.complete(out, {"attack:bite"})
    assert added2 == [] and again == out


def test_a_tail_attack_needs_a_tail_and_wolf_already_has_one():
    spec = json.loads((ROOT / "tools/creature_forge/specs/wolf.json").read_text(encoding="utf-8"))
    _, added = spec_complete.complete(spec, {"attack:tail"})
    assert added == [], "wolf.json already has a tail chain"


# ---------------------------------------------------------------------------------------------
# decision 3: the binder fits OUR skeleton to the mesh and passes the measured bar
# ---------------------------------------------------------------------------------------------
def test_quadruped_binds_to_a_forge_skeleton_and_passes_the_bar():
    v = voxelize.voxelize(FIX / "quadruped.glb", "Medium", "creature")
    anim, m = binder.bind(v, ROOT / "resources/animated_characters/forge_direwolf.anim")
    assert m["within_frac"] >= 0.98, m
    assert m["side_violations"] == 0, m
    assert m["feet_ground_err_u"] <= 0.05 * v.extent_u, m
    assert len(anim.boxes) == len(v.centers)
    assert all(b.color is not None for b in anim.boxes)
    assert {c.name for c in anim.clips} >= {"idle", "walk", "attack", "death"}, "clips inherited from the species"


@pytest.mark.parametrize("rig,kind", [("humanoid", "biped"), ("forge_serpent", "creature"),
                                       ("forge_bat", "creature")])
def test_the_bar_fails_a_quadruped_bound_to_the_wrong_skeleton(rig, kind):
    # control for the bar itself (2026-10-01): measured 0.33 / 0.965 / 0.79
    v = voxelize.voxelize(FIX / "quadruped.glb", "Medium", "creature")
    _, m = binder.bind(v, ROOT / f"resources/animated_characters/{rig}.anim", body_kind=kind)
    assert m["within_frac"] < 0.98, m


def test_species_envelopes_ignore_boxes_the_source_rig_binds_anomalously():
    # humanoid.anim binds 48 pelvis boxes to LeftHandPinky4 / RightToe_End (since 57f1cd49)
    import anim_format as af
    env = binder.bone_envelopes(af.parse(ROOT / "resources/animated_characters/humanoid.anim"))
    assert max(env.values()) < 0.3, sorted(env.items(), key=lambda t: -t[1])[:3]


def test_biped_binds_to_the_humanoid_skeleton():
    v = voxelize.voxelize(FIX / "biped.glb", "Medium", "biped")
    anim, m = binder.bind(v, ROOT / "resources/animated_characters/humanoid.anim")
    assert m["within_frac"] >= 0.98, m
    assert m["side_violations"] == 0, m
    assert len(anim.clips) >= 150, "the humanoid clip set plays unchanged"


# ---------------------------------------------------------------------------------------------
# decision 5 + 6: manifest -> rig + bindings, deterministic, echo
# ---------------------------------------------------------------------------------------------
def _manifest(tmp_path):
    m = {"id": "test_wolfling", "serves": ["test-wolfling"], "size": "Medium", "body": "dire_wolf",
         "source": {"file": "tests/fixtures/characters/quadruped.glb"}, "palette": 24}
    p = tmp_path / "test_wolfling.json"; p.write_text(json.dumps(m), encoding="utf-8")
    return p


def test_character_add_is_deterministic_and_echoes_what_it_did(tmp_path):
    bindings = tmp_path / "bindings.json"; bindings.write_text("{}", encoding="utf-8")
    r1 = character_add.run(_manifest(tmp_path), out_dir=tmp_path / "a", bindings_path=bindings, species_dir=tmp_path / "specs")
    r2 = character_add.run(_manifest(tmp_path), out_dir=tmp_path / "b", bindings_path=bindings, species_dir=tmp_path / "specs")
    a = (tmp_path / "a" / "test_wolfling.anim").read_bytes(); b = (tmp_path / "b" / "test_wolfling.anim").read_bytes()
    assert a == b, "same model + manifest must give a byte-identical rig"
    for k in ("rig", "pitch", "parts", "budget", "palette", "binder", "steps", "credits"):
        assert k in r1, k
    assert r1["parts"] <= r1["budget"]
    j = json.loads(bindings.read_text(encoding="utf-8"))
    assert j["test-wolfling"]["animFile"].endswith("test_wolfling.anim"), "the manifest writes its bindings"


def test_a_binding_that_disagrees_with_its_manifest_is_reported(tmp_path):
    bindings = tmp_path / "bindings.json"
    bindings.write_text(json.dumps({"test-wolfling": {"animFile": "resources/animated_characters/humanoid.anim", "faction": "beasts"}}), encoding="utf-8")
    problems = character_add.check_bindings([_manifest(tmp_path)], bindings, rig_dir=tmp_path / "rigs")
    assert problems and "test-wolfling" in problems[0]


# ---------------------------------------------------------------------------------------------
# decision 9 write-back: reviewable, and never on a dry run
# ---------------------------------------------------------------------------------------------
def test_completed_spec_write_back_inserts_only_the_new_entries():
    import difflib
    p = NOJAW_BEAR
    text = p.read_text(encoding="utf-8")
    out, added = spec_complete.complete(json.loads(text), {"attack:bite"})
    new = spec_complete.write_completed(p, text, out)
    assert json.loads(new) == out
    changed = [l for l in difflib.unified_diff(text.splitlines(), new.splitlines(), lineterm="", n=0)
               if l[:1] in "+-" and l[:3] not in ("+++", "---")]
    assert added == ["jaw"] and len(changed) <= 20, f"{len(changed)} changed lines (a re-dump gave 1,125)"


def test_a_beak_attack_also_gets_a_jaw():
    spec = json.loads(NOJAW_BEAR.read_text(encoding="utf-8"))
    _, added = spec_complete.complete(spec, {"attack:beak"})
    assert added == ["jaw"]


def test_a_dry_run_writes_nothing(tmp_path, monkeypatch):
    m = {"id": "dry_owlbear", "serves": ["owlbear"], "size": "Large", "body": "bear",
         "source": {"file": "tests/fixtures/characters/quadruped.glb"}, "palette": 24, "height_u": 2.6}
    mp = tmp_path / "m.json"; mp.write_text(json.dumps(m), encoding="utf-8")
    specs = tmp_path / "frozen_specs"; specs.mkdir()
    spec = specs / "bear.json"; spec.write_bytes(NOJAW_BEAR.read_bytes())
    monkeypatch.setattr(character_add, "SPEC_DIR", specs)
    before = spec.read_bytes()
    r = character_add.run(mp, out_dir=tmp_path / "rigs", bindings_path=tmp_path / "b.json",
                          species_dir=tmp_path / "specs", dry_run=True)
    assert r["species_completed"] == ["jaw"], "owlbear's beak needs a jaw the bear spec lacks"
    assert not r["written"] and not (tmp_path / "rigs").exists() and not (tmp_path / "specs").exists()
    assert not (tmp_path / "b.json").exists() and spec.read_bytes() == before


def test_the_meshy_prompt_asks_for_wings_only_when_a_served_monster_flies():
    # the fixed "wings spread flat" suffix turned the owlbear into a winged owl (2026-10-01)
    m = {"id": "x", "body": "bear", "source": {"meshy": {"prompt": "an owlbear"}}}
    walker = character_add.prompt_for(m, {"attack:beak", "attack:claw"})
    assert "no wings" in walker and "four legs" in walker and "wings spread" not in walker
    assert "wings spread flat" in character_add.prompt_for(m, {"move:fly"})
    h = {"id": "h", "body": "humanoid", "source": {"meshy": {"prompt": "a knight"}}}
    assert character_add.prompt_for(h, set()) == "a knight"


def _jawed_direwolf():
    sys.path.insert(0, str(ROOT / "tools"))
    from creature_forge.emit import Options, compile_spec
    spec = json.loads((ROOT / "tools/creature_forge/specs/dire_wolf.json").read_text(encoding="utf-8"))
    spec, added = spec_complete.complete(spec, {"attack:bite"})
    return compile_spec(spec, Options(voxel_size=0.05, target_height=1.1)).af


def test_a_completed_jaw_gets_the_lower_head_voxels_and_sits_below_the_skull():
    # owlbear 2026-10-01: Jaw landed ABOVE the skull (side branches did not follow the axis fit)
    # and 0 voxels bound to it, so the beak could never open. Contract on the MESH voxels: the
    # hinge is in the lower half of the head at its z, and the jaw is the lower head.
    v = voxelize.voxelize(FIX / "quadruped.glb", "Medium", "creature")
    anim, m = binder.bind(v, _jawed_direwolf())
    g = binder.fk(anim)
    ids = {b.name: b.id for b in anim.bones}
    ch = binder._children(anim)
    jaw_set = set(binder._subtree(ch, ids["Jaw"]))
    head_set = set(binder._subtree(ch, ids["HeadRoot"])) - jaw_set
    bone = np.array([b.bone_id for b in anim.boxes])
    jaw = np.isin(bone, list(jaw_set))
    head = np.isin(bone, list(head_set))
    assert jaw.sum() >= 10, f"{jaw.sum()} voxels on the jaw"
    assert m["jaw_voxels"] == int(jaw.sum())
    hy = g[ids["Jaw"]][1, 3]
    mean_y = v.centers[head | jaw, 1].mean()
    assert hy < mean_y, f"hinge y {hy:.3f} is above the head's mean height {mean_y:.3f}"
    assert v.centers[jaw, 1].max() < v.centers[head, 1].mean() + v.pitch, "the jaw must be the LOWER head"
    assert m["within_frac"] >= 0.98 and m["side_violations"] == 0, m


def test_clip_position_keys_follow_the_fitted_bind_pose():
    # owlbear 2026-10-01: keys mapped as s*key+t ignored the body-axis fit, so every clip lifted
    # the body ~0.46 u off the ground. A key's offset from the bind pose may scale, never shift.
    import anim_format as af
    rig = af.parse(ROOT / "resources/animated_characters/forge_direwolf.anim")
    v = voxelize.voxelize(FIX / "quadruped.glb", "Medium", "creature")
    out, m = binder.bind(v, rig)
    s = np.array(m["scale"])
    for clip_old, clip_new in zip(rig.clips, out.clips):
        for ch_old, ch_new in zip(clip_old.channels, clip_new.channels):
            if not ch_old.pos_keys:
                continue
            b_old = np.array(rig.bones[ch_old.bone_id].pos)
            b_new = np.array(out.bones[ch_new.bone_id].pos)
            for (_, p_old), (_, p_new) in zip(ch_old.pos_keys, ch_new.pos_keys):
                want = b_new + s * (np.array(p_old) - b_old)
                assert np.allclose(p_new, want, atol=1e-4), (clip_new.name, rig.bones[ch_old.bone_id].name, p_new, want)


def test_moving_a_monster_to_its_manifest_rig_drops_stand_in_overrides(tmp_path):
    # owlbear 2026-10-01 inherited forge_bear's tint [0.85,0.75,0.55] and scale 1.1
    mp = {"_comment": "x", "archetypes": {
        "ursine": {"animFile": "resources/animated_characters/forge_bear.anim", "faction": "beasts",
                   "members": {"owlbear": {"tint": [0.85, 0.75, 0.55], "scale": 1.1, "faction": "monsters"},
                               "brown-bear": {}}}}}
    p = tmp_path / "map.json"; p.write_text(json.dumps(mp), encoding="utf-8")
    m = {"id": "owlbear", "serves": ["owlbear"], "body": "bear"}
    dropped = character_add.write_bindings_via_map(m, "resources/animated_characters/owlbear.anim",
                                                   map_path=p, regenerate=False)
    out = json.loads(p.read_text(encoding="utf-8"))["archetypes"]
    assert out["owlbear"]["members"]["owlbear"] == {"faction": "monsters"}, "non-visual keys survive"
    assert dropped == {"owlbear": {"tint": [0.85, 0.75, 0.55], "scale": 1.1}}
    assert "owlbear" not in out["ursine"]["members"] and "brown-bear" in out["ursine"]["members"]


def test_no_voxel_lands_on_a_bone_the_engine_does_not_render():
    # AnimatedVoxelCharacter's part build SKIPS every box on a bone whose name contains one of
    # these (fingers, toes, eyes, *_End). Measured 2026-10-02: the biped fixture put 54 voxels on
    # ToeBase bones -> invisible feet.
    v = voxelize.voxelize(FIX / "biped.glb", "Medium", "biped")
    anim, _ = binder.bind(v, ROOT / "resources/animated_characters/humanoid.anim")
    names = {b.id: b.name.lower() for b in anim.bones}
    lost = [names[b.bone_id] for b in anim.boxes if any(k in names[b.bone_id] for k in binder.ENGINE_SKIPPED)]
    assert not lost, f"{len(lost)} voxels on engine-skipped bones: {sorted(set(lost))[:6]}"
    src = (ROOT / "engine/src/scene/AnimatedVoxelCharacter.cpp").read_text(encoding="utf-8")
    for k in binder.ENGINE_SKIPPED:
        assert f'nameLower.find("{k}")' in src, f"binder.ENGINE_SKIPPED out of sync with the engine: {k}"


def test_biped_legs_sit_over_the_mesh_legs_not_a_foot_length_behind():
    # orc 2026-10-02: the leg fit matched the TOE TIP to the foot voxels' centroid, pushing both
    # legs ~0.22 u backwards (binder refused at 0.863)
    v = voxelize.voxelize(FIX / "biped.glb", "Medium", "biped")
    anim, _ = binder.bind(v, ROOT / "resources/animated_characters/humanoid.anim")
    g = binder.fk(anim)
    for side, sign in (("Left", 1), ("Right", -1)):
        hip = [b.id for b in anim.bones if b.name.endswith(f"{side}UpLeg")][0]
        thigh = v.centers[(np.abs(v.centers[:, 1] - 0.6) < v.pitch) & (sign * v.centers[:, 0] > 0)]
        dz = g[hip][2, 3] - thigh[:, 2].mean()
        assert abs(dz) < 2 * v.pitch, f"{side} hip is {dz:+.3f} u off the mesh thigh in z"


def test_a_bent_arm_is_fitted_per_segment():
    # orc 2026-10-02: forearms forward, one straight-arm rotation left the hands 0.4 u off
    v = voxelize.voxelize(FIX / "biped_bent.glb", "Medium", "biped")
    anim, m = binder.bind(v, ROOT / "resources/animated_characters/humanoid.anim")
    g = binder.fk(anim)
    c = v.centers                     # the voxelizer re-centres the model, so measure relative
    torso_z = c[(np.abs(c[:, 0]) < 0.15) & (c[:, 1] > 1.0) & (c[:, 1] < 1.4), 2].mean()
    for side, sign in (("Left", 1), ("Right", -1)):
        hand = [b.id for b in anim.bones if b.name.endswith(f"{side}Hand")][0]
        arm = c[(sign * c[:, 0] > 0.3) & (c[:, 1] > 0.9)]
        tips = arm[arm[:, 2] >= arm[:, 2].max() - 0.1]      # the forearm tips
        fwd = g[hand][2, 3] - torso_z
        assert fwd > 0.12, f"{side} wrist only {fwd:+.3f} u in front of the torso: still pointing sideways"
        assert np.linalg.norm(g[hand][:3, 3] - tips.mean(axis=0)) < 0.2, (side, g[hand][:3, 3], tips.mean(axis=0))
    assert m["within_frac"] >= 0.98, m


# ---------------------------------------------------------------------------------------------
# R2 per-import gates (aesthetic + lint offline; oracle in tests/stress/ImportedRigOracleTest.cpp)
# ---------------------------------------------------------------------------------------------
def test_the_aesthetic_gate_refuses_subcube_pitch_and_non_voxel_boxes():
    from tools.characters import gates
    import anim_format as af
    v = voxelize.voxelize(FIX / "quadruped.glb", "Medium", "creature")
    anim, _ = binder.bind(v, ROOT / "resources/animated_characters/forge_direwolf.anim")
    assert gates.aesthetic(anim, v.pitch)["ok"]
    assert not gates.aesthetic(anim, 1 / 3)["ok"], "a subcube pitch is not sub-voxel detail"
    anim.boxes[0] = af.BoxShape(anim.boxes[0].bone_id, (0.3, 0.3, 0.3), anim.boxes[0].center, anim.boxes[0].color)
    g = gates.aesthetic(anim, v.pitch)
    assert not g["ok"] and g["non_voxel_boxes"] == 1


def test_the_lint_gate_refuses_an_import_that_adds_a_lint_error():
    import copy
    import anim_format as af
    from tools.characters import gates
    src = af.parse(ROOT / "resources/animated_characters/forge_direwolf.anim")
    v = voxelize.voxelize(FIX / "quadruped.glb", "Medium", "creature")
    anim, _ = binder.bind(v, src)
    assert gates.lint_delta(src, anim)["ok"]
    broken = copy.deepcopy(anim)
    ch = next(c for clip in broken.clips if clip.name == "walk" for c in clip.channels if c.rot_keys)
    t, q = ch.rot_keys[1]
    ch.rot_keys[1] = (t, (q[0] * 2, q[1] * 2, q[2] * 2, q[3] * 2))     # not a unit quaternion
    g = gates.lint_delta(src, broken)
    assert not g["ok"] and g["new_errors"], g


def test_a_needed_part_that_cannot_be_added_is_reported_not_skipped():
    spec = json.loads((ROOT / "tools/creature_forge/specs/giant_spider.json").read_text(encoding="utf-8"))
    out, added = spec_complete.complete(spec, {"attack:bite"})
    assert added == [], "giant_spider has no head chain to hang a jaw on"
    assert "jaw" in spec_complete.unsupported_needs(out, {"attack:bite"})


def test_a_batch_reports_every_item_and_stops_spending_below_ten_percent(tmp_path, monkeypatch):
    good = _manifest(tmp_path)
    bad = tmp_path / "bad.json"; bad.write_text(json.dumps({"id": "x"}), encoding="utf-8")
    monkeypatch.setattr(character_add, "run", lambda mp, allow_spend=False: {
        "written": True, "warnings": []} if Path(mp) == good else (_ for _ in ()).throw(ValueError("manifest missing")))
    rows = character_add.run_batch([good, bad], allow_spend=False)
    assert [r["status"] for r in rows] == ["imported", "refused"] and "manifest missing" in rows[1]["reason"]
    calls = iter([None, "Meshy credits below 10 % (300): stopped before spending more"])
    rows = character_add.run_batch([good, good, good], allow_spend=True, credit_check=lambda: next(calls))
    assert [r["status"] for r in rows] == ["imported", "not run", "not run"]
    assert "below 10 %" in rows[1]["reason"]


def test_a_refused_import_records_its_reason_and_gives_its_monsters_back(tmp_path):
    mp = _manifest(tmp_path)
    rigs = tmp_path / "rigs"; rigs.mkdir(); (rigs / "test_wolfling.anim").write_text("x", encoding="utf-8")
    cur = {"_comment": "x", "archetypes": {
        "test_wolfling": {"animFile": "resources/animated_characters/test_wolfling.anim", "faction": "beasts",
                          "members": {"test-wolfling": {}}}}}
    head = {"archetypes": {"meshy_canine": {"animFile": "resources/animated_characters/wolf_meshy.anim",
                                            "faction": "beasts", "members": {"test-wolfling": {"tint": [1, 0.9, 0.9]}}}}}
    mapp = tmp_path / "map.json"; mapp.write_text(json.dumps(cur), encoding="utf-8")
    r = character_add.refuse(mp, "oracle gate: walk stanceResidual/H", map_path=mapp, regenerate=False,
                             head_map=head, rig_dir=rigs)
    m = json.loads(mp.read_text(encoding="utf-8"))
    assert m["status"] == "refused" and "stanceResidual" in m["refused_reason"]
    assert not (rigs / "test_wolfling.anim").exists()
    out = json.loads(mapp.read_text(encoding="utf-8"))["archetypes"]
    assert out["meshy_canine"]["members"]["test-wolfling"] == {"tint": [1, 0.9, 0.9]}
    assert "test_wolfling" not in out and r["restored"] == {"test-wolfling": "meshy_canine"}


# ---------------------------------------------------------------------------------------------
# R2 stress batch (2026-10-02) refusals that were the binder's fault, not the model's
# ---------------------------------------------------------------------------------------------
def _bind_import(id_):
    from tools.characters import character_add as ca
    m = ca.load_manifest(ROOT / f"resources/characters/{id_}.json")
    rig, kind, _ = ca.target_rig(m, ca.SPEC_DIR, ca.needs_for(m["serves"]), write=False)
    v = voxelize.voxelize(ROOT / f"resources/characters/source/{id_}.glb", m["size"], kind, m.get("height_u"))
    return v, *binder.bind(v, rig, kind)


@pytest.mark.parametrize("id_", ["panther", "hyena", "giant_wolf_spider"])
def test_every_leg_of_a_mid_stride_body_gets_its_own_mesh_leg(id_):
    # the front/back half split put two mesh legs under one rig leg; the other leg got 0 voxels
    v, anim, m = _bind_import(id_)
    g = binder.fk(anim); ch = binder._children(anim)
    bone = np.array([b.bone_id for b in anim.boxes])
    for root, _ in binder._leg_roots(anim, g, ch, float(v.centers[:, 1].max())):
        n = int(np.isin(bone, binder._subtree(ch, root)).sum())
        assert n >= 10, f"{id_}: leg {anim.bones[root].name} has {n} voxels"
    # NOT asserted here: the feet bar. The panther model is mid-stride with a paw 0.11 u up —
    # a model-pose refusal, not a binder fault
    assert m["ground_contacts_used"] == len(binder._leg_roots(anim, g, ch, float(v.centers[:, 1].max()))), m


def test_a_biped_skeleton_is_centred_on_its_body_not_on_a_bbox_pulled_by_its_arms():
    # hobgoblin 2026-10-02: arms bent forward to z +0.44 moved the bbox centre; the spine sat
    # ~0.15 u in front of the body (refused at 0.889)
    v, anim, m = _bind_import("hobgoblin")
    g = binder.fk(anim)
    hips = [b.id for b in anim.bones if b.name.endswith("Hips")][0]
    H = v.centers[:, 1].max()
    band = v.centers[(v.centers[:, 1] > 0.45 * H) & (v.centers[:, 1] < 0.6 * H)]
    dz = g[hips][2, 3] - np.median(band[:, 2])
    assert abs(dz) < 2 * v.pitch, f"Hips {dz:+.3f} u off the mesh hips in z"


def test_a_biped_spine_follows_a_hunched_torso():
    # gnoll 2026-10-02: hunched back, upright humanoid spine -> 50 Spine1 voxels out of reach
    v, anim, m = _bind_import("gnoll")
    g = binder.fk(anim)
    c = v.centers
    H = c[:, 1].max()
    for nm in ("Spine1", "Spine2"):
        j = [b.id for b in anim.bones if b.name.endswith(nm)][0]
        sl = c[(np.abs(c[:, 1] - g[j][1, 3]) < 1.5 * v.pitch) & (np.abs(c[:, 0]) < 0.15 * H)]
        dz = g[j][2, 3] - np.median(sl[:, 2])
        assert abs(dz) < 2 * v.pitch, f"{nm} {dz:+.3f} u off the torso centre at its height"


def test_a_missing_source_is_re_downloaded_from_its_meshy_task_without_spending(tmp_path, monkeypatch):
    # 2026-10-07: the local repo was lost; manifests' task ids are how the models come back
    calls = []

    def fake_meshy(method, url, body=None):
        calls.append((method, url))
        return {"status": "SUCCEEDED", "model_urls": {"glb": "https://example.invalid/m.glb"}}

    class FakeResp:
        def __enter__(self): return self
        def __exit__(self, *a): return False
        def read(self): return b"glTF-bytes"

    monkeypatch.setattr(character_add, "_meshy", fake_meshy)
    monkeypatch.setattr(character_add.urllib.request, "urlopen", lambda *a, **k: FakeResp())
    monkeypatch.setattr(character_add, "SOURCE_DIR", tmp_path)
    monkeypatch.setattr(character_add, "fetch_meshy", lambda *a, **k: (_ for _ in ()).throw(AssertionError("spent credits")))
    m = {"id": "x", "source": {"meshy": {"prompt": "p", "task_ids": ["pre", "ref"]}}}
    path, credits = character_add.resolve_source(m, allow_spend=True, needs=set())
    assert path.read_bytes() == b"glTF-bytes" and credits["consumed"] == 0
    assert calls == [("GET", character_add.MESHY_T23D + "/ref")], "the REFINE task, read-only"
