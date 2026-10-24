"""27.28: the water's CPU by phase, live, the 60 x 60 m lake awake (no screen capture) - python tools/water_host_phases.py out.json [fixed_tick_s]. Windows of 5 s after the push: the phases
(ms a frame), the whole frame (CPU, GPU, fps), the tiler."""
import json, pathlib, sys, time
sys.path.insert(0, r"G:\Github\phyxel\tools")
from water_bench import Api, camera_set
OUT = pathlib.Path(sys.argv[1])
tick = float(sys.argv[2]) if len(sys.argv) > 2 else 0.0
H, TOP, X0, Z0, L, W = 1 / 9, 16, 4, 2, 60, 60
api = Api("http://127.0.0.1:8114"); api.wait_status(300)
api.debug("water_av_realtime", {"on": True}); api.debug("water_av_record_draw", {"radius": 120.0}); api.debug("water_av_fixed_tick", {"seconds": tick})
for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"], "discard": True})
box = {"x1": X0, "y1": TOP, "z1": Z0, "x2": X0 + L - 1, "y2": TOP + 2, "z2": Z0 + W - 1}
api.debug("water_av_record_tiles", {"default": True})
vid = api.debug("water_av_create", {**box, "cellSize": H, "backend": "gpu", "auto_sleep": False})["volume"]["id"]
api.debug("water_av_record_tiles", {"default": False})
for k in range(2): api.debug("place_water_box", {"target": "core", **box, "y1": TOP + k, "y2": TOP + k, "mass": 1.0})
camera_set(api, {"x": X0 + L / 2, "y": TOP + 0.95 * L, "z": Z0 + W + 0.55 * L, "yaw": -90.0, "pitch": -52.0})
time.sleep(10)
api.debug("water_av_impulse", {"x": X0 + L / 2, "y": TOP + 1.9, "z": Z0 + W / 2, "radius": 0.5, "strength": 4.0, "dx": 0.0, "dy": -1.0, "dz": 0.0})
time.sleep(3)
res = []
for w in range(4):
    api.debug("water_av_host_phases", {"reset": True})
    fps, cpu = [], []
    t0 = time.time()
    while time.time() - t0 < 5:
        et = api.get("/api/debug/engine_timing"); fps.append(et.get("fps", 0)); cpu.append(et.get("cpuFrameTime", 0)); time.sleep(0.25)
    ph = api.debug("water_av_host_phases", {})
    gt = api.get("/api/debug/gpu_timing")
    v = next((x for x in api.debug("water_av_list")["volumes"] if x["id"] == vid), {})
    row = {"window_s": [3 + 5 * w, 8 + 5 * w], "frames": ph["frames"], "fps_mean": sum(fps) / len(fps), "cpu_frame_ms_mean": sum(cpu) / len(cpu),
           "gpu_frame_ms_median": gt.get("gpu_frame_ms", {}).get("median_ms"), "awake": v.get("awake_columns"), "solver_device_ms": v.get("gpu_device_ms"),
           "phases": {k: round(x["ms_per_frame"], 3) for k, x in ph["phases"].items()}, "tiler_last": ph.get("tiler_last")}
    res.append(row)
    print(f"--- {row['window_s'][0]}-{row['window_s'][1]} s after the push: {row['frames']} frames, fps {row['fps_mean']:.1f}, CPU frame {row['cpu_frame_ms_mean']:.1f} ms, "
          f"GPU frame {row['gpu_frame_ms_median']:.2f} ms, awake {row['awake']}, solver device {row['solver_device_ms']:.2f} ms, tiler {row['tiler_last']}", flush=True)
    for k, x in sorted(row["phases"].items()):
        if x >= 0.05: print(f"     {x:7.3f}  {k}", flush=True)
for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"], "discard": True})
api.debug("water_av_fixed_tick", {"seconds": 0.0}); api.debug("water_av_record_draw", {"radius": 48.0})
OUT.write_text(json.dumps(res, indent=1))
