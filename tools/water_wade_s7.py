"""WaterCore 48 (A4) - wading, live: scenario S7 on WaterBench_Small's channel rig (2 wide x 1 deep x 12 long: x 66-77, z 14-15,
floor top y 16, one chunk). The player walks the channel's middle (z 15) with the forward key held (the camera's fps scheme, yaw 0 =
+x), sampled every ~0.1 s.

    python tools/water_wade_s7.py [--url http://127.0.0.1:8111] [--out docs/evidence/water_core_m4/a4_s7.json] [--legs dry,off,on,slept]

Legs (each prediction written in WaterCore 48.3 before the run):
  dry   - the control: no water. Speed over the middle 6 m (x 68.5 -> 74.5) = the land pace; no surface events.
  off   - 0.8 m of water, water_coupling characters:false (the red for everything the WATER does: today's binary fed no character
          to the core). The wading law still acts (it is the character's), so the speed is the law's.
  on    - the same with characters:true (shipped). Speed 0.91 +- 0.1 m/s; >= 2 cm rise 1 m ahead and fall 1 m behind (the unit
          rig measured +2.2 / -1.5 cm); the water beside the path moves; mass unchanged to 1e-3 m^3.
  slept - the channel slept into the record; the walk wakes it before the player's front reaches the water's first column.
The old hook's source (the OLD CA water at the channel, water_probe) is read too: it is what today's binary's character read here.
Nothing is dropped, nothing can rise (no debris in this rig; characters have no buoyancy, 48.1 item 6)."""
import argparse, json, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api

BOX = {"x1": 66, "y1": 16, "z1": 14, "x2": 77, "y2": 18, "z2": 15}
START = {"x": 66.6, "y": 16.0, "z": 15.0}
DEPTH = 0.8
REST = 16.0 + DEPTH
X0, X1 = 68.5, 74.5           # the middle 6 m (the speed window)
PROBES = [[70.5, 15.0], [72.5, 15.0], [70.5, 14.3], [72.5, 14.3]]   # on the path, and beside it (0.7 m off its centre line)
checks, log = [], {}


def check(name, cond, got):
    checks.append({"check": name, "ok": bool(cond), "got": got})
    print(("PASS " if cond else "FAIL ") + f"{name}: {got}", flush=True)


def ps(api):
    return api.post("/api/character/player_state", {})


def clear(api):
    for v in api.debug("water_av_list").get("volumes", []):
        api.debug("water_av_destroy", {"id": v["id"], "discard": True})


def fill(api):
    clear(api)
    r = api.debug("water_av_create", {**BOX, "cellSize": 1.0 / 9.0, "transport": "eulerian", "backend": "auto", "auto_sleep": False})
    if "error" in r: raise SystemExit(f"create: {r}")
    api.debug("place_water_box", {"x1": 66, "y1": 16, "z1": 14, "x2": 77, "y2": 16, "z2": 15, "mass": DEPTH, "target": "core"})
    api.debug("water_av_realtime", {"on": True})
    time.sleep(4.0)   # rest
    return r["volume"]["id"]


def walk(api, seconds=13.0, watch=False):
    api.post("/api/input/inject", {"release_all": True})
    api.post("/api/entity/move", {"id": "player", "position": START})
    api.post("/api/camera", {"control_scheme": "fps", "yaw": 0, "pitch": -20})
    time.sleep(1.5)   # stand: the gate's stroke ends, the water quiets
    if watch: api.debug("water_av_watch", {"points": PROBES})
    t0 = time.time()
    api.post("/api/input/inject", {"keys": ["MoveForward"], "hold": seconds})
    rip0 = api.debug("water_ripples").get("kinematic_cells_total", 0)
    samples, maxima = [], {"limbs": 0, "displacing": 0, "rippling": 0}
    while time.time() - t0 < seconds + 0.3:
        s = ps(api)
        p = s.get("position", {})
        c = api.debug("water_coupling", {}).get("characters", {})
        last = c.get("last", {})
        for k in maxima: maxima[k] = max(maxima[k], last.get(k, 0))
        samples.append({"t": time.time() - t0, "x": p.get("x"), "z": p.get("z"), "y": p.get("y"), "water": s.get("water"),
                        "volumes": len(api.debug("water_av_list").get("volumes", []))})
        if p.get("x", 0) > 76.6: break
        time.sleep(0.08)
    api.post("/api/input/inject", {"release_all": True})
    rip1 = api.debug("water_ripples").get("kinematic_cells_total", 0)
    rows = api.debug("water_av_watch", {"stop": True}).get("rows", []) if watch else []
    return {"t0": t0, "samples": samples, "maxima": maxima, "ripple_kinematic": [rip0, rip1], "watch": rows}


def speed(w):
    s = w["samples"]
    ta = next((a["t"] for a in s if a["x"] is not None and a["x"] >= X0), None)
    tb = next((a["t"] for a in s if a["x"] is not None and a["x"] >= X1), None)
    return None if ta is None or tb is None else (X1 - X0) / (tb - ta)


def surface(w):
    """per probe: the largest rise of the level while the player is 0.75-1.5 m before it, the deepest fall while it is 0.75-1.5 m
    past it. Player x at a watch row from the samples (both clocks start at the
    walk's start, within one HTTP round trip)."""
    s = [(a["t"], a["x"]) for a in w["samples"] if a["x"] is not None]
    if not s or not w["watch"]: return None
    def xat(t):
        if t <= s[0][0]: return s[0][1]
        for (ta, xa), (tb, xb) in zip(s, s[1:]):
            if ta <= t <= tb: return xa + (xb - xa) * (t - ta) / max(tb - ta, 1e-6)
        return s[-1][1]
    out = []
    for k, (px, pz) in enumerate(PROBES):
        ahead, behind = 0.0, 0.0
        for r in w["watch"]:
            lv = r[1 + 4 * k]
            if lv is None: continue
            x = xat(r[0]); d = lv - REST
            # S7's "1 m": the player's centre 0.75-1.5 m before / past the probe - a swinging foot reaches ~0.4 m past the controller
            # box, so a nearer window reads the probe column holding the foot's own crest (try2: +23.6 cm, not a wave)
            if px - 1.5 <= x <= px - 0.75: ahead = max(ahead, d)
            if px + 0.75 <= x <= px + 1.5: behind = min(behind, d)
        out.append({"probe": [px, pz], "ahead_max_m": round(ahead, 4), "behind_min_m": round(behind, 4)})
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8111")
    ap.add_argument("--out")
    ap.add_argument("--legs", default="dry,off,on,slept")
    a = ap.parse_args()
    api = Api(a.url); api.wait_status()
    legs = a.legs.split(",")
    st = ps(api)
    if not st.get("success"):
        r = api.post("/api/entity/spawn", {"type": "animated", "position": START, "id": "player"})
        log["spawn"] = r
    log["coupling_before"] = {k: v for k, v in api.debug("water_coupling", {}).items() if k in ("characters", "displace", "solids")}
    if "dry" in legs:
        clear(api)
        w = walk(api); log["dry"] = {k: v for k, v in w.items() if k != "watch"}
        v = speed(w)
        check("dry: the land pace over the middle 6 m (control)", v is not None and v > 1.5, v)
        check("dry: no wading read", all(not (s["water"] or {}).get("wading") for s in w["samples"]), "")
        log["dry_speed"] = v
    for mode in ("off", "on"):
        if mode not in legs: continue
        api.debug("water_coupling", {"characters": mode == "on"})
        fill(api)
        old = api.debug("water_probe", {"x": 72.5, "y": 16.4, "z": 15.0})
        log[f"{mode}_old_probe"] = old
        m0 = api.debug("water_av_list").get("total_mass")
        w = walk(api, watch=True)
        time.sleep(5.0)
        m1 = api.debug("water_av_list").get("total_mass")
        v = speed(w); sf = surface(w)
        log[mode] = {"speed": v, "surface": sf, "mass": [m0, m1], "maxima": w["maxima"], "ripple_kinematic": w["ripple_kinematic"],
                     "samples": w["samples"], "watch": w["watch"]}
        depths = [s["water"]["depth_m"] for s in w["samples"] if s["water"] and s["water"].get("wading")]
        check(f"{mode}: the character reads the core's depth (~{DEPTH} m)", depths and abs(sorted(depths)[len(depths) // 2] - DEPTH) < 0.1,
              sorted(depths)[len(depths) // 2] if depths else None)
        check(f"{mode}: speed over the middle 6 m 0.91 +- 0.1 m/s (the law)", v is not None and abs(v - 0.91) <= 0.1, v)
        check(f"{mode}: mass unchanged to 1e-3 m^3", m0 is not None and m1 is not None and abs(m1 - m0) < 1e-3, [m0, m1])
        on_path = [p for p in (sf or []) if p["probe"][1] == 15.0]
        ah = max((p["ahead_max_m"] for p in on_path), default=None); be = min((p["behind_min_m"] for p in on_path), default=None)
        dk = w["ripple_kinematic"][1] - w["ripple_kinematic"][0]
        if mode == "on":
            check("on: limbs fed and displacing", w["maxima"]["limbs"] > 0 and w["maxima"]["displacing"] > 0, w["maxima"])
            check("on: >= 2 cm rise 1 m ahead (on the path)", ah is not None and ah >= 0.02, ah)
            check("on: >= 2 cm fall 1 m behind (on the path)", be is not None and be <= -0.02, be)
            # the stride check is the surface beside the path (0.7 m off its centre line) moving while the wader passes - at 1/9 m cells
            # the ripple layer's remainder rule is identically 0 (its lattice IS the grid, 48.6), so the legs ring the water through
            # the solver's own displacement; distinct stride events are the owner's look call in the clip, not measured here
            side = [max(abs(p["ahead_max_m"]), abs(p["behind_min_m"])) for p in (sf or []) if p["probe"][1] != 15.0]
            check("on: the water beside the path moves (>= 5 mm) as the wader passes", side and max(side) >= 0.005, side)
        else:
            check("off (red): no limb fed", w["maxima"]["limbs"] == 0, w["maxima"])
            check("off (red): the water does not move ahead (< 5 mm)", ah is not None and ah < 0.005, ah)
            check("off (red): nor behind (< 5 mm)", be is not None and be > -0.005, be)
            side = [max(abs(p["ahead_max_m"]), abs(p["behind_min_m"])) for p in (sf or []) if p["probe"][1] != 15.0]
            check("off (red): nor beside the path (< 5 mm)", side and max(side) < 0.005, side)
    if "slept" in legs:
        api.debug("water_coupling", {"characters": True})
        vid = fill(api)
        sl = api.debug("water_av_sleep", {"id": vid, "force": True})
        log["slept_sleep"] = sl
        c0 = api.debug("water_coupling", {}).get("slept", {})
        api.post("/api/input/inject", {"release_all": True})
        api.post("/api/entity/move", {"id": "player", "position": {"x": 66.6, "y": 17.0, "z": 20.5}})   # on the slab, out of the box
        time.sleep(1.0)
        # walk along +x on the slab beside the channel? no: the channel IS the box - the walk starts in it. Instead approach it from
        # the slab end: start west of the box (x 62.6, y 17, z 15) walking +x; the channel's wall at x 65 stops the feet on the slab
        # edge above the channel's end - the wake must come before the front reaches x 66
        api.post("/api/entity/move", {"id": "player", "position": {"x": 61.0, "y": 17.0, "z": 15.0}})
        api.post("/api/camera", {"control_scheme": "fps", "yaw": 0, "pitch": -20})
        time.sleep(1.0)
        t0 = time.time()
        api.post("/api/input/inject", {"keys": ["MoveForward"], "hold": 4.0})
        woke_front = None; trace = []
        while time.time() - t0 < 4.3:
            p = ps(api).get("position", {})
            n = len(api.debug("water_av_list").get("volumes", []))
            trace.append({"t": time.time() - t0, "x": p.get("x"), "volumes": n})
            if n > 0 and woke_front is None: woke_front = p.get("x", 0) + 0.25
            time.sleep(0.05)
        api.post("/api/input/inject", {"release_all": True})
        c1 = api.debug("water_coupling", {}).get("slept", {})
        log["slept"] = {"trace": trace, "woke_front": woke_front, "before": c0, "after": c1}
        check("slept: the walker woke the channel before its front reached x 66", woke_front is not None and woke_front < 66.0, woke_front)
    fails = [c for c in checks if not c["ok"]]
    print(f"\n{len(checks) - len(fails)}/{len(checks)} passed")
    if a.out:
        pathlib.Path(a.out).write_text(json.dumps({"checks": checks, "log": log}, indent=1), encoding="utf-8")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
