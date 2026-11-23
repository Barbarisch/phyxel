"""F7.25 live (docs/evidence/voxfound_f7_prediction.txt I1-I2): items snap to the switched world's 1/8 m ground.
Project VoxFoundBoot (switched). Modes: red (item_snap_stub: today's cube scan) / green. Every artifact: WorldGenerator
Perlin s7 + flora + items spawned through /api/items/spawn (+ one size-2 brush placed through the voxel API, I2)."""
import json, os, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")
COLS = [(8140.3, 175.6), (8162.7, 181.2), (8188.4, 192.9), (8205.1, 214.4), (8221.6, 236.3), (8236.2, 252.8),
        (8148.9, 268.1), (8241.7, 171.4)]


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(r, timeout=3600) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        return {"http_status": e.code, "body": e.read().decode()[:300]}


def ground_top(x, z):   # the 1/8 m ground top in the column (pick read-back)
    vx, vz = int(x * 8) // 1, int(z * 8) // 1
    for vy in range(60 * 8, 0, -1):
        if call("POST", P, {"pick": {"origin": [x, (vy + 0.5) / 8, z], "dir": [0, -1, 0], "max_m": 0.01, "size": 8}}).get("voxel"):
            return (vy + 1) / 8
    return None


def spawn(x, z):
    r = call("POST", "/api/items/spawn", {"item": "goblet", "x": x, "y": 45.0, "z": z})
    # base_y = the prop's render-pose bottom (run 1 read position.y, an INTEGER cube of the placed object -- truncated)
    by = r.get("base_y")
    return (None if by is None else round(by, 4)), r.get("prop_id"), r.get("error")


def main():
    mode = sys.argv[1]
    out = {"mode": mode}
    for _ in range(150):
        if call("POST", P, {"boot_status": True}).get("report"):
            break
        time.sleep(2)
    call("POST", P, {"stream": False})
    if mode == "red":
        out["stub"] = call("POST", P, {"item_snap_stub": True})
    rows = []
    for x, z in COLS:
        g = ground_top(x, z)
        y, pid, err = spawn(x, z)
        rows.append({"col": [x, z], "ground": g, "base": y, "off": None if (g is None or y is None) else round(y - g, 4),
                     "error": err})
    out["I1"] = rows
    out["I1_max_off"] = max(abs(r["off"]) for r in rows if r["off"] is not None)
    # I2: a size-2 brush (2/8 m) on the ground of the first column; an item there rests on its top
    x, z = COLS[0]
    g = ground_top(x, z)
    vx, vy, vz = int(x * 8), int(round(g * 8)), int(z * 8)
    call("POST", "/api/world/voxel", {"x": vx - (vx % 2), "y": vy, "z": vz - (vz % 2), "units": "voxel", "size": 2,
                                      "material": "Stone"})
    g2 = ground_top(x, z)
    y, pid, err = spawn(x, z)
    out["I2"] = {"ground_before": g, "ground_after_brush": g2, "base": y, "off": None if y is None else round(y - g2, 4)}
    if mode == "red":
        call("POST", P, {"item_snap_stub": False})
    print(json.dumps(out))
    with open(os.path.join(EVID, "voxfound_f725_live.jsonl"), "a") as fh:
        fh.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
