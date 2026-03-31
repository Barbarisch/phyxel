"""NPC Pathfinding Wall Avoidance Test - writes to file"""
import urllib.request, json, time

API = "http://localhost:8090"
OUT = r"G:\Github\phyxel\npc_check.txt"

def get(path):
    return json.loads(urllib.request.urlopen(f"{API}{path}").read())

def post(path, body):
    data = json.dumps(body).encode()
    req = urllib.request.Request(f"{API}{path}", data=data, headers={"Content-Type": "application/json"})
    return json.loads(urllib.request.urlopen(req).read())

lines = []

# Get initial position
resp = get("/api/npcs")
for n in resp["npcs"]:
    p = n["position"]
    lines.append(f"BEFORE WALL: {n['name']}: ({p['x']:.1f}, {p['y']:.1f}, {p['z']:.1f})")

# Place wall at z=16, x=8-16, y=17-19
resp = post("/api/world/fill", {"x1":8, "y1":17, "z1":16, "x2":16, "y2":19, "z2":16, "material":"Stone"})
lines.append(f"Wall placed: {resp.get('placed', '?')} voxels")
time.sleep(1.0)

# Track Blacksmith for 30 seconds (longer to allow for detour)
positions = []
for i in range(12):
    time.sleep(2.5)
    resp = get("/api/npcs")
    for n in resp["npcs"]:
        if n["name"] == "Blacksmith":
            p = n["position"]
            positions.append((p['x'], p['y'], p['z']))
            lines.append(f"t={i*2.5:.0f}s BS: ({p['x']:.1f}, {p['y']:.1f}, {p['z']:.1f})")

# Analyze movement
if len(positions) >= 2:
    # Check total distance traveled
    total_dist = 0.0
    for i in range(1, len(positions)):
        dx = positions[i][0] - positions[i-1][0]
        dz = positions[i][2] - positions[i-1][2]
        total_dist += (dx*dx + dz*dz)**0.5
    
    # Check if NPC is stuck (barely moved from first reading)
    first_last_dist = ((positions[-1][0]-positions[0][0])**2 + (positions[-1][2]-positions[0][2])**2)**0.5
    
    # Check unique positions (count distinct cells)
    cells = set()
    for x, y, z in positions:
        cells.add((int(x), int(z)))
    
    x_vals = [p[0] for p in positions]
    z_vals = [p[2] for p in positions]
    
    lines.append(f"Total distance traveled: {total_dist:.1f}")
    lines.append(f"First-to-last distance: {first_last_dist:.1f}")
    lines.append(f"Unique cells visited: {len(cells)}")
    lines.append(f"X range: {min(x_vals):.1f} to {max(x_vals):.1f}")
    lines.append(f"Z range: {min(z_vals):.1f} to {max(z_vals):.1f}")
    
    stuck = total_dist < 2.0
    lines.append(f"STUCK: {'YES - FAIL' if stuck else 'NO - PASS'}")
    lines.append(f"NPC IS NAVIGATING: {'YES' if total_dist > 5.0 else 'BARELY' if total_dist > 2.0 else 'NO'}")
else:
    lines.append("Not enough position data")

# Remove wall
post("/api/world/clear", {"x1":8, "y1":17, "z1":16, "x2":16, "y2":19, "z2":16})
lines.append("Wall removed")

with open(OUT, "w") as f:
    f.write("\n".join(lines))
print(f"Done - output in {OUT}")
for line in lines:
    print(line)
