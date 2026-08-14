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

def cam_geom():
    """Camera-vs-player geometry: the BG3 observable. third_person => camera a
    few units behind/above the player; overhead => far above, looking down."""
    st = api("GET","/api/state")
    c = st.get("camera", {}); cp = c.get("position", {})
    pp = None
    for e in st.get("entities", []):
        if e.get("id") == "player": pp = e["position"]
    if not pp: return {"error": "no player"}
    dy = cp.get("y", 0) - pp["y"]
    dh = math.hypot(cp.get("x", 0) - pp["x"], cp.get("z", 0) - pp["z"])
    return {"cam_above_player": round(dy, 1), "cam_horiz_dist": round(dh, 1),
            "pitch": c.get("pitch")}

def calibrate2(dirs):
    """Re-measure W and D displacement â€” the third-person camera FOLLOWS the
    player, so camera-relative key directions drift as the camera swings."""
    for k in ("W","D"):
        p0 = player_pos(); key(k, 0.35); p1 = player_pos()
        if p0 is None or p1 is None: continue
        dx, dz = p1[0]-p0[0], p1[1]-p0[1]
        n = math.hypot(dx,dz)
        if n > 0.05: dirs[k] = (dx/n, dz/n)
    dirs["S"] = (-dirs["W"][0], -dirs["W"][1])
    dirs["A"] = (-dirs["D"][0], -dirs["D"][1])

def steer_to(dirs, tx, tz, tol=1.2, max_iter=50, stop=None):
    last_dist = None; stalled = 0
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
        # Adaptive: if we're not closing on the target, the camera has swung and
        # the direction map is stale â€” re-measure it.
        if last_dist is not None and dist >= last_dist - 0.1:
            stalled += 1
            if stalled >= 3:
                calibrate2(dirs); stalled = 0
        else:
            stalled = 0
        last_dist = dist
        best = max(dirs, key=lambda k2: (dirs[k2][0]*dx + dirs[k2][1]*dz)/dist)
        key(best, min(0.6, max(0.15, dist*0.09)))
    rec("steer_stuck", {"pos": player_pos()}); return "stuck"

# â”€â”€ Launch fresh â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
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
    time.sleep(5)   # FIRST-LOAD SETTLE: interacts have a dead-zone for ~5s after
                    # a scene's FIRST load (anim-parse readiness race — filed)
    dirs = {"W":(-0.71,-0.71),"D":(0.71,-0.71),"S":(0.71,0.71),"A":(-0.71,0.71)}  # seed; adaptively re-measured
    calibrate2(dirs)
    rec("initial_dirs", {k: [round(v,2) for v in d] for k,d in dirs.items()})

    def facing_check(tag, target_xz):
        ps = api("POST","/api/character/player_state",{})
        yaw = ps.get("facing_yaw")
        p = player_pos()
        expect = math.atan2(target_xz[0]-p[0], target_xz[1]-p[1]) if p else None
        err = None
        if yaw is not None and expect is not None:
            err = abs(math.atan2(math.sin(yaw-expect), math.cos(yaw-expect)))
        rec(tag, {"facing_yaw": yaw, "expected_bearing": expect,
                  "err_rad": round(err,3) if err is not None else None})

    # accept the quest â€” the speakers should square up (Elder at 18,18)
    key("E",0.1); time.sleep(0.8)
    facing_check("facing_dialogue", (18.0, 18.0))
    key("1",0.1); time.sleep(0.8)
    key("Enter",0.1); time.sleep(0.5); key("Enter",0.1); time.sleep(0.5)

    # Recruit Bram: walk to him (16,12), talk, choice 1 = join
    steer_to(dirs, 16, 12.6, tol=1.4)
    key("E",0.1); time.sleep(0.8); key("1",0.1); time.sleep(0.8)
    key("Enter",0.1); time.sleep(0.5); key("Enter",0.1); time.sleep(0.5)
    rec("recruited", {"note": "party_joined expected in log"})

    steer_to(dirs, 28, 16, stop=lambda: screen().get("scene_id")=="cellar")
    time.sleep(2)
    rec("in_cellar", {"scene": screen().get("scene_id"), "pos": player_pos(), "rat": rat_pos()})
    rec("camera_exploration", cam_geom())   # BG3: third_person while exploring

    # C2: walk into the guard region (x11-13) -> authored start_combat fires
    steer_to(dirs, 12, 18, tol=1.0, stop=lambda: combat("state").get("in_combat"))
    time.sleep(1.0)
    st = combat("state"); rec("encounter_started", st)
    order = st.get("turn_order", {}).get("order", [])
    rec("party_in_combat", {"combatants": len(order),
                            "ids": [o.get("entityId", o.get("entity_id", "?")) for o in order]})
    time.sleep(1.0)
    rp = rat_pos()
    if rp: facing_check("facing_combat_start", (rp[0], rp[2]))  # squared off vs the rat
    rec("camera_combat", cam_geom())        # BG3: overhead birds-eye in combat
    # WASD must be DEAD during turn-based combat (TurnActor owns movement)
    p0 = player_pos(); key("W", 0.5); p1 = player_pos()
    rec("wasd_suppressed", {"before": p0, "after": p1,
                            "moved": round(math.hypot(p1[0]-p0[0], p1[1]-p0[1]), 3)})

    # C3+K: fight to the KILL â€” the encounter must resolve ITSELF (no manual
    # combat/end): rat at authored maxHealth 10 dies in ~2 hits, entity_died
    # fires the rat_slain trigger, combat_victory ends the encounter.
    rounds_seen = set()
    for i in range(60):
        st = combat("state")
        if not st.get("in_combat"):
            rec("encounter_self_resolved", {"iter": i, "state": st}); break
        rounds_seen.add(st.get("round"))
        if st.get("current_entity") == "player":   # OUR turn only — player_turn is
                                                   # SIDE-based and true on Bram's too
            ti = combat("targeting_info", {"target_id": "npc_Rat"})
            if i % 7 == 0: rec("targeting", ti)
            # BG3 mouse combat: CLICK the rat on screen (attack when in reach,
            # approach-move otherwise resolves via the same click when out of
            # range? no â€” clicking the enemy always resolves attack; when out
            # of reach we click the GROUND next to the rat to close distance).
            scr = combat("screen_of", {"entity_id": "npc_Rat"})
            if ti.get("in_reach") and scr.get("ok"):
                pk = combat("player_pick", {"x": scr["x"], "y": scr["y"]})
                rec("click_attack", {"at": [round(scr["x"]), round(scr["y"])], "resolved": pk.get("resolved")})
                time.sleep(2.5)   # attack animation resolves at the hit frame
            elif scr.get("ok"):
                # click slightly BELOW the rat on screen = the ground near it
                pk = combat("player_pick", {"x": scr["x"], "y": scr["y"] + 60})
                rec("click_move", {"at": [round(scr["x"]), round(scr["y"]+60)], "resolved": pk.get("resolved")})
                time.sleep(2.0)
            else:
                rp = rat_pos()
                if rp: combat("player_move", {"x": rp[0], "y": rp[1], "z": rp[2]})
                time.sleep(2.0)
            combat("end_turn"); time.sleep(1.0)
        else:
            time.sleep(1.0)       # enemy AI turn runs on its own
    st = combat("state")
    rec("combat_final", {"state": st, "rounds_seen": sorted(rounds_seen),
                         "rat_still_listed": rat_pos() is not None})
    time.sleep(1.0)
    rec("camera_restored", cam_geom())      # BG3: back to third_person after combat

    # P1: progression â€” the kill granted XP into a real CharacterSheet
    rec("sheet_after_kill", api("POST","/api/rpg/sheet",{}))

    # C4: finish the quest with the combat beat behind us
    steer_to(dirs, 16, 16)                       # remedy shelf
    time.sleep(1)
    # I1: grabbing the remedy put a real item in a real inventory
    inv = api("POST","/api/rpg/inventory",{})
    rec("inventory_after_pickup", inv.get("inventory"))
    steer_to(dirs, 6, 16, stop=lambda: screen().get("scene_id")=="town")
    time.sleep(2)
    steer_to(dirs, 18, 17.2, tol=2.0)
    key("E",0.1); time.sleep(0.8); key("2",0.1); time.sleep(0.8)
    key("Enter",0.1); time.sleep(0.5); key("Enter",0.1); time.sleep(1.5)
    # P2: 300 XP (kill + 2 objectives) should have crossed the 5e level-2
    # threshold exactly on the final turn-in
    sheet = api("POST","/api/rpg/sheet",{})
    # I2: the turn-in dialogue CONSUMED the remedy (remove_item action)
    inv = api("POST","/api/rpg/inventory",{})
    rec("finale", {"screen": screen(),
                   "xp": sheet.get("sheet",{}).get("experiencePoints"),
                   "classes": sheet.get("sheet",{}).get("classes"),
                   "inventory_after_turnin": inv.get("inventory")})
except Exception as e:
    rec("PROBE_ERROR", {"error": repr(e)})
finally:
    proc.kill(); log.close(); time.sleep(0.5)

# P3: relaunch â€” progression survives the save/reload round trip. The win
# trigger fired save_game AFTER the level-up, so town.db carries xp=300 lvl=2.
time.sleep(1.0)
log2 = open(SCRATCH/"hv_restore.log", "w", encoding="utf-8", errors="replace")
proc2 = subprocess.Popen([str(RELDIR/"Hearthvale.exe"), "--test", str(PORT)],
                         cwd=str(RELDIR), stdout=log2, stderr=subprocess.STDOUT)
try:
    dl = time.time()+90
    while time.time() < dl:
        try: api("GET","/api/state",t=3); break
        except Exception: time.sleep(1)
    api("POST","/api/ui/click",{"x":640,"y":384}); time.sleep(4)   # Begin -> town loads profile
    sheet = api("POST","/api/rpg/sheet",{})
    rec("sheet_after_relaunch", {"xp": sheet.get("sheet",{}).get("experiencePoints"),
                                 "classes": sheet.get("sheet",{}).get("classes")})
except Exception as e:
    rec("RESTORE_ERROR", {"error": repr(e)})
finally:
    proc2.kill(); log2.close(); time.sleep(0.5)
    txt = (SCRATCH/"hv_combat.log").read_text(encoding="utf-8", errors="replace")
    ev["log"] = [l for l in txt.splitlines() if any(k in l for k in
        ("ombat","Damage","damage","attack","Attack","initiative","Initiative",
         "turn","Turn","objective","Unhandled","ERROR"))][-100:]
(SCRATCH/"hv_combat_evidence.json").write_text(json.dumps(ev, indent=2))
print("\ndone")
