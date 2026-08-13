"""Runtime gap probe for RpgGapProbe standalone (--test API host).

Measures, against the REAL packaged-game shell:
  M1: complete_objective / fail_objective are unhandled trigger actions (LOG_WARN)
  M2: loading_screen transitionStyle shows no Loading screen state during a scene transition
  M3: show_victory works (screen state -> victory)  [control: proves the probe itself works]
  M4: triggers from per-scene definitions load in the standalone
  M5: screen-state flow intro -> menu -> playing via ui_click (real shell state machine)

Writes evidence JSON + the game's stdout log excerpt to the evidence file.
"""
import json
import subprocess
import sys
import time
import urllib.request
import urllib.error
from pathlib import Path

PORT = 8100
BASE = f"http://127.0.0.1:{PORT}"
PROJ = Path.home() / "Documents" / "PhyxelProjects" / "RpgGapProbe"
EXE = PROJ / "build" / "Release" / "RpgGapProbe.exe"
SCRATCH = Path(__file__).parent
GAME_LOG = SCRATCH / "rpggapprobe_game.log"
EVIDENCE = SCRATCH / "gap_probe_evidence.json"

evidence = {"probe_started": time.strftime("%Y-%m-%d %H:%M:%S"), "steps": []}


def record(step, data):
    evidence["steps"].append({"step": step, "data": data, "t": round(time.time() - T0, 2)})
    print(f"[{step}] {json.dumps(data)[:300]}")


def api(method, path, body=None, timeout=10):
    url = BASE + path
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method,
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


T0 = time.time()

# ── Launch the real packaged game with the test API ─────────────────────────
log_fh = open(GAME_LOG, "w", encoding="utf-8", errors="replace")
proc = subprocess.Popen([str(EXE), "--test", str(PORT)], cwd=str(EXE.parent),
                        stdout=log_fh, stderr=subprocess.STDOUT)
record("launch", {"exe": str(EXE), "pid": proc.pid, "port": PORT})

try:
    state = wait_api()
    record("api_up", state)

    # M5: real shell screen-state flow
    scr = api("GET", "/api/screen/state")
    record("screen_initial", scr)

    # Click through intro -> menu -> playing (1280x720 shell layout;
    # probe a column of y positions to find the buttons like the ApiTestGame run did)
    def click(x, y):
        return api("POST", "/api/ui/click", {"x": x, "y": y})

    flow = [scr.get("screen")]
    for attempt in range(12):
        scr = api("GET", "/api/screen/state")
        s = scr.get("screen")
        if s != flow[-1]:
            flow.append(s)
        if s == "playing":
            break
        if s == "intro":
            click(640, 360)          # anywhere advances intro (or Enter)
            api("POST", "/api/input/inject", {"key": "enter", "hold": 0.1})
        elif s in ("menu", "main_menu", "mainmenu"):
            # probe likely button rows: Start/New Game
            for y in (330, 350, 370, 390, 410, 430, 450):
                click(640, y)
                time.sleep(0.25)
                if api("GET", "/api/screen/state").get("screen") == "playing":
                    break
        time.sleep(1.0)
    scr = api("GET", "/api/screen/state")
    if scr.get("screen") != "playing":
        flow.append(scr.get("screen"))
    record("screen_flow", {"sequence": flow, "final": scr})

    # M4: triggers loaded from the town scene definition?
    trg = api("GET", "/api/triggers")
    record("list_triggers", trg)

    # M1: fire win_quest -> complete_objective should hit the unhandled path,
    # show_victory should work. Response lists executed actions either way.
    fired = api("POST", "/api/triggers/fire", {"id": "win_quest"})
    record("fire_win_quest", fired)
    time.sleep(0.5)
    scr = api("GET", "/api/screen/state")
    record("screen_after_win", scr)   # M3: expect victory

    # Leave victory back to playing if possible (Esc / click), then M2.
    api("POST", "/api/input/inject", {"key": "escape", "hold": 0.1})
    time.sleep(0.5)
    click(640, 400)
    time.sleep(0.5)
    scr = api("GET", "/api/screen/state")
    record("screen_after_victory_dismiss", scr)

    # M2: scene transitions with transitionStyle=loading_screen.
    # Poll screen state fast during BOTH transitions; a wired loading screen
    # would surface state "loading". Prediction: it never does.
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

    poll_transition("to_cellar")
    st = api("GET", "/api/state")
    record("state_after_transition", st)

    # Fire return trigger (cellar scene's own trigger) to prove per-scene
    # trigger REPLACEMENT happened, polling the second transition identically.
    trg2 = api("GET", "/api/triggers")
    record("triggers_in_cellar", trg2)
    poll_transition("back_to_town")
    st = api("GET", "/api/state")
    record("state_back_in_town", st)
    scr = api("GET", "/api/screen/state")
    record("screen_back_in_town", scr)

except Exception as e:
    record("PROBE_ERROR", {"error": repr(e)})
finally:
    try:
        proc.kill()
    except Exception:
        pass
    log_fh.close()
    time.sleep(0.5)
    # Pull the interesting lines out of the game log
    try:
        log_text = GAME_LOG.read_text(encoding="utf-8", errors="replace")
        interesting = [ln for ln in log_text.splitlines()
                       if any(k in ln for k in (
                           "Unhandled trigger", "SceneManager", "Trigger",
                           "victory", "Victory", "objective", "Objective",
                           "loading", "Loading", "ERROR", "WARN"))]
        evidence["game_log_excerpt"] = interesting[-120:]
        evidence["game_log_total_lines"] = len(log_text.splitlines())
    except Exception as e:
        evidence["game_log_excerpt"] = [f"could not read log: {e!r}"]

EVIDENCE.write_text(json.dumps(evidence, indent=2), encoding="utf-8")
print(f"\nEvidence written to {EVIDENCE}")
