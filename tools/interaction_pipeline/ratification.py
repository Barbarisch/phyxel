"""Stage 4 gate — combines Stages 1–3 into a single ratification record.

The tuner must not POST to ``/api/interaction/profile`` until
:func:`ratify_inputs` returns a record with ``ok == True``. The record
contains the validated AssetMetrics + CharacterMetrics + ClipBinding plus a
provenance snapshot suitable for writing alongside the persisted profile.

Provenance is written to a sidecar file (NOT through the engine endpoint, so
no engine schema changes are needed)::

    resources/interactions/<archetype>.provenance.json

Keyed by ``(template_name, point_id, character_id)``. When a future run
inspects a saved profile and the offsets look wrong, the sidecar tells you
exactly which asset/character/clip inputs produced it.
"""
from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Optional, Sequence

from .asset_metrics import (
    AssetIssue,
    AssetMetrics,
    asset_provenance,
    characterize_asset,
    validate_asset_metrics,
)
from .character_metrics import (
    CharacterIssue,
    CharacterMetrics,
    character_provenance,
    validate_character_metrics,
)
from .clip_selector import (
    BindingIssue,
    ClipBinding,
    ClipInfo,
    select_clips,
    validate_binding,
)


# Maps the .voxel `interaction_point` kind tag to the kind_id used by the
# interaction_kinds registry and the clip-selection spec. Add new mappings as
# kinds gain support.
KIND_TAG_TO_KIND_ID: dict[str, str] = {
    "seat":        "sit",
    "door_handle": "door_open",
    "container":   "container_open",
    "ladder":      "climb",
    "pickup":      "pickup",
    "lever":       "control",
}


@dataclass
class RatificationReport:
    """Combined Stage 1–3 result for a single (asset, point, character) combo."""
    asset_metrics: Optional[AssetMetrics]
    character_metrics: Optional[CharacterMetrics]
    clip_binding: Optional[ClipBinding]
    asset_issues: list[AssetIssue] = field(default_factory=list)
    character_issues: list[CharacterIssue] = field(default_factory=list)
    binding_issues: list[BindingIssue] = field(default_factory=list)
    extra_errors: list[str] = field(default_factory=list)

    @property
    def errors(self) -> list[str]:
        out: list[str] = []
        out.extend(f"asset[{i.point_id}|{i.rule_id}]: {i.message}"
                   for i in self.asset_issues if i.severity == "error")
        out.extend(f"character[{i.key}|{i.rule_id}]: {i.message}"
                   for i in self.character_issues if i.severity == "error")
        out.extend(f"binding[{i.alias}]: {i.message}"
                   for i in self.binding_issues if i.severity == "error")
        out.extend(self.extra_errors)
        return out

    @property
    def warnings(self) -> list[str]:
        out: list[str] = []
        out.extend(f"asset[{i.point_id}|{i.rule_id}]: {i.message}"
                   for i in self.asset_issues if i.severity == "warn")
        out.extend(f"character[{i.key}|{i.rule_id}]: {i.message}"
                   for i in self.character_issues if i.severity == "warn")
        out.extend(f"binding[{i.alias}]: {i.message}"
                   for i in self.binding_issues if i.severity == "warn")
        return out

    @property
    def ok(self) -> bool:
        return not self.errors and self.asset_metrics is not None \
            and self.character_metrics is not None \
            and self.clip_binding is not None

    def to_provenance(
        self,
        *,
        template_name: str,
        point_id: str,
        morphology: str = "",
    ) -> dict[str, Any]:
        """Snapshot suitable for writing into the provenance sidecar."""
        if not self.ok:
            raise RuntimeError("cannot snapshot a non-ratified record")
        assert self.asset_metrics is not None
        assert self.character_metrics is not None
        assert self.clip_binding is not None
        return {
            "asset": asset_provenance(self.asset_metrics, point_id),
            "character": character_provenance(self.character_metrics,
                                              morphology=morphology),
            "clip_binding": self.clip_binding.to_provenance(),
            "warnings": list(self.warnings),
        }


def _resolve_kind_id(kind_tag: str) -> Optional[str]:
    return KIND_TAG_TO_KIND_ID.get(kind_tag.lower())


def ratify_inputs(
    *,
    asset_path: Path,
    point_id: str,
    character_metrics: CharacterMetrics,
    available_clips: Sequence[Any],
    archetype: str = "humanoid_normal",
    morphology: str = "",
    character_id_for_clip_select: str = "player",
) -> RatificationReport:
    """Run Stages 1–3 and return the combined report.

    ``available_clips`` accepts whatever ``clip_selector.select_clips`` accepts
    (``ClipInfo``, strings, or ``/api/animation/list`` items).
    """
    report = RatificationReport(
        asset_metrics=None, character_metrics=None, clip_binding=None,
    )

    # Stage 1 — asset
    try:
        am = characterize_asset(asset_path)
    except Exception as e:
        report.extra_errors.append(f"asset_load_failed: {e!r}")
        return report
    report.asset_metrics = am
    report.asset_issues = validate_asset_metrics(am)

    # The specific point this run targets has to exist + carry a recognised kind.
    target_point = next((p for p in am.interaction_points if p.point_id == point_id), None)
    if target_point is None:
        report.extra_errors.append(
            f"point_id '{point_id}' not found in asset '{am.template_name}' "
            f"(declared: {[p.point_id for p in am.interaction_points]})"
        )
        return report

    kind_id = _resolve_kind_id(target_point.kind)
    if kind_id is None:
        report.extra_errors.append(
            f"asset point '{point_id}' uses kind tag '{target_point.kind}' which has "
            f"no kind_id mapping (known: {sorted(KIND_TAG_TO_KIND_ID)})"
        )
        return report

    # Stage 2 — character (kind-gated)
    report.character_metrics = character_metrics
    report.character_issues = validate_character_metrics(
        character_metrics, kinds=[kind_id],
    )

    # Stage 3 — clip selection
    try:
        binding = select_clips(
            kind_id,
            available_clips,
            archetype=archetype,
            character_id=character_id_for_clip_select,
        )
    except Exception as e:
        report.extra_errors.append(f"clip_select_failed: {e!r}")
        return report
    report.clip_binding = binding
    report.binding_issues = validate_binding(binding)

    return report


# ---------------------------------------------------------------------------
# Provenance sidecar I/O
# ---------------------------------------------------------------------------

def provenance_sidecar_path(
    *,
    archetype: str,
    base_dir: Optional[Path] = None,
) -> Path:
    base = base_dir or Path(__file__).resolve().parents[2] / "resources" / "interactions"
    return base / f"{archetype}.provenance.json"


def _make_key(template_name: str, point_id: str, character_id: str) -> str:
    cid = character_id or "_base_"
    return f"{template_name}|{point_id}|{cid}"


def write_provenance(
    report: RatificationReport,
    *,
    archetype: str,
    template_name: str,
    point_id: str,
    morphology: str = "",
    base_dir: Optional[Path] = None,
) -> Path:
    """Write/merge the ratified record into the sidecar file.

    The sidecar is a flat dict keyed by ``template|point|character`` so a
    profile entry can be traced back to its inputs without scanning the
    entire file.
    """
    if not report.ok:
        raise RuntimeError("refusing to write provenance for a non-ratified record")
    path = provenance_sidecar_path(archetype=archetype, base_dir=base_dir)
    path.parent.mkdir(parents=True, exist_ok=True)

    try:
        existing = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(existing, dict):
            existing = {"records": {}}
    except (FileNotFoundError, json.JSONDecodeError):
        existing = {"records": {}, "schema_version": "interaction_provenance.v1",
                    "archetype": archetype}
    records = existing.setdefault("records", {})
    key = _make_key(template_name, point_id, morphology)
    records[key] = report.to_provenance(
        template_name=template_name,
        point_id=point_id,
        morphology=morphology,
    )
    existing.setdefault("schema_version", "interaction_provenance.v1")
    existing.setdefault("archetype", archetype)

    path.write_text(json.dumps(existing, indent=2, sort_keys=True), encoding="utf-8")
    return path


def read_provenance(
    *,
    archetype: str,
    template_name: str,
    point_id: str,
    morphology: str = "",
    base_dir: Optional[Path] = None,
) -> Optional[dict[str, Any]]:
    """Inverse of :func:`write_provenance`. Returns ``None`` if no record."""
    path = provenance_sidecar_path(archetype=archetype, base_dir=base_dir)
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (FileNotFoundError, json.JSONDecodeError):
        return None
    records = data.get("records") if isinstance(data, dict) else None
    if not isinstance(records, dict):
        return None
    return records.get(_make_key(template_name, point_id, morphology))


__all__ = [
    "KIND_TAG_TO_KIND_ID",
    "RatificationReport",
    "ratify_inputs",
    "write_provenance",
    "read_provenance",
    "provenance_sidecar_path",
]
