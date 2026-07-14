"""Adversarial playtest + trace-replay harness (game-production Phase 7).

Drives the RUNNING game over its HTTP API to try to BREAK it — the defensive /
illegal-branch / edge-input coverage that a *goal-pursuing build agent* misses
(the SMART result: goal-seeking agents hit ~55% coverage and skip the ways a
game breaks). Two complementary parts:

1. Deterministic adversarial PROBES (game-agnostic): stability / fall-through,
   world-edge, input-churn, and softlock. Each drives input via /api/input/inject
   and asserts invariants that must hold in ANY game (player stays alive + on
   finite ground, engine stays responsive, the goal stays reachable).
2. Trace REPLAY (regression): re-run authored input+assertion sequences from
   `.phyxel/playtest_traces/*.json` each build and diff outcomes — so a bug found
   once (by a probe or a fresh-agent exploration) can be captured as a repro that
   never silently comes back.

HONEST CEILING: a scripted / LLM playtest finds ~60-75% of human-found bugs — it
AUGMENTS the human playtest gate, it does NOT replace it. So the `qa_pass`
validator built on this caps at **L3**, never auto-L4.

FIDELITY: runs against the editor `--project` host today (the only process that
hosts the API). The SAME probes will run against the real packaged game once the
standalone shell gains an API flag — at which point they graduate from editor-proxy
to real play conditions. See docs/game-production/README.md §6.6.

Deterministic by construction (fixed index-varied input sequences, no RNG) so a
green run today stays reproducible and a regression is unambiguous.
"""
from __future__ import annotations

import json
import math
import time
from pathlib import Path

# Reuse the runtime helpers (no cycle: production_runtime does not import this).
from production_runtime import _get, _post, _player_pos, _triggers, _goal_region


# --------------------------------------------------------------------------- #
# Invariants — properties that must hold in ANY game, regardless of mechanic.
# --------------------------------------------------------------------------- #

def _alive(base):
    """The player entity is still present."""
    return _player_pos(base) is not None


def _responsive(base):
    """The engine still answers its status endpoint."""
    return _get(base, "/api/status") is not None


def _finite(pos):
    return pos is not None and all(math.isfinite(c) for c in pos)


def _dist2d(a, b):
    return ((a[0] - b[0]) ** 2 + (a[2] - b[2]) ** 2) ** 0.5


# --------------------------------------------------------------------------- #
# Adversarial probes — each returns {name, ok, evidence, [bug], [coverage]}.
#   ok=True  -> invariants held (no bug)
#   ok=False -> a bug was found (evidence names the failure)
#   ok=None  -> could not run (e.g. no player) — reported, not counted as pass
# --------------------------------------------------------------------------- #

def _drive(base, key, hold):
    _post(base, "/api/input/inject", {"keys": [key], "hold": hold})


def probe_stability(base):
    """Churn varied movement + jumps and assert the player never falls through the
    world / goes non-finite and the engine stays responsive. Catches physics
    blow-ups, fall-through-floor, and crashes under input the build agent never drove."""
    p0 = _player_pos(base)
    if not _finite(p0):
        return {"name": "stability", "ok": None, "evidence": "no finite player position to probe"}
    floor = p0[1] - 15.0  # generous: a legit drop is fine; a plummet is not
    seq = [("W", 0.5), ("Space", 0.2), ("D", 0.4), ("W", 0.4), ("Space", 0.2),
           ("A", 0.5), ("S", 0.4), ("Space", 0.2), ("D", 0.6), ("W", 0.5)]
    min_y, max_disp = p0[1], 0.0
    for i, (k, h) in enumerate(seq):
        _drive(base, k, h)
        for _ in range(2):  # sample mid-hold
            time.sleep(h / 2 + 0.05)
            p = _player_pos(base)
            if p is None:
                return {"name": "stability", "ok": False, "bug": "player vanished",
                        "evidence": f"player entity disappeared at step {i} ({k})"}
            if not _finite(p):
                return {"name": "stability", "ok": False, "bug": "non-finite position",
                        "evidence": f"player position went non-finite at step {i} ({k}): {p}"}
            if p[1] < floor:
                return {"name": "stability", "ok": False, "bug": "fell through world",
                        "evidence": f"player fell to y={p[1]:.1f} (< floor {floor:.1f}) at step {i} ({k})"}
            min_y = min(min_y, p[1])
            max_disp = max(max_disp, _dist2d(p, p0))
    if not _responsive(base):
        return {"name": "stability", "ok": False, "bug": "engine unresponsive",
                "evidence": "engine stopped answering /api/status after the movement churn"}
    return {"name": "stability", "ok": True,
            "evidence": f"held up under churn: minY={min_y:.1f} (floor {floor:.1f}), "
                        f"maxDisp={max_disp:.1f}u, alive + responsive",
            "coverage": {"max_displacement": round(max_disp, 1), "min_y": round(min_y, 1)}}


def probe_world_edge(base):
    """Drive hard in each cardinal direction for several seconds — try to walk off
    the world / into an illegal region. Assert the player stays finite, doesn't
    plummet, and the engine survives it. Catches missing world-boundary handling."""
    p0 = _player_pos(base)
    if not _finite(p0):
        return {"name": "world_edge", "ok": None, "evidence": "no finite player position to probe"}
    floor = p0[1] - 30.0
    far = 0.0
    for key in ("W", "D", "S", "A"):
        for _ in range(3):  # ~3s of sustained push per direction
            _drive(base, key, 1.0)
            time.sleep(1.1)
            p = _player_pos(base)
            if not _finite(p):
                return {"name": "world_edge", "ok": False, "bug": "non-finite at edge",
                        "evidence": f"driving {key} produced a non-finite position: {p}"}
            if p[1] < floor:
                return {"name": "world_edge", "ok": False, "bug": "fell off world",
                        "evidence": f"driving {key} dropped the player to y={p[1]:.1f} (< {floor:.1f})"}
            far = max(far, _dist2d(p, p0))
    if not _responsive(base):
        return {"name": "world_edge", "ok": False, "bug": "engine unresponsive",
                "evidence": "engine stopped answering after sustained edge-driving"}
    return {"name": "world_edge", "ok": True,
            "evidence": f"reached {far:.0f}u from spawn in 4 directions, stayed finite + grounded + responsive",
            "coverage": {"max_reach": round(far, 0)}}


def probe_input_churn(base):
    """Slam many overlapping keys + mouse buttons in bursts — try to crash or wedge
    the input/animation state machine. Assert the game stays alive + responsive.
    Catches input-handling crashes / stuck states the happy path never hits."""
    p0 = _player_pos(base)
    if p0 is None:
        return {"name": "input_churn", "ok": None, "evidence": "no player to probe"}
    bursts = [
        {"keys": ["W", "A", "Space", "LShift"], "hold": 0.3},
        {"keys": ["S", "D", "Space", "LCtrl"], "mouse": ["LEFT"], "hold": 0.3},
        {"keys": ["W", "D", "A", "S"], "mouse": ["LEFT", "RIGHT"], "hold": 0.3},
        {"keys": ["Space", "Space", "LShift", "W"], "hold": 0.2},
    ]
    for i, b in enumerate(bursts):
        if _post(base, "/api/input/inject", b) is None:
            return {"name": "input_churn", "ok": False, "bug": "inject failed",
                    "evidence": f"/api/input/inject failed / hung on burst {i}"}
        time.sleep(b["hold"] + 0.2)
        if not _responsive(base):
            return {"name": "input_churn", "ok": False, "bug": "engine unresponsive",
                    "evidence": f"engine stopped answering after input burst {i}"}
        if not _alive(base):
            return {"name": "input_churn", "ok": False, "bug": "player lost",
                    "evidence": f"player entity gone after input burst {i}"}
    _post(base, "/api/input/inject", {"release_all": True})
    return {"name": "input_churn", "ok": True,
            "evidence": f"survived {len(bursts)} overlapping key+mouse bursts: alive + responsive"}


def probe_softlock(base):
    """Whole-game completability: from the player's CURRENT position, is the win
    goal still reachable (NavGrid path)? An all-green milestone checklist can still
    be an unwinnable/softlocked game. Only runs when the win is location-based."""
    p = _player_pos(base)
    if p is None:
        return {"name": "softlock", "ok": None, "evidence": "no player to test reachability from"}
    goal = _goal_region(_triggers(base) or [])
    if not goal:
        return {"name": "softlock", "ok": None,
                "evidence": "no location-based win goal (entity_reached_region) — softlock check n/a"}
    sx, sz = int(round(p[0])), int(round(p[2]))
    gx, gz = goal
    path = _get(base, f"/api/navgrid/path?x1={sx}&z1={sz}&x2={gx}&z2={gz}")
    if path and path.get("found"):
        return {"name": "softlock", "ok": True,
                "evidence": f"win goal ({gx},{gz}) still reachable from current pos ({sx},{sz}), "
                            f"{len(path.get('waypoints', []))} waypoints"}
    return {"name": "softlock", "ok": False, "bug": "possible softlock",
            "evidence": f"win goal ({gx},{gz}) NOT reachable from ({sx},{sz}) via NavGrid — possible "
                        f"softlock (verify; NavGrid step-up is conservative)"}


PROBES = [probe_stability, probe_world_edge, probe_input_churn, probe_softlock]


def run_playtest(base, probe_names=None):
    """Run the adversarial probe suite. Returns:
      {clean: bool, probes: [...], bugs: [str], ran: int, skipped: int}
    `clean` is True only if EVERY probe that ran passed (bugs empty). Probes that
    could not run (ok=None) are skipped, not counted against `clean`."""
    selected = [p for p in PROBES if not probe_names or p.__name__.replace("probe_", "") in probe_names]
    results, bugs, ran, skipped = [], [], 0, 0
    for probe in selected:
        r = probe(base)
        results.append(r)
        if r["ok"] is None:
            skipped += 1
        elif r["ok"] is False:
            bugs.append(f"[{r['name']}] {r.get('bug', 'issue')}: {r['evidence']}")
            ran += 1
        else:
            ran += 1
    return {"clean": len(bugs) == 0 and ran > 0, "probes": results, "bugs": bugs,
            "ran": ran, "skipped": skipped}


def summary(report):
    passed = sum(1 for p in report["probes"] if p["ok"] is True)
    return f"{passed}/{report['ran']} probes passed, {report['skipped']} n/a, {len(report['bugs'])} bug(s)"


# --------------------------------------------------------------------------- #
# Trace replay — deterministic regression from authored input+assert sequences.
# A trace is a JSON list of steps:
#   {"do":"inject","keys":["W"],"hold":0.5}   drive input
#   {"do":"mouse","buttons":["LEFT"],"hold":0.2}
#   {"do":"wait","s":0.5}
#   {"do":"fire","id":"win"}                   fire a trigger
#   {"do":"assert","inv":"alive"}              player present
#   {"do":"assert","inv":"y_above","y":10}     not fallen through
#   {"do":"assert","inv":"responsive"}         engine answering
#   {"do":"assert","inv":"screen","is":"menu"} screen state == is
#   {"do":"assert","inv":"moved","min":0.3,"from":[x,z]}  displacement check
# --------------------------------------------------------------------------- #

def _check(base, step):
    inv = step.get("inv")
    if inv == "alive":
        return _alive(base), "player present" if _alive(base) else "player MISSING"
    if inv == "responsive":
        ok = _responsive(base)
        return ok, "engine responsive" if ok else "engine UNRESPONSIVE"
    if inv == "y_above":
        p = _player_pos(base)
        ok = p is not None and p[1] >= step.get("y", -1e9)
        return ok, f"y={p[1]:.1f}>= {step.get('y')}" if p else "no player"
    if inv == "screen":
        s = (_get(base, "/api/screen/state") or {}).get("screen")
        return s == step.get("is"), f"screen={s} (want {step.get('is')})"
    if inv == "moved":
        p = _player_pos(base)
        frm = step.get("from")
        if p is None or not frm:
            return False, "no player / no from"
        d = ((p[0] - frm[0]) ** 2 + (p[2] - frm[1]) ** 2) ** 0.5
        return d >= step.get("min", 0.3), f"moved {d:.2f}u (min {step.get('min', 0.3)})"
    return False, f"unknown invariant '{inv}'"


def run_trace(base, trace):
    """Execute a trace; return {passed: bool, steps: [{do, ok, detail}]}."""
    steps = []
    passed = True
    for step in trace:
        do = step.get("do")
        ok, detail = True, ""
        if do == "inject":
            ok = _post(base, "/api/input/inject",
                       {"keys": step.get("keys", []), "hold": step.get("hold", 0.1)}) is not None
            detail = f"inject {step.get('keys')} hold {step.get('hold', 0.1)}"
        elif do == "mouse":
            ok = _post(base, "/api/input/inject",
                       {"mouse": step.get("buttons", []), "hold": step.get("hold", 0.1)}) is not None
            detail = f"mouse {step.get('buttons')}"
        elif do == "wait":
            time.sleep(step.get("s", 0.2))
            detail = f"wait {step.get('s', 0.2)}s"
        elif do == "fire":
            body = {"id": step["id"]} if "id" in step else {"event": step.get("event"), "data": step.get("data", {})}
            ok = (_post(base, "/api/triggers/fire", body) or {}).get("success", False)
            detail = f"fire {step.get('id') or step.get('event')}"
        elif do == "assert":
            ok, detail = _check(base, step)
            detail = f"assert {step.get('inv')}: {detail}"
        else:
            ok, detail = False, f"unknown step '{do}'"
        steps.append({"do": do, "ok": ok, "detail": detail})
        if not ok:
            passed = False
            if do == "assert":
                break  # a failed assertion aborts the trace (regression caught)
    return {"passed": passed, "steps": steps}


def load_traces(project_dir):
    """Authored regression traces from <project>/.phyxel/playtest_traces/*.json.
    Each file: {"name": "...", "trace": [ ...steps... ]}."""
    d = Path(project_dir) / ".phyxel" / "playtest_traces"
    out = []
    if d.is_dir():
        for f in sorted(d.glob("*.json")):
            try:
                data = json.loads(f.read_text())
                out.append({"name": data.get("name", f.stem), "trace": data.get("trace", [])})
            except (json.JSONDecodeError, OSError):
                out.append({"name": f.stem, "trace": None, "error": "unreadable"})
    return out


def run_regression(base, project_dir):
    """Replay every authored trace; return {clean, traces:[{name, passed, ...}]}."""
    traces = load_traces(project_dir)
    results = []
    for t in traces:
        if t.get("trace") is None:
            results.append({"name": t["name"], "passed": False, "error": t.get("error", "no trace")})
            continue
        r = run_trace(base, t["trace"])
        results.append({"name": t["name"], "passed": r["passed"], "steps": r["steps"]})
    return {"clean": all(r.get("passed") for r in results) if results else True,
            "traces": results, "count": len(results)}
