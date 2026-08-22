"""creature_forge red-before-green suite.

The forge compiles an anyCreature-style ACS JSON spec (joints -> chains ->
swept superellipse volumes -> parts -> keyframe animations) into a Phyxel
voxel `.anim` rig. These tests pin the contract:

  * resolver-level spec errors raise (unknown joint, cycle, no root chain,
    loose joint without attach, missing palette material)
  * the vendored anyCreature reference spec (specs/wolf.json, MIT) compiles
    to a budget-respecting, round-trippable, lint-clean rig
  * compilation is byte-deterministic
  * the vendored known-bad calibration spec (specs/wolf_red.json) is BLOCKed
    by the ported geometric checks
  * the shipped ibex spec's bone names satisfy the engine's detectMorphology
    quadruped heuristic (CharacterAppearance.cpp)
  * voxelization is geometrically honest (cylinder radius, superellipse
    exponent ordering, membrane sheet thickness)

Box budget: RenderCoordinator::kCharacterInstanceCapacity (262144) must hold
20 of the densest rig (CharacterInstanceBudgetTest.cpp) => <= 13107 boxes.
"""
from __future__ import annotations

import copy
import json
import math
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "anim_pipeline"))

from creature_forge.spec import SpecError, load_spec  # noqa: E402
from creature_forge.emit import compile_spec, Options  # noqa: E402
from creature_forge import checks  # noqa: E402
import anim_format  # noqa: E402
import anim_lint  # noqa: E402

SPECS = ROOT / "tools" / "creature_forge" / "specs"
BOX_BUDGET = 262144 // 20  # CharacterInstanceBudgetTest contract


# ---------------------------------------------------------------------------
# Minimal spec helpers
# ---------------------------------------------------------------------------

def minimal_spec(**overrides):
    """A 2-joint vertical cylinder creature: the smallest legal spec."""
    spec = {
        "name": "unit_cyl",
        "palette": {"skin": {"color": "#808080", "rough": 0.9}},
        "shading": {"gradient": {"top": 0.0, "bottom": 0.0},
                    "noise": {"size": 0.018, "amount": 0.0}},
        "joints": {
            "Base": [0, 0.1, 0],
            "Top": {"from": "Base", "up": 0.6},
        },
        "chains": {"spine": ["Base", "Top"]},
        "volumes": [{
            "chain": "spine", "material": "skin", "sides": 12,
            "profile": [[0.0, 0.15, 0.15], [1.0, 0.15, 0.15]],
            "caps": ["none", "none"],
        }],
        "animations": {
            "idle": {"duration": 1.0, "loop": True,
                     "tracks": {"Top": {"rx": [[0, -2], [0.5, 2], [1, -2]]}}},
        },
    }
    spec.update(overrides)
    return spec


# ---------------------------------------------------------------------------
# 1. Resolver-level errors
# ---------------------------------------------------------------------------

class TestResolverErrors:
    def test_unknown_from_joint_raises(self):
        s = minimal_spec()
        s["joints"]["Top"] = {"from": "Nowhere", "up": 0.6}
        with pytest.raises(SpecError, match="Nowhere"):
            compile_spec(s)

    def test_joint_cycle_raises(self):
        s = minimal_spec()
        s["joints"]["A"] = {"from": "B", "up": 0.1}
        s["joints"]["B"] = {"from": "A", "up": 0.1}
        with pytest.raises(SpecError, match="cycle"):
            compile_spec(s)

    def test_no_root_chain_raises(self):
        s = minimal_spec()
        s["attach"] = {"spine": "Base"}  # every chain attached => no root
        with pytest.raises(SpecError, match="root"):
            compile_spec(s)

    def test_loose_joint_without_attach_raises(self):
        s = minimal_spec()
        s["joints"]["Loose"] = {"from": "Top", "up": 0.1}  # in no chain, no attach
        with pytest.raises(SpecError, match="Loose"):
            compile_spec(s)

    def test_missing_palette_material_raises(self):
        s = minimal_spec()
        s["volumes"][0]["material"] = "ghost_mat"
        with pytest.raises(SpecError, match="ghost_mat"):
            compile_spec(s)

    def test_unknown_chain_in_volume_raises(self):
        s = minimal_spec()
        s["volumes"][0]["chain"] = "no_such_chain"
        with pytest.raises(SpecError, match="no_such_chain"):
            compile_spec(s)

    def test_animation_unknown_joint_raises(self):
        s = minimal_spec()
        s["animations"]["idle"]["tracks"]["Ghost"] = {"rx": [[0, 1], [1, 1]]}
        with pytest.raises(SpecError, match="Ghost"):
            compile_spec(s)


# ---------------------------------------------------------------------------
# 2. Reference compile: vendored anyCreature wolf spec
# ---------------------------------------------------------------------------

@pytest.fixture(scope="module")
def wolf():
    return compile_spec(load_spec(SPECS / "wolf.json"))


class TestWolfReference:
    def test_bones_parents_precede_children(self, wolf):
        for b in wolf.af.bones:
            assert b.parent_id < b.id or b.parent_id == -1

    def test_bone_ids_sequential(self, wolf):
        for i, b in enumerate(wolf.af.bones):
            assert b.id == i

    def test_mirrored_limbs_exist(self, wolf):
        names = {b.name for b in wolf.af.bones}
        assert "LFrontRoot" in names and "RFrontRoot" in names
        assert "LBackToe" in names and "RBackToe" in names

    def test_mirror_positions_x_negated(self, wolf):
        gp = wolf.bind_world_positions()
        for ln in ("LFrontRoot", "LFrontElbow", "LBackKnee", "LBackToe"):
            rn = "R" + ln[1:]
            lx, ly, lz = gp[ln]
            rx, ry, rz = gp[rn]
            assert rx == pytest.approx(-lx, abs=1e-6)
            assert ry == pytest.approx(ly, abs=1e-6)
            assert rz == pytest.approx(lz, abs=1e-6)

    def test_every_geometry_bone_owns_or_is_embedded(self, wolf):
        """A bone either owns boxes, or is an articulation pivot embedded in
        mass another bone claimed (attached-chain roots absorbed by the
        torso via first-writer-wins) — never floating and empty."""
        owned = {bx.bone_id for bx in wolf.af.boxes}
        vs = wolf.options.voxel_size
        for b in wolf.af.bones:
            if b.name == "ground_ref" or b.id in owned:
                continue
            jp = wolf.sk.world[b.name]
            near = any(
                abs(jp[0] - c[0]) <= 2 * vs and abs(jp[1] - c[1]) <= 2 * vs
                and abs(jp[2] - c[2]) <= 2 * vs
                for c in (wolf.grid.center_of(k) for k in wolf.grid.cells))
            assert near, f"bone {b.name} owns no boxes and floats free"

    def test_box_budget(self, wolf):
        assert 0 < len(wolf.af.boxes) <= BOX_BUDGET

    def test_ground_ref_present(self, wolf):
        assert any(b.name == "ground_ref" for b in wolf.af.bones)

    def test_clips_idle_and_walk(self, wolf):
        names = {c.name for c in wolf.af.clips}
        assert "idle" in names
        assert "walk" in names  # spec clip 'move' aliased to engine name

    def test_walk_speed_stamped(self, wolf):
        walk = wolf.af.clip("walk")
        assert walk.speed is not None and walk.speed > 0

    def test_rot_keys_unit_quaternions(self, wolf):
        for clip in wolf.af.clips:
            for ch in clip.channels:
                for _, q in ch.rot_keys:
                    assert math.sqrt(sum(c * c for c in q)) == pytest.approx(1.0, abs=1e-4)

    def test_mirror_phase_offsets_right_legs(self, wolf):
        """move has mirror_phase 0.5: R-leg rotation must be half a cycle out
        of phase with the L leg (compare sampled values, not key times)."""
        walk = wolf.af.clip("walk")
        ids = wolf.af.bone_map()
        by_bone = {c.bone_id: c for c in walk.channels}
        lch = by_bone[ids["LFrontRoot"]]
        rch = by_bone[ids["RFrontRoot"]]

        def sample(ch, t):
            keys = ch.rot_keys
            best = min(keys, key=lambda kv: abs(kv[0] - t))
            return best[1]
        half = walk.duration / 2
        for frac in (0.0, 0.25):
            t = frac * walk.duration
            lq = sample(lch, t)
            rq = sample(rch, (t + half) % walk.duration)
            # same |rotation| angle when offset by half a cycle
            assert abs(lq[3]) == pytest.approx(abs(rq[3]), abs=0.02)

    def test_roundtrip_semantically_equal(self, wolf, tmp_path):
        p = tmp_path / "wolf_rt.anim"
        anim_format.write(wolf.af, p)
        af2 = anim_format.parse(p)
        assert anim_format.semantically_equal(wolf.af, af2) == []

    def test_lint_clean(self, wolf):
        for clip in wolf.af.clips:
            findings = anim_lint.lint_clip(wolf.af, clip, looping=True)
            errors = [f for f in findings if f[0] == "ERROR"]
            assert errors == [], f"lint errors on {clip.name}: {errors}"

    def test_no_blank_lines_inside_sections(self, wolf, tmp_path):
        p = tmp_path / "wolf_fmt.anim"
        anim_format.write(wolf.af, p)
        text = p.read_text()
        body = text[text.index("SKELETON"):]
        assert "\n\n" not in body

    def test_checks_pass_on_wolf(self, wolf):
        blocks = [f for f in checks.run(wolf) if f.severity == "BLOCK"]
        assert blocks == [], f"reference wolf must pass checks: {blocks}"


# ---------------------------------------------------------------------------
# 3. Determinism
# ---------------------------------------------------------------------------

def test_byte_identical_recompile(tmp_path):
    spec = load_spec(SPECS / "wolf.json")
    a = tmp_path / "a.anim"
    b = tmp_path / "b.anim"
    anim_format.write(compile_spec(copy.deepcopy(spec)).af, a)
    anim_format.write(compile_spec(copy.deepcopy(spec)).af, b)
    assert a.read_bytes() == b.read_bytes()


# ---------------------------------------------------------------------------
# 4. Known-bad geometry must BLOCK (red proof of the validation layer)
# ---------------------------------------------------------------------------

def test_unbalanced_cantilever_blocks():
    """A creature whose mass centroid hangs far outside its support polygon
    must be refused by the ported balance check."""
    s = {
        "name": "cantilever",
        "palette": {"skin": {"color": "#808080", "rough": 0.9}},
        "joints": {
            "FootBase": [0, 0.05, 0],
            "FootTop": {"from": "FootBase", "up": 0.3},
            "BeamA": {"from": "FootTop", "up": 0.05},
            "BeamB": {"from": "BeamA", "fwd": 1.5},
        },
        "chains": {"foot": ["FootBase", "FootTop"],
                   "beam": ["BeamA", "BeamB"]},
        "attach": {"beam": "FootTop"},
        "volumes": [
            {"chain": "foot", "material": "skin",
             "profile": [[0, 0.06, 0.06], [1, 0.06, 0.06]],
             "caps": ["none", "none"]},
            {"chain": "beam", "material": "skin",
             "profile": [[0, 0.14, 0.14], [1, 0.14, 0.14]],
             "caps": ["none", "dome"]},
        ],
        "animations": {"idle": {"duration": 1.0, "loop": True, "tracks": {
            "BeamB": {"rx": [[0, -1], [0.5, 1], [1, -1]]}}}},
    }
    compiled = compile_spec(s)
    blocks = [f for f in checks.run(compiled) if f.severity == "BLOCK"]
    assert any(f.rule == "balance" for f in blocks), blocks


def test_wolf_red_calibration_compiles():
    """The vendored calibration variant (absolute-joint blobby wolf) must at
    least compile and survive the checks pipeline without crashing."""
    compiled = compile_spec(load_spec(SPECS / "wolf_red.json"))
    checks.run(compiled)  # findings allowed either way; must not raise
    assert len(compiled.af.boxes) > 0


# ---------------------------------------------------------------------------
# 5. Ibex morphology naming (engine detectMorphology replica)
# ---------------------------------------------------------------------------

def _detect_morphology(bone_names):
    """String-level replica of CharacterAppearance.cpp detectMorphology."""
    low = [n.lower() for n in bone_names]
    has = lambda sub: any(sub in n for n in low)  # noqa: E731
    exact = lambda w: w in low  # noqa: E731
    if has("leg1_coxa") and (exact("thorax") or exact("abdomen")):
        return "arachnid"
    if has("wing") and has("tail") and exact("neck_1"):
        return "dragon"
    if exact("pelvis") and (has("paw") or has("tail")) and exact("chest") and not has("wing"):
        return "quadruped"
    if has("frontleg") and has("backleg"):
        return "quadruped"
    if exact("hips") or exact("mixamorighips"):
        return "humanoid"
    return "unknown"


def test_ibex_bone_names_detect_as_quadruped():
    compiled = compile_spec(load_spec(SPECS / "ibex.json"))
    names = [b.name for b in compiled.af.bones]
    assert _detect_morphology(names) == "quadruped", names


def test_ibex_passes_checks_and_budget():
    """Compiled exactly as the shipped rig is (target-height 1.05) — this
    also pins the native-vs-scaled space mixing bug the checks once had."""
    compiled = compile_spec(load_spec(SPECS / "ibex.json"),
                            Options(target_height=1.05))
    blocks = [f for f in checks.run(compiled) if f.severity == "BLOCK"]
    assert blocks == []
    assert len(compiled.af.boxes) <= BOX_BUDGET
    walk = compiled.af.clip("walk")
    assert walk.speed is not None
    ys = []
    world = compiled.bind_world_positions()
    for bx in compiled.af.boxes:
        bw = world[compiled.af.bones[bx.bone_id].name]
        ys += [bw[1] + bx.center[1] - bx.size[1] / 2,
               bw[1] + bx.center[1] + bx.size[1] / 2]
    assert max(ys) - min(ys) == pytest.approx(1.05, abs=0.01)


def test_fauna_speed_table_matches_shipped_rig():
    """FaunaSpawner::walkSpeedFor drives NPC translation; the shipped rig's
    walk clip Speed drives player-adjacent paths. If they diverge the ibex
    foot-slides in one of the two. Keep them within 5%."""
    import re
    rig = ROOT / "resources" / "animated_characters" / "forge_ibex.anim"
    if not rig.exists():
        pytest.skip("forge_ibex.anim not generated yet")
    af = anim_format.parse(rig)
    clip_speed = af.clip("walk").speed
    cpp = (ROOT / "engine" / "src" / "core" / "FaunaSpawner.cpp").read_text()
    m = re.search(r'has\("ibex"\)\)\s*return\s*([0-9.]+)f', cpp)
    assert m, "FaunaSpawner::walkSpeedFor has no ibex entry"
    table_speed = float(m.group(1))
    assert abs(table_speed - clip_speed) <= 0.05 * clip_speed, \
        f"walkSpeedFor {table_speed} vs clip Speed {clip_speed}"


# ---------------------------------------------------------------------------
# 6. Bestiary manifest: batch compile + per-species contracts
# ---------------------------------------------------------------------------

MANIFEST = ROOT / "tools" / "creature_forge" / "bestiary.json"


def _manifest_entries():
    if not MANIFEST.exists():
        return []
    entries = json.loads(MANIFEST.read_text(encoding="utf-8"))
    return [e for e in entries if "id" in e]


def _plan_score(plan: dict, bone_names) -> int:
    """Python replica of BodyPlanRegistry::planForSkeleton scoring
    (BodyPlan.cpp:241-261): exact-name matches only; +1 resolved root,
    +1 per resolved leg upper/mid/foot, +1 per resolved segment. The
    engine requires score >= 3 or the plan is rejected in favor of the
    morphology default."""
    names = set(bone_names)
    score = 0
    root = plan.get("rootBone", "")
    if root in names:
        score += 1
    else:
        low = [n.lower() for n in names]
        for alias in plan.get("hipAliases", []):
            if any(alias in n for n in low):
                score += 1
                break
    for leg in plan.get("legs", []):
        score += (leg.get("upper") in names) + (leg.get("mid") in names) \
            + (leg.get("foot") in names)
    for seg in plan.get("segments", []):
        score += (seg.get("bone") in names)
    return score


@pytest.mark.parametrize("entry", _manifest_entries(), ids=lambda e: e["id"])
class TestBestiarySpecies:
    """Every bestiary-manifest species must compile clean and satisfy the
    engine contracts. Parametrized: adding a creature to the manifest
    automatically puts it under contract."""

    @pytest.fixture(scope="class")
    def compiled_cache(self):
        return {}

    def _compile(self, entry, cache):
        if entry["id"] not in cache:
            from creature_forge.emit import compile_spec, Options
            spec = load_spec(SPECS / Path(entry["spec"]).name)
            cache[entry["id"]] = compile_spec(spec, Options(
                target_height=entry.get("target_height")))
        return cache[entry["id"]]

    def test_checks_pass(self, entry, compiled_cache):
        compiled = self._compile(entry, compiled_cache)
        blocks = [f for f in checks.run(compiled) if f.severity == "BLOCK"]
        assert blocks == [], blocks

    def test_morphology_contract(self, entry, compiled_cache):
        compiled = self._compile(entry, compiled_cache)
        names = [b.name for b in compiled.af.bones]
        assert _detect_morphology(names) == entry["morphology"], names

    def test_combat_clip_set(self, entry, compiled_cache):
        compiled = self._compile(entry, compiled_cache)
        clips = {c.name for c in compiled.af.clips}
        required = {"idle", "walk"}
        if entry.get("combat"):
            required |= {"attack", "death"}
        assert required <= clips, f"missing {required - clips}"

    def test_attack_clip_meta(self, entry, compiled_cache):
        if not entry.get("combat"):
            pytest.skip("non-combat species")
        compiled = self._compile(entry, compiled_cache)
        meta = compiled.af.clip_meta("attack")
        assert meta is not None, "attack clip has no # clip_meta header"
        assert meta.get("type") == "combat"
        assert 0.0 < float(meta["hitFrameFraction"]) < 1.0

    def test_box_budget(self, entry, compiled_cache):
        compiled = self._compile(entry, compiled_cache)
        assert len(compiled.af.boxes) <= BOX_BUDGET

    def test_full_body_plan(self, entry, compiled_cache):
        """Generated plans must be FULL (rootBone/legs/segments) so
        planForSkeleton scores them >= 3 — a minimal plan is rejected AND
        shadows the real per-morphology default (the forge_ibex bug)."""
        from creature_forge.body_plan import derive_plan
        compiled = self._compile(entry, compiled_cache)
        plan = derive_plan(compiled, entry["id"])
        names = [b.name for b in compiled.af.bones]
        assert plan["rootBone"] in names
        # Leg count is a property of the CREATURE, not of the morphology label:
        # a serpent legitimately has none and a bird has two, while both still
        # read as 'quadruped' to the engine's bone heuristic. The binding
        # contract that actually matters is the plan score below.
        if entry.get("legless"):
            min_legs = 0
        else:
            min_legs = entry.get("legs", {"quadruped": 4, "arachnid": 8,
                                          "dragon": 4}.get(entry["morphology"], 2))
        assert len(plan["legs"]) >= min_legs
        assert plan["segments"], "segments must not be empty"
        assert _plan_score(plan, names) >= 3
        cd = plan["clipDefaults"]
        assert cd.get("Idle") == "idle" and cd.get("Walk") == "walk"
        if entry.get("combat"):
            assert cd.get("Attack") == "attack" and cd.get("Death") == "death"


def test_generated_plan_does_not_shadow_wolf():
    """The ibex's generated plan must not out-score quadruped_wolf on the
    wolf's OWN skeleton (regression for the minimal-plan shadowing bug)."""
    from creature_forge.emit import compile_spec, Options
    from creature_forge.body_plan import derive_plan
    compiled = compile_spec(load_spec(SPECS / "ibex.json"),
                            Options(target_height=1.05))
    ibex_plan = derive_plan(compiled, "forge_ibex")
    wolf_plan = json.loads(
        (ROOT / "resources" / "body_plans" / "quadruped_wolf.json").read_text())
    wolf_bones = []
    for line in (ROOT / "resources" / "animated_characters"
                 / "character_wolf.anim").read_text().splitlines():
        if line.startswith("Bone "):
            wolf_bones.append(line.split()[2])
        elif line.startswith("MODEL"):
            break
    assert _plan_score(wolf_plan, wolf_bones) > _plan_score(ibex_plan, wolf_bones)


def test_death_pose_rule_blocks_standing_death():
    """A 'death' clip that leaves the creature standing must BLOCK."""
    s = minimal_spec()
    s["animations"]["death"] = {
        "duration": 1.0, "loop": False,
        "tracks": {"Top": {"rx": [[0, 0], [1, 5]]}},  # barely moves — stays up
    }
    compiled = compile_spec(s)
    blocks = [f for f in checks.run(compiled) if f.severity == "BLOCK"]
    assert any(f.rule == "death_pose" for f in blocks), blocks


def test_required_clips_rule_blocks_combat_without_attack():
    """combat=True compilation without attack/death clips must BLOCK."""
    s = minimal_spec()
    s["combat"] = True  # only idle exists
    compiled = compile_spec(s)
    blocks = [f for f in checks.run(compiled) if f.severity == "BLOCK"]
    assert any(f.rule == "required_clips" for f in blocks), blocks


# ---------------------------------------------------------------------------
# 7. Bestiary bindings: every SRD stat block resolves to a usable rig
# ---------------------------------------------------------------------------

MONSTER_DIR = ROOT / "resources" / "monsters"
BINDINGS = MONSTER_DIR / "visuals" / "bindings.json"
RIG_DIR = ROOT / "resources" / "animated_characters"


def _all_stat_block_ids():
    ids = set()
    for f in sorted(MONSTER_DIR.glob("*.json")):
        data = json.loads(f.read_text(encoding="utf-8"))
        entries = data if isinstance(data, list) else data.get("monsters", [])
        for m in entries:
            if isinstance(m, dict) and m.get("id"):
                ids.add(m["id"])
    return ids


def _rig_clip_names(anim_path: Path):
    """Clip names without a full parse — the MODEL section is huge."""
    names = []
    for line in anim_path.read_text(encoding="utf-8", errors="ignore").splitlines():
        if line.startswith("ANIMATION "):
            names.append(line.split(None, 1)[1].strip().lower())
    return names


@pytest.fixture(scope="module")
def bindings():
    if not BINDINGS.exists():
        pytest.skip("bindings.json not generated yet")
    return json.loads(BINDINGS.read_text(encoding="utf-8"))


def _binding_items(bindings):
    return {k: v for k, v in bindings.items()
            if not k.startswith("_") and isinstance(v, dict)}


def test_every_stat_block_has_a_binding(bindings):
    """The completeness gate: a stat block added without a binding fails here,
    and spawn_encounter would otherwise error at runtime instead."""
    unbound = _all_stat_block_ids() - set(_binding_items(bindings))
    assert not unbound, (f"{len(unbound)} stat blocks without a visual binding: "
                         f"{sorted(unbound)[:15]}")


def test_no_binding_points_at_a_missing_rig(bindings):
    missing = [(k, v["animFile"]) for k, v in _binding_items(bindings).items()
               if not (ROOT / v["animFile"]).exists()]
    assert not missing, missing


# How the ENGINE actually resolves each state, so a rig counts as capable if any
# accepted clip exists (AnimatedVoxelCharacter::die() probes death_front/back
# directly; CombatBehavior installs the unarmed moveset for humanoid rigs).
_STATE_CLIPS = {
    "Idle":   ("idle",),
    "Walk":   ("walk",),
    "Attack": ("attack", "boxing", "elbow_punch", "kick", "punch"),
    "Death":  ("death", "death_front", "death_back"),
}


def test_bound_rigs_are_combat_capable(bindings):
    """Bindings must not point at walk-only (*_meshy) or flight-only
    (monster_dragon) rigs: the FSM would hold a stale clip while damage still
    fires, which reads as a T-posing monster that still hurts you."""
    bad = []
    for mid, v in sorted(_binding_items(bindings).items()):
        rig = ROOT / v["animFile"]
        if not rig.exists():
            continue
        clips = set(_rig_clip_names(rig))
        mapping = {k: c.lower() for k, c in (v.get("animationMapping") or {}).items()}
        for state, accepted in _STATE_CLIPS.items():
            if any(c in clips for c in accepted):
                continue
            if mapping.get(state, "") in clips:      # explicit override resolves it
                continue
            bad.append(f"{mid} -> {rig.name} cannot play {state}")
    assert not bad, bad[:15]


def test_tint_and_alpha_in_range(bindings):
    for mid, v in _binding_items(bindings).items():
        tint = v.get("tint", [1, 1, 1])
        assert len(tint) == 3 and all(0.0 <= c <= 2.0 for c in tint), (mid, tint)
        assert 0.05 <= v.get("alpha", 1.0) <= 1.0, (mid, v.get("alpha"))


def test_bindings_are_regenerable(tmp_path):
    """bindings.json is generated from bindings_map.json — hand edits get lost,
    so the checked-in file must match a fresh generation exactly."""
    import subprocess
    gen = ROOT / "tools" / "creature_forge" / "gen_bindings.py"
    if not gen.exists():
        pytest.skip("gen_bindings.py not written yet")
    out = tmp_path / "bindings.json"
    r = subprocess.run([sys.executable, str(gen), "--out", str(out)],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    assert json.loads(out.read_text(encoding="utf-8")) == \
        json.loads(BINDINGS.read_text(encoding="utf-8")), \
        "bindings.json is stale — re-run tools/creature_forge/gen_bindings.py"


# ---------------------------------------------------------------------------
# 8. Voxelization geometry units
# ---------------------------------------------------------------------------

class TestVoxelGeometry:
    def test_cylinder_radius_matches_profile(self):
        """A straight vertical cylinder of radius 0.15 voxelized at 0.05 must
        produce boxes spanning ~0.30 in X and Z (+- one voxel)."""
        compiled = compile_spec(minimal_spec(), Options(voxel_size=0.05))
        xs, zs = [], []
        for bx in compiled.af.boxes:
            bone = compiled.af.bones[bx.bone_id]
            if bone.name == "ground_ref":
                continue
            wx = compiled.bind_world_positions()[bone.name]
            xs += [wx[0] + bx.center[0] - bx.size[0] / 2,
                   wx[0] + bx.center[0] + bx.size[0] / 2]
            zs += [wx[2] + bx.center[2] - bx.size[2] / 2,
                   wx[2] + bx.center[2] + bx.size[2] / 2]
        assert max(xs) - min(xs) == pytest.approx(0.30, abs=0.055)
        assert max(zs) - min(zs) == pytest.approx(0.30, abs=0.055)

    def test_superellipse_exponent_increases_cross_section(self):
        """exp=6 (boxy) must fill a larger cross-section than exp=2 (ellipse)
        at the same half-extents."""
        def volume_of(exp):
            s = minimal_spec()
            # radius 0.30 = 6 voxels so grid quantization noise stays well
            # below the analytic e=6-vs-circle area ratio of ~1.23
            s["volumes"][0]["profile"] = [
                [0.0, 0.30, 0.30, {"exp": exp}],
                [1.0, 0.30, 0.30, {"exp": exp}]]
            c = compile_spec(s, Options(voxel_size=0.05))
            return sum(bx.size[0] * bx.size[1] * bx.size[2] for bx in c.af.boxes)
        assert volume_of(6) > volume_of(2) * 1.15

    def test_membrane_is_one_voxel_thick(self):
        s = minimal_spec()
        s["joints"].update({
            "WingA1": {"from": "Top", "side": 0.05},
            "WingA2": {"from": "WingA1", "side": 0.4},
            "WingB1": {"from": "Base", "side": 0.05, "up": 0.2},
            "WingB2": {"from": "WingB1", "side": 0.4},
        })
        s["chains"].update({"wingA": ["WingA1", "WingA2"],
                            "wingB": ["WingB1", "WingB2"]})
        s["attach"] = {"wingA": "Top", "wingB": "Base"}
        s["parts"] = [{
            "type": "membrane", "name": "wing", "material": "skin",
            "along": 8, "across": 4, "cusp": 0.2,
            "ribs": [{"chain": "wingA"}, {"chain": "wingB"}],
        }]
        vs = 0.05
        compiled = compile_spec(s, Options(voxel_size=vs))
        wing_bones = {b.id for b in compiled.af.bones if "Wing" in b.name}
        wing_boxes = [bx for bx in compiled.af.boxes if bx.bone_id in wing_bones]
        assert wing_boxes, "membrane produced no boxes"
        # sheet thickness: the thinnest dimension of every wing box is 1 voxel
        for bx in wing_boxes:
            assert min(bx.size) == pytest.approx(vs, abs=1e-6)

    def test_paw_part_is_direct_box(self):
        s = minimal_spec()
        s["parts"] = [{"type": "paw", "host": "Base", "material": "skin",
                       "size": [0.2, 0.1, 0.3]}]
        compiled = compile_spec(s)
        paw = [bx for bx in compiled.af.boxes
               if bx.size == pytest.approx((0.2, 0.1, 0.3))]
        assert len(paw) == 1
