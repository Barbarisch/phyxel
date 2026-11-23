"""F7.27 live (docs/evidence/voxfound_f7_prediction.txt D1-D3 + opt-out): the switched world as the default.
Run on a COPY of a real project (unmodified game.json = default; mode "optout" = the copy with voxfound disabled).
Usage: voxfound_f727_live.py <project-name> <default|optout>. Every artifact: the project's own saved / generated world
(today's chunks) converted by VoxFound::convertPxb2 (fixed worlds) or the engine generator (streaming worlds)."""
import json, os, shutil, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")


def call(m, p, b=None, timeout=600):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(r, timeout=timeout) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        return {"http_status": e.code, "body": e.read().decode()[:300]}
    except Exception as e:
        return {"exception": str(e)}


def main():
    name, mode = sys.argv[1], sys.argv[2]
    out = {"project": name, "mode": mode}
    rep, t0 = {}, time.time()
    while time.time() - t0 < 900:
        st = call("POST", P, {"boot_status": True})
        rep = st.get("report") or {}
        if rep or (mode == "optout" and time.time() - t0 > 20):
            break
        time.sleep(3)
    out["boot"] = {k: rep.get(k) for k in ("booted", "source", "default", "build_error", "total_ms", "stream", "region_request")}
    if out["boot"].get("region_request"):
        out["boot"]["region_request"] = out["boot"]["region_request"].get("region")
    status = call("POST", P, {"stream_status": True})
    out["switched"] = status.get("switched")
    out["build_timing"] = rep.get("build_timing")
    out["source_fallback"] = rep.get("source_fallback")
    if mode == "default" and rep.get("source") in ("today", "world_db"):
        today = call("POST", P, {"today_census": True}).get("today_voxels_by_material", {})
        region = call("POST", P, {"census": True}).get("census", {})
        rows, worst = {}, 0.0
        for m, v in today.items():
            if v < 1000:
                continue
            got = region.get(m, 0)
            rel = abs(got - v) / v
            worst = max(worst, rel)
            rows[m] = [round(v), got, round(rel, 4)]
        tt = sum(today.values())
        tr = sum(v for k, v in region.items() if k != "air")
        out["D2"] = {"materials": rows, "worst_rel": round(worst, 4), "total_today": round(tt), "total_region": tr,
                     "total_rel": round(abs(tr - tt) / tt, 4) if tt else None}
    time.sleep(3)
    r = call("GET", "/api/screenshot")
    if r.get("path"):
        src = os.path.join(REPO, r["path"])
        dst = os.path.join(EVID, f"f727_{name}_{mode}.png")
        if os.path.exists(src):
            shutil.copyfile(src, dst)
            out["shot"] = os.path.relpath(dst, REPO)
    print(json.dumps(out))
    with open(os.path.join(EVID, "voxfound_f727_live.jsonl"), "a") as fh:
        fh.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
