"""WaterCore.md 27.18 (27.14 finding 3): a volume created over water the world already records - refused unseeded, seeded exact.

The 27.12 rig (the walled 46 x 28 m pond, 1 m deep) on a fresh engine: placed with tile recording ON until all 24 tiles are in the
record; the volume destroyed (the record keeps the water - the world's); then over the same box
  1. water_av_create seed:"none"  -> must be REFUSED, naming the recorded m^3 (it booked the water twice and the next write-back
     dropped 1,288 m^3 silently, 27.14);
  2. water_av_create seed:"spans" -> takes the record into the volume; recording ON again, every tile records again.
The record (water_spans_stored over the box) is read at every stage: it must stay at what was placed, within 1e-3 m^3.

    python tools/water_create_over_record.py [--json out.json]
The bench engine (WaterBench_Creek, 8114) must be running with an EMPTY record.
"""
import argparse, json, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, world_job


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--json", default=None)
    a = ap.parse_args()
    api = Api(a.url); api.wait_status(300)
    api.debug("water_av_realtime", {"on": True})
    l, w, x0, z0 = 46, 28, 2, 0
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"]})
    for b in ((x0 - 1, z0 - 1, x0 - 1, z0 + w), (x0 + l, z0 - 1, x0 + l, z0 + w), (x0 - 1, z0 - 1, x0 + l, z0 - 1), (x0 - 1, z0 + w, x0 + l, z0 + w)):
        world_job(api, "/api/world/fill", {"x1": b[0], "y1": 16, "z1": b[1], "x2": b[2], "y2": 17, "z2": b[3]}, material="StoneBricks")
    box = {"x1": x0, "y1": 16, "z1": z0, "x2": x0 + l - 1, "y2": 17, "z2": z0 + w - 1}
    spans = lambda: api.debug("water_spans_stored", {"x1": x0, "z1": z0, "x2": x0 + l - 1, "z2": z0 + w - 1}).get("total_depth", 0.0)
    out = {"stages": []}
    def stage(name, **kw):
        e = {"stage": name, "spans_m3": spans(), **kw}; out["stages"].append(e); print(json.dumps(e)); return e
    if spans() > 1e-3: sys.exit("the record is not empty - restart the engine")

    def wait_recorded(vid, n=24, limit=40):
        t0 = time.time()
        while time.time() - t0 < limit:
            v = next(x for x in api.debug("water_av_list")["volumes"] if x["id"] == vid)
            if v.get("recorded_tiles") == n: return v, time.time() - t0
            time.sleep(0.25)
        return v, None

    vid = api.debug("water_av_create", {**box, "cellSize": 1 / 9, "backend": "gpu", "auto_sleep": False})["volume"]["id"]
    api.debug("water_av_record_tiles", {"id": vid, "on": True, "after_s": 2.0})
    api.debug("place_water_box", {"target": "core", **{**box, "y2": 16}, "mass": 1.0})
    c = api.debug("water_av_census", {"id": vid}); placed = c["mass_m3"] + c["record_m3"]
    v, t = wait_recorded(vid)
    stage("placed, recorded", placed_m3=placed, recorded_tiles=v.get("recorded_tiles"), after_s=t)
    api.debug("water_av_destroy", {"id": vid})
    stage("volume destroyed (the record keeps the water)")
    r1 = api.debug("water_av_create", {**box, "cellSize": 1 / 9, "backend": "gpu", "auto_sleep": False})
    refused = "error" in r1
    stage("create seed:none over the record", refused=refused, response={k: r1.get(k) for k in ("error", "recorded_m3", "success")})
    if not refused:   # the defect: book the water twice, as 27.14's harness did
        vid1 = r1["volume"]["id"]
        api.debug("water_av_record_tiles", {"id": vid1, "on": True, "after_s": 2.0})
        api.debug("place_water_box", {"target": "core", **{**box, "y2": 16}, "mass": 1.0})
        c = api.debug("water_av_census", {"id": vid1})
        v, t = wait_recorded(vid1)
        stage("UNSEEDED create, placed again, recorded", census_cells_plus_record_m3=c["mass_m3"] + c["record_m3"], recorded_tiles=v.get("recorded_tiles"))
        api.debug("water_av_destroy", {"id": vid1})
    r2 = api.debug("water_av_create", {**box, "cellSize": 1 / 9, "backend": "gpu", "auto_sleep": False, "seed": "spans", "partial_wake": True})
    vid2 = r2["volume"]["id"]
    stage("create seed:spans", seeded_m3=r2.get("seeded_mass"))
    api.debug("water_av_record_tiles", {"id": vid2, "on": True, "after_s": 2.0})
    v, t = wait_recorded(vid2)
    c = api.debug("water_av_census", {"id": vid2})
    stage("seeded volume, recorded again", recorded_tiles=v.get("recorded_tiles"), after_s=t, cells_m3=c["mass_m3"], record_m3=c["record_m3"])
    final = out["stages"][-1]
    out["placed_m3"] = placed
    out["drift_m3"] = max(abs(s["spans_m3"] - placed) for s in out["stages"][1:])
    out["pass"] = refused and final.get("recorded_tiles") == 24 and out["drift_m3"] < 1e-3 and final["cells_m3"] < 1e-3
    print(f"placed {placed:.4f} m3; record drift worst {out['drift_m3']:.2e} m3; unseeded create refused: {refused}; PASS {out['pass']}")
    if a.json: pathlib.Path(a.json).write_text(json.dumps(out, indent=1))


if __name__ == "__main__":
    main()
