"""
NPC Subcube Step Test
=====================
Tests whether NPCs with capsule colliders can walk over subcube-height (1/3 block)
obstacles and stairs. Also verifies they CANNOT walk over full-block obstacles.

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

def place_subcube(x, y, z, sx, sy, sz, material="Stone"):
    """Place a single subcube. (x,y,z)=parent cube pos, (sx,sy,sz)=subcube pos within cube (0-2)."""
    return post("/api/world/subcube", {"x": x, "y": y, "z": z, "sx": sx, "sy": sy, "sz": sz, "material": material})

def fill_region(x1, y1, z1, x2, y2, z2, material="Stone"):
    return post("/api/world/fill", {"x1":x1, "y1":y1, "z1":z1, "x2":x2, "y2":y2, "z2":z2, "material": material})

def clear_region(x1, y1, z1, x2, y2, z2):
    return post("/api/world/clear", {"x1":x1, "y1":y1, "z1":z1, "x2":x2, "y2":y2, "z2":z2})

def build_structure(params):
    return post("/api/structure/build", params)

def set_camera(x, y, z, yaw, pitch):
    post("/api/camera", {"x": x, "y": y, "z": z, "yaw": yaw, "pitch": pitch})

def track_npc(name, duration_s, interval_s=2.0):
    """Track NPC positions over time. Returns list of (x,y,z) tuples."""
    positions = []
    steps = int(duration_s / interval_s)
    for i in range(steps):
        time.sleep(interval_s)
        try:
            pos = get_npc(name)
            if pos:
                positions.append(pos)
                print(f"    t={i*interval_s:.0f}s: ({pos[0]:.1f}, {pos[1]:.2f}, {pos[2]:.1f})")
        except Exception as e:
            print(f"    t={i*interval_s:.0f}s: ERROR — {e}")
    return positions

results = []

# ============================================================================
# TEST 1: NPC walks over subcube-height obstacle (1/3 block = 0.333)
# ============================================================================
print("=" * 60)
print("TEST 1: NPC walks over subcube-height obstacle (1/3 block)")
print("=" * 60)
print()

# The Blacksmith patrols (10,17,12)->(10,17,20)->(14,17,20)->(14,17,12)
# Place a line of subcubes across z=15 from x=8 to x=16.
# Each subcube is 1/3 block tall, sitting on the ground at y=17 (surface=16, NPC walks at y=17).
# We place subcubes at (x, 16, 15) with subcubePos (sx, 2, 1) — top subcube row of y=16 block
# This creates a 1/3-block-tall ridge at the surface level.

# Clear any previous test artifacts
clear_region(4, 17, 13, 20, 20, 18)
time.sleep(0.5)

print("  Placing subcube obstacle line at z=15, x=8-16...")
# Place subcubes on top of the ground. Ground is solid at y=16. NPC walks on top (y=17).
# A subcube at (x, 17, 15) with sy=0 (bottom third of cube y=17) creates a 1/3-block bump.
for x in range(8, 17):
    for sx in range(3):  # All 3 x-subdivisions per cube to cover the full cube width
        place_subcube(x, 17, 15, sx, 0, 1, "Wood")
time.sleep(1)

set_camera(12, 25, 10, -90, -45)
time.sleep(0.5)
screenshot("subcube obstacle placed")

print("  Tracking Blacksmith (should walk over subcube step)...")
positions = track_npc("Blacksmith", 25, 2.5)

if positions:
    y_vals = [p[1] for p in positions]
    z_vals = [p[2] for p in positions]
    crossed = any(z < 14.5 for z in z_vals) and any(z > 15.5 for z in z_vals)
    total_dist = sum(
        ((positions[i][0]-positions[i-1][0])**2 + (positions[i][2]-positions[i-1][2])**2)**0.5
        for i in range(1, len(positions))
    )
    print(f"\n  Y range: {min(y_vals):.2f} to {max(y_vals):.2f}")
    print(f"  Z range: {min(z_vals):.1f} to {max(z_vals):.1f}")
    print(f"  Total XZ distance: {total_dist:.1f}")
    print(f"  Crossed obstacle z=15: {'YES' if crossed else 'NO'}")
    
    if total_dist > 5.0 and crossed:
        print("  RESULT: PASS — NPC stepped over subcube obstacle")
        results.append(("Subcube step-over (1/3 block)", "PASS"))
    elif total_dist > 5.0:
        print("  RESULT: PARTIAL — NPC moved but went around")
        results.append(("Subcube step-over (1/3 block)", "PARTIAL"))
    else:
        print("  RESULT: FAIL — NPC stuck at subcube obstacle")
        results.append(("Subcube step-over (1/3 block)", "FAIL"))
else:
    print("  RESULT: FAIL — No NPC positions tracked")
    results.append(("Subcube step-over (1/3 block)", "FAIL"))

# Clean up
clear_region(8, 17, 15, 16, 17, 15)
time.sleep(0.5)

# ============================================================================
# TEST 2: NPC walks over subcube staircase (built via structure API)
# ============================================================================
print()
print("=" * 60)
print("TEST 2: NPC walks over subcube staircase")
print("=" * 60)
print()

# Build a subcube staircase across the patrol path.
# The staircase at (10, 16, 13), height=1, width=5, facing north.
# This creates 3 subcube steps going up 1 block total over z=13.
print("  Building subcube staircase at (10,17,13) h=1 w=5 facing south...")
resp = build_structure({
    "type": "subcube_staircase",
    "position": {"x": 10, "y": 17, "z": 13},
    "height": 1,
    "width": 5,
    "facing": "south",
    "material": "Wood"
})
print(f"  Structure result: placed={resp.get('placed', '?')}, failed={resp.get('failed', '?')}")
time.sleep(1)

set_camera(12, 25, 10, -90, -40)
time.sleep(0.5)
screenshot("subcube staircase built")

print("  Tracking Blacksmith (should traverse subcube stairs)...")
positions = track_npc("Blacksmith", 30, 2.5)

if positions:
    y_vals = [p[1] for p in positions]
    z_vals = [p[2] for p in positions]
    y_max = max(y_vals)
    crossed_stair = any(z < 13.0 for z in z_vals) and any(z > 13.5 for z in z_vals)
    total_dist = sum(
        ((positions[i][0]-positions[i-1][0])**2 + (positions[i][2]-positions[i-1][2])**2)**0.5
        for i in range(1, len(positions))
    )
    print(f"\n  Y range: {min(y_vals):.2f} to {y_max:.2f}")
    print(f"  Z range: {min(z_vals):.1f} to {max(z_vals):.1f}")
    print(f"  Total XZ distance: {total_dist:.1f}")
    print(f"  Crossed staircase area: {'YES' if crossed_stair else 'NO'}")
    
    if total_dist > 5.0 and crossed_stair:
        print("  RESULT: PASS — NPC traversed subcube staircase")
        results.append(("Subcube staircase", "PASS"))
    elif total_dist > 5.0:
        print("  RESULT: PARTIAL — NPC moved but avoided stairs")
        results.append(("Subcube staircase", "PARTIAL"))
    else:
        print("  RESULT: FAIL — NPC stuck")
        results.append(("Subcube staircase", "FAIL"))
else:
    print("  RESULT: FAIL — No NPC positions tracked")
    results.append(("Subcube staircase", "FAIL"))

screenshot("staircase test done")

# Clean up stairs
clear_region(10, 17, 13, 15, 18, 13)
time.sleep(0.5)

# ============================================================================
# TEST 3: NPC CANNOT walk over full-block obstacle (should go around)
# ============================================================================
print()
print("=" * 60)
print("TEST 3: NPC blocked by full-block obstacle (should go around)")
print("=" * 60)
print()

# Place a 1-block-high wall across the patrol path.
# The NPC should NOT be able to step over this — it should path around it.
print("  Placing full-block wall at z=16, x=8-16, y=17-18 (2 blocks tall)...")
fill_region(8, 17, 16, 16, 18, 16, "Stone")
time.sleep(1)

set_camera(12, 25, 10, -90, -45)
time.sleep(0.5)
screenshot("full-block wall placed")

print("  Tracking Blacksmith (should NOT step over, should go around)...")
positions = track_npc("Blacksmith", 35, 2.5)

if positions:
    y_vals = [p[1] for p in positions]
    z_vals = [p[2] for p in positions]
    total_dist = sum(
        ((positions[i][0]-positions[i-1][0])**2 + (positions[i][2]-positions[i-1][2])**2)**0.5
        for i in range(1, len(positions))
    )
    # The NPC should still move (going around), so distance > 5
    # But it should NOT have Y noticeably above 17 (it didn't climb the block)
    max_y = max(y_vals)
    went_above = max_y > 17.5
    
    print(f"\n  Y range: {min(y_vals):.2f} to {max_y:.2f}")
    print(f"  Z range: {min(z_vals):.1f} to {max(z_vals):.1f}")
    print(f"  Total XZ distance: {total_dist:.1f}")
    print(f"  Went above y=17.5 (climbed block): {'YES — BAD' if went_above else 'NO — GOOD'}")
    
    if not went_above and total_dist > 5.0:
        print("  RESULT: PASS — NPC correctly blocked by full block, navigated around")
        results.append(("Full-block blocked (should not step)", "PASS"))
    elif went_above:
        print("  RESULT: FAIL — NPC climbed full block (should not be able to)")
        results.append(("Full-block blocked (should not step)", "FAIL"))
    else:
        print("  RESULT: PARTIAL — NPC may be stuck")
        results.append(("Full-block blocked (should not step)", "PARTIAL"))
else:
    print("  RESULT: FAIL — No NPC positions tracked")
    results.append(("Full-block blocked (should not step)", "FAIL"))

# Clean up
clear_region(8, 17, 16, 16, 17, 16)
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
with open("npc_subcube_step_results.txt", "w") as f:
    for name, result in results:
        f.write(f"{result}: {name}\n")
    f.write(f"\nPositions from last test:\n")
    if positions:
        for i, pos in enumerate(positions):
            f.write(f"  t={i*2.5:.0f}s: ({pos[0]:.1f}, {pos[1]:.2f}, {pos[2]:.1f})\n")

print("\nResults written to npc_subcube_step_results.txt")
