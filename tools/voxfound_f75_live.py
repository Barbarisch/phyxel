"""F7.5 live (docs/evidence/voxfound_f7_prediction.txt W1-W5): destruction on the switched world, the VoxFoundGate
project (port 8120). Modes:
  control : the switch on, POST apply_damage (DamageSystem) at open ground -> today's carving; the region must not change.
  red     : damage_stub (the override set but ignored): the same; today's carving runs.
  green   : W1-W5 -- today's column top (switch off), switch on, screenshot, apply_damage at E/T 400 -> the world rig:
            bodies, crumbs, the window's region voxels vs the lab grid; run 20 s (rest on): ledger checks, bakes, the
            check again, screenshots; switch off -> today's column top unchanged (today's chunk never touched).
Every artifact: WorldGenerator Perlin s7 + flora (today's chunks; the 1/8 m region)."""
import json, os, shutil, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")
REGION = {"type": "Perlin", "seed": 7, "flora": True, "editable": True, "hide_world": False,
          "region": {"from": [254, 0, 2], "to": [257, 1, 5]}}
XZ = (8132, 68)


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=3600) as resp:
        return json.loads(resp.read().decode())


def column(xz):
    return call("POST", P, {"column_top": [list(xz)]})["column_top"][0]


def region_window_hash(lo, hi):   # region voxels along the hit column's neighbourhood (a cheap fingerprint)
    pk = []
    for dx in range(-2, 3):
        for dz in range(-2, 3):
            r = call("POST", P, {"pick": {"origin": [XZ[0] + 0.5 + dx, 40, XZ[1] + 0.5 + dz], "dir": [0, -1, 0],
                                         "max_m": 30, "size": 8}})
            pk.append(r.get("voxel"))
    return pk


B = "/api/debug/voxfound_bodies"
POSE = {"mode": "free", "position": {"x": 8132.5, "y": 31.5, "z": 61.5}, "yaw": 90, "pitch": -30}


def shot(name):
    shutil.copy(os.path.join(REPO, call("GET", "/api/screenshot")["path"]), os.path.join(EVID, name))


def lab_status(check=True):
    s = call("POST", B, {"status": True, "pen": False, "world_check": check})
    keep = {k: s.get(k) for k in ("tick", "bodies", "awake", "crumbs", "ledger", "world")}
    keep["exit"] = {k: s.get("exit", {}).get(k) for k in ("ledger_checks", "ledger_bad", "ledger_now_ok")}
    keep["rest"] = {k: s.get("rest", {}).get(k) for k in ("on", "passes", "baked", "voxels")}
    keep["fluid"] = {k: s.get("fluid", {}).get(k) for k in ("quiet_tick", "rubble_units", "ms_p95")}
    return keep


def green(out):
    call("POST", P, {"world_switch": False})
    out["today_top_before"] = column(XZ)
    call("POST", P, {"damage_stub": False})
    out["switch"] = call("POST", P, {"world_switch": True})
    top = column(XZ)
    out["region_top_before"] = top
    call("POST", "/api/camera", POSE)
    time.sleep(3.0)
    shot("voxfound_f75_0_before.png")
    centre = {"x": XZ[0] + 0.5, "y": top[1] + 0.5, "z": XZ[1] + 0.5}
    t0 = time.time()
    out["apply_damage"] = call("POST", "/api/damage/apply", dict(centre, radius=2.0, energy=400.0))
    out["apply_wall_s"] = round(time.time() - t0, 3)
    out["after_break"] = lab_status()
    out["region_top_after_break"] = column(XZ)
    time.sleep(1.0)
    shot("voxfound_f75_1_broken.png")
    time.sleep(20.0)
    out["after_20s"] = lab_status()
    shot("voxfound_f75_2_after_20s.png")
    call("POST", B, {"run": False})
    # W4: every resting body baked back (the policy only bakes over its budget / outside the keep radius: forced here,
    # stated) -- then the window's region voxels vs the lab grid again
    t0 = time.time()
    call("POST", B, {"bake_now": {"budget": 0, "keep_m": 0.0, "rest_ticks": 0}})
    out["bake_wall_s"] = round(time.time() - t0, 3)
    out["after_bake"] = lab_status()
    out["region_top_after_bake"] = column(XZ)
    time.sleep(2.0)
    shot("voxfound_f75_3_baked.png")
    call("POST", P, {"world_switch": False})
    out["today_top_after"] = column(XZ)
    out["today_chunk_untouched"] = out["today_top_after"] == out["today_top_before"]


def main():
    mode = sys.argv[1]
    out = {"mode": mode}
    out["region"] = {k: v for k, v in call("POST", P, REGION).items() if k in ("faces", "chunks")}
    if mode == "green":
        green(out)
        print(json.dumps(out)[:4000])
        with open(os.path.join(EVID, "voxfound_f75_live.jsonl"), "a") as f:
            f.write(json.dumps(out) + "\n")
        return
    if mode == "red":
        call("POST", P, {"world_switch": False})
        out["stub"] = call("POST", P, {"damage_stub": True})
    out["switch"] = call("POST", P, {"world_switch": True})
    top = column(XZ)
    out["before"] = {"top": top, "picks": region_window_hash(None, None)}
    centre = {"x": XZ[0] + 0.5, "y": top[1] + 0.5, "z": XZ[1] + 0.5}
    if mode in ("control", "red"):
        out["apply_damage"] = call("POST", "/api/damage/apply", dict(centre, radius=2.0, energy=400.0))
        time.sleep(2.0)
    out["after"] = {"top": column(XZ), "picks": region_window_hash(None, None)}
    out["region_changed"] = out["before"] != out["after"]
    if mode == "red":
        call("POST", P, {"world_switch": False})
        call("POST", P, {"damage_stub": False})
    print(json.dumps(out)[:3000])
    with open(os.path.join(EVID, "voxfound_f75_live.jsonl"), "a") as f:
        f.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
