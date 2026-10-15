"""WaterCore G3 L4 on the Coast bench (docs/WaterCore.md 18.7): a body's look reaches BOTH the shore
band's mesh and the sea sheet, a control region does not move, `clear` restores, and the look
survives save_world + a cold restart.

Rig: the shipped Coast bench (port 8109), the elevated pose (165, 40, 650) looking north over the
band and the sheet, the band on (radius 48). One variable: the ocean body's look - clarity 2 m and a
brown tint [0.20, 0.13, 0.04] vs unset. Regions (viewport pixels of the 1600 x 900 capture, chosen
from the G2 foam/flow tap, which paints the band green): band = two boxes beside the tree on the
band's surface, sheet = two boxes beyond the band (the compass HUD excluded), control = dry sand.
Predictions: band and sheet mean-colour distance > 0.05 after the change; control < 0.01; after
`clear` the band is back within 0.02 of before; after the restart the echoed look is identical.

Usage: python tools/water_look_l4.py [--restart-cmd "python .../restart_bench.py Coast"]
"""
import argparse, json, subprocess, sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from water_bench import Api, camera_set, camera_get
from water_feel import capture
from PIL import Image

EV = Path(__file__).resolve().parents[1] / "docs" / "evidence" / "water_core_g"
POSE = {"x": 165, "y": 40, "z": 650, "yaw": 90, "pitch": -35}
REGIONS = {"band": [(850, 160, 1150, 240), (250, 160, 450, 230)],
           "sheet": [(250, 50, 560, 110), (880, 50, 1150, 110)],
           "control_sand": [(880, 300, 1050, 360)]}
LOOK = {"clarity": 2.0, "tint": [0.20, 0.13, 0.04]}


def means(png):
    im = Image.open(png).convert("RGB")
    out = {}
    for name, boxes in REGIONS.items():
        acc = [0.0, 0.0, 0.0]; n = 0
        for b in boxes:
            for px in im.crop(b).getdata():
                acc[0] += px[0]; acc[1] += px[1]; acc[2] += px[2]; n += 1
        out[name] = [round(c / n / 255.0, 4) for c in acc]
    return out


def dist(a, b):
    return round(sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5, 4)


def shot(api, tag):
    time.sleep(2.0)
    p = capture(api, EV / f"coast_look_{tag}.png")
    return p, means(p)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8109")
    ap.add_argument("--restart-cmd", dest="restart_cmd", default=None)
    args = ap.parse_args()
    api = Api(args.url); api.wait_status(900)
    log = {"pose": POSE, "regions": REGIONS, "look": LOOK}
    camera_set(api, POSE); time.sleep(6)
    log["band"] = {k: v for k, v in api.debug("water_shore", {"on": True, "radius": 48}).items() if k in ("active", "n", "centre", "still")}
    time.sleep(8)
    probe = api.debug("water_look", {"at": [165, 720]})   # a sea column: resolves (and if needed imports) the ocean body
    if "error" in probe:
        raise SystemExit(f"water_look at the sea: {probe}")
    body = probe["body"]
    api.debug("water_look", {"body": body, "clear": True})
    log["before_png"], log["before"] = shot(api, "before")
    set_r = api.debug("water_look", dict(LOOK, body=body))
    log["set"] = set_r
    log["after_png"], log["after"] = shot(api, "after")
    log["cleared"] = api.debug("water_look", {"body": body, "clear": True})
    log["restored_png"], log["restored"] = shot(api, "restored")
    d = {r: dist(log["before"][r], log["after"][r]) for r in REGIONS}
    d_restore = {r: dist(log["before"][r], log["restored"][r]) for r in REGIONS}
    log["distance_after"] = d; log["distance_restored"] = d_restore
    # persistence: set, save, cold restart, read back
    persisted = None
    if args.restart_cmd:
        api.debug("water_look", dict(LOOK, body=body))
        log["save"] = api.post("/api/world/save", {}, timeout=300)
        print(subprocess.run(args.restart_cmd, shell=True, capture_output=True, text=True).stdout.strip())
        time.sleep(5)
        api = Api(args.url); api.wait_status(900)
        back = api.debug("water_look", {"body": body})   # no knobs: echo only
        persisted = back.get("look")
        log["after_restart"] = back
        api.debug("water_look", {"body": body, "clear": True})   # leave the bench as found
        log["cleanup_save"] = api.post("/api/world/save", {}, timeout=300)
    want = json.loads(json.dumps(set_r.get("look")))
    verdict = {"band_changed": d["band"] > 0.05, "sheet_changed": d["sheet"] > 0.05, "control_still": d["control_sand"] < 0.01,
               "band_restored": d_restore["band"] < 0.02,
               "persisted": (persisted == want) if args.restart_cmd else "skipped (no --restart-cmd)"}
    log["verdict"] = verdict
    log["pass"] = all(v is True for v in verdict.values() if not isinstance(v, str))
    stamp = time.strftime("%Y%m%d_%H%M%S")
    (EV / f"coast_look_l4_{stamp}.json").write_text(json.dumps(log, indent=1), encoding="utf-8")
    print(json.dumps({"body": body, "distance_after": d, "distance_restored": d_restore, "verdict": verdict, "pass": log["pass"],
                      "persisted_look": persisted, "evidence": f"coast_look_l4_{stamp}.json"}, indent=1))


if __name__ == "__main__":
    main()
