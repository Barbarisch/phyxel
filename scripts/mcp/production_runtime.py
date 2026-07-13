"""Runtime (L3/L4) milestone validators — drive the RUNNING engine over its HTTP API to confirm
milestones *functionally* (the world actually loads + renders with the player present), where the
static validators (production_validators) can only inspect files. Sync urllib calls to the engine
base URL; results are applied ONLY when the engine has the same project loaded that the tracker
belongs to (else the runtime state describes a different game).

This is the first of the runtime validators — `world` L4. More (menus render, a win trigger actually
fires, a playtest reaches victory) need more engine API surface + synthetic-input injection (logged as
an engine feature request); they slot into this same registry.
"""
from __future__ import annotations

import json
import urllib.request
from pathlib import Path

import production_tracker as pt  # same-dir; pt does NOT import this module (no cycle)


def _get(base, path, timeout=8):
    try:
        with urllib.request.urlopen(f"{base}{path}", timeout=timeout) as r:
            return json.loads(r.read().decode())
    except Exception:
        return None


def _V(reached, ok, evidence):
    return {"reached": reached, "ok": ok, "evidence": evidence}


def rv_world(base):
    """L4: a playable world is loaded AND renders (visible faces + chunks) AND the player is present."""
    state = _get(base, "/api/state")
    rs = _get(base, "/api/render/stats")
    faces = (rs or {}).get("total_visible_faces", 0)
    chunks = (rs or {}).get("visible_chunk_count", 0)
    has_player = any(e.get("id") == "player" for e in (state or {}).get("entities", []))
    if faces > 0 and chunks > 0 and has_player:
        return _V("L4", True,
                  f"runtime: world renders ({faces} faces, {chunks} chunks) + player present")
    return _V("L2", False,
              f"runtime: world not confirmed (faces={faces}, chunks={chunks}, player={has_player})")


RUNTIME_REGISTRY = {"world": rv_world}


def engine_project(base):
    """The project_dir the running engine has loaded, or None if not running / no project."""
    p = _get(base, "/api/project/info")
    return p.get("project_dir") if (p and "error" not in p) else None


def run(project_dir, base_url, milestone=None) -> dict:
    """Run runtime validators against the engine at `base_url`, applying results ONLY if the engine's
    loaded project matches `project_dir`. Writes reached levels + evidence back to production.json
    (same rules as static validate: never downgrade; done when reached>=required + feel ok)."""
    loaded = engine_project(base_url)
    if not loaded:
        return {"ran": False, "reason": "engine not running / no project loaded — runtime validators skipped"}
    if Path(loaded).resolve() != Path(project_dir).resolve():
        return {"ran": False, "reason": f"engine has a different project loaded ({loaded}) — skipped"}

    prod = pt.load(project_dir)
    ms = prod.get("milestones", {})
    targets = [milestone] if milestone else list(RUNTIME_REGISTRY)
    upgraded, results = [], []
    for name in targets:
        fn = RUNTIME_REGISTRY.get(name)
        if fn is None or name not in ms:
            continue
        v = fn(base_url)
        v["milestone"] = name
        results.append(v)
        if not v["ok"]:
            continue
        m = ms[name]
        if pt._lvl(v["reached"]) > pt._lvl(m.get("validated", "L0")):
            m["validated"] = v["reached"]
        m["evidence"] = v["evidence"]
        req = m.get("required", "L1")
        if pt._lvl(m.get("validated")) >= pt._lvl(req) and pt._feel_ok(m):
            if m.get("status") in ("todo", "in_progress"):
                m["status"] = "done"
        pt._stamp(project_dir, m, name)
        upgraded.append(name)
    pt.save(project_dir, prod)
    return {"ran": True, "engine": base_url, "upgraded": upgraded, "results": results}
