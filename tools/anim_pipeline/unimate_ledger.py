"""Review ledger for generated (UniMate) clips — the single source of truth for what a
generated clip may become (docs/UniMateIntegrationPlan.md M1b).

File: resources/animated_characters/unimate_review.json (fixed path; the editor panel reads
and writes the same file). Schema, version 1:

{
  "version": 1,
  "clips": {
    "<clip name>": {
      "rig": "humanoid.anim",            # basename of the .anim the clip lives in
      "verdict": "pending|accept|reject",
      "notes": "",
      "reviewed_at": null | ISO-8601,
      "promote_to": null | "<fsm clip name>",   # accept only; null = keep as a variant
      "replace_shipped": false,          # accept may overwrite a non-unimate clip only if true
      "prompt": "...", "seed": 10, "cfg": 3.0, "ckpt": 100000, "rep": 1,
      "source_glb": "...", "imported_at": ISO-8601,
      "eval": {"root_speed", "feet_speed", "root_vs_feet", "residual",
               "speed", "speed_source", "lint_errors", "lint_warns"},
      "applied": null | "kept|promoted:<name>|removed", "applied_at": null | ISO-8601
    }
  }
}

The importer creates the `pending` entry; the editor's Accept/Reject/Note buttons set the
verdict; `unimate_promote.py` applies verdicts and records `applied`.
"""
from __future__ import annotations

import datetime as _dt
import json
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DEFAULT_LEDGER = REPO / "resources" / "animated_characters" / "unimate_review.json"
VERDICTS = ("pending", "accept", "reject")


def now_iso() -> str:
    return _dt.datetime.now(_dt.timezone.utc).replace(microsecond=0).isoformat()


def load(path: Path = DEFAULT_LEDGER) -> dict:
    if not Path(path).exists():
        return {"version": 1, "clips": {}}
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    if data.get("version") != 1 or "clips" not in data:
        raise ValueError(f"{path}: unsupported ledger format")
    return data


def save(data: dict, path: Path = DEFAULT_LEDGER) -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    # sorted keys + trailing newline: stable diffs, and two writers (importer, editor)
    # converge on the same bytes for the same content.
    Path(path).write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def entry_from_import_report(report: dict, rig: str) -> dict:
    prov = report.get("provenance", {})
    st = report.get("stance_estimate", {}) or {}
    rt = report.get("root_travel") or {}
    return {
        "rig": rig,
        "verdict": "pending",
        "notes": "",
        "reviewed_at": None,
        "promote_to": None,
        "replace_shipped": False,
        "prompt": prov.get("prompt", ""),
        "seed": prov.get("seed"),
        "cfg": prov.get("cfg"),
        "ckpt": prov.get("ckpt"),
        "rep": prov.get("rep"),
        "source_glb": report.get("source"),
        "imported_at": now_iso(),
        "eval": {
            "root_speed": rt.get("speed"),
            "feet_speed": st.get("est_speed"),
            "root_vs_feet": report.get("root_vs_feet_mismatch"),
            "residual": st.get("residual"),
            "speed": report.get("speed"),
            "speed_source": report.get("speed_source"),
            "lint_errors": report.get("lint_errors"),
            "lint_warns": sum(1 for f in report.get("lint", []) if f.get("severity") == "WARN"),
        },
        "applied": None,
        "applied_at": None,
    }


def upsert_pending(data: dict, clip: str, entry: dict) -> None:
    """Create or refresh a clip's entry. A re-import resets the verdict to pending: the
    clip's data changed, so any earlier review no longer describes it."""
    old = data["clips"].get(clip)
    if old is not None and old.get("notes"):
        entry["notes"] = old["notes"] + " | (re-imported; verdict reset)"
    data["clips"][clip] = entry


def set_verdict(data: dict, clip: str, verdict: str, notes: str | None = None,
                promote_to: str | None = None, replace_shipped: bool | None = None) -> dict:
    if verdict not in VERDICTS:
        raise ValueError(f"verdict must be one of {VERDICTS}, got {verdict!r}")
    e = data["clips"].get(clip)
    if e is None:
        raise KeyError(f"{clip!r} is not in the ledger (import it first)")
    e["verdict"] = verdict
    e["reviewed_at"] = now_iso()
    if notes is not None:
        e["notes"] = notes
    if promote_to is not None:
        e["promote_to"] = promote_to or None
    if replace_shipped is not None:
        e["replace_shipped"] = bool(replace_shipped)
    return e
