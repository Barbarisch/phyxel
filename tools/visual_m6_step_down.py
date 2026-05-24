"""Visual confirmation for Phase M6 auto-step-down.

Character stands on top of a 1-block elevated platform. The script triggers
the override-driven try_climb_down (we cannot reliably make the player walk
forward via the API, but the override still exercises the same mantle path).

Output: 5 screenshots showing the eased descent.
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
    print(f"[visualM6] {label:14s} -> {r.get('path')}")
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

            # Clear a generous region.
            c.post(f"{base}/api/world/clear", json={
                "x1": fx - 5, "y1": fy, "z1": fz - 5,
                "x2": fx + 5, "y2": fy + 8, "z2": fz + 10,
            })
            time.sleep(0.6)

            # Build a 3x3 elevated platform under the character's current spot
            # AND extending forward; the forward-edge will be the step-down ledge.
            # Platform top at y=fy+1 (1m above ground), so when character is on
            # top at y=fy+1 and walks forward off the edge, drop is 1m.
            for off_s in (-1, 0, 1):
                for off_f in (0, 1, 2):
                    vx = fx + sx * off_s + dx * off_f
                    vz = fz + sz * off_s + dz * off_f
                    c.post(f"{base}/api/world/voxel",
                           json={"x": vx, "y": fy, "z": vz, "material": "Stone"}).json()
            time.sleep(0.4)

            # Lift the character onto the platform via a quick override mantle.
            top_y = fy + 1.0
            c.post(f"{base}/api/interaction/try_climb_up", json={
                "start":    [pos[0], pos[1], pos[2]],
                "end":      [pos[0], top_y, pos[2]],
                "duration": 0.25,
                "clip":     "",
            }).json()
            time.sleep(0.5)

            # Camera: side view, look at the step-down direction.
            cam_x = fx + sx * 5.0 + dx * 1.5
            cam_z = fz + sz * 5.0 + dz * 1.5
            cam_y = fy + 2.5
            if sx > 0:   cam_yaw = 180.0
            elif sx < 0: cam_yaw = 0.0
            elif sz > 0: cam_yaw = -90.0
            else:        cam_yaw = 90.0
            c.post(f"{base}/api/camera", json={
                "position": {"x": cam_x, "y": cam_y, "z": cam_z},
                "yaw": cam_yaw, "pitch": -10.0,
            }).json()
            time.sleep(0.3)

            cap(c, base, "00_on_top", frames)

            # Trigger step-down mantle. From (x, top_y, z) to ground forward of
            # the platform edge. Forward edge is dx*2/dz*2 away from spawn.
            start_pos = [float(fx) + dx * 1.0, top_y, float(fz) + dz * 1.0]
            end_pos   = [float(fx) + dx * 3.0, float(fy), float(fz) + dz * 3.0]
            t0 = time.time()
            r = c.post(f"{base}/api/interaction/try_climb_down", json={
                "start":    start_pos,
                "end":      end_pos,
                "duration": 0.7,
                "clip":     "",
            }).json()
            print(f"[visualM6] try_climb_down -> started={r.get('started')}")

            # Mantle duration 0.7s — sample mid + landed.
            for t_target, label in [(0.18, "01_t018"),
                                    (0.40, "02_t040"),
                                    (0.62, "03_t062"),
                                    (1.20, "04_landed")]:
                while time.time() - t0 < t_target:
                    time.sleep(0.01)
                cap(c, base, label, frames)

    print("\n[visualM6] === captured frames ===")
    for label, path in frames:
        print(f"  {label}: {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
