"""Phase M7 smoke: ladder ascent/descent.

Validates the continuous ladder climb:
  1. ladder/start locks XZ to the rail and snaps Y into the rail extent.
  2. ladder/input vertical=+1 raises Y at climb speed.
  3. ladder/input vertical=-1 lowers Y.
  4. Reaching top_y auto-mantles off; ladder is no longer active.
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
            for _ in range(20):
                try:
                    if c.get(f"{base}/api/placed_objects", timeout=3.0).status_code == 200:
                        break
                except Exception:
                    pass
                time.sleep(0.5)

            c.post(f"{base}/api/character/spawn_for_test", json={"appearance": {}}).json()
            time.sleep(0.3)

            # 1) Start ladder: rail at (50, 50), Y extent 17..22, speed 2 m/s.
            rail_x, rail_z = 50.0, 50.0
            top_y, bot_y   = 22.0, 17.0
            r = c.post(f"{base}/api/interaction/ladder/start", json={
                "rail_x": rail_x, "rail_z": rail_z,
                "top_y": top_y,   "bottom_y": bot_y,
                "speed": 2.0,
            }).json()
            print(f"[phaseM7] start={r}")
            if not r.get("started"):
                fails.append(f"ladder/start failed: {r}")
                return 1
            pos = r["position"]
            if abs(pos[0] - rail_x) > 0.05 or abs(pos[2] - rail_z) > 0.05:
                fails.append(f"XZ not snapped to rail: pos={pos} rail=({rail_x},{rail_z})")
            if pos[1] < bot_y - 0.05 or pos[1] > top_y + 0.05:
                fails.append(f"Y not clamped to rail extent: {pos[1]} not in [{bot_y},{top_y}]")

            # 2) Climb up for ~0.4s. Expected delta ≈ 0.8 m (2 m/s × 0.4 s).
            y0 = pos[1]
            c.post(f"{base}/api/interaction/ladder/input", json={"vertical": 1.0})
            time.sleep(0.4)
            r = c.post(f"{base}/api/interaction/ladder/input", json={"vertical": 0.0}).json()
            print(f"[phaseM7] after up={r}")
            y1 = r["position"][1]
            if y1 - y0 < 0.5:
                fails.append(f"Climb up did not progress: y0={y0:.3f} y1={y1:.3f}")
            if not r.get("on_ladder"):
                fails.append("Character should still be on ladder")

            # 3) Climb down for ~0.3s.
            c.post(f"{base}/api/interaction/ladder/input", json={"vertical": -1.0})
            time.sleep(0.3)
            r = c.post(f"{base}/api/interaction/ladder/input", json={"vertical": 0.0}).json()
            print(f"[phaseM7] after down={r}")
            y2 = r["position"][1]
            if y1 - y2 < 0.4:
                fails.append(f"Climb down did not progress: y1={y1:.3f} y2={y2:.3f}")

            # 4) Drive to top → should auto-mantle off.
            c.post(f"{base}/api/interaction/ladder/input", json={"vertical": 1.0})
            time.sleep(3.0)  # plenty of time to reach top_y from anywhere in [bot,top]
            r = c.get(f"{base}/api/character/contact").json()
            print(f"[phaseM7] post-top contact={r}")
            # After auto-mantle finishes, character should no longer be on the rail XZ.
            ppos = r.get("position", [0, 0, 0])
            if abs(ppos[0] - rail_x) < 0.05 and abs(ppos[2] - rail_z) < 0.05:
                fails.append(f"Character should have mantled off the rail XZ: pos={ppos}")

            # 5) ladder/end after detached should still respond.
            r = c.post(f"{base}/api/interaction/ladder/end", json={}).json()
            print(f"[phaseM7] end={r}")
            if r.get("on_ladder"):
                fails.append(f"Ladder should be inactive after end: {r}")

    print("\n[phaseM7] === verdict ===")
    if fails:
        for f in fails: print(f"  FAIL: {f}")
        return 1
    print("  PASS: ladder ascent/descent + auto-mantle at top")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
