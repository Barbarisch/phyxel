"""Visual confirmation for door open/close using the new pipeline.

Boots the engine in project mode, clears a region, spawns `door_wood`,
registers it as an animated door, then captures frames through one full
open -> close cycle.

Output: screenshots/screenshot_*.png (engine writes them). Paths are
printed at the end.
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
    print(f"[visualDoor] {label:14s} -> {r.get('path')}")
    frames.append((label, r.get("path")))


def door_state(c: httpx.Client, base: str, poid: str) -> dict:
    r = c.get(f"{base}/api/doors", timeout=5.0).json()
    for d in r.get("doors", []):
        if d.get("placed_object_id") == poid:
            return d
    return {}


def main() -> int:
    frames: list[tuple[str, str]] = []
    with EngineSession(Mode.PROJECT, target=PROJECT, on_crash="abort", verbose=True) as session:
        base = session.base_url
        with httpx.Client(timeout=30.0) as c:
            # Wait for placed_objects route to be live.
            for _ in range(20):
                try:
                    if c.get(f"{base}/api/placed_objects", timeout=3.0).status_code == 200:
                        break
                except Exception:
                    pass
                time.sleep(0.5)

            # Spawn character to anchor a known pos; we don't actually use it
            # for interaction here, but it gives us a reference origin.
            c.post(f"{base}/api/character/spawn_for_test", json={"appearance": {}}).json()
            time.sleep(0.3)

            contact = c.get(f"{base}/api/character/contact").json()
            pos = contact.get("position") or [16.0, 17.0, 16.0]
            facing = contact.get("facing") or [0.0, 0.0, 1.0]
            ax, az = facing[0], facing[2]
            if abs(az) >= abs(ax):
                dx, dz = 0, (1 if az > 0 else -1)
            else:
                dx, dz = (1 if ax > 0 else -1), 0
            sx, sz = (1, 0) if dz != 0 else (0, 1)

            fx, fy, fz = int(round(pos[0])), int(round(pos[1])), int(round(pos[2]))
            print(f"[visualDoor] character pos=({fx},{fy},{fz}) forward=({dx},{dz}) side=({sx},{sz})")

            # Clear a region in front of and around the door site.
            c.post(f"{base}/api/world/clear", json={
                "x1": fx - 4, "y1": fy, "z1": fz - 4,
                "x2": fx + 6, "y2": fy + 6, "z2": fz + 8,
            })
            time.sleep(0.5)

            # Place door 2 cells forward of the character. The template's
            # local origin (0,0,0) sits at world (door_x, door_y, door_z) and
            # extends +X, +Y, +Z by (1, 2, 1). For a side-view test we put it
            # so its 1m face spans the side axis (sx, sz). Since door_wood is
            # 1x2x1 it doesn't matter which axis is "front" for visibility —
            # we'll rotate so the door's swing arc sweeps toward the camera.
            door_x = fx + dx * 2
            door_z = fz + dz * 2
            door_y = fy

            spawn_body = {
                "name": "door_wood",
                "position": {"x": door_x, "y": door_y, "z": door_z},
                "static": True,
                "rotation": 0,
            }
            sp = c.post(f"{base}/api/world/template", json=spawn_body).json()
            poid = sp.get("object_id")
            print(f"[visualDoor] spawn_template -> success={sp.get('success')} object_id={poid}")
            if not poid:
                print(f"[visualDoor] FAIL: spawn returned no object_id; resp={sp}")
                return 1
            time.sleep(0.4)

            # Register as door. Default hinge = placed object position (lower
            # corner of the door slab). open_angle=90 deg, swing_speed=120
            # deg/s -> full swing ~0.75s.
            reg = c.post(f"{base}/api/door/register", json={
                "placed_object_id":  poid,
                "template_name":     "door_wood",
                "base_rotation":     0,
                "open_angle":        90.0,
                "swing_speed":       120.0,
                "thickness":         2,
            }, timeout=30.0).json()
            print(f"[visualDoor] register_door -> {reg}")
            if not reg.get("success"):
                print("[visualDoor] FAIL: door registration did not succeed")
                return 1
            time.sleep(0.3)

            # Camera: side view, looking across the door so its swing arc is
            # perpendicular to the camera ray. The door hinges around Y, so a
            # camera offset along the side axis with a bit of forward bias
            # gives a 3/4 view of the swing.
            cam_x = door_x + sx * 4.5 + dx * (-1.5)
            cam_z = door_z + sz * 4.5 + dz * (-1.5)
            cam_y = door_y + 2.5
            if sx > 0:
                cam_yaw = 180.0
            elif sx < 0:
                cam_yaw = 0.0
            elif sz > 0:
                cam_yaw = -90.0
            else:
                cam_yaw = 90.0
            # Aim diagonally toward the door by biasing yaw 25 deg toward
            # forward axis.
            cam_yaw += (-25.0 if dx > 0 or dz > 0 else 25.0)
            c.post(f"{base}/api/camera", json={
                "position": {"x": cam_x, "y": cam_y, "z": cam_z},
                "yaw":      cam_yaw,
                "pitch":    -10.0,
            }).json()
            time.sleep(0.3)

            # === Capture: closed state ===
            st = door_state(c, base, poid)
            print(f"[visualDoor] pre-open state: is_open={st.get('is_open')} angle={st.get('current_angle'):.1f}"
                  if st else "[visualDoor] state lookup empty")
            cap(c, base, "00_closed", frames)

            # === Open ===
            t0 = time.time()
            r_open = c.post(f"{base}/api/door/open", json={"placed_object_id": poid}).json()
            print(f"[visualDoor] open -> {r_open}")
            for t_target, label in [(0.18, "01_open_t018"),
                                    (0.40, "02_open_t040"),
                                    (0.95, "03_open_full")]:
                while time.time() - t0 < t_target:
                    time.sleep(0.01)
                cap(c, base, label, frames)
            st = door_state(c, base, poid)
            if st:
                print(f"[visualDoor] after open: is_open={st.get('is_open')} "
                      f"angle={st.get('current_angle'):.1f} settled={st.get('settled')}")

            # === Close ===
            t0 = time.time()
            r_close = c.post(f"{base}/api/door/close", json={"placed_object_id": poid}).json()
            print(f"[visualDoor] close -> {r_close}")
            for t_target, label in [(0.20, "04_close_t020"),
                                    (0.55, "05_close_t055"),
                                    (1.10, "06_closed_final")]:
                while time.time() - t0 < t_target:
                    time.sleep(0.01)
                cap(c, base, label, frames)
            st = door_state(c, base, poid)
            if st:
                print(f"[visualDoor] after close: is_open={st.get('is_open')} "
                      f"angle={st.get('current_angle'):.1f} settled={st.get('settled')}")

            # Cleanup: unregister + delete the placed object so this test
            # doesn't leave a permanent door in the project DB.
            c.post(f"{base}/api/door/unregister", json={"placed_object_id": poid})
            try:
                c.request("DELETE", f"{base}/api/placed_objects/{poid}", timeout=5.0)
            except Exception:
                pass

    print("\n[visualDoor] === captured frames ===")
    for label, path in frames:
        print(f"  {label}: {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
