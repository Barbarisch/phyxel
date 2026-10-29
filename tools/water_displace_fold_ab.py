"""44.5: the bodies' box folded into the step vs uploaded in a call of its own (test_solid_upload_sync), alternated in one session,
the pond scene with displacement on - the manager's phases per frame, to see where residency's extra cost comes from."""
import json, random, statistics, sys, time
sys.path.insert(0, r"G:\Github\phyxel\tools")
from water_bench import Api
import water_float_rest as w
api = Api("http://127.0.0.1:8111")
rows = []
for sync in (False, True, True, False, False, True):
    api.debug("water_coupling", {"test_solid_upload_sync": sync})
    w.AUTO_SLEEP = False; w.PRESSURE = True; w.DISPLACE = True; w.pond(api); time.sleep(3)
    got = api.debug("water_coupling", {}).get("test_solid_upload_sync")
    if got is not sync: raise SystemExit(f"knob not taken: {got}")
    api.debug("water_av_host_phases", {"reset": True})
    rng = random.Random(5); nxt = time.time(); t0 = time.time(); fps = []; trav = []
    while time.time() - t0 < 10:
        if time.time() >= nxt:
            for mat in ("Wood", "Wood", "Wood", "Stone"):
                api.debug("spawn_gpu_particle", {"x": 100.4 + rng.random() * 3.2, "y": w.LEVEL + 1.17, "z": 12.4 + rng.random() * 3.2, "material": mat, "scale": 1 / 3, "lifetime": 8.0, "spin": 0})
            nxt += 2.5
        fps.append(api.get("/api/debug/engine_timing").get("fps", 0))
        v = api.debug("water_av_list").get("volumes", [{}])[0]
        trav.append((v.get("gpu_stored_bricks") or v.get("bricks") or 0, v.get("max_speed") or v.get("gpu_max_speed") or 0))
        time.sleep(0.1)
    ph = api.debug("water_av_host_phases", {})["phases"]
    pick = {k: round(ph[k]["ms_per_frame"], 3) for k in ph if k.startswith(("gpu.0", "solids", "mgr.4", "collect"))}
    r = {"sync": sync, "fps": round(statistics.mean(fps), 1), "phases": pick, "bricks_speed_median": [statistics.median(t[0] for t in trav), statistics.median(t[1] for t in trav)]}
    print(r, flush=True); rows.append(r)
    api.debug("clear_dynamics")
api.debug("water_coupling", {"test_solid_upload_sync": False})
json.dump(rows, open(r"G:\Github\phyxel\docs\evidence\water_core_m4\displace_fold_ab.json", "w"), indent=1)
