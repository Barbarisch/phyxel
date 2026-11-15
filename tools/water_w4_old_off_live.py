"""WaterCore 52 W4.2 live (L4): the old CA water off everywhere. Per bench, at a generated lake or sea:
  - the old water: its mass (water_stats total_mass) and its region origin - before W4.2 it runs every frame in every world, whatever
    water.enabled says (the baked table binds by default: RiverCarve, water.enabled off, held 137 cells' worth, drawn nowhere);
  - the readers at a water point and a dry control (water_query: source, depth, a half-under box) and the camera 1 m under;
  - the frame's "Water" profile scope (the old update + the core's block) and the whole frame, 6 s at rest.
Prediction (written 2026-11-14, before W4.2): after, the old mass 0 in both benches and its region never moves; the readers answer
from "record" at the same values (0.5 / 1.0 m) - on Coast too, whose water.enabled is ON (its sea is in the record: waterSpanAt
counts water below sea level as ocean); the dry control 0; the sea plane still drawn on Coast (plane_enabled).

    python tools/water_w4_old_off_live.py --bench rivercarve|coast [--tag w4_old_before]
Writes docs/evidence/water_core_m4/<tag>_<bench>.json.
"""
import argparse, json, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
import water_splash_frames as W

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"
BENCHES = {
    # the 51.13 lake (52.1's spot): water 90, ground 87
    "rivercarve": {"url": "http://127.0.0.1:8113", "wet": (-2227.5, -1349.5), "dry": (-2169.1, 98.0, -1350.0),
                   "pose": {"x": -2169.1, "y": 98.0, "z": -1350.0, "yaw": -176.1, "pitch": -14.0}},
    # the sea north of the beach (game.json's comment: seabed below 16 from z ~676, y 3 at z 964); the dry control over the beach
    "coast": {"url": "http://127.0.0.1:8109", "wet": (165.5, 820.5), "dry": (165.0, 30.0, 600.0),
              "pose": {"x": 165.0, "y": 50.0, "z": 600.0, "yaw": 90.0, "pitch": -28.0}},
}


def readers(api, x, z, surface):
    camera_set(api, {"x": x, "y": surface - 1.0, "z": z, "yaw": 0.0, "pitch": 0.0}); time.sleep(1.5)
    ws = api.debug("water_stats", {})
    q = api.debug("water_query", {"points": [[x, surface - 1.0, z]], "boxes": [[x - 0.5, surface - 0.5, z - 0.5, x + 0.5, surface + 0.5, z + 0.5]]})
    p, b = q["points"][0], q["boxes"][0]
    return {"camera_submergence": ws.get("camera_submergence"), "camera_depth_below": ws.get("camera_depth_below"), "plane_enabled": ws.get("plane_enabled"),
            "source": p["source"], "depth_below": p["depth_below"], "submerged_fraction": b["submerged_fraction"],
            "old_submerged_fraction": b["old_submerged_fraction"], "old_water_on": q.get("old_water_on")}


def old_state(api):
    w = api.debug("water_stats", {})
    return {"total_mass": w.get("total_mass"), "surface_cells": w.get("surface_cells"), "origin": w.get("origin")}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bench", required=True, choices=sorted(BENCHES))
    ap.add_argument("--tag", default="w4_old_before")
    a = ap.parse_args()
    B = BENCHES[a.bench]
    api = Api(B["url"]); api.wait_status(600)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    camera_set(api, B["pose"]); time.sleep(30.0)
    out = {"bench": a.bench, "old_at_pose": old_state(api)}
    x, z = B["wet"]
    col = api.debug("water_av_column", {"points": [[x, z]]})["columns"][0]
    out["wet_column"] = col
    if col.get("surface_y") is None: raise SystemExit(f"no water at {x},{z}")
    surf = col.get("surface_mean") or col["surface_y"]
    out["wet"] = readers(api, x, z, surf)
    out["old_at_wet"] = old_state(api)
    dx, dy, dz = B["dry"]
    out["dry"] = readers(api, dx, dz, dy + 1.0)
    camera_set(api, B["pose"]); time.sleep(4.0)
    f0 = W.mark(api); time.sleep(6.0)
    s = W.summarize(W.frames_since(api, f0))
    prof = api.get("/api/debug/frame_profile")
    water_scope = None
    def walk(n):
        nonlocal water_scope
        if isinstance(n, dict):
            if n.get("name") == "Water": water_scope = n.get("durationMs")
            for v in n.values(): walk(v)
        elif isinstance(n, list):
            for v in n: walk(v)
    walk(prof)
    out["frame"] = {k: v for k, v in s.items() if k != "slow_frames"}
    out["frame"]["water_scope_ms"] = water_scope
    out["old_end"] = old_state(api)
    print(json.dumps({k: out[k] for k in ("old_at_pose", "wet", "old_at_wet", "dry", "old_end")}, indent=1))
    print("frame:", {k: out["frame"][k] for k in ("fps_mean", "dt_p50", "gpu_ms_median", "water_ms_mean", "water_scope_ms")})
    (EV / f"{a.tag}_{a.bench}.json").write_text(json.dumps(out, indent=1))


if __name__ == "__main__":
    main()
