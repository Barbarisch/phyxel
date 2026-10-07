"""A3 item 1 (docs/AnimationSystemV3Plan.md §4 A3): `# clip_meta:` lines have ONE typed schema,
resources/anim/clip_meta_schema.json, read by the engine and by the lint. A wrong-typed value or
a value outside an enum is a lint ERROR; an unknown key is a WARN; every shipped rig validates
clean. RED 2026-09-30: the validator module does not exist (ImportError)."""
from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.anim_pipeline import clip_meta_schema as cms  # noqa: E402


def _sev(findings):
    return sorted(s for s, _ in findings)


def test_schema_file_parses_and_every_key_is_typed():
    schema = json.loads((ROOT / "resources/anim/clip_meta_schema.json").read_text(encoding="utf-8"))
    assert schema["version"] == 1
    for key, spec in schema["keys"].items():
        assert spec["type"] in ("float", "bool", "string", "enum"), key
        if spec["type"] == "enum":
            assert spec["values"], f"{key}: enum without values"
    # the factor vocabulary the plan names must be present
    for k in ("gait", "state", "grip", "load", "condition", "mood", "role", "mask", "additive"):
        assert schema["keys"][k].get("factor"), k


def test_wrong_type_is_an_error():
    # grip is an enum: a number is neither a type match nor an allowed value
    assert "ERROR" in _sev(cms.validate_meta({"grip": "1.5"}))
    assert "ERROR" in _sev(cms.validate_meta({"hitFrameFraction": "abc"}))
    assert "ERROR" in _sev(cms.validate_meta({"additive": "maybe"}))


def test_enum_value_outside_set_is_an_error_and_case_insensitive_inside():
    assert "ERROR" in _sev(cms.validate_meta({"grip": "three_handed"}))
    assert cms.validate_meta({"grip": "2H_HEAVY"}) == []


def test_unknown_key_is_a_warning_not_an_error():
    f = cms.validate_meta({"bogusKey": "1"})
    assert _sev(f) == ["WARN"]


def test_valid_base_and_layer_lines_validate_clean():
    assert cms.validate_meta({"role": "base", "gait": "biped", "state": "walk", "type": "locomotion"}) == []
    assert cms.validate_meta({"role": "layer", "grip": "2h_heavy", "mask": "upper", "additive": "1"}) == []


@pytest.mark.parametrize("rig", sorted((ROOT / "resources/animated_characters").glob("*.anim")),
                         ids=lambda p: p.stem)
def test_every_shipped_rig_validates_without_errors(rig):
    findings = cms.validate_file(rig)
    errors = [m for s, m in findings if s == "ERROR"]
    assert errors == [], errors


# ---------------------------------------------------------------------------------------------
# Roadmap R1 decisions 2 and 5 (docs/CharacterAnimationRoadmap.md): creature attack kinds and the
# per-clip review status are typed schema keys. RED 2026-10-01: neither key exists.
# ---------------------------------------------------------------------------------------------
ATTACK_KINDS = ["bite", "claw", "tail", "slam", "gore", "sting", "beak", "talons", "hooves",
                "tentacle", "touch", "breath", "spit", "constrict", "weapon_melee", "weapon_ranged", "spell", "aura"]


def test_attack_kind_is_a_factor_enum_with_the_roadmap_kinds():
    schema = json.loads((ROOT / "resources/anim/clip_meta_schema.json").read_text(encoding="utf-8"))
    spec = schema["keys"]["attackKind"]
    assert spec["type"] == "enum" and spec.get("factor") is True
    assert spec["values"] == ATTACK_KINDS
    assert cms.validate_meta({"attackKind": "bite"}) == []
    assert "ERROR" in _sev(cms.validate_meta({"attackKind": "nibble"}))


def test_action_is_a_tool_only_string():
    schema = json.loads((ROOT / "resources/anim/clip_meta_schema.json").read_text(encoding="utf-8"))
    spec = schema["keys"]["action"]
    assert spec["type"] == "string" and spec.get("toolOnly") is True
    assert cms.validate_meta({"action": "rage"}) == []


def test_review_is_a_tool_only_status_enum():
    schema = json.loads((ROOT / "resources/anim/clip_meta_schema.json").read_text(encoding="utf-8"))
    spec = schema["keys"]["review"]
    assert spec["type"] == "enum" and spec.get("toolOnly") is True and not spec.get("factor")
    assert spec["values"] == ["approved", "shipped", "draft", "rejected"]
    assert cms.validate_meta({"review": "draft"}) == []
    assert "ERROR" in _sev(cms.validate_meta({"review": "maybe"}))

