"""WaterCore 30.2 (track C, step 1) - the realistic play scene: the player WADES across a lake (the first-person camera at the
player's eye) while wood is thrown into the water ahead of them; the whole frame (fps, CPU frame, GPU frame, worst frame) and the
water's CPU parts / GPU per call, by tools/water_a3_cost.py's window. Written 2026-11-18 for §57.

WaterBench_Creek (port 8114, Release, flat and empty). The rig (world edits, the bench world backed up and restored by hand):
a sand bed x 4-63, z 2-61 at y 15 with a stone kerb 2 high round it (water_record_lake_live.py's bed), the lake written straight
into the record (water_av_record_lake, 0.8 m: wading depth, as A4's channel). Every engine control at its shipped default except
water_av_realtime on (as every live water rig) and the debris lifetime 20 s (so a window starts with no wood from the last one).

The walk: the player put at (X_START, 16, Z_WALK), yaw 0 (+X), the forward key held; each window starts 1 s into the stride and
lasts --seconds. The throws: one 1/3 m Wood piece every THROW_S, launched from 1.6 m above the player, 3-4 m/s forward and 2 m/s up,
alternately 1.2 m/s to either side (lands ~2-4 m ahead of the wader, in the water they walk into).

Windows (the dry control first - the lake is not yet recorded; then lake windows interleaved A B B A):
  dry_walk          the same walk on the dry bed (no water anywhere): the control
  lake_walk         wading the lake
  lake_walk_debris  wading the lake, wood thrown in
  worst_awake       the worst case on this lake: the whole 60 x 60 m lake taken into one awake volume (seed spans) and a stone
                    at its centre, the player standing in it (30.2's worst case is the 2 m churning lake - this is that lake
                    at 0.8 m: stated, not the same rig)
Each window reports the player's travel (verifies the walk happened) and the live debris count.

--churn: 30.2's WORST case instead - the 60 x 60 m lake 2 m deep (the 27.24 / 50.3 lake) and a VISIBLE stone at its centre
(radius 2 m, strength 8: 50.6 - the 0.5 m / strength-4 stone makes no visible wave), the player off the lake, a free camera
8 m up (water_record_lake_live.py's). Two cases, each with windows 0-8 s, 20-28 s and 44-52 s after the stone:
  record   the lake in the record (as play has it since 51): the stone wakes a patch round it
  volume   the whole lake one awake volume (seed spans) - the pre-51 rig of 27.24 / 50.3 / 50.6, for comparison with them

    python tools/water_play_scene.py [--url http://127.0.0.1:8114] [--seconds 12] [--out docs/evidence/water_core_m4/s57_play_scene.json]
"""
import argparse, json, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, world_job
from water_a3_cost import window

X0, Z0, TOP, W = 4, 2, 16, 60
DEPTH = 0.8
X_START, Z_WALK = 7.0, 32.0
THROW_S = 1.0
LIFE = 20.0


def ps(api):
    return api.post("/api/character/player_state", {})


def ppos(api):
    s = ps(api)
    p = s.get("position") or s.get("player", {}).get("position") or {}
    return [round(p.get("x", 0.0), 2), round(p.get("y", 0.0), 2), round(p.get("z", 0.0), 2)]


def lay_bed(api):
    world_job(api, "/api/world/clear", {"x1": X0, "y1": TOP - 1, "z1": Z0, "x2": X0 + W - 1, "y2": TOP + 3, "z2": Z0 + W - 1})
    world_job(api, "/api/world/fill", {"x1": X0, "y1": TOP - 1, "z1": Z0, "x2": X0 + W - 1, "y2": TOP - 1, "z2": Z0 + W - 1}, material="Sand")
    for b in ((X0 - 1, Z0 - 1, X0 - 1, Z0 + W), (X0 + W, Z0 - 1, X0 + W, Z0 + W), (X0 - 1, Z0 - 1, X0 + W, Z0 - 1), (X0 - 1, Z0 + W, X0 + W, Z0 + W)):
        world_job(api, "/api/world/fill", {"x1": b[0], "y1": TOP, "z1": b[1], "x2": b[2], "y2": TOP + 1, "z2": b[3]}, material="StoneBricks")


def destroy_all(api):
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"], "discard": False})


def clear_record(api):
    """The bench keeps what earlier rigs recorded (a 2 m lake from 51.2 was still here): take the box's stored water into a volume
    and discard it, then prove the record is empty there (an unseeded create over recorded water is refused, 27.18)."""
    api.post("/api/entity/move", {"id": "player", "position": {"x": 2.0, "y": TOP + 1.2, "z": 0.0}})
    box = {"x1": 0, "y1": TOP - 2, "z1": 0, "x2": X0 + W + 3, "y2": TOP + 6, "z2": Z0 + W + 3}
    r = api.debug("water_av_create", {**box, "cellSize": 1.0 / 3.0, "backend": "cpu", "auto_sleep": False, "seed": "spans"})
    if "volume" in r: api.debug("water_av_destroy", {"id": r["volume"]["id"], "discard": True})
    print("  record cleared:", round(r.get("seeded_mass", 0.0), 3), "m^3 discarded", flush=True)
    r2 = api.debug("water_av_create", {**box, "cellSize": 1.0 / 3.0, "backend": "cpu", "auto_sleep": False})
    if "error" in r2: raise SystemExit("the record still holds water: " + r2["error"])
    api.debug("water_av_destroy", {"id": r2["volume"]["id"], "discard": True})


class Thrower:
    """Throws between window samples: window() polls every 0.15 s, so the throws ride a background thread."""
    def __init__(self, api):
        import threading
        self.api, self.stop, self.n, self.side = api, threading.Event(), 0, 1.0
        self.t = threading.Thread(target=self.run, daemon=True)

    def run(self):
        while not self.stop.is_set():
            p = ppos(self.api)
            self.side = -self.side
            self.api.debug("spawn_gpu_particle", {"x": p[0] + 0.6, "y": p[1] + 1.6, "z": p[2], "material": "Wood", "scale": 1.0 / 3.0,
                                                  "lifetime": LIFE, "spin": 2.0, "velocity": {"x": 3.0 + 0.5 * (self.n % 3), "y": 2.0, "z": 1.2 * self.side}})
            self.n += 1
            self.stop.wait(THROW_S)


def foreign():
    """Other phyxel processes on this machine (another checkout's engine or GPU tests share the GPU: 2026-11-18 a
    phyxel_integration_tests run from another checkout held the GPU at 100 % and turned this rig's fence waits into 0.3-2.4 s frames).
    A window measured while one runs is marked, not trusted."""
    import subprocess
    try:
        o = subprocess.run(["powershell", "-NoProfile", "-Command",
                            "Get-CimInstance Win32_Process | Where-Object { $_.Name -like 'phyxel*' } | ForEach-Object { $_.ExecutablePath }"],
                           capture_output=True, text=True, timeout=20).stdout
    except Exception as e:
        return [f"unknown ({e})"]
    repo = str(pathlib.Path(__file__).resolve().parent.parent).lower()   # this checkout's engine (phyxel up runs <repo>/phyxel.exe)
    return sorted({p.strip() for p in o.splitlines() if p.strip() and not (p.strip().lower().startswith(repo + "\\") and p.strip().lower().endswith(r"\phyxel.exe"))})


def walk_window(api, label, seconds, throw=False):
    api.post("/api/input/inject", {"release_all": True})
    api.post("/api/entity/move", {"id": "player", "position": {"x": X_START, "y": TOP + 0.2, "z": Z_WALK}})
    api.post("/api/camera", {"control_scheme": "fps", "yaw": 0, "pitch": -12})
    time.sleep(2.0)
    p0 = ppos(api)
    api.post("/api/input/inject", {"keys": ["MoveForward"], "hold": seconds + 2.5})
    time.sleep(1.0)
    th = Thrower(api) if throw else None
    if th: th.t.start()
    r = window(api, seconds, label)
    if th: th.stop.set(); th.t.join()
    p1 = ppos(api)
    api.post("/api/input/inject", {"release_all": True})
    r.update({"player_start": p0, "player_end": p1, "travel_m": round(p1[0] - p0[0], 2), "throws": th.n if th else 0, "foreign_gpu_processes": foreign()})
    if r["foreign_gpu_processes"]: print("    CONTAMINATED - other phyxel processes ran:", r["foreign_gpu_processes"], flush=True)
    print(f"    player {p0} -> {p1}  (travel {r['travel_m']} m)  throws {r['throws']}", flush=True)
    return r


CHURN_CAM = {"x": X0 + W / 2 + 4, "y": TOP + 8, "z": Z0 + W / 2 + 18, "yaw": -90.0, "pitch": -30.0}
CHURN_T = (0.0, 20.0, 44.0)


def churn_case(api, case, seconds):
    from water_bench import camera_set
    api.post("/api/entity/move", {"id": "player", "position": {"x": 2.0, "y": TOP + 1.2, "z": 0.0}})
    time.sleep(1.0)
    destroy_all(api)
    time.sleep(0.5)
    if api.debug("water_av_list").get("volumes"): raise SystemExit("a volume is still live before the churn case")
    rl = api.debug("water_av_record_lake", {"x1": X0, "z1": Z0, "x2": X0 + W - 1, "z2": Z0 + W - 1, "bottom": TOP, "top": TOP + 2.0})
    print(f"[{case}] lake into the record:", rl, flush=True)
    res = {"case": case, "record_lake": rl, "windows": []}
    if case == "volume":
        cr = api.debug("water_av_create", {"x1": X0, "y1": TOP, "z1": Z0, "x2": X0 + W - 1, "y2": TOP + 2, "z2": Z0 + W - 1,
                                           "cellSize": 1.0 / 9.0, "backend": "gpu", "auto_sleep": False, "seed": "spans"})
        res["create"] = {k: cr.get(k) for k in ("error", "seeded_mass")}
        print(f"[{case}] create:", res["create"], flush=True)
    camera_set(api, CHURN_CAM)
    time.sleep(8.0)   # rest (the volume settles its seed; the record has nothing to do)
    res["rest"] = window(api, 6.0, f"churn_{case}_rest")
    k = api.debug("water_av_impulse", {"x": X0 + W / 2, "y": TOP + 1.9, "z": Z0 + W / 2, "radius": 2.0, "strength": 8.0, "dx": 0.0, "dy": -1.0, "dz": 0.0})
    t0 = time.time()
    for t in CHURN_T:
        while time.time() - t0 < t: time.sleep(0.1)
        r = window(api, seconds, f"churn_{case}_{int(t)}s")
        vols = api.debug("water_av_list").get("volumes", [])
        r["volumes_after"] = [{q: v.get(q) for q in ("id", "box", "is_patch", "asleep", "awake_columns", "frozen_columns", "mass")} for v in vols]
        r["awake_columns"] = sum(v.get("awake_columns", 0) for v in vols if not v.get("asleep"))
        r["foreign_gpu_processes"] = foreign()
        if r["foreign_gpu_processes"]: print("    CONTAMINATED:", r["foreign_gpu_processes"], flush=True)
        print(f"    awake columns {r['awake_columns']}, volumes {len(vols)}", flush=True)
        res["windows"].append(r)
    res["stone"] = k
    return res


def churn_main(api, a):
    out = {"bench": "WaterBench_Creek", "lake": {"x": [X0, X0 + W - 1], "z": [Z0, Z0 + W - 1], "bottom": TOP, "depth_m": 2.0},
           "stone": {"radius": 2.0, "strength": 8.0}, "camera": CHURN_CAM, "window_s": a.seconds, "cases": []}
    try:
        destroy_all(api)
        clear_record(api)
        lay_bed(api)
        time.sleep(2.0)
        for case in ("record", "volume", "record", "volume"):
            out["cases"].append(churn_case(api, case, a.seconds))
    finally:
        destroy_all(api)
        pathlib.Path(a.out).write_text(json.dumps(out, indent=1), encoding="utf-8")
        print("wrote", a.out)
    print("\nlabel                       fps     cpu ms   gpu ms   worst ms  water host ms  water gpu ms/call  awake")
    for c in out["cases"]:
        for r in [c["rest"]] + c["windows"]:
            print(f"{r['label']:26s} {r['fps_mean']:7.1f}  {r['cpu_frame_ms_mean']:7.3f}  {r['gpu_frame_ms_median']:7.3f}  {str(r['frame_ms_max']):>8s}"
                  f"  {r['water_host_cpu_ms_per_frame']:12.3f}  {r['water_gpu_ms_per_call']:16.4f}  {r.get('awake_columns')}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--seconds", type=float, default=12.0)
    ap.add_argument("--churn", action="store_true", help="30.2's worst case (the 2 m lake, a visible stone) instead of the walk")
    ap.add_argument("--out", default=str(pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4" / "s57_play_scene.json"))
    a = ap.parse_args()
    if a.churn and a.out.endswith("s57_play_scene.json"): a.out = a.out.replace("s57_play_scene.json", "s57_churn.json")
    if foreign(): raise SystemExit(f"other phyxel processes share the GPU: {foreign()} - measure when they are done")
    api = Api(a.url); api.wait_status(300)
    if not ps(api).get("success", True) or "error" in ps(api):
        api.post("/api/entity/spawn", {"type": "animated", "position": {"x": X_START, "y": TOP + 0.2, "z": Z_WALK}, "id": "player"})
    api.debug("water_av_realtime", {"on": True})
    if a.churn:
        a.seconds = min(a.seconds, 8.0)
        return churn_main(api, a)
    coupling = api.debug("water_coupling", {})
    out = {"bench": "WaterBench_Creek", "lake": {"x": [X0, X0 + W - 1], "z": [Z0, Z0 + W - 1], "bottom": TOP, "depth_m": DEPTH},
           "walk": {"start": [X_START, Z_WALK], "yaw": 0, "seconds": a.seconds}, "throw_s": THROW_S, "debris_lifetime_s": LIFE,
           "coupling": coupling, "windows": []}
    try:
        destroy_all(api)
        clear_record(api)
        lay_bed(api)
        time.sleep(2.0)
        for k in range(2): out["windows"].append(walk_window(api, f"dry_walk_{k}", a.seconds))
        # the player off the lake before it is recorded (the dry walk ends inside the box: a second patch woke there in run 1)
        api.post("/api/entity/move", {"id": "player", "position": {"x": 2.0, "y": TOP + 1.2, "z": 0.0}})
        time.sleep(1.0)
        rl = api.debug("water_av_record_lake", {"x1": X0, "z1": Z0, "x2": X0 + W - 1, "z2": Z0 + W - 1, "bottom": TOP, "top": TOP + DEPTH})
        print("lake into the record:", rl, flush=True)
        out["record_lake"] = rl
        time.sleep(3.0)
        for lab, th in (("lake_walk_0", False), ("lake_walk_debris_0", True), ("lake_walk_debris_1", True), ("lake_walk_1", False)):
            out["windows"].append(walk_window(api, lab, a.seconds, throw=th))
            if th: time.sleep(LIFE + 1.0)   # the wood gone before the next window
            out["windows"][-1]["volumes_after"] = [{k: v.get(k) for k in ("id", "box", "is_patch", "asleep", "awake_columns", "mass")}
                                                   for v in api.debug("water_av_list").get("volumes", [])]
        # the worst case on this lake: the whole lake awake, a stone at its centre, the player standing in it
        # the player OFF the lake first: a wader wakes a patch at once, and water_av_create does not refuse a box over a live volume -
        # the 2026-11-18 run seeded 2,777.6 m^3 beside an 870 m^3 patch over the same water (booked twice; logged in 57)
        api.post("/api/entity/move", {"id": "player", "position": {"x": 2.0, "y": TOP + 1.2, "z": 0.0}})
        time.sleep(1.0)
        destroy_all(api)
        time.sleep(0.5)
        if api.debug("water_av_list").get("volumes"): raise SystemExit("a volume is still live before the worst-case create")
        cr = api.debug("water_av_create", {"x1": X0, "y1": TOP, "z1": Z0, "x2": X0 + W - 1, "y2": TOP + 2, "z2": Z0 + W - 1,
                                           "cellSize": 1.0 / 9.0, "backend": "gpu", "auto_sleep": False, "seed": "spans"})
        print("worst case create:", {k: cr.get(k) for k in ("error", "seeded_mass")}, cr.get("volume", {}).get("cells"), flush=True)
        out["worst_create"] = {k: cr.get(k) for k in ("error", "seeded_mass")}
        api.post("/api/entity/move", {"id": "player", "position": {"x": X0 + W / 2 - 6, "y": TOP + 0.2, "z": Z0 + W / 2}})
        api.post("/api/camera", {"control_scheme": "fps", "yaw": 0, "pitch": -12})
        time.sleep(3.0)
        out["worst_volumes_before_stone"] = [{k: v.get(k) for k in ("id", "box", "mass")} for v in api.debug("water_av_list").get("volumes", [])]
        print("  before the stone:", out["worst_volumes_before_stone"], flush=True)
        api.debug("water_av_impulse", {"x": X0 + W / 2, "y": TOP + DEPTH - 0.1, "z": Z0 + W / 2, "radius": 0.5, "strength": 4.0, "dx": 0.0, "dy": -1.0, "dz": 0.0})
        out["windows"].append(window(api, a.seconds, "worst_awake"))
        out["windows"][-1]["foreign_gpu_processes"] = foreign()
        out["windows"][-1]["volumes_after"] = [{k: v.get(k) for k in ("id", "box", "asleep", "awake_columns", "mass")}
                                               for v in api.debug("water_av_list").get("volumes", [])]
    finally:
        api.post("/api/input/inject", {"release_all": True})
        destroy_all(api)
        pathlib.Path(a.out).write_text(json.dumps(out, indent=1), encoding="utf-8")
        print("wrote", a.out)
    print("\nlabel                 fps     cpu ms   gpu ms   worst ms  water host ms  water gpu ms/call  travel")
    for r in out["windows"]:
        print(f"{r['label']:20s} {r['fps_mean']:7.1f}  {r['cpu_frame_ms_mean']:7.3f}  {r['gpu_frame_ms_median']:7.3f}  {str(r['frame_ms_max']):>8s}"
              f"  {r['water_host_cpu_ms_per_frame']:12.3f}  {r['water_gpu_ms_per_call']:16.4f}  {r.get('travel_m')}")


if __name__ == "__main__":
    main()
