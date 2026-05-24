"""
Deterministic detectors for the interaction pipeline.

Input:  SweepReport (from `sweep.run_sit_sweep`)
Output: list[Finding] classified as either `profile` (offset tuning) or
        `engine_bug` (logic defect in the engine itself).

The split is critical: profile findings flow back to InteractionProfileManager
deltas the tuner can apply automatically, while engine_bug findings go to
`engine_fix_queue.json` for human review and NEVER mutate the asset profile.

All detectors are deterministic numeric checks over the telemetry — the LLM
tuner sits on top of these, not in place of them.
"""
from __future__ import annotations

import enum
import json
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Any, Optional

from .sweep import SweepReport, FrameRecord


# ---------------------------------------------------------------------------
# Catalog & default tolerances
# ---------------------------------------------------------------------------

_CATALOG_PATH = Path(__file__).resolve().parent / "engine_fix_catalog.json"
_CONTACT_RULES_PATH = (
    Path(__file__).resolve().parents[2] / "resources" / "interactions" / "contact_rules.json"
)


def _load_catalog() -> dict[str, Any]:
    if _CATALOG_PATH.is_file():
        return json.loads(_CATALOG_PATH.read_text(encoding="utf-8"))
    return {"symptoms": {}}


def _load_contact_rules() -> dict[str, Any]:
    if _CONTACT_RULES_PATH.is_file():
        return json.loads(_CONTACT_RULES_PATH.read_text(encoding="utf-8"))
    return {"interactions": {}}


# ---------------------------------------------------------------------------
# Finding records
# ---------------------------------------------------------------------------

class FindingKind(str, enum.Enum):
    PROFILE = "profile"
    ENGINE_BUG = "engine_bug"


class Severity(str, enum.Enum):
    INFO = "info"
    WARN = "warn"
    ERROR = "error"


@dataclass
class Finding:
    id: str                       # e.g. "POSITION_SNAP_AT_CLIP_BOUNDARY"
    kind: FindingKind             # profile vs engine_bug
    severity: Severity
    message: str
    evidence: dict[str, Any] = field(default_factory=dict)
    suggested_action: Optional[str] = None
    catalog_ref: Optional[str] = None  # link into engine_fix_catalog.json

    def to_dict(self) -> dict[str, Any]:
        d = asdict(self)
        d["kind"] = self.kind.value
        d["severity"] = self.severity.value
        return d


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _centroid_xyz(frame: FrameRecord) -> Optional[tuple[float, float, float]]:
    c = (frame.telemetry or {}).get("centroid")
    if not c:
        return None
    return (float(c["x"]), float(c["y"]), float(c["z"]))


def _world_pos_xyz(frame: FrameRecord) -> Optional[tuple[float, float, float]]:
    """Engine-reported character world position (foot anchor). Distinct from
    `centroid` which is the average of bone AABBs and therefore includes pose
    motion baked into the clip. For "is the character sliding across the floor"
    questions, only `world_pos` is meaningful — centroid drift can be legitimate
    animation pose motion (e.g. leaning forward to stand up).
    """
    wp = (frame.telemetry or {}).get("world_pos")
    if not wp:
        return None
    return (float(wp["x"]), float(wp["y"]), float(wp["z"]))


def _dist3(a: tuple[float, float, float], b: tuple[float, float, float]) -> float:
    return ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2) ** 0.5


def _bones_by_name(frame: FrameRecord) -> dict[str, dict[str, Any]]:
    out: dict[str, dict[str, Any]] = {}
    for b in (frame.telemetry or {}).get("bones", []) or []:
        out[b.get("name", "")] = b
    return out


def _frames_grouped_by_clip(report: SweepReport) -> dict[str, list[FrameRecord]]:
    out: dict[str, list[FrameRecord]] = {}
    for f in report.frames:
        out.setdefault(f.clip, []).append(f)
    for clip in out:
        out[clip].sort(key=lambda fr: fr.t)
    return out


# ---------------------------------------------------------------------------
# Individual detectors
# ---------------------------------------------------------------------------

def detect_position_snap_at_clip_boundary(
    report: SweepReport,
    *,
    centroid_jump_threshold: float = 0.10,
    world_pos_jump_threshold: float = 0.05,
) -> list[Finding]:
    """Flag the engine bug where worldPosition jumps between adjacent clips.

    Pose-only discontinuities (centroid moves while world_pos is stable) are a
    clip-authoring concern, not an engine teleport, so they are not flagged here.
    A finding is only raised when both:
      * the centroid jump exceeds `centroid_jump_threshold`, AND
      * the world_pos jump exceeds `world_pos_jump_threshold` (or world_pos is
        unavailable, in which case we fall back to the centroid-only check for
        backwards compatibility with older sweep reports).
    """
    findings: list[Finding] = []
    grouped = _frames_grouped_by_clip(report)
    clip_order = report.clips
    for i in range(len(clip_order) - 1):
        a_clip, b_clip = clip_order[i], clip_order[i + 1]
        a_frames = grouped.get(a_clip, [])
        b_frames = grouped.get(b_clip, [])
        if not a_frames or not b_frames:
            continue
        a_last = a_frames[-1]
        b_first = b_frames[0]
        ca = _centroid_xyz(a_last)
        cb = _centroid_xyz(b_first)
        if ca is None or cb is None:
            continue
        d = _dist3(ca, cb)
        if d <= centroid_jump_threshold:
            continue

        wa = _world_pos_xyz(a_last)
        wb = _world_pos_xyz(b_first)
        if wa is not None and wb is not None:
            world_d = _dist3(wa, wb)
            if world_d <= world_pos_jump_threshold:
                # World position is stable across the boundary — the centroid
                # jump is a pose discontinuity baked into the clip data, not an
                # engine teleport. Skip.
                continue
        else:
            world_d = None

        findings.append(Finding(
            id="POSITION_SNAP_AT_CLIP_BOUNDARY",
            kind=FindingKind.ENGINE_BUG,
            severity=Severity.ERROR,
            message=(
                f"Centroid jumped {d:.3f} world units between "
                f"{a_clip}@t=1.0 and {b_clip}@t=0.0 (threshold {centroid_jump_threshold})."
            ),
            evidence={
                "from_clip": a_clip, "to_clip": b_clip,
                "from_centroid": ca, "to_centroid": cb, "distance": d,
                "from_world_pos": wa, "to_world_pos": wb, "world_distance": world_d,
            },
            suggested_action="Queue engine fix — see catalog entry.",
            catalog_ref="POSITION_SNAP_AT_CLIP_BOUNDARY",
        ))
    return findings


def detect_pose_feet_desync(
    report: SweepReport,
    *,
    foot_tolerance: float = 0.10,
    persistent_frames: int = 2,
) -> list[Finding]:
    """Flag the engine bug where feet drift from their support surface.

    Note on `signed_distance`: telemetry reports the distance from the foot AABB
    to the **nearest asset voxel** (the chair). In a real game the floor would
    also be a voxel and feet would touch it (distance ≈ 0), but the interaction
    editor renders the asset in isolation — no floor voxels. Feet therefore sit
    in midair below the chair seat and the reported `signed_distance` simply
    measures "how far below the chair seat are the feet" — not a desync.

    A real desync would manifest as `signed_distance` differing between left
    and right foot, or growing/shrinking persistently within a single clip
    (one foot drifting away from the support while the other stays). Use the
    asymmetry/drift as the actual signal.
    """
    findings: list[Finding] = []

    def _foot_dist(fr: FrameRecord, side: str) -> Optional[float]:
        feet = (fr.telemetry or {}).get("feet") or {}
        v = (feet.get(side) or {}).get("signed_distance")
        try:
            return float(v) if v is not None else None
        except (TypeError, ValueError):
            return None

    # Track per-foot drift WITHIN each clip (how much does each foot's distance
    # to the asset change across consecutive samples?).
    max_asymmetry = 0.0
    max_drift = 0.0
    worst_evidence: dict[str, Any] = {}
    grouped = _frames_grouped_by_clip(report)
    for clip_name, frames in grouped.items():
        prev_l: Optional[float] = None
        prev_r: Optional[float] = None
        for fr in frames:
            dl = _foot_dist(fr, "left")
            dr = _foot_dist(fr, "right")
            if dl is None or dr is None:
                continue
            asym = abs(dl - dr)
            if asym > max_asymmetry:
                max_asymmetry = asym
                if asym > foot_tolerance:
                    worst_evidence["asymmetry"] = {
                        "clip": clip_name, "t": fr.t,
                        "left": dl, "right": dr, "delta": asym,
                    }
            if prev_l is not None:
                dL = abs(dl - prev_l)
                dR = abs(dr - prev_r) if prev_r is not None else 0.0
                drift = max(dL, dR)
                if drift > max_drift:
                    max_drift = drift
                    if drift > foot_tolerance:
                        worst_evidence["drift"] = {
                            "clip": clip_name, "t": fr.t,
                            "d_left": dL, "d_right": dR,
                        }
            prev_l, prev_r = dl, dr

    if worst_evidence:
        findings.append(Finding(
            id="POSE_FEET_DESYNC",
            kind=FindingKind.ENGINE_BUG,
            severity=Severity.WARN,
            message=(
                f"Per-frame foot asymmetry or drift exceeded {foot_tolerance}. "
                f"max_asymmetry={max_asymmetry:.3f}, max_drift={max_drift:.3f}."
            ),
            evidence={
                "worst": worst_evidence,
                "tolerance": foot_tolerance,
                "note": (
                    "signed_distance is distance to asset voxels (the chair), "
                    "not to a floor. Absolute values are expected to be large "
                    "in the interaction editor; we flag asymmetry and drift only."
                ),
            },
            suggested_action="Queue engine fix — see catalog entry.",
            catalog_ref="POSE_FEET_DESYNC",
        ))
    return findings


def detect_initial_teleport(
    report: SweepReport,
    *,
    feet_above_seat_tolerance: float = -0.10,
) -> list[Finding]:
    """Flag the case where the engine snaps the character onto the seat at t=0.

    Evidence: at t=0 of stand_to_sit, the character should be standing in front
    of the chair — feet on the floor, well below the seat top. If both feet are
    AT or ABOVE the seat top (feet.y - seat.y >= feet_above_seat_tolerance,
    where the default −0.10 means within 10cm below the seat or higher), the
    engine teleported the character onto the seat instead of leaving them in
    front of it.

    Centroid-vs-seat is NOT used: a standing person's centroid (avg of all bone
    centers) is around pelvis height, which is naturally below the seat top of
    a typical chair — that does not indicate a teleport.
    """
    findings: list[Finding] = []
    if not report.frames:
        return findings
    first = report.frames[0]
    tele = first.telemetry or {}
    seat = tele.get("seat_anchor")
    feet = tele.get("feet") or {}
    if not seat or not feet:
        return findings
    seat_y = float(seat["y"])
    fl = (feet.get("left") or {}).get("y")
    fr = (feet.get("right") or {}).get("y")
    try:
        fl_y = float(fl) if fl is not None else None
        fr_y = float(fr) if fr is not None else None
    except (TypeError, ValueError):
        return findings
    if fl_y is None or fr_y is None:
        return findings
    # Both feet at-or-above the seat top => engine has lifted the character
    # onto the seat (or higher) at the very first frame.
    fl_delta = fl_y - seat_y
    fr_delta = fr_y - seat_y
    if fl_delta >= feet_above_seat_tolerance and fr_delta >= feet_above_seat_tolerance:
        findings.append(Finding(
            id="INITIAL_TELEPORT",
            kind=FindingKind.ENGINE_BUG,
            severity=Severity.ERROR,
            message=(
                f"At first sample (clip={first.clip}, t={first.t}), both feet are at "
                f"or above the seat top (left {fl_delta:+.3f}, right {fr_delta:+.3f}; "
                f"tolerance {feet_above_seat_tolerance:+.3f}) — engine appears to have "
                "teleported the character onto the seat instead of leaving them standing in front of it."
            ),
            evidence={
                "seat_y": seat_y,
                "feet_y": {"left": fl_y, "right": fr_y},
                "feet_delta_vs_seat": {"left": fl_delta, "right": fr_delta},
                "tolerance": feet_above_seat_tolerance,
            },
            suggested_action="Queue engine fix — see catalog entry.",
            catalog_ref="INITIAL_TELEPORT",
        ))
    return findings


def detect_offset_too_low_or_high(
    report: SweepReport,
    *,
    profile_max_penetration: float = 0.05,
    profile_min_clearance: float = 0.02,
) -> list[Finding]:
    """Flag profile-level offset issues using contact_rules taxonomy.

    These are the *profile* findings the tuner is allowed to act on:
      - OFFSET_TOO_LOW   — desired contact bones penetrate the seat too deeply
      - OFFSET_TOO_HIGH  — desired contact bones never reach the seat
      - FREE_BONE_PENETRATION — free bones intersect the asset (offset XY/rotation)
    """
    findings: list[Finding] = []
    rules = _load_contact_rules().get("interactions", {}).get("sit", {})
    contact_bones = set(rules.get("expected_contact_bones", []))
    free_bones = set(rules.get("expected_free_bones", []))
    tol = rules.get("tolerances", {})
    max_pen = float(tol.get("desired_contact_penetration_max", profile_max_penetration))
    min_clr = float(tol.get("free_clearance_min", profile_min_clearance))

    # Focus the offset judgement on the sitting_idle plateau where the pose
    # is at rest — transition clips will dip in/out by design.
    idle_frames = [f for f in report.frames if f.clip == "sitting_idle"]
    if not idle_frames:
        idle_frames = report.frames  # fallback

    too_low_evidence: list[dict[str, Any]] = []
    too_high_evidence: list[dict[str, Any]] = []
    free_penetration_evidence: list[dict[str, Any]] = []

    for fr in idle_frames:
        bones = _bones_by_name(fr)
        for name in contact_bones:
            b = bones.get(name)
            if not b:
                continue
            sd = b.get("signed_distance")
            cls = b.get("overlap_class")
            if sd is None:
                continue
            try:
                sd = float(sd)
            except (TypeError, ValueError):
                continue
            if cls == "desired_contact" and sd < -max_pen:
                too_low_evidence.append({
                    "frame": fr.keyframe_index, "clip": fr.clip, "t": fr.t,
                    "bone": name, "signed_distance": sd, "limit": -max_pen,
                })
            elif cls == "free" and sd > 0:
                too_high_evidence.append({
                    "frame": fr.keyframe_index, "clip": fr.clip, "t": fr.t,
                    "bone": name, "signed_distance": sd, "limit": 0.0,
                })
        for name in free_bones:
            b = bones.get(name)
            if not b:
                continue
            cls = b.get("overlap_class")
            sd = b.get("signed_distance")
            if cls in ("inside_asset", "inside_world") or (
                isinstance(sd, (int, float)) and float(sd) < -min_clr
            ):
                free_penetration_evidence.append({
                    "frame": fr.keyframe_index, "clip": fr.clip, "t": fr.t,
                    "bone": name, "overlap_class": cls, "signed_distance": sd,
                })

    if too_low_evidence:
        findings.append(Finding(
            id="OFFSET_TOO_LOW",
            kind=FindingKind.PROFILE,
            severity=Severity.WARN,
            message=(
                f"{len(too_low_evidence)} desired-contact bone samples penetrate the "
                f"seat beyond {max_pen} units — sit offset should be raised."
            ),
            evidence={"samples": too_low_evidence, "max_penetration": max_pen},
            suggested_action="Raise sit_down/sitting_idle/sit_stand_up Y offset.",
        ))
    if too_high_evidence:
        findings.append(Finding(
            id="OFFSET_TOO_HIGH",
            kind=FindingKind.PROFILE,
            severity=Severity.WARN,
            message=(
                f"{len(too_high_evidence)} desired-contact bone samples float above the "
                "seat — sit offset should be lowered."
            ),
            evidence={"samples": too_high_evidence},
            suggested_action="Lower sit_down/sitting_idle/sit_stand_up Y offset.",
        ))
    if free_penetration_evidence:
        findings.append(Finding(
            id="FREE_BONE_PENETRATION",
            kind=FindingKind.PROFILE,
            severity=Severity.ERROR,
            message=(
                f"{len(free_penetration_evidence)} free bone samples penetrate the asset. "
                "Likely XZ offset or facing/rotation issue."
            ),
            evidence={"samples": free_penetration_evidence},
            suggested_action="Adjust XZ offset or facing_yaw in profile.",
        ))
    return findings


# ---------------------------------------------------------------------------
# Sliding detector — root-motion / world-position desync within a clip
# ---------------------------------------------------------------------------

# States during which the character should be planted (no horizontal world
# translation). Drift within these states points at root motion being applied
# on top of a seat-anchored world position, or the world position being
# tweened toward something else mid-clip.
_PLANTED_STATES = {"sitting_idle", "sitting_down", "standing_up"}


def detect_horizontal_sliding(
    report: SweepReport,
    *,
    intra_clip_threshold: float = 0.10,    # per-sample horizontal drift
    cumulative_threshold: float = 0.20,    # total horizontal drift within a clip
    idle_threshold: float = 0.03,          # sitting_idle must be near-zero
) -> list[Finding]:
    """Flag horizontal world drift while the character is supposed to be planted.

    The user-visible symptom is "the entire character model slides in one
    direction" during the end of stand_to_sit or all of sit_to_stand. The
    cause is the engine applying root motion (or tweening the world
    position) while the IK pose is locked to a seat anchor.

    This is an ENGINE_BUG: no offset tweak in the profile can fix a clip
    whose root motion accumulates world translation.

    Reads `world_pos` (engine-reported character anchor), NOT `centroid`.
    Centroid drift can legitimately reflect pose motion baked into the clip
    (e.g. the character leaning forward to push off the chair during
    sit_to_stand). World position changes during a "planted" state, however,
    are always wrong — they cause the actual visible slide across the floor.
    """
    findings: list[Finding] = []
    grouped = _frames_grouped_by_clip(report)
    for clip, frames in grouped.items():
        if len(frames) < 2:
            continue

        # Look only at samples where the engine reported a planted state.
        # If state info is missing, fall back to clip-name heuristics.
        per_sample_drift: list[dict[str, Any]] = []
        cumulative_dxz = 0.0
        prev = None
        for fr in frames:
            c = _world_pos_xyz(fr)
            if c is None:
                continue
            state = (fr.telemetry or {}).get("state")
            if prev is not None:
                pc, pstate = prev
                # Consider this segment "planted" if either endpoint reports a
                # planted state, OR (no state info) the clip name says so.
                planted = (
                    (state in _PLANTED_STATES)
                    or (pstate in _PLANTED_STATES)
                    or (not state and clip in {"sitting_idle", "stand_to_sit", "sit_to_stand"})
                )
                if planted:
                    dx = c[0] - pc[0]; dz = c[2] - pc[2]
                    dxz = (dx * dx + dz * dz) ** 0.5
                    cumulative_dxz += dxz
                    per_sample_drift.append({
                        "t": fr.t, "dx": dx, "dz": dz, "dxz": dxz,
                        "from": pc, "to": c, "state": state,
                    })
            prev = (c, state)

        if not per_sample_drift:
            continue

        # Detect strong per-sample drift OR a monotonic accumulation.
        worst = max(per_sample_drift, key=lambda s: s["dxz"])
        # sitting_idle has a tighter bar.
        threshold = idle_threshold if clip == "sitting_idle" else intra_clip_threshold

        # Monotonic = signs of dx/dz are consistent across samples
        # (character drifts the same direction throughout).
        signs_x = [1 if s["dx"] > 0.005 else (-1 if s["dx"] < -0.005 else 0) for s in per_sample_drift]
        signs_z = [1 if s["dz"] > 0.005 else (-1 if s["dz"] < -0.005 else 0) for s in per_sample_drift]
        monotonic_x = len(signs_x) >= 2 and all(s == signs_x[0] != 0 for s in signs_x)
        monotonic_z = len(signs_z) >= 2 and all(s == signs_z[0] != 0 for s in signs_z)
        monotonic = monotonic_x or monotonic_z

        fires = False
        reasons: list[str] = []
        if worst["dxz"] > threshold:
            fires = True
            reasons.append(
                f"intra-clip drift {worst['dxz']:.3f} m at t={worst['t']:.2f} "
                f"(threshold {threshold:.3f})"
            )
        if cumulative_dxz > cumulative_threshold:
            fires = True
            reasons.append(
                f"cumulative horizontal drift {cumulative_dxz:.3f} m across {clip} "
                f"(threshold {cumulative_threshold:.3f})"
            )
        if monotonic and cumulative_dxz > 0.05:
            fires = True
            reasons.append(
                f"monotonic horizontal drift "
                f"(dx_sign={signs_x[0] if signs_x else 0}, dz_sign={signs_z[0] if signs_z else 0})"
            )

        if fires:
            findings.append(Finding(
                id="POSE_HORIZONTAL_SLIDING",
                kind=FindingKind.ENGINE_BUG,
                severity=Severity.ERROR,
                message=(
                    f"Character model slides horizontally during {clip}: "
                    + "; ".join(reasons)
                ),
                evidence={
                    "clip": clip,
                    "cumulative_dxz": cumulative_dxz,
                    "worst_segment": worst,
                    "per_sample": per_sample_drift,
                    "monotonic_x": monotonic_x,
                    "monotonic_z": monotonic_z,
                    "thresholds": {
                        "intra_clip": intra_clip_threshold,
                        "cumulative": cumulative_threshold,
                        "idle": idle_threshold,
                    },
                },
                suggested_action=(
                    "Investigate root-motion application in AnimatedVoxelCharacter "
                    "during the sit/stand transition — world position is changing "
                    "while the pose is anchored to the seat."
                ),
                catalog_ref="POSE_HORIZONTAL_SLIDING",
            ))
    return findings


# ---------------------------------------------------------------------------
# Aggregate runner
# ---------------------------------------------------------------------------

ALL_DETECTORS = (
    detect_position_snap_at_clip_boundary,
    detect_pose_feet_desync,
    detect_initial_teleport,
    detect_offset_too_low_or_high,
    detect_horizontal_sliding,
    # NB: detect_seated_posture is appended below — must be after its def.
)


# ---------------------------------------------------------------------------
# Seated-posture model
# ---------------------------------------------------------------------------
#
# What does a humanoid sitting on a chair look like, in world space?
#
#   1. BUTTOCKS on the seat: the Hips/Pelvis bone center sits within the
#      seat footprint horizontally and at the seat top vertically.
#         - |Hips.xz - Seat.xz| projected onto the seat plane should be
#           inside the seat's half-extent (default 0.30 m for a chair).
#         - Hips.y should be at seat.y ± a small tolerance (~0.10 m).
#
#   2. TORSO upright over the hips: Spine and Head stack vertically above
#      the Hips. Head.y is well above Hips.y (~0.6 m for an adult), and the
#      Spine/Head XZ stays close to the Hips XZ (no leaning off the chair).
#
#   3. THIGHS forward of hips, roughly horizontal: knees (LeftLeg /
#      RightLeg upper joint) lie FORWARD of the hips in character-facing
#      space, and at roughly the same height as the hips (±0.15 m).
#
#   4. SHINS vertical, FEET on the floor: feet are below the hips by at
#      least the shin length (~0.4 m for an adult), and feet stay roughly
#      under the knees in XZ.
#
# Each rule emits its own PROFILE finding so the tuner can attribute the
# fix to a specific axis of a specific state offset.
#
# Sign conventions for "forward":
#   - facing_yaw == 0 means the character faces world +Z (engine right-
#     handed, Y up, Z toward viewer). Forward unit vector is therefore
#     (sin(yaw), 0, cos(yaw)).
#   - To project a world-space delta (dx, dz) onto the character's forward
#     axis: forward = dx*sin(yaw) + dz*cos(yaw). A positive value means
#     "in front of" the reference point.
#
# All detectors are STATELESS: they receive the sweep report and emit
# Finding records with concrete suggested_offset_d{x,y,z} that the
# heuristic tuner can apply directly.
# ---------------------------------------------------------------------------


def _bone_center(tele: dict[str, Any], names: tuple[str, ...]) -> Optional[dict[str, float]]:
    """Find the first bone whose name matches any of the given suffixes."""
    for b in tele.get("bones", []) or []:
        bname = str(b.get("name", ""))
        for n in names:
            if bname == n or bname.endswith(":" + n) or bname.endswith(n):
                c = b.get("center") or {}
                try:
                    return {"x": float(c["x"]), "y": float(c["y"]), "z": float(c["z"])}
                except (KeyError, TypeError, ValueError):
                    return None
    return None


def _project_forward(dx: float, dz: float, yaw: float) -> float:
    """Project a world (dx,dz) onto the character's forward axis."""
    import math
    return dx * math.sin(yaw) + dz * math.cos(yaw)


def _median(xs: list[float]) -> float:
    s = sorted(xs)
    return s[len(s) // 2]


def detect_seated_posture(
    report: SweepReport,
    *,
    seat_half_extent: float = 0.30,
    hips_y_below_tol: float = 0.10,
    hips_y_above_tol: float = 0.15,
    torso_xz_tol: float = 0.20,
    head_above_hips_min: float = 0.40,
    knees_forward_min: float = 0.10,
    knees_y_tol: float = 0.35,
    feet_below_hips_min: float = 0.30,
    feet_xz_under_knees_tol: float = 0.20,
) -> list[Finding]:
    """Comprehensive seated-posture validator.

    Evaluates sitting_idle frames against the anatomical model above and
    emits one PROFILE finding per posture rule that fails. Each finding
    carries a suggested per-axis offset delta the tuner can apply.
    """
    findings: list[Finding] = []

    # Collect per-sample metrics across sitting_idle frames.
    hips_dx, hips_dy, hips_dz = [], [], []   # hips - seat in world axes
    hips_forward = []                        # hips - seat projected forward
    hips_lateral = []                        # hips - seat projected lateral
    head_minus_hips_y = []
    spine_xz_dist_from_hips = []
    knee_forward_of_hips = []
    knee_y_minus_hips_y = []
    feet_y_minus_hips_y = []
    feet_xz_minus_knee_xz = []
    sample_count = 0

    import math

    for fr in report.frames:
        if fr.clip != "sitting_idle":
            continue
        tele = fr.telemetry or {}
        seat = tele.get("seat_anchor") or {}
        try:
            sx, sy, sz = float(seat["x"]), float(seat["y"]), float(seat["z"])
        except (KeyError, TypeError, ValueError):
            continue
        yaw = float(tele.get("facing_yaw", 0.0) or 0.0)

        hips = _bone_center(tele, ("Hips", "Pelvis", "mixamorig:Hips"))
        if hips is None:
            continue
        sample_count += 1

        dx, dy, dz = hips["x"] - sx, hips["y"] - sy, hips["z"] - sz
        hips_dx.append(dx); hips_dy.append(dy); hips_dz.append(dz)
        hips_forward.append(_project_forward(dx, dz, yaw))
        # lateral = perpendicular component; rotate (dx,dz) by -yaw and take X
        hips_lateral.append(dx * math.cos(yaw) - dz * math.sin(yaw))

        head = _bone_center(tele, ("Head", "mixamorig:Head"))
        if head is not None:
            head_minus_hips_y.append(head["y"] - hips["y"])

        spine = _bone_center(tele, ("Spine2", "Spine1", "Spine", "mixamorig:Spine2"))
        if spine is not None:
            sd = math.hypot(spine["x"] - hips["x"], spine["z"] - hips["z"])
            spine_xz_dist_from_hips.append(sd)

        # Knees: average of LeftLeg + RightLeg upper joints (Mixamo names them "Leg").
        knees = []
        for side in ("LeftLeg", "RightLeg"):
            k = _bone_center(tele, (side, f"mixamorig:{side}"))
            if k is not None:
                knees.append(k)
        if len(knees) == 2:
            kc = {
                "x": (knees[0]["x"] + knees[1]["x"]) * 0.5,
                "y": (knees[0]["y"] + knees[1]["y"]) * 0.5,
                "z": (knees[0]["z"] + knees[1]["z"]) * 0.5,
            }
            knee_forward_of_hips.append(_project_forward(kc["x"] - hips["x"], kc["z"] - hips["z"], yaw))
            knee_y_minus_hips_y.append(kc["y"] - hips["y"])

            # Feet under knees: feet.y already in telemetry; we need feet.xz.
            # The telemetry exposes feet only via signed_distance + y, not xz.
            # So we approximate feet xz from leg bone if "LeftToeBase" / "RightToeBase"
            # is exported; otherwise we skip this rule for this frame.
            feet = []
            for side in ("LeftFoot", "RightFoot"):
                fb = _bone_center(tele, (side, f"mixamorig:{side}"))
                if fb is not None:
                    feet.append(fb)
            if len(feet) == 2:
                fc = {
                    "x": (feet[0]["x"] + feet[1]["x"]) * 0.5,
                    "y": (feet[0]["y"] + feet[1]["y"]) * 0.5,
                    "z": (feet[0]["z"] + feet[1]["z"]) * 0.5,
                }
                feet_y_minus_hips_y.append(fc["y"] - hips["y"])
                feet_xz_minus_knee_xz.append(math.hypot(fc["x"] - kc["x"], fc["z"] - kc["z"]))

    if sample_count == 0:
        return findings

    # ----- Rule 1a: Hips XZ inside seat footprint -----
    med_fwd = _median(hips_forward) if hips_forward else 0.0
    med_lat = _median(hips_lateral) if hips_lateral else 0.0
    horiz_dist = math.hypot(med_fwd, med_lat)
    if horiz_dist > seat_half_extent:
        # Suggest world-space dx, dz that pulls hips back to seat center.
        med_dx = _median(hips_dx)
        med_dz = _median(hips_dz)
        findings.append(Finding(
            id="SEATED_HIPS_OFF_SEAT_XZ",
            kind=FindingKind.PROFILE,
            severity=Severity.ERROR,
            message=(
                f"sitting_idle: Hips is {horiz_dist:.3f} m off the seat horizontally "
                f"(forward={med_fwd:+.3f}, lateral={med_lat:+.3f}; seat half-extent "
                f"={seat_half_extent:.2f}). Character is not on the chair."
            ),
            evidence={
                "median_hips_minus_seat_dx": med_dx,
                "median_hips_minus_seat_dz": med_dz,
                "median_forward": med_fwd,
                "median_lateral": med_lat,
                "horiz_distance": horiz_dist,
                "seat_half_extent": seat_half_extent,
                "samples": sample_count,
                "suggested_offset_dx": -med_dx,
                "suggested_offset_dz": -med_dz,
            },
            suggested_action=(
                "Set sit_stand_up_offset.x/.z so Hips lands inside the seat footprint."
            ),
            catalog_ref="SEATED_HIPS_OFF_SEAT_XZ",
        ))

    # ----- Rule 1b: Hips Y on seat surface -----
    if hips_dy:
        med_dy = _median(hips_dy)
        if med_dy < -hips_y_below_tol or med_dy > hips_y_above_tol:
            findings.append(Finding(
                id="SEATED_HIPS_OFF_SEAT_Y",
                kind=FindingKind.PROFILE,
                severity=Severity.ERROR,
                message=(
                    f"sitting_idle: Hips is {abs(med_dy):.3f} m "
                    f"{'below' if med_dy < 0 else 'above'} the seat surface "
                    f"(tolerance below={hips_y_below_tol:.2f} above={hips_y_above_tol:.2f})."
                ),
                evidence={
                    "median_hips_minus_seat_y": med_dy,
                    "samples": sample_count,
                    "suggested_offset_dy": -med_dy,
                },
                suggested_action="Adjust sit_stand_up_offset.y so Hips meets the seat top.",
                catalog_ref="SEATED_HIPS_OFF_SEAT_Y",
            ))

    # ----- Rule 2: Torso upright (Head above Hips, Spine over Hips) -----
    if head_minus_hips_y:
        med_head_dy = _median(head_minus_hips_y)
        if med_head_dy < head_above_hips_min:
            findings.append(Finding(
                id="SEATED_TORSO_NOT_UPRIGHT",
                kind=FindingKind.ENGINE_BUG,
                severity=Severity.ERROR,
                message=(
                    f"sitting_idle: Head is only {med_head_dy:+.3f} m above Hips "
                    f"(expected >= {head_above_hips_min:.2f}). Torso pose may be "
                    f"lying down, slumped, or the rig is rotated incorrectly."
                ),
                evidence={
                    "median_head_minus_hips_y": med_head_dy,
                    "minimum": head_above_hips_min,
                    "samples": len(head_minus_hips_y),
                },
                suggested_action=(
                    "Inspect sitting_idle clip — torso bones may not be retargeted "
                    "to an upright pose. This is an animation/rigging issue."
                ),
                catalog_ref="SEATED_TORSO_NOT_UPRIGHT",
            ))
    if spine_xz_dist_from_hips:
        med_spine_xz = _median(spine_xz_dist_from_hips)
        if med_spine_xz > torso_xz_tol:
            findings.append(Finding(
                id="SEATED_TORSO_LEANING_OFF",
                kind=FindingKind.ENGINE_BUG,
                severity=Severity.WARN,
                message=(
                    f"sitting_idle: Spine is {med_spine_xz:.3f} m away from Hips "
                    f"in XZ (tolerance {torso_xz_tol:.2f}). Torso is leaning off the "
                    f"chair or bone hierarchy is broken."
                ),
                evidence={
                    "median_spine_xz_distance": med_spine_xz,
                    "tolerance": torso_xz_tol,
                    "samples": len(spine_xz_dist_from_hips),
                },
                suggested_action="Inspect spine bone chain — likely animation issue.",
                catalog_ref="SEATED_TORSO_LEANING_OFF",
            ))

    # ----- Rule 3: Knees forward of Hips at roughly hip height -----
    if knee_forward_of_hips:
        med_knee_fwd = _median(knee_forward_of_hips)
        if med_knee_fwd < knees_forward_min:
            findings.append(Finding(
                id="SEATED_KNEES_NOT_FORWARD",
                kind=FindingKind.ENGINE_BUG,
                severity=Severity.ERROR,
                message=(
                    f"sitting_idle: Knees are only {med_knee_fwd:+.3f} m forward of "
                    f"Hips (expected >= {knees_forward_min:.2f}). Character may be "
                    f"facing the wrong way or sitting backwards on the chair."
                ),
                evidence={
                    "median_knee_forward": med_knee_fwd,
                    "minimum": knees_forward_min,
                    "samples": len(knee_forward_of_hips),
                },
                suggested_action=(
                    "Verify facing_yaw matches the chair-front normal, or that the "
                    "sitting_idle clip is the correct one for this archetype."
                ),
                catalog_ref="SEATED_KNEES_NOT_FORWARD",
            ))
    if knee_y_minus_hips_y:
        med_knee_dy = _median(knee_y_minus_hips_y)
        if abs(med_knee_dy) > knees_y_tol:
            findings.append(Finding(
                id="SEATED_THIGHS_NOT_HORIZONTAL",
                kind=FindingKind.ENGINE_BUG,
                severity=Severity.WARN,
                message=(
                    f"sitting_idle: Knees are {med_knee_dy:+.3f} m from Hips in Y "
                    f"(expected within ±{knees_y_tol:.2f}). Thighs are not "
                    f"horizontal — pose may be standing or kneeling, not sitting."
                ),
                evidence={
                    "median_knee_minus_hips_y": med_knee_dy,
                    "tolerance": knees_y_tol,
                    "samples": len(knee_y_minus_hips_y),
                },
                suggested_action="Inspect sitting_idle clip thigh keyframes.",
                catalog_ref="SEATED_THIGHS_NOT_HORIZONTAL",
            ))

    # ----- Rule 4: Feet below hips, roughly under knees -----
    if feet_y_minus_hips_y:
        med_feet_dy = _median(feet_y_minus_hips_y)
        if med_feet_dy > -feet_below_hips_min:
            findings.append(Finding(
                id="SEATED_FEET_NOT_BELOW_HIPS",
                kind=FindingKind.ENGINE_BUG,
                severity=Severity.ERROR,
                message=(
                    f"sitting_idle: Feet are only {med_feet_dy:+.3f} m below Hips "
                    f"(expected at most -{feet_below_hips_min:.2f}). Feet are "
                    f"level with or above the hips — pose is broken."
                ),
                evidence={
                    "median_feet_minus_hips_y": med_feet_dy,
                    "max_allowed": -feet_below_hips_min,
                    "samples": len(feet_y_minus_hips_y),
                },
                suggested_action="Inspect sitting_idle clip leg/foot keyframes.",
                catalog_ref="SEATED_FEET_NOT_BELOW_HIPS",
            ))
    if feet_xz_minus_knee_xz:
        med_feet_xz = _median(feet_xz_minus_knee_xz)
        if med_feet_xz > feet_xz_under_knees_tol:
            findings.append(Finding(
                id="SEATED_FEET_NOT_UNDER_KNEES",
                kind=FindingKind.ENGINE_BUG,
                severity=Severity.WARN,
                message=(
                    f"sitting_idle: Feet are {med_feet_xz:.3f} m off from knees in "
                    f"XZ (tolerance {feet_xz_under_knees_tol:.2f}). Shins are not "
                    f"vertical."
                ),
                evidence={
                    "median_feet_xz_from_knees": med_feet_xz,
                    "tolerance": feet_xz_under_knees_tol,
                    "samples": len(feet_xz_minus_knee_xz),
                },
                suggested_action="Inspect sitting_idle shin keyframes.",
                catalog_ref="SEATED_FEET_NOT_UNDER_KNEES",
            ))

    return findings


# Append posture detector to the aggregate list.
ALL_DETECTORS = ALL_DETECTORS + (detect_seated_posture,)


@dataclass
class DetectionResult:
    profile_findings: list[Finding]
    engine_findings: list[Finding]

    def to_dict(self) -> dict[str, Any]:
        return {
            "profile_findings": [f.to_dict() for f in self.profile_findings],
            "engine_findings":  [f.to_dict() for f in self.engine_findings],
            "profile_count":    len(self.profile_findings),
            "engine_count":     len(self.engine_findings),
        }


def run_detectors(report: SweepReport) -> DetectionResult:
    """Run all detectors over a sweep report and partition the findings."""
    findings: list[Finding] = []
    for det in ALL_DETECTORS:
        findings.extend(det(report))
    profile = [f for f in findings if f.kind == FindingKind.PROFILE]
    engine = [f for f in findings if f.kind == FindingKind.ENGINE_BUG]
    return DetectionResult(profile_findings=profile, engine_findings=engine)


def write_engine_fix_queue(
    result: DetectionResult,
    out_path: Path,
    *,
    sweep_report_path: Optional[Path] = None,
    append: bool = True,
) -> None:
    """Persist engine-bug findings to a human-review queue.

    Findings are appended (not overwritten) so multiple pipeline runs can
    accumulate evidence on the same recurring symptom.
    """
    out_path.parent.mkdir(parents=True, exist_ok=True)
    existing: dict[str, Any] = {"schema_version": "engine_fix_queue.v1", "entries": []}
    if append and out_path.is_file():
        try:
            existing = json.loads(out_path.read_text(encoding="utf-8"))
            existing.setdefault("entries", [])
        except (json.JSONDecodeError, OSError):
            pass

    catalog = _load_catalog().get("symptoms", {})
    for f in result.engine_findings:
        catalog_entry = catalog.get(f.catalog_ref or f.id, {}) if (f.catalog_ref or f.id) else {}
        existing["entries"].append({
            "finding": f.to_dict(),
            "catalog": catalog_entry,
            "sweep_report": str(sweep_report_path) if sweep_report_path else None,
        })

    out_path.write_text(json.dumps(existing, indent=2), encoding="utf-8")


# ---------------------------------------------------------------------------
# CLI for ad-hoc use against a saved report.json
# ---------------------------------------------------------------------------

def _main() -> int:
    import argparse

    p = argparse.ArgumentParser(description="Run interaction pipeline detectors over a sweep report.json")
    p.add_argument("report_json", help="Path to report.json produced by sweep.run_sit_sweep")
    p.add_argument("--queue-out", default=None,
                   help="Path to engine_fix_queue.json (default: alongside report)")
    args = p.parse_args()

    data = json.loads(Path(args.report_json).read_text(encoding="utf-8"))
    # Rehydrate just enough of SweepReport for detectors (they only need .frames/.clips)
    frames = [FrameRecord(**fr) for fr in data["frames"]]
    sr = SweepReport(
        schema_version=data.get("schema_version", "sweep.v1"),
        run_id=data.get("run_id", ""),
        asset_path=data.get("asset_path", ""),
        asset_stem=data.get("asset_stem", ""),
        interaction=data.get("interaction", "sit"),
        clips=data.get("clips", []),
        samples_per_clip=data.get("samples_per_clip", 0),
        started_at=data.get("started_at", ""),
        finished_at=data.get("finished_at", ""),
        duration_s=data.get("duration_s", 0.0),
        engine_pid=data.get("engine_pid"),
        engine_uptime_s=data.get("engine_uptime_s"),
        frames=frames,
        report_dir=data.get("report_dir", ""),
        screenshot_dir=data.get("screenshot_dir", ""),
        notes=data.get("notes", []),
    )
    result = run_detectors(sr)
    print(json.dumps(result.to_dict(), indent=2))
    queue_out = Path(args.queue_out) if args.queue_out else (
        Path(args.report_json).parent / "engine_fix_queue.json"
    )
    if result.engine_findings:
        write_engine_fix_queue(result, queue_out, sweep_report_path=Path(args.report_json))
        print(f"Engine fix queue updated: {queue_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())
