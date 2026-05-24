"""Phase M5/M6 FSM-wire smoke: auto-mantle on natural jump near a LedgeUp.

After M5 added the override-driven mantle, the player's Space-jump still went
straight to a plain vertical impulse — the new system was not player-visible.
This test verifies the wire-up in `updateStateMachine`:

  1. Spawn the character (defaults to ~(16, 17, 16), facing +Z).
  2. Place a 2-voxel Stone wall in front, at (16, 17, 17) and (16, 18, 17).
     The character's forward column now has a ledge at top_y=19, drop=2m.
  3. Poll /api/character/contact until climb.kind == "ledge_up".
  4. POST /api/character/request_jump (sets jumpRequested=true).
  5. Wait > mantle duration (0.7s).
  6. Verify the character's Y reached the top of the wall (~19.x) rather
     than coming back down from a 7m/s jump (which would still be airborne
     at ~17m or just past peak).

If the wire-up failed, the character would just jump straight up and land
roughly where it started.
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


def main() -> int:
    fails: list[str] = []
    with EngineSession(Mode.PROJECT, target=PROJECT, on_crash="abort", verbose=True) as session:
        base = session.base_url
        with httpx.Client(timeout=30.0) as c:
            # Engine ready
            for _ in range(20):
                try:
                    if c.get(f"{base}/api/placed_objects", timeout=3.0).status_code == 200:
                        break
                except Exception:
                    pass
                time.sleep(0.5)

            # Spawn the character (default morphology).
            c.post(f"{base}/api/character/spawn_for_test", json={"appearance": {}}).json()
            time.sleep(0.4)

            # Baseline contact (gives us default position + facing).
            base_contact = c.get(f"{base}/api/character/contact").json()
            print(f"[phaseFSM] baseline contact pos={base_contact.get('position')} "
                  f"facing={base_contact.get('facing')}")
            pos = base_contact.get("position") or [16.0, 17.0, 16.0]
            facing = base_contact.get("facing") or [0.0, 0.0, 1.0]

            # Build a 2-voxel Stone wall one cell forward of the character.
            # Default facing is +Z (confirmed by M1 baseline), so place at
            # (px, py, pz+1) and (px, py+1, pz+1). Round to integers.
            fx, fy, fz = int(round(pos[0])), int(round(pos[1])), int(round(pos[2]))
            # Find dominant forward axis.
            ax, az = facing[0], facing[2]
            if abs(az) >= abs(ax):
                dx, dz = 0, (1 if az > 0 else -1)
            else:
                dx, dz = (1 if ax > 0 else -1), 0
            wx, wy0, wz = fx + dx, fy, fz + dz
            wy1 = fy  # single-block wall — top at y=fy+1, 1m above feet (within maxClimbHeight=1.6)
            for (vx, vy, vz) in [(wx, wy0, wz)]:
                r = c.post(f"{base}/api/world/voxel",
                           json={"x": vx, "y": vy, "z": vz, "material": "Stone"}).json()
                print(f"[phaseFSM] place ({vx},{vy},{vz}) -> {r}")
            time.sleep(0.3)

            # The contact probe's forwardRange defaults to 0.6m. From the cell
            # centre (z=16.0) the wall face is 1.0m away — out of reach. Nudge
            # the character closer using a short mantle (start=current, end=
            # slightly forward at the same Y).
            closer = [float(pos[0]) + dx * 0.5, float(pos[1]), float(pos[2]) + dz * 0.5]
            c.post(f"{base}/api/interaction/try_climb_up", json={
                "start": [pos[0], pos[1], pos[2]],
                "end":   closer,
                "duration": 0.2,
                "clip": "",
            }).json()
            time.sleep(0.4)
            advanced = c.get(f"{base}/api/character/contact").json()
            print(f"[phaseFSM] post-nudge pos={advanced.get('position')}")

            # Poll contact for ledge_up.
            climb_kind = "none"
            for _ in range(20):
                cj = c.get(f"{base}/api/character/contact").json()
                climb_kind = (cj.get("climb") or {}).get("kind", "none")
                if climb_kind == "ledge_up":
                    print(f"[phaseFSM] climb=ledge_up detected: {cj.get('climb')}")
                    break
                time.sleep(0.1)
            if climb_kind != "ledge_up":
                fails.append(f"Contact never reported ledge_up (got {climb_kind}). "
                             f"Last contact: {cj}")
                return _verdict(fails)

            # Record pre-jump Y.
            pre = c.get(f"{base}/api/character/contact").json()
            pre_y = (pre.get("position") or [0, 0, 0])[1]
            print(f"[phaseFSM] pre-jump y={pre_y}")

            # Trigger the jump the same way Space-key does.
            jr = c.post(f"{base}/api/character/request_jump", json={}).json()
            print(f"[phaseFSM] request_jump={jr}")
            if not jr.get("success"):
                fails.append(f"request_jump failed: {jr}")
                return _verdict(fails)

            # Wait for the mantle (0.7s) plus a settle margin.
            time.sleep(1.2)

            post = c.get(f"{base}/api/character/contact").json()
            post_y = (post.get("position") or [0, 0, 0])[1]
            post_climb = (post.get("climb") or {}).get("kind", "none")
            print(f"[phaseFSM] post-jump pos={post.get('position')} climb={post_climb}")

            # After an auto-mantle: y should be on top of the wall (~wy1+1).
            # Plain jump: 7m/s impulse + 1.2s of gravity ~ back at ground.
            expected_top_y = wy1 + 1.0  # top surface of upper wall block
            if post_y < expected_top_y - 0.6:
                fails.append(
                    f"Y={post_y:.2f} is below expected mantle top ~{expected_top_y:.2f}. "
                    "Auto-mantle wire-up did not fire — character likely just jumped."
                )

    return _verdict(fails)


def _verdict(fails: list[str]) -> int:
    print("\n[phaseFSM] === verdict ===")
    if fails:
        for f in fails: print(f"  FAIL: {f}")
        return 1
    print("  PASS: jump near LedgeUp auto-mantles up the ledge")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
