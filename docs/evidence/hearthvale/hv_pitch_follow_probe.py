"""L4 for the mouseCaptured-leak fix (unmasked) + companion FOLLOW mode.

RED context: unit test GameplayCameraControllerTest.LookFrozenWhileNotDriving
reproduced the field defect on the pre-fix engine (pitch racked to +89 by
free-cursor moves while driveCharacter=false). The scaffold's snapshot/restore
mask is REMOVED in this build — nothing hides a regression here.

CAM checks (with a live positive control for the input channel):
  CAM0: synthetic WM_MOUSEMOVE sweep during EXPLORATION changes the camera
        (proves posted mouse messages reach GLFW->InputManager in this build;
        without this control, "combat pitch unchanged" could mean "messages
        never arrived").
  CAM1: the same sweep during COMBAT does NOT move the camera look.
  CAM2: after combat resolves, the restored camera pose equals the pre-combat
        pose (no mask in the build - this is the root fix carrying it).

FOLLOW checks (Bram respawns in the cellar with NPCBehaviorType::Follow):
  F1: while the player walks, Bram's distance stays bounded/converges (<8u).
  F2: when the player stands still, Bram settles inside the ~3u deadzone
      (+hysteresis) and stops (displacement < 0.5u over 2s).
"""
import ctypes, json, math, subprocess, time, urllib.request
from pathlib import Path

PORT = 8101
BASE = f"http://127.0.0.1:{PORT}"
RELDIR = Path.home() / "Documents/PhyxelProjects/Hearthvale/build/Release"
SCRATCH = Path(__file__).parent
ev = {"steps": []}
T0 = time.time()

def rec(s, d):
    ev["steps"].append({"step": s, "t": round(time.time()-T0,1), "data": d})
    print(f"[{s}] {json.dumps(d, default=str)[:260]}")

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

def cam():
    c = api("GET","/api/state").get("camera", {})
    return {"yaw": c.get("yaw"), "pitch": c.get("pitch"),
            "pos": c.get("position", {})}

def key(k, hold):
    api("POST","/api/input/inject",{"keys":[k],"hold":hold})
    time.sleep(hold + 0.25)

def screen(): return api("GET","/api/screen/state")

# ── Synthetic mouse traffic: PostMessage WM_MOUSEMOVE over the client area.
# Does NOT move the user's real cursor. GLFW's cursor-pos callback is
# WM_MOUSEMOVE-driven in normal cursor mode, and --test mode never disables
# the cursor (no raw-input path).
user32 = ctypes.windll.user32
WM_MOUSEMOVE = 0x0200

def find_hwnd():
    return user32.FindWindowW(None, "Hearthvale")

def mouse_sweep(hwnd, n=40):
    # Diagonal sweeps across the 1280x720 client area, the same shape as
    # click-targeting traffic.
    for i in range(n):
        x = 100 + (1080 * i) // n
        y = 650 - (560 * i) // n
        user32.PostMessageW(hwnd, WM_MOUSEMOVE, 0, (y << 16) | x)
        time.sleep(0.01)
    time.sleep(0.4)

def restore_look(hwnd, target=-22.0):
    # CAM0 deliberately scrambles pitch to the +89 clamp; drive it back down
    # with downward mouse traffic so the rest of the run (and CAM2's camera-
    # shape guard) starts from a sane exploration pose.
    for _ in range(40):
        p = cam().get("pitch")
        if p is not None and p <= target:
            break
        for i in range(25):
            y = 100 + i * 20                      # cursor moving DOWN = pitch down
            user32.PostMessageW(hwnd, WM_MOUSEMOVE, 0, (y << 16) | 640)
            time.sleep(0.005)
        time.sleep(0.15)
    rec("look_restored", cam())

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
            if stalled >= 3:
                calibrate2(dirs); stalled = 0
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

# ── Launch fresh ────────────────────────────────────────────────────────────
for f in (RELDIR/"worlds").glob("*.db*"):
    for _ in range(10):
        try: f.unlink(); break
        except PermissionError: time.sleep(1)
log = open(SCRATCH/"hv_pitch_follow.log", "w", encoding="utf-8", errors="replace")
proc = subprocess.Popen([str(RELDIR/"Hearthvale.exe"), "--test", str(PORT)],
                        cwd=str(RELDIR), stdout=log, stderr=subprocess.STDOUT)
try:
    dl = time.time()+90
    while time.time() < dl:
        try: api("GET","/api/state",t=3); break
        except Exception: time.sleep(1)
    hwnd = find_hwnd()
    rec("hwnd", {"hwnd": hwnd})

    api("POST","/api/ui/click",{"x":640,"y":384}); time.sleep(3)   # Begin
    time.sleep(5)   # first-load interact settle
    dirs = {"W":(-0.71,-0.71),"D":(0.71,-0.71),"S":(0.71,0.71),"A":(-0.71,0.71)}
    calibrate2(dirs)

    # ── CAM0: positive control — sweep during exploration MUST move the look
    c0 = cam(); mouse_sweep(hwnd); c1 = cam()
    dyaw = abs((c1["yaw"] or 0) - (c0["yaw"] or 0))
    dpitch = abs((c1["pitch"] or 0) - (c0["pitch"] or 0))
    check("CAM0 exploration sweep moves look (channel alive)",
          dyaw + dpitch > 2.0, {"before": c0, "after": c1})
    restore_look(hwnd)

    # accept quest, recruit Bram (same script as the combat probe)
    key("E",0.1); time.sleep(0.8); key("1",0.1); time.sleep(0.8)
    key("Enter",0.1); time.sleep(0.5); key("Enter",0.1); time.sleep(0.5)
    steer_to(dirs, 16, 12.6, tol=1.4)
    key("E",0.1); time.sleep(0.8); key("1",0.1); time.sleep(0.8)
    key("Enter",0.1); time.sleep(0.5); key("Enter",0.1); time.sleep(0.5)
    rec("recruited", {})

    steer_to(dirs, 28, 16, stop=lambda: screen().get("scene_id")=="cellar")
    time.sleep(6)   # scene-load settle (input dead-zone race after first load)
    rec("in_cellar", {"scene": screen().get("scene_id"),
                      "player": player_pos(), "bram": ent_pos("npc_Bram")})

    # CAM2's PRE snapshot — taken NOW, before any walking can drift us into the
    # guard region (first run: the follow walk wandered close enough that
    # combat had already begun by the "pre" snapshot, making CAM2 vacuous).
    pre = cam()
    rec("cam_pre_combat", pre)
    assert not combat("state").get("in_combat"), "combat started before pre-snapshot"

    # ── F1: walk away from Bram, sampling his distance — bounded/converging.
    # The guard_post trigger region is a WALL at x 11-13 spanning z 10-28, and
    # the cellar entry is x~10 — the only combat-safe walk is NORTH along x<=10.
    # Direction maps have burned two runs (calibration races the scene-load
    # input dead-zone; one bad press crosses into the trigger wall) — so steer
    # by MEASURED per-press feedback instead: press, measure the displacement,
    # remember what each key actually does, undo any eastward drift at once.
    samples = []
    def sample():
        p, b = player_pos(), ent_pos("npc_Bram")
        if p and b:
            samples.append(round(math.hypot(b[0]-p[0], b[2]-p[1]), 2))
    sample()
    # Micro-press calibration: 0.12s taps (~0.3u) measured and immediately
    # undone — worst-case eastward transient stays well short of the x=11 wall
    # (a full 0.4s probing press reached x=11.05 and started combat last run).
    opposite = {"W":"S","S":"W","A":"D","D":"A"}
    measured = {}
    for k2 in ("W","A","S","D"):
        p0 = player_pos()
        key(k2, 0.12)
        p1 = player_pos()
        key(opposite[k2], 0.12)   # undo
        if p0 and p1:
            dx, dz = p1[0]-p0[0], p1[1]-p0[1]
            n = math.hypot(dx, dz)
            if n > 0.05: measured[k2] = (dx/n, dz/n)
    rec("micro_calibration", {k2: [round(v,2) for v in d] for k2, d in measured.items()})
    assert measured, "no key produced measurable movement"
    west = min(measured, key=lambda k2: measured[k2][0])   # most-negative dx
    assert measured[west][0] < -0.5, f"no clearly-westward key: {measured}"
    for _ in range(5):
        key(west, 0.5)
        sample()
    rec("follow_walk_samples", {"dist_series": samples, "west_key": west})
    assert not combat("state").get("in_combat"), "follow walk crossed the guard region"
    check("F1 follow stays bounded while walking",
          samples and samples[-1] < 8.0 and max(samples) < 20.0,
          {"series": samples})

    # ── F2: stand still — Bram settles into the deadzone and STOPS
    time.sleep(3.0)
    b0 = ent_pos("npc_Bram"); p = player_pos(); time.sleep(2.0)
    b1 = ent_pos("npc_Bram")
    settle = math.hypot(b1[0]-b0[0], b1[2]-b0[2]) if (b0 and b1) else None
    hold_dist = math.hypot(b1[0]-p[0], b1[2]-p[1]) if (b1 and p) else None
    check("F2 follow settles in the deadzone and stops",
          settle is not None and settle < 0.5 and hold_dist is not None and hold_dist < 6.0,
          {"displacement_2s": settle, "hold_dist": hold_dist})

    # ── CAM1+CAM2: combat round trip with cursor traffic, NO mask in build
    steer_to(dirs, 12, 18, tol=1.0, stop=lambda: combat("state").get("in_combat"))
    time.sleep(1.5)
    st = combat("state"); rec("encounter", {"in_combat": st.get("in_combat"),
        "order": [o.get("entityId", o.get("entity_id","?")) for o in st.get("turn_order",{}).get("order",[])]})

    ct0 = cam(); mouse_sweep(hwnd); mouse_sweep(hwnd); ct1 = cam()
    dyaw = abs((ct1["yaw"] or 0) - (ct0["yaw"] or 0))
    dpitch = abs((ct1["pitch"] or 0) - (ct0["pitch"] or 0))
    check("CAM1 combat sweep does NOT move the look",
          dyaw + dpitch < 1.0, {"before": ct0, "after": ct1})

    # fight to the kill (click-to-act core from the combat probe)
    resolved = False
    for i in range(60):
        st = combat("state")
        if not st.get("in_combat"):
            rec("combat_resolved", {"iter": i}); resolved = True; break
        if st.get("current_entity") == "player":
            ti = combat("targeting_info", {"target_id": "npc_Rat"})
            scr = combat("screen_of", {"entity_id": "npc_Rat"})
            if ti.get("in_reach") and scr.get("ok"):
                combat("player_pick", {"x": scr["x"], "y": scr["y"]}); time.sleep(2.5)
            elif scr.get("ok"):
                combat("player_pick", {"x": scr["x"], "y": scr["y"] + 60}); time.sleep(2.0)
            else:
                rp = ent_pos("npc_Rat")
                if rp: combat("player_move", {"x": rp[0], "y": rp[1], "z": rp[2]})
                time.sleep(2.0)
            combat("end_turn"); time.sleep(1.0)
        else:
            time.sleep(1.0)
    time.sleep(2.0)
    post = cam()
    dyaw = abs((post["yaw"] or 0) - (pre["yaw"] or 0))
    dpitch = abs((post["pitch"] or 0) - (pre["pitch"] or 0))
    cam_y = post["pos"].get("y", 0); pp = ent_pos("player")
    above = (cam_y - pp[1]) if pp else None
    # Vacuity guards (first run's lesson): the encounter must have RESOLVED and
    # the camera must be back in third-person SHAPE (a few units above, not the
    # 25+-above overhead) — a stalled encounter can no longer fake this pass.
    check("CAM2 post-combat look equals pre-combat (no mask)",
          resolved and not combat("state").get("in_combat")
          and dyaw < 3.0 and dpitch < 3.0
          and above is not None and 0.5 < above < 15.0,
          {"resolved": resolved, "pre": pre, "post": post, "cam_above_player": above})

except Exception as e:
    rec("PROBE_ERROR", {"error": repr(e)})
finally:
    proc.kill(); log.close(); time.sleep(0.5)

aborted = any(s["step"] == "PROBE_ERROR" for s in ev["steps"])
expected = 5   # CAM0, F1, F2, CAM1, CAM2 — a truncated run can never pass
ok = (not aborted) and len(results) == expected and all(v for _, v in results)
for n, v in results:
    print(("PASS " if v else "FAIL ") + n)
print("VERDICT:", "PASS" if ok else "FAIL",
      f"({len(results)}/{expected} checks ran{', ABORTED' if aborted else ''})")
(SCRATCH/"hv_pitch_follow_evidence.json").write_text(json.dumps(ev, indent=2), encoding="utf-8")
raise SystemExit(0 if ok else 1)
