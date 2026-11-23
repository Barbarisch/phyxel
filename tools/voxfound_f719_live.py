"""F7.19 live (docs/evidence/voxfound_f7_prediction.txt E1-E3): water span edits on the switched world, saved.
Project VoxFoundWaterSave (VoxFoundWater with boot load_saved true). Modes: red (span_edit_stub) / green, each followed
by `reload <mode>` after a relaunch. Edits through the engine's voxel API (/api/world/clear, /api/world/fill -- routed
to the switched world). Every artifact: WorldGenerator Perlin s20260804 hydrology + the switched 1/8 m terrain + those
two 1 m edits."""
import json, os, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")
OUT = os.path.join(EVID, "voxfound_f719_live.jsonl")
X0, Z0, Z1, STEP = 165.0, 600.0, 920.0, 16.0


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(r, timeout=600) as resp:
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
    return {"error": "async timeout"}


def in_region(x, z, rg):
    f, t = rg["from"], rg["to"]
    return f[0] * 32 <= x < (t[0] + 1) * 32 and f[2] * 32 <= z < (t[2] + 1) * 32


def walk_to_sea():
    z = Z0
    while z <= Z1:
        call("POST", "/api/camera", {"mode": "free", "position": {"x": X0, "y": 60, "z": z}, "yaw": 90, "pitch": -40})
        t0 = time.time()
        time.sleep(0.4)
        while not in_region(X0, z, call("POST", P, {"stream_status": True})["region"]) and time.time() - t0 < 60:
            time.sleep(0.25)
        z += STEP
    time.sleep(3)


def column(x, z):
    r = call("POST", "/api/debug/water_spans_stored", {"x1": x, "z1": z, "x2": x, "z2": z})
    d, t = r.get("total_depth") or 0.0, r.get("max_top") or 0.0
    return {"spans": r.get("spans"), "depth": d, "top": t, "holder": r.get("holder")}


def main():
    mode = sys.argv[1]
    out = {"mode": mode}
    for _ in range(300):
        if call("POST", P, {"boot_status": True}).get("report"):
            break
        time.sleep(2)
    out["gate"] = call("POST", P, {"today_stream_off": True})
    walk_to_sea()
    cx, cz = int(X0), int(Z1)
    if mode.startswith("reload"):
        prev = [json.loads(l) for l in open(OUT) if l.strip()]
        before = [p for p in prev if p["mode"] == mode.split()[-1]][-1]
        out["E3_column"] = column(cx, cz)
        out["E3_expected"] = before["after_E2"]
        out["E3_equal"] = out["E3_column"]["depth"] == before["after_E2"]["depth"] and \
            out["E3_column"]["spans"] == before["after_E2"]["spans"]
        out["check"] = call("POST", P, {"span_check": True})
    else:
        if mode == "red":
            out["stub"] = call("POST", P, {"span_edit_stub": True})
        c0 = column(cx, cz)
        out["C"] = [cx, cz]
        out["before"] = c0
        bottom = c0["top"] - c0["depth"]   # one span in an open-sea column
        by = int(round(bottom)) - 1        # the cube directly under the bed
        out["E1_clear"] = {k: v for k, v in wait_async(call("POST", "/api/world/clear", {"x1": cx, "y1": by, "z1": cz, "x2": cx, "y2": by, "z2": cz})).items() if k in ("status", "error", "result")}
        out["after_E1"] = column(cx, cz)
        out["E1_pass"] = abs(out["after_E1"]["depth"] - (c0["depth"] + 1.0)) < 1e-3
        my = int(c0["top"]) - 2            # a cube inside the water
        out["E2_y"] = my
        out["E2_fill"] = {k: v for k, v in wait_async(call("POST", "/api/world/fill", {"x1": cx, "y1": my, "z1": cz, "x2": cx, "y2": my, "z2": cz, "material": "Stone"})).items() if k in ("status", "error", "result")}
        out["after_E2"] = column(cx, cz)
        out["E2_pass"] = abs(out["after_E2"]["depth"] - (out["after_E1"]["depth"] - 1.0)) < 1e-3
        out["check"] = call("POST", P, {"span_check": True})
        out["save"] = (call("POST", "/api/world/save", {}).get("voxfound") or {}).get("saved_chunks")
        if mode == "red":
            call("POST", P, {"span_edit_stub": False})
    print(json.dumps(out))
    with open(OUT, "a") as f:
        f.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
