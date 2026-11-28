"""D4 follow-ups live (docs/evidence/voxfound_f7_prediction.txt "D4 FOLLOW-UPS"): the features D4 left with no 1/8 m
feed, each checked on the running switched world (a COPY of a project, 8120). Every artifact is the engine's: the voxel
API (fill / clear routed to the switched world), the template spawner, the entity spawner, the Python binding; the
readings are the engine's own routes.
  E1 emissive: a 1 m glow cube -> one U3.2 light (emissive_status), cleared -> none; frames before / after at night.
  E2 fire: barony_campfire (state=flaming) spawned static -> flaming points registered and kept while the cube holds
     matter; cleared -> the VFX feed drops them.
  E3 residency: an animated character spawned 6 m above the ground FALLS to it (the hold read today's Chunk map and
     held every character on a streaming world since D2).
  E4 validators: validate_placed_objects reports resident structures (not all "unverifiable"); scan_micro / micro_cells
     / occupancy_cell / light_occupancy read the switched world.
  E5 python: ChunkManager.add_cube_with_material / remove_cube change the world."""
import json, os, shutil, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")


def call(m, p, b=None, timeout=120):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(r, timeout=timeout) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        return {"http_status": e.code, "body": e.read().decode()[:400]}
    except Exception as e:
        return {"exception": str(e)}


def shot(name):
    s = call("GET", "/api/screenshot")
    path = s.get("path") if isinstance(s, dict) else None
    if path:
        src = path if os.path.isabs(path) else os.path.join(REPO, path)
        dst = os.path.join(EVID, name)
        try:
            shutil.copyfile(src, dst)
            return "docs/evidence/" + name
        except Exception as e:
            return "copy failed: %s" % e
    return s


def voxel(x, y, z):
    return call("GET", "/api/world/voxel?x=%d&y=%d&z=%d" % (x, y, z)).get("voxfound")


def main():
    out = {"project": sys.argv[1] if len(sys.argv) > 1 else "?"}
    cam = call("GET", "/api/camera")
    cx, cz = int(cam["position"]["x"]), int(cam["position"]["z"])
    th = call("GET", "/api/world/terrain_height?x=%d&z=%d" % (cx + 6, cz + 6))
    sy = int(th.get("surface_y", 20))
    gx, gz = cx + 6, cz + 6
    out["ground"] = [gx, sy, gz]

    # E1 emissive
    call("POST", "/api/daynight/set", {"enabled": True, "paused": True, "timeOfDay": 22.0})
    call("POST", "/api/camera", {"mode": "free", "position": {"x": gx - 3.5, "y": sy + 3.0, "z": gz - 3.5}, "yaw": 45.0, "pitch": -30.0})
    time.sleep(1.5)
    e0 = call("POST", P, {"emissive_status": True})
    out["E1_shot_before"] = shot("d4f_glow_before.png")
    f = call("POST", "/api/world/fill", {"x1": gx, "y1": sy + 1, "z1": gz, "x2": gx, "y2": sy + 1, "z2": gz, "material": "glow"})
    time.sleep(2.0)
    e1 = call("POST", P, {"emissive_status": True})
    e1["lights_api"] = call("GET", "/api/lights")
    out["E1_shot_glow"] = shot("d4f_glow_on.png")
    call("POST", "/api/world/clear", {"x1": gx, "y1": sy + 1, "z1": gz, "x2": gx, "y2": sy + 1, "z2": gz})
    time.sleep(2.0)
    e2 = call("POST", P, {"emissive_status": True})
    out["E1"] = {"before": e0, "fill": f, "with_glow": e1, "after_clear": e2}

    # E2 fire (a static template with state=flaming voxels)
    tx, tz = gx + 4, gz
    tsy = int(call("GET", "/api/world/terrain_height?x=%d&z=%d" % (tx, tz)).get("surface_y", sy))
    t = call("POST", "/api/world/template", {"name": "barony_campfire", "position": {"x": tx, "y": tsy + 1, "z": tz}, "static": True})
    time.sleep(1.5)
    f1 = call("POST", P, {"emissive_status": True})
    vfx1 = call("GET", "/api/render/stats")
    call("POST", "/api/world/clear", {"x1": tx - 2, "y1": tsy + 1, "z1": tz - 2, "x2": tx + 3, "y2": tsy + 4, "z2": tz + 3})
    time.sleep(1.5)
    f2 = call("POST", P, {"emissive_status": True})
    out["E2"] = {"spawn": t, "after_spawn": f1, "after_clear": f2,
                 "fire_stats": {k: v for k, v in vfx1.items() if "fire" in k or "vfx" in k} if isinstance(vfx1, dict) else vfx1}

    # E3 residency: a character dropped 6 m above the ground falls to it
    rx, rz = gx - 3, gz + 3
    rsy = int(call("GET", "/api/world/terrain_height?x=%d&z=%d" % (rx, rz)).get("surface_y", sy))
    sp = call("POST", "/api/entity/spawn", {"type": "animated", "id": "d4f_faller",
                                            "position": {"x": rx + 0.5, "y": rsy + 7.0, "z": rz + 0.5}})
    time.sleep(0.3)
    y0 = call("GET", "/api/entity/d4f_faller")
    time.sleep(4.0)
    y1 = call("GET", "/api/entity/d4f_faller")
    out["E3"] = {"spawn": sp, "ground_surface_y": rsy, "t0": y0.get("position") if isinstance(y0, dict) else y0,
                 "t4s": y1.get("position") if isinstance(y1, dict) else y1}

    # E4 validators / probes on the switched world
    call("POST", "/api/world/fill", {"x1": gx + 8, "y1": sy + 1, "z1": gz, "x2": gx + 8, "y2": sy + 1, "z2": gz, "material": "Stone"})
    time.sleep(1.0)
    out["E4"] = {
        "validate_placed": call("POST", "/api/placed_objects/validate", {}),
        "validate_world": {k: v for k, v in call("POST", "/api/world/validate", {}).items() if k.startswith("scanned") or k in ("passed", "errors", "issues")},
        "scan_micro": call("POST", "/api/world/scan_micro", {"x1": gx + 8, "y1": sy + 1, "z1": gz, "x2": gx + 8, "y2": sy + 1, "z2": gz}),
        "micro_cells": call("POST", "/api/debug/micro_cells", {"cells": [[(gx + 8) * 9 + 4, (sy + 1) * 9 + 4, gz * 9 + 4], [(gx + 8) * 9 + 4, (sy + 3) * 9 + 4, gz * 9 + 4]]}),
        "occupancy_cell": call("GET", "/api/debug/occupancy_cell?x=%d&y=%d&z=%d" % (gx + 8, sy + 1, gz)),
        "light_occupancy_cell": call("GET", "/api/debug/light_occupancy?x=%d&y=%d&z=%d" % (gx + 8, sy + 1, gz)),
    }

    # E5 python
    px, py, pz = gx + 10, sy + 1, gz
    script = ("import phyxel\ncm = phyxel.get_app().get_chunk_manager()\n"
              "a = cm.add_cube_with_material(%d, %d, %d, 'Gold')\n"
              "phyxel.Logger.info('D4F', 'add_cube=' + str(a))\n") % (px, py, pz)
    r1 = call("POST", "/api/script", {"code": script})
    time.sleep(1.0)
    v1 = voxel(px, py, pz)
    r2 = call("POST", "/api/script", {"code": "import phyxel\nphyxel.get_app().get_chunk_manager().remove_cube(%d, %d, %d)\n" % (px, py, pz)})
    time.sleep(1.0)
    v2 = voxel(px, py, pz)
    out["E5"] = {"add_script": r1, "after_add": v1, "remove_script": r2, "after_remove": v2}

    # E6 the axe chop: a generated trunk near the camera, chopped from -x along +x until it is severed
    # a real trunk: a column with >= 100 Log voxels in each of the cubes 1..3 m above the ground
    trunk = None
    for r_ in (16, 32, 48):
        sc = call("POST", "/api/world/scan_micro", {"x1": cx - r_, "y1": sy + 1, "z1": cz - r_, "x2": cx + r_, "y2": sy + 3, "z2": cz + r_})
        logs = {}
        for cell in sc.get("cells", []) if isinstance(sc, dict) else []:
            n = sum(v for m, v in cell.get("counts", {}).items() if m.startswith("Log"))
            if n >= 100:
                logs.setdefault((cell["x"], cell["z"]), set()).add(cell["y"])
        for (xx, zz), ys in sorted(logs.items()):
            if {sy + 1, sy + 2, sy + 3} <= ys:
                trunk = (xx, sy + 1, zz)
                break
        if trunk:
            break
    e6 = {"trunk_cube": trunk, "swings": []}
    if trunk:
        tx_, ty_, tz_ = trunk
        call("POST", "/api/camera", {"mode": "free", "position": {"x": tx_ - 7.0, "y": ty_ + 4.0, "z": tz_ - 7.0},
                                     "yaw": 45.0, "pitch": -12.0})
        time.sleep(1.5)
        e6["shot_before"] = shot("d4f_chop_before.png")
        e6["fps_before"] = call("GET", "/api/debug/engine_timing").get("fps")
        bx = tx_ - 0.2
        for k in range(30):
            rr = call("POST", P, {"chop": {"x": bx, "y": ty_ + 0.5, "z": tz_ + 0.5, "dir": [1, 0, 0], "depth": 0.36}})
            e6["swings"].append({k2: rr.get(k2) for k2 in ("contact", "material", "contact_voxel", "kerf_voxels", "flood", "severed", "release")})
            if rr.get("severed"):
                break
            cv = rr.get("contact_voxel")
            if rr.get("material", "").startswith("Log") and cv:
                bx = (cv[0] + 0.5) / 8.0 + 0.3   # the blade follows the notch to its new frontier
            else:
                bx += 0.15                         # no wood in reach yet: step in
        e6["trunk_after"] = call("POST", "/api/world/scan_micro", {"x1": tx_ - 1, "y1": ty_, "z1": tz_ - 1, "x2": tx_ + 1, "y2": ty_ + 4, "z2": tz_ + 1})
        time.sleep(1.0)
        e6["bodies_1s"] = call("POST", "/api/debug/voxfound_bodies", {"status": True})
        e6["shot_falling"] = shot("d4f_chop_falling.png")
        time.sleep(4.0)
        e6["shot_after"] = shot("d4f_chop_after.png")
        e6["fps_after"] = [call("GET", "/api/debug/engine_timing").get("fps") for _ in range(3)]
        e6["bodies_5s"] = call("POST", "/api/debug/voxfound_bodies", {"status": True})
    # control: the same switched-world break (B key / blast path, F7.5) on a plain Stone cube -- does a world break
    # alone leave the frame rate where the chop's release left it?
    call("POST", "/api/world/fill", {"x1": gx + 12, "y1": sy + 1, "z1": gz, "x2": gx + 12, "y2": sy + 1, "z2": gz, "material": "Stone"})
    time.sleep(1.0)
    fb = call("GET", "/api/debug/engine_timing").get("fps")
    br = call("POST", "/api/damage/apply", {"x": gx + 12.5, "y": sy + 1.5, "z": gz + 0.5, "radius": 8.0, "energy": 0.02})
    time.sleep(5.0)
    out["E6_control_break"] = {"fps_before": fb, "break": {k: br.get(k) for k in ("bodies", "error")} if isinstance(br, dict) else br,
                               "fps_after": [call("GET", "/api/debug/engine_timing").get("fps") for _ in range(3)],
                               "bodies_5s": call("POST", "/api/debug/voxfound_bodies", {"status": True})}
    out["E6"] = e6

    with open(os.path.join(EVID, "voxfound_d4f_live.jsonl"), "a", encoding="utf-8") as fh:
        fh.write(json.dumps(out) + "\n")
    print(json.dumps(out)[:6000])


if __name__ == "__main__":
    main()
