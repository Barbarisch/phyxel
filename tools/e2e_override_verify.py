"""End-to-end verification: per-character override -> runtime pose.

We verify the override actually changes the seated pose by reading the
Hips bone world position via telemetry. `ie_seek_animation` computes
worldPosition = seat + sittingIdleOffset - rotateByYaw(hipsRef), so a
+0.5m Y bump on the override must raise Hips Y by ~0.5m for the matching
characterId only.
"""
from __future__ import annotations

import sys
import time
from pathlib import Path

import httpx

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.interaction_pipeline.engine_lifecycle import EngineSession, Mode  # noqa: E402
from tools.interaction_pipeline import morphology_presets  # noqa: E402
from tools.interaction_pipeline.character_metrics import spawn_for_test  # noqa: E402


T = {"archetype": "humanoid_normal", "template_name": "chair_wood", "point_id": "seat_0"}
ASSET = ROOT / "resources" / "templates" / "chair_wood.voxel"
BASE_Y = 0.667
OVERRIDE_Y_DELTA = 0.5


def _hips_y(c: httpx.Client, base: str, clip: str = "sitting_idle", t: float = 0.5) -> float:
    r = c.post(f"{base}/api/interaction/ie/seek",
               json={"clip_name": clip, "normalized_time": t}).json()
    print(f"    seek: override_used={r.get('debug_override_used')} "
          f"sittingIdle={r.get('debug_sitting_idle_offset')} "
          f"world_pos.y={r['world_pos']['y']:.3f} "
          f"seat_anchor.y={r['seat_anchor']['y']:.3f}")
    return float(r["world_pos"]["y"])


def _post_profile(c: httpx.Client, base: str, body: dict) -> None:
    c.post(f"{base}/api/interaction/profile", json=body).raise_for_status()


def main() -> int:
    with EngineSession(Mode.INTERACTION_EDITOR, target=str(ASSET),
                       on_crash="abort", verbose=True) as session:
        base_url = session.base_url
        port = session.plan.port

        with httpx.Client(timeout=10.0) as c:
            # 1. Seed base profile AND explicitly write a baseline override for
            # every preset so Pass A starts from a known state regardless of
            # any per_character entries persisted on disk from previous runs.
            _post_profile(c, base_url, dict(
                T,
                sit_down_offset=[0.333, BASE_Y, 0.333],
                sitting_idle_offset=[0.333, BASE_Y, 0.333],
                sit_stand_up_offset=[0.333, BASE_Y, 0.333],
                sit_blend_duration=0.5, seat_height_offset=0.0))
            for pid in ("standard", "giant", "dwarf", "child"):
                _post_profile(c, base_url, dict(
                    T, character_id=pid,
                    sit_down_offset=[0.333, BASE_Y, 0.333],
                    sitting_idle_offset=[0.333, BASE_Y, 0.333],
                    sit_stand_up_offset=[0.333, BASE_Y, 0.333],
                    seat_height_offset=0.0))

            # 2. Pass A: NO overrides. Capture Hips Y for all presets.
            print("\n[e2e] --- Pass A: base profile only ---")
            pass_a: dict[str, float] = {}
            for pid in ("standard", "giant", "dwarf", "child"):
                spawn_for_test(morphology_presets.get(pid).to_appearance_json(), port=port)
                pass_a[pid] = _hips_y(c, base_url)
                print(f"  {pid:9}: Hips Y = {pass_a[pid]:.3f}")

            # 3. Pass B: write +0.5m Y override for 'giant' only.
            print(f"\n[e2e] --- Pass B: giant override Y +{OVERRIDE_Y_DELTA}m ---")
            ov_y = BASE_Y + OVERRIDE_Y_DELTA
            ov_resp = c.post(f"{base_url}/api/interaction/profile", json=dict(
                T, character_id="giant",
                sit_down_offset=[0.333, ov_y, 0.333],
                sitting_idle_offset=[0.333, ov_y, 0.333],
                sit_stand_up_offset=[0.333, ov_y, 0.333]))
            print(f"  POST status={ov_resp.status_code} body={ov_resp.json()}")
            # Confirm it landed.
            chk = c.get(f"{base_url}/api/interaction/profile",
                        params={"archetype": T["archetype"],
                                "template_name": T["template_name"],
                                "point_id": T["point_id"],
                                "character_id": "giant"}).json()
            print(f"  GET giant: sitting_idle_offset={chk.get('sitting_idle_offset')} "
                  f"resolved_from_override={chk.get('resolved_from_override')}")

            pass_b: dict[str, float] = {}
            for pid in ("standard", "giant", "dwarf", "child"):
                spawn_for_test(morphology_presets.get(pid).to_appearance_json(), port=port)
                pass_b[pid] = _hips_y(c, base_url)
                d = pass_b[pid] - pass_a[pid]
                print(f"  {pid:9}: Hips Y = {pass_b[pid]:.3f}  (delta vs Pass A: {d:+.3f})")

            # 4. Verdict.
            print("\n[e2e] === verdict ===")
            ok = True
            tol = 0.05

            gd = pass_b["giant"] - pass_a["giant"]
            if abs(gd - OVERRIDE_Y_DELTA) < tol:
                print(f"  PASS: giant Hips Y rose by {gd:+.3f} m (expected +{OVERRIDE_Y_DELTA})")
            else:
                print(f"  FAIL: giant Hips Y delta {gd:+.3f} != +{OVERRIDE_Y_DELTA} (tol {tol})")
                ok = False

            for pid in ("standard", "dwarf", "child"):
                d = pass_b[pid] - pass_a[pid]
                if abs(d) < tol:
                    print(f"  PASS: {pid} Hips Y unchanged ({d:+.3f} m)")
                else:
                    print(f"  FAIL: {pid} Hips Y drifted {d:+.3f} m -- override leaked")
                    ok = False

            return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
