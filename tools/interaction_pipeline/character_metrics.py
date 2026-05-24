"""Character anatomical metrics — the "understanding" layer for any humanoid rig.

A CharacterMetrics describes the rig's proportions independently of any asset.
The interaction pipeline uses it to (a) author per-character profile offsets
(e.g., a giant needs a deeper sit-down offset because their hip-to-knee distance
is larger) and (b) run compatibility checks against asset metrics (e.g., this
chair's seat is too narrow for that giant's hip width).

The source of truth is the engine endpoint `/api/character/metrics`, which
computes everything from the live bone AABBs of the currently-loaded character.
This module is just a typed Python facade for that response.
"""
from __future__ import annotations

from dataclasses import dataclass, field, asdict
from typing import Any, Optional

import httpx


SCHEMA_VERSION = "character_metrics.v1"


@dataclass
class BoneExtent:
    """Per-bone bind-pose extent — used to reason about clearance (e.g., does
    this character's shoulders fit through a doorway)."""
    half_extents: tuple[float, float, float]
    center_y_above_floor: float


@dataclass
class CharacterMetrics:
    """Structured anatomical metrics, all in metres. See module docstring."""
    schema_version: str
    total_height: float          # crown of head down to lowest bone bottom
    hip_height: float            # Hips center Y above floor
    eye_height: float            # Head center Y above floor (proxy for eye)
    leg_length: float            # Hips center down to foot/shin bottom
    arm_reach: float             # Shoulder->ForeArm->Hand bind-pose chain length
    shoulder_width: float        # LeftArm <-> RightArm 3D distance
    hip_width: float             # LeftUpLeg <-> RightUpLeg 3D distance
    body_depth: float            # Spine box Z extent
    sitting_height: float        # Hips bottom -> Head top while sitting_idle (t=0.5)
    per_bone: dict[str, BoneExtent] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        d = asdict(self)
        d["per_bone"] = {
            k: {"half_extents": list(v.half_extents),
                "center_y_above_floor": v.center_y_above_floor}
            for k, v in self.per_bone.items()
        }
        return d

    @classmethod
    def from_api_response(cls, payload: dict[str, Any]) -> "CharacterMetrics":
        if not payload.get("success"):
            raise RuntimeError(f"character metrics endpoint reported failure: {payload!r}")
        schema = str(payload.get("schema_version", ""))
        if schema != SCHEMA_VERSION:
            raise RuntimeError(
                f"unsupported character metrics schema {schema!r}; expected {SCHEMA_VERSION!r}"
            )
        m = payload["metrics"]
        per_bone: dict[str, BoneExtent] = {}
        for name, body in (m.get("per_bone") or {}).items():
            he = body.get("half_extents") or {}
            per_bone[name] = BoneExtent(
                half_extents=(float(he.get("x", 0.0)),
                              float(he.get("y", 0.0)),
                              float(he.get("z", 0.0))),
                center_y_above_floor=float(body.get("center_y_above_floor", 0.0)),
            )
        return cls(
            schema_version=schema,
            total_height=float(m.get("total_height", 0.0)),
            hip_height=float(m.get("hip_height", 0.0)),
            eye_height=float(m.get("eye_height", 0.0)),
            leg_length=float(m.get("leg_length", 0.0)),
            arm_reach=float(m.get("arm_reach", 0.0)),
            shoulder_width=float(m.get("shoulder_width", 0.0)),
            hip_width=float(m.get("hip_width", 0.0)),
            body_depth=float(m.get("body_depth", 0.0)),
            sitting_height=float(m.get("sitting_height", 0.0)),
            per_bone=per_bone,
        )


def fetch_character_metrics(
    *,
    host: str = "localhost",
    port: int = 8090,
    timeout_s: float = 5.0,
) -> CharacterMetrics:
    """Fetch metrics for the engine's currently-loaded character."""
    url = f"http://{host}:{port}/api/character/metrics"
    with httpx.Client(timeout=timeout_s) as client:
        r = client.get(url)
        r.raise_for_status()
        return CharacterMetrics.from_api_response(r.json())


def spawn_for_test(
    appearance: dict[str, Any],
    *,
    host: str = "localhost",
    port: int = 8090,
    timeout_s: float = 30.0,
) -> CharacterMetrics:
    """Rebuild the active character with the given `CharacterAppearance` JSON,
    then return the post-rebuild metrics.

    `appearance` is the JSON form produced by
    `tools.interaction_pipeline.morphology_presets.MorphologyPreset.to_appearance_json()`.
    The engine endpoint queues this on the main thread and runs
    `setAppearance` + `rebuildWithAppearance`, then re-measures the rig so the
    caller can verify the morphology took effect (e.g., a `giant` preset
    should produce `total_height` ≈ 2.1m).
    """
    url = f"http://{host}:{port}/api/character/spawn_for_test"
    body = {"appearance": appearance}
    with httpx.Client(timeout=timeout_s) as client:
        r = client.post(url, json=body)
        r.raise_for_status()
        payload = r.json()
    if not payload.get("success"):
        raise RuntimeError(f"spawn_for_test reported failure: {payload!r}")

    # Shape the response into the CharacterMetrics shape `from_api_response`
    # expects (it's the same fields under "metrics" but no schema_version /
    # per_bone is returned by the spawn endpoint to keep the response small).
    shim = {
        "success": True,
        "schema_version": SCHEMA_VERSION,
        "metrics": {**payload["metrics"], "per_bone": {}},
    }
    return CharacterMetrics.from_api_response(shim)


# ---------------------------------------------------------------------------
# Stage 2 validation gate
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class CharacterIssue:
    """One problem found by :func:`validate_character_metrics`.

    Severity ``"error"`` means the tuner must not run; ``"warn"`` is logged
    but does not block.
    """
    severity: str
    key: str
    rule_id: str
    message: str


# Plausible upper bound for any anatomical measurement in metres. Anything
# above this is almost certainly a bug (e.g. metres-vs-centimetres mixup or
# unset value defaulting to garbage), not a giant.
_PLAUSIBLE_MAX_M: float = 8.0


def _required_keys_for_kind(kind_id: str) -> tuple[str, ...]:
    """Pull `required_character_keys` from a kind plugin. Avoids a hard import
    cycle by deferring the import until call time."""
    from tools.interaction_pipeline.interaction_kinds import get as _get
    try:
        return tuple(_get(kind_id).required_character_keys)
    except KeyError:
        return ()


def validate_character_metrics(
    metrics: CharacterMetrics,
    *,
    kinds: Optional[list[str]] = None,
) -> list[CharacterIssue]:
    """Stage 2 gate. Empty list = metrics are ratified for downstream stages.

    ``kinds`` lists the interaction kind ids the caller intends to run; the
    validator pulls each kind's ``required_character_keys`` and confirms the
    values are present and positive. With ``kinds=None`` only the always-on
    structural checks run.
    """
    issues: list[CharacterIssue] = []

    if metrics.schema_version != SCHEMA_VERSION:
        issues.append(CharacterIssue(
            "error", "schema_version", "SCHEMA_MISMATCH",
            f"Expected {SCHEMA_VERSION!r}, got {metrics.schema_version!r}"))

    # Always-on plausibility checks on the core fields. These guard against
    # an unloaded character (all zeros) or a corrupted spawn (NaN/huge).
    core_fields = {
        "total_height": metrics.total_height,
        "hip_height": metrics.hip_height,
        "eye_height": metrics.eye_height,
        "leg_length": metrics.leg_length,
        "arm_reach": metrics.arm_reach,
        "shoulder_width": metrics.shoulder_width,
        "hip_width": metrics.hip_width,
        "body_depth": metrics.body_depth,
        "sitting_height": metrics.sitting_height,
    }
    for key, val in core_fields.items():
        if val != val:  # NaN
            issues.append(CharacterIssue("error", key, "VALUE_NAN",
                                         f"{key} is NaN."))
            continue
        if val < 0.0:
            issues.append(CharacterIssue("error", key, "VALUE_NEGATIVE",
                                         f"{key}={val} is negative."))
        elif val > _PLAUSIBLE_MAX_M:
            issues.append(CharacterIssue("error", key, "VALUE_IMPLAUSIBLE",
                                         f"{key}={val:.3f}m exceeds plausible max "
                                         f"{_PLAUSIBLE_MAX_M:.1f}m."))

    # Per-kind required keys must be present and > 0.
    for kind_id in kinds or []:
        for key in _required_keys_for_kind(kind_id):
            val = core_fields.get(key)
            if val is None:
                issues.append(CharacterIssue(
                    "error", key, "REQUIRED_KEY_UNKNOWN",
                    f"Kind '{kind_id}' requires '{key}' which is not in CharacterMetrics."))
            elif val <= 0.0:
                issues.append(CharacterIssue(
                    "error", key, "REQUIRED_KEY_ZERO",
                    f"Kind '{kind_id}' requires '{key}' > 0; got {val}."))

    # Cross-field invariants. These catch broken rigs more cleanly than
    # range checks: e.g. a humanoid whose eye is below their hip is bogus.
    th = metrics.total_height
    if th > 0.0:
        if metrics.eye_height > th + 0.05:
            issues.append(CharacterIssue("error", "eye_height", "EYE_ABOVE_HEAD",
                                         f"eye_height {metrics.eye_height:.3f} > total_height {th:.3f}."))
        if metrics.hip_height > metrics.eye_height and metrics.eye_height > 0.0:
            issues.append(CharacterIssue("error", "hip_height", "HIP_ABOVE_EYE",
                                         f"hip_height {metrics.hip_height:.3f} > eye_height {metrics.eye_height:.3f}."))
        if metrics.sitting_height > 0.0 and metrics.sitting_height >= th:
            issues.append(CharacterIssue("warn", "sitting_height", "SITTING_HEIGHT_GE_TOTAL",
                                         f"sitting_height {metrics.sitting_height:.3f} ≥ total_height {th:.3f}; "
                                         f"character may be measured upright."))
    if metrics.hip_width > 0.0 and metrics.shoulder_width > 0.0:
        if metrics.hip_width > metrics.shoulder_width + 0.05:
            issues.append(CharacterIssue("warn", "hip_width", "HIP_WIDER_THAN_SHOULDER",
                                         f"hip_width {metrics.hip_width:.3f} > shoulder_width "
                                         f"{metrics.shoulder_width:.3f}; unusual proportions."))

    return issues


def character_provenance(
    metrics: CharacterMetrics,
    *,
    morphology: str = "",
) -> dict[str, Any]:
    """Snapshot of CharacterMetrics suitable for embedding in a persisted
    profile. Used by Stage 4 to record which character the profile was
    tuned against."""
    return {
        "schema_version": metrics.schema_version,
        "morphology": morphology,
        "total_height": metrics.total_height,
        "hip_height": metrics.hip_height,
        "eye_height": metrics.eye_height,
        "leg_length": metrics.leg_length,
        "arm_reach": metrics.arm_reach,
        "shoulder_width": metrics.shoulder_width,
        "hip_width": metrics.hip_width,
        "body_depth": metrics.body_depth,
        "sitting_height": metrics.sitting_height,
    }


__all__ = ["BoneExtent", "CharacterMetrics", "CharacterIssue", "SCHEMA_VERSION",
           "fetch_character_metrics", "spawn_for_test",
           "validate_character_metrics", "character_provenance"]
