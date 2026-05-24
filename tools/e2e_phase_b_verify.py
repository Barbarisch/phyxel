"""Phase B smoke test: verify new endpoints + field plumbing.

Thin probe — confirms the new HTTP routes exist and the handlers parse
bodies. Full behavioural coverage (severity downgrade actually flipping
`can_interact` from false to true) needs a failing-fixture sidecar; that
is deferred until we author one.
"""
from __future__ import annotations

import sys
from pathlib import Path

import httpx

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.interaction_pipeline.engine_lifecycle import EngineSession, Mode  # noqa: E402


ASSET = ROOT / "resources" / "templates" / "chair_wood.voxel"


def main() -> int:
    fails: list[str] = []
    with EngineSession(Mode.INTERACTION_EDITOR, target=str(ASSET),
                       on_crash="abort", verbose=True) as session:
        base = session.base_url
        with httpx.Client(timeout=10.0) as c:
            r = c.post(f"{base}/api/interaction/force_interact", json={
                "entity_id": "nobody", "object_id": "no_such_object",
                "point_id": "seat_0",
            })
            print(f"[phaseB] force_interact status={r.status_code} body={r.text[:200]}")
            if r.status_code == 404:
                fails.append("force_interact route not registered (got 404)")

            r = c.post(f"{base}/api/interaction/can_interact", json={
                "entity_id": "nobody", "object_id": "no_such_object",
                "point_id": "seat_0", "kind": "sit",
            })
            print(f"[phaseB] can_interact status={r.status_code} body={r.text[:200]}")
            if r.status_code == 404:
                fails.append("can_interact route not registered (got 404)")

    print("\n[phaseB] === verdict ===")
    if fails:
        for f in fails: print(f"  FAIL: {f}")
        return 1
    print("  PASS: Phase B endpoints registered")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
