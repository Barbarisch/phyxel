"""Runtime (L3/L4) milestone validators — drive the RUNNING engine over its HTTP API to confirm
milestones *functionally* (the world actually loads + renders with the player present), where the
static validators (production_validators) can only inspect files. Sync urllib calls to the engine
base URL; results are applied ONLY when the engine has the same project loaded that the tracker
belongs to (else the runtime state describes a different game).

Runtime validators so far: `world` L4 (world loads + renders + player present) and `player` L4
(player is CONTROLLABLE — proven by injecting forward movement via /api/input/inject and confirming
the player moved). More (menus render, a win trigger actually fires, a playtest reaches victory) slot
into this same registry as the engine API grows (trigger-fire + screen-state, TraversalProbe-at-scale).
"""
from __future__ import annotations

import json
import time
import urllib.request
from pathlib import Path

import production_tracker as pt  # same-dir; pt does NOT import this module (no cycle)


def _get(base, path, timeout=8):
    try:
        with urllib.request.urlopen(f"{base}{path}", timeout=timeout) as r:
            return json.loads(r.read().decode())
    except Exception:
        return None


def _post(base, path, body, timeout=8):
    try:
        data = json.dumps(body).encode()
        req = urllib.request.Request(f"{base}{path}", data=data,
                                     headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read().decode())
    except Exception:
        return None


def _player_pos(base):
    """Horizontal (x,z) + y of the 'player' entity, or None if absent."""
    state = _get(base, "/api/state")
    for e in (state or {}).get("entities", []):
        if e.get("id") == "player":
            p = e.get("position") or e
            try:
                return (float(p.get("x")), float(p.get("y")), float(p.get("z")))
            except (TypeError, ValueError):
                return None
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


def rv_player(base):
    """L4: the player is CONTROLLABLE — inject forward movement and confirm the player actually moves.

    Uses synthetic input injection (the /api/input/inject route). Honest by construction: if the
    player doesn't move (no player entity, game not in a controllable state, injection ignored by a
    capturing menu), it reports not-confirmed rather than a false 'done'. This is a SIDE-EFFECTING
    validator (it moves the player), so it only runs when explicitly targeted — never in a run-all sweep.
    """
    p0 = _player_pos(base)
    if p0 is None:
        return _V("L0", False, "runtime: no 'player' entity present — cannot confirm control")
    hold = 1.0
    resp = _post(base, "/api/input/inject", {"keys": ["W", "MoveForward"], "hold": hold})
    if resp is None or not resp.get("success"):
        return _V("L2", False, "runtime: inject_input route unavailable or failed (engine rebuilt?)")
    time.sleep(hold + 0.4)  # let the held key drive movement, then auto-release + settle
    p1 = _player_pos(base)
    if p1 is None:
        return _V("L2", False, "runtime: player entity vanished during control test")
    dx, dz = p1[0] - p0[0], p1[2] - p0[2]
    dist = (dx * dx + dz * dz) ** 0.5
    if dist > 0.3:
        return _V("L4", True,
                  f"runtime: player controllable — moved {dist:.2f}u on injected forward "
                  f"({p0[0]:.1f},{p0[2]:.1f})->({p1[0]:.1f},{p1[2]:.1f})")
    return _V("L2", False,
              f"runtime: injected forward but player did not move (dist={dist:.2f}u) — "
              f"is it in a controllable playing state?")


RUNTIME_REGISTRY = {"world": rv_world, "player": rv_player}

# Validators with runtime side effects (they drive input / move the player). Excluded from the
# default run-all so a routine `validate`/`sweep` never perturbs the game — only run when targeted.
SIDE_EFFECTING = {"player"}


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
    # Run-all excludes side-effecting validators (e.g. `player`, which moves the character);
    # those only run when explicitly named via `milestone`.
    targets = [milestone] if milestone else [n for n in RUNTIME_REGISTRY if n not in SIDE_EFFECTING]
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
