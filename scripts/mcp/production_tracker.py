"""Production-tracker ops backing the `production` MCP tool.

Reads/updates a project's `.phyxel/production.json` (the game-production milestone tracker;
design: docs/game-production/README.md, schema: docs/game-production/milestone-schema.md). Pure
file operations — production.json is just a project file, so no engine is required. The digest
format mirrors the `phyxel status` CLI (tools/phyxel-cli/phyxel_cli/status.py); keep the two
consistent. ASCII-only output (flows through MCP / arbitrary consoles).
"""
from __future__ import annotations

import json
from pathlib import Path

_LEVELS = {"L0": 0, "L1": 1, "L2": 2, "L3": 3, "L4": 4}
STATUSES = ("todo", "in_progress", "done", "n/a", "blocked", "stale")
FEELS = ("n/a", "pending", "passed")
STAGES = ("concept", "vertical_slice", "feature_complete", "content_complete", "shippable")
OPS = ("status", "report", "set", "add_milestone", "remove_milestone", "advance_stage")


def _path(project_dir) -> Path:
    return Path(project_dir) / ".phyxel" / "production.json"


def load(project_dir) -> dict:
    p = _path(project_dir)
    if not p.is_file():
        raise FileNotFoundError(
            f"no production tracker at {p} — scaffold one with `phyxel new/link --genre <g>`")
    return json.loads(p.read_text(encoding="utf-8"))


def save(project_dir, data: dict) -> None:
    p = _path(project_dir)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def _lvl(x) -> int:
    return _LEVELS.get(str(x), 0)


def _feel_ok(m: dict) -> bool:
    return m.get("feel", "n/a") in ("n/a", "passed")


def is_complete(m: dict) -> bool:
    return (m.get("status") == "done"
            and _lvl(m.get("validated")) >= _lvl(m.get("required"))
            and _feel_ok(m))


def _counts(m: dict) -> bool:
    return not m.get("optional") and m.get("status") != "n/a"


def summarize(prod: dict) -> dict:
    ms: dict = prod.get("milestones", {})
    required = [(n, m) for n, m in ms.items() if _counts(m)]
    total = len(required)
    done = [n for n, m in required if is_complete(m)]
    stale = [n for n, m in ms.items() if m.get("status") == "stale"]
    blocked = [n for n, m in ms.items() if m.get("status") == "blocked"]
    nxt = [(n, m) for n, m in required
           if not is_complete(m) and m.get("status") not in ("stale", "blocked")]
    nxt.sort(key=lambda it: 0 if it[1].get("status") == "in_progress" else 1)
    oc_todo = [n for n, m in ms.items()
               if m.get("ordering_critical") and m.get("status") == "todo"]
    pct = int(round(100 * len(done) / total)) if total else 0
    return {"stage": prod.get("stage", "concept"), "genres": prod.get("genres", []),
            "focus": (prod.get("focus") or "").strip(), "total": total, "done": done,
            "stale": stale, "blocked": blocked, "next": nxt,
            "ordering_critical_todo": oc_todo, "pct": pct}


def format_digest(project_name: str, prod: dict) -> str:
    s = summarize(prod)
    genres = ", ".join(s["genres"]) if s["genres"] else "core"
    lines = [f"[phyxel] {project_name} ({genres}) - stage: {s['stage']} - "
             f"{len(s['done'])}/{s['total']} milestones ({s['pct']}%)"]
    if s["done"]:
        lines.append("  DONE:  " + ", ".join(s["done"][:8]) + ("..." if len(s["done"]) > 8 else ""))
    if s["next"]:
        lines.append("  NEXT:  " + "; ".join(
            f"{n} ({m.get('status', 'todo')}, {m.get('validated', 'L0')}->{m.get('required', 'L1')})"
            for n, m in s["next"][:4]))
    if s["stale"]:
        lines.append(f"  (!) STALE (re-validate): {', '.join(s['stale'])}")
    if s["blocked"]:
        lines.append(f"  (!) BLOCKED: {', '.join(s['blocked'])}")
    if s["ordering_critical_todo"]:
        lines.append("  (!) ORDERING-CRITICAL not started (un-retrofittable): "
                     + ", ".join(s["ordering_critical_todo"]))
    if s["focus"]:
        lines.append(f"  FOCUS: {s['focus']}")
    return "\n".join(lines)


# --- ops -------------------------------------------------------------------------------------

def _digest(project_dir, prod) -> str:
    return format_digest(Path(project_dir).name, prod)


def op_status(project_dir, prod) -> dict:
    return {"ok": True, "digest": _digest(project_dir, prod), "summary": summarize(prod)}


def op_report(project_dir, prod) -> dict:
    s = summarize(prod)
    ms: dict = prod.get("milestones", {})
    ledger = []
    for n, m in ms.items():
        ledger.append({
            "milestone": n, "status": m.get("status", "todo"),
            "required": m.get("required", "L1"), "validated": m.get("validated", "L0"),
            "feel": m.get("feel"), "complete": is_complete(m),
            "optional": bool(m.get("optional")),
            "content": m.get("content"), "note": m.get("note"), "reason": m.get("reason"),
        })
    incomplete_required = [row["milestone"] for row in ledger
                           if not row["complete"] and not row["optional"]
                           and row["status"] != "n/a"]
    return {"ok": True, "name": Path(project_dir).name, "stage": s["stage"],
            "genres": s["genres"], "focus": s["focus"], "pct_complete": s["pct"],
            "complete": len(s["done"]), "total_required": s["total"],
            "stale": s["stale"], "blocked": s["blocked"],
            "incomplete_required": incomplete_required, "ledger": ledger,
            "digest": _digest(project_dir, prod)}


def op_set(project_dir, prod, a: dict) -> dict:
    ms: dict = prod.setdefault("milestones", {})
    changed = []
    if a.get("focus") is not None:
        prod["focus"] = a["focus"]; changed.append("focus")
    if a.get("stage") is not None:
        if a["stage"] not in STAGES:
            raise ValueError(f"unknown stage '{a['stage']}'. valid: {', '.join(STAGES)}")
        prod["stage"] = a["stage"]; changed.append(f"stage={a['stage']}")
    mname = a.get("milestone")
    if mname is not None:
        if mname not in ms:
            raise ValueError(f"unknown milestone '{mname}' — use op=add_milestone to create it")
        m = ms[mname]
        if a.get("status") is not None:
            if a["status"] not in STATUSES:
                raise ValueError(f"invalid status '{a['status']}'. valid: {', '.join(STATUSES)}")
            m["status"] = a["status"]
        if a.get("validated") is not None:
            if a["validated"] not in _LEVELS:
                raise ValueError(f"invalid validated '{a['validated']}'. valid: L0..L4")
            m["validated"] = a["validated"]
        if a.get("feel") is not None:
            if a["feel"] not in FEELS:
                raise ValueError(f"invalid feel '{a['feel']}'. valid: {', '.join(FEELS)}")
            m["feel"] = a["feel"]
        if a.get("note") is not None:
            m["note"] = a["note"]
        if a.get("reason") is not None:
            m["reason"] = a["reason"]
        if a.get("evidence") is not None:
            m["evidence"] = a["evidence"]
        if m.get("status") == "n/a" and not m.get("reason"):
            return {"ok": False,
                    "error": f"milestone '{mname}' set to n/a but has no 'reason' — pass reason=..."}
        changed.append(mname)
    if not changed:
        return {"ok": False, "error": "nothing to set — pass milestone (+status/validated/feel/...), "
                                      "focus, and/or stage"}
    save(project_dir, prod)
    return {"ok": True, "changed": changed, "digest": _digest(project_dir, prod)}


def op_add_milestone(project_dir, prod, a: dict) -> dict:
    name = a.get("milestone") or a.get("name")
    if not name:
        raise ValueError("op=add_milestone needs 'milestone' (the name)")
    ms: dict = prod.setdefault("milestones", {})
    if name in ms:
        raise ValueError(f"milestone '{name}' already exists — use op=set")
    req = a.get("required", "L1")
    if req not in _LEVELS:
        raise ValueError(f"invalid required '{req}'. valid: L0..L4")
    entry = {"status": "todo", "required": req, "validated": "L0"}
    if a.get("feel") is not None:
        entry["feel"] = a["feel"]
    if a.get("note") is not None:
        entry["note"] = a["note"]
    if a.get("optional"):
        entry["optional"] = True
    ms[name] = entry
    save(project_dir, prod)
    return {"ok": True, "added": name, "digest": _digest(project_dir, prod)}


def op_remove_milestone(project_dir, prod, a: dict) -> dict:
    name = a.get("milestone") or a.get("name")
    ms: dict = prod.get("milestones", {})
    if not name or name not in ms:
        raise ValueError(f"unknown milestone '{name}'")
    del ms[name]
    save(project_dir, prod)
    return {"ok": True, "removed": name, "digest": _digest(project_dir, prod)}


def op_advance_stage(project_dir, prod, a: dict) -> dict:
    cur = prod.get("stage", "concept")
    idx = STAGES.index(cur) if cur in STAGES else 0
    target = a.get("stage") or (STAGES[idx + 1] if idx + 1 < len(STAGES) else cur)
    if target not in STAGES:
        raise ValueError(f"unknown stage '{target}'. valid: {', '.join(STAGES)}")
    s = summarize(prod)
    # Readiness note (advisory — not a hard gate; the human/agent decides).
    incomplete = [n for n, m in s["next"]]
    prod["stage"] = target
    save(project_dir, prod)
    return {"ok": True, "stage": target,
            "readiness_note": (f"advanced to '{target}'. {len(s['done'])}/{s['total']} required "
                               f"milestones complete; still open: "
                               f"{', '.join(incomplete[:6]) or '(none)'}"),
            "digest": _digest(project_dir, prod)}


_HANDLERS = {
    "status": op_status, "report": op_report,
    "set": op_set, "add_milestone": op_add_milestone,
    "remove_milestone": op_remove_milestone, "advance_stage": op_advance_stage,
}


def handle(op: str, project_dir, args: dict) -> dict:
    """Dispatch one production op. Raises FileNotFoundError/ValueError on bad input."""
    if op not in _HANDLERS:
        raise ValueError(f"unknown op '{op}'. valid: {', '.join(OPS)}")
    prod = load(project_dir)
    fn = _HANDLERS[op]
    # status/report take (project_dir, prod); mutating ops take (project_dir, prod, args).
    if op in ("status", "report"):
        return fn(project_dir, prod)
    return fn(project_dir, prod, args)
