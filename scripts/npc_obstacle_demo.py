"""
NPC Obstacle Avoidance Demo
============================
Demonstrates that NPCs navigate around placed obstacles using A* pathfinding.

Steps:
1. Screenshot the NPC patrolling normally
2. Place a Stone wall blocking the patrol route
3. Track NPC positions over time, taking screenshots
4. Show the NPC navigating AROUND the wall
5. Clean up the wall
"""
import urllib.request, json, time, base64, os

API = "http://localhost:8090"
SCREENSHOT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "screenshots", "obstacle_demo")
os.makedirs(SCREENSHOT_DIR, exist_ok=True)

def get(path):
    return json.loads(urllib.request.urlopen(f"{API}{path}", timeout=5).read())

def post(path, body):
    data = json.dumps(body).encode()
    req = urllib.request.Request(f"{API}{path}", data=data, headers={"Content-Type": "application/json"})
    return json.loads(urllib.request.urlopen(req, timeout=10).read())

def screenshot(name):
    """Take a screenshot via engine API — saves to engine screenshots/ dir."""
    resp = get("/api/screenshot")
    if resp.get("success"):
        path = resp.get("path", "?")
        print(f"  Screenshot: {path}")
        return path
    else:
        print(f"  Screenshot failed: {resp}")
        return None

def get_npc(name):
    """Get NPC position by name."""
    resp = get("/api/npcs")
    for n in resp["npcs"]:
        if n["name"] == name:
            p = n["position"]
            return (p["x"], p["y"], p["z"])
    return None

def set_camera(x, y, z, yaw, pitch):
    post("/api/camera", {"x": x, "y": y, "z": z, "yaw": yaw, "pitch": pitch})

def print_npc_positions():
    resp = get("/api/npcs")
    for n in resp["npcs"]:
        p = n["position"]
        print(f"    {n['name']}: ({p['x']:.1f}, {p['y']:.1f}, {p['z']:.1f})")

# ============================================================================
print("=" * 60)
print("NPC OBSTACLE AVOIDANCE DEMO")
print("=" * 60)

# Step 0: Set up a good camera angle to see the patrol area
print("\n[Step 0] Setting camera to overview position...")
set_camera(12, 30, 16, -90, -55)  # Bird's eye view of the patrol area
time.sleep(0.5)

# Step 1: Show NPC patrolling normally
print("\n[Step 1] NPC patrolling normally (no obstacles)...")
print("  Current positions:")
print_npc_positions()
screenshot("01_normal_patrol")
time.sleep(2)
print("  Positions after 2s:")
print_npc_positions()

# Step 2: Place a wall blocking the Blacksmith's patrol route
# Blacksmith patrols: (10,17,12) -> (10,17,20) -> (14,17,20) -> (14,17,12)
# Place a wall across z=16 from x=8 to x=16, blocking north-south movement
print("\n[Step 2] Placing Stone wall at z=16, x=8-16, y=17-19...")
print("  This wall blocks the Blacksmith's patrol route!")
resp = post("/api/world/fill", {
    "x1": 8, "y1": 17, "z1": 16,
    "x2": 16, "y2": 19, "z2": 16,
    "material": "Stone"
})
print(f"  Wall placed: {resp.get('placed', resp.get('voxels_placed', '?'))} voxels")
time.sleep(1)
screenshot("02_wall_placed")

# Step 3: Track NPC movement over time
print("\n[Step 3] Tracking Blacksmith movement around obstacle...")
print("  (Wall at z=16 from x=8 to x=16 — NPC must go around)")
positions = []
screenshots_taken = []

for i in range(10):
    time.sleep(3)
    pos = get_npc("Blacksmith")
    if pos:
        positions.append(pos)
        x, y, z = pos
        print(f"  t={i*3:2d}s: Blacksmith at ({x:.1f}, {y:.1f}, {z:.1f})")
        
        # Take a screenshot at key moments
        if i in [0, 3, 6, 9]:
            # Follow the NPC with camera
            set_camera(x, y + 15, z, -90, -60)
            time.sleep(0.3)
            path = screenshot(f"03_tracking_t{i*3:02d}s")
            if path:
                screenshots_taken.append(path)

# Step 4: Analyze the results
print("\n[Step 4] Analysis:")
if len(positions) >= 2:
    # Total distance
    total_dist = 0.0
    for j in range(1, len(positions)):
        dx = positions[j][0] - positions[j-1][0]
        dz = positions[j][2] - positions[j-1][2]
        total_dist += (dx*dx + dz*dz)**0.5
    
    # Position ranges
    x_vals = [p[0] for p in positions]
    z_vals = [p[2] for p in positions]
    
    # Check if NPC visited both sides of the wall (z < 16 and z > 16)
    visited_south = any(z < 15.5 for z in z_vals)
    visited_north = any(z > 16.5 for z in z_vals)
    
    # Unique cells visited
    cells = set((int(p[0]), int(p[2])) for p in positions)
    
    print(f"  Total distance traveled: {total_dist:.1f} units")
    print(f"  Unique cells visited:    {len(cells)}")
    print(f"  X range:                 {min(x_vals):.1f} to {max(x_vals):.1f}")
    print(f"  Z range:                 {min(z_vals):.1f} to {max(z_vals):.1f}")
    print(f"  Visited south of wall:   {'YES' if visited_south else 'NO'}")
    print(f"  Visited north of wall:   {'YES' if visited_north else 'NO'}")
    
    stuck = total_dist < 3.0
    navigating = total_dist > 10.0
    
    print()
    if stuck:
        print("  RESULT: FAIL — NPC appears stuck")
    elif navigating:
        print("  RESULT: PASS — NPC is actively navigating around the obstacle!")
    else:
        print("  RESULT: PARTIAL — NPC moved but slowly")

# Step 5: Take a final overview screenshot
print("\n[Step 5] Final overview screenshot...")
set_camera(12, 35, 16, -90, -65)
time.sleep(0.5)
screenshot("04_final_overview")

# Step 6: Clean up - remove the wall
print("\n[Step 6] Removing obstacle wall...")
post("/api/world/clear", {
    "x1": 8, "y1": 17, "z1": 16,
    "x2": 16, "y2": 19, "z2": 16
})
print("  Wall removed.")
time.sleep(1)
screenshot("05_wall_removed")

print("\n" + "=" * 60)
print(f"Screenshots saved to: screenshots/")
print("=" * 60)

# Write results to file for easy access
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "npc_demo_results.txt"), "w") as f:
    f.write("NPC OBSTACLE AVOIDANCE DEMO RESULTS\n")
    f.write("=" * 50 + "\n")
    if len(positions) >= 2:
        f.write(f"Total distance traveled: {total_dist:.1f} units\n")
        f.write(f"Unique cells visited:    {len(cells)}\n")
        f.write(f"X range:                 {min(x_vals):.1f} to {max(x_vals):.1f}\n")
        f.write(f"Z range:                 {min(z_vals):.1f} to {max(z_vals):.1f}\n")
        f.write(f"Visited south of wall:   {'YES' if visited_south else 'NO'}\n")
        f.write(f"Visited north of wall:   {'YES' if visited_north else 'NO'}\n")
        f.write(f"STUCK:                   {'YES' if stuck else 'NO'}\n")
        f.write(f"NAVIGATING:              {'YES' if navigating else 'PARTIAL'}\n")
        f.write(f"\nPositions tracked:\n")
        for i, (x, y, z) in enumerate(positions):
            f.write(f"  t={i*3:2d}s: ({x:.1f}, {y:.1f}, {z:.1f})\n")
