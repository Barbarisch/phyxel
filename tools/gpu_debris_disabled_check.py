#!/usr/bin/env python3
"""gpu_init_failure_is_loud — DebrisInteractionPlan Phase 0 exit check (L4, live engine).

All debris is GPU debris. If the GPU solver is missing (init failed, or forced off with
PHYXEL_DISABLE_GPU_DEBRIS=1 / --disable-gpu-debris), breaks and blasts must still remove
voxels, but the absence of debris must be LOUD, not silent:

  * exactly one ERROR log line "GPU debris DISABLED (<reason>)";
  * /api/debug/gpu_physics echoes enabled=false + disabled_reason + refused_spawns;
  * /api/damage/apply reports debris=0 and debris_refused=N, and the process-wide
    refused counter grows by exactly N.

Control (--expect enabled, a normal launch): enabled=true, refused_spawns unchanged,
debris>0 and gpu_active grows.

Usage (engine already running on DebrisLab):
    # record the log size, launch with --disable-gpu-debris, then:
    python tools/gpu_debris_disabled_check.py --url http://localhost:8097 --expect disabled \
        --log phyxel.log --log-offset <bytes before launch>
    python tools/gpu_debris_disabled_check.py --url http://localhost:8097 --expect enabled
Exit code 0 = every check passed.
"""
import argparse
import json
import sys
import time
import urllib.request


def call(url, path, body=None):
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(url + path, data=data,
                                 headers={"Content-Type": "application/json"} if data else {})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return json.loads(r.read())
    except urllib.error.HTTPError as e:   # gpu_physics answers 4xx/5xx with a JSON body when off
        return json.loads(e.read() or b"{}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://localhost:8090")
    ap.add_argument("--expect", choices=["disabled", "enabled"], required=True)
    ap.add_argument("--log", default=None, help="engine log to count ERROR lines in (disabled run)")
    ap.add_argument("--log-offset", type=int, default=0,
                    help="byte size of --log recorded before this engine launch")
    # Default = the settle bench's BLAST chunk (5), which the bench restores before every run.
    # It used to be (16,15,16) — the middle of drop_layer's floor — and repeated runs left holes
    # there that made the bench report bodies tunnelling through the slab (2026-10-04).
    ap.add_argument("--at", default="176,15,16",
                    help="slab cell to blast (DebrisLab: Stone top y=15; keep it in the blast chunk x 160..191)")
    a = ap.parse_args()
    x, y, z = (int(v) for v in a.at.split(","))
    ok = True

    def check(name, cond, detail):
        nonlocal ok
        ok &= bool(cond)
        print(f"  [{'PASS' if cond else 'FAIL'}] {name}: {detail}")

    st0 = call(a.url, "/api/debug/gpu_physics", {})
    print("gpu_physics before:", st0)
    stats0 = call(a.url, "/api/debug/dynamic_stats")
    dmg = call(a.url, "/api/damage/apply", {"x": x + 0.5, "y": y + 0.5, "z": z + 0.5,
                                            "radius": 2.0, "energy": 1500.0})
    print("damage/apply:", dmg)
    time.sleep(0.5)
    st1 = call(a.url, "/api/debug/gpu_physics", {})
    stats1 = call(a.url, "/api/debug/dynamic_stats")
    voxel = call(a.url, f"/api/world/voxel?x={x}&y={y}&z={z}")
    print("gpu_physics after:", st1)

    check("blast removed voxels", dmg.get("broken", 0) > 0 and voxel.get("exists") is False,
          f"broken={dmg.get('broken')} target exists={voxel.get('exists')}")
    refused_delta = st1.get("refused_spawns", -1) - st0.get("refused_spawns", -1)
    if a.expect == "disabled":
        check("gpu_physics enabled=false", st1.get("enabled") is False, st1.get("enabled"))
        check("disabled_reason echoed", bool(st1.get("disabled_reason")), repr(st1.get("disabled_reason")))
        check("apply_damage debris=0", dmg.get("debris") == 0, dmg.get("debris"))
        check("apply_damage debris_refused>0", dmg.get("debris_refused", 0) > 0, dmg.get("debris_refused"))
        check("refused counter grew by exactly debris_refused", refused_delta == dmg.get("debris_refused"),
              f"delta={refused_delta}")
        check("no GPU bodies", stats1.get("gpu_active", 0) == 0, stats1.get("gpu_active"))
        if a.log:
            # Only this run: the log is appended across launches, so read from the byte
            # offset recorded BEFORE the engine was launched (--log-offset).
            with open(a.log, "rb") as f:
                f.seek(a.log_offset)
                run = f.read().decode("utf-8", errors="replace").splitlines()
            errs = [l for l in run if "[ERROR]" in l and "GPU debris DISABLED" in l]
            check("exactly one ERROR line", len(errs) == 1, errs[:2] or "none")
    else:
        check("gpu_physics enabled=true", st1.get("enabled") is True, st1.get("enabled"))
        check("refused counter unchanged", refused_delta == 0, f"delta={refused_delta}")
        check("apply_damage debris>0", dmg.get("debris", 0) > 0, dmg.get("debris"))
        check("apply_damage debris_refused=0", dmg.get("debris_refused") == 0, dmg.get("debris_refused"))
        check("GPU bodies grew", stats1.get("gpu_active", 0) > stats0.get("gpu_active", 0),
              f"{stats0.get('gpu_active')} -> {stats1.get('gpu_active')}")

    print("RESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
