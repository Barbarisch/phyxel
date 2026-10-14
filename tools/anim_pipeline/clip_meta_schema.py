"""Typed validation of `# clip_meta:` header lines against resources/anim/clip_meta_schema.json.

THE schema is that JSON file — the engine (graphics/ClipMetaSchema.cpp) reads the same one, so
a key the lint accepts is a key the runtime understands, and vice versa (A3 item 1,
docs/AnimationSystemV3Plan.md §4 A3).

Severities: unknown key → WARN (the runtime logs it once and ignores it); wrong type or a value
outside an enum → ERROR (the runtime ignores the value; a clip authored that way is broken).
Findings are (severity, message) tuples, the anim_lint convention.

    python tools/anim_pipeline/anim_lint.py metacheck resources/animated_characters/humanoid.anim
"""
from __future__ import annotations

import json
from functools import lru_cache
from pathlib import Path
from typing import Iterable

ROOT = Path(__file__).resolve().parents[2]
SCHEMA_PATH = ROOT / "resources" / "anim" / "clip_meta_schema.json"
META_PREFIX = "# clip_meta:"
_TRUE = {"1", "true"}
_FALSE = {"0", "false"}


@lru_cache(maxsize=1)
def schema() -> dict:
    return json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))["keys"]


def factor_keys() -> list[str]:
    return [k for k, spec in schema().items() if spec.get("factor")]


def validate_meta(meta: dict) -> list[tuple[str, str]]:
    """Validate one clip's key→value dict (values as the raw strings from the line)."""
    findings: list[tuple[str, str]] = []
    keys = schema()
    for key, raw in meta.items():
        value = str(raw)
        spec = keys.get(key)
        if spec is None:
            findings.append(("WARN", f"clip_meta: unknown key '{key}' (ignored by the runtime)"))
            continue
        t = spec["type"]
        if t == "float":
            try:
                float(value)
            except ValueError:
                findings.append(("ERROR", f"clip_meta: '{key}' expects a number, got '{value}'"))
        elif t == "bool":
            if value.lower() not in _TRUE | _FALSE:
                findings.append(("ERROR", f"clip_meta: '{key}' expects 0/1, got '{value}'"))
        elif t == "enum":
            if value.lower() not in {v.lower() for v in spec["values"]}:
                allowed = "|".join(spec["values"])
                findings.append(("ERROR", f"clip_meta: '{key}={value}' is not one of {allowed}"))
        # string: anything goes
    return findings


def parse_meta_line(line: str) -> tuple[str, dict] | None:
    """'# clip_meta: <clip> k=v k=v' → (clip, {k: v}) or None for other header lines."""
    if not line.startswith(META_PREFIX):
        return None
    parts = line[len(META_PREFIX):].split()
    if not parts:
        return None
    meta = {}
    for kv in parts[1:]:
        if "=" in kv:
            k, v = kv.split("=", 1)
            meta[k] = v
    return parts[0], meta


def iter_header_meta(path: Path) -> Iterable[tuple[str, dict]]:
    """Yield (clip, meta) for every clip_meta line, reading ONLY the header (stops at the first
    non-comment line) — a rig file is tens of MB and this must stay cheap."""
    with Path(path).open(encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line:
                continue
            if not line.startswith("#"):
                break
            parsed = parse_meta_line(line)
            if parsed:
                yield parsed


def validate_file(path: Path) -> list[tuple[str, str]]:
    findings: list[tuple[str, str]] = []
    for clip, meta in iter_header_meta(path):
        for sev, msg in validate_meta(meta):
            findings.append((sev, f"{clip}: {msg}"))
    return findings
