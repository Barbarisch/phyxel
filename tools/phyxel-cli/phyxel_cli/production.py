"""Production-tracker scaffolding: build `.phyxel/production.json` + `GAMEPLAN.md` for a project.

Merges `core.json` + the selected genre template(s) (from the engine repo's
`docs/game-production/genre-templates/`) into a per-project `production/v2` state file, and renders
the `GAMEPLAN.md` GDD from `docs/game-production/GAMEPLAN.template.md`.

Design + schema: docs/game-production/README.md and docs/game-production/milestone-schema.md.

Templates are the source of truth (human-editable); the emitted `production.json` is self-contained
project data. Reading templates needs the engine repo resolvable (`phyxel init` / PHYXEL_HOME); if it
isn't, scaffolding is skipped (best-effort — the rest of `phyxel new`/`link` still succeeds).
"""
from __future__ import annotations

import json
from collections import OrderedDict
from datetime import date
from pathlib import Path
from typing import Iterable, Optional

from . import paths

SCHEMA = "production/v2"
# Fields on a template milestone that carry through to a production.json entry (besides `required`).
_CARRY = ("feel", "content", "ordering_critical", "combat_model", "optional")


def _gp_dir() -> Optional[Path]:
    home = paths.engine_home()
    if home is None:
        return None
    d = home / "docs" / "game-production"
    return d if d.is_dir() else None


def templates_dir() -> Optional[Path]:
    d = _gp_dir()
    if d is None:
        return None
    t = d / "genre-templates"
    return t if t.is_dir() else None


def available_genres() -> list[str]:
    t = templates_dir()
    if t is None:
        return []
    return sorted(p.stem for p in t.glob("*.json") if p.stem != "core")


def _load_template(name: str) -> dict:
    t = templates_dir()
    if t is None:
        raise FileNotFoundError("genre templates not found (is the engine repo resolvable? run `phyxel init`)")
    p = t / f"{name}.json"
    if not p.is_file():
        raise ValueError(f"unknown genre '{name}'. available: {', '.join(available_genres()) or '(none)'}")
    return json.loads(p.read_text(encoding="utf-8"))


def _milestone_entry(tmpl: dict) -> "OrderedDict[str, object]":
    """Convert a template milestone dict into an initial production.json entry."""
    e: "OrderedDict[str, object]" = OrderedDict()
    e["status"] = "todo"
    e["required"] = tmpl.get("required", "L1")
    e["validated"] = "L0"
    for k in _CARRY:
        if k in tmpl:
            e[k] = tmpl[k]
    return e


def build_production(genres: Iterable[str]) -> tuple[dict, dict]:
    """Merge core + selected genres. Returns (production_dict, gameplan_context)."""
    genres = list(genres)
    core = _load_template("core")

    milestones: "OrderedDict[str, dict]" = OrderedDict()
    notes: "OrderedDict[str, dict]" = OrderedDict()  # name -> {required, desc} for GAMEPLAN
    matrix_rows: list[dict] = []
    content_targets: "OrderedDict[str, object]" = OrderedDict()

    def absorb(tmpl: dict) -> None:
        for name, m in tmpl.get("milestones", {}).items():
            if name in milestones:  # later template overrides earlier fields
                milestones[name].update(_milestone_entry(m))
            else:
                milestones[name] = _milestone_entry(m)
            notes[name] = {"required": m.get("required", "L1"), "desc": m.get("desc", "")}
        matrix_rows.extend(tmpl.get("interaction_matrix_seed", []))
        content_targets.update(tmpl.get("content_targets", {}))

    absorb(core)
    for g in genres:
        absorb(_load_template(g))

    production = OrderedDict()
    production["schema"] = SCHEMA
    production["genres"] = genres
    production["stage"] = core.get("stages", ["concept"])[0]
    production["strictPackaging"] = False
    production["focus"] = ""
    production["milestones"] = milestones

    gameplan_ctx = {
        "genres": genres,
        "notes": notes,
        "matrix_rows": matrix_rows,
        "content_targets": content_targets,
    }
    return production, gameplan_ctx


def starter_game(genres: Iterable[str]) -> Optional[dict]:
    """The starter game.json fragment for the first selected genre that defines one (world/player/
    camera/description), merged over the base by `scaffold.new`. None if unavailable."""
    if templates_dir() is None:
        return None
    for g in genres:
        t = _load_template(g)
        if "starter" in t:
            return t["starter"]
    return None


def _matrix_table(rows: list[dict]) -> str:
    if not rows:
        return "_(none seeded — add rows as systems are built)_"
    out = ["| System A | System B | Expected effect |", "|----------|----------|-----------------|"]
    for r in rows:
        out.append(f"| {r.get('a','')} | {r.get('b','')} | {r.get('expect','')} |")
    return "\n".join(out)


def _content_table(targets: dict) -> str:
    if not targets:
        return "_(no volume targets for this genre)_"
    out = ["| Content | Target | Current |", "|---------|--------|---------|"]
    for k, v in targets.items():
        out.append(f"| {k} | {v} | 0 |")
    return "\n".join(out)


def _notes_list(notes: dict) -> str:
    return "\n".join(f"- **{n}** (req {d['required']}): {d['desc']}" for n, d in notes.items())


def render_gameplan(name: str, ctx: dict, today: Optional[str] = None) -> Optional[str]:
    d = _gp_dir()
    if d is None:
        return None
    tmpl_path = d / "GAMEPLAN.template.md"
    if not tmpl_path.is_file():
        return None
    text = tmpl_path.read_text(encoding="utf-8")
    genres = ", ".join(ctx["genres"]) if ctx["genres"] else "unspecified (core only)"
    repl = {
        "{{NAME}}": name,
        "{{GENRES}}": genres,
        "{{DATE}}": today or date.today().isoformat(),
        "{{INTERACTION_MATRIX}}": _matrix_table(ctx["matrix_rows"]),
        "{{CONTENT_TARGETS}}": _content_table(ctx["content_targets"]),
        "{{MILESTONE_NOTES}}": _notes_list(ctx["notes"]),
    }
    for k, v in repl.items():
        text = text.replace(k, v)
    return text


def scaffold_production(project_dir: Path, genres: Iterable[str], name: Optional[str] = None) -> dict:
    """Write .phyxel/production.json + GAMEPLAN.md into a project. Idempotent: never clobbers an
    existing production.json or GAMEPLAN.md. Best-effort: returns {'skipped': reason} if templates
    are unresolvable."""
    project_dir = Path(project_dir)
    name = name or project_dir.name
    if templates_dir() is None:
        return {"skipped": "genre templates not resolvable (run `phyxel init` in the engine repo)"}

    genres = list(genres)
    production, ctx = build_production(genres)

    wrote: list[str] = []
    prod_path = project_dir / ".phyxel" / "production.json"
    if prod_path.exists():
        wrote.append(".phyxel/production.json (kept existing)")
    else:
        prod_path.parent.mkdir(parents=True, exist_ok=True)
        prod_path.write_text(json.dumps(production, indent=2) + "\n", encoding="utf-8")
        wrote.append(".phyxel/production.json")

    gp_path = project_dir / "GAMEPLAN.md"
    if gp_path.exists():
        wrote.append("GAMEPLAN.md (kept existing)")
    else:
        gp = render_gameplan(name, ctx)
        if gp is not None:
            gp_path.write_text(gp, encoding="utf-8")
            wrote.append("GAMEPLAN.md")

    return {"wrote": wrote, "genres": genres}
