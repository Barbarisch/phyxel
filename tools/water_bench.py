#!/usr/bin/env python3
"""Water v4 benches (docs/WaterRethink.md WP0 Step 0): author, verify and pose the three
WaterBench worlds over the engine's HTTP API. Rig-as-code: the geometry lives in each project's
game.json `waterBench` block and this script only executes it, so the JSON is the single source.

    python tools/water_bench.py build-basin [--force]   # author + save + verify the Basin rig
    python tools/water_bench.py verify basin|coast|river # definition-of-done; appends evidence
    python tools/water_bench.py vantage <bench> <name>   # pose the camera at a pinned vantage, read back
    python tools/water_bench.py shot <bench> <name> <dst.png>

Engine target: the bench project's own port (.phyxel/config.json) unless --url is given. Launch
with `phyxel up <project dir>`; never on 8090 if something else holds it.

Discipline (docs/FeatureDesignKeys.md): verify the WORLD, not the API response - fills/clears are
async and report no count, so every build step is followed by a probe of the voxels themselves.
Evidence goes to docs/evidence/water_v4_benches.json keyed by bench, with git HEAD + timestamp.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROJECTS = Path(os.environ.get("USERPROFILE", os.path.expanduser("~"))) / "Documents" / "PhyxelProjects"
BENCHES = {"basin": "WaterBench_Basin", "coast": "WaterBench_Coast", "river": "WaterBench_River"}
EVIDENCE = ROOT / "docs" / "evidence" / "water_v4_benches.json"


# ---------------------------------------------------------------- HTTP ------------------------
class Api:
    def __init__(self, url):
        self.url = url.rstrip("/")

    def _req(self, method, path, body=None, timeout=60):
        data = None if body is None else json.dumps(body).encode()
        req = urllib.request.Request(self.url + path, data=data, method=method,
                                     headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read().decode() or "{}")

    def post(self, path, body=None, timeout=60):
        return self._req("POST", path, body if body is not None else {}, timeout)

    def get(self, path, timeout=60):
        return self._req("GET", path, None, timeout)

    def wait_status(self, max_s=600):
        t0 = time.time()
        while time.time() - t0 < max_s:
            try:
                s = self.get("/api/status", timeout=5)
                if s:
                    return s
            except Exception:
                pass
            time.sleep(2.0)
        raise SystemExit(f"engine at {self.url} never answered /api/status in {max_s}s")

    def debug(self, name, body=None, timeout=120):
        return self.post(f"/api/debug/{name}", body or {}, timeout)


def world_job(api, path, box, **extra):
    """/api/world/fill and /api/world/clear are async: wait for the job to leave the queue."""
    r = api.post(path, dict(box, **extra))
    aid = r.get("async_id")
    res = r
    for _ in range(600):
        if aid is None:
            break
        res = api.get(f"/api/async/{aid}")
        if res.get("status") not in ("pending", "running", "accepted", "processing", "queued"):
            break
        time.sleep(0.5)
    return res.get("result") or res


def surface_y(api, x, z):
    # max_y defaults to 255 and Mountains terrain exceeds it: a None here then means "higher than
    # 255", not "unloaded" (cost an hour on the River bench, 2026-10-07). Ask for the full range.
    r = api.get(f"/api/world/terrain_height?x={x}&z={z}&min_y=-64&max_y=1024")
    return r.get("surface_y")


def solid_top_set(api, x1, z1, x2, z2, y):
    r = api.get(f"/api/world/scan?x1={x1}&y1={y}&z1={z1}&x2={x2}&y2={y}&z2={z2}", timeout=120)
    return {(v["x"], v["z"]) for v in r.get("voxels", [])}


def camera_get(api):
    c = api.get("/api/camera")
    pos = c.get("position") or {k: c.get(k) for k in ("x", "y", "z")}
    return {"x": pos.get("x"), "y": pos.get("y"), "z": pos.get("z"),
            "yaw": c.get("yaw"), "pitch": c.get("pitch")}


def camera_set(api, v):
    api.post("/api/camera", {"mode": "free", "position": {"x": v["x"], "y": v["y"], "z": v["z"]},
                             "yaw": v["yaw"], "pitch": v["pitch"]})
    time.sleep(0.3)
    return camera_get(api)


def pose_ok(want, got, pos_tol=0.5, ang_tol=1.0):
    try:
        return (abs(want["x"] - got["x"]) <= pos_tol and abs(want["y"] - got["y"]) <= pos_tol
                and abs(want["z"] - got["z"]) <= pos_tol
                and abs(((want["yaw"] - got["yaw"] + 180) % 360) - 180) <= ang_tol
                and abs(want["pitch"] - got["pitch"]) <= ang_tol)
    except (TypeError, KeyError):
        return False


# ---------------------------------------------------------------- bench plumbing -------------
def project_dir(bench):
    d = PROJECTS / BENCHES[bench]
    if not (d / "game.json").is_file():
        raise SystemExit(f"{d} has no game.json - scaffold it with `phyxel new {BENCHES[bench]}`")
    return d


def project_url(bench, override):
    if override:
        return override
    cfg = project_dir(bench) / ".phyxel" / "config.json"
    port = json.loads(cfg.read_text(encoding="utf-8")).get("apiPort", 8090) if cfg.is_file() else 8090
    return f"http://localhost:{port}"


def load_def(bench):
    return json.loads((project_dir(bench) / "game.json").read_text(encoding="utf-8-sig"))


def vantage(gdef, name):
    for v in gdef.get("testVantages", {}).get("vantages", []):
        if v["name"] == name:
            return v
    raise SystemExit(f"no vantage '{name}' in game.json (have: "
                     f"{[v['name'] for v in gdef.get('testVantages', {}).get('vantages', [])]})")


def git_head():
    try:
        return subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT).decode().strip()
    except Exception:
        return "unknown"


def record_evidence(bench, result):
    EVIDENCE.parent.mkdir(parents=True, exist_ok=True)
    data = json.loads(EVIDENCE.read_text(encoding="utf-8")) if EVIDENCE.is_file() else {}
    result = dict(result, timestamp=time.strftime("%Y-%m-%dT%H:%M:%S"), git_head=git_head())
    data[bench] = result
    EVIDENCE.write_text(json.dumps(data, indent=2), encoding="utf-8")
    print(f"evidence -> {EVIDENCE.relative_to(ROOT)} [{bench}]")


# ---------------------------------------------------------------- BASIN rig ------------------
def basin_expected_tops(spec):
    """Expected solid-top Y per (x,z) column of the one-chunk rig, from the game.json spec."""
    sx, sz, sy = spec["slab"]["x"], spec["slab"]["z"], spec["slab"]["y"]
    tops = {(x, z): sy[1] for x in range(sx[0], sx[1] + 1) for z in range(sz[0], sz[1] + 1)}
    a = spec["basinA"]
    for step in a["ramp"]:
        for x in range(step["x"][0], step["x"][1] + 1):
            for z in range(a["z"][0], a["z"][1] + 1):
                tops[(x, z)] = step["floorTop"]
    for x in range(a["flat"]["x"][0], a["flat"]["x"][1] + 1):
        for z in range(a["z"][0], a["z"][1] + 1):
            tops[(x, z)] = a["flat"]["floorTop"]
    c = spec["channel"]
    for x in range(c["x"][0], c["x"][1] + 1):
        for z in range(c["z"][0], c["z"][1] + 1):
            tops[(x, z)] = c["floorTop"]
    b = spec["basinB"]
    for x in range(b["x"][0], b["x"][1] + 1):
        for z in range(b["z"][0], b["z"][1] + 1):
            tops[(x, z)] = b["floorTop"]
    return tops


def build_basin(api, gdef, force):
    spec = gdef["waterBench"]
    assert spec["rig"] == "basin"
    sx, sz, sy = spec["slab"]["x"], spec["slab"]["z"], spec["slab"]["y"]
    existing = solid_top_set(api, sx[0], sz[0], sx[1], sz[1], sy[1])
    if existing and not force:
        raise SystemExit(f"slab top already has {len(existing)} solid cells at y={sy[1]}; "
                         "pass --force to rebuild (clears the chunk column 0..31 first)")
    if existing:
        print("clearing old rig ...")
        world_job(api, "/api/world/clear", {"x1": sx[0], "y1": 0, "z1": sz[0], "x2": sx[1], "y2": 31, "z2": sz[1]})
    print("filling slab ...")
    r = world_job(api, "/api/world/fill",
                  {"x1": sx[0], "y1": sy[0], "z1": sz[0], "x2": sx[1], "y2": sy[1], "z2": sz[1]},
                  material=spec["slab"]["material"])
    print("  ", json.dumps(r)[:160])
    tops = basin_expected_tops(spec)
    # Carve: every column whose expected top is below the slab top loses y (top+1 .. slabTop).
    # Group by (top, x-range, z-range) using the spec blocks so each clear is one box.
    a = spec["basinA"]
    boxes = []
    for step in a["ramp"]:
        boxes.append((step["x"], a["z"], step["floorTop"]))
    boxes.append((a["flat"]["x"], a["z"], a["flat"]["floorTop"]))
    boxes.append((spec["channel"]["x"], spec["channel"]["z"], spec["channel"]["floorTop"]))
    boxes.append((spec["basinB"]["x"], spec["basinB"]["z"], spec["basinB"]["floorTop"]))
    for xr, zr, top in boxes:
        r = world_job(api, "/api/world/clear",
                      {"x1": xr[0], "y1": top + 1, "z1": zr[0], "x2": xr[1], "y2": sy[1], "z2": zr[1]})
        print(f"   carve x{xr} z{zr} -> top {top}: {json.dumps(r)[:120]}")
    print("saving ...", api.post("/api/world/save", {"all": True}, timeout=180))
    return tops


def verify_basin(api, gdef):
    spec = gdef["waterBench"]
    sx, sz, sy = spec["slab"]["x"], spec["slab"]["z"], spec["slab"]["y"]
    tops = basin_expected_tops(spec)
    bad = []
    # 1. Every column's solid top, from the world. Derived from one scan per layer between the
    #    lowest carved floor and the slab top (5 scans) instead of 1024 terrain_height probes:
    #    the per-column path took 35 min on a Debug engine (2026-10-07), the scans take seconds.
    #    Below the lowest floor the slab is solid by construction (checked by the slab fill count).
    lowest = min(tops.values())
    layers = {y: solid_top_set(api, sx[0], sz[0], sx[1], sz[1], y) for y in range(lowest, sy[1] + 1)}
    wrong = []
    for (x, z), want in sorted(tops.items()):
        got = max((y for y in layers if (x, z) in layers[y]), default=None)
        if got != want:
            wrong.append(((x, z), want, got))
        # a carved column must be AIR above its floor, not merely solid at the floor
        for y in range(want + 1, sy[1] + 1):
            if (x, z) in layers[y]:
                wrong.append(((x, z), want, f"solid at y={y}"))
                break
    if wrong:
        bad.append(f"{len(wrong)} columns with wrong solid top (first: {wrong[:5]})")
    # 2. The slab top layer as a set (catches stray holes a column probe would miss).
    want_top = {k for k, v in tops.items() if v == sy[1]}
    got_top = solid_top_set(api, sx[0], sz[0], sx[1], sz[1], sy[1])
    if got_top != want_top:
        bad.append(f"slab top y={sy[1]}: {len(want_top - got_top)} missing, {len(got_top - want_top)} extra")
    # 3. Nothing above the slab (an open-sky rig; the Flat-world lid trap, Water.md sec. 8 #2).
    above = solid_top_set(api, sx[0], sz[0], sx[1], sz[1], sy[1] + 1)
    if above:
        bad.append(f"{len(above)} solid cells above the slab at y={sy[1] + 1}")
    # 4. Bone dry: water disabled -> WaterManager may be absent; a present one must hold 0 mass.
    ws = api.debug("water_stats")
    mass = ws.get("total_mass", 0.0) if "error" not in ws else 0.0
    if mass > 0:
        bad.append(f"water present at boot: total_mass {mass}")
    # 5. Pinned vantages read back.
    poses = {}
    for v in gdef["testVantages"]["vantages"]:
        got = camera_set(api, v)
        poses[v["name"]] = {"ok": pose_ok(v, got), "got": got}
        if not poses[v["name"]]["ok"]:
            bad.append(f"vantage {v['name']} did not take: {got}")
    result = {"columns_checked": len(tops), "wrong_columns": len(wrong), "slab_top_ok": got_top == want_top,
              "above_slab": len(above), "total_mass": mass, "water_stats": ws, "vantages": poses,
              "status": api.get("/api/status"), "ok": not bad}
    return result, bad


# ---------------------------------------------------------------- streaming benches ----------
def wait_spans_stable(api, rect, max_s=240):
    """Spans exist only in RESIDENT chunks; after posing the camera, wait for the count to settle."""
    x1, z1, x2, z2 = rect
    last, same, t0 = None, 0, time.time()
    r = {}
    while time.time() - t0 < max_s:
        r = api.debug("water_spans_stored", {"x1": x1, "z1": z1, "x2": x2, "z2": z2})
        n = r.get("count", r.get("spans"))
        if n == last:
            same += 1
            if same >= 4:
                break
        else:
            same = 0
        last = n
        time.sleep(3.0)
    return r


def verify_streaming(api, gdef, bench):
    spec = gdef["waterBench"]
    bad = []
    status = api.get("/api/status")
    bake = api.debug("water_bake_info")
    if "error" in bake:
        bad.append(f"no hydrology bake: {bake}")
    # Pose at the first vantage so the shore/gorge streams in, then measure what is there.
    vantages = gdef["testVantages"]["vantages"]
    poses, heights = {}, {}
    for v in vantages:
        got = camera_set(api, v)
        poses[v["name"]] = {"ok": pose_ok(v, got), "got": got}
        if not poses[v["name"]]["ok"]:
            bad.append(f"vantage {v['name']} did not take: {got}")
    camera_set(api, vantages[0])
    time.sleep(5.0)
    for v in vantages:
        sy = surface_y(api, int(v["x"]), int(v["z"]))
        heights[v["name"]] = sy
        if sy is not None and v["y"] <= sy:
            bad.append(f"vantage {v['name']} is underground (y {v['y']} <= surface {sy})")
    # Spans live only in RESIDENT chunks, so a count taken from a vantage elsewhere depends on
    # where the camera happens to be (2026-10-07: the Coast rect read 66,004 from one pose and
    # 44,335 with 21,669 columns unloaded from another). Pose at the rect's centre, high enough to
    # clear the terrain, wait for residency, and only then count — and refuse a count with
    # unloaded columns (Water.md sec. 8 #1: a zero over unloaded ground is not a result).
    x1, z1, x2, z2 = spec["spanRect"]
    cx, cz = (x1 + x2) // 2, (z1 + z2) // 2
    sy_c = surface_y(api, cx, cz)
    camera_set(api, {"x": cx, "y": (sy_c if sy_c is not None else 16) + 60, "z": cz, "yaw": 90, "pitch": -60})
    validate = {}
    t0 = time.time()
    while time.time() - t0 < 300:
        validate = api.debug("water_validate", {"x1": x1, "z1": z1, "x2": x2, "z2": z2, "maxY": 400})
        if validate.get("unloaded", 1) == 0:
            break
        time.sleep(5.0)
    if validate.get("unloaded", 1) != 0:
        bad.append(f"span rect {spec['spanRect']} never became fully resident: unloaded {validate.get('unloaded')}")
    spans = wait_spans_stable(api, spec["spanRect"])
    n = spans.get("count", spans.get("spans", 0)) or 0
    exp = spec.get("expectSpans", {})
    if n < exp.get("min", 0):
        bad.append(f"spans in rect {spec['spanRect']}: {n} < expected min {exp.get('min')}")
    if "max" in exp and n > exp["max"]:
        bad.append(f"spans in rect {spec['spanRect']}: {n} > expected max {exp['max']}")
    if "topEquals" in exp and n and spans.get("max_top") is not None:
        if abs(spans["max_top"] - exp["topEquals"]) > 1e-3 or abs(spans.get("min_top", spans["max_top"]) - exp["topEquals"]) > 1e-3:
            bad.append(f"span tops {spans.get('min_top')}..{spans.get('max_top')} != {exp['topEquals']}")
    result = {"status": status, "bake_info": bake, "vantages": poses, "surface_y": heights,
              "spans_stored": spans, "water_validate": validate}
    if bench == "river":
        g = spec.get("trunk") or spec["gorge"]
        riv = api.debug("water_find_river", {"x": g["x"], "z": g["z"], "radius": g["searchRadius"],
                                              "step": 32, "min_order": g["minOrder"], "count": 5})
        result["find_river"] = riv
        sites = riv.get("rivers") or []  # water_find_river returns {"rivers": [...]} (2026-10-07)
        if not sites:
            bad.append(f"no order>={g['minOrder']} river within {g['searchRadius']} of ({g['x']},{g['z']}): {riv}")
        if "riverColumnRect" in spec:
            # WP1's red baseline: spans on the river column itself (the big rect also holds lakes).
            rx1, rz1, rx2, rz2 = spec["riverColumnRect"]
            rc = api.debug("water_spans_stored", {"x1": rx1, "z1": rz1, "x2": rx2, "z2": rz2})
            result["river_column_spans"] = rc
            print(f"river column rect {spec['riverColumnRect']}: spans = {rc.get('spans')} "
                  f"(expected today: {spec.get('expectRiverColumnSpans', {}).get('today')})")
    if bench == "coast":
        p = gdef.get("player", {}).get("position")
        if p:
            tl = api.debug("water_table_level", {"x": int(p["x"]), "z": int(p["z"])})
            result["player_table"] = tl
            if tl.get("wet"):
                bad.append(f"player spawn {p} is over water: {tl}")
    result["ok"] = not bad
    return result, bad


# ---------------------------------------------------------------- main -----------------------
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["build-basin", "verify", "vantage", "shot", "refshots"])
    ap.add_argument("bench", nargs="?", help="basin | coast | river (implied for build-basin)")
    ap.add_argument("name", nargs="?", help="vantage name (vantage/shot)")
    ap.add_argument("dst", nargs="?", help="destination png (shot)")
    ap.add_argument("--url", default=os.environ.get("PHYXEL_API_URL"))
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()

    bench = "basin" if args.cmd == "build-basin" else args.bench
    if bench not in BENCHES:
        raise SystemExit(f"bench must be one of {list(BENCHES)}")
    gdef = load_def(bench)
    api = Api(project_url(bench, args.url))
    print(f"[{bench}] engine {api.url} ...", end=" ", flush=True)
    api.wait_status()
    print("up")

    if args.cmd == "build-basin":
        build_basin(api, gdef, args.force)
        result, bad = verify_basin(api, gdef)
        record_evidence(bench, result)
        if bad:
            raise SystemExit("BASIN RIG WRONG: " + "; ".join(bad))
        print(f"basin rig verified: {result['columns_checked']} columns, slab top exact, dry, "
              f"{sum(p['ok'] for p in result['vantages'].values())}/{len(result['vantages'])} vantages")
    elif args.cmd == "verify":
        result, bad = verify_basin(api, gdef) if bench == "basin" else verify_streaming(api, gdef, bench)
        record_evidence(bench, result)
        print(json.dumps({k: v for k, v in result.items() if k not in ("status", "water_stats")}, indent=1)[:4000])
        if bad:
            raise SystemExit("VERIFY FAILED: " + "; ".join(bad))
        print(f"{bench}: definition of done MET")
    elif args.cmd == "refshots":
        # The look-first rule's reference set: one HUD-free capture per pinned vantage, named
        # <bench>_<vantage>.png in `name` (a directory). Re-taken only deliberately; every later
        # visual change is judged against these at the same pose ("is it prettier than this?").
        outdir = Path(args.name or (ROOT / "docs" / "evidence" / "water_v4_refs"))
        outdir.mkdir(parents=True, exist_ok=True)
        try:
            api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
        except Exception as e:
            print(f"   (editor_panels unavailable on this engine: {e})")
        for v in gdef["testVantages"]["vantages"]:
            got = camera_set(api, v)
            if not pose_ok(v, got):
                raise SystemExit(f"vantage {v['name']} did not take: {got}")
            time.sleep(8.0)                                   # let streaming/remesh settle a little
            r = api.get("/api/screenshot", timeout=90)
            path = r.get("path") or next((s.get("path") for s in r.get("screenshots", [])), None)
            if path and not os.path.isabs(path):
                path = os.path.join(ROOT, path)
            if not path or not os.path.exists(path):
                raise SystemExit(f"no screenshot at {v['name']}: {r}")
            dst = outdir / f"{bench}_{v['name']}.png"
            shutil.copy(path, dst)
            print("->", dst.relative_to(ROOT) if str(dst).startswith(str(ROOT)) else dst)
    elif args.cmd in ("vantage", "shot"):
        v = vantage(gdef, args.name)
        got = camera_set(api, v)
        print(json.dumps({"want": v, "got": got, "ok": pose_ok(v, got)}, indent=1))
        if args.cmd == "shot":
            # Reference captures must not carry editor panels (the Item Equipper sat over every
            # 2026-10-07 capture). The route exists from 2026-10-08; older engines just skip it.
            try:
                api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
            except Exception as e:
                print(f"   (editor_panels unavailable on this engine: {e})")
            time.sleep(1.0)
            r = api.get("/api/screenshot", timeout=90)
            path = r.get("path") or next((s.get("path") for s in r.get("screenshots", [])), None)
            if path and not os.path.isabs(path):
                path = os.path.join(ROOT, path)
            if not path or not os.path.exists(path):
                raise SystemExit(f"no screenshot: {r}")
            shutil.copy(path, args.dst)
            print("->", args.dst)


if __name__ == "__main__":
    main()
