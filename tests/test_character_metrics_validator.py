"""Stage 2 validator tests for tools/interaction_pipeline/character_metrics.py."""
from __future__ import annotations

import math

import pytest

from tools.interaction_pipeline.character_metrics import (
    CharacterIssue,
    CharacterMetrics,
    SCHEMA_VERSION,
    character_provenance,
    validate_character_metrics,
)


def _ok_metrics(**overrides):
    base = dict(
        schema_version=SCHEMA_VERSION,
        total_height=1.80,
        hip_height=0.95,
        eye_height=1.70,
        leg_length=0.90,
        arm_reach=0.78,
        shoulder_width=0.45,
        hip_width=0.32,
        body_depth=0.24,
        sitting_height=0.95,
    )
    base.update(overrides)
    return CharacterMetrics(**base)


def test_validate_clean_metrics_no_issues():
    m = _ok_metrics()
    issues = validate_character_metrics(m)
    assert issues == []


def test_validate_with_sit_kind_required_keys_clean():
    m = _ok_metrics()
    issues = validate_character_metrics(m, kinds=["sit"])
    # all sit-required keys are positive in _ok_metrics
    assert issues == []


def test_zero_required_key_blocks_sit():
    m = _ok_metrics(hip_width=0.0)
    issues = validate_character_metrics(m, kinds=["sit"])
    assert any(i.rule_id == "REQUIRED_KEY_ZERO" and i.key == "hip_width"
               for i in issues), issues


def test_negative_value_errors():
    m = _ok_metrics(leg_length=-0.5)
    issues = validate_character_metrics(m)
    assert any(i.rule_id == "VALUE_NEGATIVE" and i.key == "leg_length"
               for i in issues), issues


def test_implausibly_large_value_errors():
    m = _ok_metrics(total_height=42.0)
    issues = validate_character_metrics(m)
    assert any(i.rule_id == "VALUE_IMPLAUSIBLE" for i in issues), issues


def test_nan_value_errors():
    m = _ok_metrics(hip_height=float("nan"))
    issues = validate_character_metrics(m)
    assert any(i.rule_id == "VALUE_NAN" for i in issues), issues


def test_eye_above_head_errors():
    m = _ok_metrics(eye_height=2.5)  # > total_height 1.8
    issues = validate_character_metrics(m)
    assert any(i.rule_id == "EYE_ABOVE_HEAD" for i in issues), issues


def test_hip_above_eye_errors():
    m = _ok_metrics(hip_height=1.75)  # > eye_height 1.70
    issues = validate_character_metrics(m)
    assert any(i.rule_id == "HIP_ABOVE_EYE" for i in issues), issues


def test_schema_mismatch_errors():
    m = _ok_metrics()
    m.schema_version = "character_metrics.vBOGUS"
    issues = validate_character_metrics(m)
    assert any(i.rule_id == "SCHEMA_MISMATCH" for i in issues), issues


def test_character_provenance_snapshot():
    m = _ok_metrics()
    p = character_provenance(m, morphology="giant")
    assert p["morphology"] == "giant"
    assert p["total_height"] == 1.80
    assert p["schema_version"] == SCHEMA_VERSION


def test_unknown_kind_is_silently_ignored():
    # Unknown kinds shouldn't crash; they just contribute no required-key checks.
    m = _ok_metrics()
    issues = validate_character_metrics(m, kinds=["not_a_kind"])
    assert issues == []
