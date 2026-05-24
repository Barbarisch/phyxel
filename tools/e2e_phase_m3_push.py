"""Phase M3 smoke: try_push impulse on a dynamic cube.

Spawn a Stone dynamic cube in front of the character, POST try_push,
assert applied_impulse > 0 and object_id_pushed non-empty.
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
            # Wait until placed_objects answers.
            for _ in range(20):
                try:
                    if c.get(f"{base}/api/placed_objects", timeout=3.0).status_code == 200:
                        break
                except Exception:
                    pass
                time.sleep(0.5)

            # Character spawns at ~(16, 17, 16) facing +Z. Place a dynamic
            # cube ~1.1 m in front (center at z=17.1).
            r = c.post(f"{base}/api/debug/spawn_bullet_cube", json={
                "x": 15.5, "y": 17.0, "z": 16.6,
                "material": "Stone", "scale": 1.0, "lifetime": 30.0,
            })
            print(f"[phaseM3] spawn cube body={r.text[:160]}")
            if r.status_code != 200 or not r.json().get("success"):
                fails.append("spawn_bullet_cube failed")
                return 1

            # Let physics settle one tick.
            time.sleep(0.5)

            # Push.
            r = c.post(f"{base}/api/interaction/try_push", json={
                "force": 8.0, "reach": 1.0, "clip": "push",
            })
            print(f"[phaseM3] try_push body={r.text}")
            j = r.json()
            if not j.get("success"):
                fails.append(f"try_push not success: {j}")
            if not j.get("object_id_pushed"):
                fails.append(f"no object pushed (cone miss?): impulse={j.get('applied_impulse')}")
            ai = j.get("applied_impulse") or [0, 0, 0]
            mag = (ai[0]**2 + ai[1]**2 + ai[2]**2) ** 0.5
            if mag < 0.1:
                fails.append(f"applied_impulse too small: {ai}")
            if abs(ai[2]) < abs(ai[0]) + 1e-3:
                fails.append(f"impulse not forward-dominant: {ai}")

    print("\n[phaseM3] === verdict ===")
    if fails:
        for f in fails: print(f"  FAIL: {f}")
        return 1
    print("  PASS: try_push applied forward impulse on dynamic cube")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
