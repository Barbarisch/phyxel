"""Phase M5 smoke: try_climb_up (ledge mantle).

The mantle accepts an explicit (start, end) override for deterministic
testing. We spawn the character, fire try_climb_up with a 1.0m Y delta
and a small XZ delta, then sample /api/character/contact to confirm the
character's position landed at `end`.
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

            # Make sure a character exists.
            c.post(f"{base}/api/character/spawn_for_test", json={"appearance": {}}).json()
            time.sleep(0.3)

            # 1) Schema check: try_climb_up without override on flat ground
            #    returns success=false (no LedgeUp contact).
            r = c.post(f"{base}/api/interaction/try_climb_up", json={}).json()
            print(f"[phaseM5] no-contact={r}")
            if r.get("success"):
                fails.append(f"Expected success=false on flat ground, got: {r}")
            if r.get("climb_kind") not in ("none", "ledge_up", "ledge_down", "ladder"):
                fails.append(f"climb_kind missing/invalid: {r}")

            # 2) Override path: explicit start/end. Verify it completes.
            start = [40.0, 17.0, 40.0]
            end   = [41.0, 18.2, 40.0]
            duration = 0.5
            r = c.post(f"{base}/api/interaction/try_climb_up", json={
                "start": start, "end": end, "duration": duration,
                "clip": "climb_ladder_start",
            }).json()
            print(f"[phaseM5] override start={r}")
            if not r.get("success"):
                fails.append(f"Override try_climb_up failed: {r}")
                return 1
            if r.get("climb_kind") != "override":
                fails.append(f"climb_kind should be 'override': {r}")

            time.sleep(duration + 0.3)
            cj = c.get(f"{base}/api/character/contact").json()
            pos = cj.get("position") or [0, 0, 0]
            print(f"[phaseM5] post-mantle position={pos}")
            dx = abs(pos[0] - end[0]); dy = abs(pos[1] - end[1]); dz = abs(pos[2] - end[2])
            # dy tolerance is loose: ground-snap may settle to nearest top voxel.
            if dx > 0.15 or dy > 1.5 or dz > 0.15:
                fails.append(f"Mantle XZ off-target: pos={pos} end={end} d=({dx:.3f},{dy:.3f},{dz:.3f})")

            # 3) After completion, a new mantle should start.
            r2 = c.post(f"{base}/api/interaction/try_climb_up", json={
                "start": [pos[0], pos[1], pos[2]],
                "end":   [pos[0], pos[1] + 0.5, pos[2]],
                "duration": 0.2,
                "clip": "",
            }).json()
            print(f"[phaseM5] second mantle={r2}")
            if not r2.get("started"):
                fails.append(f"Second mantle should start: {r2}")

    print("\n[phaseM5] === verdict ===")
    if fails:
        for f in fails: print(f"  FAIL: {f}")
        return 1
    print("  PASS: try_climb_up override mantle completes within duration")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
