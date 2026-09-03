"""Red-before-green for the tactical AI layer: cover, chain of command, INT.

Two runs of the SAME battle, same build, same terrain:

  CONTROL   (BATTLE_TACTICS=0)  no squads, no officers, every soldier at INT 3.
                                Cover discipline and obedience both clamp to
                                zero, so nothing but "engage" is reachable.
  TREATMENT (BATTLE_TACTICS=1)  squads of 20 under officers, INT 8/11/14.

The measurement is /api/rpg/tactics — the live distribution of tactical
intents across every melee fighter. The claim "they take cover and follow
orders now" is only worth anything if the CONTROL comes back 100% engage.
Squad orders are read from the game log ("Command: squad ... -> hold").

Usage:  python tactics_probe.py control|treatment
"""
import json, re, subprocess, sys, time, urllib.request
from pathlib import Path

MODE    = sys.argv[1] if len(sys.argv) > 1 else "treatment"
PORT    = 8102
BASE    = f"http://127.0.0.1:{PORT}"
RELDIR  = Path.home() / "Documents/PhyxelProjects/BattleSim/build/Release"
SCRATCH = Path(__file__).parent

def api(m, p, b=None, t=20):
    d = json.dumps(b).encode() if b is not None else None
    r = urllib.request.Request(BASE + p, data=d, method=m,
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=t) as x:
        return json.loads(x.read().decode())

def shot(tag):
    r = api("POST", "/api/rpg/capture_screenshot", {})
    if not r.get("success"):
        return None
    dst = SCRATCH / f"tac_{MODE}_{tag}.png"
    (RELDIR / r["path"]).replace(dst)
    return dst

def set_cam(x, y, z, yaw, pitch):
    return api("POST", "/api/rpg/set_camera",
               {"x": x, "y": y, "z": z, "yaw": yaw, "pitch": pitch, "detach": True})

def mass_centre():
    st = api("GET", "/api/state")
    pts = [(e["position"]["x"], e["position"]["z"]) for e in st.get("entities", [])
           if e.get("id", "").startswith("npc_")]
    if not pts:
        return 30.0, 30.0
    return (sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts))

# Fresh world every run — a stale DB would carry the previous run's terrain.
for f in (RELDIR / "worlds").glob("*.db*"):
    for _ in range(10):
        try:
            f.unlink(); break
        except PermissionError:
            time.sleep(1)

logpath = SCRATCH / f"tac_{MODE}.log"
log = open(logpath, "w", encoding="utf-8", errors="replace")
proc = subprocess.Popen([str(RELDIR / "BattleSim.exe"), "--test", str(PORT)],
                        cwd=str(RELDIR), stdout=log, stderr=subprocess.STDOUT)

samples, shots = [], []
try:
    dl = time.time() + 240
    while time.time() < dl:
        try:
            api("GET", "/api/state", t=3); break
        except Exception:
            time.sleep(1.5)
    time.sleep(6)

    cx, cz = mass_centre()
    # High and well back: at y=20 the camera stands IN the (tall) grass and the
    # cover blocks loom into frame from the edges. y=40 / 42u back / -34 clears
    # the grass line and shows the whole field including the ruins.
    set_cam(cx, 40, cz + 42, -90, -34)
    time.sleep(1.5)

    t0 = time.time()
    plan = [("t00", 0), ("t01", 10), ("t02", 20), ("t03", 32),
            ("t04", 46), ("t05", 62), ("t06", 80)]
    for tag, when in plan:
        while time.time() - t0 < when:
            time.sleep(0.5)
        cx, cz = mass_centre()
        set_cam(cx, 40, cz + 42, -90, -34)
        time.sleep(0.4)
        tac = api("POST", "/api/rpg/tactics", {})
        bs  = api("POST", "/api/rpg/battle_stats", {})
        p = shot(tag)
        row = {"tag": tag, "t": round(time.time() - t0, 1),
               "alive": bs.get("alive"), "fps": round(bs.get("fps", 0), 1),
               "melee_alive": tac.get("melee_alive"),
               "tactical": tac.get("tactical"),
               "tactical_fraction": round(tac.get("tactical_fraction", 0), 3),
               # Cumulative decisions — the signal the instantaneous census misses.
               "cover_taken": tac.get("cover_taken"),
               "orders_obeyed": tac.get("orders_obeyed"),
               "by_faction": {f["faction"]: f["intents"] for f in tac.get("factions", [])},
               "tallies": {f["faction"]: {k: f.get(k) for k in
                                          ("cover_taken", "cover_denied",
                                           "orders_obeyed", "orders_ignored")}
                           for f in tac.get("factions", [])},
               "shot": p.name if p else None}
        samples.append(row)
        shots.append(row["shot"])
        print(json.dumps(row))
finally:
    proc.kill(); log.close()

txt = logpath.read_text(encoding="utf-8", errors="replace")
orders = re.findall(r"squad '([^']+)' \(([^)]+)\) -> (\w+)", txt)
order_counts = {}
for _, _, o in orders:
    order_counts[o] = order_counts.get(o, 0) + 1

peak_tac = max((s["tactical_fraction"] for s in samples), default=0.0)
intents_seen = sorted({i for s in samples for f in s["by_faction"].values() for i in f})

final = samples[-1] if samples else {}
result = {"mode": MODE, "samples": samples,
          "order_changes": len(orders), "orders": order_counts,
          "peak_tactical_fraction": peak_tac,
          "cover_taken_total": final.get("cover_taken"),
          "orders_obeyed_total": final.get("orders_obeyed"),
          "final_tallies": final.get("tallies"),
          "intents_seen": intents_seen}
(SCRATCH / f"tactics_{MODE}.json").write_text(json.dumps(result, indent=2),
                                              encoding="utf-8")
print("\n=== %s ===" % MODE.upper())
print("intents seen       :", intents_seen)
print("peak tactical frac :", peak_tac)
print("cover taken (cum)  :", final.get("cover_taken"))
print("orders obeyed (cum):", final.get("orders_obeyed"))
print("final tallies      :", json.dumps(final.get("tallies")))
print("squad order changes:", len(orders), order_counts)
