"""Phase M4 smoke: try_pull (coupled animation).

Spawn drawer_test, POST try_pull with total_offset_m=0.4, verify the
target offset is set; then POST again with delta_m via try_slide and
verify the relative path also works.
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

            r = c.post(f"{base}/api/world/template", json={
                "name": "drawer_test",
                "position": {"x": 30, "y": 17, "z": 30},
                "static": True,
                "rotation": 0,
            })
            dj = r.json()
            print(f"[phaseM4] drawer spawn={dj}")
            if not dj.get("success"):
                fails.append(f"drawer spawn failed: {dj}")
                return 1
            drawer_id = dj["object_id"]

            # try_pull
            r = c.post(f"{base}/api/interaction/try_pull", json={
                "object_id": drawer_id,
                "part_index": 0,
                "total_offset_m": 0.4,
                "speed_mps": 0.5,
                "clip": "pull",
            })
            j = r.json()
            print(f"[phaseM4] try_pull={j}")
            if not j.get("success"):
                fails.append(f"try_pull failed: {j}")
            if abs(j.get("target_offset_m", 0.0) - 0.4) > 1e-6:
                fails.append(f"target_offset_m mismatch: {j}")
            if not j.get("clip_played"):
                fails.append(f"pull clip did not play: clip_error={j.get('clip_error')}")

            # Second pull: relative — should add to current offset.
            r = c.post(f"{base}/api/interaction/try_pull", json={
                "object_id": drawer_id,
                "part_index": 0,
                "total_offset_m": 0.2,
                "speed_mps": 0.0,  # snap
            })
            j = r.json()
            print(f"[phaseM4] try_pull #2 (accumulate)={j}")
            cur2 = j.get("current_offset_m", 0.0)
            if abs(j.get("target_offset_m", 0.0) - (cur2 + 0.2)) > 1e-3:
                fails.append(f"second pull did not accumulate: cur={cur2} target={j.get('target_offset_m')}")

            # delta_m via try_slide (Phase M4 alternative).
            # Snap to a known value first, then apply delta.
            c.post(f"{base}/api/interaction/try_slide", json={
                "object_id": drawer_id, "part_index": 0,
                "offset_m": 0.5, "speed_mps": 0.0,
            })
            time.sleep(0.05)
            r = c.post(f"{base}/api/interaction/try_slide", json={
                "object_id": drawer_id,
                "part_index": 0,
                "delta_m": -0.3,
                "speed_mps": 0.0,
            })
            j = r.json()
            print(f"[phaseM4] try_slide delta_m={j}")
            if abs(j.get("target_offset_m", 0.0) - 0.2) > 0.05:
                fails.append(f"delta_m did not subtract from 0.5: target={j.get('target_offset_m')}")

    print("\n[phaseM4] === verdict ===")
    if fails:
        for f in fails: print(f"  FAIL: {f}")
        return 1
    print("  PASS: try_pull coupling + delta_m on try_slide both work")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
