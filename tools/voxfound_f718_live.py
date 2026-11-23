"""F7.18 live (docs/evidence/voxfound_f7_prediction.txt S1-S3): water spans off today's Chunk objects, judged by the
step-7 gate (today's streaming OFF while switched). Project VoxFoundWater (the coast bench booted switched, streaming).
Modes: red (span_store_stub: readers walk today's chunks) / green.
Every artifact: WorldGenerator Perlin s20260804's hydrology bake (sea level 16) + the switched world's 1/8 m terrain."""
import json, os, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")
X0, Z0, Z1, STEP = 165.0, 600.0, 920.0, 16.0


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(r, timeout=600) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        return {"http_status": e.code, "body": e.read().decode()[:300]}


def status():
    return call("POST", P, {"stream_status": True})


def in_region(x, z, rg):
    f, t = rg["from"], rg["to"]
    return f[0] * 32 <= x < (t[0] + 1) * 32 and f[2] * 32 <= z < (t[2] + 1) * 32


def cam(x, z):
    return call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": 60, "z": z}, "yaw": 90, "pitch": -40})


def spans_in(rect):
    r = call("POST", "/api/debug/water_spans_stored", {"x1": rect[0], "z1": rect[1], "x2": rect[2], "z2": rect[3]})
    return {k: r.get(k) for k in ("holder", "chunks_loaded", "chunks_with_spans", "spans", "total_depth", "min_top", "max_top")}


def rect_of(rg):
    f, t = rg["from"], rg["to"]
    return [f[0] * 32, f[2] * 32, (t[0] + 1) * 32 - 1, (t[2] + 1) * 32 - 1]


def main():
    mode = sys.argv[1]
    out = {"mode": mode}
    for _ in range(300):
        if call("POST", P, {"boot_status": True}).get("report"):
            break
        time.sleep(2)
    cam(X0, Z0)
    time.sleep(8)   # today's streaming lands around the camera (it is still ON here)
    rg0 = status()["region"]
    r0 = rect_of(rg0)
    out["region0"] = rg0
    if mode != "green":
        call("POST", P, {"span_store_stub": True})
    out["S1"] = call("POST", P, {"span_check": True})
    out["region0_spans"] = spans_in(r0)
    # the gate: today's streaming off, then the walk north into the sea
    out["gate"] = call("POST", P, {"today_stream_off": True})
    walk = []
    z = Z0
    while z <= Z1:
        cam(X0, z)
        t0 = time.time()
        time.sleep(0.4)
        while True:
            st = status()
            if in_region(X0, z, st["region"]) or time.time() - t0 > 60:
                break
            time.sleep(0.25)
        walk.append({"z": z, "in_region": in_region(X0, z, st["region"]), "shifts": st["shifts"]})
        z += STEP
    out["walk_all_in_region"] = all(w["in_region"] for w in walk)
    out["walk_shifts"] = walk[-1]["shifts"]
    time.sleep(3)
    rg1 = status()["region"]
    out["region1"] = rg1
    r1 = rect_of(rg1)
    new_rect = [r1[0], max(r1[1], r0[3] + 1), r1[2], r1[3]]   # the chunks streamed in after the stop
    out["S2_new_rect"] = new_rect
    out["S2_new_spans"] = spans_in(new_rect) if new_rect[1] <= new_rect[3] else None
    out["S2_check"] = call("POST", P, {"span_check": True})
    time.sleep(4)   # the span grid's cooldown
    g = call("POST", "/api/debug/water_render_grid", {"x1": int(X0) - 64, "z1": int(Z1) - 64, "x2": int(X0) + 63, "z2": int(Z1) + 63})
    out["S2_render"] = {k: g.get(k) for k in ("source", "wet", "dry", "off_grid", "min_level", "max_level")}
    if mode == "green":
        # S1 / S3 over WATER (the start region is dry land, so the first runs' S1 / S3 compared empty lists --
        # vacuous): today's streaming back on at the sea, landed; store vs today's chunks; today's spans before / after
        # switch-off over the wet end rect
        call("POST", P, {"today_stream_off": False})
        call("POST", P, {"stream": False})
        time.sleep(30)
        out["S1_wet"] = call("POST", P, {"span_check": True})
        call("POST", P, {"span_store_stub": True})
        out["S3_before_today"] = spans_in(r1)
        call("POST", P, {"span_store_stub": False})
        out["S3_store"] = spans_in(r1)
        call("POST", P, {"world_switch": False})
        out["S3_after_off"] = spans_in(r1)
        out["S3_equal"] = out["S3_after_off"].get("spans") == out["S3_before_today"].get("spans") and \
            out["S3_after_off"].get("total_depth") == out["S3_before_today"].get("total_depth")
    else:
        call("POST", P, {"span_store_stub": False})
        call("POST", P, {"today_stream_off": False})
    print(json.dumps(out))
    with open(os.path.join(EVID, "voxfound_f718_live.jsonl"), "a") as f:
        f.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
