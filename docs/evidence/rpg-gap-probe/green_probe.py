"""Green-run probe for the scaffold-parity fixes (StandaloneParityGaps.md §5 item 1).

Run A (fresh DBs): objectives load + complete_objective chain, loading-screen
shown/dismissed on BOTH transitions, save_game action, graceful quit-save.
Run B (relaunch): saved profile restored from the world DB.

Green flips vs docs/evidence/rpg-gap-probe (red baseline):
  G1: NO "Unhandled trigger action" WARN; quest_chain trigger fired=true via
      the objective_complete event (complete_objective now handled end-to-end)
  G2: "Loading screen shown"/"dismissed" log lines on each transition (+ the
      API may or may not sample the brief "loading" state at ~1200fps — the
      log is the deterministic observable)
  G3: player_state row in town.db after save/quit; Run B logs
      "Restored saved player profile"
"""
import json
import sqlite3
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

PORT = 8100
BASE = f"http://127.0.0.1:{PORT}"
PROJ = Path.home() / "Documents" / "PhyxelProjects" / "RpgGapProbe"
RELDIR = PROJ / "build" / "Release"
EXE = RELDIR / "RpgGapProbe.exe"
SCRATCH = Path(__file__).parent
EVIDENCE = SCRATCH / "green_probe_evidence.json"

evidence = {"probe_started": time.strftime("%Y-%m-%d %H:%M:%S"), "steps": []}
T0 = time.time()


def record(step, data):
    evidence["steps"].append({"step": step, "data": data, "t": round(time.time() - T0, 2)})
    print(f"[{step}] {json.dumps(data, default=str)[:300]}")


def api(method, path, body=None, timeout=10):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(BASE + path, data=data, method=method,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode())


def wait_api(seconds=90):
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            return api("GET", "/api/state", timeout=3)
        except Exception:
            time.sleep(1.0)
    raise RuntimeError("API never came up")


def launch(log_name):
    fh = open(SCRATCH / log_name, "w", encoding="utf-8", errors="replace")
    p = subprocess.Popen([str(EXE), "--test", str(PORT)], cwd=str(RELDIR),
                         stdout=fh, stderr=subprocess.STDOUT)
    record("launch", {"log": log_name, "pid": p.pid})
    return p, fh


def poll_transition(trigger_id, seconds=8.0):
    fired = api("POST", "/api/triggers/fire", {"id": trigger_id})
    record(f"fire_{trigger_id}", fired)
    seen = []
    t_end = time.time() + seconds
    while time.time() < t_end:
        try:
            s = api("GET", "/api/screen/state", timeout=2).get("screen")
            if not seen or seen[-1] != s:
                seen.append(s)
        except Exception:
            if not seen or seen[-1] != "API_UNRESPONSIVE":
                seen.append("API_UNRESPONSIVE")
        time.sleep(0.03)
    record(f"transition_states_{trigger_id}", {"states_seen": seen})
    return seen


# ── Fresh start: delete scene DBs so Run A is pristine ──────────────────────
for f in (RELDIR / "worlds").glob("*.db*"):
    f.unlink()
record("fresh_dbs", {"deleted": True})

# ═══ RUN A ═══════════════════════════════════════════════════════════════════
proc, fh = launch("green_runA.log")
try:
    wait_api()
    record("screen_initial", api("GET", "/api/screen/state"))

    trg = api("GET", "/api/triggers")
    record("list_triggers", {"ids": [t.get("id") for t in trg.get("triggers", [])]})

    # G1: objective chain — complete_objective must now be handled, and its
    # objective_complete event must fire the quest_chain trigger.
    record("fire_win_quest", api("POST", "/api/triggers/fire", {"id": "win_quest"}))
    time.sleep(1.0)
    trg = api("GET", "/api/triggers")
    chain = next((t for t in trg.get("triggers", []) if t.get("id") == "quest_chain"), None)
    record("quest_chain_state", chain or {"error": "quest_chain not found"})
    record("screen_after_win", api("GET", "/api/screen/state"))

    # dismiss victory (escape -> playing per GameScreen)
    api("POST", "/api/input/inject", {"key": "escape", "hold": 0.1})
    time.sleep(0.5)

    # G2: both transitions, polled identically to the red baseline
    poll_transition("to_cellar")
    poll_transition("back_to_town")

    # G3: authored save point, then graceful quit (quit-save in onShutdown)
    record("fire_save_point", api("POST", "/api/triggers/fire", {"id": "save_point"}))
    time.sleep(0.5)
    record("fire_graceful_quit", api("POST", "/api/triggers/fire", {"id": "graceful_quit"}))
    for _ in range(60):           # wait for a clean exit
        if proc.poll() is not None:
            break
        time.sleep(0.5)
    record("process_exit", {"returncode": proc.poll()})
except Exception as e:
    record("RUN_A_ERROR", {"error": repr(e)})
finally:
    if proc.poll() is None:
        proc.kill()
        record("hard_kill_runA", {"note": "graceful quit did not exit in 30s"})
    fh.close()

# G3 evidence: player_state row present in the town DB?
time.sleep(0.5)
try:
    con = sqlite3.connect(RELDIR / "worlds" / "town.db")
    rows = con.execute("SELECT player_id, profile_json FROM player_state").fetchall()
    con.close()
    record("db_player_state", {"rows": [(r[0], json.loads(r[1])) for r in rows]})
except Exception as e:
    record("db_player_state", {"error": repr(e)})

# ═══ RUN B: restore ══════════════════════════════════════════════════════════
proc, fh = launch("green_runB.log")
try:
    wait_api()
    record("runB_screen", api("GET", "/api/screen/state"))
    record("runB_state", api("GET", "/api/state"))
except Exception as e:
    record("RUN_B_ERROR", {"error": repr(e)})
finally:
    try:
        proc.kill()
    except Exception:
        pass
    fh.close()

# ── Log excerpts ────────────────────────────────────────────────────────────
KEYS = ("Unhandled trigger", "objective", "Objective", "Loading screen",
        "profile", "Profile", "SceneManager", "ERROR", "WARN", "victory")
for run in ("green_runA.log", "green_runB.log"):
    try:
        text = (SCRATCH / run).read_text(encoding="utf-8", errors="replace")
        evidence[run] = [ln for ln in text.splitlines() if any(k in ln for k in KEYS)][-80:]
    except Exception as e:
        evidence[run] = [f"could not read: {e!r}"]

EVIDENCE.write_text(json.dumps(evidence, indent=2), encoding="utf-8")
print(f"\nEvidence written to {EVIDENCE}")
