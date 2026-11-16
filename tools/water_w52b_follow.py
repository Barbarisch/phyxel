"""WaterCore.md §54.3 (W5.2b) - a simulated reach FOLLOWS the camera along a generated river (L4).

WaterBench_RiverCarve (port 8113), the shipped game.json: the water core steps in play by default now (realtime ON - this tool
only READS it, it never switches it on) and the follow is on by default (water_av_river_follow). The river is traced from the 51.14
site (the record's current, water_query's river.dir, 2 m steps) 50 m upstream, then the camera flies 150 m down it at 4 m/s on the
engine's own constant-speed camera path (/api/camera/path - smooth, no HTTP stepping), 12 m above the water, 10 m behind its
point, looking down the river. A 1 Hz sampler (the tool's only load during the flight: the owner's jitter question) reads
engine_timing (fps, CPU / GPU ms, the longest frame since the last read) and the follow's state.
  --flight (default): makes / releases with their make ms; the longest frame in each second, the seconds holding a make (the
      swap hitch, A3) against the rest; wood dropped on the river ahead of the camera at 3 points, tracked (particle_log), its
      speed along the river in 1 s windows, the windows holding a swap against the rest (A1).
  --cost [--reps 2]: whole frame along the same flight, follow on vs off, ABBA x reps.
  --clip: the flight recorded (gdigrab), captioned (t, fps, the reach made / released) -> <tag>.mp4.
Predictions (written before the run, §54.3): realtime reads true with no tool switching it on; a reach is live over the river
under the camera from the first check on (>= 95 % of samples); 1 + 150 / 16 ~ 10 makes; each make one long frame of ~150-300 ms
(the make: 131-172 ms live in W5.2a, 300 ms in the test) - reported, not cut; the wood keeps moving across a swap (its speed in a
swap window >= half its speed outside them).

    python tools/water_w52b_follow.py [--tag w52b_follow]
    python tools/water_w52b_follow.py --cost [--reps 2]
    python tools/water_w52b_follow.py --clip [--tag w52b_clip]
54.4 (W5.3): --river on|off sets the river ripple layer (water_ripples {river: {on}}) for the flight / clip; --cost states "ron" /
"roff" = follow on (shipped) with the river layer on / off.
Writes docs/evidence/water_core_m4/<tag>.json.
"""
import argparse, json, math, pathlib, shutil, statistics, subprocess, sys, threading, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
import water_w5_river_current as w51
from water_w5_river_current import tracks_xz
from water_w5_clip import srt_time

EV = w51.EV
SITE = w51.SITES["river"]


def river_at(api, pts, y):
    rows = api.debug("water_query", {"points": [[x, y, z] for x, z in pts]})["points"]
    return rows


def trace(api, back_m=50.0, ahead_m=150.0, step=2.0):
    """The river's centre path through the record: from the site, back_m upstream then ahead_m downstream along the record's
    current (water_query river.dir), re-centred across the river every step (the deepest of 9 points across it)."""
    y = SITE["level"] - 0.15

    def walk(p, sign, metres):
        out = [p]
        d0 = None
        for _ in range(int(metres / step)):
            r = river_at(api, [p], y)[0]
            rv = r.get("river") or {}
            d = rv.get("dir")
            if not d or math.hypot(*d) < 0.5: d = d0
            if d is None: break
            d0 = d
            q = (p[0] + sign * step * d[0], p[1] + sign * step * d[1])
            perp = (-d[1], d[0])
            across = [(q[0] + t * perp[0], q[1] + t * perp[1]) for t in (-4, -3, -2, -1, 0, 1, 2, 3, 4)]
            rows = river_at(api, across, y)
            best = max(range(9), key=lambda k: ((rows[k].get("river") or {}).get("depth_m", 0.0) if rows[k].get("in_water") else -1.0, -abs(k - 4)))
            p = across[best] if rows[best].get("in_water") else q
            out.append(p)
        return out

    c = tuple(SITE["c"])
    up = walk(c, -1.0, back_m)
    start = up[-1]
    down = walk(start, 1.0, back_m + ahead_m)
    return down


def waypoints(path, height=12.0, behind=10.0, every=4):
    wps = []
    for i in range(0, len(path), every):
        j = min(i + every, len(path) - 1); k = max(i - every, 0)
        dx, dz = path[j][0] - path[k][0], path[j][1] - path[k][1]
        n = math.hypot(dx, dz) or 1.0
        dx, dz = dx / n, dz / n
        wps.append({"x": path[i][0] - behind * dx, "y": SITE["level"] + height, "z": path[i][1] - behind * dz,
                    "yaw": math.degrees(math.atan2(dz, dx)), "pitch": -35.0})
    return wps


def follow(api, **kw):
    return api.debug("water_av_river_follow", kw)


class Sampler:
    def __init__(self, api, hz=1.0):
        self.api, self.dt, self.rows, self.stop = api, 1.0 / hz, [], False
        self.t0 = time.time()
        self.th = threading.Thread(target=self.run, daemon=True)

    def run(self):
        while not self.stop:
            t = time.time() - self.t0
            row = {"t": round(t, 2)}
            try:
                e = self.api.get("/api/debug/engine_timing")
                row.update({"fps": e.get("fps"), "cpu_ms": e.get("cpuFrameTime"), "gpu_ms": e.get("gpu_frame_ms"),
                            "frame_ms_max": e.get("frame_ms_max_since_last_read")})
            except Exception as ex: row["err"] = str(ex)
            try:
                f = follow(self.api)
                row.update({k: f.get(k) for k in ("reach_id", "makes", "releases", "refusals", "last_make_ms", "last_error", "min", "max", "point_found")})
            except Exception as ex: row["err2"] = str(ex)
            try:   # 54.4: the river ripple layer's step (this frame) and its session worst
                rv = self.api.debug("water_ripples", {"shape": False}).get("river") or {}
                row.update({"river_active": rv.get("active"), "river_ms": rv.get("ms"), "river_max_ms": (rv.get("max_ms") or {}).get("all")})
            except Exception: pass
            try:
                cp = self.api.get("/api/camera/path")
                row["progress"] = cp.get("progress"); row["playing"] = cp.get("playing")
                cam = cp.get("camera") or {}
                row["cam"] = [round(cam.get("x", 0.0), 1), round(cam.get("z", 0.0), 1)]
            except Exception: pass
            self.rows.append(row)
            time.sleep(max(0.0, self.dt - (time.time() - self.t0 - t)))

    def start(self): self.th.start(); return self

    def finish(self): self.stop = True; self.th.join(timeout=5.0); return self.rows


def fly(api, wps, speed=4.0, on_tick=None):
    """Start the engine's camera path and wait for it to finish; on_tick(t) every 0.25 s (wood drops)."""
    camera_set(api, wps[0]); time.sleep(8.0)   # the first pose's chunks and record tiles
    r = api.post("/api/camera/path", {"waypoints": wps, "speed_u_per_s": speed, "loop": False, "stream_follow": True})
    if not r.get("success"): raise SystemExit(f"camera path refused: {r}")
    t0 = time.time()
    while True:
        t = time.time() - t0
        if on_tick: on_tick(t)
        cp = api.get("/api/camera/path")
        if cp.get("finished") or (not cp.get("playing") and t > 2.0): break
        time.sleep(0.25)
    api.post("/api/camera/path", {"stop": True})
    return time.time() - t0


def swap_seconds(rows):
    """Indexes of the sampler rows whose second held a make (the makes counter moved since the row before)."""
    out = set()
    for i in range(1, len(rows)):
        a, b = rows[i - 1].get("makes"), rows[i].get("makes")
        if isinstance(a, int) and isinstance(b, int) and b > a: out.add(i)
    return out


def summarise(rows):
    sw = swap_seconds(rows)
    def vals(idx, k): return [rows[i][k] for i in idx if isinstance(rows[i].get(k), (int, float))]
    rest = [i for i in range(1, len(rows)) if i not in sw]
    covered = [r for r in rows if r.get("reach_id")]
    def stat(v): return {"n": len(v), "mean": round(statistics.mean(v), 2) if v else None, "max": round(max(v), 2) if v else None,
                         "median": round(statistics.median(v), 2) if v else None}
    return {"samples": len(rows), "samples_with_reach": len(covered), "swap_seconds": sorted(sw),
            "frame_ms_max_in_swap_seconds": stat(vals(sw, "frame_ms_max")), "frame_ms_max_other_seconds": stat(vals(rest, "frame_ms_max")),
            "fps": stat(vals(range(len(rows)), "fps")), "cpu_ms": stat(vals(range(len(rows)), "cpu_ms")), "gpu_ms": stat(vals(range(len(rows)), "gpu_ms")),
            "make_ms": [rows[i].get("last_make_ms") for i in sorted(sw)]}


def wood(rows, log, log_start_s):
    """The wood's speed in 1 s windows (particle_log), on the sampler's clock (the log's first snapshot = log_start_s), each window
    flagged if a make happened in it (a make lies in the second before the sampler row that first shows it)."""
    tr = tracks_xz(log)
    sw_t = [rows[i]["t"] for i in swap_seconds(rows)]
    tmin = min((ps[0][0] for ps in tr.values() if ps), default=0.0)
    out = []
    for slot, ps in tr.items():
        if len(ps) < 20: continue
        k0 = next((i for i, p in enumerate(ps) if p[2] < SITE["level"] + 0.8), None)   # in the water (after the fall)
        if k0 is None: continue
        ps = [(log_start_s + p[0] - tmin, p[1], p[2], p[3]) for p in ps[k0:]]
        wins = []
        t = ps[0][0]
        while t + 1.0 <= ps[-1][0]:
            a = min(ps, key=lambda p: abs(p[0] - t)); b = min(ps, key=lambda p: abs(p[0] - (t + 1.0)))
            if b[0] - a[0] > 0.5:
                swap = any(a[0] - 0.5 < ts <= b[0] + 1.0 for ts in sw_t)
                wins.append({"t": round(a[0], 2), "speed": round(math.hypot(b[1] - a[1], b[3] - a[3]) / (b[0] - a[0]), 3), "swap": swap,
                             "pos": [round(a[1], 1), round(a[3], 1)]})
            t += 1.0
        sw = [w["speed"] for w in wins if w["swap"]]; ot = [w["speed"] for w in wins if not w["swap"]]
        out.append({"slot": slot, "samples": len(ps), "drift_m": round(math.hypot(ps[-1][1] - ps[0][1], ps[-1][3] - ps[0][3]), 2),
                    "t_s": round(ps[-1][0] - ps[0][0], 1), "speed_swap_windows": round(statistics.mean(sw), 3) if sw else None,
                    "speed_other_windows": round(statistics.mean(ot), 3) if ot else None, "windows": wins})
    return {"tracks": out, "swap_times_sampler_s": sw_t}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8113")
    ap.add_argument("--tag", default=None)
    ap.add_argument("--cost", action="store_true")
    ap.add_argument("--clip", action="store_true")
    ap.add_argument("--reps", type=int, default=2)
    ap.add_argument("--order", default=None)
    ap.add_argument("--speed", type=float, default=4.0)
    ap.add_argument("--window", default="WaterBench_RiverCarve")
    ap.add_argument("--crop", default="954:560:243:60")
    ap.add_argument("--river", choices=("on", "off"), default=None)   # 54.4: the river ripple layer for the flight / clip
    a = ap.parse_args()
    tag = a.tag or ("w52b_cost" if a.cost else "w52b_clip" if a.clip else "w52b_follow")
    api = Api(a.url); api.wait_status(600)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    rt = api.debug("water_av_realtime", {})   # READ only: no "on" - the shipped default must be ON (54.3)
    f0 = follow(api)
    print("realtime (read, not set):", rt, "| follow:", {k: f0.get(k) for k in ("on", "radius_m", "half_m", "margin_m", "realtime")}, flush=True)
    out = {"bench": "WaterBench_RiverCarve", "realtime_default": rt.get("realtime"), "follow_default": f0}
    rv = api.debug("water_ripples", {"shape": False}).get("river")
    out["river_ripple_default"] = rv
    print("river ripple layer (read):", rv, flush=True)
    if a.river: out["river_ripple_set"] = api.debug("water_ripples", {"shape": False, "river": {"on": a.river == "on"}}).get("river")
    if not rt.get("realtime"): print("!! realtime is OFF by default - the prediction failed", flush=True)
    camera_set(api, SITE["cam"]); time.sleep(12.0)
    path = trace(api)
    wps = waypoints(path)
    print(f"traced {len(path)} points ({2.0 * (len(path) - 1):.0f} m), {len(wps)} waypoints", flush=True)
    out["path"] = [[round(x, 2), round(z, 2)] for x, z in path]; out["waypoints"] = wps
    if a.cost:
        runs = []
        # --order: the flights' states in order (default ABBA x reps); "rtoff" = follow off AND realtime off (the engine before 54.3)
        order = a.order.split(",") if a.order else [st for rep in range(a.reps) for st in (("off", "on") if rep % 2 == 0 else ("on", "off"))]
        for k, state in enumerate(order):
                rep = k
                api.debug("water_av_realtime", {"on": state != "rtoff"})
                follow(api, on=(state in ("on", "ron", "roff")))
                if state in ("ron", "roff"): api.debug("water_ripples", {"shape": False, "river": {"on": state == "ron"}})   # 54.4
                time.sleep(3.0)
                s = Sampler(api).start(); secs = fly(api, wps, a.speed); rows = s.finish()
                sm = summarise(rows); sm.update({"rep": rep, "state": state, "flight_s": round(secs, 1)})
                print({k: sm[k] for k in ("rep", "state", "fps", "cpu_ms", "gpu_ms", "samples_with_reach", "samples")}, flush=True)
                runs.append(sm)
        follow(api, on=True); api.debug("water_av_realtime", {"on": True})
        def mean(state, k): v = [r[k]["mean"] for r in runs if r["state"] == state and r[k]["mean"] is not None]; return round(statistics.mean(v), 2) if v else None
        out["runs"] = runs
        def med(state, k): v = [r[k]["median"] for r in runs if r["state"] == state and r[k]["median"] is not None]; return round(statistics.mean(v), 2) if v else None
        out["summary"] = {st: {k: mean(st, k) for k in ("fps", "cpu_ms", "gpu_ms")} for st in ("rtoff", "off", "on", "ron", "roff")}
        out["summary_medians"] = {st: {k: med(st, k) for k in ("fps", "cpu_ms", "gpu_ms")} for st in ("rtoff", "off", "on", "ron", "roff")}
        out["river_ripple_end"] = api.debug("water_ripples", {"shape": False}).get("river")
        print("summary:", out["summary"])
    else:
        log = (EV / f"{tag}_debris_tmp.csv").resolve()
        dropped = []
        n = len(path)
        def on_tick(t):   # wood dropped on the river ~24 m ahead of the camera's point at 4, 14 and 24 s
            for td in (4.0, 14.0, 24.0):
                if t >= td and td not in [d["t"] for d in dropped]:
                    i = min(n - 1, 24 + int((t * a.speed + 24.0) / 2.0))   # the flight starts at path point 24 (wps_fly[0])
                    x, z = path[i]
                    q = river_at(api, [(x, z)], SITE["level"])[0]
                    ys = q.get("surface_y") or SITE["level"]
                    for off in (-1.5, 0.0, 1.5):
                        api.debug("spawn_gpu_particle", {"x": x + off, "y": ys + 1.2, "z": z + off, "material": "Wood", "scale": 1.0 / 3.0, "lifetime": 120.0, "spin": 0.0})
                    dropped.append({"t": td, "at": [round(x, 1), round(z, 1)], "surface_y": ys})
        rec = None; raw = EV / f"{tag}_raw.mp4"
        wps_fly = wps[int(25 / 4):]   # the flight: from the traced start's 50 m up-river point on
        if a.clip:
            ff = shutil.which("ffmpeg")
            dur = 8.0 + 2.0 + 150.0 / a.speed + 6.0
            rec = subprocess.Popen([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(dur), "-i", f"title={a.window}",
                                    "-c:v", "libx264", "-pix_fmt", "yuv420p", str(raw)])
        s = Sampler(api).start()
        api.debug("particle_log", {"action": "start", "file": str(log)})
        log_start_s = time.time() - s.t0
        secs = fly(api, wps_fly, a.speed, on_tick)
        time.sleep(4.0)
        rows = s.finish()
        api.debug("particle_log", {"action": "stop"})
        out["flight_s"] = round(secs, 1); out["rows"] = rows; out["drops"] = dropped
        out["summary"] = summarise(rows)
        out["wood"] = wood(rows, log, log_start_s)
        log.unlink(missing_ok=True)
        print("summary:", out["summary"], flush=True)
        for tk in out["wood"]["tracks"]: print("wood:", {k: tk[k] for k in ("slot", "drift_m", "t_s", "speed_swap_windows", "speed_other_windows")}, flush=True)
        if rec is not None:
            rec.wait()
            lines = []
            mk = swap_seconds(rows)
            for i, r in enumerate(rows):
                t1 = rows[i + 1]["t"] if i + 1 < len(rows) else r["t"] + 1.0
                ev = f"reach {r.get('reach_id')} made ({r.get('last_make_ms') or 0:.0f} ms)" if i in mk else (f"simulated reach {r.get('reach_id')} under the camera" if r.get("reach_id") else "no reach (the record only)")
                lines.append(f"{i + 1}\n{srt_time(r['t'])} --> {srt_time(t1)}\nflying down a generated river at {a.speed:.0f} m/s - {ev}\n"
                             f"t {r['t']:.0f} s | {(r.get('fps') or 0):.0f} fps | longest frame {(r.get('frame_ms_max') or 0):.0f} ms | makes {r.get('makes')}\n")
            (EV / f"{tag}.srt").write_text("\n".join(lines), encoding="utf-8")
            style = "FontName=Arial,FontSize=12,PrimaryColour=&H00FFFFFF,OutlineColour=&H00000000,BorderStyle=3,Outline=1,Alignment=2,MarginV=6"
            subprocess.run([shutil.which("ffmpeg"), "-y", "-loglevel", "error", "-i", raw.name, "-vf", f"crop={a.crop},scale=1280:-2,subtitles={tag}.srt:force_style='{style}'",
                            "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "21", f"{tag}.mp4"], check=True, cwd=str(EV))
            raw.unlink(missing_ok=True)
            print("wrote", EV / f"{tag}.mp4")
    out["follow_end"] = follow(api)
    (EV / f"{tag}.json").write_text(json.dumps(out, indent=1))
    print("wrote", EV / f"{tag}.json")


if __name__ == "__main__":
    main()
