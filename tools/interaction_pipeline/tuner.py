"""
LLM tuner for the interaction pipeline.

Takes a SweepReport + DetectionResult and proposes:
  - profile_deltas:    edits to apply to InteractionProfile (sit_down_offset etc.)
  - engine_findings:   pass-through of engine_bug findings (NOT auto-applied)
  - confidence:        0..1 self-rated
  - recommend_continue:bool — whether another iteration is worth running
  - rationale:         short human-readable reason

The tuner NEVER auto-applies engine fixes — those are forwarded to
engine_fix_queue.json by the caller. The LLM is only allowed to propose profile
offset deltas; everything else goes to the human queue.

Backends (auto-selected by env vars, falls back to heuristic):
  - Anthropic (ANTHROPIC_API_KEY)
  - OpenAI    (OPENAI_API_KEY)
  - Heuristic (no key, always available)
"""
from __future__ import annotations

import json
import os
import statistics
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Any, Optional

from .detectors import DetectionResult, Finding, FindingKind
from .sweep import SweepReport


# ---------------------------------------------------------------------------
# Output schema
# ---------------------------------------------------------------------------

@dataclass
class ProfileDelta:
    """One offset edit to apply to InteractionProfileManager."""
    target: str         # "sit_down" | "sitting_idle" | "sit_stand_up"
    axis: str           # "x" | "y" | "z" | "facing_yaw"
    delta: float        # signed amount to add to the current value
    reason: str

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass
class TunerOutput:
    profile_deltas: list[ProfileDelta]
    engine_findings: list[dict[str, Any]]
    confidence: float
    recommend_continue: bool
    rationale: str
    backend: str  # which path produced this (anthropic / openai / heuristic)

    def to_dict(self) -> dict[str, Any]:
        return {
            "profile_deltas":    [d.to_dict() for d in self.profile_deltas],
            "engine_findings":   self.engine_findings,
            "confidence":        self.confidence,
            "recommend_continue": self.recommend_continue,
            "rationale":         self.rationale,
            "backend":           self.backend,
        }


# ---------------------------------------------------------------------------
# Strict JSON schema sent to the LLM
# ---------------------------------------------------------------------------

RESPONSE_SCHEMA = {
    "type": "object",
    "required": ["profile_deltas", "confidence", "recommend_continue", "rationale"],
    "additionalProperties": False,
    "properties": {
        "profile_deltas": {
            "type": "array",
            "items": {
                "type": "object",
                "required": ["target", "axis", "delta", "reason"],
                "additionalProperties": False,
                "properties": {
                    "target": {"type": "string", "enum": ["sit_down", "sitting_idle", "sit_stand_up"]},
                    "axis":   {"type": "string", "enum": ["x", "y", "z", "facing_yaw"]},
                    "delta":  {"type": "number"},
                    "reason": {"type": "string"},
                },
            },
        },
        "confidence":         {"type": "number", "minimum": 0, "maximum": 1},
        "recommend_continue": {"type": "boolean"},
        "rationale":          {"type": "string"},
    },
}


# ---------------------------------------------------------------------------
# Prompt construction
# ---------------------------------------------------------------------------

_SYSTEM_PROMPT = """You are the interaction-tuning diagnostician for the Phyxel voxel engine.

You receive structured telemetry from a sit-interaction sweep and detector \
findings already classified as PROFILE issues (offset tuning you may act on) \
or ENGINE_BUG issues (logic defects you must NOT try to fix via profile \
offsets). Engine bugs are listed for your awareness only.

Your job: propose minimal profile_deltas to fix the PROFILE findings. Each \
delta is a signed adjustment to add to one axis of one sit-state offset.

Rules:
- NEVER attempt to mask an engine_bug finding with profile deltas.
- Keep deltas small (typically |delta| <= 0.2).
- If no profile findings exist, return an empty profile_deltas array and set \
  recommend_continue=false.
- Reply with a SINGLE JSON object matching the provided schema. No prose, no \
  markdown, no commentary outside the JSON.
"""


def _build_user_message(
    report: SweepReport,
    detection: DetectionResult,
) -> str:
    # Compress: only ship summary stats + the findings + a few keyframes
    centroid_y_samples = []
    for f in report.frames:
        c = (f.telemetry or {}).get("centroid")
        if c:
            centroid_y_samples.append(float(c["y"]))
    summary = {
        "asset": report.asset_stem,
        "clips": report.clips,
        "samples_per_clip": report.samples_per_clip,
        "frame_count": len(report.frames),
        "centroid_y_min":    min(centroid_y_samples) if centroid_y_samples else None,
        "centroid_y_max":    max(centroid_y_samples) if centroid_y_samples else None,
        "centroid_y_median": statistics.median(centroid_y_samples) if centroid_y_samples else None,
    }
    payload = {
        "summary": summary,
        "profile_findings": [f.to_dict() for f in detection.profile_findings],
        "engine_findings":  [f.to_dict() for f in detection.engine_findings],
        "schema":           RESPONSE_SCHEMA,
    }
    return json.dumps(payload)


# ---------------------------------------------------------------------------
# Backend implementations
# ---------------------------------------------------------------------------

def _heuristic_tune(
    report: SweepReport,
    detection: DetectionResult,
) -> TunerOutput:
    """No-LLM fallback: maps the OFFSET_TOO_LOW/HIGH/FREE_BONE_PENETRATION
    profile findings to small Y deltas. Conservative — clamps to ±0.2."""
    deltas: list[ProfileDelta] = []
    rationale_bits: list[str] = []

    for f in detection.profile_findings:
        samples = (f.evidence or {}).get("samples", [])
        if f.id == "OFFSET_TOO_LOW":
            # Most-negative signed distance — raise Y.
            worst = min((float(s.get("signed_distance", 0.0)) for s in samples), default=0.0)
            magnitude = min(abs(worst), 0.2)
            if magnitude > 0:
                for target in ("sit_down", "sitting_idle", "sit_stand_up"):
                    deltas.append(ProfileDelta(target=target, axis="y", delta=+magnitude,
                                               reason=f"raise to resolve OFFSET_TOO_LOW (worst penetration {worst:.3f})"))
                rationale_bits.append(f"raised Y by {magnitude:.3f}")
        elif f.id == "OFFSET_TOO_HIGH":
            worst = max((float(s.get("signed_distance", 0.0)) for s in samples), default=0.0)
            magnitude = min(abs(worst), 0.2)
            if magnitude > 0:
                for target in ("sit_down", "sitting_idle", "sit_stand_up"):
                    deltas.append(ProfileDelta(target=target, axis="y", delta=-magnitude,
                                               reason=f"lower to resolve OFFSET_TOO_HIGH (worst clearance {worst:.3f})"))
                rationale_bits.append(f"lowered Y by {magnitude:.3f}")
        elif f.id == "FREE_BONE_PENETRATION":
            # Conservative XZ nudge backward (positive Z by 0.05).
            for target in ("sit_down", "sitting_idle", "sit_stand_up"):
                deltas.append(ProfileDelta(target=target, axis="z", delta=+0.05,
                                           reason="nudge Z back to resolve FREE_BONE_PENETRATION"))
            rationale_bits.append("nudged Z +0.05")
        elif f.id == "SEATED_HIPS_OFF_SEAT":
            # Apply suggested_offset_dy to the per-state Y offsets so Hips lands on the seat.
            dy = float((f.evidence or {}).get("suggested_offset_dy", 0.0))
            magnitude = max(-0.3, min(0.3, dy))
            if abs(magnitude) > 1e-4:
                for target in ("sit_down", "sitting_idle", "sit_stand_up"):
                    deltas.append(ProfileDelta(target=target, axis="y", delta=magnitude,
                                               reason=f"align Hips with seat (SEATED_HIPS_OFF_SEAT, dy={dy:+.3f})"))
                rationale_bits.append(f"hips-on-seat Y {magnitude:+.3f}")
        elif f.id == "SEATED_HIPS_OFF_SEAT_XZ":
            # Hips is off the seat horizontally — nudge X and/or Z back.
            ev = f.evidence or {}
            dx = float(ev.get("suggested_offset_dx", 0.0))
            dz = float(ev.get("suggested_offset_dz", 0.0))
            for axis, val in (("x", dx), ("z", dz)):
                mag = max(-0.4, min(0.4, val))
                if abs(mag) > 1e-4:
                    for target in ("sit_down", "sitting_idle", "sit_stand_up"):
                        deltas.append(ProfileDelta(target=target, axis=axis, delta=mag,
                                                   reason=f"centre Hips over seat (SEATED_HIPS_OFF_SEAT_XZ, {axis}={val:+.3f})"))
                    rationale_bits.append(f"hips-on-seat {axis.upper()} {mag:+.3f}")
        elif f.id == "SEATED_HIPS_OFF_SEAT_Y":
            dy = float((f.evidence or {}).get("suggested_offset_dy", 0.0))
            magnitude = max(-0.3, min(0.3, dy))
            if abs(magnitude) > 1e-4:
                for target in ("sit_down", "sitting_idle", "sit_stand_up"):
                    deltas.append(ProfileDelta(target=target, axis="y", delta=magnitude,
                                               reason=f"align Hips with seat top (SEATED_HIPS_OFF_SEAT_Y, dy={dy:+.3f})"))
                rationale_bits.append(f"hips-on-seat Y {magnitude:+.3f}")

    recommend = bool(deltas) and not detection.engine_findings
    rationale = (
        ("Heuristic adjustments: " + "; ".join(rationale_bits)) if rationale_bits
        else ("No profile findings to act on." if not detection.engine_findings
              else "Engine bugs detected; deferring profile tuning until engine fixes land.")
    )
    return TunerOutput(
        profile_deltas=deltas,
        engine_findings=[f.to_dict() for f in detection.engine_findings],
        confidence=0.5 if deltas else 0.8,
        recommend_continue=recommend,
        rationale=rationale,
        backend="heuristic",
    )


def _anthropic_tune(
    report: SweepReport,
    detection: DetectionResult,
    api_key: str,
    model: str = "claude-sonnet-4-20250514",
) -> Optional[TunerOutput]:
    try:
        import httpx
    except ImportError:
        return None
    user_msg = _build_user_message(report, detection)
    try:
        r = httpx.post(
            "https://api.anthropic.com/v1/messages",
            headers={
                "x-api-key": api_key,
                "anthropic-version": "2023-06-01",
                "content-type": "application/json",
            },
            json={
                "model": model,
                "max_tokens": 1024,
                "system": _SYSTEM_PROMPT,
                "messages": [{"role": "user", "content": user_msg}],
            },
            timeout=60.0,
        )
        r.raise_for_status()
        data = r.json()
        text = "".join(blk.get("text", "") for blk in data.get("content", []) if blk.get("type") == "text")
        parsed = _extract_json_object(text)
        if parsed is None:
            return None
        return _parse_llm_response(parsed, detection, backend="anthropic")
    except (httpx.HTTPError, ValueError, KeyError):
        return None


def _openai_tune(
    report: SweepReport,
    detection: DetectionResult,
    api_key: str,
    model: str = "gpt-4o",
) -> Optional[TunerOutput]:
    try:
        import httpx
    except ImportError:
        return None
    user_msg = _build_user_message(report, detection)
    try:
        r = httpx.post(
            "https://api.openai.com/v1/chat/completions",
            headers={
                "Authorization": f"Bearer {api_key}",
                "Content-Type": "application/json",
            },
            json={
                "model": model,
                "messages": [
                    {"role": "system", "content": _SYSTEM_PROMPT},
                    {"role": "user",   "content": user_msg},
                ],
                "response_format": {"type": "json_object"},
            },
            timeout=60.0,
        )
        r.raise_for_status()
        data = r.json()
        text = (data.get("choices") or [{}])[0].get("message", {}).get("content", "")
        parsed = _extract_json_object(text)
        if parsed is None:
            return None
        return _parse_llm_response(parsed, detection, backend="openai")
    except (httpx.HTTPError, ValueError, KeyError):
        return None


# ---------------------------------------------------------------------------
# Response parsing
# ---------------------------------------------------------------------------

def _extract_json_object(text: str) -> Optional[dict[str, Any]]:
    """Pull the first {...} object out of `text`, tolerating prose/markdown."""
    if not text:
        return None
    s = text.strip()
    # Strip ``` fences if present
    if s.startswith("```"):
        s = s.strip("`")
        if s.lower().startswith("json"):
            s = s[4:]
        s = s.strip()
    # Find first { and matching depth
    depth = 0
    start = -1
    for i, ch in enumerate(s):
        if ch == "{":
            if depth == 0:
                start = i
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0 and start >= 0:
                try:
                    return json.loads(s[start:i + 1])
                except json.JSONDecodeError:
                    return None
    return None


def _parse_llm_response(
    parsed: dict[str, Any],
    detection: DetectionResult,
    *,
    backend: str,
) -> TunerOutput:
    deltas: list[ProfileDelta] = []
    for d in parsed.get("profile_deltas", []) or []:
        try:
            target = str(d["target"])
            axis = str(d["axis"])
            delta = float(d["delta"])
            reason = str(d.get("reason", ""))
        except (KeyError, TypeError, ValueError):
            continue
        if target not in ("sit_down", "sitting_idle", "sit_stand_up"):
            continue
        if axis not in ("x", "y", "z", "facing_yaw"):
            continue
        # Hard safety clamp regardless of what the model says
        if axis == "facing_yaw":
            delta = max(-15.0, min(15.0, delta))
        else:
            delta = max(-0.3, min(0.3, delta))
        deltas.append(ProfileDelta(target=target, axis=axis, delta=delta, reason=reason))

    try:
        conf = float(parsed.get("confidence", 0.5))
    except (TypeError, ValueError):
        conf = 0.5
    conf = max(0.0, min(1.0, conf))
    recommend = bool(parsed.get("recommend_continue", False))
    rationale = str(parsed.get("rationale", ""))

    return TunerOutput(
        profile_deltas=deltas,
        engine_findings=[f.to_dict() for f in detection.engine_findings],
        confidence=conf,
        recommend_continue=recommend,
        rationale=rationale,
        backend=backend,
    )


# ---------------------------------------------------------------------------
# Public entry point
# ---------------------------------------------------------------------------

def tune(
    report: SweepReport,
    detection: DetectionResult,
    *,
    prefer_backend: Optional[str] = None,
) -> TunerOutput:
    """Run the tuner. Backend auto-selected from env vars; falls back to heuristic.

    prefer_backend ∈ {"anthropic", "openai", "heuristic", None}.
    None = best available.
    """
    if prefer_backend == "heuristic":
        return _heuristic_tune(report, detection)

    anth_key = os.environ.get("ANTHROPIC_API_KEY") or os.environ.get("PHYXEL_AI_API_KEY")
    oai_key = os.environ.get("OPENAI_API_KEY")

    if prefer_backend == "anthropic" or (prefer_backend is None and anth_key):
        if anth_key:
            out = _anthropic_tune(report, detection, anth_key)
            if out is not None:
                return out
    if prefer_backend == "openai" or (prefer_backend is None and oai_key):
        if oai_key:
            out = _openai_tune(report, detection, oai_key)
            if out is not None:
                return out

    return _heuristic_tune(report, detection)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _main() -> int:
    import argparse
    from .detectors import run_detectors
    from .sweep import FrameRecord

    p = argparse.ArgumentParser(description="Tune interaction profile from a sweep report.json")
    p.add_argument("report_json")
    p.add_argument("--backend", default=None,
                   choices=["anthropic", "openai", "heuristic"])
    args = p.parse_args()

    data = json.loads(Path(args.report_json).read_text(encoding="utf-8"))
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
    det = run_detectors(sr)
    out = tune(sr, det, prefer_backend=args.backend)
    print(json.dumps(out.to_dict(), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())
