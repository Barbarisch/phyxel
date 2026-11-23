"""F7.30 live (docs/evidence/voxfound_f7_prediction.txt R1-R2): the switched world's grass follows edits.
A copy of VoxFoundBoot booted without saved edits (a clean meadow). Modes: red (grass_refresh_stub) / green.
Every artifact: WorldGenerator Perlin s7 + flora + two voxel-API edits by this driver."""
import json, os, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=600) as x:
        return json.loads(x.read().decode())


def wait_async(r):
    aid = r.get("async_id")
    for _ in range(600):
        if not aid:
            return r
        s = call("GET", f"/api/async/{aid}")
        if s.get("status") == "complete" or "error" in s:
            return s
        time.sleep(0.1)


def status():
    return call("POST", P, {"grass_status": True})


def main():
    mode = sys.argv[1]
    out = {"mode": mode}
    for _ in range(150):
        if call("POST", P, {"boot_status": True}).get("report"):
            break
        time.sleep(2)
    call("POST", P, {"stream": False})
    if mode == "red":
        call("POST", P, {"grass_refresh_stub": True})
    x0, z0 = 8190, 200
    tops = []
    for x in (x0, x0 + 1):
        for z in (z0, z0 + 1):
            for vy in range(40 * 8, 10 * 8, -1):
                if call("POST", P, {"pick": {"origin": [x + 0.5, (vy + 0.5) / 8, z + 0.5], "dir": [0, -1, 0], "max_m": 0.01, "size": 8}}).get("voxel"):
                    tops.append((vy + 1) / 8)
                    break
    out["tops_before"] = tops
    cy = int(min(tops) - 1e-6) if tops else 22
    s0 = status()
    out["before"] = s0
    wait_async(call("POST", "/api/world/clear", {"x1": x0, "y1": cy, "z1": z0, "x2": x0 + 1, "y2": cy + 3, "z2": z0 + 1}))
    time.sleep(1.0)
    s1 = status()
    out["after_clear"] = s1
    out["R1_drop"] = s0["grass_instances"] - s1["grass_instances"]
    out["R1_renderer_equal"] = s1["grass_instances"] == s1["renderer_instances"]
    call("POST", "/api/world/voxel", {"x": x0, "y": cy - 1, "z": z0, "material": "Grass"})
    time.sleep(1.0)
    s2 = status()
    out["after_place"] = s2
    out["R2_gain"] = s2["grass_instances"] - s1["grass_instances"]
    if mode == "red":
        call("POST", P, {"grass_refresh_stub": False})
    print(json.dumps(out))
    with open(os.path.join(EVID, "voxfound_f730_live.jsonl"), "a") as fh:
        fh.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
