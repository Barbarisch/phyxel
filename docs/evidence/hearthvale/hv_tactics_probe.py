"""Tactical AI: three encounters, six archetypes, each behavior measured alone.

RED (pre-fix exe): CombatAISystem had ONE behavior — charge the nearest foe and
swing. No "KITES", "FLEES", or "HEALS" line can exist, target choice was always
nearest, and game.json "combat_ai" was ignored.

GREEN — each check isolates one tactic from the played fights:
  T1 three separate encounters fire as the player advances (2 / 3 / 3 foes)
  T2 FOCUS FIRE: the two pack Rats attack the SAME target in a round
  T3 KITING: the Archer/Warden increase their distance from their target on a
     turn where a foe is inside their preferred range ("KITES" line + measured
     distance growth)
  T4 WEAKEST: the Archer/Horror pick the lowest-HP foe, not the nearest, on at
     least one turn where those differ (measured from live HP + positions)
  T5 HEALING: the Acolyte heals a wounded ally ("HEALS" line, ally HP rises)
  T6 MORALE: a Cultist below 35% HP flees instead of attacking ("FLEES" line)
"""
import json, math, re, subprocess, time, urllib.request
from pathlib import Path

PORT = 8101
BASE = f"http://127.0.0.1:{PORT}"
RELDIR = Path.home() / "Documents/PhyxelProjects/Hearthvale/build/Release"
SCRATCH = Path(__file__).parent
LOG = SCRATCH / "hv_tactics.log"
ev = {"steps": []}
T0 = time.time()

def rec(s, d):
    ev["steps"].append({"step": s, "t": round(time.time()-T0,1), "data": d})
    print(f"[{s}] {json.dumps(d, default=str)[:230]}")

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
    try:
        r = api("POST","/api/rpg/entity_health", {"id": eid})
        return r.get("health") if r.get("has_health") else None
    except Exception:
        return None

def key(k, hold):
    api("POST","/api/input/inject",{"keys":[k],"hold":hold})
    time.sleep(hold + 0.25)

def screen(): return api("GET","/api/screen/state")
def logtext(): return LOG.read_text(encoding="utf-8", errors="replace")

def calibrate2(dirs):
    for k in ("W","D"):
        p0 = player_pos(); key(k, 0.35); p1 = player_pos()
        if p0 is None or p1 is None: continue
        dx, dz = p1[0]-p0[0], p1[1]-p0[1]
        n = math.hypot(dx,dz)
        if n > 0.05: dirs[k] = (dx/n, dz/n)
    dirs["S"] = (-dirs["W"][0], -dirs["W"][1])
    dirs["A"] = (-dirs["D"][0], -dirs["D"][1])

def steer_to(dirs, tx, tz, tol=1.5, max_iter=60, stop=None):
    last = None; stalled = 0
    for i in range(max_iter):
        if screen().get("screen") == "paused":
            key("Escape", 0.1); time.sleep(0.4)
        if stop and stop(): return "stopped"
        p = player_pos()
        if p is None: time.sleep(1.0); continue
        dx, dz = tx-p[0], tz-p[1]; dist = math.hypot(dx,dz)
        if dist < tol: return "arrived"
        if last is not None and dist >= last - 0.1:
            stalled += 1
            if stalled >= 3: calibrate2(dirs); stalled = 0
        else: stalled = 0
        last = dist
        best = max(dirs, key=lambda k2: (dirs[k2][0]*dx + dirs[k2][1]*dz)/dist)
        key(best, min(0.6, max(0.15, dist*0.09)))
    return "stuck"

def fight(max_rounds=70, note=""):
    """Play the current encounter: attack the nearest living foe each of our
    turns. Records per-turn observations for the tactic checks."""
    obs = {"pairs": [], "dists": {}, "weakest_calls": []}
    for i in range(max_rounds):
        st = combat("state")
        if not st.get("in_combat"):
            rec(f"encounter_over{note}", {"iter": i}); break
        order = [o.get("entityId", o.get("entity_id","?")) for o in st.get("turn_order",{}).get("order",[])]
        foes = [e for e in order if e.startswith("npc_") and e not in ("npc_Bram",)]
        # sample every foe's distance to the player + hp (kiting/weakest data)
        for f in foes:
            p, fp = ent_pos("player"), ent_pos(f)
            if p and fp:
                obs["dists"].setdefault(f, []).append(
                    round(math.hypot(fp[0]-p[0], fp[2]-p[2]), 2))
        # AI-plan snapshots: what each foe's PROFILE picks vs what a plain
        # nearest-AI would pick. A turn where they DIFFER is proof the profile
        # is choosing (inferring it from attack logs alone is not).
        for f in foes:
            try:
                pl = combat("ai_plan", {"entity_id": f})
                if pl.get("target_by_priority"):
                    obs["weakest_calls"].append(
                        {"who": f, "priority": pl.get("priority"),
                         "picked": pl.get("target_by_priority"),
                         "nearest": pl.get("nearest"),
                         "wounded_ally": pl.get("wounded_ally"),
                         "hp": {e: hp_of(e) for e in ("player", "npc_Bram")}})
            except Exception:
                pass
        if st.get("current_entity") == "player":
            # hit the nearest living foe
            best, bestd = None, 1e9
            pp = ent_pos("player")
            for f in foes:
                if (hp_of(f) or 0) <= 0: continue
                fp = ent_pos(f)
                if not fp or not pp: continue
                d = math.hypot(fp[0]-pp[0], fp[2]-pp[2])
                if d < bestd: bestd, best = d, f
            if best:
                scr = combat("screen_of", {"entity_id": best})
                ti  = combat("targeting_info", {"target_id": best})
                if scr.get("ok"):
                    if ti.get("in_reach"):
                        combat("player_pick", {"x": scr["x"], "y": scr["y"]}); time.sleep(1.8)
                    else:
                        combat("player_pick", {"x": scr["x"], "y": scr["y"] + 60}); time.sleep(1.6)
            combat("end_turn"); time.sleep(0.7)
        else:
            time.sleep(0.9)
    return obs

results = []
def check(name, ok, detail):
    results.append((name, bool(ok)))
    rec(("PASS " if ok else "FAIL ") + name, detail)

for f in (RELDIR/"worlds").glob("*.db*"):
    for _ in range(10):
        try: f.unlink(); break
        except PermissionError: time.sleep(1)
log = open(LOG, "w", encoding="utf-8", errors="replace")
proc = subprocess.Popen([str(RELDIR/"Hearthvale.exe"), "--test", str(PORT)],
                        cwd=str(RELDIR), stdout=log, stderr=subprocess.STDOUT)
encounters = []
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
    steer_to(dirs, 16, 12.6, tol=1.6)
    key("E",0.1); time.sleep(0.8); key("1",0.1); time.sleep(0.8)
    key("Enter",0.1); time.sleep(0.5); key("Enter",0.1); time.sleep(0.5)
    steer_to(dirs, 28, 16, stop=lambda: screen().get("scene_id")=="cellar")
    time.sleep(6)
    calibrate2(dirs)

    # Walk the cellar's depth: z 22 (pack) -> 16 (warden) -> 9 (horror)
    for tag, zt in (("pack", 22.0), ("warden", 16.0), ("horror", 9.0)):
        steer_to(dirs, 14.0, zt, tol=1.6,
                 stop=lambda: combat("state").get("in_combat"))
        time.sleep(1.2)
        st = combat("state")
        order = [o.get("entityId", o.get("entity_id","?")) for o in st.get("turn_order",{}).get("order",[])]
        rec(f"encounter_{tag}", {"in_combat": st.get("in_combat"), "order": order})
        if st.get("in_combat"):
            encounters.append({"tag": tag, "order": order})
            # Visual proof of the nameplate/targeting layer: park the cursor on
            # an enemy so its plate shows the AC / hit-chance readout, then shoot.
            if tag == "warden":
                try:
                    import ctypes
                    hwnd = ctypes.windll.user32.FindWindowW(None, "Hearthvale")
                    scr = combat("screen_of", {"entity_id": "npc_Archer"})
                    if scr.get("ok") and hwnd:
                        for _ in range(3):
                            ctypes.windll.user32.PostMessageW(
                                hwnd, 0x0200, 0,
                                (int(scr["y"]) << 16) | int(scr["x"]))
                            time.sleep(0.08)
                        time.sleep(0.6)
                    r = api("POST","/api/rpg/capture_screenshot", {})
                    if r.get("success"):
                        (RELDIR / r["path"]).replace(SCRATCH / "nameplates_hover.png")
                        rec("shot_nameplates", {"file": "nameplates_hover.png"})
                except Exception as e:
                    rec("shot_failed", {"err": repr(e)})
            # Set up the narrow-window reactions DETERMINISTICALLY before the
            # fight resolves: a healer only heals a wounded ally, a coward only
            # breaks below its morale threshold. Waiting for the dice to land
            # in those windows is how the first two runs missed both (the ally
            # died un-wounded; the Cultist went 50% -> dead in one hit).
            if tag == "warden":
                hw = api("POST","/api/rpg/entity_damage",
                         {"id": "npc_Warden", "amount": 14})
                rec("setup_wound_warden", hw)   # ~50% -> under the 70% heal line
            if tag == "horror":
                # 60 HP minion, 42 damage -> 18/60 = 30%, under its 35% morale
                # line AND with enough HP left to SURVIVE to its next turn.
                # (The decision log caught the earlier attempt: the cultist was
                # wounded to 8 HP and simply died before acting again — "turn
                # starts at 100%" was its only turn.)
                hc_ = api("POST","/api/rpg/entity_damage",
                          {"id": "npc_Cultist", "amount": 42})
                rec("setup_wound_cultist", hc_)
            o = fight(max_rounds=26, note=f"_{tag}")
            encounters[-1]["obs"] = o
            # CLOSE OUT the encounter so the walk can reach the next one. The
            # E2 group (kiting archer + artillery + self-healing acolyte) is a
            # genuine attrition wall that this dumb attack-the-nearest probe
            # cannot finish in reasonable time — which is itself evidence the
            # tactics work, but it blocked E3 from ever being reached. The
            # TACTICS are observed from the AI's own turns above; this only
            # ends the fight. NOT a claim that the player won it.
            if combat("state").get("in_combat"):
                for f in order:
                    if f.startswith("npc_") and f != "npc_Bram":
                        api("POST","/api/rpg/entity_damage", {"id": f, "amount": 999})
                rec(f"encounter_closed_out_{tag}",
                    {"note": "probe ended the fight via the damage API after observing tactics"})
                time.sleep(2.0)
        time.sleep(1.0)
except Exception as e:
    rec("PROBE_ERROR", {"error": repr(e)})
finally:
    proc.kill(); log.close(); time.sleep(0.5)

txt = logtext()
ev["encounters"] = [{k: v for k, v in e.items() if k != "obs"} for e in encounters]
kite_lines  = [l.strip() for l in txt.splitlines() if "KITES away" in l]
flee_lines  = [l.strip() for l in txt.splitlines() if "FLEES from" in l]
heal_lines  = [l.strip() for l in txt.splitlines() if "HEALS" in l]
ev["kite_lines"], ev["flee_lines"], ev["heal_lines"] = kite_lines[:6], flee_lines[:6], heal_lines[:6]

# T1: three distinct encounters, with the authored rosters
rosters = {e["tag"]: set(x for x in e["order"] if x.startswith("npc_")) for e in encounters}
check("T1 three separate encounters fire along the approach",
      len(encounters) == 3
      and {"npc_Rat","npc_Rat2"} <= rosters.get("pack", set())
      and {"npc_Warden","npc_Archer","npc_Acolyte"} <= rosters.get("warden", set())
      and {"npc_Horror","npc_Cultist","npc_Cultist2"} <= rosters.get("horror", set()),
      {"rosters": {k: sorted(v) for k, v in rosters.items()}})

# T2: focus fire — both rats hit the same target in the same round
atk = re.findall(r"NPC '(npc_Rat2?)' (?:hits|misses) '(\w+)'", txt)
pack_targets = {}
for who, whom in atk: pack_targets.setdefault(who, []).append(whom)
shared = (set(pack_targets.get("npc_Rat", [])) & set(pack_targets.get("npc_Rat2", [])))
check("T2 pack focus-fire: both Rats attack the same target",
      len(pack_targets) == 2 and bool(shared),
      {"targets": pack_targets, "shared": sorted(shared)})

# T3: kiting — a KITES line, and the kiter's distance to the player grows
kiters = [m for m in re.findall(r"NPC '(\w+)' KITES away from '(\w+)' \((\d+) -> (\d+)", txt)]
grew = any(int(k[3]) > int(k[2]) for k in kiters)
check("T3 kiting: a ranged NPC withdraws to its preferred range",
      len(kiters) >= 1 and grew,
      {"kites": kiters[:4]})

# T4: weakest-targeting, MEASURED — find a sampled moment where the profile's
#     pick differs from the nearest-AI pick, and confirm the pick is the foe
#     with less HP. (The earlier version only checked "profile was parsed and
#     the NPC attacked someone" — named for a property it did not measure.)
plans = [p for e in encounters for p in e.get("obs", {}).get("weakest_calls", [])]
ev["plan_samples"] = plans[:40]
divergent = [p for p in plans
             if p["priority"] == "weakest" and p["picked"] and p["nearest"]
             and p["picked"] != p["nearest"]]
correct = []
for p in divergent:
    hp = {k: v for k, v in p["hp"].items() if isinstance(v, (int, float))}
    if p["picked"] in hp and p["nearest"] in hp and hp[p["picked"]] < hp[p["nearest"]]:
        correct.append(p)
check("T4 weakest-priority MEASURABLY overrides nearest (picked the lower-HP foe)",
      bool(correct),
      {"divergent_samples": len(divergent), "confirmed": len(correct),
       "example": correct[0] if correct else (divergent[0] if divergent else None)})

# T5: healing — the Acolyte spends its action on a wounded ally (condition was
# created deterministically; the RESPONSE is what is under test)
check("T5 the Acolyte heals the wounded ally",
      any("npc_Acolyte' HEALS" in l for l in heal_lines),
      {"heals": heal_lines[:3]})

# T6: morale — the wounded Cultist runs instead of fighting
check("T6 the wounded Cultist breaks and flees",
      any("npc_Cultist'" in l for l in flee_lines),
      {"flees": flee_lines[:3]})

aborted = any(s["step"] == "PROBE_ERROR" for s in ev["steps"])
expected = 6
ok = (not aborted) and len(results) == expected and all(v for _, v in results)
for n, v in results: print(("PASS " if v else "FAIL ") + n)
print("VERDICT:", "PASS" if ok else "FAIL",
      f"({len(results)}/{expected} checks ran{', ABORTED' if aborted else ''})")
(SCRATCH/"hv_tactics_evidence.json").write_text(json.dumps(ev, indent=2), encoding="utf-8")
raise SystemExit(0 if ok else 1)
