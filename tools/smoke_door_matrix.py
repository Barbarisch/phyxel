"""Smoke matrix for door interactions across all door templates.

Uses the new pipeline's EngineSession to drive each door template through
register -> open -> close -> toggle -> lock -> open-while-locked -> unlock
-> open -> unregister. Verifies state at each step from /api/doors.

Outputs PASS/FAIL per template. No screenshots (covered by
visual_door_open_close.py).
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
TEMPLATES = ["door_wood", "door_wood_wide", "door_metal"]


def door_state(c: httpx.Client, base: str, poid: str) -> dict:
    r = c.get(f"{base}/api/doors", timeout=5.0).json()
    for d in r.get("doors", []):
        if d.get("placed_object_id") == poid:
            return d
    return {}


def wait_settled(c: httpx.Client, base: str, poid: str, timeout: float = 3.0) -> dict:
    t0 = time.time()
    while time.time() - t0 < timeout:
        st = door_state(c, base, poid)
        if st and st.get("settled"):
            return st
        time.sleep(0.05)
    return door_state(c, base, poid)


def test_template(c: httpx.Client, base: str, template: str, slot: int) -> list[tuple[str, bool, str]]:
    """Return list of (step, ok, detail)."""
    results: list[tuple[str, bool, str]] = []

    # Use a fixed slot far from where other tests work, spaced 4 cells apart.
    door_x = 30 + slot * 4
    door_y = 17
    door_z = 30

    # Clear footprint.
    c.post(f"{base}/api/world/clear", json={
        "x1": door_x - 2, "y1": door_y, "z1": door_z - 2,
        "x2": door_x + 4, "y2": door_y + 4, "z2": door_z + 4,
    })
    time.sleep(0.3)

    sp = c.post(f"{base}/api/world/template", json={
        "name": template,
        "position": {"x": door_x, "y": door_y, "z": door_z},
        "static": True,
        "rotation": 0,
    }).json()
    poid = sp.get("object_id")
    results.append(("spawn", bool(sp.get("success") and poid), f"object_id={poid}"))
    if not poid:
        return results

    reg = c.post(f"{base}/api/door/register", json={
        "placed_object_id": poid,
        "template_name":    template,
        "open_angle":       90.0,
        "swing_speed":      240.0,  # fast so settle is quick
    }, timeout=30.0).json()
    results.append(("register", bool(reg.get("success")), str(reg)))
    if not reg.get("success"):
        return results

    # open
    c.post(f"{base}/api/door/open", json={"placed_object_id": poid})
    st = wait_settled(c, base, poid)
    results.append(("open", bool(st.get("is_open")) and abs(st.get("current_angle", 0) - 90.0) < 1.0,
                    f"is_open={st.get('is_open')} angle={st.get('current_angle')}"))

    # close
    c.post(f"{base}/api/door/close", json={"placed_object_id": poid})
    st = wait_settled(c, base, poid)
    results.append(("close", not st.get("is_open") and abs(st.get("current_angle", 0)) < 1.0,
                    f"is_open={st.get('is_open')} angle={st.get('current_angle')}"))

    # toggle (closed -> open)
    c.post(f"{base}/api/door/toggle", json={"placed_object_id": poid})
    st = wait_settled(c, base, poid)
    results.append(("toggle_open", bool(st.get("is_open")),
                    f"is_open={st.get('is_open')} angle={st.get('current_angle')}"))

    # toggle (open -> closed)
    c.post(f"{base}/api/door/toggle", json={"placed_object_id": poid})
    st = wait_settled(c, base, poid)
    results.append(("toggle_close", not st.get("is_open"),
                    f"is_open={st.get('is_open')} angle={st.get('current_angle')}"))

    # lock
    c.post(f"{base}/api/door/lock", json={"placed_object_id": poid, "locked": True})
    st = door_state(c, base, poid)
    results.append(("lock", bool(st.get("locked")), f"locked={st.get('locked')}"))

    # try-open while locked: should remain closed
    c.post(f"{base}/api/door/open", json={"placed_object_id": poid})
    time.sleep(0.4)
    st = door_state(c, base, poid)
    results.append(("open_while_locked_rejected",
                    not st.get("is_open"),
                    f"is_open={st.get('is_open')} angle={st.get('current_angle')}"))

    # unlock + open
    c.post(f"{base}/api/door/lock", json={"placed_object_id": poid, "locked": False})
    c.post(f"{base}/api/door/open", json={"placed_object_id": poid})
    st = wait_settled(c, base, poid)
    results.append(("open_after_unlock", bool(st.get("is_open")),
                    f"is_open={st.get('is_open')} angle={st.get('current_angle')}"))

    # cleanup
    c.post(f"{base}/api/door/unregister", json={"placed_object_id": poid})
    try:
        c.request("DELETE", f"{base}/api/placed_objects/{poid}", timeout=5.0)
    except Exception:
        pass

    return results


def main() -> int:
    all_results: dict[str, list[tuple[str, bool, str]]] = {}
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

            for slot, template in enumerate(TEMPLATES):
                print(f"\n[doorMatrix] === {template} ===")
                all_results[template] = test_template(c, base, template, slot)
                for step, ok, detail in all_results[template]:
                    flag = "PASS" if ok else "FAIL"
                    print(f"  [{flag}] {step:30s} {detail}")

    # Summary
    total = 0
    failed = 0
    for tpl, rows in all_results.items():
        for step, ok, _ in rows:
            total += 1
            if not ok:
                failed += 1
    print(f"\n[doorMatrix] {total - failed}/{total} steps passed across {len(TEMPLATES)} templates")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
