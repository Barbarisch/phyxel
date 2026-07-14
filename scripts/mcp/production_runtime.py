"""Runtime (L3/L4) milestone validators — drive the RUNNING engine over its HTTP API to confirm
milestones *functionally* (the world actually loads + renders with the player present), where the
static validators (production_validators) can only inspect files. Sync urllib calls to the engine
base URL; results are applied ONLY when the engine has the same project loaded that the tracker
belongs to (else the runtime state describes a different game).

Runtime validators: `world`/`player`/`perf_target` L4; `level_playability`/`win_condition`/
`lose_condition`/`core_loop`/`qa_pass` L3. `qa_pass` runs the adversarial playtest harness
(production_playtest) — capped at L3 (augments, not replaces, a human playtest). Side-effecting ones
(player/win/lose/core_loop/qa_pass — they drive input or fire triggers) are excluded from run-all
sweeps and only run when explicitly targeted.

WINDOWS/urllib PERF: resolving "localhost" via urllib costs ~2s/call (IPv6 fallback); _norm() forces
127.0.0.1 so multi-call validators (esp. the playtest) don't balloon — see _norm.
"""
from __future__ import annotations

import json
import time
import urllib.request
from pathlib import Path

import production_tracker as pt  # same-dir; pt does NOT import this module (no cycle)


def _norm(base):
    # WINDOWS/urllib GOTCHA: resolving "localhost" via urllib costs ~2s/call (IPv6
    # fallback), vs ~0.015s for 127.0.0.1 — a 130x tax that silently balloons any
    # multi-call validator (a 60-call playtest went 172s -> ~2s). Force the literal.
    return base.replace("localhost", "127.0.0.1")


def _get(base, path, timeout=8):
    try:
        with urllib.request.urlopen(f"{_norm(base)}{path}", timeout=timeout) as r:
            return json.loads(r.read().decode())
    except Exception:
        return None


def _post(base, path, body, timeout=8):
    try:
        data = json.dumps(body).encode()
        req = urllib.request.Request(f"{_norm(base)}{path}", data=data,
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


def _triggers(base):
    """The declarative trigger list, or None if unavailable."""
    r = _get(base, "/api/triggers")
    if r is None:
        return None
    return r.get("triggers", []) if isinstance(r, dict) else []


def _then_types(trig):
    return {a.get("type") for a in (trig.get("then") or []) if isinstance(a, dict)}


def _pick_trigger(triggers, role):
    """Best-guess the win/lose trigger. Transparent — the evidence names which id
    was chosen, so a wrong guess is visible rather than silent."""
    WIN_TYPES, LOSE_TYPES = {"show_victory", "show_credits"}, {"fail_objective"}
    WIN_IDS = ("win", "victory", "escape", "complete", "beacon")
    LOSE_IDS = ("lose", "death", "die", "fail", "gameover", "game_over", "defeat", "freeze")
    best = None
    for t in triggers:
        tid = (t.get("id") or "").lower()
        types = _then_types(t)
        if role == "win":
            if tid in ("win", "victory") or (types & WIN_TYPES):
                return t
            if any(k in tid for k in WIN_IDS) and not (types & LOSE_TYPES):
                best = best or t
        else:
            if any(k in tid for k in LOSE_IDS) or (types & LOSE_TYPES):
                return t
    return best


def _goal_region(triggers):
    """Center (x,z) of the first entity_reached_region trigger's AABB, or None."""
    for t in triggers:
        w = t.get("when") or {}
        if w.get("event") == "entity_reached_region":
            r = w.get("region") or {}
            a, b = (r.get("from") or {}), (r.get("to") or {})
            try:
                return (int(round((float(a["x"]) + float(b["x"])) / 2)),
                        int(round((float(a["z"]) + float(b["z"])) / 2)))
            except (KeyError, TypeError, ValueError):
                continue
    return None


def rv_perf(base):
    """L4: the game meets its FPS budget. Reads the engine's real frame rate from
    /api/debug/engine_timing (computed as 1000/cpuFrameTime) and compares to the
    milestone's `target` min-FPS (default 30 = a playable floor). Read-only."""
    t = _get(base, "/api/debug/engine_timing")
    fps = (t or {}).get("fps")
    if fps is None:
        return _V("L2", False, "runtime: /api/debug/engine_timing has no fps field")
    # `target` on the perf_target milestone overrides the default floor.
    target = rv_perf.target or 30.0
    if fps >= target:
        return _V("L4", True, f"runtime: {fps:.0f} FPS >= {target:.0f} target")
    return _V("L2", False, f"runtime: {fps:.0f} FPS < {target:.0f} target (perf budget not met)")


rv_perf.target = None  # set per-call in run() from the milestone's `target`, if any


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


def rv_level_playability(base):
    """L3: the player spawns on WALKABLE ground and (if the win is location-based) the goal is
    reachable. Uses the world-scale NavGrid (/api/navgrid/cell + /api/navgrid/path). Read-only.
    Caveat: NavGrid's step-up is conservative (MAX_STEP_UP=0 + jump-links), so a 'not reachable'
    is a SIGNAL to inspect, not a proof of softlock."""
    start = _player_pos(base)
    if start is None:
        return _V("L0", False, "runtime: no player entity to test spawn walkability")
    sx, sz = int(round(start[0])), int(round(start[2]))
    cell = _get(base, f"/api/navgrid/cell?x={sx}&z={sz}")
    if cell is None or "error" in cell:
        return _V("L2", False, "runtime: NavGrid unavailable (needs a project world loaded)")
    if not cell.get("walkable"):
        return _V("L2", False, f"runtime: player spawn ({sx},{sz}) is NOT on walkable NavGrid "
                               f"(spawned in a wall / over void?)")
    goal = _goal_region(_triggers(base) or [])
    if goal:
        gx, gz = goal
        path = _get(base, f"/api/navgrid/path?x1={sx}&z1={sz}&x2={gx}&z2={gz}")
        if path and path.get("found"):
            return _V("L3", True, f"runtime: spawn walkable + goal ({gx},{gz}) reachable "
                                  f"({len(path.get('waypoints', []))} waypoints)")
        return _V("L2", False, f"runtime: spawn walkable but goal ({gx},{gz}) NOT reachable via NavGrid "
                               f"(possible softlock — verify; NavGrid step-up is conservative)")
    return _V("L3", True, f"runtime: player spawn ({sx},{sz}) is on walkable NavGrid (no location goal to path)")


def _fire_and_observe(base, trig):
    before = _get(base, "/api/screen/state") or {}
    resp = _post(base, "/api/triggers/fire", {"id": trig.get("id")})
    after = _get(base, "/api/screen/state") or {}
    return resp, before, after


def rv_win_condition(base):
    """L3: a scripted fire of the win trigger executes its terminal action. Side-effecting (fires the
    trigger). Caps at L3 — L4 (playtest actually reaches victory in the packaged shell) needs a real
    playthrough; show_victory/show_credits no-op in the editor host, so a transition_scene win is the
    observable case."""
    trs = _triggers(base)
    if trs is None:
        return _V("L0", False, "runtime: /api/triggers unavailable")
    t = _pick_trigger(trs, "win")
    if not t:
        return _V("L2", False, "runtime: no win trigger found (need a terminal then-action or a win id)")
    resp, before, after = _fire_and_observe(base, t)
    if not resp or not resp.get("success"):
        return _V("L2", False, f"runtime: fire_trigger failed for '{t.get('id')}'")
    executed = resp.get("executed", [])
    changed = (before.get("screen") != after.get("screen")) or (before.get("scene_id") != after.get("scene_id"))
    obs = (f"screen {before.get('screen')}->{after.get('screen')}" if changed
           else "screen unchanged (shell-only terminal no-ops in editor)")
    return _V("L3", True, f"runtime: win trigger '{t.get('id')}' fired, executed {executed}; {obs}")


def rv_lose_condition(base):
    """L3: a scripted fire of the lose/fail trigger executes. Side-effecting. If the game has no
    explicit lose TRIGGER (death handled by the respawn system instead), reports not-confirmed
    rather than a false pass."""
    trs = _triggers(base)
    if trs is None:
        return _V("L0", False, "runtime: /api/triggers unavailable")
    t = _pick_trigger(trs, "lose")
    if not t:
        return _V("L2", False, "runtime: no explicit lose trigger (death may be handled by the respawn "
                               "system — needs a scripted death check or a human playtest)")
    resp, before, after = _fire_and_observe(base, t)
    if not resp or not resp.get("success"):
        return _V("L2", False, f"runtime: fire_trigger failed for '{t.get('id')}'")
    executed = resp.get("executed", [])
    return _V("L3", True, f"runtime: lose trigger '{t.get('id')}' fired, executed {executed}")


def rv_core_loop(base):
    """L3 (partial): a driven SMOKE playthrough — inject a WASD+jump sequence and confirm the game
    survives it (engine responsive, player alive + not fallen through world, moved at some point).
    Side-effecting. Honest ceiling: this proves the game is playable-without-crashing under input, NOT
    that the core mechanic is fun/complete — full core_loop L4 needs a real playtest."""
    p0 = _player_pos(base)
    if p0 is None:
        return _V("L0", False, "runtime: no player entity for the core-loop smoke")
    moved = False
    for keys, hold in [("W", 0.6), ("D", 0.4), ("Space", 0.2), ("A", 0.4), ("S", 0.6)]:
        if _post(base, "/api/input/inject", {"keys": [keys], "hold": hold}) is None:
            return _V("L2", False, "runtime: inject_input unavailable during core-loop smoke")
        time.sleep(hold + 0.15)
        p = _player_pos(base)
        if p and ((p[0] - p0[0]) ** 2 + (p[2] - p0[2]) ** 2) ** 0.5 > 0.3:
            moved = True
    alive = _get(base, "/api/status") is not None
    p1 = _player_pos(base)
    if not alive or p1 is None:
        return _V("L2", False, "runtime: engine/player not responsive after the input smoke")
    if p1[1] < -20:
        return _V("L2", False, f"runtime: player fell through the world (y={p1[1]:.1f}) under input")
    return _V("L3", True, f"SMOKE: survived injected WASD+jump (moved={moved}, alive @y={p1[1]:.1f}, "
                          f"engine responsive) — partial core-loop signal; full L4 needs a playtest")


def rv_qa_pass(base):
    """L3: an adversarial playtest (production_playtest) finds no game-breaking issue — the
    defensive/illegal-branch coverage a goal-pursuing build agent misses. Runs deterministic
    probes (fall-through, world-edge, input-churn, softlock). Side-effecting (drives lots of input).
    HONEST CEILING: a scripted playtest finds ~60-75% of human-found bugs, so this AUGMENTS but never
    REPLACES a human playtest — it caps at L3, so qa_pass (required L4) stays in_progress until a human
    signs off. Any probe that finds a bug reports it (ok=False) rather than a false pass."""
    import production_playtest as pp
    rep = pp.run_playtest(base)
    if rep["clean"]:
        return _V("L3", True, f"adversarial playtest clean ({pp.summary(rep)}) — augments, not replaces, "
                              f"a human playtest")
    return _V("L2", False, "adversarial playtest FOUND ISSUES: " + " | ".join(rep["bugs"]))


RUNTIME_REGISTRY = {
    "world": rv_world,
    "player": rv_player,
    "perf_target": rv_perf,
    "level_playability": rv_level_playability,
    "win_condition": rv_win_condition,
    "lose_condition": rv_lose_condition,
    "core_loop": rv_core_loop,
    "qa_pass": rv_qa_pass,
}

# Validators with runtime side effects (drive input / fire triggers / move the player). Excluded from
# the default run-all so a routine `validate`/`sweep` never perturbs the game — only run when targeted.
SIDE_EFFECTING = {"player", "win_condition", "lose_condition", "core_loop", "qa_pass"}


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
        if name == "perf_target":
            rv_perf.target = ms[name].get("target")  # min-FPS override, if the milestone sets one
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
