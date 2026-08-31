"""Show the battle sim WITH health bars — close enough to actually read them.

Two things must both be true before this counts:
  1. the engine says it QUEUED plates ("rt nameplates: queued=N" in the log)
  2. the frame shows them

(1) without (2) is the camera-relative-view bug all over again; (2) without (1)
would mean I'm reading something else. So this asserts on the log line AND
saves frames at a range where names render (inside kRtNameRange = 20u).
"""
import json, re, subprocess, time, urllib.request
from pathlib import Path

PORT = 8102
BASE = f"http://127.0.0.1:{PORT}"
RELDIR = Path.home() / "Documents/PhyxelProjects/BattleSim/build/Release"
SCRATCH = Path(__file__).parent

def api(m, p, b=None, t=25):
    d = json.dumps(b).encode() if b is not None else None
    r = urllib.request.Request(BASE + p, data=d, method=m,
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=t) as x:
        return json.loads(x.read().decode())

def shot(tag):
    r = api("POST", "/api/rpg/capture_screenshot", {})
    if not r.get("success"):
        return None
    dst = SCRATCH / f"plates_{tag}.png"
    (RELDIR / r["path"]).replace(dst)
    return dst

def set_cam(x, y, z, yaw, pitch):
    return api("POST", "/api/rpg/set_camera",
               {"x": x, "y": y, "z": z, "yaw": yaw, "pitch": pitch, "detach": True})

def mass_centre():
    st = api("GET", "/api/state")
    pts = [(e["position"]["x"], e["position"]["y"], e["position"]["z"])
           for e in st.get("entities", []) if e.get("id", "").startswith("npc_")]
    if not pts:
        return 48.0, 17.0, 32.0
    n = len(pts)
    return (sum(p[0] for p in pts) / n, sum(p[1] for p in pts) / n,
            sum(p[2] for p in pts) / n)

for f in (RELDIR / "worlds").glob("*.db*"):
    for _ in range(10):
        try:
            f.unlink(); break
        except PermissionError:
            time.sleep(1)

logpath = SCRATCH / "plates.log"
log = open(logpath, "w", encoding="utf-8", errors="replace")
proc = subprocess.Popen([str(RELDIR / "BattleSim.exe"), "--test", str(PORT)],
                        cwd=str(RELDIR), stdout=log, stderr=subprocess.STDOUT)
shots = []
try:
    dl = time.time() + 240
    while time.time() < dl:
        try:
            api("GET", "/api/state", t=3); break
        except Exception:
            time.sleep(1.5)
    time.sleep(8)

    # Let the fight develop so health bars are actually PARTIAL — full-green
    # bars everywhere would not show that they track damage.
    time.sleep(18)

    cx, cy, cz = mass_centre()
    print(f"mass centre: ({cx:.1f}, {cy:.1f}, {cz:.1f})")

    # Close, low, looking into the melee: inside kRtNameRange so NAMES draw too.
    for tag, (dx, dy, dz, yaw, pitch) in {
        "close":  (0.0,  8.0, 15.0, -90, -22),
        "closer": (6.0,  6.0, 11.0, -115, -18),
        "wide":   (0.0, 18.0, 30.0, -90, -30),
    }.items():
        set_cam(cx + dx, cy + dy, cz + dz, yaw, pitch)
        time.sleep(1.2)
        p = shot(tag)
        shots.append(p.name if p else None)
        print("shot:", p.name if p else "FAILED")
        time.sleep(2.0)
finally:
    proc.kill(); log.close()

txt = logpath.read_text(encoding="utf-8", errors="replace")
q = [int(m) for m in re.findall(r"rt nameplates: queued=(\d+)", txt)]
print("\n=== NAMEPLATES ===")
print("queued samples :", q[:10])
print("max queued     :", max(q) if q else 0)
print("shots          :", shots)
if not q:
    print("VERDICT: FAIL — engine never logged a queued plate")
elif max(q) == 0:
    print("VERDICT: FAIL — plates queued but count was 0 (all culled)")
else:
    print(f"VERDICT: engine queued up to {max(q)} plates; confirm visually in the shots")
