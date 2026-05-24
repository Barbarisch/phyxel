"""Live demo of the 5 currently-tuned interaction profiles.

Cycles through every tuned (template, morphology) pair so you can visually
judge whether the persisted offsets actually look good in-game. Tuned set:

    chair_wood      seat_0      (sit + stand)        x {standard, giant, dwarf, child}
    test_chair      seat_0      (sit + stand)        x {standard, giant, dwarf, child}
    door_wood       handle_0    (open + close)       x {standard, giant, dwarf, child}
    door_wood_wide  handle_0    (open + close)       x {standard, giant, dwarf, child}
    door_metal      handle_0    (open + close)       x {standard, giant, dwarf, child}

Total: 20 scenes. The engine opens once and stays open for the whole run.
The window stays open at the end for 30s so you can free-camera around.

Usage:
    python tools/demo_tuned_interactions.py
    python tools/demo_tuned_interactions.py --templates chair_wood door_wood
    python tools/demo_tuned_interactions.py --morphs standard giant
    python tools/demo_tuned_interactions.py --hold 3.0    # extra seconds per pose
"""
from __future__ import annotations

import argparse
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import httpx

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.interaction_pipeline.engine_lifecycle import EngineSession, Mode  # noqa: E402
from tools.interaction_pipeline.morphology_presets import get as get_preset  # noqa: E402

PROJECT = r"C:\Users\jack\Documents\PhyxelProjects\CharacterTestbed"

ALL_TEMPLATES = {
    # template_name -> (kind, point_id)
    "chair_wood":     ("sit",  "seat_0"),
    "test_chair":     ("sit",  "seat_0"),
    "door_wood":      ("door", "handle_0"),
    "door_wood_wide": ("door", "handle_0"),
    "door_metal":     ("door", "handle_0"),
}
ALL_MORPHS = ("standard", "giant", "dwarf", "child")


@dataclass
class Demo:
    c: httpx.Client
    base: str
    entity_id: str = "player"
    origin: tuple[int, int, int] = (16, 17, 16)
    # Forward = +Z, side = +X (matches spawn_for_test default facing).
    dx: int = 0
    dz: int = 1
    sx: int = 1
    sz: int = 0
    hold: float = 1.5

    placed_ids: list[str] = None  # type: ignore[assignment]
    registered_door_ids: list[str] = None  # type: ignore[assignment]

    def __post_init__(self) -> None:
        self.placed_ids = []
        self.registered_door_ids = []

    # ---- helpers ----
    def capture(self, label: str) -> None:
        try:
            r = self.c.get(f"{self.base}/api/screenshot", timeout=10.0).json()
            print(f"    capture {label:14s} -> {r.get('path', '?')}")
        except Exception as e:
            print(f"    capture {label}: ERROR {e!r}")

    def spawn_template(self, name: str, dx_off: int, dz_off: int,
                       rotation: int = 0) -> str:
        ox, oy, oz = self.origin
        r = self.c.post(f"{self.base}/api/world/template", json={
            "name":     name,
            "position": {"x": ox + dx_off, "y": oy, "z": oz + dz_off},
            "static":   True,
            "rotation": rotation,
        }).json()
        oid = r.get("object_id", "")
        if oid:
            self.placed_ids.append(oid)
        return oid

    def delete_placed(self, oid: str) -> None:
        try:
            self.c.request("DELETE", f"{self.base}/api/placed_objects/{oid}", timeout=5.0)
        except Exception:
            pass

    def camera(self, dx_off: float, dy_off: float, dz_off: float,
              yaw: float, pitch: float = -8.0) -> None:
        ox, oy, oz = self.origin
        self.c.post(f"{self.base}/api/camera", json={
            "position": {"x": ox + dx_off, "y": oy + dy_off, "z": oz + dz_off},
            "yaw":      yaw,
            "pitch":    pitch,
        })

    def cleanup_world(self) -> None:
        for did in list(self.registered_door_ids):
            self.c.post(f"{self.base}/api/door/unregister",
                        json={"placed_object_id": did})
        self.registered_door_ids.clear()
        for oid in list(self.placed_ids):
            self.delete_placed(oid)
        self.placed_ids.clear()
        ox, oy, oz = self.origin
        self.c.post(f"{self.base}/api/world/clear", json={
            "x1": ox - 8, "y1": oy,     "z1": oz - 8,
            "x2": ox + 8, "y2": oy + 8, "z2": oz + 12,
        })
        time.sleep(0.3)

    def spawn_morphology(self, morph: str) -> None:
        preset = get_preset(morph)
        self.c.post(f"{self.base}/api/character/spawn_for_test",
                    json={"appearance": preset.to_appearance_json()})
        time.sleep(0.5)
        # Refresh origin from new contact (spawn may move character).
        try:
            con = self.c.get(f"{self.base}/api/character/contact", timeout=5.0).json()
            pos = con.get("position") or list(self.origin)
            self.origin = (int(round(pos[0])), int(round(pos[1])), int(round(pos[2])))
        except Exception:
            pass

    # ---- scene runners ----
    def run_sit_scene(self, template: str, morph: str) -> None:
        print(f"\n  [sit] {template} x {morph}")
        self.cleanup_world()
        self.spawn_morphology(morph)
        ox, oy, oz = self.origin

        # Place chair 2 cells in front, facing the character.
        chair = self.spawn_template(template, self.dx * 2, self.dz * 2, rotation=2)
        if not chair:
            print(f"    SKIP: chair {template} failed to spawn")
            return
        # Camera: side view at character chest height.
        self.camera(self.sx * 4.5 + self.dx * 1.0, 1.7,
                    self.sz * 4.5 + self.dz * 1.0,
                    yaw=(180.0 if self.sx > 0 else 0.0))
        time.sleep(0.5)
        self.capture("00_pre_sit")
        time.sleep(self.hold)

        r = self.c.post(f"{self.base}/api/interaction/sit", json={
            "entity_id": self.entity_id, "object_id": chair,
            "point_id": "seat_0", "force": True,
        }).json()
        ok = r.get("success")
        print(f"    sit -> success={ok} err={r.get('error')}")
        time.sleep(1.2)
        self.capture("01_sat")
        time.sleep(self.hold + 0.5)

        self.c.post(f"{self.base}/api/interaction/stand_up",
                    json={"entity_id": self.entity_id})
        time.sleep(1.2)
        self.capture("02_stood_up")
        time.sleep(self.hold)

    def run_door_scene(self, template: str, morph: str) -> None:
        print(f"\n  [door] {template} x {morph}")
        self.cleanup_world()
        self.spawn_morphology(morph)
        ox, oy, oz = self.origin

        door = self.spawn_template(template, self.dx * 3, self.dz * 3, rotation=0)
        if not door:
            print(f"    SKIP: door {template} failed to spawn")
            return
        try:
            reg = self.c.post(f"{self.base}/api/door/register", json={
                "placed_object_id": door,
                "template_name":    template,
                "open_angle":       90.0,
                "swing_speed":      180.0,
            }, timeout=20.0).json()
            if reg.get("success"):
                self.registered_door_ids.append(door)
            else:
                print(f"    SKIP: door register failed: {reg.get('error')}")
                return
        except Exception as e:
            print(f"    SKIP: door register exception: {e!r}")
            return

        # Camera: angled front, slightly elevated.
        self.camera(self.sx * 5.0 + self.dx * 0.5, 2.8,
                    self.sz * 5.0 + self.dz * 0.5,
                    yaw=(180.0 - 25.0 if self.sx > 0 else 25.0),
                    pitch=-10.0)
        time.sleep(0.5)
        self.capture("00_closed")
        time.sleep(self.hold)

        self.c.post(f"{self.base}/api/door/open", json={"placed_object_id": door})
        time.sleep(1.2)
        self.capture("01_opened")
        time.sleep(self.hold)

        self.c.post(f"{self.base}/api/door/close", json={"placed_object_id": door})
        time.sleep(1.2)
        self.capture("02_closed_again")
        time.sleep(self.hold)


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--templates", nargs="+", default=None,
                   help="Restrict to these template names")
    p.add_argument("--morphs", nargs="+", default=None,
                   help="Restrict to these morphologies")
    p.add_argument("--hold", type=float, default=1.5,
                   help="Extra hold seconds per captured pose (default 1.5)")
    p.add_argument("--end-hold", type=float, default=30.0,
                   help="Seconds to keep engine open after the demo (default 30)")
    args = p.parse_args()

    templates = [t for t in ALL_TEMPLATES if (args.templates is None or t in args.templates)]
    morphs = [m for m in ALL_MORPHS if (args.morphs is None or m in args.morphs)]

    print("=" * 70)
    print("Phyxel — Tuned Interaction Demo")
    print("=" * 70)
    print(f"Templates ({len(templates)}): {', '.join(templates)}")
    print(f"Morphologies ({len(morphs)}): {', '.join(morphs)}")
    total = len(templates) * len(morphs)
    print(f"Scenes: {total}  •  Hold per pose: {args.hold}s  •  End hold: {args.end_hold}s")
    print("Switch focus to the engine window when it opens.")

    with EngineSession(Mode.PROJECT, target=PROJECT,
                       on_crash="abort", verbose=True) as session:
        base = session.base_url
        with httpx.Client(timeout=30.0) as c:
            for _ in range(40):
                try:
                    if c.get(f"{base}/api/placed_objects", timeout=3.0).status_code == 200:
                        break
                except Exception:
                    pass
                time.sleep(0.5)

            demo = Demo(c=c, base=base, hold=args.hold)
            # Initial spawn to anchor origin/facing.
            demo.spawn_morphology("standard")

            i = 0
            for template in templates:
                kind, _pid = ALL_TEMPLATES[template]
                for morph in morphs:
                    i += 1
                    print(f"\n--- Scene {i}/{total}: {template} ({kind}) x {morph} ---")
                    try:
                        if kind == "sit":
                            demo.run_sit_scene(template, morph)
                        elif kind == "door":
                            demo.run_door_scene(template, morph)
                    except Exception as e:
                        print(f"    SCENE ERROR: {e!r}")

            print("\n" + "=" * 70)
            print("All scenes complete.")
            print(f"Engine stays open for {args.end_hold:.0f}s — fly around to inspect.")
            print("=" * 70)
            for remaining in range(int(args.end_hold), 0, -5):
                print(f"  ...closing in {remaining}s")
                time.sleep(5.0)
            demo.cleanup_world()

    print("\nDone.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
