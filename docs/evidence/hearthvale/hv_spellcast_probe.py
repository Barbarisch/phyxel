"""Spellcasting beat in the SHIPPED game: the rat dies by magic.

RED (captured 2026-08-19 on the pre-cast exe, archived in the result txt):
  POST /api/rpg/combat/player_cast -> {"error":"unknown action: combat/player_cast"}

GREEN checks:
  S1: player_cast answers ok on the player's turn, cast=true (budget spent).
  S2: the engine log carries the real resolution ("Player casts 'guiding_bolt'
      at 'npc_Rat' -> N dmg" — attack roll + 4d6 through PlayerTurnController).
  S3: the encounter RESOLVES with the kill by magic only (this probe never
      clicks a melee attack), the kill XP lands, quest chain stays intact.
  S4: the cast is VISIBLE — cast-animation segments logged by the character
      (castSpell) and/or the VfxDirector fires; screenshot captured mid-cast.
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

def shot(tag):
    try:
        r = api("POST", "/api/rpg/capture_screenshot", {})
        if r.get("success"): rec(f"shot_{tag}", {"path": r["path"]}); return r["path"]
    except Exception:
        pass
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
            rec("steer_arrived", {"iter": i}); return "arrived"
        if last_dist is not None and dist >= last_dist - 0.1:
            stalled += 1
            if stalled >= 3: calibrate2(dirs); stalled = 0
        else:
            stalled = 0
        last_dist = dist
        best = max(dirs, key=lambda k2: (dirs[k2][0]*dx + dirs[k2][1]*dz)/dist)
        key(best, min(0.6, max(0.15, dist*0.09)))
    rec("steer_stuck", {"pos": player_pos()}); return "stuck"

results = []
def check(name, ok, detail):
    results.append((name, bool(ok)))
    rec(("PASS " if ok else "FAIL ") + name, detail)

for f in (RELDIR/"worlds").glob("*.db*"):
    for _ in range(10):
        try: f.unlink(); break
        except PermissionError: time.sleep(1)
log = open(SCRATCH/"hv_spellcast.log", "w", encoding="utf-8", errors="replace")
proc = subprocess.Popen([str(RELDIR/"Hearthvale.exe"), "--test", str(PORT)],
                        cwd=str(RELDIR), stdout=log, stderr=subprocess.STDOUT)
try:
    dl = time.time()+90
    while time.time() < dl:
        try: api("GET","/api/state",t=3); break
        except Exception: time.sleep(1)

    api("POST","/api/ui/click",{"x":640,"y":384}); time.sleep(3)   # Begin
    time.sleep(5)
    dirs = {"W":(-0.71,-0.71),"D":(0.71,-0.71),"S":(0.71,0.71),"A":(-0.71,0.71)}
    calibrate2(dirs)
    key("E",0.1); time.sleep(0.8); key("1",0.1); time.sleep(0.8)   # Elder: accept
    key("Enter",0.1); time.sleep(0.5); key("Enter",0.1); time.sleep(0.5)
    steer_to(dirs, 16, 12.6, tol=1.4)                              # Bram: join
    key("E",0.1); time.sleep(0.8); key("1",0.1); time.sleep(0.8)
    key("Enter",0.1); time.sleep(0.5); key("Enter",0.1); time.sleep(0.5)
    steer_to(dirs, 28, 16, stop=lambda: screen().get("scene_id")=="cellar")
    time.sleep(6)
    rec("in_cellar", {"scene": screen().get("scene_id")})

    steer_to(dirs, 12, 18, tol=1.0, stop=lambda: combat("state").get("in_combat"))
    time.sleep(1.5)
    st = combat("state")
    rec("encounter", {"in_combat": st.get("in_combat")})

    # ── The magic kill loop: cast every player turn, never click an attack ──
    cast_results = []
    first_cast_shot = None
    resolved = False
    for i in range(60):
        st = combat("state")
        if not st.get("in_combat"):
            rec("combat_resolved", {"iter": i}); resolved = True; break
        if st.get("current_entity") == "player":
            spell = "guiding_bolt" if len(cast_results) % 2 == 0 else "sacred_flame"
            r = combat("player_cast", {"spell_id": spell, "target_id": "npc_Rat"})
            cast_results.append({"spell": spell, "resp": r})
            rec("cast", {"spell": spell, "resp": r})
            if first_cast_shot is None:
                time.sleep(0.6)                 # mid cast-animation
                first_cast_shot = shot("mid_cast")
            time.sleep(2.5)                     # release frame resolves
            combat("end_turn"); time.sleep(1.0)
        else:
            time.sleep(1.0)
    ok_casts = [c for c in cast_results if c["resp"].get("cast")]
    check("S1 player_cast answers and casts (red: unknown action)",
          bool(ok_casts), {"casts": len(cast_results), "accepted": len(ok_casts)})
    check("S3a encounter resolved by magic only (no melee clicks issued)",
          resolved, {"iterations": len(cast_results)})

    time.sleep(1.5)
    sheet = api("POST","/api/rpg/sheet",{}).get("sheet", {})
    check("S3b kill XP landed on the cleric sheet",
          sheet.get("experiencePoints", 0) >= 100 and
          any("cleric" in str(c).lower() for c in sheet.get("classes", [])),
          {"xp": sheet.get("experiencePoints"), "classes": sheet.get("classes")})
except Exception as e:
    rec("PROBE_ERROR", {"error": repr(e)})
finally:
    proc.kill(); log.close(); time.sleep(0.5)

# ── Log-based observables (S2 + S4) ──────────────────────────────────────────
txt = (SCRATCH/"hv_spellcast.log").read_text(encoding="utf-8", errors="replace")
cast_lines = [l for l in txt.splitlines() if "Player casts" in l]
# Release-visible observables for the VISUAL cast: the VfxDirector emission per
# cast + the SpellAnimMapper's anim-plan selection. ("castSpell: N segment(s)"
# is LOG_DEBUG — invisible in Release; the first run FAILed on that pattern
# while the ~0.55s cast->VFX release delay was right there in the timestamps.)
vfx_lines  = [l for l in txt.splitlines() if "VfxDirector] Cast" in l]
anim_lines = [l for l in txt.splitlines() if "Selected TargetAnim" in l]
ev["cast_lines"] = cast_lines[-10:]
ev["vfx_lines"] = vfx_lines[-10:]
ev["anim_lines"] = anim_lines[-10:]
check2 = lambda n, ok, d: (results.append((n, bool(ok))), print(("PASS " if ok else "FAIL ") + n, json.dumps(d)[:200]))
check2("S2 real spell resolution in the log", bool(cast_lines), {"lines": cast_lines[-3:]})
check2("S4 cast VFX fired + anim plan selected per cast",
       len(vfx_lines) >= 1 and len(anim_lines) >= 1,
       {"vfx": vfx_lines[-2:], "anim": anim_lines[-2:]})

aborted = any(s["step"] == "PROBE_ERROR" for s in ev["steps"])
expected = 5
ok = (not aborted) and len(results) == expected and all(v for _, v in results)
for n, v in results:
    print(("PASS " if v else "FAIL ") + n)
print("VERDICT:", "PASS" if ok else "FAIL",
      f"({len(results)}/{expected} checks ran{', ABORTED' if aborted else ''})")
(SCRATCH/"hv_spellcast_evidence.json").write_text(json.dumps(ev, indent=2), encoding="utf-8")
raise SystemExit(0 if ok else 1)
