"""WaterCore 31.6 item 7: the hand-over - a floater resting on an awake pond (the pressure push) when the pond sleeps into the record
(the floater then floats on the record's surface by the table law). Both give the draft 1 / b, so the predicted jump is <= 1 cm.

On the Small bench: the pond of tools/water_float_rest.py kept awake (auto_sleep off) while a Wood block settles, then put to sleep
(water_av_sleep force); the block's height from the particle log before and after. The camera is put at the pond: the record is
only drawn - and only seen by debris - within the record-draw radius of the camera (31.9: a viewer-dependent defect, found here).

    python tools/water_float_handover.py [--url http://127.0.0.1:8111] [--size 0.3333] [--tag now]
Writes docs/evidence/water_core_m4/handover_<tag>.json.
"""
import argparse, json, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api
import water_float_rest as w


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8111")
    ap.add_argument("--size", type=float, default=1.0 / 3.0)
    ap.add_argument("--tag", default="now")
    args = ap.parse_args()
    api = Api(args.url); api.wait_status(900)
    api.post("/api/camera", {"position": {"x": 102.0, "y": 20.0, "z": 8.0}, "yaw": 90, "pitch": -40})
    w.PRESSURE = True; w.AUTO_SLEEP = False; w.DISPLACE = True
    w.pond(api)
    time.sleep(4.0)
    vid = w.vol(api).get("id")
    a = args.size / 2
    log = (w.EV / "handover.csv").resolve()
    api.debug("particle_log", {"action": "start", "file": str(log)})
    api.debug("spawn_gpu_particle", {"x": w.CX, "y": w.LEVEL + a + 0.01, "z": w.CZ, "material": "Wood", "scale": args.size, "lifetime": 90, "spin": 0})
    time.sleep(15.0)
    t_sleep = None
    r = api.debug("water_av_sleep", {"id": vid, "force": True})
    t_mark = time.time()
    time.sleep(8.0)
    api.debug("particle_log", {"action": "stop"})
    body = w.read_log(log, a)
    log.unlink(missing_ok=True)
    v = w.vol(api)
    state = "recorded" if not v else ("asleep" if v.get("asleep") else "awake")
    # the log's clock is the frames' own (it does not track the wall over a long run): the sleep call came 15 / 23 of the way in
    n = len(body); k = int(n * 15.0 / 23.0)
    before = [x[1] for x in body[int(n * 12.0 / 23.0):k]]
    after = [x[1] for x in body[int(n * 16.0 / 23.0):]]
    out = {"size": args.size, "sleep_call": r, "state_after": state, "y_before_mean": sum(before) / len(before) if before else None,
           "y_after_min": min(after) if after else None, "y_after_max": max(after) if after else None,
           "draft_before": (w.LEVEL - (before[-1] - a)) / (2 * a) if before else None,
           "draft_after": (w.LEVEL - (after[-1] - a)) / (2 * a) if after else None}
    if before and after: out["jump_mm"] = 1000.0 * max(abs(min(after) - before[-1]), abs(max(after) - before[-1]))
    print(json.dumps({k: v for k, v in out.items() if k != "sleep_call"}, indent=1))
    (w.EV / f"handover_{args.tag}.json").write_text(json.dumps({**out, "body": body[::10]}, indent=1))
    api.debug("clear_dynamics")


if __name__ == "__main__":
    main()
