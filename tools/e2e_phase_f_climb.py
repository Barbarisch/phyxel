"""Phase F smoke: ladder_test spawns, climb kind registered.

Phase F is scaffold-only — runtime XZ-lock + climb FSM state remain
deferred. This just verifies the asset+kind registry plumbing.
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

    if "climb" not in all_kinds():
        fails.append("climb kind not registered in interaction_kinds registry")

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
                "name": "ladder_test",
                "position": {"x": 40, "y": 17, "z": 30},
                "static": True,
                "rotation": 0,
            })
            j = r.json()
            print(f"[phaseF] ladder spawn body={r.text[:250]}")
            if not j.get("success"):
                fails.append(f"ladder spawn failed: {j}")
            else:
                lid = j["object_id"]
                r = c.get(f"{base}/api/placed_object", params={"id": lid})
                md = (r.json().get("metadata") or {})
                kids = md.get("kinematic_part_ids") or []
                print(f"[phaseF] ladder kinematic_part_ids={kids}")
                # Ladder is static — should have no movable parts.
                if kids:
                    fails.append(f"ladder should have no movable parts, got {kids}")

    print("\n[phaseF] === verdict ===")
    if fails:
        for f in fails: print(f"  FAIL: {f}")
        return 1
    print("  PASS: climb kind registered + ladder_test asset spawns clean")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
