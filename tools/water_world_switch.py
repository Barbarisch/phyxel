"""WaterCore 47 (A2i) - the water core across world changes, live. Engine on WaterBench_Scenes (scene 'pond' = WaterBench_Small's
world with its 4 x 4 m pond slept into the record; scene 'dry' = a flat floor in the same chunk, no water).

    python tools/water_world_switch.py --url http://127.0.0.1:8112 [--project-switch C:/.../WaterBench_Dry] [--out docs/evidence/...json]

Legs (each a written prediction, checked; the run prints PASS / FAIL per check):
  0. control: in 'pond', the record holds R m^3 (24 m^3 expected); a wake (a volume seeded from the record) holds R.
  1. 'pond' with the pond AWAKE (woken 0.5 s before) -> 'dry': no live volume, no slept box, no body row of 'pond'; 'dry''s record
     over the pond box holds 0; the write-back reported one volume of R m^3.
  2. 'dry' -> 'pond': one slept box restored, body rows restored; the record holds R +- 5e-6 m^3 (0.005 L, float32 spans); a wake
     holds R.
  3. (--project-switch) 'pond' awake -> projects_open another project: no live volume left.
No body is dropped (the wake is a seeded create), so nothing can rise; GPU backend, shipped settings."""
import argparse, json, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api

BOX = {"x1": 99, "y1": 14, "z1": 11, "x2": 104, "y2": 19, "z2": 16}
TOL = 5e-6
checks, log = [], {}


def check(name, cond, got):
    checks.append({"check": name, "ok": bool(cond), "got": got})
    print(("PASS " if cond else "FAIL ") + f"{name}: {got}")


def recorded(api):
    r = api.debug("water_av_create", {**BOX, "cellSize": 1.0 / 9.0, "transport": "eulerian", "seed": "none"})
    if "error" not in r:   # it made a volume: the record held nothing - undo it
        api.debug("water_av_destroy", {"id": r["volume"]["id"], "discard": True})
        return 0.0
    return r.get("recorded_m3")


def state(api):
    c = api.debug("water_coupling")
    v = api.debug("water_av_list")
    return {"volumes": len(v.get("volumes", [])), "total_mass": v.get("total_mass"), "slept_boxes": c.get("slept", {}).get("boxes"),
            "bodies": len(api.debug("water_body_table").get("table", [])), "av_mass": api.debug("water_body_table").get("av_mass"),
            "world": c.get("world")}


def wake(api):
    r = api.debug("water_av_create", {**BOX, "cellSize": 1.0 / 9.0, "transport": "eulerian", "backend": "auto", "seed": "spans"})
    if "error" in r: raise SystemExit(f"wake: {r}")
    time.sleep(0.5)
    return api.debug("water_av_list").get("total_mass")


def scene(api, sid, limit=60):
    r = api.post("/api/scene/transition", {"scene_id": sid})
    if not r.get("success"): raise SystemExit(f"transition {sid}: {r}")
    t0 = time.time()
    while time.time() - t0 < limit:
        a = api.get("/api/scene/active")
        if a.get("id", a.get("scene_id")) == sid and not a.get("transitioning", False): break
        time.sleep(0.5)
    time.sleep(2.0)   # the load's frames
    return api.get("/api/scene/active")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8112")
    ap.add_argument("--project-switch")
    ap.add_argument("--out")
    ap.add_argument("--red", action="store_true", help="TEST: the world hooks off (water_coupling test_world_reset_off) - before 47")
    a = ap.parse_args()
    api = Api(a.url)
    api.wait_status()
    log["reset_off"] = api.debug("water_coupling", {"test_world_reset_off": a.red}).get("world", {}).get("test_reset_off")
    if log["reset_off"] != a.red: raise SystemExit(f"test_world_reset_off not taken (stale binary?): {log['reset_off']}")
    act = api.get("/api/scene/active")
    log["start_scene"] = act
    if act.get("id", act.get("scene_id")) != "pond": scene(api, "pond")
    log["pond_before"] = state(api)
    R = recorded(api)
    check("0 control: the record holds the pond", R is not None and abs(R - 24.0) < 0.05, R)
    m = wake(api)
    check("0 control: a wake holds the record", m is not None and abs(m - R) < 1e-3, m)
    # 1. pond (awake) -> dry
    log["pond_awake"] = state(api)
    log["to_dry"] = scene(api, "dry")
    s = state(api); log["dry"] = s
    check("1 no live volume of 'pond' in 'dry'", s["volumes"] == 0, s["volumes"])
    check("1 no slept box of 'pond' in 'dry'", s["slept_boxes"] == [], s["slept_boxes"])
    check("1 no body row of 'pond' in 'dry'", s["bodies"] == 0, s["bodies"])
    rd = recorded(api)
    check("1 'dry''s record over the pond box is empty", rd == 0.0, rd)
    wb = ((s.get("world") or {}).get("last") or {})
    check("1 the write-back reported one volume of the pond", (wb.get("written_back") or {}).get("volumes") == 1
          and abs((wb.get("written_back") or {}).get("stored_m3", 0) - R) < 1e-3, wb.get("written_back"))
    # 2. dry -> pond
    log["to_pond"] = scene(api, "pond")
    s = state(api); log["pond_after"] = s
    check("2 one slept box restored", s["slept_boxes"] is not None and len(s["slept_boxes"]) == 1, s["slept_boxes"])
    check("2 no live volume carried", s["volumes"] == 0, s["volumes"])
    check("2 body rows restored", s["bodies"] == log["pond_before"]["bodies"] and s["bodies"] > 0, {"before": log["pond_before"]["bodies"], "after": s["bodies"]})
    R2 = recorded(api)
    check("2 the record holds what 'pond' held", R2 is not None and abs(R2 - R) <= TOL, {"before": R, "after": R2, "diff": None if R2 is None else R2 - R})
    m = wake(api)
    check("2 a wake holds it", m is not None and abs(m - R) < 1e-3, m)
    if a.project_switch:
        log["project_switch_before"] = state(api)
        r = api.post("/api/projects/open", {"path": a.project_switch}, timeout=120)
        log["projects_open"] = r
        time.sleep(3.0)
        s = state(api); log["project_switch_after"] = s
        check("3 no live volume after the project switch", s["volumes"] == 0, s["volumes"])
        check("3 no slept box of the last project", s["slept_boxes"] == [], s["slept_boxes"])
    fails = [c for c in checks if not c["ok"]]
    print(f"\n{len(checks) - len(fails)}/{len(checks)} passed")
    if a.out:
        pathlib.Path(a.out).write_text(json.dumps({"checks": checks, "log": log}, indent=1), encoding="utf-8")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
