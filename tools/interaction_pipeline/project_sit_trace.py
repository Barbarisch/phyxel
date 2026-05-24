"""Drive sit + stand-up against a running engine in project mode, capture the
per-frame motion trace, and report any worldPosition slides.

Usage:
    python -m tools.interaction_pipeline.project_sit_trace \
        --object chair_wood_8 \
        --entity player \
        --sit-hold 3.0 \
        --stand-hold 2.0
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path
from typing import Any

import httpx

API = "http://localhost:8090"
TIMEOUT = 10.0


def post(endpoint: str, body: dict) -> dict:
    r = httpx.post(f"{API}{endpoint}", json=body, timeout=TIMEOUT)
    r.raise_for_status()
    return r.json()


def get(endpoint: str, params: dict | None = None) -> dict:
    r = httpx.get(f"{API}{endpoint}", params=params or {}, timeout=TIMEOUT)
    r.raise_for_status()
    return r.json()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--object", default="chair_wood_8")
    ap.add_argument("--entity", default="player")
    ap.add_argument("--point", default="seat_0")
    ap.add_argument("--sit-hold", type=float, default=3.0,
                    help="seconds to hold seated before standing up")
    ap.add_argument("--stand-hold", type=float, default=2.0,
                    help="seconds to wait after standUp before dumping trace")
    ap.add_argument("--capacity", type=int, default=4096)
    ap.add_argument("--slide-threshold", type=float, default=0.02,
                    help="per-frame horizontal worldPosition delta that counts as slide (m)")
    ap.add_argument("--out", type=Path,
                    default=Path("tools/interaction_pipeline/reports/project_sit_trace.json"))
    args = ap.parse_args()

    print("[trace] engine status:", get("/api/engine/status"))

    print(f"[trace] clearing motion trace (capacity={args.capacity}) on entity '{args.entity}'")
    post("/api/character/motion_trace/clear",
         {"entity_id": args.entity, "capacity": args.capacity})

    print(f"[trace] sit_character object='{args.object}' point='{args.point}'")
    sit_resp = post("/api/interaction/sit",
                    {"entity_id": args.entity,
                     "object_id": args.object,
                     "point_id": args.point})
    print(f"        -> {sit_resp}")

    print(f"[trace] holding seated for {args.sit_hold}s ...")
    time.sleep(args.sit_hold)

    print(f"[trace] stand_up_character")
    stand_resp = post("/api/interaction/stand_up", {"entity_id": args.entity})
    print(f"        -> {stand_resp}")

    print(f"[trace] waiting {args.stand_hold}s for stand-up animation to finish ...")
    time.sleep(args.stand_hold)

    print(f"[trace] dumping motion trace")
    trace = get("/api/character/motion_trace", {"entity_id": args.entity})
    entries = trace.get("entries", [])
    print(f"[trace] {len(entries)} entries captured")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(trace, indent=2), encoding="utf-8")
    print(f"[trace] saved -> {args.out}")

    # ---- Inline slide analysis ----
    # Detect per-frame horizontal worldPosition deltas above threshold, and
    # attribute them to (is_sitting, is_blending, state, clip_index).
    if not entries:
        print("[trace] no entries, nothing to analyze")
        return 1

    slides: list[dict[str, Any]] = []
    prev = entries[0]
    for cur in entries[1:]:
        dx = cur["world_pos"]["x"] - prev["world_pos"]["x"]
        dz = cur["world_pos"]["z"] - prev["world_pos"]["z"]
        dxz = (dx * dx + dz * dz) ** 0.5
        if dxz > args.slide_threshold:
            slides.append({
                "dxz": dxz,
                "dx": dx, "dz": dz,
                "from_t": prev["total_time"],
                "to_t": cur["total_time"],
                "dt": cur["total_time"] - prev["total_time"],
                "from_pos": prev["world_pos"],
                "to_pos":   cur["world_pos"],
                "is_sitting":  cur["is_sitting"],
                "is_blending": cur["is_blending"],
                "state":       cur["state"],
                "clip_index":  cur["clip_index"],
            })
        prev = cur

    print()
    print(f"=== Slide events (>{args.slide_threshold} m / frame): {len(slides)} ===")
    for s in slides[:40]:
        print(f"  t={s['from_t']:7.3f}->{s['to_t']:7.3f} (dt={s['dt']:.4f}) "
              f"dxz={s['dxz']:.4f}m  state={s['state']} sit={int(s['is_sitting'])} "
              f"blend={int(s['is_blending'])} clip={s['clip_index']} "
              f"from=({s['from_pos']['x']:.3f},{s['from_pos']['z']:.3f}) "
              f"to=({s['to_pos']['x']:.3f},{s['to_pos']['z']:.3f})")
    if len(slides) > 40:
        print(f"  ... and {len(slides) - 40} more")

    # ---- Visible Hips-world-position slide ----
    # This is the user-facing metric: where in world space does the Hips bone
    # appear? Computed as world_pos + rotateByYaw(hips_local, XZ). A per-frame
    # delta above threshold here means the character is visibly jumping/sliding.
    # The yaw used is m_seatFacingYaw during sit, character yaw otherwise. We
    # don't have yaw in the trace yet, so assume facing_yaw=0 (the test scene).
    visible_slides: list[dict[str, Any]] = []
    visible_threshold = 0.05
    prev = entries[0]
    for cur in entries[1:]:
        if "hips_local" not in cur or "hips_local" not in prev:
            prev = cur
            continue
        # yaw=0: rotation is identity
        prev_vx = prev["world_pos"]["x"] + prev["hips_local"]["x"]
        prev_vz = prev["world_pos"]["z"] + prev["hips_local"]["z"]
        cur_vx  = cur["world_pos"]["x"]  + cur["hips_local"]["x"]
        cur_vz  = cur["world_pos"]["z"]  + cur["hips_local"]["z"]
        dx = cur_vx - prev_vx
        dz = cur_vz - prev_vz
        dxz = (dx * dx + dz * dz) ** 0.5
        if dxz > visible_threshold:
            visible_slides.append({
                "dxz": dxz,
                "from_t": prev["total_time"], "to_t": cur["total_time"],
                "dt": cur["total_time"] - prev["total_time"],
                "from_vp": (prev_vx, prev_vz), "to_vp": (cur_vx, cur_vz),
                "is_sitting":  cur["is_sitting"],
                "state":       cur["state"],
                "clip_change": cur["clip_index"] != prev["clip_index"],
                "from_clip":   prev["clip_index"], "clip": cur["clip_index"],
            })
        prev = cur

    print()
    print(f"=== Visible Hips world-position slides (>{visible_threshold} m / frame): {len(visible_slides)} ===")
    print("    (this is the canonical 'is the character jumping?' metric)")
    for s in visible_slides[:40]:
        kind = "CLIP_CHANGE" if s["clip_change"] else "WITHIN_CLIP"
        print(f"  t={s['from_t']:7.3f}->{s['to_t']:7.3f} (dt={s['dt']:.4f}) "
              f"dxz={s['dxz']:.4f}m  state={s['state']} sit={int(s['is_sitting'])} "
              f"clip={s['from_clip']}->{s['clip']} {kind} "
              f"from=({s['from_vp'][0]:.3f},{s['from_vp'][1]:.3f}) "
              f"to=({s['to_vp'][0]:.3f},{s['to_vp'][1]:.3f})")
    if len(visible_slides) > 40:
        print(f"  ... and {len(visible_slides) - 40} more")

    # ---- Hips-pose slide analysis ----
    # worldPosition can be flat while the rendered character still slides if the
    # animation clip translates the Hips bone in model space. Crossfade between
    # two clips whose Hips translations differ produces a visible slide even
    # though worldPosition never moves. Detect those by looking at the bone's
    # local position trace.
    pose_slides: list[dict[str, Any]] = []
    # Use a tighter threshold than the worldPos detector — bone-space slides are
    # smooth lerps spread over many frames (~0.03 m/frame for a 0.5 m / 0.3 s
    # crossfade) so a 0.05 m/frame threshold would miss them entirely.
    pose_threshold = 0.015
    prev = entries[0]
    for cur in entries[1:]:
        # Skip the very first frame after a clip change — animTime resets and the
        # snap is structural, not a slide. We DO care about within-blend motion.
        clip_changed = cur["clip_index"] != prev["clip_index"]
        if "hips_local" not in cur or "hips_local" not in prev:
            prev = cur
            continue
        dx = cur["hips_local"]["x"] - prev["hips_local"]["x"]
        dz = cur["hips_local"]["z"] - prev["hips_local"]["z"]
        dxz = (dx * dx + dz * dz) ** 0.5
        if dxz > pose_threshold and not clip_changed:
            pose_slides.append({
                "dxz": dxz,
                "dx": dx, "dz": dz,
                "from_t": prev["total_time"],
                "to_t": cur["total_time"],
                "dt": cur["total_time"] - prev["total_time"],
                "from_hips": prev["hips_local"],
                "to_hips":   cur["hips_local"],
                "is_sitting":  cur["is_sitting"],
                "is_blending": cur["is_blending"],
                "state":       cur["state"],
                "clip_index":  cur["clip_index"],
                "from_clip":   prev["clip_index"],
            })
        prev = cur

    print()
    print(f"=== Hips-pose slide events (>{pose_threshold} m / frame): {len(pose_slides)} ===")
    for s in pose_slides[:40]:
        print(f"  t={s['from_t']:7.3f}->{s['to_t']:7.3f} (dt={s['dt']:.4f}) "
              f"dxz={s['dxz']:.4f}m  state={s['state']} sit={int(s['is_sitting'])} "
              f"blend={int(s['is_blending'])} clip={s['from_clip']}->{s['clip_index']} "
              f"from=({s['from_hips']['x']:.3f},{s['from_hips']['z']:.3f}) "
              f"to=({s['to_hips']['x']:.3f},{s['to_hips']['z']:.3f})")
    if len(pose_slides) > 40:
        print(f"  ... and {len(pose_slides) - 40} more")

    # Cumulative hips drift summary per clip
    by_clip: dict[int, dict[str, float]] = {}
    prev = entries[0]
    for cur in entries[1:]:
        if "hips_local" not in cur or "hips_local" not in prev or cur["clip_index"] != prev["clip_index"]:
            prev = cur
            continue
        c = cur["clip_index"]
        dx = cur["hips_local"]["x"] - prev["hips_local"]["x"]
        dz = cur["hips_local"]["z"] - prev["hips_local"]["z"]
        rec = by_clip.setdefault(c, {"abs_dx": 0.0, "abs_dz": 0.0, "frames": 0})
        rec["abs_dx"] += abs(dx)
        rec["abs_dz"] += abs(dz)
        rec["frames"] += 1
        prev = cur
    print()
    print("=== Per-clip cumulative hips drift ===")
    for c, rec in sorted(by_clip.items()):
        print(f"  clip {c:3d}: |dx|={rec['abs_dx']:.3f}m |dz|={rec['abs_dz']:.3f}m over {rec['frames']} frames")

    # ---- Per-blend-window net hips drift ----
    # Aggregate consecutive frames where is_blending=True. For each contiguous
    # blend window report the net (signed) drift from start to end. This is
    # the most direct "visible slide" metric for the rendered character.
    blend_windows: list[dict[str, Any]] = []
    in_blend = False
    win_start = None
    for cur in entries:
        if cur.get("is_blending") and "hips_local" in cur:
            if not in_blend:
                in_blend = True
                win_start = cur
        else:
            if in_blend and win_start is not None:
                # window ended on the previous frame; use prev as the end of the window
                blend_windows.append({"start": win_start, "end": prev_blend_entry})
                in_blend = False
                win_start = None
        prev_blend_entry = cur
    if in_blend and win_start is not None:
        blend_windows.append({"start": win_start, "end": prev_blend_entry})

    print()
    print(f"=== Blend-window net hips drift ({len(blend_windows)} windows) ===")
    for w in blend_windows:
        s, e = w["start"], w["end"]
        dx = e["hips_local"]["x"] - s["hips_local"]["x"]
        dy = e["hips_local"]["y"] - s["hips_local"]["y"]
        dz = e["hips_local"]["z"] - s["hips_local"]["z"]
        dxz = (dx * dx + dz * dz) ** 0.5
        flag = " <-- SLIDE" if dxz > 0.10 else ""
        print(f"  t={s['total_time']:7.3f}->{e['total_time']:7.3f} ({e['total_time']-s['total_time']:.3f}s) "
              f"clip {s['clip_index']}->{e['clip_index']} state {s['state']}->{e['state']} "
              f"hips_dxz={dxz:.3f}m  dx={dx:+.3f} dy={dy:+.3f} dz={dz:+.3f}{flag}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
