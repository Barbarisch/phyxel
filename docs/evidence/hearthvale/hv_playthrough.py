"""Hearthvale full playthrough over the --test API: menu -> accept quest ->
walk to cellar -> find remedy -> walk back -> turn in -> victory. Then a
relaunch (save/reload) and a sequence-break run (turn-in without finding).

Movement: no camera-set endpoint on the standalone, so steering is empirical -
sample the XZ displacement of W and D presses once, derive the 4 key directions,
then greedily press the best-aligned key toward the target each iteration.
"""
import json, math, subprocess, time, urllib.request
from pathlib import Path

PORT = 8101
BASE = f"http://127.0.0.1:{PORT}"
RELDIR = Path.home() / "Documents/PhyxelProjects/Hearthvale/build/Release"
SCRATCH = Path(__file__).parent
ev = {"runs": []}
T0 = time.time()
cur = None  # current run record

def rec(s, d):
    cur["steps"].append({"step": s, "t": round(time.time()-T0,1), "data": d})
    print(f"[{s}] {json.dumps(d, default=str)[:240]}")

def api(m, p, b=None, t=10):
    d = json.dumps(b).encode() if b is not None else None
    r = urllib.request.Request(BASE+p, data=d, method=m, headers={"Content-Type":"application/json"})
    with urllib.request.urlopen(r, timeout=t) as x: return json.loads(x.read().decode())

def player_pos():
    for e in api("GET","/api/state").get("entities", []):
        if e.get("id") == "player":
            p = e["position"]; return (p["x"], p["z"])
    return None

def key(k, hold):
    r = api("POST","/api/input/inject",{"keys":[k],"hold":hold})
    if not r.get("injected"):
        rec("inject_unresolved", {"key": k, "resp": r})
    time.sleep(hold + 0.25)

def screen():
    return api("GET","/api/screen/state")

def launch(tag):
    global cur
    cur = {"tag": tag, "steps": []}
    ev["runs"].append(cur)
    fh = open(SCRATCH/f"hv_{tag}.log", "w", encoding="utf-8", errors="replace")
    p = subprocess.Popen([str(RELDIR/"Hearthvale.exe"), "--test", str(PORT)],
                        cwd=str(RELDIR), stdout=fh, stderr=subprocess.STDOUT)
    dl = time.time()+90
    while time.time() < dl:
        try: api("GET","/api/state",t=3); break
        except Exception: time.sleep(1)
    return p, fh

def calibrate():
    """Measure the XZ direction of W and D. Returns dict key->unit vector."""
    dirs = {}
    for k in ("W","D"):
        p0 = player_pos(); key(k, 0.35); p1 = player_pos()
        dx, dz = p1[0]-p0[0], p1[1]-p0[1]
        n = math.hypot(dx,dz) or 1e-9
        dirs[k] = (dx/n, dz/n)
    dirs["S"] = (-dirs["W"][0], -dirs["W"][1])
    dirs["A"] = (-dirs["D"][0], -dirs["D"][1])
    rec("calibrate", {k: [round(v,2) for v in d] for k,d in dirs.items()})
    return dirs

def steer_to(dirs, tx, tz, tol=1.2, max_iter=50, stop_scene=None):
    """Greedy steering; returns 'arrived' | 'scene_changed' | 'stuck'."""
    for i in range(max_iter):
        if screen().get("screen") == "paused":
            key("Escape", 0.1); time.sleep(0.4)   # un-pause if something paused us
        if stop_scene:
            sc = screen().get("scene_id")
            if sc == stop_scene:
                rec("steer_scene_changed", {"iter": i, "scene": sc}); return "scene_changed"
        p = player_pos()
        if p is None:  # mid-transition
            time.sleep(1.0); continue
        dx, dz = tx-p[0], tz-p[1]
        dist = math.hypot(dx,dz)
        if dist < tol:
            rec("steer_arrived", {"iter": i, "pos": [round(p[0],1),round(p[1],1)]}); return "arrived"
        best = max(dirs, key=lambda k: (dirs[k][0]*dx + dirs[k][1]*dz)/dist)
        key(best, min(0.6, max(0.15, dist*0.09)))
    rec("steer_stuck", {"pos": player_pos()}); return "stuck"

def dialogue(choice_key):
    """E to interact, pick a numbered choice, Enter through the reply, Esc out."""
    key("E", 0.1); time.sleep(0.8)
    key(choice_key, 0.1); time.sleep(0.8)
    key("Enter", 0.1); time.sleep(0.5)
    key("Enter", 0.1); time.sleep(0.5)

def fired(trig_id):
    for t in api("GET","/api/triggers").get("triggers", []):
        if t.get("id") == trig_id: return t.get("fired")
    return None

# â•â•â• RUN A: the intended quest loop â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
for f in (RELDIR/"worlds").glob("*.db*"):
    for _ in range(10):
        try: f.unlink(); break
        except PermissionError: time.sleep(1)
proc, fh = launch("runA")
try:
    rec("boot", screen())
    api("POST","/api/ui/click",{"x":640,"y":384}); time.sleep(3)   # Begin
    rec("in_town", {"screen": screen(), "pos": player_pos()})
    dirs = calibrate()
    # Quest accept: Elder is within interact range of spawn
    dialogue("1")
    rec("after_accept", {"log_check": "see hv_runA.log for dialogue lines"})
    # Walk east to the cellar door region (center 28,16)
    r = steer_to(dirs, 28, 16, stop_scene="cellar")
    time.sleep(2); rec("cellar_arrival", {"result": r, "screen": screen(), "pos": player_pos()})
    # Walk to the remedy shelf (16,16)
    r = steer_to(dirs, 16, 16)
    time.sleep(1)
    rec("remedy", {"result": r, "find_remedy_fired": fired("find_remedy")})
    # Walk to the exit region (6,16)
    r = steer_to(dirs, 6, 16, stop_scene="town")
    time.sleep(2); rec("back_in_town", {"result": r, "screen": screen(), "pos": player_pos()})
    # Walk to the Elder (18,18) and turn in (choice 2)
    r = steer_to(dirs, 18, 17.2, tol=2.0)
    dialogue("2")
    time.sleep(1.5)
    rec("finale", {"screen": screen(), "win_fired": fired("win")})
except Exception as e:
    rec("RUN_A_ERROR", {"error": repr(e)})
finally:
    proc.kill(); fh.close(); time.sleep(1)

# â•â•â• RUN B: relaunch â€” what survives a save/reload? â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
proc, fh = launch("runB")
try:
    rec("boot", screen())
    st = api("GET","/api/state")
    rec("restored_state", {"camera": st.get("camera"),
                           "entities": [e.get("id") for e in st.get("entities",[])]})
except Exception as e:
    rec("RUN_B_ERROR", {"error": repr(e)})
finally:
    proc.kill(); fh.close(); time.sleep(1)

# â•â•â• RUN C: sequence break â€” turn in WITHOUT ever finding the remedy â•â•â•â•â•â•â•â•
for f in (RELDIR/"worlds").glob("*.db*"):
    for _ in range(10):
        try: f.unlink(); break
        except PermissionError: time.sleep(1)
proc, fh = launch("runC")
try:
    rec("boot", screen())
    api("POST","/api/ui/click",{"x":640,"y":384}); time.sleep(3)
    dialogue("2")   # straight to "I have your remedy" â€” never visited the cellar
    time.sleep(1.5)
    rec("sequence_break", {"screen": screen(), "win_fired": fired("win"),
                           "note": "victory without find_remedy = dialogue conditions needed"})
except Exception as e:
    rec("RUN_C_ERROR", {"error": repr(e)})
finally:
    proc.kill(); fh.close(); time.sleep(1)

for tag in ("runA","runB","runC"):
    try:
        txt = (SCRATCH/f"hv_{tag}.log").read_text(encoding="utf-8", errors="replace")
        ev[f"log_{tag}"] = [l for l in txt.splitlines() if any(k in l for k in
            ("objective","Objective","Loading screen","Dialogue","dialogue","conversation",
             "profile","Trigger","Unhandled","ERROR"))][-60:]
    except Exception as e:
        ev[f"log_{tag}"] = [repr(e)]
(SCRATCH/"hv_playthrough_evidence.json").write_text(json.dumps(ev, indent=2))
print("\ndone")
