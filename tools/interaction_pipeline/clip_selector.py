"""Stage 3 of the interaction pipeline — animation selection.

Bridges the gap between a kind's profile-key *aliases* (e.g. ``sit_down``,
``sit_stand_up``) and the actual *clip names* a particular character ships
(e.g. ``stand_to_sit``, ``sit_to_stand``). This used to be done implicitly by
``sweep.SIT_CLIPS`` hardcoding the real clip names and by hardcoded
``required_clip_aliases`` in the kind plugins. With more characters, more
assets, and per-morphology overrides those two strings drifted out of sync
and the tuner happily wrote offsets against mismatched clips.

The selector solves it declaratively:

1. Load ``resources/interactions/clip_selection.json`` (per-kind, per-alias
   regex candidates with weights).
2. List the clips the loaded character exposes via ``/api/animation/list``.
3. For each required alias score every clip; keep the best match.
4. Return a :class:`ClipBinding` capturing the alias -> clip_name mapping plus
   per-alias score and rationale.

The binding is the **only** sanctioned input to stage 4 (the tuner). The
tuner must not run until every required alias has bound and the binding
passes :func:`validate_binding`.
"""
from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable, Mapping, Optional, Sequence

import httpx

# Public location of the spec; loaded once and cached.
DEFAULT_SPEC_PATH = (
    Path(__file__).resolve().parents[2]
    / "resources" / "interactions" / "clip_selection.json"
)

# Minimum score for an alias binding to count as "good enough". Anything
# below this is reported as a warning so the tuner can decide whether to
# proceed (e.g. heuristic fallback only) or refuse.
MIN_BINDING_SCORE: float = 40.0


# ---------------------------------------------------------------------------
# Data classes
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class ClipInfo:
    """Subset of clip metadata used by the selector."""
    name: str
    duration: float = 0.0
    speed: float = 1.0
    index: int = -1

    @classmethod
    def from_api(cls, item: Mapping[str, Any]) -> "ClipInfo":
        return cls(
            name=str(item.get("name", "")),
            duration=float(item.get("duration", 0.0) or 0.0),
            speed=float(item.get("speed", 1.0) or 1.0),
            index=int(item.get("index", -1)),
        )


@dataclass(frozen=True)
class CandidateMatch:
    """One alias -> clip resolution attempt."""
    clip_name: str
    score: float
    pattern: str
    rationale: str = ""


@dataclass
class AliasBinding:
    alias: str
    candidates: list[CandidateMatch] = field(default_factory=list)

    @property
    def best(self) -> Optional[CandidateMatch]:
        return self.candidates[0] if self.candidates else None


@dataclass
class ClipBinding:
    """Stage 3 output: per-alias clip resolution for a (character, kind) pair.

    ``bindings`` always has an entry for every alias the kind requires, even
    when no candidate matched (in that case ``best`` is ``None``).
    """
    kind_id: str
    archetype: str
    character_id: str
    available_clips: tuple[str, ...]
    bindings: dict[str, AliasBinding] = field(default_factory=dict)

    # ----- convenience -----
    def clip_for(self, alias: str) -> Optional[str]:
        b = self.bindings.get(alias)
        return b.best.clip_name if b and b.best else None

    def to_map(self) -> dict[str, Optional[str]]:
        return {a: self.clip_for(a) for a in self.bindings}

    def to_provenance(self) -> dict[str, Any]:
        """Compact dict suitable for embedding in the persisted profile."""
        out: dict[str, Any] = {
            "kind_id": self.kind_id,
            "archetype": self.archetype,
            "character_id": self.character_id,
            "available_clips": list(self.available_clips),
            "aliases": {},
        }
        for alias, b in self.bindings.items():
            best = b.best
            out["aliases"][alias] = {
                "clip": best.clip_name if best else None,
                "score": best.score if best else 0.0,
                "pattern": best.pattern if best else None,
                "rationale": best.rationale if best else "no match",
            }
        return out


# ---------------------------------------------------------------------------
# Spec loading
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class _PatternEntry:
    regex: re.Pattern[str]
    weight: float
    rationale: str
    raw: str


@dataclass(frozen=True)
class _KindSpec:
    aliases: Mapping[str, tuple[_PatternEntry, ...]]


def load_spec(path: Optional[Path] = None) -> dict[str, _KindSpec]:
    """Load and compile the alias selection spec.

    Raises ``FileNotFoundError`` if the spec file is missing and
    ``ValueError`` on malformed JSON; both indicate a setup bug, not an
    in-flight failure.
    """
    spec_path = path or DEFAULT_SPEC_PATH
    raw = json.loads(spec_path.read_text(encoding="utf-8"))
    kinds_raw = raw.get("kinds") or {}
    if not isinstance(kinds_raw, dict):
        raise ValueError(f"{spec_path}: 'kinds' must be a mapping")

    out: dict[str, _KindSpec] = {}
    for kind_id, kind_block in kinds_raw.items():
        aliases_raw = (kind_block or {}).get("aliases") or {}
        compiled: dict[str, tuple[_PatternEntry, ...]] = {}
        for alias, patterns in aliases_raw.items():
            entries: list[_PatternEntry] = []
            for p in patterns or []:
                pat = str(p.get("pattern", "")).strip()
                if not pat:
                    continue
                entries.append(_PatternEntry(
                    regex=re.compile(pat, re.IGNORECASE),
                    weight=float(p.get("weight", 0.0)),
                    rationale=str(p.get("rationale", "")),
                    raw=pat,
                ))
            compiled[alias] = tuple(entries)
        out[kind_id] = _KindSpec(aliases=compiled)
    return out


# ---------------------------------------------------------------------------
# Selection
# ---------------------------------------------------------------------------

def select_clips(
    kind_id: str,
    available_clips: Iterable[ClipInfo | str | Mapping[str, Any]],
    *,
    spec: Optional[Mapping[str, _KindSpec]] = None,
    archetype: str = "",
    character_id: str = "player",
    required_aliases: Optional[Sequence[str]] = None,
) -> ClipBinding:
    """Pick the best clip for every alias required by ``kind_id``.

    ``available_clips`` accepts a mix of :class:`ClipInfo`, raw strings, or
    ``/api/animation/list`` items.

    ``required_aliases`` overrides the spec's alias set (useful when the
    kind plugin requires a subset for a specific scenario).
    """
    spec_map = spec if spec is not None else load_spec()
    if kind_id not in spec_map:
        raise KeyError(f"No selection spec for kind '{kind_id}'")
    kind_spec = spec_map[kind_id]

    clips: list[ClipInfo] = []
    for c in available_clips:
        if isinstance(c, ClipInfo):
            clips.append(c)
        elif isinstance(c, str):
            clips.append(ClipInfo(name=c))
        elif isinstance(c, Mapping):
            clips.append(ClipInfo.from_api(c))
        # else: silently skip — selector is read-only and shouldn't crash on
        # surprising payloads; tuner gate will reject incomplete bindings.

    binding = ClipBinding(
        kind_id=kind_id,
        archetype=archetype,
        character_id=character_id,
        available_clips=tuple(c.name for c in clips),
    )
    alias_iter: Sequence[str] = (
        required_aliases if required_aliases is not None
        else list(kind_spec.aliases.keys())
    )
    for alias in alias_iter:
        patterns = kind_spec.aliases.get(alias, ())
        matches: list[CandidateMatch] = []
        for clip in clips:
            best_for_clip: Optional[CandidateMatch] = None
            for entry in patterns:
                if entry.regex.search(clip.name):
                    cand = CandidateMatch(
                        clip_name=clip.name,
                        score=entry.weight,
                        pattern=entry.raw,
                        rationale=entry.rationale,
                    )
                    if best_for_clip is None or cand.score > best_for_clip.score:
                        best_for_clip = cand
            if best_for_clip is not None:
                matches.append(best_for_clip)
        matches.sort(key=lambda m: m.score, reverse=True)
        binding.bindings[alias] = AliasBinding(alias=alias, candidates=matches)
    return binding


# ---------------------------------------------------------------------------
# Validation gate
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class BindingIssue:
    severity: str  # "error" | "warn"
    alias: str
    message: str


def validate_binding(
    binding: ClipBinding,
    *,
    min_score: float = MIN_BINDING_SCORE,
) -> list[BindingIssue]:
    """Gate Stage 3 -> Stage 4. Returns empty list when binding is ratified."""
    issues: list[BindingIssue] = []
    for alias, b in binding.bindings.items():
        best = b.best
        if best is None:
            issues.append(BindingIssue(
                severity="error", alias=alias,
                message=f"No clip in character ({len(binding.available_clips)} available) "
                        f"matched any pattern for alias '{alias}'."))
            continue
        if best.score < min_score:
            issues.append(BindingIssue(
                severity="warn", alias=alias,
                message=f"Alias '{alias}' bound to '{best.clip_name}' at low score "
                        f"{best.score:.0f} (min {min_score:.0f}); review pattern weights."))
    # Detect collisions: two aliases bound to the same clip is suspicious.
    by_clip: dict[str, list[str]] = {}
    for alias, b in binding.bindings.items():
        if b.best:
            by_clip.setdefault(b.best.clip_name, []).append(alias)
    for clip, aliases in by_clip.items():
        if len(aliases) > 1:
            issues.append(BindingIssue(
                severity="warn", alias=",".join(aliases),
                message=f"Multiple aliases ({', '.join(aliases)}) collapsed onto a single "
                        f"clip '{clip}'. The kind likely needs distinct clips per phase."))
    return issues


# ---------------------------------------------------------------------------
# HTTP fetch helper (separated so the selector is testable offline)
# ---------------------------------------------------------------------------

def fetch_available_clips(
    client: httpx.Client,
    base_url: str,
    *,
    character_id: str = "",
    timeout: float = 5.0,
) -> list[ClipInfo]:
    """Call ``GET /api/animation/list?id=<character_id>`` and unpack."""
    params = {"id": character_id} if character_id else None
    r = client.get(f"{base_url}/api/animation/list", params=params, timeout=timeout)
    r.raise_for_status()
    data = r.json() or {}
    return [ClipInfo.from_api(item) for item in (data.get("animations") or [])]


__all__ = [
    "ClipInfo",
    "CandidateMatch",
    "AliasBinding",
    "ClipBinding",
    "BindingIssue",
    "MIN_BINDING_SCORE",
    "DEFAULT_SPEC_PATH",
    "load_spec",
    "select_clips",
    "validate_binding",
    "fetch_available_clips",
]
