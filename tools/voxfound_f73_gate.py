"""F7.3 gate (docs/evidence/voxfound_f7_prediction.txt D1-D4): drawing behind the world switch, with section 4.3's pixel
diff. Run on the VoxFoundGate project (port 8120), Release, from the repo root:
    python tools/voxfound_f73_gate.py docs/evidence/voxfound_f29_poses.json OUTDIR [--red]
Builds the region (F2.19's: Perlin s7, flora, editable, chunks 254..257 x 0..1 x 2..5), then
  --red : the stub switch (hide + draw, no routing): the reads check must NOT equal the region's rule.
  else  : D3 reads check and D4 nav check with the switch off and on; D1 / D2 pixels at each pose (debug tonemap,
          wind 0, GI off): P = F2.19's B ({"hide_world":true,"show_preview":true}), S = {"world_switch":true}, S2 = S
          again, A = {"world_switch":false}. D1: S vs P and S vs S2 (0 px predicted); D2: A vs S (% px > 8/255).
Every artifact: the engine's WorldGenerator (Perlin seed 7, flora) -- today's chunks (A) and the 1/8 m region (S)."""
import json, os, shutil, sys, time, urllib.request
import numpy as np
from PIL import Image

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
CLEAN = (425, 105, 1195, 620)   # F2.19's 3D-viewport crop
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
REGION = {"type": "Perlin", "seed": 7, "flora": True, "editable": True, "hide_world": False,
          "region": {"from": [254, 0, 2], "to": [257, 1, 5]}}
NAV = {"from": [8150, 120], "to": [8200, 150]}   # two ground points 58 m apart inside the region (world x, z)


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=3600) as resp:
        return json.loads(resp.read().decode())


def shot(out, name):
    d = os.path.join(out, name)
    shutil.copy(os.path.join(REPO, call("GET", "/api/screenshot")["path"]), d)
    x0, y0, x1, y1 = CLEAN
    return np.asarray(Image.open(d).convert("RGB")).astype(np.int16)[y0:y1, x0:x1]


def ndiff(a, b, t=8):
    return int((np.max(np.abs(a - b), axis=2) > t).sum())


def main():
    poses = json.load(open(sys.argv[1]))
    out = sys.argv[2]
    red = "--red" in sys.argv
    navonly = "--nav-only" in sys.argv
    os.makedirs(out, exist_ok=True)
    res = {"region": {k: v for k, v in call("POST", P, REGION).items() if k in ("faces", "chunks", "editable", "error")}}
    print("region", res["region"], flush=True)
    if red:
        call("POST", P, {"world_switch_stub": True})
        on = call("POST", P, {"world_switch": True, "reads_check": {"n": 4096}})
        res["red"] = on
        print("RED stub switch:", json.dumps(on), flush=True)
        call("POST", P, {"world_switch": False, "world_switch_stub": False})
        json.dump(res, open(os.path.join(out, "f73_red.json"), "w"), indent=1)
        return
    # D4's points: open ground, not canopy -- columns whose top solid cube (on the switched reads) is below 32 m, the
    # first and the farthest of a scan of the region (run 1 took the top cube of fixed columns: tree crowns, no path)
    call("POST", P, {"world_switch": True})
    cols = [[x, z] for x in range(8132, 8252, 6) for z in range(68, 188, 6)]
    tops = [c for c in call("POST", P, {"column_top": cols})["column_top"] if 0 <= c[1] < 32]
    a = tops[0]
    b = max(tops, key=lambda c: (c[0] - a[0]) ** 2 + (c[2] - a[2]) ** 2)
    NAV.update({"from": [a[0] + 0.5, a[1] + 1, a[2] + 0.5], "to": [b[0] + 0.5, b[1] + 1, b[2] + 0.5]})
    res["nav_points"] = {"ground_columns": len(tops), "of": len(cols), "from": NAV["from"], "to": NAV["to"]}
    print("nav points", res["nav_points"], flush=True)
    off = call("POST", P, {"world_switch": False, "reads_check": {"n": 4096}, "nav_check": NAV})
    on = call("POST", P, {"world_switch": True, "reads_check": {"n": 4096}, "nav_check": NAV})
    res["switch_off"], res["switch_on"] = off, on
    print("OFF:", json.dumps(off), flush=True)
    print("ON :", json.dumps(on), flush=True)
    if navonly:
        call("POST", P, {"world_switch": False})
        json.dump(res, open(os.path.join(out, "f73_nav.json"), "w"), indent=1)
        return
    call("POST", "/api/debug/foliage", {"windStrength": 0.0})
    call("POST", "/api/debug/gi", {"enabled": False})
    call("POST", "/api/debug/tonemap", {"curve": 0})
    try:
        for name, (x, y, z, yaw, pitch) in poses.items():
            call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
            im = {}
            for lab, cfgs in (("P", [{"world_switch": False}, {"hide_world": True, "show_preview": True}]),
                              ("S", [{"world_switch": True}]), ("S2", [{"world_switch": True}]),
                              ("A", [{"world_switch": False}])):
                for cfg in cfgs:   # P: the switch off (unrouted), then F2.19's B knobs (a separate call)
                    call("POST", P, cfg)
                time.sleep(4.0)
                im[lab] = shot(out, f"f73_{name}_{lab}.png")
            n = im["A"].shape[0] * im["A"].shape[1]
            r = {"px": n, "D1_switch_vs_F219B_px": ndiff(im["S"], im["P"]), "D1_switch_vs_switch_px": ndiff(im["S"], im["S2"]),
                 "D2_today_vs_switch_pct": round(100.0 * ndiff(im["A"], im["S"]) / n, 2)}
            res[name] = r
            print(name, json.dumps(r), flush=True)
    finally:
        call("POST", "/api/debug/tonemap", {"curve": 1})
        call("POST", "/api/debug/foliage", {"windStrength": 0.05})
        call("POST", "/api/debug/gi", {"enabled": True})
        call("POST", P, {"world_switch": False})
    json.dump(res, open(os.path.join(out, "f73_gate.json"), "w"), indent=1)


if __name__ == "__main__":
    main()
