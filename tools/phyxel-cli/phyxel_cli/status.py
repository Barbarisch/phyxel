"""`phyxel status` — the production digest a fresh Claude session is auto-oriented with.

Reads `.phyxel/production.json` (+ its `focus`) and prints a compact, stale-first summary of
where the game is and what's next. No engine required (works at SessionStart hook time). Design:
docs/game-production/README.md §6.3.

Output is ASCII-only on purpose (it flows through hook stdout / arbitrary Windows consoles).
"""
from __future__ import annotations

import json
from pathlib import Path
from typing import Optional

_LEVELS = {"L0": 0, "L1": 1, "L2": 2, "L3": 3, "L4": 4}


def _lvl(x) -> int:
    return _LEVELS.get(str(x), 0)


def read_production(project_dir: Path) -> Optional[dict]:
    p = Path(project_dir) / ".phyxel" / "production.json"
    if not p.is_file():
        return None
    try:
        return json.loads(p.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None


def _feel_ok(m: dict) -> bool:
    return m.get("feel", "n/a") in ("n/a", "passed")


def _is_complete(m: dict) -> bool:
    return (m.get("status") == "done"
            and _lvl(m.get("validated")) >= _lvl(m.get("required"))
            and _feel_ok(m))


def _counts_toward_total(m: dict) -> bool:
    return not m.get("optional") and m.get("status") != "n/a"


def summarize(prod: dict) -> dict:
    ms: dict = prod.get("milestones", {})
    required = [(n, m) for n, m in ms.items() if _counts_toward_total(m)]
    total = len(required)
    done = [n for n, m in required if _is_complete(m)]
    stale = [n for n, m in ms.items() if m.get("status") == "stale"]
    blocked = [n for n, m in ms.items() if m.get("status") == "blocked"]

    # NEXT: not-complete required work, in_progress first, then declared (build) order.
    # (Ordering-critical items get their own dedicated line below, so we don't double-surface them.)
    def rank(item):
        n, m = item
        return 0 if m.get("status") == "in_progress" else 1

    nxt = [(n, m) for n, m in required
           if not _is_complete(m) and m.get("status") not in ("stale", "blocked")]
    nxt.sort(key=rank)

    # ordering-critical that hasn't even started (un-retrofittable — nudge early)
    oc_todo = [n for n, m in ms.items()
               if m.get("ordering_critical") and m.get("status") == "todo"]

    pct = int(round(100 * len(done) / total)) if total else 0
    return {
        "stage": prod.get("stage", "concept"),
        "genres": prod.get("genres", []),
        "focus": (prod.get("focus") or "").strip(),
        "total": total, "done": done, "stale": stale, "blocked": blocked,
        "next": nxt, "ordering_critical_todo": oc_todo, "pct": pct,
    }


def format_digest(project_name: str, prod: dict) -> str:
    s = summarize(prod)
    genres = ", ".join(s["genres"]) if s["genres"] else "core"
    lines = [f"[phyxel] {project_name} ({genres}) - stage: {s['stage']} - "
             f"{len(s['done'])}/{s['total']} milestones ({s['pct']}%)"]
    if s["done"]:
        shown = ", ".join(s["done"][:8]) + ("..." if len(s["done"]) > 8 else "")
        lines.append(f"  DONE:  {shown}")
    if s["next"]:
        parts = [f"{n} ({m.get('status','todo')}, "
                 f"{m.get('validated','L0')}->{m.get('required','L1')})"
                 for n, m in s["next"][:4]]
        lines.append("  NEXT:  " + "; ".join(parts))
    if s["stale"]:
        lines.append(f"  (!) STALE (re-validate): {', '.join(s['stale'])}")
    if s["blocked"]:
        lines.append(f"  (!) BLOCKED: {', '.join(s['blocked'])}")
    if s["ordering_critical_todo"]:
        lines.append(f"  (!) ORDERING-CRITICAL not started (un-retrofittable): "
                     f"{', '.join(s['ordering_critical_todo'])}")
    if s["focus"]:
        lines.append(f"  FOCUS: {s['focus']}")
    lines.append("  See GAMEPLAN.md; `phyxel status` / production(op=\"report\") for the full ledger.")
    return "\n".join(lines)


def digest(project_dir: Path) -> Optional[str]:
    """The full digest string, or None if this isn't a linked project with a tracker."""
    prod = read_production(project_dir)
    if prod is None:
        return None
    return format_digest(Path(project_dir).name, prod)
