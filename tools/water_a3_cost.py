"""WaterCore plan A3 (drift D4): the whole-frame cost of A1 (the water's push, §31) and A2 (a slept pond is still water, §32), live,
on the Small bench (port 8111), with the shipped settings unless a scene says otherwise.

Each window reports the whole frame - fps, the CPU frame (engine_timing cpuFrameTime), the GPU frame (gpu_timing gpu_frame_ms,
median of its last 240 frames) - and the water's own share: its host CPU (water_av_host_phases, ms per frame) and its GPU
(water_av_kernel_timing: the volume calls' kernels, ms per call, and calls per second). The push windows' own GPU time is the
kernel interval `reduce_surface` (WaterCoreGpu.cpp: the stamp after `surface_copy` and the one after the window dispatch bracket
the BodyPressure dispatch and nothing else).

Scenes:
  still - the 4 x 4 m pond, no bodies: AWAKE (auto_sleep off) vs SLEPT (in the record), interleaved twice.
  push  - the awake pond with N floating 1/3 m Wood blocks (N = 0 is the control), the push ON (always: the old floating rule
          launches blocks and is never run, owner 2026-10-25), 3 s and 25 s after the drop.
  wake  - the stall of waking a slept pond: the 4 x 4 m pond (4 x 5 x 4 m box) and the S9 pit (8 x 8 m, 4 m deep, 256 m^3;
          a 10 x 9 x 10 m box), each slept, then a stone dropped in; slept.last_wake_ms (the create + seed, on the main thread,
          inside one frame) and the GPU frame's max over the window.
Not measured: the debris solver's integrate pass on its own (gpu_timing "GPU Particles" scope is reported);
any scene but these two rigs; the 2 M-cell volume cap (the pit's box is ~0.66 M cells).

    python tools/water_a3_cost.py [--only still,push,wake] [--counts 0,20,100] [--tag a3]
Writes docs/evidence/water_core_m4/a3_cost_<tag>.json.
"""
import argparse, json, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api
import water_float_rest as w

PIT_BOX = {"x1": 149, "y1": 11, "z1": 9, "x2": 158, "y2": 19, "z2": 18}
PIT_WATER = {"x1": 150, "y1": 12, "z1": 10, "x2": 157, "y2": 15, "z2": 17}   # 8 x 4 x 8 = 256 m^3 (pit floor top 11)


def scope(gt, name):
    for s in gt.get("scopes", []):
        if s.get("key") == name: return round(s.get("median_ms", 0.0), 4)
    return None


def window(api, seconds, label):
    api.debug("water_av_host_phases", {"reset": True})
    api.debug("water_av_kernel_timing", {"on": True, "reset": True})
    fps, cpu, parts = [], [], {}
    t0 = time.time()
    while time.time() - t0 < seconds:
        et = api.get("/api/debug/engine_timing"); fps.append(et.get("fps", 0)); cpu.append(et.get("cpuFrameTime", 0))
        for k, v in api.debug("water_av_host_phases", {}).get("app_water_cpu_ms_last", {}).items(): parts.setdefault(k, []).append(v)
        time.sleep(0.15)
    el = time.time() - t0
    ph = api.debug("water_av_host_phases", {})
    kt = api.debug("water_av_kernel_timing", {})
    gt = api.get("/api/debug/gpu_timing")
    c = api.debug("water_coupling", {})
    host = sum(x["ms_per_frame"] for x in ph.get("phases", {}).values())
    calls = kt.get("calls", 0)
    r = {"label": label, "fps_mean": round(sum(fps) / len(fps), 1), "fps_min": round(min(fps), 1),
         "cpu_frame_ms_mean": round(sum(cpu) / len(cpu), 3), "cpu_frame_ms_max_sampled": round(max(cpu), 3),
         "gpu_frame_ms_median": round(gt.get("gpu_frame_ms", {}).get("median_ms", 0.0), 3),
         "gpu_frame_ms_max": round(gt.get("gpu_frame_ms", {}).get("max_ms", 0.0), 3),
         "gpu_particles_ms_median": scope(gt, "GPU Particles"),
         "water_host_cpu_ms_per_frame": round(host, 3),
         # the editor's water block, by part (A3 timers; mean of the sampled frames): total, update (the manager), bodies,
         # solids (rasterizing displacing bodies), pressure (the push's asks), exchange, feed, motion
         "app_water_cpu_ms": {k: round(sum(v) / len(v), 3) for k, v in parts.items()},
         "water_gpu_ms_per_call": round(kt.get("total_ms_per_call", 0.0), 4), "water_gpu_calls_per_s": round(calls / el, 1),
         "push_windows_gpu_ms_per_call": round(kt.get("ms_per_call", {}).get("reduce_surface", 0.0), 4),
         "volumes": len(api.debug("water_av_list").get("volumes", [])),
         "wet_bodies": c.get("moving_solids", {}).get("wet_bodies"), "pressure": c.get("pressure_force"), "slept": c.get("slept")}
    print(f"  {label:28s} fps {r['fps_mean']:6.1f} (min {r['fps_min']:6.1f})  CPU {r['cpu_frame_ms_mean']:.3f} ms  GPU {r['gpu_frame_ms_median']:.3f} ms"
          f" (max {r['gpu_frame_ms_max']:.3f})  water: host {r['water_host_cpu_ms_per_frame']:.3f} ms/frame, GPU {r['water_gpu_ms_per_call']:.3f} ms/call"
          f" x {r['water_gpu_calls_per_s']}/s, push windows {r['push_windows_gpu_ms_per_call']:.4f} ms/call  vols {r['volumes']}  wet {r['wet_bodies']}\n"
          f"    app water CPU: {r['app_water_cpu_ms']}", flush=True)
    return r


def wait_slept(api, max_s=60.0):
    t0 = time.time()
    while time.time() - t0 < max_s:
        if not api.debug("water_av_list").get("volumes"): return time.time() - t0
        time.sleep(0.5)
    raise SystemExit("the pond did not sleep")


def still(api, seconds):
    rows = []
    for rep in range(2):
        w.AUTO_SLEEP = False; w.PRESSURE = True; w.pond(api); time.sleep(3.0)
        rows.append(window(api, seconds, f"still pond AWAKE #{rep + 1}"))
        w.AUTO_SLEEP = True; w.pond(api); print(f"  (slept after {wait_slept(api):.1f} s)"); time.sleep(2.0)
        rows.append(window(api, seconds, f"still pond SLEPT #{rep + 1}"))
    return rows


def push(api, seconds, counts):
    # the push is always on: the old floating rule launches blocks and is never run (owner 2026-10-25). The cost is read against
    # the same pond with fewer or no bodies, at two times after the drop (the splash at 3 s, the rest at 25 s), plus the push's
    # own CPU (app water part "pressure") and GPU (the window interval)
    rows = []
    for n in counts:
        w.AUTO_SLEEP = False; w.PRESSURE = True; w.pond(api); time.sleep(2.0)
        if n: api.debug("spawn_gpu_particle", {"x": 100.6, "y": w.LEVEL + 1 / 3, "z": 12.6, "material": "Wood", "scale": 1 / 3, "count": n,
                                               "lifetime": 900, "spin": 0})
        t0 = time.time()
        for at in (3.0, 25.0):
            time.sleep(max(0.0, at - (time.time() - t0)))
            r = window(api, seconds, f"push N={n} at {at:.0f} s"); r["count"] = n; r["at_s"] = at; rows.append(r)
        api.debug("clear_dynamics")
    return rows


def make_pit(api):
    api.debug("clear_dynamics")
    for v in api.debug("water_av_list").get("volumes", []):
        api.debug("water_av_destroy", {"id": v["id"], "discard": True})
    cr = api.debug("water_av_create", {**PIT_BOX, "cellSize": 1.0 / 9.0, "transport": "eulerian", "backend": "auto", "auto_sleep": True})
    if "error" in cr and cr.get("recorded_m3"):   # the pit's last water is in the record: take it into a volume, discard it, start fresh
        cr = api.debug("water_av_create", {**PIT_BOX, "cellSize": 1.0 / 9.0, "transport": "eulerian", "backend": "auto", "seed": "spans", "auto_sleep": False})
        if "error" in cr: raise SystemExit(f"pit (seeded, to purge): {cr}")
        api.debug("water_av_destroy", {"id": cr["volume"]["id"], "discard": True})
        cr = api.debug("water_av_create", {**PIT_BOX, "cellSize": 1.0 / 9.0, "transport": "eulerian", "backend": "auto", "auto_sleep": True})
    if "error" in cr: raise SystemExit(f"pit: {cr}")
    api.debug("place_water_box", {**PIT_WATER, "mass": 1.0, "target": "core"})
    api.debug("water_av_realtime", {"on": True})
    api.debug("water_coupling", {"enabled": True, "solids": True, "pressure_force": True, "displace": True, "wake_on_touch": True})


def wake(api, seconds):
    rows = []
    for name in ("small pond", "S9 pit"):
        for rep in range(2):
            if name == "small pond":
                w.AUTO_SLEEP = True; w.PRESSURE = True; w.pond(api); drop = (101.0, w.LEVEL + 1.2, 13.5)
            else:
                make_pit(api); drop = (153.5, 18.5, 13.5)
            print(f"  {name}: slept after {wait_slept(api, 90.0):.1f} s"); time.sleep(2.0)
            before = api.debug("water_coupling", {}).get("slept", {}).get("woken", 0)
            api.debug("water_av_kernel_timing", {"on": True, "reset": True})
            api.debug("spawn_gpu_particle", {"x": drop[0], "y": drop[1], "z": drop[2], "material": "Stone", "scale": 1 / 3, "lifetime": 120, "spin": 0})
            r = window(api, seconds, f"wake {name} #{rep + 1}")
            s = r["slept"] or {}
            r["woke"] = s.get("woken", 0) > before; r["wake_ms"] = s.get("last_wake_ms"); r["wake_split_ms"] = s.get("last_wake_split_ms")   # A2f
            print(f"    woke {r['woke']}  wake (create + seed) {r['wake_ms']} ms  GPU frame max {r['gpu_frame_ms_max']} ms", flush=True)
            print(f"    split {r['wake_split_ms']}", flush=True)
            # 39.7: the first step after a wake uploads the seeded grid - outside the wake's own timer, in the same frame
            vols = api.debug("water_av_list").get("volumes", [])
            r["step_uploads"] = [{"uploads": v.get("gpu_uploads"), "last_upload_ms": v.get("gpu_upload_ms")} for v in vols]
            print(f"    first-step upload {r['step_uploads']}", flush=True)
            r["scene"] = name; rows.append(r)
            api.debug("clear_dynamics")
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8111")
    ap.add_argument("--only", default="still,push,wake")
    ap.add_argument("--counts", default="0,20,100")
    ap.add_argument("--seconds", type=float, default=5.0)
    ap.add_argument("--tag", default="a3")
    a = ap.parse_args()
    api = Api(a.url); api.wait_status(300)
    # the camera at the small pond, ~6 m off, so its drawing is in the frame (the pit is ~50 m away, in view range). The
    # realistic play scene (§30.2) is not built; this is the rig, stated as such
    api.post("/api/camera", {"mode": "free", "position": {"x": 102.0, "y": 20.0, "z": 8.0}, "yaw": 90, "pitch": -40})
    out = {"seconds": a.seconds}
    for sc in a.only.split(","):
        print(sc, flush=True)
        if sc == "still": out["still"] = still(api, a.seconds)
        elif sc == "push": out["push"] = push(api, a.seconds, [int(c) for c in a.counts.split(",")])
        elif sc == "wake": out["wake"] = wake(api, a.seconds)
        (w.EV / f"a3_cost_{a.tag}.json").write_text(json.dumps(out, indent=1))
    api.debug("clear_dynamics")
    print("wrote", w.EV / f"a3_cost_{a.tag}.json")


if __name__ == "__main__":
    main()
