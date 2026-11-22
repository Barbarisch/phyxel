"""F7.8 live (docs/evidence/voxfound_f7_prediction.txt P2-P4): the physics occupancy on the switched world.
VoxFoundGate (port 8120). Modes: control (the F7.6d editor: no phys_probe -- the player drop only), red (phys_stub),
green. The region: editable, load_saved:false (the generator's). Edits through the engine's voxel API (clear / fill).
Every artifact: WorldGenerator Perlin s7 + flora (the 1/8 m region)."""
import json, os, random, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")
PID = "f78probe"   # an animated character spawned for the drops (the gate project has no player)
REGION = {"type": "Perlin", "seed": 7, "flora": True, "editable": True, "hide_world": False,
          "region": {"from": [254, 0, 2], "to": [257, 1, 5]}}


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(r, timeout=3600) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        return {"http_status": e.code, "body": e.read().decode()[:300]}


def wait_async(r):
    aid = r.get("async_id")
    for _ in range(600):
        if not aid:
            return r
        s = call("GET", f"/api/async/{aid}")
        if s.get("status") == "complete" or "error" in s:
            return s
        time.sleep(0.1)


def box(lo, hi, **kw):
    b = {"x1": lo[0], "y1": lo[1], "z1": lo[2], "x2": hi[0], "y2": hi[1], "z2": hi[2]}
    b.update(kw)
    return b


def player_y():
    e = call("GET", "/api/entity/" + PID)
    p = e.get("position") or {}
    return p.get("y"), e


def drop(x, y, z, wait=4.0):
    call("POST", "/api/entity/move", {"id": PID, "position": {"x": x, "y": y, "z": z}})
    time.sleep(wait)
    return player_y()[0]


def probes(n=4000):
    rnd = random.Random(78)
    pts = []
    for _ in range(n):
        v = (rnd.randrange(8128 * 8, 8140 * 8), rnd.randrange(20 * 8, 34 * 8), rnd.randrange(64 * 8, 76 * 8))
        pts.append([(v[0] + 0.5) / 8, (v[1] + 0.5) / 8, (v[2] + 0.5) / 8])
    return pts


def main():
    mode = sys.argv[1]
    out = {"mode": mode}
    out["region"] = {k: v for k, v in call("POST", P, REGION).items() if k in ("faces", "chunks")}
    out["spawn"] = call("POST", "/api/entity/spawn", {"type": "animated", "id": PID,
                                                       "position": {"x": 8132.5, "y": 34.0, "z": 68.5}})
    time.sleep(2.0)
    out["player_entity"] = player_y()[1]
    call("POST", P, {"world_switch": False})
    out["today_ground_y"] = drop(8132.5, 34.0, 68.5)      # today's surface: top cube 27 -> ground at 28 m
    if mode == "red":
        out["stub"] = call("POST", P, {"phys_stub": True})
    out["switch"] = call("POST", P, {"world_switch": True, "load_saved": False})
    out["pit"] = {k: v for k, v in wait_async(call("POST", "/api/world/clear", box([8131, 24, 67], [8133, 27, 69]))).items()
                  if k in ("removed", "world")}
    out["pillar"] = {k: v for k, v in wait_async(call("POST", "/api/world/fill", box([8136, 28, 72], [8136, 30, 72],
                                                                                     material="Stone"))).items()
                     if k in ("placed", "world")}
    if mode != "control":
        out["P2"] = call("POST", P, {"phys_probe": probes()})
    out["P3_pit_y"] = drop(8132.5, 34.0, 68.5)            # pit floor: top cube 23 -> 24 m
    out["P3_pillar_y"] = drop(8136.5, 34.0, 72.5)         # pillar top 31 m
    out["P4_clear_pillar"] = {k: v for k, v in wait_async(call("POST", "/api/world/clear",
                                                               box([8136, 28, 72], [8136, 30, 72]))).items()
                              if k in ("removed", "world")}
    time.sleep(4.0)
    out["P4_after_clear_y"] = player_y()[0]                # back to the ground (28 m) if edits are seen live
    call("POST", P, {"world_switch": False})
    out["switch_off_y"] = drop(8132.5, 34.0, 68.5)
    if mode == "red":
        call("POST", P, {"phys_stub": False})
    print(json.dumps(out)[:4000])
    with open(os.path.join(EVID, "voxfound_f78_live.jsonl"), "a") as f:
        f.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
