"""Phase I + J smoke: control/pose kinds registered, lever pivots, bedroll spawns.

Phase I (control) and Phase J (pose) are scaffold-only here. Runtime
control-event publishing and pose-FSM dispatch remain deferred.
"""
from __future__ import annotations

import sys
import time
from pathlib import Path

import httpx

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.interaction_pipeline.engine_lifecycle import EngineSession, Mode  # noqa: E402
from tools.interaction_pipeline.interaction_kinds import all_kinds  # noqa: E402


PROJECT = r"C:\Users\jack\Documents\PhyxelProjects\CharacterTestbed"


def main() -> int:
    fails: list[str] = []

    ks = all_kinds()
    for needed in ("control", "pose", "carry"):
        if needed not in ks:
            fails.append(f"{needed} kind not registered")

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

            # ---- Phase I: lever_test ----
            r = c.post(f"{base}/api/world/template", json={
                "name": "lever_test",
                "position": {"x": 45, "y": 17, "z": 30},
                "static": True,
                "rotation": 0,
            })
            j = r.json()
            print(f"[phaseIJ] lever spawn body={r.text[:250]}")
            if not j.get("success"):
                fails.append(f"lever spawn failed: {j}")
            else:
                lid = j["object_id"]
                r = c.get(f"{base}/api/placed_object", params={"id": lid})
                md = (r.json().get("metadata") or {})
                kids = md.get("kinematic_part_ids") or []
                print(f"[phaseIJ] lever kinematic_part_ids={kids}")
                if len(kids) != 1:
                    fails.append(f"lever should expose 1 movable part, got {kids}")
                else:
                    r = c.post(f"{base}/api/interaction/try_pivot", json={
                        "object_id": lid,
                        "part_index": 0,
                        "angle_deg": -45.0,
                        "speed_deg": 0.0,
                    })
                    pr = r.json()
                    print(f"[phaseIJ] lever pivot body={r.text[:250]}")
                    if not pr.get("success"):
                        fails.append(f"lever pivot failed: {pr}")

            # ---- Phase J: bedroll_test ----
            r = c.post(f"{base}/api/world/template", json={
                "name": "bedroll_test",
                "position": {"x": 48, "y": 17, "z": 30},
                "static": True,
                "rotation": 0,
            })
            j = r.json()
            print(f"[phaseIJ] bedroll spawn body={r.text[:250]}")
            if not j.get("success"):
                fails.append(f"bedroll spawn failed: {j}")
            else:
                bid = j["object_id"]
                r = c.get(f"{base}/api/placed_object", params={"id": bid})
                md = (r.json().get("metadata") or {})
                kids = md.get("kinematic_part_ids") or []
                if kids:
                    fails.append(f"bedroll should have no movable parts, got {kids}")

    print("\n[phaseIJ] === verdict ===")
    if fails:
        for f in fails: print(f"  FAIL: {f}")
        return 1
    print("  PASS: control/pose/carry kinds registered + lever pivots + bedroll spawns")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
