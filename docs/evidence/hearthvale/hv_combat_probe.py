"""Hearthvale increment 2: the combat beat, played over the --test API.

Green flips vs the red baseline (503 on /api/rpg/combat/state, captured
2026-08-13 on the pre-combat exe):
  C1: combat/state answers (mode turn_based, from game.json "combat")
  C2: crossing the cellar guard region fires the authored start_combat trigger
      -> in_combat true, initiative rolled, turn order real
  C3: player turn: move-toward + attack the rat resolve through the funnel
      (log evidence); end_turn hands over; the enemy AI takes its turn;
      rounds advance
  C4: the quest STILL completes with the combat beat in the path
      (remedy -> return -> turn-in -> victory)
"""
import json, math, subprocess, time, urllib.request
from pathlib import Path

PORT = 8101
BASE = f"http://127.0.0.1:{PORT}"
RELDIR = Path.home() / "Documents/PhyxelProjects/Hearthvale/build/Release"
SCRATCH = Path(__file__).parent
ev = {"steps": []}
T0 = time.time()

def rec(s, d):
    ev["steps"].append({"step": s, "t": round(time.time()-T0,1), "data": d})
    print(f"[{s}] {json.dumps(d, default=str)[:250]}")

def api(m, p, b=None, t=10):
    d = json.dumps(b).encode() if b is not None else None
    r = urllib.request.Request(BASE+p, data=d, method=m, headers={"Content-Type":"application/json"})
    with urllib.request.urlopen(r, timeout=t) as x: return json.loads(x.read().decode())

def combat(action, body=None):
    return api("POST", f"/api/rpg/combat/{action}", body or {})

def player_pos():
    for e in api("GET","/api/state").get("entities", []):
        if e.get("id") == "player":
            p = e["position"]; return (p["x"], p["z"])
    return None

def rat_pos():
    for e in api("GET","/api/state").get("entities", []):
        if e.get("id") == "npc_Rat":
            p = e["position"]; return (p["x"], p["y"], p["z"])
    return None

def key(k, hold):
    api("POST","/api/input/inject",{"keys":[k],"hold":hold})
    time.sleep(hold + 0.25)

def screen(): return api("GET","/api/screen/state")

def steer_to(dirs, tx, tz, tol=1.2, max_iter=50, stop=None):
    for i in range(max_iter):
        if screen().get("screen") == "paused":
            key("Escape", 0.1); time.sleep(0.4)
        if stop and stop():
            rec("steer_stopped", {"iter": i}); return "stopped"
        p = player_pos()
        if p is None: time.sleep(1.0); continue
        dx, dz = tx-p[0], tz-p[1]
        dist = math.hypot(dx,dz)
        if dist < tol:
            rec("steer_arrived", {"iter": i, "pos": [round(p[0],1),round(p[1],1)]}); return "arrived"
        best = max(dirs, key=lambda k2: (dirs[k2][0]*dx + dirs[k2][1]*dz)/dist)
        key(best, min(0.6, max(0.15, dist*0.09)))
    rec("steer_stuck", {"pos": player_pos()}); return "stuck"

# ── Launch fresh ────────────────────────────────────────────────────────────
for f in (RELDIR/"worlds").glob("*.db*"):
    for _ in range(10):
        try: f.unlink(); break
        except PermissionError: time.sleep(1)
log = open(SCRATCH/"hv_combat.log", "w", encoding="utf-8", errors="replace")
proc = subprocess.Popen([str(RELDIR/"Hearthvale.exe"), "--test", str(PORT)],
                        cwd=str(RELDIR), stdout=log, stderr=subprocess.STDOUT)
try:
    dl = time.time()+90
    while time.time() < dl:
        try: api("GET","/api/state",t=3); break
        except Exception: time.sleep(1)

    # C1: the endpoint that 503'd on the red exe now answers
    rec("combat_state_boot", combat("state"))

    api("POST","/api/ui/click",{"x":640,"y":384}); time.sleep(3)   # Begin
    dirs = {"W":(-0.71,-0.71),"D":(0.71,-0.71),"S":(0.71,0.71),"A":(-0.71,0.71)}  # calibrated 3x today
    # accept the quest, walk east to the cellar
    key("E",0.1); time.sleep(0.8); key("1",0.1); time.sleep(0.8)
    key("Enter",0.1); time.sleep(0.5); key("Enter",0.1); time.sleep(0.5)
    steer_to(dirs, 28, 16, stop=lambda: screen().get("scene_id")=="cellar")
    time.sleep(2)
    rec("in_cellar", {"scene": screen().get("scene_id"), "pos": player_pos(), "rat": rat_pos()})

    # C2: walk into the guard region (x11-13) -> authored start_combat fires
    steer_to(dirs, 12, 18, tol=1.0, stop=lambda: combat("state").get("in_combat"))
    time.sleep(1.0)
    st = combat("state"); rec("encounter_started", st)

    # C3: fight 4 rounds — on the player's turn approach + attack, then end turn
    rounds_seen = set()
    for i in range(40):
        st = combat("state")
        if not st.get("in_combat"): break
        rounds_seen.add(st.get("round"))
        if len(rounds_seen) >= 4: break
        if st.get("player_turn"):
            ti = combat("targeting_info", {"target_id": "npc_Rat"})
            if i % 7 == 0: rec("targeting", ti)
            if ti.get("in_reach"):
                rec("attack", combat("player_attack", {"target_id": "npc_Rat"}))
                time.sleep(2.0)   # attack animation resolves at the hit frame
            else:
                rp = rat_pos()
                if rp: combat("player_move", {"x": rp[0], "y": rp[1], "z": rp[2]})
                time.sleep(2.0)
            combat("end_turn"); time.sleep(1.0)
        else:
            time.sleep(1.0)       # enemy AI turn runs on its own
    st = combat("state")
    rec("combat_after_rounds", {"state": st, "rounds_seen": sorted(rounds_seen)})

    # C4: wrap the encounter and finish the quest with the combat beat behind us
    combat("end")
    steer_to(dirs, 16, 16)                       # remedy shelf
    time.sleep(1)
    steer_to(dirs, 6, 16, stop=lambda: screen().get("scene_id")=="town")
    time.sleep(2)
    steer_to(dirs, 18, 17.2, tol=2.0)
    key("E",0.1); time.sleep(0.8); key("2",0.1); time.sleep(0.8)
    key("Enter",0.1); time.sleep(0.5); key("Enter",0.1); time.sleep(1.5)
    rec("finale", {"screen": screen(), "objectives_done": None})
except Exception as e:
    rec("PROBE_ERROR", {"error": repr(e)})
finally:
    proc.kill(); log.close(); time.sleep(0.5)
    txt = (SCRATCH/"hv_combat.log").read_text(encoding="utf-8", errors="replace")
    ev["log"] = [l for l in txt.splitlines() if any(k in l for k in
        ("ombat","Damage","damage","attack","Attack","initiative","Initiative",
         "turn","Turn","objective","Unhandled","ERROR"))][-100:]
(SCRATCH/"hv_combat_evidence.json").write_text(json.dumps(ev, indent=2))
print("\ndone")
