"""
Pathing Features Live Demo
===========================
Visually demonstrates the new NPC pathing improvements in a running engine.
Watch in-game while this script runs — no screenshots, no leftover geometry.

Usage:
    python scripts/pathing_features_demo.py

Requires the engine to be running with the HTTP API on localhost:8090.

Tests
-----
1. Jump Gap  (Phase 3 — NavGrid jump links)
   Two floating stone islands, 1-cell gap between them.
   NPC must cross via an auto-generated jump link.

2. Terrain Probe + Path Invalidation  (Phases 1 & 2)
   NPC walks a straight corridor.  8 s in, a wide pit is
   opened ahead of it.  The forward probe should detect
   the change and stop the NPC before it falls.

Cleanup
-------
Each test tears down its geometry at the start (so a previous
interrupted run leaves nothing behind) and again at the end.
A try/finally ensures cleanup even on Ctrl+C.
"""

import urllib.request
import json
import time
import sys

API = "http://localhost:8090"

# ---------------------------------------------------------------------------
# HTTP helpers
# ---------------------------------------------------------------------------

def _get(path):
    with urllib.request.urlopen(f"{API}{path}", timeout=5) as r:
        return json.loads(r.read())

def _post(path, body):
    data = json.dumps(body).encode()
    req = urllib.request.Request(
        f"{API}{path}", data=data,
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.loads(r.read())

def fill_region(x1, y1, z1, x2, y2, z2, material="Stone"):
    _post("/api/world/fill",
          {"x1": x1, "y1": y1, "z1": z1,
           "x2": x2, "y2": y2, "z2": z2,
           "material": material})

def clear_region(x1, y1, z1, x2, y2, z2):
    _post("/api/world/clear",
          {"x1": x1, "y1": y1, "z1": z1,
           "x2": x2, "y2": y2, "z2": z2})

def set_camera(x, y, z, yaw, pitch):
    _post("/api/camera", {"x": x, "y": y, "z": z, "yaw": yaw, "pitch": pitch})

def spawn_npc(name, x, y, z, waypoints, speed=2.5):
    _post("/api/npc/spawn", {
        "name": name,
        "position": {"x": x, "y": y, "z": z},
        "behavior": "patrol",
        "waypoints": waypoints,
        "walkSpeed": speed,
        "waitTime": 1.0,
    })

def remove_npc(name):
    try:
        _post("/api/npc/remove", {"name": name})
    except Exception:
        pass  # fine if NPC was never spawned

def get_npc_pos(name):
    resp = _get("/api/npcs")
    for n in resp.get("npcs", []):
        if n["name"] == name:
            p = n["position"]
            return (p["x"], p["y"], p["z"])
    return None

def track(name, duration_s, interval_s=2.0):
    """Poll the NPC position on a timer and print each sample.
    Returns a list of (x, y, z) tuples."""
    positions = []
    steps = max(1, int(duration_s / interval_s))
    for i in range(steps):
        time.sleep(interval_s)
        pos = get_npc_pos(name)
        if pos:
            positions.append(pos)
            print(f"    t={( i + 1) * interval_s:4.0f}s  "
                  f"({pos[0]:6.1f}, {pos[1]:5.1f}, {pos[2]:6.1f})")
        else:
            print(f"    t={( i + 1) * interval_s:4.0f}s  [NPC not found]")
    return positions

# ---------------------------------------------------------------------------
# Geometry constants
# Floating islands high enough to be visible but above typical terrain.
# ---------------------------------------------------------------------------

BASE_Y = 60        # solid-voxel surface Y
NPC_Y  = BASE_Y + 1  # stand height (one above surface)

# Test 1 — jump gap
T1_L_X1, T1_L_X2 = 60, 72   # left island X
T1_GAP_X          = 73       # single non-walkable column
T1_R_X1, T1_R_X2 = 74, 86   # right island X
T1_Z1,   T1_Z2   = 60, 70   # Z width for both islands

# Test 2 — terrain probe
T2_X1, T2_X2     = 60, 95   # full corridor X
T2_Z1, T2_Z2     = 80, 90   # Z width
T2_PIT_X1        = 78       # pit starts here
T2_PIT_X2        = 82       # pit ends here (5 cells — too wide to jump)

# ---------------------------------------------------------------------------
# Per-test cleanup helpers (idempotent — safe to call before geometry exists)
# ---------------------------------------------------------------------------

def cleanup_t1():
    remove_npc("JumpTester")
    clear_region(T1_L_X1, BASE_Y - 2, T1_Z1,
                 T1_R_X2, BASE_Y + 6,  T1_Z2)

def cleanup_t2():
    remove_npc("ProbeTester")
    clear_region(T2_X1, BASE_Y - 2, T2_Z1,
                 T2_X2, BASE_Y + 6,  T2_Z2)

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

results = []

try:

    # ========================================================================
    # TEST 1 — Jump Gap  (Phase 3: NavGrid jump links)
    # ========================================================================
    print()
    print("=" * 60)
    print("TEST 1: NPC crosses a 1-cell gap via jump link  (Phase 3)")
    print("=" * 60)

    # Always clean the test volume first so re-runs start fresh
    print("  Cleaning test area...")
    cleanup_t1()
    time.sleep(0.4)

    # Build two islands; the column at X=T1_GAP_X is intentionally left empty
    print("  Building left island  "
          f"(X={T1_L_X1}–{T1_L_X2}, Z={T1_Z1}–{T1_Z2}, Y={BASE_Y})...")
    fill_region(T1_L_X1, BASE_Y, T1_Z1, T1_L_X2, BASE_Y, T1_Z2, "Stone")

    print("  Building right island "
          f"(X={T1_R_X1}–{T1_R_X2}, Z={T1_Z1}–{T1_Z2}, Y={BASE_Y})...")
    fill_region(T1_R_X1, BASE_Y, T1_Z1, T1_R_X2, BASE_Y, T1_Z2, "Stone")

    time.sleep(0.6)

    # Position camera above and slightly south so the gap is front-and-centre
    set_camera(73, BASE_Y + 18, T1_Z1 - 10, 0, -38)
    time.sleep(0.3)

    NPC1 = "JumpTester"
    print(f"\n  Spawning {NPC1} on the left island (X={T1_L_X1 + 5})...")
    spawn_npc(NPC1,
              x=T1_L_X1 + 5, y=NPC_Y, z=(T1_Z1 + T1_Z2) // 2,
              waypoints=[
                  {"x": T1_L_X1 + 5, "y": NPC_Y, "z": (T1_Z1 + T1_Z2) // 2},
                  {"x": T1_R_X2 - 5, "y": NPC_Y, "z": (T1_Z1 + T1_Z2) // 2},
              ],
              speed=3.0)
    time.sleep(1.0)

    print(f"\n  Tracking {NPC1} for 40 s")
    print(f"  (needs to reach X > {T1_GAP_X + 0.5:.0f} to cross the gap)")
    print()
    positions1 = track(NPC1, 40, 2.0)

    if positions1:
        x_vals     = [p[0] for p in positions1]
        crossed    = any(x > T1_GAP_X + 0.5 for x in x_vals)
        max_x      = max(x_vals)
        total_dist = sum(abs(positions1[i][0] - positions1[i - 1][0])
                         for i in range(1, len(positions1)))
        print(f"\n  Max X reached : {max_x:.1f}  (gap at X={T1_GAP_X})")
        print(f"  Crossed gap   : {'YES' if crossed else 'NO'}")
        print(f"  Total X travel: {total_dist:.1f}")

        if crossed:
            print("  RESULT: PASS — NPC jumped the gap!")
            results.append(("Jump gap (Phase 3)", "PASS"))
        elif total_dist > 3.0:
            print("  RESULT: PARTIAL — NPC moved but didn't cross")
            results.append(("Jump gap (Phase 3)", "PARTIAL"))
        else:
            print("  RESULT: FAIL — NPC stuck before gap")
            results.append(("Jump gap (Phase 3)", "FAIL"))
    else:
        print("  RESULT: FAIL — NPC not found")
        results.append(("Jump gap (Phase 3)", "FAIL"))

    cleanup_t1()
    time.sleep(0.5)

    # ========================================================================
    # TEST 2 — Terrain Probe + Path Invalidation  (Phases 1 & 2)
    # ========================================================================
    print()
    print("=" * 60)
    print("TEST 2: Terrain probe detects removed floor  (Phases 1 + 2)")
    print("=" * 60)

    print("  Cleaning test area...")
    cleanup_t2()
    time.sleep(0.4)

    print(f"  Building corridor "
          f"(X={T2_X1}–{T2_X2}, Z={T2_Z1}–{T2_Z2}, Y={BASE_Y})...")
    fill_region(T2_X1, BASE_Y, T2_Z1, T2_X2, BASE_Y, T2_Z2, "Stone")
    time.sleep(0.6)

    # Camera: side-on view so the whole corridor is visible
    mid_z = (T2_Z1 + T2_Z2) // 2
    set_camera((T2_X1 + T2_X2) // 2, BASE_Y + 22, T2_Z1 - 14, 0, -42)
    time.sleep(0.3)

    NPC2 = "ProbeTester"
    print(f"\n  Spawning {NPC2} at X={T2_X1 + 3}, heading toward X={T2_X2 - 3}...")
    spawn_npc(NPC2,
              x=T2_X1 + 3, y=NPC_Y, z=mid_z,
              waypoints=[
                  {"x": T2_X1 + 3, "y": NPC_Y, "z": mid_z},
                  {"x": T2_X2 - 3, "y": NPC_Y, "z": mid_z},
              ],
              speed=3.0)
    time.sleep(1.0)

    print(f"\n  Letting NPC walk for 8 s before opening pit...")
    print()
    pre_positions = track(NPC2, 8, 2.0)

    print(f"\n  *** Opening pit at X={T2_PIT_X1}–{T2_PIT_X2} ***")
    clear_region(T2_PIT_X1, BASE_Y, T2_Z1, T2_PIT_X2, BASE_Y, T2_Z2)
    time.sleep(0.3)

    print(f"\n  Tracking {NPC2} for 15 s — should stop before X={T2_PIT_X1}")
    print()
    post_positions = track(NPC2, 15, 2.0)

    if pre_positions and post_positions:
        pre_max_x   = max(p[0] for p in pre_positions)
        post_x_vals = [p[0] for p in post_positions]
        max_post_x  = max(post_x_vals)
        crossed_pit = any(x > T2_PIT_X2 + 0.5 for x in post_x_vals)
        stopped     = max_post_x < T2_PIT_X1 + 1.5
        was_moving  = pre_max_x > T2_X1 + 4

        print(f"\n  NPC was moving before pit : {'YES' if was_moving else 'NO'}")
        print(f"  Max X after pit opened    : {max_post_x:.1f}"
              f"  (pit X={T2_PIT_X1}–{T2_PIT_X2})")
        print(f"  Stopped short of pit      : {'YES' if stopped else 'NO'}")
        print(f"  Crossed pit (should NOT)  : {'YES' if crossed_pit else 'NO'}")

        if was_moving and stopped and not crossed_pit:
            print("  RESULT: PASS — NPC stopped when terrain was invalidated!")
            results.append(("Terrain probe + invalidation (Phases 1+2)", "PASS"))
        elif not was_moving:
            print("  RESULT: FAIL — NPC never started moving")
            results.append(("Terrain probe + invalidation (Phases 1+2)", "FAIL"))
        elif crossed_pit:
            print("  RESULT: FAIL — NPC walked into the pit")
            results.append(("Terrain probe + invalidation (Phases 1+2)", "FAIL"))
        else:
            print("  RESULT: PARTIAL — NPC stopped but uncertain if probe fired")
            results.append(("Terrain probe + invalidation (Phases 1+2)", "PARTIAL"))
    else:
        print("  RESULT: FAIL — insufficient position data")
        results.append(("Terrain probe + invalidation (Phases 1+2)", "FAIL"))

    cleanup_t2()
    time.sleep(0.5)

except KeyboardInterrupt:
    print("\n\n  Interrupted — cleaning up...")
    cleanup_t1()
    cleanup_t2()
    sys.exit(1)

finally:
    # Belt-and-suspenders: always runs even if an unhandled exception occurs
    cleanup_t1()
    cleanup_t2()

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
print()
print("=" * 60)
print("SUMMARY")
print("=" * 60)
for label, result in results:
    tag = f"{result:7s}"
    print(f"  [{tag}]  {label}")

all_pass = all(r == "PASS" for _, r in results)
print(f"\nOverall: {'ALL PASS' if all_pass else 'NEEDS WORK'}")
