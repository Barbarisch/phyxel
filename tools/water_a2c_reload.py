"""WaterCore 35 (plan A2c) - a pond that slept into the record still wakes after a save and a reload.

Small bench (port 8111), Release, the shipped settings: auto-sleep ON, displacement off (34), the push on. docs/WaterCore.md 35.
  1. the pond (tools/water_float_rest.py's 4 x 4 m, 1.5 m deep, 1/9 m cells) sleeps into the record;
  2. CONTROL: a 1/3 m Wood block dropped from 1 m wakes it (34.2's green); cleared; the pond sleeps again;
  3. the save, then the engine goes down:
       --shutdown kill   API save_world, then the process killed (no shutdown save: the API save alone carries the state)
       --shutdown close  NO API save; the window closed (WM_CLOSE: the editor's own shutdown, which saves dirty chunks)
  4. the engine up again; the same drop. RED (before 35): slept.boxes 0, the pond not woken, the block floats on the record.
     GREEN: boxes 1 (restored 1), the pond wakes, draft ~0.70, the peak after entry within the control's + 2 cm.
Nothing here can float a block up: every drop is the shipped push with displacement off (A2g, A2h measured it).

    python tools/water_a2c_reload.py [--shutdown kill|close] [--tag red]
Writes docs/evidence/water_core_m4/a2c_<tag>.json.
"""
import argparse, json, os, pathlib, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api
import water_float_rest as w

PROJECT = r"C:\Users\jack\Documents\PhyxelProjects\WaterBench_Small"
UV_PY = r"C:\Users\jack\AppData\Roaming\uv\python\cpython-3.12-windows-x86_64-none"


def settings(api):
    api.debug("water_av_realtime", {"on": True})
    c = api.debug("water_coupling", {"enabled": True, "solids": True, "pressure_force": True, "displace": False})
    if c.get("displace") is not False: raise SystemExit(f"displace not taken: {c.get('displace')}")
    return c


def slept(api):
    return api.debug("water_coupling", {}).get("slept", {})


def wait_recorded(api, limit=60.0):
    t0 = time.time()
    while api.debug("water_av_list").get("volumes") and time.time() - t0 < limit: time.sleep(0.5)
    if api.debug("water_av_list").get("volumes"): raise SystemExit(f"the pond did not sleep in {limit} s")
    return round(time.time() - t0, 1)


def drop(api, name, seconds):
    """A 1/3 m Wood block from 1 m into the slept pond: did it wake, where did it rest, its peak after entry."""
    api.debug("clear_dynamics")
    a = 1 / 6
    y0 = w.LEVEL + a + 1.0 + 0.01
    log = (w.EV / f"a2c_{name}.csv").resolve()
    api.debug("particle_log", {"action": "start", "file": str(log)})
    r = api.debug("spawn_gpu_particle", {"x": w.CX, "y": y0, "z": w.CZ, "material": "Wood", "scale": 1 / 3, "lifetime": seconds + 30, "spin": 0})
    if not r.get("success"): raise SystemExit(f"spawn: {r}")
    woke, t0 = False, time.time()
    while time.time() - t0 < seconds:
        if api.debug("water_av_list").get("volumes"): woke = True
        time.sleep(0.25)
    api.debug("particle_log", {"action": "stop"})
    rows = w.read_log(log, a); log.unlink(missing_ok=True)
    k = next((i for i, b in enumerate(rows) if abs(b[1] - y0) < 0.05), None)   # from the spawn pose (a reused slot can open the log)
    rows = rows[k:] if k is not None else []
    tail = [b for b in rows if b[0] > rows[-1][0] - 3.0] if rows else []
    drafts = [(w.LEVEL - (b[1] - a)) / (2 * a) for b in tail]
    entry = next((i for i, b in enumerate(rows) if b[1] - a < w.LEVEL), None)
    out = {"pond_woke": woke, "slept": slept(api), "spawn_y": y0,
           "draft_last3s": [min(drafts), max(drafts)] if drafts else None,
           "peak_y_after_entry": max((b[1] for b in rows[entry:]), default=None) if entry is not None else None,
           "end_y": rows[-1][1] if rows else None}
    api.debug("clear_dynamics")
    print(f"  {name}: pond woke {woke} | draft (last 3 s) {out['draft_last3s']} peak after entry {out['peak_y_after_entry']}"
          f" end y {out['end_y']} | slept {out['slept']}", flush=True)
    return out


def engine_up():
    env = dict(os.environ); env["PATH"] = UV_PY + ";" + env["PATH"]
    subprocess.Popen(["phyxel", "up", PROJECT], env=env, shell=True)


def engine_down(how):
    if how == "kill":
        subprocess.run(["powershell", "-NoProfile", "-Command", "Get-Process phyxel -ErrorAction SilentlyContinue | Stop-Process"], check=False)
    else:
        subprocess.run(["powershell", "-NoProfile", "-Command", "(Get-Process phyxel).CloseMainWindow() | Out-Null"], check=False)
    t0 = time.time()
    while time.time() - t0 < 60:
        r = subprocess.run(["powershell", "-NoProfile", "-Command", "(Get-Process phyxel -ErrorAction SilentlyContinue | Measure-Object).Count"],
                           capture_output=True, text=True)
        if r.stdout.strip() == "0": return round(time.time() - t0, 1)
        time.sleep(1.0)
    raise SystemExit("the engine did not exit in 60 s")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8111")
    ap.add_argument("--shutdown", default="kill", choices=["kill", "close"])
    ap.add_argument("--seconds", type=float, default=15.0)
    ap.add_argument("--tag", default="now")
    a = ap.parse_args()
    api = Api(a.url); st = api.wait_status(900)
    out = {"shutdown": a.shutdown, "status_before": {k: st.get(k) for k in ("build_config", "version", "git") if k in st}}
    w.AUTO_SLEEP = True; w.PRESSURE = True; w.DISPLACE = False
    w.pond(api); settings(api)
    out["slept_s_first"] = wait_recorded(api)
    out["control"] = drop(api, "control", a.seconds)
    out["slept_s_before_save"] = wait_recorded(api)
    out["slept_before_save"] = slept(api)
    if a.shutdown == "kill":
        out["save"] = api.post("/api/world/save", {"all": True})
        print("  save_world:", out["save"], flush=True)
    out["down_s"] = engine_down(a.shutdown)
    print(f"  engine down ({a.shutdown}) in {out['down_s']} s; up again", flush=True)
    engine_up()
    api = Api(a.url); api.wait_status(900); time.sleep(3.0)
    settings(api)
    out["slept_after_reload"] = slept(api)
    out["volumes_after_reload"] = len(api.debug("water_av_list").get("volumes", []))
    print(f"  after the reload: slept {out['slept_after_reload']} volumes {out['volumes_after_reload']}", flush=True)
    out["after"] = drop(api, "after_reload", a.seconds)
    c, r = out["control"], out["after"]
    out["peak_within_control_2cm"] = (r["peak_y_after_entry"] is not None and c["peak_y_after_entry"] is not None
                                      and r["peak_y_after_entry"] <= c["peak_y_after_entry"] + 0.02)
    out["green"] = bool(r["pond_woke"] and out["peak_within_control_2cm"])
    print(f"  {'GREEN' if out['green'] else 'RED'}: woke after the reload {r['pond_woke']}, peak within the control + 2 cm {out['peak_within_control_2cm']}", flush=True)
    (w.EV / f"a2c_{a.tag}.json").write_text(json.dumps(out, indent=1))


if __name__ == "__main__":
    main()
