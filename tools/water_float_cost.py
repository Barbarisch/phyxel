"""WaterCore 31.5: what the pressure push costs - the whole frame (engine_timing cpuFrameTime + fps, gpu_timing gpu_frame_ms) and the
water's own CPU (water_av_host_phases), with the push ON vs OFF, interleaved on the same water, at N floating blocks.

On the Small bench pond of tools/water_float_rest.py (kept awake: auto_sleep off). N Wood blocks (size --size) are spawned in a grid
just above the surface, settle for --settle seconds, then 2 x 2 windows of 5 s alternate the push (on, off, on, off). Not measured:
the debris solver's integrate pass on its own (only the whole GPU frame), and any scene but this 4 x 4 m pond.

    python tools/water_float_cost.py [--count 20] [--size 0.3333] [--tag now]
Writes docs/evidence/water_core_m4/cost_<tag>.json.
"""
import argparse, json, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api
import water_float_rest as w


def window(api, seconds=5.0):
    api.debug("water_av_host_phases", {"reset": True})
    fps, cpu = [], []
    t0 = time.time()
    while time.time() - t0 < seconds:
        et = api.get("/api/debug/engine_timing"); fps.append(et.get("fps", 0)); cpu.append(et.get("cpuFrameTime", 0)); time.sleep(0.25)
    ph = api.debug("water_av_host_phases", {})
    gt = api.get("/api/debug/gpu_timing")
    c = api.debug("water_coupling", {})
    water_cpu = sum(x["ms_per_frame"] for k, x in ph.get("phases", {}).items() if not k.startswith("gpu.") or True)
    return {"fps_mean": sum(fps) / len(fps), "cpu_frame_ms_mean": sum(cpu) / len(cpu), "gpu_frame_ms_median": gt.get("gpu_frame_ms", {}).get("median_ms"),
            "water_host_phases_ms": round(water_cpu, 3), "pressure": c.get("pressure_force"),
            "wet_bodies": c.get("moving_solids", {}).get("wet_bodies")}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8111")
    ap.add_argument("--count", type=int, default=20)
    ap.add_argument("--size", type=float, default=1.0 / 3.0)
    ap.add_argument("--settle", type=float, default=2.0, help="seconds after the drop before the 5 s window")
    ap.add_argument("--tag", default="now")
    args = ap.parse_args()
    api = Api(args.url); api.wait_status(900)
    api.post("/api/camera", {"position": {"x": 102.0, "y": 20.0, "z": 8.0}, "yaw": 90, "pitch": -40})
    rows = []
    for k, on in enumerate((True, False, True, False)):
        # each window its own fresh pond and drop (the blocks settle and sleep: one drop cannot be timed both ways)
        w.PRESSURE = on; w.AUTO_SLEEP = False; w.DISPLACE = True
        w.pond(api)
        time.sleep(3.0)
        api.debug("spawn_gpu_particle", {"x": 100.6, "y": w.LEVEL + args.size, "z": 12.6, "material": "Wood", "scale": args.size,
                                         "count": args.count, "lifetime": 600, "spin": 0})
        time.sleep(args.settle)
        r = window(api); r["push"] = on; rows.append(r)
        print(f"push {'ON ' if on else 'OFF'}: fps {r['fps_mean']:.1f}  CPU frame {r['cpu_frame_ms_mean']:.2f} ms  GPU frame {r['gpu_frame_ms_median']:.3f} ms  "
              f"water host phases {r['water_host_phases_ms']:.3f} ms  wet bodies {r['wet_bodies']}  windows {r['pressure'].get('windows')}"
              f" cells {r['pressure'].get('window_cells')} pushed/fell/nowin {r['pressure'].get('ticks_pushed')}/{r['pressure'].get('ticks_fell_back')}/{r['pressure'].get('ticks_no_window')}", flush=True)
        api.debug("clear_dynamics")
    (w.EV / f"cost_{args.tag}.json").write_text(json.dumps({"count": args.count, "size": args.size, "rows": rows}, indent=1))
    api.debug("clear_dynamics")


if __name__ == "__main__":
    main()
