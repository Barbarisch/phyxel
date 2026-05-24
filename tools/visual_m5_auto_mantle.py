"""Visual confirmation for Phase M5 auto-mantle.

Captures 5 frames of the character climbing a single-block Stone wall via
the auto-mantle path (jump near LedgeUp). Side-on camera so the climb
arc is visible.

Output: screenshots/m5_visual_*.png (engine writes them).
The script prints the captured paths at the end — open them to inspect.
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


def cap(c: httpx.Client, base: str, label: str, frames: list) -> None:
    r = c.get(f"{base}/api/screenshot", timeout=10.0).json()
    print(f"[visualM5] {label:14s} -> {r.get('path')}")
    frames.append((label, r.get("path")))


def main() -> int:
    frames: list[tuple[str, str]] = []
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

            base_contact = c.get(f"{base}/api/character/contact").json()
            pos = base_contact.get("position") or [16.0, 17.0, 16.0]
            facing = base_contact.get("facing") or [0.0, 0.0, 1.0]
            print(f"[visualM5] pos={pos} facing={facing}")

            # Forward dominant axis.
            ax, az = facing[0], facing[2]
            if abs(az) >= abs(ax):
                dx, dz = 0, (1 if az > 0 else -1)
            else:
                dx, dz = (1 if ax > 0 else -1), 0

            fx, fy, fz = int(round(pos[0])), int(round(pos[1])), int(round(pos[2]))

            # Wipe a generous test region (CharacterTestbed accumulates voxels
            # across runs because --project persists to the project DB). Range:
            #   X: fx-4 .. fx+4
            #   Y: fy   .. fy+8   (keep ground intact at y=fy is fine — fy is feet)
            #   Z: fz-4 .. fz+10  (includes the wall position fz+dz*1)
            c.post(f"{base}/api/world/clear", json={
                "x1": fx - 4, "y1": fy, "z1": fz - 4,
                "x2": fx + 4, "y2": fy + 8, "z2": fz + 10,
            })
            time.sleep(0.6)  # async clear

            # Place a clean 3-wide Stone wall one cell forward of the
            # character. Top surface is at y=fy+1, giving a 1.0m ledge — well
            # within the 1.6m mantle reach. Wall is 1-block deep × 1-block tall
            # × 3-wide along the side axis so the climb has a visible target
            # and a small landing platform.
            wx = fx + dx
            wz = fz + dz
            # Side axis (perpendicular to forward).
            sx, sz = (1, 0) if dz != 0 else (0, 1)
            for off in (-1, 0, 1):
                vx = wx + sx * off
                vz = wz + sz * off
                c.post(f"{base}/api/world/voxel",
                       json={"x": vx, "y": fy, "z": vz, "material": "Stone"}).json()
            # Add a second row one cell beyond the wall in the forward direction
            # so the character has somewhere to land after the mantle.
            for off in (-1, 0, 1):
                vx = wx + dx + sx * off
                vz = wz + dz + sz * off
                c.post(f"{base}/api/world/voxel",
                       json={"x": vx, "y": fy, "z": vz, "material": "Stone"}).json()
            time.sleep(0.4)

            # Camera: side-on view perpendicular to facing direction.
            # Per CLAUDE.md: front = (cos(yaw), sin(yaw)). To look along -X from
            # +X side, yaw=180. To look along -Z from +Z, yaw=-90.
            cam_x = fx + sx * 5.0 + dx * 0.5
            cam_z = fz + sz * 5.0 + dz * 0.5
            cam_y = fy + 2.0
            if sx > 0:
                cam_yaw = 180.0
            elif sx < 0:
                cam_yaw = 0.0
            elif sz > 0:
                cam_yaw = -90.0
            else:
                cam_yaw = 90.0
            c.post(f"{base}/api/camera", json={
                "position": {"x": cam_x, "y": cam_y, "z": cam_z},
                "yaw": cam_yaw,
                "pitch": -8.0,
            }).json()
            time.sleep(0.3)

            # Nudge character within probe forwardRange (0.6m). Without this the
            # contact probe doesn't see the wall and ledge_up never triggers.
            closer = [float(pos[0]) + dx * 0.5, float(pos[1]), float(pos[2]) + dz * 0.5]
            c.post(f"{base}/api/interaction/try_climb_up", json={
                "start":    [pos[0], pos[1], pos[2]],
                "end":      closer,
                "duration": 0.2,
                "clip":     "",
            }).json()
            time.sleep(0.4)

            # Confirm ledge_up before triggering jump.
            ledge_up = False
            for _ in range(20):
                cj = c.get(f"{base}/api/character/contact").json()
                if (cj.get("climb") or {}).get("kind") == "ledge_up":
                    ledge_up = True
                    break
                time.sleep(0.1)
            print(f"[visualM5] ledge_up={ledge_up}")
            if not ledge_up:
                print("[visualM5] FAIL: never reached ledge_up; aborting capture")
                return 1

            # === Capture sequence ===
            cap(c, base, "00_pre_jump", frames)

            t0 = time.time()
            c.post(f"{base}/api/character/request_jump", json={}).json()
            # Mantle duration = 0.7s. Sample 3 mid-frames + 1 settled frame.
            for t_target, label in [(0.18, "01_t018"),
                                    (0.40, "02_t040"),
                                    (0.62, "03_t062"),
                                    (1.20, "04_landed")]:
                while time.time() - t0 < t_target:
                    time.sleep(0.01)
                cap(c, base, label, frames)

    print("\n[visualM5] === captured frames ===")
    for label, path in frames:
        print(f"  {label}: {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
