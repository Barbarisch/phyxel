"""Phase M6 smoke: try_climb_down (step-down mantle).

Validates the override path of /api/interaction/try_climb_down — the
direction-aware easing should land the character cleanly at end.y < start.y.
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

            # 1) No ledge_down → success=false.
            r = c.post(f"{base}/api/interaction/try_climb_down", json={}).json()
            print(f"[phaseM6] no-contact={r}")
            if r.get("success"):
                fails.append(f"Expected success=false on flat ground: {r}")
            if r.get("climb_kind") not in ("none", "ledge_up", "ledge_down", "ladder"):
                fails.append(f"climb_kind invalid: {r}")

            # 2) Override: descend 1.2 m over 0.5 s.
            start = [40.0, 18.5, 40.0]
            end   = [41.0, 17.3, 40.0]
            duration = 0.5
            r = c.post(f"{base}/api/interaction/try_climb_down", json={
                "start": start, "end": end, "duration": duration, "clip": "step_down",
            }).json()
            print(f"[phaseM6] override start={r}")
            if not r.get("success"):
                fails.append(f"Override try_climb_down failed: {r}")
                return 1

            # 3) Mid-mantle sample (~25% in): with back-loaded Y easing, the
            #    character should still be HIGH (closer to start.y than end.y).
            time.sleep(0.12)
            mid = c.get(f"{base}/api/character/contact").json().get("position", [0, 0, 0])
            print(f"[phaseM6] t~0.25 pos={mid}")
            # Y should still be much closer to start (18.5) than to end (17.3).
            if mid[1] < 18.0:
                fails.append(f"Y dropped too early (back-loading broken): mid.y={mid[1]:.3f}")

            # 4) After full duration, verify end reached.
            time.sleep(duration + 0.3)
            pos = c.get(f"{base}/api/character/contact").json().get("position", [0, 0, 0])
            print(f"[phaseM6] post-mantle pos={pos}")
            dx = abs(pos[0] - end[0]); dz = abs(pos[2] - end[2])
            if dx > 0.15 or dz > 0.15:
                fails.append(f"XZ off-target: pos={pos} end={end}")
            # Y could be snapped by ground search to the nearest column top — give it room.
            if pos[1] > start[1] - 0.5:
                fails.append(f"Y did not descend: start={start[1]} end={end[1]} got={pos[1]}")

    print("\n[phaseM6] === verdict ===")
    if fails:
        for f in fails: print(f"  FAIL: {f}")
        return 1
    print("  PASS: try_climb_down override mantle descends with back-loaded easing")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
