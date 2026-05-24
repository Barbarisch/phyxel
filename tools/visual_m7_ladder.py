"""Visual confirmation for Phase M7 ladder climb.

Builds a tall (5-block) Stone pillar as a visual placeholder for the ladder,
attaches a virtual ladder along its side, and captures the character
ascending and descending. Side camera so the continuous vertical motion is
visible.
"""
from __future__ import annotations

import sys
import time
from pathlib import Path

import httpx

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.interaction_pipeline.engine_lifecycle import EngineSession, Mode  # noqa: E402

PROJECT = r"C:\Users\jack\Documents\PhyxelProjects\CharacterTestbed"


def cap(c, base, label, frames):
    r = c.get(f"{base}/api/screenshot", timeout=10.0).json()
    print(f"[visualM7] {label:14s} -> {r.get('path')}")
    frames.append((label, r.get("path")))


def main() -> int:
    frames = []
    with EngineSession(Mode.PROJECT, target=PROJECT, on_crash="abort", verbose=True) as session:
        base = session.base_url
        with httpx.Client(timeout=30.0) as c:
            for _ in range(20):
                try:
                    if c.get(f"{base}/api/placed_objects", timeout=3.0).status_code == 200:
                        break
                except Exception:
                    pass
                time.sleep(0.5)

            c.post(f"{base}/api/character/spawn_for_test", json={"appearance": {}}).json()
            time.sleep(0.4)

            bc = c.get(f"{base}/api/character/contact").json()
            pos = bc.get("position") or [16.0, 17.0, 16.0]
            facing = bc.get("facing") or [0.0, 0.0, 1.0]
            fx, fy, fz = int(round(pos[0])), int(round(pos[1])), int(round(pos[2]))
            ax, az = facing[0], facing[2]
            if abs(az) >= abs(ax):
                dx, dz = 0, (1 if az > 0 else -1)
            else:
                dx, dz = (1 if ax > 0 else -1), 0
            sx, sz = (1, 0) if dz != 0 else (0, 1)

            # Clear.
            c.post(f"{base}/api/world/clear", json={
                "x1": fx - 5, "y1": fy, "z1": fz - 5,
                "x2": fx + 5, "y2": fy + 10, "z2": fz + 10,
            })
            time.sleep(0.6)

            # 5-block tall Stone pillar one cell forward of the character.
            # Top at y=fy+5, ladder rail face is the -dx/-dz face.
            px = fx + dx * 2
            pz = fz + dz * 2
            for h in range(5):
                c.post(f"{base}/api/world/voxel",
                       json={"x": px, "y": fy + h, "z": pz, "material": "Stone"}).json()
            # Top platform 3x3 so the auto-mantle off the top has somewhere to land.
            for off_s in (-1, 0, 1):
                for off_f in (0, 1):
                    vx = px + sx * off_s + dx * off_f
                    vz = pz + sz * off_s + dz * off_f
                    c.post(f"{base}/api/world/voxel",
                           json={"x": vx, "y": fy + 5, "z": vz, "material": "Stone"}).json()
            time.sleep(0.4)

            # Camera: side view of the pillar.
            cam_x = px + sx * 6.0
            cam_z = pz + sz * 6.0
            cam_y = fy + 3.5
            if sx > 0:   cam_yaw = 180.0
            elif sx < 0: cam_yaw = 0.0
            elif sz > 0: cam_yaw = -90.0
            else:        cam_yaw = 90.0
            c.post(f"{base}/api/camera", json={
                "position": {"x": cam_x, "y": cam_y, "z": cam_z},
                "yaw": cam_yaw, "pitch": -8.0,
            }).json()
            time.sleep(0.3)

            cap(c, base, "00_start", frames)

            # Begin ladder climb. Rail at (px - dx*0.5, pz - dz*0.5) so the
            # character stands flush against the pillar face. Top at fy+5
            # (= top of pillar), bottom at fy.
            rail_x = float(px) - dx * 0.5
            rail_z = float(pz) - dz * 0.5
            r = c.post(f"{base}/api/interaction/ladder/start", json={
                "rail_x": rail_x, "rail_z": rail_z,
                "top_y":  float(fy + 5),
                "bottom_y": float(fy),
                "climb_speed": 1.5,
            }).json()
            print(f"[visualM7] ladder/start -> {r}")

            # Ascend.
            c.post(f"{base}/api/interaction/ladder/input", json={"vertical": 1.0}).json()
            t0 = time.time()
            for t_target, label in [(1.0, "01_climb_t1"),
                                    (2.0, "02_climb_t2"),
                                    (3.5, "03_near_top")]:
                while time.time() - t0 < t_target:
                    time.sleep(0.01)
                cap(c, base, label, frames)

            # Stop input — at the top, the engine auto-mantles off.
            c.post(f"{base}/api/interaction/ladder/input", json={"vertical": 0.0}).json()
            time.sleep(1.0)
            cap(c, base, "04_on_top", frames)

    print("\n[visualM7] === captured frames ===")
    for label, path in frames:
        print(f"  {label}: {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
