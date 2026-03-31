"""
NPC Step-Up / Stair Climbing Test
==================================
Tests whether NPCs can:
1. Step over a 1-block-high obstacle
2. Climb a staircase (1-block steps)
3. Step down from elevated terrain

Uses the engine HTTP API at localhost:8090.
"""
import urllib.request, json, time, sys

API = "http://localhost:8090"

def get(path):
    return json.loads(urllib.request.urlopen(f"{API}{path}", timeout=5).read())

def post(path, body):
    data = json.dumps(body).encode()
    req = urllib.request.Request(f"{API}{path}", data=data, headers={"Content-Type": "application/json"})
    return json.loads(urllib.request.urlopen(req, timeout=10).read())

def get_npc(name):
    resp = get("/api/npcs")
    for n in resp["npcs"]:
        if n["name"] == name:
            p = n["position"]
            return (p["x"], p["y"], p["z"])
    return None

def screenshot(label=""):
    resp = get("/api/screenshot")
    if resp.get("success"):
        print(f"  Screenshot: {resp.get('path', '?')} {label}")

def place_voxel(x, y, z, material="Stone"):
    post("/api/world/fill", {"x1":x, "y1":y, "z1":z, "x2":x, "y2":y, "z2":z, "material": material})

def fill_region(x1, y1, z1, x2, y2, z2, material="Stone"):
    post("/api/world/fill", {"x1":x1, "y1":y1, "z1":z1, "x2":x2, "y2":y2, "z2":z2, "material": material})

def clear_region(x1, y1, z1, x2, y2, z2):
    post("/api/world/clear", {"x1":x1, "y1":y1, "z1":z1, "x2":x2, "y2":y2, "z2":z2})

def set_camera(x, y, z, yaw, pitch):
    post("/api/camera", {"x": x, "y": y, "z": z, "yaw": yaw, "pitch": pitch})

def track_npc(name, duration_s, interval_s=2.0):
    """Track NPC positions over time. Returns list of (x,y,z) tuples."""
    positions = []
    steps = int(duration_s / interval_s)
    for i in range(steps):
        time.sleep(interval_s)
        pos = get_npc(name)
        if pos:
            positions.append(pos)
            print(f"    t={i*interval_s:.0f}s: ({pos[0]:.1f}, {pos[1]:.1f}, {pos[2]:.1f})")
    return positions

results = []

# ============================================================================
# TEST 1: Step over a 1-block obstacle
# ============================================================================
print("=" * 60)
print("TEST 1: NPC steps over 1-block obstacle")
print("=" * 60)
print()

# The Blacksmith patrols (10,17,12)->(10,17,20)->(14,17,20)->(14,17,12)
# Place a 1-block-high obstacle across z=15 from x=8 to x=16
# This is a single voxel high step — the NPC should be able to step over it

# Clear any previous test artifacts
clear_region(4, 17, 14, 20, 20, 16)
time.sleep(0.5)

# Place 1-block obstacle line at y=17 (ground is y=16, so this is 1 block up)
print("  Placing 1-block obstacle at z=15, x=8-16, y=17...")
fill_region(8, 17, 15, 16, 17, 15, "Stone")
time.sleep(1)

# Set camera to see the obstacle
set_camera(12, 25, 10, -90, -45)
time.sleep(0.5)
screenshot("1-block obstacle placed")

# Track Blacksmith for 25 seconds
print("  Tracking Blacksmith (should step over 1-block obstacle)...")
positions = track_npc("Blacksmith", 25, 2.5)

if positions:
    y_vals = [p[1] for p in positions]
    z_vals = [p[2] for p in positions]
    y_max = max(y_vals)
    y_min = min(y_vals)
    crossed = any(z < 14.5 for z in z_vals) and any(z > 15.5 for z in z_vals)
    total_dist = sum(
        ((positions[i][0]-positions[i-1][0])**2 + (positions[i][2]-positions[i-1][2])**2)**0.5
        for i in range(1, len(positions))
    )
    print(f"\n  Y range: {y_min:.1f} to {y_max:.1f}")
    print(f"  Z range: {min(z_vals):.1f} to {max(z_vals):.1f}")
    print(f"  Total XZ distance: {total_dist:.1f}")
    print(f"  Crossed obstacle z=15: {'YES' if crossed else 'NO'}")
    
    if total_dist > 5.0 and crossed:
        print("  RESULT: PASS — NPC stepped over obstacle")
        results.append(("1-block step-over", "PASS"))
    elif total_dist > 5.0:
        print("  RESULT: PARTIAL — NPC moved but possibly went around")
        results.append(("1-block step-over", "PARTIAL"))
    else:
        print("  RESULT: FAIL — NPC stuck at obstacle")
        results.append(("1-block step-over", "FAIL"))

# Clean up
clear_region(8, 17, 15, 16, 17, 15)
time.sleep(0.5)

# ============================================================================
# TEST 2: Climb a staircase (1-block steps going up)
# ============================================================================
print()
print("=" * 60)
print("TEST 2: NPC climbs staircase")
print("=" * 60)
print()

# Build a 3-step staircase going north (z direction) at x=12
# Step 1: y=17 at z=14
# Step 2: y=18 at z=13
# Step 3: y=19 at z=12
# The flat terrain is at y=16 surface. Voxels placed become the new surface.
# Each step is 1 block higher.
print("  Building 3-step staircase at x=10-14...")
# Step 1: z=14, one block up from ground
fill_region(10, 17, 14, 14, 17, 14, "Wood")
# Step 2: z=13, two blocks up 
fill_region(10, 17, 13, 14, 18, 13, "Wood")
# Step 3: z=12, three blocks up
fill_region(10, 17, 12, 14, 19, 12, "Wood")
# Landing platform at top
fill_region(10, 17, 11, 14, 19, 11, "Wood")
time.sleep(1)

# Camera angle to see the staircase
set_camera(18, 25, 12, -135, -35)
time.sleep(0.5)
screenshot("staircase built")

# Track Blacksmith — waypoint at (10,17,12) is now at top of stairs
print("  Tracking Blacksmith (should climb stairs to reach waypoint)...")
positions = track_npc("Blacksmith", 30, 2.5)

if positions:
    y_vals = [p[1] for p in positions]
    y_max = max(y_vals)
    reached_top = y_max >= 19.5  # Top of stairs is y=19 surface, NPC stands at y=20
    total_dist = sum(
        ((positions[i][0]-positions[i-1][0])**2 + (positions[i][2]-positions[i-1][2])**2)**0.5
        for i in range(1, len(positions))
    )
    print(f"\n  Y range: {min(y_vals):.1f} to {y_max:.1f}")
    print(f"  Total XZ distance: {total_dist:.1f}")
    print(f"  Reached stair top (y>=19.5): {'YES' if reached_top else 'NO'}")
    
    if reached_top:
        print("  RESULT: PASS — NPC climbed the staircase!")
        results.append(("Staircase climb", "PASS"))
    elif total_dist > 5.0:
        print("  RESULT: PARTIAL — NPC moved but didn't reach top")
        results.append(("Staircase climb", "PARTIAL"))
    else:
        print("  RESULT: FAIL — NPC stuck at staircase")
        results.append(("Staircase climb", "FAIL"))

screenshot("staircase test done")

# Clean up stairs
clear_region(10, 17, 11, 14, 19, 14)
time.sleep(0.5)

# ============================================================================
# TEST 3: Step down from elevated platform
# ============================================================================
print()
print("=" * 60)
print("TEST 3: NPC steps down from elevated platform")
print("=" * 60)
print()

# Build a 1-block elevated platform across the patrol path
print("  Building 1-block platform at z=18, x=8-16...")
fill_region(8, 17, 18, 16, 17, 18, "Stone")
time.sleep(1)

set_camera(12, 25, 22, -90, -45)
time.sleep(0.5)
screenshot("step-down platform placed")

print("  Tracking Blacksmith (should step up then back down)...")
positions = track_npc("Blacksmith", 25, 2.5)

if positions:
    y_vals = [p[1] for p in positions]
    z_vals = [p[2] for p in positions]
    went_above = any(y > 17.5 for y in y_vals)
    went_below = any(y < 17.5 for y in y_vals)
    total_dist = sum(
        ((positions[i][0]-positions[i-1][0])**2 + (positions[i][2]-positions[i-1][2])**2)**0.5
        for i in range(1, len(positions))
    )
    print(f"\n  Y range: {min(y_vals):.1f} to {max(y_vals):.1f}")
    print(f"  Z range: {min(z_vals):.1f} to {max(z_vals):.1f}")
    print(f"  Total XZ distance: {total_dist:.1f}")
    print(f"  Went above y=17.5: {'YES' if went_above else 'NO'}")
    print(f"  Returned below y=17.5: {'YES' if went_below else 'NO'}")

    if went_above and went_below and total_dist > 5.0:
        print("  RESULT: PASS — NPC stepped up and back down")
        results.append(("Step down", "PASS"))
    elif total_dist > 5.0:
        print("  RESULT: PARTIAL — NPC moved but didn't step up")
        results.append(("Step down", "PARTIAL"))
    else:
        print("  RESULT: FAIL — NPC stuck")
        results.append(("Step down", "FAIL"))

# Clean up
clear_region(8, 17, 18, 16, 17, 18)
time.sleep(0.5)

# ============================================================================
# SUMMARY
# ============================================================================
print()
print("=" * 60)
print("SUMMARY")
print("=" * 60)
for name, result in results:
    status = "PASS" if result == "PASS" else "FAIL" if result == "FAIL" else "PARTIAL"
    print(f"  [{status:7s}] {name}")

all_pass = all(r == "PASS" for _, r in results)
print(f"\nOverall: {'ALL PASS' if all_pass else 'SOME TESTS NEED WORK'}")

# Write results to file
with open("npc_step_test_results.txt", "w") as f:
    for name, result in results:
        f.write(f"{result}: {name}\n")
    for i, pos in enumerate(positions):
        f.write(f"last_test t={i*2.5:.0f}s: ({pos[0]:.1f}, {pos[1]:.1f}, {pos[2]:.1f})\n")
