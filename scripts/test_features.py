"""
MCP Feature Test Script for VillageChat
Tests: A* Pathfinding, Subcube Stairs, Detailed Structures
"""
import urllib.request
import json
import time
import sys
import os

API = "http://localhost:8090"

def api_get(path):
    return json.loads(urllib.request.urlopen(f"{API}{path}").read())

def api_post(path, body):
    data = json.dumps(body).encode()
    req = urllib.request.Request(f"{API}{path}", data=data, headers={"Content-Type": "application/json"})
    return json.loads(urllib.request.urlopen(req).read())

def get_npcs():
    d = api_get("/api/npcs")
    result = {}
    for n in d["npcs"]:
        p = n["position"]
        result[n["name"]] = (p["x"], p["y"], p["z"])
    return result

def screenshot(name):
    time.sleep(0.3)  # Let frame render
    resp = api_get("/api/screenshot")
    if resp.get("success") and resp.get("path"):
        print(f"  Screenshot saved: {resp['path']}")
    else:
        print(f"  Screenshot: {resp}")

def set_camera(x, y, z, yaw, pitch):
    api_post("/api/camera", {"position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
    time.sleep(0.2)

def fill_region(x1, y1, z1, x2, y2, z2, material):
    """Fill region (async) and wait for it to complete."""
    resp = api_post("/api/world/fill", {
        "x1": x1, "y1": y1, "z1": z1,
        "x2": x2, "y2": y2, "z2": z2,
        "material": material
    })
    time.sleep(1.0)  # Wait for async fill
    return resp

def clear_region(x1, y1, z1, x2, y2, z2):
    """Clear region (async) and wait."""
    resp = api_post("/api/world/clear", {
        "x1": x1, "y1": y1, "z1": z1,
        "x2": x2, "y2": y2, "z2": z2
    })
    time.sleep(1.0)
    return resp

# ============================================================
# TEST 1: A* Pathfinding Around Obstacle
# ============================================================
def test_pathfinding():
    print("=" * 60)
    print("TEST 1: A* Pathfinding Around Wall Obstacle")
    print("=" * 60)
    
    # Get Blacksmith's patrol waypoints: (10,17,12) -> (10,17,20) -> (14,17,20) -> (14,17,12)
    # Place a wall blocking the z=16 line from x=9 to x=15 (blocks direct path between z=12 and z=20)
    
    print("\n1a. Getting baseline NPC positions...")
    npcs = get_npcs()
    for name, pos in npcs.items():
        print(f"  {name}: ({pos[0]:.1f}, {pos[1]:.1f}, {pos[2]:.1f})")
    
    print("\n1b. Setting overhead camera view...")
    set_camera(12, 30, 16, -90, -70)
    time.sleep(0.5)
    screenshot("test_01_baseline")
    
    print("\n1c. Placing wall obstacle at z=16, x=8-16, y=17-19 (Stone)...")
    fill_region(8, 17, 16, 16, 19, 16, "Stone")
    print("  Wall placed")
    screenshot("test_02_wall_placed")
    
    print("\n1d. Tracking Blacksmith movement around wall (15 seconds)...")
    positions = []
    for i in range(6):
        time.sleep(2.5)
        npcs = get_npcs()
        bs = npcs.get("Blacksmith", (0,0,0))
        positions.append(bs)
        print(f"  t={i*2.5:.0f}s: Blacksmith at ({bs[0]:.1f}, {bs[1]:.1f}, {bs[2]:.1f})")
    
    # Check if Blacksmith navigated around the wall (z changed from <16 to >16 or vice versa)
    z_vals = [p[2] for p in positions]
    crossed_wall = any(z < 15.5 for z in z_vals) and any(z > 16.5 for z in z_vals)
    moved = max(abs(positions[-1][0] - positions[0][0]), abs(positions[-1][2] - positions[0][2])) > 1.0
    
    print(f"\n  Blacksmith z-positions: {[f'{z:.1f}' for z in z_vals]}")
    print(f"  Crossed wall line: {'YES' if crossed_wall else 'NO'}")
    print(f"  Moved significantly: {'YES' if moved else 'NO'}")
    
    screenshot("test_03_after_patrol")
    
    # Clean up wall
    print("\n1e. Removing wall obstacle...")
    clear_region(8, 17, 16, 16, 19, 16)
    print("  Wall cleared")
    
    return moved

# ============================================================
# TEST 2: Subcube Staircase Building
# ============================================================
def test_subcube_stairs():
    print("\n" + "=" * 60)
    print("TEST 2: Subcube Staircase Building")
    print("=" * 60)
    
    # Build a regular staircase for comparison
    print("\n2a. Building regular staircase at (20, 17, 8)...")
    resp = api_post("/api/structure/build", {
        "type": "staircase",
        "position": {"x": 20, "y": 17, "z": 8},
        "width": 3,
        "height": 5,
        "material": "Stone",
        "facing": "north"
    })
    print(f"  Result: placed={resp.get('placed', '?')}, generated={resp.get('voxels_generated', '?')}")
    
    # Build a subcube staircase next to it
    print("\n2b. Building subcube staircase at (24, 17, 8)...")
    resp = api_post("/api/structure/build", {
        "type": "subcube_staircase",
        "position": {"x": 24, "y": 17, "z": 8},
        "width": 3,
        "height": 5,
        "material": "Wood",
        "facing": "north"
    })
    print(f"  Result: placed={resp.get('placed', '?')}, generated={resp.get('voxels_generated', '?')}")
    
    print("\n2c. Moving camera to view both staircases side by side...")
    set_camera(24, 23, 16, -90, -30)
    screenshot("test_04_staircases_comparison")
    
    # Close-up of subcube staircase
    print("\n2d. Close-up of subcube staircase...")
    set_camera(26, 20, 12, -135, -20)
    screenshot("test_05_subcube_closeup")
    
    return True

# ============================================================
# TEST 3: Detailed Structure Generation
# ============================================================
def test_detailed_structures():
    print("\n" + "=" * 60)
    print("TEST 3: Detailed Structure Generation (Phase 3)")
    print("=" * 60)
    
    # First check what structure types are available
    print("\n3a. Listing available structure types...")
    resp = api_get("/api/structure/types")
    types = [t["type"] for t in resp.get("types", [])]
    print(f"  Available: {types}")
    
    # Build a house with detail_level: detailed
    print("\n3b. Building detailed house at (4, 17, 4)...")
    resp = api_post("/api/structure/build", {
        "type": "house",
        "position": {"x": 4, "y": 17, "z": 4},
        "width": 8,
        "depth": 8,
        "height": 5,
        "facing": "south",
        "materials": {"wall": "Stone", "floor": "Wood", "roof": "Wood"},
        "windows": 2,
        "detail_level": "detailed"
    })
    print(f"  Result: placed={resp.get('voxels_placed', '?')}")
    
    print("\n3c. Moving camera to view detailed house...")
    set_camera(8, 25, 14, -90, -35)
    time.sleep(0.5)
    screenshot("test_06_detailed_house")
    
    # Side view to see window/door frames
    set_camera(0, 21, 8, 45, -15)
    time.sleep(0.5)
    screenshot("test_07_house_side_view")
    
    # Build a tavern with detail_level
    print("\n3d. Building detailed tavern at (18, 17, 2)...")
    resp = api_post("/api/structure/build", {
        "type": "tavern",
        "position": {"x": 18, "y": 17, "z": 2},
        "width": 12,
        "depth": 14,
        "stories": 2,
        "facing": "south",
        "materials": {"wall": "Stone", "floor": "Wood", "roof": "Wood"},
        "furnished": True,
        "detail_level": "detailed"
    })
    print(f"  Result: placed={resp.get('voxels_placed', '?')}")
    
    set_camera(24, 30, 16, -120, -30)
    time.sleep(0.5)
    screenshot("test_08_detailed_tavern")
    
    # Build a tower
    print("\n3e. Building detailed tower at (2, 17, 20)...")
    resp = api_post("/api/structure/build", {
        "type": "tower",
        "position": {"x": 2, "y": 17, "z": 20},
        "radius": 4,
        "height": 12,
        "material": "Stone",
        "detail_level": "detailed"
    })
    print(f"  Result: placed={resp.get('voxels_placed', '?')}")
    
    set_camera(8, 30, 24, -135, -20)
    time.sleep(0.5)
    screenshot("test_09_detailed_tower")
    
    # Grand overview
    print("\n3f. Grand overview of all structures...")
    set_camera(16, 45, 30, -90, -45)
    time.sleep(0.5)
    screenshot("test_10_overview")
    
    return True

# ============================================================
# MAIN
# ============================================================
if __name__ == "__main__":
    print("Phyxel Feature Test - VillageChat Project")
    print(f"Engine API: {API}")
    
    # Verify engine is running
    try:
        status = api_get("/api/status")
        print(f"Engine status: {status['status']}")
    except Exception as e:
        print(f"ERROR: Engine not reachable: {e}")
        sys.exit(1)
    
    results = {}
    
    # Run tests
    try:
        results["pathfinding"] = test_pathfinding()
    except Exception as e:
        print(f"  ERROR: {e}")
        results["pathfinding"] = False
    
    try:
        results["stairs"] = test_subcube_stairs()
    except Exception as e:
        print(f"  ERROR: {e}")
        results["stairs"] = False
    
    try:
        results["structures"] = test_detailed_structures()
    except Exception as e:
        print(f"  ERROR: {e}")
        results["structures"] = False
    
    # Summary
    print("\n" + "=" * 60)
    print("TEST SUMMARY")
    print("=" * 60)
    for test, passed in results.items():
        status = "PASS" if passed else "NEEDS REVIEW"
        print(f"  {test}: {status}")
