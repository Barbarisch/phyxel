"""Characters face the direction they move (user report 2026-08-19).

RED (current exe): the player's facing couples to the CAMERA — pressing S/A/D
moves the body backward/sideways while it keeps facing camera-forward
(facing-vs-bearing error ~180/~90 degrees).

GREEN (action-RPG locomotion in GameplayCameraController): WASD is camera-
relative, the body TURNS to the world move direction and walks forward —
facing error < 30 degrees for EVERY key. Idle keeps the last facing.

Method: press each of W/A/S/D for 0.6s in open town ground; bearing = the
measured XZ displacement; facing = player_state.facing_yaw right after the
press (idle keeps last facing, so post-press sampling is stable).
"""
import json, math, subprocess, time, urllib.request
from pathlib import Path

PORT = 8101
BASE = f"http://127.0.0.1:{PORT}"
RELDIR = Path.home() / "Documents/PhyxelProjects/Hearthvale/build/Release"
SCRATCH = Path(__file__).parent

def api(m, p, b=None, t=10):
    d = json.dumps(b).encode() if b is not None else None
    r = urllib.request.Request(BASE+p, data=d, method=m, headers={"Content-Type":"application/json"})
    with urllib.request.urlopen(r, timeout=t) as x: return json.loads(x.read().decode())

def player_pos():
    for e in api("GET","/api/state").get("entities", []):
        if e.get("id") == "player":
            p = e["position"]; return (p["x"], p["z"])
    return None

def facing_yaw():
    return api("POST","/api/character/player_state",{}).get("facing_yaw")

def key(k, hold):
    api("POST","/api/input/inject",{"keys":[k],"hold":hold})
    time.sleep(hold + 0.3)

results = []
def check(name, ok, detail):
    results.append((name, bool(ok)))
    print(("PASS " if ok else "FAIL ") + name, json.dumps(detail, default=str)[:180])

for f in (RELDIR/"worlds").glob("*.db*"):
    for _ in range(10):
        try: f.unlink(); break
        except PermissionError: time.sleep(1)
log = open(SCRATCH/"hv_facing.log", "w", encoding="utf-8", errors="replace")
proc = subprocess.Popen([str(RELDIR/"Hearthvale.exe"), "--test", str(PORT)],
                        cwd=str(RELDIR), stdout=log, stderr=subprocess.STDOUT)
try:
    dl = time.time()+90
    while time.time() < dl:
        try: api("GET","/api/state",t=3); break
        except Exception: time.sleep(1)
    api("POST","/api/ui/click",{"x":640,"y":384}); time.sleep(3)   # Begin
    time.sleep(5)

    for k in ("W","A","S","D"):
        p0 = player_pos()
        key(k, 0.6)
        p1 = player_pos()
        fy = facing_yaw()
        if p0 is None or p1 is None or fy is None:
            check(f"{k} facing matches bearing", False, {"err": "no data"}); continue
        dx, dz = p1[0]-p0[0], p1[1]-p0[1]
        dist = math.hypot(dx, dz)
        if dist < 0.3:
            check(f"{k} facing matches bearing", False,
                  {"err": "no displacement", "dist": round(dist,2)}); continue
        bearing = math.atan2(dx, dz)
        err = abs(math.atan2(math.sin(fy-bearing), math.cos(fy-bearing)))
        check(f"{k} facing matches bearing", math.degrees(err) < 30.0,
              {"bearing_deg": round(math.degrees(bearing),1),
               "facing_deg": round(math.degrees(fy),1),
               "err_deg": round(math.degrees(err),1), "dist": round(dist,2)})
finally:
    proc.kill(); log.close(); time.sleep(0.5)

ok = len(results) == 4 and all(v for _, v in results)
print("VERDICT:", "PASS" if ok else "FAIL", f"({len(results)}/4)")
raise SystemExit(0 if ok else 1)
