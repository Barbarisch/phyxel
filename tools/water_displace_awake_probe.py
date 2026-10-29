"""Why the near pond never sleeps with displacement on: one L5 drop, the viewer near, polled for 50 s."""
import sys, time, json
sys.path.insert(0, r"G:\Github\phyxel\tools")
from water_bench import Api
import water_float_rest as w
import water_l5_outcome as l5
api = Api("http://127.0.0.1:8111")
mode = sys.argv[1] if len(sys.argv) > 1 else "on"
api.debug("water_locality", {"test_viewer": l5.NEAR})
w.AUTO_SLEEP = True; w.PRESSURE = True; w.DISPLACE = mode != "off"; w.pond(api)
if not l5.wait_slept(api, 60): raise SystemExit("did not sleep first")
if mode == "noripple": api.debug("water_locality", {"ripple_m": 0.0})
for b in l5.BODIES.values():
    api.debug("spawn_gpu_particle", {"x": b["x"], "y": l5.Y0, "z": b["z"], "material": b["material"], "scale": 1 / 3, "lifetime": 90, "spin": 0})
t0 = time.time()
while time.time() - t0 < 50:
    v = api.debug("water_av_list").get("volumes", [])
    c = api.debug("water_coupling", {})
    g = (c.get("displace_gate") or {}).get("last") or {}
    rp = api.debug("water_ripples", {})
    ms = c.get("moving_solids") or {}
    vv = v[0] if v else {}
    dr = api.debug("water_droplets", {})
    print(f"{time.time()-t0:5.1f}s vols {len(v)} asleep {vv.get('asleep')} awake_cols {vv.get('awake_columns')} | rasterized {ms.get('bodies')} wake_cells {ms.get('wake_cells')} | ripples awake {rp.get('awake')} max_abs {rp.get('max_abs_m', 0):.2e} impulses {rp.get('impulses')} kin {rp.get('kinematic_cells')} | droplets alive {dr.get('alive')} born {dr.get('born_m3', dr.get('born'))}", flush=True)
    if not v and time.time() - t0 > 3: print("SLEPT"); break
    time.sleep(2.0)
api.debug("clear_dynamics")
api.debug("water_locality", {"clear_test_viewer": True, "ripple_m": 21.0})
