"""Living-scene demo: character does several actions on several assets.

Goal: a single script that drives one character through a sequence of
interactions with different asset types, capturing screenshots at each
beat. This is the stepping-stone template for the future LLM-influenced
character AI scripting interface.

Design notes (read this if you're extending the script):

  * Each interaction is a `Scene` — a small dict-shaped record with
    `name`, `setup(ctx)`, `act(ctx)` and `teardown(ctx)` callables.
    A "character action" is one Scene.
  * `ctx` is a SceneContext exposing the engine HTTP client, base URL,
    paths to capture frames, character contact, and helpers like
    `capture(label)`, `place_voxel(...)`, `spawn_template(...)`.
  * Scenes are executed in order; failures in one don't abort the rest.
  * This script intentionally has zero engine-side smarts — every
    decision is in Python here. The character's facing/position is
    queried from `/api/character/contact` once at the start and reused
    as the local origin for asset placement.

When we add an LLM-driven scripting interface, each LLM-generated
"intent" can compile down to a Scene record (or a sequence of them).
The same APIs and the same scene runner can drive both this demo and
the eventual agent.
"""
from __future__ import annotations

import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

import httpx

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.interaction_pipeline.engine_lifecycle import EngineSession, Mode  # noqa: E402

PROJECT = r"C:\Users\jack\Documents\PhyxelProjects\CharacterTestbed"


# ---------------------------------------------------------------------------
# Scene context + helpers
# ---------------------------------------------------------------------------

@dataclass
class SceneContext:
    c: httpx.Client
    base: str
    entity_id: str = "player"
    # Character origin (set during init). All scenes place assets relative
    # to this so the same camera framing works for every scene.
    origin: tuple[int, int, int] = (16, 17, 16)
    # Forward / side axes (cardinal, derived from facing).
    dx: int = 0
    dz: int = 1
    sx: int = 1
    sz: int = 0
    # Per-scene scratch for cleanup tracking.
    placed_object_ids: list[str] = field(default_factory=list)
    registered_door_ids: list[str] = field(default_factory=list)
    frames: list[tuple[str, str, str]] = field(default_factory=list)  # (scene, label, path)
    current_scene: str = ""

    # ----- generic helpers -----
    def capture(self, label: str) -> None:
        r = self.c.get(f"{self.base}/api/screenshot", timeout=10.0).json()
        path = r.get("path", "")
        print(f"[{self.current_scene}] {label:18s} -> {path}")
        self.frames.append((self.current_scene, label, path))

    def clear_region(self, dx_min: int, dx_max: int, dy_max: int, dz_min: int, dz_max: int) -> None:
        ox, oy, oz = self.origin
        self.c.post(f"{self.base}/api/world/clear", json={
            "x1": ox + dx_min, "y1": oy,            "z1": oz + dz_min,
            "x2": ox + dx_max, "y2": oy + dy_max,   "z2": oz + dz_max,
        })

    def place_voxel(self, dx_off: int, dy_off: int, dz_off: int, material: str = "Stone") -> None:
        ox, oy, oz = self.origin
        self.c.post(f"{self.base}/api/world/voxel", json={
            "x": ox + dx_off, "y": oy + dy_off, "z": oz + dz_off, "material": material,
        })

    def spawn_template(self, name: str, dx_off: int, dy_off: int, dz_off: int,
                       rotation: int = 0) -> str:
        ox, oy, oz = self.origin
        r = self.c.post(f"{self.base}/api/world/template", json={
            "name": name,
            "position": {"x": ox + dx_off, "y": oy + dy_off, "z": oz + dz_off},
            "static":   True,
            "rotation": rotation,
        }).json()
        oid = r.get("object_id", "")
        if oid:
            self.placed_object_ids.append(oid)
        return oid

    def delete_placed(self, oid: str) -> None:
        try:
            self.c.request("DELETE", f"{self.base}/api/placed_objects/{oid}", timeout=5.0)
        except Exception:
            pass

    def camera_side(self, dx_off: float, dy_off: float, dz_off: float,
                    yaw: float, pitch: float = -8.0) -> None:
        ox, oy, oz = self.origin
        self.c.post(f"{self.base}/api/camera", json={
            "position": {"x": ox + dx_off, "y": oy + dy_off, "z": oz + dz_off},
            "yaw":      yaw,
            "pitch":    pitch,
        }).json()

    def cleanup(self) -> None:
        for did in list(self.registered_door_ids):
            self.c.post(f"{self.base}/api/door/unregister", json={"placed_object_id": did})
        self.registered_door_ids.clear()
        for oid in list(self.placed_object_ids):
            self.delete_placed(oid)
        self.placed_object_ids.clear()
        # Clear a generous bounding box around origin.
        self.clear_region(-8, 8, 8, -8, 12)
        time.sleep(0.3)


# ---------------------------------------------------------------------------
# Scenes
# ---------------------------------------------------------------------------

@dataclass
class Scene:
    name: str
    setup: Callable[[SceneContext], dict[str, Any]]
    act:   Callable[[SceneContext, dict[str, Any]], None]
    teardown: Callable[[SceneContext, dict[str, Any]], None] = lambda ctx, st: None


# --- Scene A: sit on a chair, then stand up ---

def setup_sit(ctx: SceneContext) -> dict:
    # Place chair 1 cell forward + 1 cell to the side so the camera sees it.
    chair = ctx.spawn_template("chair_wood", ctx.dx * 1, 0, ctx.dz * 1, rotation=0)
    ctx.camera_side(ctx.sx * 5.0 + ctx.dx * 0.5, 1.5, ctx.sz * 5.0 + ctx.dz * 0.5,
                    yaw=(180.0 if ctx.sx > 0 else (0.0 if ctx.sx < 0 else (-90.0 if ctx.sz > 0 else 90.0))))
    time.sleep(0.3)
    return {"chair": chair}


def act_sit(ctx: SceneContext, st: dict) -> None:
    chair = st["chair"]
    if not chair:
        print(f"[{ctx.current_scene}] FAIL: chair did not spawn")
        return
    ctx.capture("00_pre_sit")
    r = ctx.c.post(f"{ctx.base}/api/interaction/sit", json={
        "entity_id": ctx.entity_id, "object_id": chair, "point_id": "seat_0", "force": True,
    }).json()
    print(f"[{ctx.current_scene}] sit -> success={r.get('success')} err={r.get('error')}")
    time.sleep(0.6)
    ctx.capture("01_sat")
    ctx.c.post(f"{ctx.base}/api/interaction/stand_up", json={"entity_id": ctx.entity_id})
    time.sleep(0.7)
    ctx.capture("02_stood_up")


# --- Scene B: open a door, then close it ---

def setup_door(ctx: SceneContext) -> dict:
    door = ctx.spawn_template("door_wood", ctx.dx * 2, 0, ctx.dz * 2, rotation=0)
    if door:
        reg = ctx.c.post(f"{ctx.base}/api/door/register", json={
            "placed_object_id": door,
            "template_name":    "door_wood",
            "open_angle":       90.0,
            "swing_speed":      180.0,
        }, timeout=20.0).json()
        if reg.get("success"):
            ctx.registered_door_ids.append(door)
    ctx.camera_side(ctx.sx * 5.0 + ctx.dx * (-1.0), 2.5, ctx.sz * 5.0 + ctx.dz * (-1.0),
                    yaw=(180.0 - 20.0 if ctx.sx > 0 else 20.0) if ctx.sx != 0
                        else (-90.0 - 20.0 if ctx.sz > 0 else 90.0 + 20.0),
                    pitch=-10.0)
    time.sleep(0.3)
    return {"door": door}


def act_door(ctx: SceneContext, st: dict) -> None:
    door = st["door"]
    if not door:
        print(f"[{ctx.current_scene}] FAIL: door did not spawn/register")
        return
    ctx.capture("00_closed")
    ctx.c.post(f"{ctx.base}/api/door/open", json={"placed_object_id": door})
    time.sleep(0.55)
    ctx.capture("01_opened")
    ctx.c.post(f"{ctx.base}/api/door/close", json={"placed_object_id": door})
    time.sleep(0.55)
    ctx.capture("02_closed_again")


# --- Scene C: climb a ledge (mantle) ---

def setup_climb(ctx: SceneContext) -> dict:
    # Single-block wall in front of character + landing row.
    for off in (-1, 0, 1):
        ctx.place_voxel(ctx.dx + ctx.sx * off, 0, ctx.dz + ctx.sz * off, "Stone")
    for off in (-1, 0, 1):
        ctx.place_voxel(ctx.dx * 2 + ctx.sx * off, 0, ctx.dz * 2 + ctx.sz * off, "Stone")
    ctx.camera_side(ctx.sx * 5.0 + ctx.dx * 0.5, 2.0, ctx.sz * 5.0 + ctx.dz * 0.5,
                    yaw=(180.0 if ctx.sx > 0 else (0.0 if ctx.sx < 0 else (-90.0 if ctx.sz > 0 else 90.0))))
    time.sleep(0.3)
    return {}


def act_climb(ctx: SceneContext, st: dict) -> None:
    ox, oy, oz = ctx.origin
    start = [float(ox), float(oy), float(oz)]
    end   = [float(ox) + ctx.dx * 0.9, float(oy) + 1.0, float(oz) + ctx.dz * 0.9]
    ctx.capture("00_pre_climb")
    r = ctx.c.post(f"{ctx.base}/api/interaction/try_climb_up", json={
        "start": start, "end": end, "duration": 0.7, "clip": "climb_ladder_start",
    }).json()
    print(f"[{ctx.current_scene}] try_climb_up -> started={r.get('started')}")
    for t_target, label in [(0.30, "01_t030"), (0.65, "02_t065"), (1.10, "03_landed")]:
        time.sleep(max(0.0, t_target - 0.0)) if not ctx.frames else None
        time.sleep(0.0)  # interval-based capture below
    # Simpler: just sleep+capture in real time
    t0 = time.time()
    for t_target, label in [(0.30, "01_t030"), (0.65, "02_t065"), (1.10, "03_landed")]:
        while time.time() - t0 < t_target:
            time.sleep(0.02)
        ctx.capture(label)


# --- Scene D: climb a ladder ---

def setup_ladder(ctx: SceneContext) -> dict:
    # 5-block pillar one cell forward + 3x3 platform on top.
    for h in range(5):
        ctx.place_voxel(ctx.dx * 2, h, ctx.dz * 2, "Stone")
    for off_a in range(-1, 2):
        for off_b in range(-1, 2):
            ctx.place_voxel(ctx.dx * 2 + ctx.sx * off_a + ctx.dx * off_b,
                            5,
                            ctx.dz * 2 + ctx.sz * off_a + ctx.dz * off_b, "Stone")
    ctx.camera_side(ctx.sx * 6.0, 3.5, ctx.sz * 6.0,
                    yaw=(180.0 if ctx.sx > 0 else (0.0 if ctx.sx < 0 else (-90.0 if ctx.sz > 0 else 90.0))),
                    pitch=-12.0)
    time.sleep(0.3)
    return {}


def act_ladder(ctx: SceneContext, st: dict) -> None:
    ox, oy, oz = ctx.origin
    rail_x = float(ox + ctx.dx * 2) - ctx.dx * 0.5
    rail_z = float(oz + ctx.dz * 2) - ctx.dz * 0.5
    ctx.capture("00_start")
    r = ctx.c.post(f"{ctx.base}/api/interaction/ladder/start", json={
        "rail_x": rail_x, "rail_z": rail_z,
        "top_y": float(oy + 5), "bottom_y": float(oy),
        "climb_speed": 1.8,
    }).json()
    print(f"[{ctx.current_scene}] ladder/start -> on={r.get('on_ladder')}")
    ctx.c.post(f"{ctx.base}/api/interaction/ladder/input", json={"vertical": 1.0})
    t0 = time.time()
    for t_target, label in [(1.0, "01_climb_t1"), (2.0, "02_climb_t2"), (3.2, "03_top")]:
        while time.time() - t0 < t_target:
            time.sleep(0.02)
        ctx.capture(label)
    ctx.c.post(f"{ctx.base}/api/interaction/ladder/input", json={"vertical": 0.0})


# --- Scene E: push a dynamic cube ---

def setup_push(ctx: SceneContext) -> dict:
    ox, oy, oz = ctx.origin
    r = ctx.c.post(f"{ctx.base}/api/debug/spawn_bullet_cube", json={
        "x": float(ox) + ctx.dx * 1.1,
        "y": float(oy),
        "z": float(oz) + ctx.dz * 1.1,
        "material": "Stone",
    }).json()
    print(f"[{ctx.current_scene}] cube spawn -> {r.get('success')}")
    ctx.camera_side(ctx.sx * 5.5 + ctx.dx * 0.5, 1.8, ctx.sz * 5.5 + ctx.dz * 0.5,
                    yaw=(180.0 if ctx.sx > 0 else (0.0 if ctx.sx < 0 else (-90.0 if ctx.sz > 0 else 90.0))))
    time.sleep(0.3)
    return {}


def act_push(ctx: SceneContext, st: dict) -> None:
    ctx.capture("00_pre_push")
    r = ctx.c.post(f"{ctx.base}/api/interaction/try_push", json={
        "force": 6.0, "reach": 1.5, "clip": "push",
    }).json()
    print(f"[{ctx.current_scene}] try_push -> obj={r.get('object_id')} impulse={r.get('impulse')}")
    t0 = time.time()
    for t_target, label in [(0.30, "01_t030"), (0.80, "02_t080"), (1.50, "03_t150")]:
        while time.time() - t0 < t_target:
            time.sleep(0.02)
        ctx.capture(label)


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------

SCENES: list[Scene] = [
    Scene("A_sit",    setup_sit,    act_sit),
    Scene("B_door",   setup_door,   act_door),
    Scene("C_climb",  setup_climb,  act_climb),
    Scene("D_ladder", setup_ladder, act_ladder),
    Scene("E_push",   setup_push,   act_push),
]


def main() -> int:
    with EngineSession(Mode.PROJECT, target=PROJECT, on_crash="abort", verbose=True) as session:
        base = session.base_url
        with httpx.Client(timeout=30.0) as c:
            # Wait until API is fully responsive.
            for _ in range(30):
                try:
                    if c.get(f"{base}/api/placed_objects", timeout=3.0).status_code == 200:
                        break
                except Exception:
                    pass
                time.sleep(0.5)

            ctx = SceneContext(c=c, base=base)

            # Single-shot character spawn — we'll respawn between scenes so
            # interaction state (sat/climbing/...) doesn't leak across.
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
            print(f"[demo] origin={ctx.origin} forward=({ctx.dx},{ctx.dz}) side=({ctx.sx},{ctx.sz})")

            for scene in SCENES:
                ctx.current_scene = scene.name
                print(f"\n[demo] === {scene.name} ===")
                # Reset character to a clean Idle state between scenes.
                c.post(f"{base}/api/character/spawn_for_test", json={"appearance": {}}).json()
                time.sleep(0.3)
                ctx.cleanup()
                try:
                    st = scene.setup(ctx)
                    scene.act(ctx, st)
                    scene.teardown(ctx, st)
                except Exception as e:
                    print(f"[demo] {scene.name} raised: {e}")
                finally:
                    ctx.cleanup()

    print("\n[demo] === captured frames ===")
    by_scene: dict[str, list[tuple[str, str]]] = {}
    for s, l, p in ctx.frames:
        by_scene.setdefault(s, []).append((l, p))
    for s, rows in by_scene.items():
        print(f"  {s}:")
        for l, p in rows:
            print(f"    {l}: {p}")
    print(f"\n[demo] total frames captured: {len(ctx.frames)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
