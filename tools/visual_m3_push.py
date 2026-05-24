"""Visual confirmation for Phase M3 push.

Spawns a dynamic Stone cube in front of the character, captures the
moment before/after try_push so the impulse-driven motion is visible.
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
    print(f"[visualM3] {label:14s} -> {r.get('path')}")
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

            c.post(f"{base}/api/world/clear", json={
                "x1": fx - 5, "y1": fy, "z1": fz - 5,
                "x2": fx + 5, "y2": fy + 6, "z2": fz + 15,
            })
            time.sleep(0.6)

            # Spawn dynamic Stone cube ~1.1m in front of character.
            cube_x = pos[0] + dx * 1.1
            cube_z = pos[2] + dz * 1.1
            rsp = c.post(f"{base}/api/debug/spawn_bullet_cube", json={
                "x": cube_x, "y": fy, "z": cube_z,
                "material": "Stone", "scale": 1.0, "lifetime": 30.0,
            }).json()
            print(f"[visualM3] spawn cube -> {rsp.get('success')}")
            time.sleep(0.5)

            # Camera: side view, slightly elevated.
            cam_x = fx + sx * 5.0 + dx * 1.0
            cam_z = fz + sz * 5.0 + dz * 1.0
            cam_y = fy + 2.0
            if sx > 0:   cam_yaw = 180.0
            elif sx < 0: cam_yaw = 0.0
            elif sz > 0: cam_yaw = -90.0
            else:        cam_yaw = 90.0
            c.post(f"{base}/api/camera", json={
                "position": {"x": cam_x, "y": cam_y, "z": cam_z},
                "yaw": cam_yaw, "pitch": -10.0,
            }).json()
            time.sleep(0.3)

            cap(c, base, "00_pre_push", frames)

            t0 = time.time()
            r = c.post(f"{base}/api/interaction/try_push", json={
                "force": 12.0, "reach": 1.5, "clip": "push",
            }).json()
            print(f"[visualM3] try_push -> obj={r.get('object_id_pushed')} "
                  f"impulse={r.get('applied_impulse')}")

            for t_target, label in [(0.20, "01_t020"),
                                    (0.50, "02_t050"),
                                    (1.00, "03_t100"),
                                    (2.00, "04_settled")]:
                while time.time() - t0 < t_target:
                    time.sleep(0.01)
                cap(c, base, label, frames)

    print("\n[visualM3] === captured frames ===")
    for label, path in frames:
        print(f"  {label}: {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
