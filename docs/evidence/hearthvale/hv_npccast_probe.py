"""NPC casters: the cellar Warden hurls spells from range.

RED (pre-fix exe): CombatAI could only attack-or-move; an NPC out of melee
reach ALWAYS walked. No "NPC ... casts ..." line can exist in the log, and
game.json "casters" is ignored entirely.

GREEN:
  N1: the Warden joins the encounter (3+ combatants) and is a bound caster.
  N2: it CASTS from range instead of closing — log carries
      "NPC 'npc_Warden' casts '<spell>' at '<target>'", and its first cast
      happens while it is still far from its target (measured distance).
  N3: it spends REAL slots: magic_missile (level 1, 2 slots) first, then
      falls back to the fire_bolt cantrip once dry — i.e. the log shows
      magic_missile at most twice and fire_bolt thereafter.
  N4: its spells actually HURT — the player-side takes damage from the
      warden (HP drops with a warden cast in the log), proving the damage
      funnel is wired, not just the roll.
"""
import json, math, re, subprocess, time, urllib.request
from pathlib import Path

PORT = 8101
BASE = f"http://127.0.0.1:{PORT}"
RELDIR = Path.home() / "Documents/PhyxelProjects/Hearthvale/build/Release"
SCRATCH = Path(__file__).parent
ev = {"steps": []}
T0 = time.time()

def rec(s, d):
    ev["steps"].append({"step": s, "t": round(time.time()-T0,1), "data": d})
    print(f"[{s}] {json.dumps(d, default=str)[:240]}")

def api(m, p, b=None, t=10):
    d = json.dumps(b).encode() if b is not None else None
    r = urllib.request.Request(BASE+p, data=d, method=m, headers={"Content-Type":"application/json"})
    with urllib.request.urlopen(r, timeout=t) as x: return json.loads(x.read().decode())

def combat(a, b=None): return api("POST", f"/api/rpg/combat/{a}", b or {})

def ent_pos(eid):
    for e in api("GET","/api/state").get("entities", []):
        if e.get("id") == eid:
            p = e["position"]; return (p["x"], p["y"], p["z"])
    return None

def player_pos():
    p = ent_pos("player")
    return (p[0], p[2]) if p else None

def hp_of(eid):
    """Live HP via /api/rpg/entity_health (added this increment — HP was not
    observable over the API at all before, so damage was log-only)."""
    try:
        r = api("POST","/api/rpg/entity_health", {"id": eid})
        return r.get("health") if r.get("has_health") else None
    except Exception:
        return None

def side_hp():
    """Total HP across the player side (the Warden may target EITHER the
    player or Bram — the check is that the player SIDE takes real damage)."""
    vals = [hp_of("player"), hp_of("npc_Bram")]
    vals = [v for v in vals if isinstance(v, (int, float))]
    return sum(vals) if vals else None

def key(k, hold):
    api("POST","/api/input/inject",{"keys":[k],"hold":hold})
    time.sleep(hold + 0.25)

def screen(): return api("GET","/api/screen/state")

def calibrate2(dirs):
    for k in ("W","D"):
        p0 = player_pos(); key(k, 0.35); p1 = player_pos()
        if p0 is None or p1 is None: continue
        dx, dz = p1[0]-p0[0], p1[1]-p0[1]
        n = math.hypot(dx,dz)
        if n > 0.05: dirs[k] = (dx/n, dz/n)
    dirs["S"] = (-dirs["W"][0], -dirs["W"][1])
    dirs["A"] = (-dirs["D"][0], -dirs["D"][1])

def steer_to(dirs, tx, tz, tol=1.2, max_iter=50, stop=None):
    last = None; stalled = 0
    for i in range(max_iter):
        if screen().get("screen") == "paused":
            key("Escape", 0.1); time.sleep(0.4)
        if stop and stop(): rec("steer_stopped", {"iter": i}); return
        p = player_pos()
        if p is None: time.sleep(1.0); continue
        dx, dz = tx-p[0], tz-p[1]; dist = math.hypot(dx,dz)
        if dist < tol: rec("steer_arrived", {"iter": i}); return
        if last is not None and dist >= last - 0.1:
            stalled += 1
            if stalled >= 3: calibrate2(dirs); stalled = 0
        else: stalled = 0
        last = dist
        best = max(dirs, key=lambda k2: (dirs[k2][0]*dx + dirs[k2][1]*dz)/dist)
        key(best, min(0.6, max(0.15, dist*0.09)))
    rec("steer_stuck", {"pos": player_pos()})

results = []
def check(name, ok, detail):
    results.append((name, bool(ok)))
    rec(("PASS " if ok else "FAIL ") + name, detail)

for f in (RELDIR/"worlds").glob("*.db*"):
    for _ in range(10):
        try: f.unlink(); break
        except PermissionError: time.sleep(1)
log = open(SCRATCH/"hv_npccast.log", "w", encoding="utf-8", errors="replace")
proc = subprocess.Popen([str(RELDIR/"Hearthvale.exe"), "--test", str(PORT)],
                        cwd=str(RELDIR), stdout=log, stderr=subprocess.STDOUT)
warden_far_at_first_cast = None
hp_series = []
try:
    dl = time.time()+90
    while time.time() < dl:
        try: api("GET","/api/state",t=3); break
        except Exception: time.sleep(1)

    api("POST","/api/ui/click",{"x":640,"y":384}); time.sleep(3)
    time.sleep(5)
    dirs = {"W":(-0.71,-0.71),"D":(0.71,-0.71),"S":(0.71,0.71),"A":(-0.71,0.71)}
    calibrate2(dirs)
    key("E",0.1); time.sleep(0.8); key("1",0.1); time.sleep(0.8)
    key("Enter",0.1); time.sleep(0.5); key("Enter",0.1); time.sleep(0.5)
    steer_to(dirs, 16, 12.6, tol=1.4)
    key("E",0.1); time.sleep(0.8); key("1",0.1); time.sleep(0.8)
    key("Enter",0.1); time.sleep(0.5); key("Enter",0.1); time.sleep(0.5)
    steer_to(dirs, 28, 16, stop=lambda: screen().get("scene_id")=="cellar")
    time.sleep(6)
    steer_to(dirs, 12, 18, tol=1.0, stop=lambda: combat("state").get("in_combat"))
    time.sleep(1.5)

    st = combat("state")
    order = [o.get("entityId", o.get("entity_id","?")) for o in st.get("turn_order",{}).get("order",[])]
    rec("encounter", {"in_combat": st.get("in_combat"), "order": order})
    check("N1 the Warden joins the encounter as a combatant",
          st.get("in_combat") is True and "npc_Warden" in order and len(order) >= 3,
          {"order": order})

    # Play the fight WITHOUT approaching the Warden: attack the rat only, end
    # turn, and let the enemy AI act. The Warden should hurt us from where it
    # stands — never closing to melee.
    hp_series.append(side_hp())
    for i in range(45):
        st = combat("state")
        if not st.get("in_combat"):
            rec("combat_over", {"iter": i}); break
        if st.get("current_entity") == "player":
            scr = combat("screen_of", {"entity_id": "npc_Rat"})
            ti  = combat("targeting_info", {"target_id": "npc_Rat"})
            if ti.get("in_reach") and scr.get("ok"):
                combat("player_pick", {"x": scr["x"], "y": scr["y"]}); time.sleep(2.0)
            elif scr.get("ok"):
                combat("player_pick", {"x": scr["x"], "y": scr["y"] + 60}); time.sleep(1.8)
            combat("end_turn"); time.sleep(0.8)
        else:
            time.sleep(1.0)
        hp_series.append(side_hp())
        # snapshot the warden's distance to the player the first time a cast is
        # visible in the log (measured, not assumed)
        if warden_far_at_first_cast is None:
            txt = Path(SCRATCH/"hv_npccast.log").read_text(encoding="utf-8", errors="replace")
            m = re.search(r"NPC 'npc_Warden' casts '\w+' at '(\w+)'", txt)
            if m:
                # Distance to the ENTITY IT CAST AT (it may pick the player or
                # Bram) — "cast from range" is about its actual target.
                w, t = ent_pos("npc_Warden"), ent_pos(m.group(1))
                if w and t:
                    warden_far_at_first_cast = round(math.hypot(w[0]-t[0], w[2]-t[2]), 2)
                    rec("warden_distance_at_first_cast",
                        {"target": m.group(1), "dist": warden_far_at_first_cast})
    rec("hp_series", {"hp": hp_series})
except Exception as e:
    rec("PROBE_ERROR", {"error": repr(e)})
finally:
    proc.kill(); log.close(); time.sleep(0.5)

txt = (SCRATCH/"hv_npccast.log").read_text(encoding="utf-8", errors="replace")
casts = re.findall(r"NPC 'npc_Warden' casts '(\w+)' at '(\w+)' for (\d+)", txt)
ev["warden_casts"] = casts
ev["cast_lines"] = [l.strip() for l in txt.splitlines() if "npc_Warden' casts" in l][-8:]
# A caster CASTS: never a melee swing while it still has castable spells.
melee_lines = [l for l in txt.splitlines()
               if re.search(r"NPC 'npc_Warden' (hits|misses) ", l)]
ev["warden_melee_lines"] = melee_lines[:4]
check("N2 the Warden CASTS (spells preferred over melee, at range)",
      len(casts) >= 2 and not melee_lines
      and warden_far_at_first_cast is not None and warden_far_at_first_cast > 2.0,
      {"casts": len(casts), "melee_swings": len(melee_lines),
       "dist_at_first_cast": warden_far_at_first_cast,
       "lines": ev["cast_lines"][:2]})
mm = [c for c in casts if c[0] == "magic_missile"]
fb = [c for c in casts if c[0] == "fire_bolt"]
check("N3 real slots: magic_missile at most twice, then the cantrip",
      len(mm) <= 2 and (len(casts) <= 2 or len(fb) >= 1),
      {"magic_missile": len(mm), "fire_bolt": len(fb), "total": len(casts)})
hp_clean = [h for h in hp_series if h is not None]
damage_taken = bool(hp_clean) and min(hp_clean) < max(hp_clean)
check("N4 warden spells actually damage the player side",
      damage_taken and any(int(c[2]) > 0 for c in casts),
      {"hp_min": min(hp_clean) if hp_clean else None,
       "hp_max": max(hp_clean) if hp_clean else None,
       "cast_damage": [c[2] for c in casts]})

aborted = any(s["step"] == "PROBE_ERROR" for s in ev["steps"])
expected = 4
ok = (not aborted) and len(results) == expected and all(v for _, v in results)
for n, v in results: print(("PASS " if v else "FAIL ") + n)
print("VERDICT:", "PASS" if ok else "FAIL",
      f"({len(results)}/{expected} checks ran{', ABORTED' if aborted else ''})")
(SCRATCH/"hv_npccast_evidence.json").write_text(json.dumps(ev, indent=2), encoding="utf-8")
raise SystemExit(0 if ok else 1)
