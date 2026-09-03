"""Capture the BG3-style tactical camera in a live fight (and its controls).

Walks to the first encounter, then captures:
  cam_tactical_default.png   the entry angle (~52 deg, perspective, close)
  cam_tactical_orbited.png   after Q-orbit  (yaw changed, same fight)
  cam_tactical_zoomed.png    after wheel zoom-in (distance reduced)
  cam_tactical_steep.png     after R (steeper angle, still not flat)
Reports the camera pose for each so the change is measured, not just seen.
"""
import ctypes, json, math, subprocess, time, urllib.request
from pathlib import Path

PORT = 8101
BASE = f"http://127.0.0.1:{PORT}"
RELDIR = Path.home() / "Documents/PhyxelProjects/Hearthvale/build/Release"
SCRATCH = Path(__file__).parent
user32 = ctypes.windll.user32
WM_MOUSEWHEEL = 0x020A

def api(m, p, b=None, t=10):
    d = json.dumps(b).encode() if b is not None else None
    r = urllib.request.Request(BASE+p, data=d, method=m, headers={"Content-Type":"application/json"})
    with urllib.request.urlopen(r, timeout=t) as x: return json.loads(x.read().decode())

def combat(a, b=None): return api("POST", f"/api/rpg/combat/{a}", b or {})

def player_pos():
    for e in api("GET","/api/state").get("entities", []):
        if e.get("id") == "player":
            p = e["position"]; return (p["x"], p["z"])
    return None

def cam():
    c = api("GET","/api/state").get("camera", {})
    p = c.get("position", {})
    return {"yaw": round(c.get("yaw", 0), 1), "pitch": round(c.get("pitch", 0), 1),
            "pos": {k: round(v, 1) for k, v in p.items()}}

def cam_vs_player():
    c = api("GET","/api/state").get("camera", {})
    cp = c.get("position", {})
    pp = None
    for e in api("GET","/api/state").get("entities", []):
        if e.get("id") == "player": pp = e["position"]
    if not pp: return {}
    dy = cp.get("y",0) - pp["y"]
    dh = math.hypot(cp.get("x",0)-pp["x"], cp.get("z",0)-pp["z"])
    return {"height_above": round(dy,1), "ground_dist": round(dh,1),
            "boom": round(math.hypot(dy, dh),1),
            "elevation_deg": round(math.degrees(math.atan2(dy, max(dh,0.01))),1)}

def key(k, hold):
    api("POST","/api/input/inject",{"keys":[k],"hold":hold})
    time.sleep(hold + 0.25)

def screen(): return api("GET","/api/screen/state")

def shot(tag):
    r = api("POST","/api/rpg/capture_screenshot", {})
    if r.get("success"):
        dst = SCRATCH / f"{tag}.png"
        (RELDIR / r["path"]).replace(dst)
        print(f"[shot] {tag}: {dst.name}  cam={cam()}  geom={cam_vs_player()}")
        return dst
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

for f in (RELDIR/"worlds").glob("*.db*"):
    for _ in range(10):
        try: f.unlink(); break
        except PermissionError: time.sleep(1)
log = open(SCRATCH/"hv_camera.log", "w", encoding="utf-8", errors="replace")
proc = subprocess.Popen([str(RELDIR/"Hearthvale.exe"), "--test", str(PORT)],
                        cwd=str(RELDIR), stdout=log, stderr=subprocess.STDOUT)
poses = {}
try:
    dl = time.time()+90
    while time.time() < dl:
        try: api("GET","/api/state",t=3); break
        except Exception: time.sleep(1)
    hwnd = user32.FindWindowW(None, "Hearthvale")

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

    print("exploration camera:", cam(), cam_vs_player())
    poses["exploration"] = {"cam": cam(), "geom": cam_vs_player()}

    steer_to(dirs, 14.0, 22.0, tol=1.6, stop=lambda: combat("state").get("in_combat"))
    time.sleep(2.0)
    print("in_combat:", combat("state").get("in_combat"))
    poses["tactical_default"] = {"cam": cam(), "geom": cam_vs_player()}
    shot("cam_tactical_default")

    for _ in range(6): key("Q", 0.25)          # orbit
    poses["orbited"] = {"cam": cam(), "geom": cam_vs_player()}
    shot("cam_tactical_orbited")

    for _ in range(5):                          # zoom in (wheel up)
        user32.PostMessageW(hwnd, WM_MOUSEWHEEL, 120 << 16, (400 << 16) | 640)
        time.sleep(0.15)
    time.sleep(0.5)
    poses["zoomed"] = {"cam": cam(), "geom": cam_vs_player()}
    shot("cam_tactical_zoomed")

    for _ in range(6): key("R", 0.25)          # steeper
    poses["steep"] = {"cam": cam(), "geom": cam_vs_player()}
    shot("cam_tactical_steep")

    # ── ACTION FRAMING: play turns and capture a frame while an ENEMY acts.
    # The camera should have moved off the player toward the actor (the boom
    # widens with their separation), so both stay in frame.
    for _ in range(8): key("F", 0.25)          # back to a normal angle
    combat("end_turn"); time.sleep(0.6)
    for i in range(30):
        st = combat("state")
        if not st.get("in_combat"): break
        cur = st.get("current_entity")
        if cur and cur != "player":
            time.sleep(0.5)
            g = cam_vs_player()
            actor = None
            for e in api("GET","/api/state").get("entities", []):
                if e.get("id") == cur: actor = e["position"]
            poses["enemy_turn_framing"] = {
                "acting": cur, "cam": cam(), "geom": g,
                "cam_to_actor": (round(math.hypot(
                    api("GET","/api/state")["camera"]["position"]["x"] - actor["x"],
                    api("GET","/api/state")["camera"]["position"]["z"] - actor["z"]), 1)
                    if actor else None)}
            print("enemy turn:", cur, poses["enemy_turn_framing"])
            shot("cam_enemy_turn")
            break
        if cur == "player":
            combat("end_turn")
        time.sleep(0.8)
finally:
    proc.kill(); log.close()

print(json.dumps(poses, indent=2))
(SCRATCH/"hv_camera_poses.json").write_text(json.dumps(poses, indent=2), encoding="utf-8")
