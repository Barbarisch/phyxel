"""Phase C/D smoke test: composite-part spawn + animate via try_pivot.

Steps:
  1. Launch engine in --project mode against an existing testbed.
  2. Spawn the chest_test.voxel template (1 static base + 1 movable lid).
  3. Read back the PlacedObject — confirm metadata.kinematic_part_ids has 1 entry.
  4. POST /api/interaction/try_pivot with angle=90deg, speed=0 (snap).
  5. Poll the engine status briefly to allow one frame, then verify the
     kinematic transform via the placed object's metadata-mirrored fields
     (no direct KVM endpoint exists, so we infer success from the API
     response saying 'success' and the part_id round-tripping).

Clip-free: the character is not animated; only the lid rotates.
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
            # Warmup: heavy projects may need a few seconds before the dispatch
            # queue starts draining. Probe a known cheap endpoint.
            import time
            for _ in range(20):
                try:
                    rr = c.get(f"{base}/api/placed_objects", timeout=3.0)
                    if rr.status_code == 200:
                        break
                except Exception:
                    pass
                time.sleep(0.5)

            # 1) Spawn the composite template through the place path.
            r = c.post(f"{base}/api/world/template", json={
                "name": "chest_test",
                "position": {"x": 40, "y": 17, "z": 40},
                "static": True,
                "rotation": 0,
            })
            print(f"[phaseD] spawn status={r.status_code} body={r.text[:300]}")
            if r.status_code != 200:
                fails.append(f"spawn returned {r.status_code}")
                return 1
            spawn = r.json()
            if not spawn.get("success"):
                fails.append(f"spawn unsuccessful: {spawn}")
                return 1
            obj_id = spawn.get("object_id")
            if not obj_id:
                fails.append("no object_id in spawn response")
                return 1

            # 2) Inspect the placed object metadata.
            r = c.get(f"{base}/api/placed_object", params={"id": obj_id})
            print(f"[phaseD] placed_object status={r.status_code} body={r.text[:500]}")
            obj = r.json()
            md = obj.get("metadata") or {}
            kids = md.get("kinematic_part_ids") or []
            print(f"[phaseD] kinematic_part_ids={kids}")
            if len(kids) != 1:
                fails.append(f"expected 1 kinematic part, got {len(kids)}: {kids}")

            # 3) Fire try_pivot. speed=0 snaps so we can verify in a single
            # frame without polling for animation completion.
            r = c.post(f"{base}/api/interaction/try_pivot", json={
                "object_id": obj_id,
                "part_index": 0,
                "angle_deg": 90.0,
                "speed_deg": 0.0,
            })
            print(f"[phaseD] try_pivot status={r.status_code} body={r.text[:400]}")
            if r.status_code != 200:
                fails.append(f"try_pivot returned {r.status_code}")
            else:
                resp = r.json()
                if not resp.get("success"):
                    fails.append(f"try_pivot unsuccessful: {resp}")
                if resp.get("target_angle_deg") != 90.0:
                    fails.append(f"target_angle_deg not echoed: {resp}")
                if resp.get("part_id") != (kids[0] if kids else None):
                    fails.append(f"part_id mismatch: {resp.get('part_id')} vs {kids}")

            # 4) Negative path: bad part_index returns success=false with error.
            r = c.post(f"{base}/api/interaction/try_pivot", json={
                "object_id": obj_id,
                "part_index": 99,
                "angle_deg": 0.0,
                "speed_deg": 0.0,
            })
            print(f"[phaseD] bad part_index status={r.status_code} body={r.text[:200]}")
            j = r.json()
            if j.get("success", True):
                fails.append("expected success=false for bad part_index")

            # 5) Optional clip pass-through: clip_played=false when no
            # character_id resolves (engine refuses unknown id, but the
            # dispatch should still pivot the lid and return success=true).
            r = c.post(f"{base}/api/interaction/try_pivot", json={
                "object_id": obj_id,
                "part_index": 0,
                "angle_deg": 45.0,
                "speed_deg": 0.0,
                "character_id": "no_such_character",
                "clip": "open_lid",
            })
            print(f"[phaseD] clip(missing char) body={r.text[:300]}")
            j = r.json()
            if not j.get("success"):
                fails.append(f"clip+missing-char should still succeed: {j}")
            if j.get("clip_played") is not False:
                fails.append(f"clip_played should be false for missing char: {j}")
            if not j.get("clip_error"):
                fails.append(f"clip_error should be set: {j}")

            # 6) Door pivot — annotated door_wood with `# part: panel
            # hinge=left_bottom axis=y`. Spawn it and exercise the same path.
            r = c.post(f"{base}/api/world/template", json={
                "name": "door_wood",
                "position": {"x": 50, "y": 17, "z": 50},
                "static": True,
                "rotation": 0,
            })
            print(f"[phaseD] door spawn body={r.text[:200]}")
            dj = r.json()
            if not dj.get("success"):
                fails.append(f"door spawn failed: {dj}")
            else:
                door_id = dj["object_id"]
                r = c.get(f"{base}/api/placed_object", params={"id": door_id})
                door_md = (r.json().get("metadata") or {})
                door_kids = door_md.get("kinematic_part_ids") or []
                print(f"[phaseD] door kinematic_part_ids={door_kids}")
                if len(door_kids) != 1:
                    fails.append(f"door should have 1 movable part, got {door_kids}")
                else:
                    r = c.post(f"{base}/api/interaction/try_pivot", json={
                        "object_id": door_id,
                        "part_index": 0,
                        "angle_deg": 90.0,
                        "speed_deg": 0.0,
                    })
                    dr = r.json()
                    print(f"[phaseD] door try_pivot body={r.text[:300]}")
                    if not dr.get("success"):
                        fails.append(f"door pivot failed: {dr}")
                    if dr.get("part_id") != door_kids[0]:
                        fails.append(f"door part_id mismatch: {dr}")

    print("\n[phaseD] === verdict ===")
    if fails:
        for f in fails: print(f"  FAIL: {f}")
        return 1
    print("  PASS: composite spawn + pivot animation wired end-to-end")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
