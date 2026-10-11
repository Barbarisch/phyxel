"""Live splash demo on the Small water bench (docs/WaterCore.md 21 droplets, 22 ripples).

Builds the 4 x 4 m test pond, points the camera at it, and drops a stone each time you press Enter. Between
drops you can change the droplet size or the other knobs; every drop prints its droplet ledger (peak in the
air, litres born / landed). Old stones are cleared before each drop so the runs compare.

Needs the Small bench running on port 8111 (`phyxel up` in Documents/PhyxelProjects/WaterBench_Small).

    python tools/water_splash_demo.py                 # interactive, droplet size 1/9 (the default)
    python tools/water_splash_demo.py --size 27       # start at 1/27
    python tools/water_splash_demo.py --auto 5 --size 27 --interval 4   # five drops, no prompts

At the prompt:
    Enter          drop a stone
    9 / 18 / 27    droplet size 1/N of a cell (clamped to [1/27, 1] by the engine)
    big / small    the stone: 1 m or 1/3 m
    h 5            drop height above the pond, metres (default 3.5)
    r              rebuild the pond (fresh water, stones cleared)
    rip            toggle the ripple layer
    d              toggle droplets on/off (off = the A/B control)
    cam            put the camera back on the pond
    q              quit
"""
import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from water_bench import Api, camera_set  # noqa: E402

POND_BOX = {"x1": 99, "y1": 14, "z1": 11, "x2": 104, "y2": 19, "z2": 16}   # the volume (its floor holds the stone floor)
DROP_X, DROP_Z, POND_TOP = 101.5, 13.5, 16.5
POSES = {"mid": {"x": 105.2, "y": 18.6, "z": 13.5, "yaw": 180, "pitch": -24},
         "high": {"x": 109.5, "y": 20.5, "z": 13.5, "yaw": 180, "pitch": -38},
         "low": {"x": 107.5, "y": 17.6, "z": 13.5, "yaw": 180, "pitch": -12}}


def build_pond(api):
    api.debug("clear_dynamics")
    for v in api.debug("water_av_list").get("volumes", []):
        api.debug("water_av_destroy", {"id": v["id"]})
    api.debug("water_av_create", {**POND_BOX, "cellSize": 1.0 / 3.0, "transport": "eulerian", "backend": "auto", "auto_sleep": False})
    api.debug("place_water_box", {"x1": 100, "y1": 15, "z1": 12, "x2": 103, "y2": 15, "z2": 15, "mass": 1.0, "target": "core"})
    api.debug("place_water_box", {"x1": 100, "y1": 16, "z1": 12, "x2": 103, "y2": 16, "z2": 15, "mass": 0.5, "target": "core"})
    api.debug("water_av_realtime", {"on": True})
    api.debug("water_coupling", {"enabled": True, "solids": True})


def drop(api, st, watch):
    api.debug("clear_dynamics")   # last drop's stone off the floor, so every run starts the same
    time.sleep(0.3)
    before = api.debug("water_droplets", {})
    scale = 1.0 if st["big"] else 1.0 / 3.0
    api.debug("spawn_gpu_particle", {"x": DROP_X, "y": POND_TOP + st["height"], "z": DROP_Z, "material": "Stone",
                                     "scale": scale, "lifetime": 60.0})
    peak, t0 = 0, time.time()
    while time.time() - t0 < watch:
        peak = max(peak, api.debug("water_droplets", {}).get("alive", 0))
        time.sleep(0.05)
    after = api.debug("water_droplets", {})
    born = (after["born_m3"] - before["born_m3"]) * 1000.0
    landed = (after["landed_m3"] - before["landed_m3"]) * 1000.0
    print(f"  drop: stone {'1 m' if st['big'] else '1/3 m'} from {st['height']:.1f} m, size 1/{st['size']:g}, droplets {'on' if st['droplets'] else 'OFF'}"
          f" -> peak in the air {peak}, born {born:.2f} l, landed {landed:.2f} l, still flying {after['in_flight_m3'] * 1000.0:.2f} l")


def apply(api, st):
    print("  droplets", api.debug("water_droplets", {"enabled": st["droplets"], "size": 1.0 / st["size"]}).get("size"))
    api.debug("water_ripples", {"enabled": st["ripples"]})


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--url", default="http://127.0.0.1:8111")
    ap.add_argument("--size", type=float, default=9, help="droplet size as 1/N of a cell (9 = default, 27 = finest)")
    ap.add_argument("--height", type=float, default=3.5, help="drop height above the pond (m)")
    ap.add_argument("--big", action="store_true", help="drop a 1 m stone instead of 1/3 m")
    ap.add_argument("--pose", choices=sorted(POSES), default="mid")
    ap.add_argument("--watch", type=float, default=3.0, help="seconds to count droplets after each drop")
    ap.add_argument("--auto", type=int, default=0, help="drop N stones without prompting")
    ap.add_argument("--interval", type=float, default=4.0, help="seconds between --auto drops (after the watch)")
    args = ap.parse_args()

    api = Api(args.url)
    api.wait_status(60)
    st = {"size": args.size, "height": args.height, "big": args.big, "droplets": True, "ripples": True}
    build_pond(api)
    apply(api, st)
    camera_set(api, POSES[args.pose])
    print("pond built; letting it settle 4 s")
    time.sleep(4)

    if args.auto:
        for _ in range(args.auto):
            drop(api, st, args.watch)
            time.sleep(args.interval)
        return

    print(__doc__.split("At the prompt:")[1])
    while True:
        try:
            cmd = input(f"[size 1/{st['size']:g}, {'big' if st['big'] else 'small'} stone, h {st['height']:g} m] > ").strip().lower()
        except EOFError:
            break
        if cmd == "":
            drop(api, st, args.watch)
        elif cmd == "q":
            break
        elif cmd in ("big", "small"):
            st["big"] = cmd == "big"
        elif cmd.startswith("h "):
            st["height"] = float(cmd.split()[1])
        elif cmd == "r":
            build_pond(api); apply(api, st); print("  pond rebuilt"); time.sleep(3)
        elif cmd == "rip":
            st["ripples"] = not st["ripples"]; apply(api, st); print("  ripples", "on" if st["ripples"] else "off")
        elif cmd == "d":
            st["droplets"] = not st["droplets"]; apply(api, st)
        elif cmd == "cam":
            camera_set(api, POSES[args.pose])
        else:
            try:
                st["size"] = float(cmd); apply(api, st)
            except ValueError:
                print("  ? (Enter, 9/18/27, big/small, h <m>, r, rip, d, cam, q)")


if __name__ == "__main__":
    main()
