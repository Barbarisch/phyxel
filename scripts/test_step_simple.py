"""
Minimal step-up test. Loads a flat world with one NPC walking z=10 to z=25.
Places a single subcube bump at z=17 and watches if the NPC crosses it.
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
    for n in get("/api/npcs")["npcs"]:
        if n["name"] == name:
            p = n["position"]
            return p["x"], p["y"], p["z"]
    return None

# Step 1: Load the test world
print("Loading step_test game definition...")
with open("samples/game_definitions/step_test.json") as f:
    game_def = json.load(f)
resp = post("/api/game/load_definition", game_def)
print(f"  Load result: {resp}")
# It's async, wait for it to complete
async_id = resp.get("async_id", "")
if async_id:
    for _ in range(20):
        time.sleep(1)
        try:
            status = get(f"/api/jobs/{async_id}")
            print(f"  Job status: {status.get('status', '?')}")
            if status.get("status") in ("completed", "failed"):
                break
        except:
            pass
time.sleep(2)

# Step 2: Verify NPC exists and is moving
print("\nChecking NPC exists...")
pos = get_npc("TestWalker")
if not pos:
    print("  FAIL: TestWalker NPC not found!")
    sys.exit(1)
print(f"  TestWalker at: x={pos[0]:.2f} y={pos[1]:.2f} z={pos[2]:.2f}")

# Watch for 5s to confirm NPC is moving
print("\nTest 0: Confirm NPC moves on flat ground (5s)...")
positions = []
for i in range(6):
    time.sleep(1)
    p = get_npc("TestWalker")
    if p:
        positions.append(p)
        print(f"  t={i+1}s: x={p[0]:.2f} y={p[1]:.2f} z={p[2]:.2f}")

if len(positions) >= 2:
    total_dist = sum(
        abs(positions[i][2] - positions[i-1][2])
        for i in range(1, len(positions))
    )
    print(f"  Z distance traveled: {total_dist:.2f}")
    if total_dist < 1.0:
        print("  FAIL: NPC not moving on flat ground!")
        sys.exit(1)
    print("  PASS: NPC is moving")

# Step 3: Place one subcube bump at z=17 across x=14-18
# Ground is solid at y<=16 (Flat world). NPC walks on y=17.
# Subcube at (x, 17, 17) with sy=0 = bottom third of empty cube y=17 = 0.333 block bump.
print("\nPlacing subcube bump at z=17, x=14-18...")
for x in range(14, 19):
    for sx in range(3):
        resp = post("/api/world/subcube", {"x": x, "y": 17, "z": 17, "sx": sx, "sy": 0, "sz": 1, "material": "Wood"})
# Take screenshot to see the bump
time.sleep(1)
resp = get("/api/screenshot")
print(f"  Screenshot: {resp.get('path', '?')}")

# Step 4: Watch NPC for 20s — does it cross z=17?
print("\nTest 1: NPC walks over subcube bump (20s)...")
positions = []
for i in range(20):
    time.sleep(1)
    p = get_npc("TestWalker")
    if p:
        positions.append(p)
        if i % 3 == 0:
            print(f"  t={i+1}s: x={p[0]:.2f} y={p[1]:.2f} z={p[2]:.2f}")

if positions:
    z_vals = [p[2] for p in positions]
    y_vals = [p[1] for p in positions]
    crossed = any(z < 16.0 for z in z_vals) and any(z > 18.0 for z in z_vals)
    print(f"\n  Z range: {min(z_vals):.2f} to {max(z_vals):.2f}")
    print(f"  Y range: {min(y_vals):.2f} to {max(y_vals):.2f}")
    print(f"  Crossed z=17 bump: {'YES' if crossed else 'NO'}")
    if crossed:
        print("  RESULT: PASS")
    else:
        print("  RESULT: FAIL — NPC did not cross subcube bump")
else:
    print("  RESULT: FAIL — No positions tracked")

# Take final screenshot
resp = get("/api/screenshot")
print(f"  Final screenshot: {resp.get('path', '?')}")
