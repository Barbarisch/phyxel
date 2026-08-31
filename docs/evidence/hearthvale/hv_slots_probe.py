"""Spell slots are really spent; DC/attack derive from the sheet.

RED (pre-fix exe): /api/rpg/spellbook is an unknown action; guiding_bolt (a
LEVEL-1 spell) can be cast unlimited times in one encounter — a cleric 1 has
exactly 2 first-level slots.

GREEN:
  L1: spellbook answers, bound=true, derived DC + attack bonus.
      Cleric 1, WIS 10 in the default sheet -> DC 8+2+0 = 10, atk +2.
      (Assert against the SHEET, not a constant: DC == 8+prof+WISmod.)
  L2: slot table is the PHB cleric-1 row: two level-1 slots, nothing higher.
  L3: casting guiding_bolt DECREMENTS remaining; the 3rd cast in the same
      encounter is REFUSED with blocked="no slots" (red: it succeeded).
  L4: cantrips (sacred_flame) still cast with zero slots left.
  L5: long_rest restores the slots.
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
    print(f"[{s}] {json.dumps(d, default=str)[:240]}")

def api(m, p, b=None, t=10):
    d = json.dumps(b).encode() if b is not None else None
    r = urllib.request.Request(BASE+p, data=d, method=m, headers={"Content-Type":"application/json"})
    with urllib.request.urlopen(r, timeout=t) as x: return json.loads(x.read().decode())

def combat(a, b=None): return api("POST", f"/api/rpg/combat/{a}", b or {})
def book():            return api("POST", "/api/rpg/spellbook", {})

def ent_pos(eid):
    for e in api("GET","/api/state").get("entities", []):
        if e.get("id") == eid:
            p = e["position"]; return (p["x"], p["y"], p["z"])
    return None

def player_pos():
    p = ent_pos("player")
    return (p[0], p[2]) if p else None

def key(k, hold):
    api("POST","/api/input/inject",{"keys":[k],"hold":hold})
    time.sleep(hold + 0.25)

def screen(): return api("GET","/api/screen/state")

def slots_left(bk, level=1):
    for s in bk.get("slots", []):
        if s.get("level") == level: return s.get("remaining")
    return None

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
log = open(SCRATCH/"hv_slots.log", "w", encoding="utf-8", errors="replace")
proc = subprocess.Popen([str(RELDIR/"Hearthvale.exe"), "--test", str(PORT)],
                        cwd=str(RELDIR), stdout=log, stderr=subprocess.STDOUT)
try:
    dl = time.time()+90
    while time.time() < dl:
        try: api("GET","/api/state",t=3); break
        except Exception: time.sleep(1)

    api("POST","/api/ui/click",{"x":640,"y":384}); time.sleep(3)
    time.sleep(5)

    # ── L1/L2: the spellbook itself ──
    try:
        bk = book()
    except Exception as e:
        bk = {"error": repr(e)}
    rec("spellbook", bk)
    sheet = api("POST","/api/rpg/sheet",{}).get("sheet", {})
    # attributes.wisdom is an AbilityScore object: {base,racial,...,total,modifier}
    wis_obj = (sheet.get("attributes") or {}).get("wisdom") or {}
    wis_mod = wis_obj.get("modifier", 0) if isinstance(wis_obj, dict) else 0
    prof = 2   # PHB proficiency bonus at levels 1-4
    expect_dc  = 8 + prof + wis_mod
    expect_atk = prof + wis_mod
    check("L1 spellbook bound with sheet-derived DC + attack bonus",
          bk.get("bound") is True and bk.get("save_dc") == expect_dc
          and bk.get("spell_attack_bonus") == expect_atk,
          {"dc": bk.get("save_dc"), "expected_dc": expect_dc,
           "atk": bk.get("spell_attack_bonus"), "expected_atk": expect_atk,
           "wis_mod": wis_mod})
    check("L2 PHB cleric-1 slot table (2 first-level, none higher)",
          slots_left(bk, 1) == 2 and all(s.get("level") == 1 for s in bk.get("slots", [])),
          {"slots": bk.get("slots")})

    # ── into the fight ──
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
    rec("encounter", {"in_combat": combat("state").get("in_combat")})

    # ── L3: burn both level-1 slots, then get refused ──
    # Instrument = cure_wounds cast on SELF: a level-1 spell that consumes a
    # slot without killing anything. (guiding_bolt 4d6 one-shots the 10 HP rat
    # and ends the encounter before a second cast — measured.) One action per
    # turn, so end_turn between casts.
    casts = []
    for i in range(8):
        st = combat("state")
        if not st.get("in_combat"):
            rec("combat_over_early", {"i": i, "casts": len(casts)}); break
        if st.get("current_entity") != "player":
            time.sleep(1.0); continue
        r = combat("player_cast", {"spell_id": "cure_wounds", "target_id": "player"})
        bk = book()
        casts.append({"i": i, "resp": r, "slots_left": slots_left(bk, 1)})
        rec("cast_cure", casts[-1])
        if slots_left(bk, 1) == 0: break
        combat("end_turn"); time.sleep(1.0)
    ok_casts = [c for c in casts if c["resp"].get("cast")]
    check("L3 both slots spend, one per cast, down to zero",
          len(ok_casts) == 2 and casts[-1]["slots_left"] == 0
          and casts[0]["slots_left"] == 1,
          {"casts": casts})

    # ── L3b/L4: with ZERO slots, the gate is the authority ──
    # castBlockedReason checks spell state BEFORE turn state, so these hold
    # whether or not the encounter is still running.
    bk = book()
    by_id = {s["id"]: s for s in bk.get("spells", [])}
    r = combat("player_cast", {"spell_id": "guiding_bolt", "target_id": "npc_Rat"})
    check("L3b leveled spells refuse with 'no slots' at zero",
          r.get("cast") is False and r.get("blocked") == "no slots"
          and by_id.get("guiding_bolt", {}).get("blocked") == "no slots"
          and by_id.get("cure_wounds", {}).get("blocked") == "no slots",
          {"cast_resp": r, "book": by_id})
    check("L4 cantrips stay castable at zero slots",
          by_id.get("sacred_flame", {}).get("cantrip") is True
          and by_id.get("sacred_flame", {}).get("blocked") in ("", "not your turn",
                                                               "action spent"),
          {"sacred_flame": by_id.get("sacred_flame"), "slots_left": slots_left(bk, 1)})

    # ── L5: long rest restores ──
    api("POST","/api/rpg/long_rest",{})
    bk = book()
    check("L5 long rest restores the slots", slots_left(bk, 1) == 2,
          {"slots": bk.get("slots")})
except Exception as e:
    rec("PROBE_ERROR", {"error": repr(e)})
finally:
    proc.kill(); log.close(); time.sleep(0.5)

aborted = any(s["step"] == "PROBE_ERROR" for s in ev["steps"])
expected = 6
ok = (not aborted) and len(results) == expected and all(v for _, v in results)
for n, v in results: print(("PASS " if v else "FAIL ") + n)
print("VERDICT:", "PASS" if ok else "FAIL",
      f"({len(results)}/{expected} checks ran{', ABORTED' if aborted else ''})")
(SCRATCH/"hv_slots_evidence.json").write_text(json.dumps(ev, indent=2), encoding="utf-8")
raise SystemExit(0 if ok else 1)
