"""D4 follow-ups, part 2 live (docs/evidence/voxfound_f7_prediction.txt S2 T3 / S3): glass and mirrors on the
switched world, on a COPY of a project (8120). Every artifact is the engine's: the voxel API (/api/world/fill routed to
the switched world) on generated terrain; readings from the engine's own routes (/api/render/stats, engine_timing).
  G glass: a 6 x 3 x 1 m Glass wall standing on the ground, a Stone pillar 3 m behind it. Frames at ONE stated pose
     before / after the fill; the wall's screen box compared (mean |diff|, dark-pixel share): glass at alpha 0.02 must
     leave the terrain and the pillar visible through it (a missing face under the glass would show the void).
Usage: python tools/voxfound_d4g_live.py <project-label> [glass|mirror|all]"""
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


def fps(n=5):
    v = []
    for _ in range(n):
        v.append(call("GET", "/api/debug/engine_timing").get("fps"))
        time.sleep(0.4)
    v = sorted(x for x in v if isinstance(x, (int, float)))
    return v[len(v) // 2] if v else None


def project(cam, fov_deg, w, h, p):
    """World point -> pixel for the engine's camera (yaw/pitch degrees, vertical fov)."""
    import math
    yaw, pitch = math.radians(cam["yaw"]), math.radians(cam["pitch"])
    f = (math.cos(yaw) * math.cos(pitch), math.sin(pitch), math.sin(yaw) * math.cos(pitch))
    up0 = (0.0, 1.0, 0.0)
    r = (f[1] * up0[2] - f[2] * up0[1], f[2] * up0[0] - f[0] * up0[2], f[0] * up0[1] - f[1] * up0[0])
    rn = math.sqrt(sum(c * c for c in r))
    r = tuple(c / rn for c in r)
    u = (r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0])
    d = tuple(p[i] - cam["pos"][i] for i in range(3))
    z = sum(d[i] * f[i] for i in range(3))
    if z <= 0.01:
        return None
    t = math.tan(math.radians(fov_deg) / 2)
    x = sum(d[i] * r[i] for i in range(3)) / (z * t * w / h)
    y = sum(d[i] * u[i] for i in range(3)) / (z * t)
    return (int((x * 0.5 + 0.5) * w), int((0.5 - y * 0.5) * h))


def region_stats(a_path, b_path, box):
    from PIL import Image
    a = Image.open(os.path.join(REPO, a_path)).convert("RGB")
    b = Image.open(os.path.join(REPO, b_path)).convert("RGB")
    x0, y0, x1, y1 = box
    x0, y0 = max(0, x0), max(0, y0)
    x1, y1 = min(a.width, x1), min(a.height, y1)
    diff, darkA, darkB, n = 0.0, 0, 0, 0
    pa, pb = a.load(), b.load()
    for y in range(y0, y1, 2):
        for x in range(x0, x1, 2):
            ca, cb = pa[x, y], pb[x, y]
            diff += sum(abs(ca[i] - cb[i]) for i in range(3)) / 3.0
            darkA += sum(ca) < 30
            darkB += sum(cb) < 30
            n += 1
    return {"box": [x0, y0, x1, y1], "samples": n, "mean_abs_diff": round(diff / max(n, 1), 2),
            "dark_share_before": round(darkA / max(n, 1), 4), "dark_share_after": round(darkB / max(n, 1), 4)}


def glass(out):
    cam = call("GET", "/api/camera")
    cx, cz = int(cam["position"]["x"]), int(cam["position"]["z"])
    gx, gz = cx + 4, cz + 8
    sy = int(call("GET", "/api/world/terrain_height?x=%d&z=%d" % (gx + 3, gz)).get("surface_y", 20))
    g = {"wall": [[gx, sy + 1, gz], [gx + 5, sy + 3, gz]], "pillar": [gx + 2, sy + 1, gz + 3]}
    call("POST", "/api/daynight/set", {"enabled": True, "paused": True, "timeOfDay": 12.0})
    # level the patch the wall stands on (one flat Stone floor) so every wall column rests on ground at sy
    call("POST", "/api/world/clear", {"x1": gx - 1, "y1": sy + 1, "z1": gz - 1, "x2": gx + 6, "y2": sy + 6, "z2": gz + 4})
    call("POST", "/api/world/fill", {"x1": gx - 1, "y1": sy, "z1": gz - 1, "x2": gx + 6, "y2": sy, "z2": gz + 4, "material": "Stone",
                                     "replace": True})   # fill is air-only unless replace (the S2 run omitted it: Grass)
    call("POST", "/api/world/fill", {"x1": gx + 2, "y1": sy + 1, "z1": gz + 3, "x2": gx + 2, "y2": sy + 3, "z2": gz + 3, "material": "Bricks"})
    pose = {"x": gx + 3.0, "y": sy + 2.6, "z": gz - 5.0}
    call("POST", "/api/camera", {"mode": "free", "position": pose, "yaw": 90.0, "pitch": -12.0})
    time.sleep(2.5)
    g["pose"] = {"position": pose, "yaw": 90.0, "pitch": -12.0}
    g["stats_before"] = call("GET", "/api/render/stats")
    g["fps_before"] = fps()
    g["shot_before"] = shot("d4g_glass_before.png")
    g["fill"] = call("POST", "/api/world/fill", {"x1": gx, "y1": sy + 1, "z1": gz, "x2": gx + 5, "y2": sy + 3, "z2": gz,
                                                 "material": "Glass"})
    time.sleep(2.5)
    g["voxel_check"] = call("GET", "/api/world/voxel?x=%d&y=%d&z=%d" % (gx + 2, sy + 2, gz))
    g["ground_under_glass"] = call("GET", "/api/world/voxel?x=%d&y=%d&z=%d" % (gx + 2, sy, gz))
    g["stats_after"] = call("GET", "/api/render/stats")
    g["fps_after"] = fps()
    g["shot_after"] = shot("d4g_glass_after.png")
    st = g["stats_after"] if isinstance(g["stats_after"], dict) else {}
    g["voxfound_oit_faces_before"] = (g["stats_before"] or {}).get("voxfound_oit_faces") if isinstance(g["stats_before"], dict) else None
    g["voxfound_oit_faces_after"] = st.get("voxfound_oit_faces")
    # the wall's screen box (its 8 corners projected at the pose; the engine's vertical fov 45)
    try:
        from PIL import Image
        im = Image.open(os.path.join(REPO, g["shot_after"]))
        w, h = im.size
        c = {"pos": (pose["x"], pose["y"], pose["z"]), "yaw": 90.0, "pitch": -12.0}
        pts = [project(c, 45.0, w, h, (x, y, z)) for x in (gx, gx + 6) for y in (sy + 1, sy + 4) for z in (gz, gz + 1)]
        pts = [p for p in pts if p]
        box = (min(p[0] for p in pts), min(p[1] for p in pts), max(p[0] for p in pts), max(p[1] for p in pts))
        g["wall_region"] = region_stats(g["shot_before"], g["shot_after"], box)
    except Exception as e:
        g["wall_region"] = {"error": str(e)}
    out["G"] = g


def mirror(out):
    """S3 T2: a Stone wall whose 2 x 2 m face (z = gz - 1) is Stone, then Mirror; a Gold cube + Bricks pillar in front.
    Frames before (Stone face) / after (Mirror face) at one pose; render/stats + the switched world's mirror plane."""
    cam = call("GET", "/api/camera")
    cx, cz = int(cam["position"]["x"]), int(cam["position"]["z"])
    gx, gz = cx - 6, cz + 10
    sy = int(call("GET", "/api/world/terrain_height?x=%d&z=%d" % (gx + 1, gz - 2)).get("surface_y", 20))
    m = {"wall": [[gx - 1, sy + 1, gz], [gx + 2, sy + 3, gz]], "face": [[gx, sy + 1, gz - 1], [gx + 1, sy + 2, gz - 1]],
         "gold": [gx - 2, sy + 1, gz - 4], "pillar": [gx + 3, sy + 1, gz - 3]}
    call("POST", "/api/daynight/set", {"enabled": True, "paused": True, "timeOfDay": 12.0})
    call("POST", "/api/world/clear", {"x1": gx - 3, "y1": sy + 1, "z1": gz - 6, "x2": gx + 4, "y2": sy + 6, "z2": gz})
    call("POST", "/api/world/fill", {"x1": gx - 3, "y1": sy, "z1": gz - 9, "x2": gx + 4, "y2": sy, "z2": gz + 1,
                                     "material": "Stone", "replace": True})
    call("POST", "/api/world/fill", {"x1": gx - 1, "y1": sy + 1, "z1": gz, "x2": gx + 2, "y2": sy + 3, "z2": gz, "material": "Stone"})
    call("POST", "/api/world/fill", {"x1": gx, "y1": sy + 1, "z1": gz - 1, "x2": gx + 1, "y2": sy + 2, "z2": gz - 1, "material": "Stone"})
    call("POST", "/api/world/fill", {"x1": gx - 2, "y1": sy + 1, "z1": gz - 4, "x2": gx - 2, "y2": sy + 1, "z2": gz - 4, "material": "Gold"})
    call("POST", "/api/world/fill", {"x1": gx + 3, "y1": sy + 1, "z1": gz - 3, "x2": gx + 3, "y2": sy + 2, "z2": gz - 3, "material": "Bricks"})
    pose = {"x": gx + 1.0, "y": sy + 2.4, "z": gz - 7.5}
    call("POST", "/api/camera", {"mode": "free", "position": pose, "yaw": 90.0, "pitch": -8.0})
    time.sleep(2.5)
    m["pose"] = {"position": pose, "yaw": 90.0, "pitch": -8.0}
    sb = call("GET", "/api/render/stats")
    m["stats_before"] = {k: sb.get(k) for k in ("mirror_pass_ran", "mirror_plane", "mirror_normal", "reflection_draw_calls",
                                                "mirror_geom_draw_calls", "reflected_cam")} if isinstance(sb, dict) else sb
    m["scan_before"] = call("POST", P, {"emissive_status": True}).get("mirror")
    m["fps_before"] = fps()
    m["shot_before"] = shot("d4g_mirror_before.png")
    m["fill"] = call("POST", "/api/world/fill", {"x1": gx, "y1": sy + 1, "z1": gz - 1, "x2": gx + 1, "y2": sy + 2, "z2": gz - 1,
                                                 "material": "Mirror", "replace": True})
    time.sleep(3.0)
    m["face_voxel"] = call("GET", "/api/world/voxel?x=%d&y=%d&z=%d" % (gx, sy + 1, gz - 1))
    sa = call("GET", "/api/render/stats")
    m["stats_after"] = {k: sa.get(k) for k in ("mirror_pass_ran", "mirror_plane", "mirror_normal", "reflection_draw_calls",
                                               "mirror_geom_draw_calls", "reflected_cam")} if isinstance(sa, dict) else sa
    m["scan_after"] = call("POST", P, {"emissive_status": True}).get("mirror")
    m["fps_after"] = fps()
    m["shot_after"] = shot("d4g_mirror_after.png")
    # a second pose: from the side, the gold cube's reflection must move with the view
    pose2 = {"x": gx - 3.0, "y": sy + 2.2, "z": gz - 6.5}
    call("POST", "/api/camera", {"mode": "free", "position": pose2, "yaw": 65.0, "pitch": -8.0})
    time.sleep(2.0)
    m["pose2"] = {"position": pose2, "yaw": 65.0, "pitch": -8.0}
    m["shot_side"] = shot("d4g_mirror_side.png")
    out["M"] = m


def census():
    return (call("POST", P, {"census": True}) or {}).get("census", {})


def fell(out):
    """S4 F1: a generated tree (the thinnest trunk with >= 60 Log voxels in each cube 1..3 m up) chopped from -x along
    +x until severed; the crown released; census before / after (bodies baked back) -- the ledger must be conserved."""
    cam = call("GET", "/api/camera")
    cx, cz = int(cam["position"]["x"]), int(cam["position"]["z"])
    sy = int(call("GET", "/api/world/terrain_height?x=%d&z=%d" % (cx, cz)).get("surface_y", 20))
    best = None
    for r_ in (16, 32, 48):
        sc = call("POST", "/api/world/scan_micro", {"x1": cx - r_, "y1": sy + 1, "z1": cz - r_, "x2": cx + r_, "y2": sy + 3, "z2": cz + r_})
        cols = {}
        for cell in sc.get("cells", []) if isinstance(sc, dict) else []:
            n = sum(v for m, v in cell.get("counts", {}).items() if m.startswith("Log"))
            if n >= 60:
                cols.setdefault((cell["x"], cell["z"]), {})[cell["y"]] = n
        for (xx, zz), ys in cols.items():
            if all(y in ys for y in (sy + 1, sy + 2, sy + 3)):
                th = int(call("GET", "/api/world/terrain_height?x=%d&z=%d" % (xx - 2, zz)).get("surface_y", sy))
                if th != sy:
                    continue
                if best is None or ys[sy + 1] < best[1]:
                    best = ((xx, sy + 1, zz), ys[sy + 1])
        if best:
            break
    f = {"trunk_cube": best[0] if best else None, "trunk_logs_1m": best[1] if best else None, "swings": []}
    if best:
        tx_, ty_, tz_ = best[0]
        call("POST", "/api/daynight/set", {"enabled": True, "paused": True, "timeOfDay": 12.0})
        call("POST", "/api/camera", {"mode": "free", "position": {"x": tx_ - 9.0, "y": ty_ + 5.0, "z": tz_ - 9.0},
                                     "yaw": 45.0, "pitch": -14.0})
        time.sleep(2.0)
        f["census_before"] = census()
        f["shot_before"] = shot("d4g_fell_before.png")
        f["fps_before"] = fps()
        bx = tx_ - 0.2
        for k in range(40):
            rr = call("POST", P, {"chop": {"x": bx, "y": ty_ + 0.5, "z": tz_ + 0.5, "dir": [1, 0, 0], "depth": 0.36,
                                           "release": True}})
            f["swings"].append({k2: rr.get(k2) for k2 in ("contact", "material", "footprint_columns", "kerf_voxels",
                                                          "severed", "crown_voxels", "anchored", "crown_box", "release")})
            if rr.get("severed"):
                break
            cv = rr.get("contact_voxel")
            if rr.get("material", "").startswith("Log") and cv:
                bx = (cv[0] + 0.5) / 8.0 + 0.3
            else:
                bx += 0.15
        time.sleep(1.0)
        f["fps_1s"] = fps(3)
        f["shot_falling"] = shot("d4g_fell_falling.png")
        time.sleep(9.0)
        f["fps_10s"] = fps()
        f["shot_after"] = shot("d4g_fell_after.png")
        f["bodies_10s"] = {k: v for k, v in (call("POST", "/api/debug/voxfound_bodies", {"status": True}) or {}).items()
                           if k in ("bodies", "awake", "as_world", "voxels")}
        # the ledger: stop the bodies, bake them back into the world, settle the rubble; census again
        call("POST", "/api/debug/voxfound_bodies", {"run": False})
        f["bake"] = (call("POST", "/api/debug/voxfound_bodies", {"bake_now": {"budget": 0, "keep_m": 0.0, "rest_ticks": 0,
                     "awake": True}, "status": True, "pen": False}).get("rest") or {}).get("baked")
        f["settle"] = call("POST", P, {"settle_rubble": True})
        c1 = census()
        c0 = f["census_before"]
        f["census_diff"] = {k: c1.get(k, 0) - c0.get(k, 0) for k in set(c0) | set(c1) if c1.get(k, 0) != c0.get(k, 0)}
    out["F"] = f


def main():
    out = {"project": sys.argv[1] if len(sys.argv) > 1 else "?", "t": time.strftime("%Y-%m-%d %H:%M:%S")}
    what = sys.argv[2] if len(sys.argv) > 2 else "all"
    if what in ("glass", "all"):
        glass(out)
    if what in ("mirror", "all"):
        mirror(out)
    if what in ("fell", "all"):
        fell(out)
    print(json.dumps(out)[:6000])
    with open(os.path.join(EVID, "voxfound_d4g_live.jsonl"), "a") as f:
        f.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
