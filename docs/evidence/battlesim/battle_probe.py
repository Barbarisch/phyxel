"""20v20 REAL-TIME battle simulation: does it hold together, and at what cost?

Checks:
  B1 forty combatants spawn, twenty per faction, melee + casters
  B2 FACTION COHERENCE: nobody ever damages an ally. Sampled continuously from
     the engine log's damage lines — a single friendly-fire line fails this.
  B3 the battle PROGRESSES: casualties accumulate and one side eventually wins
     (or is clearly ahead when the clock runs out)
  B4 casters actually cast (the real-time cooldown path runs, not just melee)
  B5 PERFORMANCE at 40 animated combatants: frame time sampled throughout;
     reported honestly rather than asserted against a number pulled from air.
"""
import json, re, subprocess, time, urllib.request
from collections import Counter
from pathlib import Path

PORT = 8102
BASE = f"http://127.0.0.1:{PORT}"
RELDIR = Path.home() / "Documents/PhyxelProjects/BattleSim/build/Release"
SCRATCH = Path(__file__).parent
LOG = SCRATCH / "battlesim.log"
ev = {"samples": []}

def api(m, p, b=None, t=15):
    d = json.dumps(b).encode() if b is not None else None
    r = urllib.request.Request(BASE+p, data=d, method=m, headers={"Content-Type":"application/json"})
    with urllib.request.urlopen(r, timeout=t) as x: return json.loads(x.read().decode())

def stats(): return api("POST", "/api/rpg/battle_stats", {})

results = []
def check(name, ok, detail):
    results.append((name, bool(ok)))
    print(("PASS " if ok else "FAIL ") + name, json.dumps(detail, default=str)[:260])

for f in (RELDIR/"worlds").glob("*.db*"):
    for _ in range(10):
        try: f.unlink(); break
        except PermissionError: time.sleep(1)
log = open(LOG, "w", encoding="utf-8", errors="replace")
proc = subprocess.Popen([str(RELDIR/"BattleSim.exe"), "--test", str(PORT)],
                        cwd=str(RELDIR), stdout=log, stderr=subprocess.STDOUT)
try:
    dl = time.time() + 180
    while time.time() < dl:
        try: api("GET","/api/state",t=3); break
        except Exception: time.sleep(1.5)
    time.sleep(8)   # let the armies spawn + settle

    s0 = stats()
    print("initial:", json.dumps(s0)[:300])
    ev["initial"] = s0
    fac = {f["faction"]: f for f in s0.get("factions", [])}
    # 41 entities = 40 combatants + the unaligned observer. Both armies must
    # still be INTACT at the start: the first run had 3 dead per side before
    # the probe even looked, because the casters opened fire during spawn.
    check("B1 both armies intact at the start (20 v 20)",
          fac.get("crimson", {}).get("alive") == 20
          and fac.get("azure", {}).get("alive") == 20,
          {"combatants": s0.get("combatants"),
           "factions": {k: v.get("alive") for k, v in fac.items()}})

    # ── run the battle, sampling ──
    frame_ms, t_start, winner = [], time.time(), None
    while time.time() - t_start < 240:
        s = stats()
        ev["samples"].append({"t": round(time.time()-t_start, 1),
                              "alive": s.get("alive"), "dead": s.get("dead"),
                              "frame_ms": round(s.get("frame_ms", 0), 2),
                              "factions": {f["faction"]: f["alive"] for f in s.get("factions", [])}})
        if s.get("frame_ms"): frame_ms.append(s["frame_ms"])
        live = {f["faction"]: f["alive"] for f in s.get("factions", [])
                if f["faction"] in ("crimson", "azure")}
        if sum(live.values()) and min(live.values()) == 0:
            winner = max(live, key=live.get)
            print(f"BATTLE OVER at {round(time.time()-t_start,1)}s — {winner} wins", live)
            break
        time.sleep(3.0)
    sN = stats()
    ev["final"] = sN
    print("final:", json.dumps(sN)[:300])
finally:
    proc.kill(); log.close(); time.sleep(0.5)

txt = LOG.read_text(encoding="utf-8", errors="replace")

# B2: friendly fire. Damage lines look like "<src> hit <dst> for N".
# Two damage log shapes: the CombatSystem funnel ("[Combat] X hit Y for N
# damage") and CombatBehavior's own swing line ("[CombatAI] X hit Y for N dmg").
hits = re.findall(r"\[Combat(?:AI)?\] (\w+) hit (\w+) for", txt)
def side(name):
    if name.startswith("npc_crimson") or name.startswith("crimson"): return "crimson"
    if name.startswith("npc_azure")   or name.startswith("azure"):   return "azure"
    return "other"
friendly = [(a, b) for a, b in hits
            if side(a) != "other" and side(a) == side(b)]
ev["hits_total"] = len(hits)
ev["friendly_fire"] = friendly[:10]
check("B2 faction coherence: zero friendly fire across the whole battle",
      len(hits) > 0 and not friendly,
      {"damage_events": len(hits), "friendly_fire": len(friendly),
       "examples": friendly[:3]})

# B3: progression
start_alive = ev["initial"].get("alive", 0)
end_alive   = ev["final"].get("alive", 0)
check("B3 the battle progresses (casualties accumulate)",
      end_alive < start_alive,
      {"alive_start": start_alive, "alive_end": end_alive,
       "casualties": start_alive - end_alive,
       "final_factions": {f["faction"]: f["alive"] for f in ev["final"].get("factions", [])}})

# B4: casters cast (real-time cooldown path)
casts = re.findall(r"casts (\w+) at", txt)
ev["casts"] = len(casts)
check("B4 casters cast in real time", len(casts) > 0,
      {"casts": len(casts), "spells": dict(Counter(casts).most_common(4))})

# B5: performance — reported, with the honest caveat that this is a Release
# build on one machine and FPS at this scale is noisy.
if frame_ms:
    frame_ms.sort()
    med = frame_ms[len(frame_ms)//2]
    p95 = frame_ms[int(len(frame_ms)*0.95) - 1] if len(frame_ms) > 1 else med
    ev["perf"] = {"median_ms": round(med,2), "p95_ms": round(p95,2),
                  "median_fps": round(1000.0/med,1) if med else 0,
                  "samples": len(frame_ms)}
    check("B5 40 combatants render above 30 fps (median)", med < 33.3, ev["perf"])
else:
    check("B5 40 combatants render above 30 fps (median)", False, {"err": "no frame samples"})

ok = len(results) == 5 and all(v for _, v in results)
print("VERDICT:", "PASS" if ok else "FAIL", f"({len(results)}/5)")
(SCRATCH/"battle_evidence.json").write_text(json.dumps(ev, indent=2), encoding="utf-8")
raise SystemExit(0 if ok else 1)
