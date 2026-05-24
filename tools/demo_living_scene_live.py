"""Live walkthrough of demo_living_scene.py — same scenes, but paced for
human viewing.

Usage:
    python tools/demo_living_scene_live.py

A Vulkan window opens (phyxel.exe). The script narrates each scene to
stdout while it runs. There are deliberate pauses so you can watch each
action complete. At the end of all scenes, the window stays open for
20 s before the engine is stopped — focus the window during the run.
"""
from __future__ import annotations

import sys
import time
from pathlib import Path

import httpx

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.interaction_pipeline.engine_lifecycle import EngineSession, Mode  # noqa: E402

# Reuse all the scene definitions from the original demo.
from tools.demo_living_scene import (  # noqa: E402
    SceneContext, SCENES,
)

PROJECT = r"C:\Users\jack\Documents\PhyxelProjects\CharacterTestbed"

# Extra seconds of "hold" inserted after each scene so you can see the
# settled state before the next scene clears the world.
HOLD_BETWEEN_SCENES_S = 2.5
HOLD_AT_END_S = 20.0


def banner(text: str) -> None:
    bar = "=" * (len(text) + 8)
    print(f"\n{bar}\n=== {text} ===\n{bar}")


def main() -> int:
    banner("Phyxel — Living Scene Demo (LIVE)")
    print("A Vulkan window will open. Switch focus to it now.")
    print("Pacing: ~2-3 s of hold time between scenes.\n")

    with EngineSession(Mode.PROJECT, target=PROJECT, on_crash="abort", verbose=True) as session:
        base = session.base_url
        with httpx.Client(timeout=30.0) as c:
            for _ in range(40):
                try:
                    if c.get(f"{base}/api/placed_objects", timeout=3.0).status_code == 200:
                        break
                except Exception:
                    pass
                time.sleep(0.5)

            ctx = SceneContext(c=c, base=base)

            c.post(f"{base}/api/character/spawn_for_test", json={"appearance": {}}).json()
            time.sleep(0.4)
            contact = c.get(f"{base}/api/character/contact").json()
            pos = contact.get("position") or [16.0, 17.0, 16.0]
            facing = contact.get("facing") or [0.0, 0.0, 1.0]
            ax, az = facing[0], facing[2]
            if abs(az) >= abs(ax):
                ctx.dx, ctx.dz = 0, (1 if az > 0 else -1)
            else:
                ctx.dx, ctx.dz = (1 if ax > 0 else -1), 0
            ctx.sx, ctx.sz = (1, 0) if ctx.dz != 0 else (0, 1)
            ctx.origin = (int(round(pos[0])), int(round(pos[1])), int(round(pos[2])))
            print(f"[live] origin={ctx.origin} forward=({ctx.dx},{ctx.dz}) side=({ctx.sx},{ctx.sz})")

            for i, scene in enumerate(SCENES, 1):
                ctx.current_scene = scene.name
                banner(f"Scene {i}/{len(SCENES)} — {scene.name}")
                c.post(f"{base}/api/character/spawn_for_test", json={"appearance": {}}).json()
                time.sleep(0.4)
                ctx.cleanup()
                try:
                    st = scene.setup(ctx)
                    # Small pause so you can see the staged scene before the
                    # character starts acting.
                    print(f"[live] {scene.name}: scene staged — watch...")
                    time.sleep(0.7)
                    scene.act(ctx, st)
                    print(f"[live] {scene.name}: action complete — hold {HOLD_BETWEEN_SCENES_S}s")
                    time.sleep(HOLD_BETWEEN_SCENES_S)
                    scene.teardown(ctx, st)
                except Exception as e:
                    print(f"[live] {scene.name} raised: {e}")
                finally:
                    ctx.cleanup()

            banner("All scenes complete")
            print(f"Window will stay open for {HOLD_AT_END_S:.0f}s, then engine will stop.")
            for remaining in range(int(HOLD_AT_END_S), 0, -5):
                print(f"  ...stopping in {remaining}s")
                time.sleep(5.0)

    print("[live] Demo complete.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
