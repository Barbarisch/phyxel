"""Phase H smoke test: drawer + chest_closed spawn + animate via try_slide / try_pivot.

Validates:
  1. The `slide=z+` part directive routes through KinematicVoxelManager.
  2. /api/interaction/try_slide drives the part offset and echoes parameters.
  3. chest_closed.voxel (migrated to # part: directive) still spawns and its
     lid pivots via try_pivot.
  4. Negative cases: bad part_index, unknown character_id.
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
            # Warmup probe.
            for _ in range(20):
                try:
                    rr = c.get(f"{base}/api/placed_objects", timeout=3.0)
                    if rr.status_code == 200:
                        break
                except Exception:
                    pass
                time.sleep(0.5)

            # ---- 1) Drawer slide ----
            r = c.post(f"{base}/api/world/template", json={
                "name": "drawer_test",
                "position": {"x": 30, "y": 17, "z": 30},
                "static": True,
                "rotation": 0,
            })
            print(f"[phaseH] drawer spawn body={r.text[:250]}")
            dj = r.json()
            if not dj.get("success"):
                fails.append(f"drawer spawn failed: {dj}")
                return 1
            drawer_id = dj["object_id"]

            r = c.get(f"{base}/api/placed_object", params={"id": drawer_id})
            md = (r.json().get("metadata") or {})
            kids = md.get("kinematic_part_ids") or []
            print(f"[phaseH] drawer kinematic_part_ids={kids}")
            if len(kids) != 1:
                fails.append(f"drawer should have 1 movable part, got {kids}")

            r = c.post(f"{base}/api/interaction/try_slide", json={
                "object_id": drawer_id,
                "part_index": 0,
                "offset_m": 0.35,
                "speed_mps": 0.0,
            })
            sr = r.json()
            print(f"[phaseH] drawer try_slide body={r.text[:300]}")
            if not sr.get("success"):
                fails.append(f"try_slide failed: {sr}")
            if abs(sr.get("target_offset_m", 0.0) - 0.35) > 1e-6:
                fails.append(f"target_offset_m not echoed: {sr}")
            if sr.get("part_id") != (kids[0] if kids else None):
                fails.append(f"slide part_id mismatch: {sr}")

            # Negative: bad part_index
            r = c.post(f"{base}/api/interaction/try_slide", json={
                "object_id": drawer_id,
                "part_index": 99,
                "offset_m": 0.0,
            })
            j = r.json()
            print(f"[phaseH] bad slide part_index body={r.text[:200]}")
            if j.get("success", True):
                fails.append("expected success=false for bad slide part_index")

            # Negative: missing character (still succeeds on slide path).
            r = c.post(f"{base}/api/interaction/try_slide", json={
                "object_id": drawer_id,
                "part_index": 0,
                "offset_m": 0.1,
                "speed_mps": 0.0,
                "character_id": "no_such_character",
                "clip": "open_door_in",
            })
            j = r.json()
            print(f"[phaseH] slide+missing char body={r.text[:250]}")
            if not j.get("success"):
                fails.append(f"slide+missing-char should still succeed: {j}")
            if j.get("clip_played") is not False:
                fails.append(f"slide clip_played should be false: {j}")

            # ---- 2) chest_closed migration via try_pivot ----
            r = c.post(f"{base}/api/world/template", json={
                "name": "chest_closed",
                "position": {"x": 35, "y": 17, "z": 30},
                "static": True,
                "rotation": 0,
            })
            print(f"[phaseH] chest_closed spawn body={r.text[:250]}")
            cj = r.json()
            if not cj.get("success"):
                fails.append(f"chest_closed spawn failed: {cj}")
            else:
                cid = cj["object_id"]
                r = c.get(f"{base}/api/placed_object", params={"id": cid})
                cmd = (r.json().get("metadata") or {})
                ckids = cmd.get("kinematic_part_ids") or []
                print(f"[phaseH] chest_closed kinematic_part_ids={ckids}")
                if len(ckids) != 1:
                    fails.append(f"chest_closed should expose 1 movable part (lid), got {ckids}")
                else:
                    r = c.post(f"{base}/api/interaction/try_pivot", json={
                        "object_id": cid,
                        "part_index": 0,
                        "angle_deg": 110.0,
                        "speed_deg": 0.0,
                    })
                    pr = r.json()
                    print(f"[phaseH] chest_closed pivot body={r.text[:280]}")
                    if not pr.get("success"):
                        fails.append(f"chest_closed pivot failed: {pr}")

    print("\n[phaseH] === verdict ===")
    if fails:
        for f in fails: print(f"  FAIL: {f}")
        return 1
    print("  PASS: slide directive + try_slide + chest_closed migration all wired")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
