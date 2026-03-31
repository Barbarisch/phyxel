"""Inspect the world: NPC positions + voxel terrain scan. Then clear blockers and test movement."""
import urllib.request, json, time
from collections import defaultdict

API = "http://localhost:8090"

def get(path):
    return json.loads(urllib.request.urlopen(f"{API}{path}", timeout=5).read())

def post(path, body):
    data = json.dumps(body).encode()
    req = urllib.request.Request(f"{API}{path}", data=data, headers={"Content-Type": "application/json"})
    return json.loads(urllib.request.urlopen(req, timeout=10).read())

# 1. NPC positions
npcs = get("/api/npcs")
print("=== NPCs ===")
for n in npcs["npcs"]:
    p = n["position"]
    print(f"  {n['name']}: ({p['x']:.1f}, {p['y']:.1f}, {p['z']:.1f}) behavior={n.get('behavior','?')}")

# 2. Scan around NPC area
print("\n=== Voxel scan: x=10-20, z=5-30, y=14-20 ===")
scan = get("/api/world/scan?x1=10&y1=14&z1=5&x2=20&y2=20&z2=30")
voxels = scan.get("voxels", [])
print(f"Total voxels in scan: {len(voxels)}")

by_y = defaultdict(list)
for v in voxels:
    by_y[v["y"]].append(v)
for y in sorted(by_y.keys()):
    count = len(by_y[y])
    zs = sorted(set(v["z"] for v in by_y[y]))
    xs = sorted(set(v["x"] for v in by_y[y]))
    print(f"  y={y}: {count} voxels, x=[{min(xs)}-{max(xs)}], z=[{min(zs)}-{max(zs)}]")

# Show any voxels above ground (y>=17)
above_ground = [v for v in voxels if v["y"] >= 17]
if above_ground:
    print(f"\n=== Above-ground voxels (y>=17): {len(above_ground)} ===")
    for v in above_ground:
        print(f"  ({v['x']}, {v['y']}, {v['z']}) mat={v.get('material','?')}")

# 3. Clear above-ground blockers
if above_ground:
    print("\nClearing above-ground voxels...")
    resp = post("/api/world/clear", {"x1":10, "y1":17, "z1":5, "x2":20, "y2":20, "z2":30})
    print(f"  Cleared: {resp}")
    time.sleep(1)

# 4. Watch TestWalker for 10s on clear ground
print("\n=== Baseline: TestWalker on flat ground (10s) ===")
for t in range(11):
    if t > 0:
        time.sleep(1)
    npcs = get("/api/npcs")
    for n in npcs["npcs"]:
        if n["name"] == "TestWalker":
            p = n["position"]
            print(f"  t={t}s: ({p['x']:.2f}, {p['y']:.2f}, {p['z']:.2f})")

# 5. Now place subcube bump at z=17 and test
print("\n=== Placing subcube bump at z=17, x=14-18 ===")
for x in range(14, 19):
    for sx in range(3):
        post("/api/world/subcube", {"x": x, "y": 17, "z": 17, "sx": sx, "sy": 0, "sz": 1, "material": "Wood"})
time.sleep(1)

# Verify bump exists
scan2 = get("/api/world/scan?x1=14&y1=17&z1=17&x2=18&y2=17&z2=17")
print(f"  Voxels at bump location: {len(scan2.get('voxels', []))}")

# Screenshot
resp = get("/api/screenshot")
print(f"  Screenshot: {resp.get('path', '?')}")

# 6. Watch TestWalker for 20s - can it cross z=17?
print("\n=== Test: Can TestWalker step over subcube bump? (20s) ===")
positions = []
for t in range(21):
    if t > 0:
        time.sleep(1)
    npcs = get("/api/npcs")
    for n in npcs["npcs"]:
        if n["name"] == "TestWalker":
            p = n["position"]
            positions.append((p["x"], p["y"], p["z"]))
            if t % 3 == 0:
                print(f"  t={t}s: ({p['x']:.2f}, {p['y']:.2f}, {p['z']:.2f})")

if positions:
    z_vals = [p[2] for p in positions]
    y_vals = [p[1] for p in positions]
    crossed = any(z < 16.0 for z in z_vals) and any(z > 18.0 for z in z_vals)
    print(f"\n  Z range: {min(z_vals):.2f} to {max(z_vals):.2f}")
    print(f"  Y range: {min(y_vals):.2f} to {max(y_vals):.2f}")
    print(f"  Crossed z=17: {'YES' if crossed else 'NO'}")
    if crossed:
        print("  RESULT: PASS")
    else:
        print("  RESULT: FAIL")

