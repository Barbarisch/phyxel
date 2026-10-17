"""WaterCore.md 25.36 - the dam & pond bench: the creek (25.24) fills a pond, spills, is dammed, and the dam is broken.

  --build : carve the pond (x 112-117, z 29-34, floor y 14.0), the creek's last metres (x 110-111) and the spill channel
            (x 118-127, z 31-32, floor y 15.0) into WaterBench_Creek's world, and save it
  --run   : one column region over the creek + pond + spill (x 10-127, z 27-36, y 13-17), the spring at the head, outfalls
            at the slab's edge; then three phases, polled every 0.5 s and recorded:
              FILL  until the outfall carries the inflow (out/in >= 0.98 over 30 s) or --fill-max seconds
              DAM   Stone subcubes across the creek at x 60 (crest 1/3 m above the bed), --dam-seconds
              BREAK the dam removed, --break-seconds
Predictions (25.36): pond rise rate Q / A = 0.2 / 36 = 5.6 mm/s within 10 %; out/in 1.00 +- 0.02 once spilling; the
pond's inflow < 10 % of Q within 20 s of the dam; the inflow ABOVE Q for a while after the break; the ledger never moves
(|error| < 1e-5 m^3) and does not change across an edit (edits never create water).

    python tools/water_dam_pond.py --build
    python tools/water_dam_pond.py --run [--q 0.2] [--record]
Writes docs/evidence/water_core_h1/dam_pond.json (+ dam_pond.mp4 with --record).
"""
import argparse, json, math, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set, world_job
import water_creek_bench as cb

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"
POND = (112, 117, 29, 34)          # x0, x1, z0, z1 (voxels)
POND_FLOOR, SPILL_FLOOR = 14.0, 15.0
SPILL = (118, 127, 31, 32)
REGION = {"x1": 10, "y1": 13, "z1": 27, "x2": 127, "y2": 17, "z2": 36}
DAM_X = 60
POND_AREA = (POND[1] - POND[0] + 1) * (POND[3] - POND[2] + 1)


def build(api):
    # the creek's last metres, carved like the creek (cb.bed_top: the line floored to the carve's grid - 1/9 m since 26.15;
    # gravel subcubes for whole thirds, microcubes for the rest, as water_creek_bench.py --build)
    world_job(api, "/api/world/clear", {"x1": 110, "y1": 15, "z1": cb.Z0, "x2": 111, "y2": 15, "z2": cb.Z1})
    subs, micros = [], []
    for x in (110, 111):
        nine = int(round((cb.bed_top(x) - 15.0) * 9))
        layers, rem = nine // 3, nine % 3
        for z in range(cb.Z0, cb.Z1 + 1):
            for sx in range(3):
                for sz in range(3):
                    for sy in range(layers):
                        subs.append({"x": x, "y": 15, "z": z, "sx": sx, "sy": sy, "sz": sz, "material": "Gravel"})
                    for my in range(rem):
                        for mx in range(3):
                            for mz in range(3):
                                micros.append({"x": x, "y": 15, "z": z, "sx": sx, "sy": layers, "sz": sz, "mx": mx, "my": my, "mz": mz, "material": "Gravel"})
    if subs: api.post("/api/world/subcubes/batch", {"subcubes": subs})
    if micros: api.post("/api/world/microcubes/batch", {"microcubes": micros})
    world_job(api, "/api/world/clear", {"x1": POND[0], "y1": 14, "z1": POND[2], "x2": POND[1], "y2": 15, "z2": POND[3]})
    world_job(api, "/api/world/fill", {"x1": POND[0], "y1": 13, "z1": POND[2], "x2": POND[1], "y2": 13, "z2": POND[3], "material": "Gravel"})
    world_job(api, "/api/world/clear", {"x1": SPILL[0], "y1": 15, "z1": SPILL[2], "x2": SPILL[1], "y2": 15, "z2": SPILL[3]})
    world_job(api, "/api/world/fill", {"x1": SPILL[0], "y1": 14, "z1": SPILL[2], "x2": SPILL[1], "y2": 14, "z2": SPILL[3], "material": "Gravel"})
    print("built: pond + spill; save:", str(api.post("/api/world/save", {}))[:100])


def verify(api):
    def top(x, z): return api.get(f"/api/world/terrain_height?x={x}&z={z}&min_y=0&max_y=64").get("surface_y")
    bad = []
    if top(114, 31) != 13: bad.append(f"pond floor top {top(114, 31)} (want 13)")
    if top(122, 31) != 14: bad.append(f"spill floor top {top(122, 31)} (want 14)")
    if top(114, 27) != 15: bad.append(f"pond rim top {top(114, 27)} (want 15)")
    return bad


def dam_subcubes():
    top = math.floor(cb.line(DAM_X + 0.5) * 3 + 1e-4) / 3.0      # the creek's gravel at the dam
    sy = int(round((top - 15.0) * 3))                             # the first free subcube layer = the dam's
    return [{"x": DAM_X, "y": 15, "z": z, "sx": sx, "sy": sy, "sz": sz} for z in range(cb.Z0, cb.Z1 + 1) for sx in range(3) for sz in range(3)], top + 1.0 / 3.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--build", action="store_true")
    ap.add_argument("--run", action="store_true")
    ap.add_argument("--q", type=float, default=0.2)
    ap.add_argument("--fill-max", type=float, default=600.0)
    ap.add_argument("--dam-seconds", type=float, default=240.0)
    ap.add_argument("--break-seconds", type=float, default=150.0)
    ap.add_argument("--record", action="store_true")
    ap.add_argument("--pond", action="store_true", help="26.22 P3c: the bench as POND water (one sparse GPU volume), not a column region")
    ap.add_argument("--sweeps", type=int, default=0, help="--pond: pressure sweeps per substep (0 = the engine's automatic count; the creek bench runs 40); tags the outputs _s<N>")
    args = ap.parse_args()
    api = Api(args.url); api.wait_status(120)
    if args.build: build(api)
    bad = verify(api)
    if bad: raise SystemExit("the pond is not built (run --build): " + "; ".join(bad))
    if not args.run: return
    EV.mkdir(parents=True, exist_ok=True)
    api.debug("water_av_realtime", {"on": True})
    if args.pond: return run_pond_water(api, args)
    for old in api.debug("water_columns", {}).get("regions", []): api.debug("water_columns", {"id": old["id"], "destroy": True})
    r = api.debug("water_columns", {"create": REGION, "manning_n": cb.N})
    if "error" in r: raise SystemExit(r)
    rid = r["id"]
    api.debug("water_columns", {"id": rid, "plane": {"y0": cb.LINE0 + cb.SLOPE * cb.X0, "slope": cb.SLOPE, "dir_deg": 0.0}})
    api.debug("water_columns", {"id": rid, "bed": "plane"})
    cols = [cb.Z0 + (k + 0.5) / 3.0 for k in range(3 * (cb.Z1 - cb.Z0 + 1))]
    for z in cols: api.debug("water_columns", {"id": rid, "outfall": {"x": SPILL[1] + 1 - 1.0 / 6.0, "z": z}})
    for z in cols: api.debug("water_columns", {"id": rid, "spring": {"x": cb.X0 + 1.0 / 6.0, "z": z, "rate": args.q / len(cols)}})
    pond_c = ((POND[0] + POND[1] + 1) / 2.0, (POND[2] + POND[3] + 1) / 2.0)
    zc = cb.Z0 + 0.5 + 1.0 / 6.0
    cam_pond = {"x": pond_c[0] - 9.0, "y": 19.5, "z": pond_c[1] + 7.0, "yaw": -38.0, "pitch": -24.0}
    cam_dam = {"x": DAM_X - 9.0, "y": 18.2, "z": zc + 5.0, "yaw": -28.0, "pitch": -20.0}
    camera_set(api, cam_pond)
    rec = None
    total = args.fill_max + args.dam_seconds + args.break_seconds + 10
    if args.record and shutil.which("ffmpeg"):
        rec = subprocess.Popen([shutil.which("ffmpeg"), "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "15", "-t", str(total),
                                "-i", "title=WaterBench_Creek", "-vf", "crop=960:630:240:42,scale=640:-2", "-c:v", "libx264", "-crf", "32", "-pix_fmt", "yuv420p",
                                str(EV / "dam_pond.mp4")], stdin=subprocess.PIPE)
    t0 = time.time(); rows = []; events = []

    def region():
        return next(x for x in api.debug("water_columns", {})["regions"] if x["id"] == rid)

    def probe(x, z):
        p = api.debug("water_columns", {"id": rid, "probe": {"x": x, "z": z}})["probe"]
        return p
    last = {"t": None, "out": None}

    def poll(phase):
        g = region()
        pp = probe(pond_c[0], pond_c[1]); pd = probe(DAM_X - 5.0, zc); pe = probe(110.5, zc); ps = probe(SPILL[0] + 0.5, zc)
        t = round(time.time() - t0, 2)
        # flow arriving at the pond: the creek's last metre, depth x speed x 2 m (its width)
        q_pond = (pe.get("depth_m") or 0.0) * (pe.get("u") or 0.0) * 2.0 if pe.get("wet") else 0.0
        row = {"t": t, "phase": phase, "pond_level": pp.get("surface_y"), "pond_depth": pp.get("depth_m") if pp.get("wet") else 0.0,
               "pool_level": pd.get("surface_y"), "q_pond": q_pond, "in_m3": g["in_m3"], "out_m3": g["out_m3"], "stored_m3": g["stored_m3"],
               "ledger": g["ledger_error_m3"], "step_ms": g["step_ms"], "wet": g["wet_columns"],
               "spill_depth": ps.get("depth_m") if ps.get("wet") else 0.0, "spill_bed": ps.get("bed_y")}
        rows.append(row)
        return row

    def out_rate(window):
        w = [r for r in rows if r["t"] >= rows[-1]["t"] - window]
        if len(w) < 2 or w[-1]["t"] - w[0]["t"] < window * 0.8: return None
        return (w[-1]["out_m3"] - w[0]["out_m3"]) / (w[-1]["t"] - w[0]["t"])

    # FILL
    while time.time() - t0 < args.fill_max:
        poll("fill"); time.sleep(0.5)
        o = out_rate(30.0)
        if o is not None and o >= 0.98 * args.q: break
    # DAM
    subs, crest = dam_subcubes()
    camera_set(api, cam_dam)
    before = region()["ledger_error_m3"]
    for s in subs: api.post("/api/world/subcube", {**s, "material": "Stone"})
    t_dam = round(time.time() - t0, 2)
    events.append({"t": t_dam, "event": "dam placed", "crest": crest, "ledger_before": before})
    t_end = time.time() + args.dam_seconds
    while time.time() < t_end: poll("dam"); time.sleep(0.5)
    # BREAK
    before = region()["ledger_error_m3"]
    for s in subs: api.post("/api/world/subcube/remove", {"x": s["x"], "y": s["y"], "z": s["z"], "sx": s["sx"], "sy": s["sy"], "sz": s["sz"]})
    t_break = round(time.time() - t0, 2)
    events.append({"t": t_break, "event": "dam removed", "ledger_before": before})
    camera_set(api, cam_pond)
    t_end = time.time() + args.break_seconds
    while time.time() < t_end: poll("break"); time.sleep(0.5)
    if rec:   # ffmpeg must finish the file ('q'): a killed ffmpeg leaves an mp4 with no index (the first run's 64 MB was unreadable)
        try: rec.communicate(b"q", timeout=30)
        except Exception: rec.kill()

    # ---- the numbers ----
    fill = [r for r in rows if r["phase"] == "fill"]
    rise = [r for r in fill if r["pond_depth"] is not None and 0.2 <= (r["pond_level"] - POND_FLOOR) <= 0.8]
    rate = ((rise[-1]["pond_level"] - rise[0]["pond_level"]) / (rise[-1]["t"] - rise[0]["t"])) if len(rise) >= 2 and rise[-1]["t"] > rise[0]["t"] else None
    q_in = (rise[-1]["q_pond"] + rise[0]["q_pond"]) / 2.0 if rise else None
    o = None
    if fill:
        w = [r for r in fill if r["t"] >= fill[-1]["t"] - 30.0]
        if len(w) >= 2: o = (w[-1]["out_m3"] - w[0]["out_m3"]) / (w[-1]["t"] - w[0]["t"]) / args.q
    dam = [r for r in rows if r["phase"] == "dam"]
    after20 = [r for r in dam if r["t"] - t_dam >= 20.0]
    brk = [r for r in rows if r["phase"] == "break"]
    summary = {
        "q": args.q, "pond_area_m2": POND_AREA, "predicted_rise_mm_s": 1000.0 * args.q / POND_AREA,
        "measured_rise_mm_s": rate * 1000.0 if rate else None, "q_arriving_during_rise": q_in,
        "rise_vs_q_arriving_mm_s": 1000.0 * q_in / POND_AREA if q_in else None,
        "fill_seconds": fill[-1]["t"] if fill else None, "out_over_in_last30s": o,
        "spill_crest": next((r["spill_bed"] for r in rows if r["spill_bed"]), None),
        "pond_over_crest_when_spilling": next(((r["pond_level"] - r["spill_bed"]) for r in fill if (r["spill_depth"] or 0.0) > 0.01), None),
        "pond_inflow_min_after_dam": min((r["q_pond"] for r in dam), default=None),
        "pond_inflow_min_t_after_dam": min(dam, key=lambda r: r["q_pond"])["t"] - t_dam if dam else None,
        "pond_inflow_end_of_dam": dam[-1]["q_pond"] if dam else None,
        "dam_crest": crest, "pond_inflow_20s_after_dam": after20[0]["q_pond"] if after20 else None,
        "pool_level_max": max((r["pool_level"] or 0.0) for r in dam) if dam else None,
        "overtop_t_after_dam": next((r["t"] - t_dam for r in dam if (r["pool_level"] or 0.0) > crest + 0.02), None),
        "pond_inflow_max_after_break": max((r["q_pond"] for r in brk), default=None),
        "ledger_max_abs": max(abs(r["ledger"]) for r in rows), "events": events,
        "ledger_after_dam": next((r["ledger"] for r in dam), None), "ledger_after_break": next((r["ledger"] for r in brk), None),
        "step_ms_max": max(r["step_ms"] for r in rows),
    }
    (EV / "dam_pond.json").write_text(json.dumps({"summary": summary, "rows": rows}, indent=1))
    print(json.dumps(summary, indent=1))


AV_BOX = {"x1": 10, "y1": 14, "z1": 29, "x2": 127, "y2": 16, "z2": 34}   # 26.22: 1062 x 27 x 54 cells at 1/9 m (1.55 M, under the 2 M cap)
POND_COLS = [[x + 0.5, z + 0.5] for x in range(POND[0], POND[1] + 1) for z in range(POND[2], POND[3] + 1)]
SPILL_COLS = [[x + 0.5, z + 0.5] for x in range(SPILL[0], SPILL[1] + 1) for z in range(SPILL[2], SPILL[3] + 1)]


def run_pond_water(api, args):
    """26.22 P3c - the same three phases on POND water: one sparse GPU volume over creek + pond + spill, springs across the
    head's 1/9 m cells, outfalls across the spill channel's end. Gauges: pond and pool levels = the drawn surface's mean (still
    water: 26.21), the flow INTO the pond by the books = d(pond + spill-channel water)/dt + the outfall rate (5 s windows),
    the ledger from the volume's record. Writes dam_pond_pondwater.json (+ .mp4 with --record)."""
    for old in api.debug("water_columns", {}).get("regions", []): api.debug("water_columns", {"id": old["id"], "destroy": True})
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"]})
    h = 1.0 / 9.0
    r = api.debug("water_av_create", {**AV_BOX, "cellSize": h, "backend": "gpu", "auto_sleep": False, "manning_n": cb.N, **({"sweeps": args.sweeps} if args.sweeps else {})})
    tag = f"_s{args.sweeps}" if args.sweeps else ""
    sweeps_used = r["volume"].get("gpu_sweeps")
    if "error" in r: raise SystemExit(r)
    vid = r["volume"]["id"]
    print("volume", {k: r["volume"].get(k) for k in ("id", "cells", "backend", "gpu_sweeps", "manning_n", "gpu_brick_height")}, flush=True)
    for k in range(int(round((SPILL[3] - SPILL[2] + 1) / h))):
        api.debug("water_av_source", {"id": vid, "x": SPILL[1] + 1 - h / 2, "y": SPILL_FLOOR + h / 2, "z": SPILL[2] + (k + 0.5) * h, "outfall": True})
    pond_c = ((POND[0] + POND[1] + 1) / 2.0, (POND[2] + POND[3] + 1) / 2.0)
    zc = cb.Z0 + 0.5 + 1.0 / 6.0
    cam_pond = {"x": pond_c[0] - 9.0, "y": 19.5, "z": pond_c[1] + 7.0, "yaw": -38.0, "pitch": -24.0}
    cam_dam = {"x": DAM_X - 9.0, "y": 18.2, "z": zc + 5.0, "yaw": -28.0, "pitch": -20.0}
    camera_set(api, cam_pond)
    time.sleep(2.0)
    rec = None
    total = args.fill_max + args.dam_seconds + args.break_seconds + 10
    if args.record and shutil.which("ffmpeg"):
        rec = subprocess.Popen([shutil.which("ffmpeg"), "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "15", "-t", str(total),
                                "-i", "title=WaterBench_Creek", "-vf", "crop=960:630:240:42,scale=640:-2", "-c:v", "libx264", "-crf", "32", "-pix_fmt", "yuv420p",
                                str(EV / f"dam_pond_pondwater{tag}.mp4")], stdin=subprocess.PIPE)
    cells_z = [cb.Z0 + (k + 0.5) * h for k in range(int(round((cb.Z1 - cb.Z0 + 1) / h)))]
    for z in cells_z:
        api.debug("water_av_source", {"id": vid, "x": cb.X0 + h / 2, "y": cb.bed_top(cb.X0) + h / 2, "z": z, "rate": args.q / len(cells_z)})
    t0 = time.time(); rows = []; events = []

    def vol():
        return next(x for x in api.debug("water_av_list")["volumes"] if x["id"] == vid)

    def ledger(v):
        return v["mass"] - (v.get("source_in_m3", 0.0) - v.get("outfall_out_m3", 0.0))

    def poll(phase):
        v = vol()
        lv = api.debug("water_av_column", {"points": [list(pond_c), [DAM_X - 5.0 + 0.5, zc], [SPILL[0] + 0.5, zc]]}).get("columns", [])
        m = api.debug("water_av_column", {"points": POND_COLS + SPILL_COLS, "mass": True}).get("columns", [])
        store = sum(c.get("mass_depth", 0.0) for c in m)
        et = api.get("/api/debug/engine_timing")
        row = {"t": round(time.time() - t0, 2), "phase": phase,
               "pond_level": lv[0].get("surface_mean") if lv and lv[0].get("surface_y") is not None else None,
               "pool_level": lv[1].get("surface_mean") if len(lv) > 1 and lv[1].get("surface_y") is not None else None,
               "spill_level": lv[2].get("surface_mean") if len(lv) > 2 and lv[2].get("surface_y") is not None else None,
               "pond_store_m3": store, "in_m3": v.get("source_in_m3", 0.0), "out_m3": v.get("outfall_out_m3", 0.0), "mass_m3": v["mass"],
               "ledger": ledger(v), "gpu_device_ms": v.get("gpu_device_ms", 0.0), "fps": et.get("fps", 0.0)}
        rows.append(row)
        return row

    def rate(key, window, upto=None):
        rs = [r for r in rows if upto is None or r["t"] <= upto]
        if not rs: return None
        w = [r for r in rs if r["t"] >= rs[-1]["t"] - window]
        if len(w) < 2 or w[-1]["t"] - w[0]["t"] < window * 0.6: return None
        return (w[-1][key] - w[0][key]) / (w[-1]["t"] - w[0]["t"])

    # FILL
    while time.time() - t0 < args.fill_max:
        poll("fill"); time.sleep(0.5)
        o = rate("out_m3", 30.0)
        if o is not None and o >= 0.98 * args.q: break
    # DAM
    subs, crest = dam_subcubes()
    camera_set(api, cam_dam)
    before = ledger(vol())
    for s in subs: api.post("/api/world/subcube", {**s, "material": "Stone"})
    t_dam = round(time.time() - t0, 2)
    time.sleep(2.0)
    events.append({"t": t_dam, "event": "dam placed", "crest": crest, "ledger_before": before, "ledger_2s_after": ledger(vol())})
    t_end = time.time() + args.dam_seconds
    while time.time() < t_end: poll("dam"); time.sleep(0.5)
    # BREAK
    before = ledger(vol())
    for s in subs: api.post("/api/world/subcube/remove", {"x": s["x"], "y": s["y"], "z": s["z"], "sx": s["sx"], "sy": s["sy"], "sz": s["sz"]})
    t_break = round(time.time() - t0, 2)
    time.sleep(2.0)
    events.append({"t": t_break, "event": "dam removed", "ledger_before": before, "ledger_2s_after": ledger(vol())})
    camera_set(api, cam_pond)
    t_end = time.time() + args.break_seconds
    while time.time() < t_end: poll("break"); time.sleep(0.5)
    if rec:
        try: rec.communicate(b"q", timeout=30)
        except Exception: rec.kill()

    # ---- the numbers ----
    for i, r in enumerate(rows):   # the flow into the pond by the books, centred 5 s windows
        w = [x for x in rows if abs(x["t"] - r["t"]) <= 2.5]
        dt = w[-1]["t"] - w[0]["t"]
        r["q_pond"] = ((w[-1]["pond_store_m3"] - w[0]["pond_store_m3"]) + (w[-1]["out_m3"] - w[0]["out_m3"])) / dt if dt > 2.0 else None
    fill = [r for r in rows if r["phase"] == "fill"]
    rise = [r for r in fill if r["pond_level"] is not None and 0.2 <= (r["pond_level"] - POND_FLOOR) <= 0.8]
    rate_rise = ((rise[-1]["pond_level"] - rise[0]["pond_level"]) / (rise[-1]["t"] - rise[0]["t"])) if len(rise) >= 2 and rise[-1]["t"] > rise[0]["t"] else None
    qs = [r["q_pond"] for r in rise if r["q_pond"] is not None]
    w = [r for r in fill if r["t"] >= fill[-1]["t"] - 30.0] if fill else []
    o = (w[-1]["out_m3"] - w[0]["out_m3"]) / (w[-1]["t"] - w[0]["t"]) / args.q if len(w) >= 2 else None
    dam = [r for r in rows if r["phase"] == "dam" and r["q_pond"] is not None]
    brk = [r for r in rows if r["phase"] == "break" and r["q_pond"] is not None]
    steady = [r for r in rows if r["phase"] == "fill" and r["t"] >= fill[-1]["t"] - 30.0] if fill else []
    med = lambda xs: sorted(xs)[len(xs) // 2] if xs else None
    first_spill = next((r for r in fill if r["spill_level"] is not None and r["spill_level"] - SPILL_FLOOR > 0.01), None)
    dmin = min(dam, key=lambda r: r["q_pond"]) if dam else None
    summary = {
        "model": "pond (26.22 P3c)", "sweeps": sweeps_used, "q": args.q, "pond_area_m2": POND_AREA, "predicted_rise_mm_s": 1000.0 * args.q / POND_AREA,
        "measured_rise_mm_s": rate_rise * 1000.0 if rate_rise else None,
        "q_arriving_during_rise": sum(qs) / len(qs) if qs else None,
        "fill_seconds": fill[-1]["t"] if fill else None, "out_over_in_last30s": o,
        "pond_over_spill_floor_when_spilling": (first_spill["pond_level"] - SPILL_FLOOR) if first_spill and first_spill["pond_level"] else None,
        "dam_crest": crest, "pond_inflow_min_after_dam_over_q": dmin["q_pond"] / args.q if dmin else None,
        "pond_inflow_min_t_after_dam": dmin["t"] - t_dam if dmin else None,
        "pond_inflow_end_of_dam_over_q": dam[-1]["q_pond"] / args.q if dam else None,
        "pool_level_max": max((r["pool_level"] or 0.0) for r in rows if r["phase"] == "dam"),
        "overtop_t_after_dam": next((r["t"] - t_dam for r in rows if r["phase"] == "dam" and (r["pool_level"] or 0.0) > crest + 0.02), None),
        "pond_inflow_max_after_break_over_q": max(r["q_pond"] for r in brk) / args.q if brk else None,
        "ledger_max_abs": max(abs(r["ledger"]) for r in rows), "events": events,
        "gpu_device_ms_median_steady_spill": med([r["gpu_device_ms"] for r in steady]), "gpu_device_ms_median_all": med([r["gpu_device_ms"] for r in rows]),
        "gpu_device_ms_max": max(r["gpu_device_ms"] for r in rows), "fps_median": med([r["fps"] for r in rows]),
    }
    (EV / f"dam_pond_pondwater{tag}.json").write_text(json.dumps({"summary": summary, "rows": rows}, indent=1))
    print(json.dumps(summary, indent=1))


if __name__ == "__main__":
    main()
