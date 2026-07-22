"""Narrow-door differential matrix (Phase B part 2).

Wall at z=138 with doorways of graduated HEIGHT; each race patrols straight
at its assigned doorway from z=142 toward z=134. Crossing to z<136.5 = PASS,
staying on the near side = BLOCKED. Honest collision means the goblin fits
where the ogre cannot, and nobody passes the solid wall.

Capsule heights (resizeController logs): goblin 1.33, standard 2.12, ogre 2.96.
Doors: A = 2 tall (x133), B = 3 tall (x141), C = 4 tall (x149), solid x=155.
"""
import json
import time
import urllib.request

BASE = "http://localhost:8090"

RACES = {
    "goblin":   {"appearance": {"preset": "goblin"}},
    "standard": {},
    "ogre":     {"animFile": "resources/animated_characters/ogre.anim",
                 "appearance": {"preset": "ogre"}},
}
DOORS = {"A_h2": 133.0, "B_h3": 141.0, "C_h4": 149.0, "solid": 155.0}
# capsule height -> expected outcome per door height
EXPECT = {
    "goblin":   {"A_h2": "PASS",  "B_h3": "PASS", "C_h4": "PASS", "solid": "BLOCK"},
    "standard": {"A_h2": "BLOCK", "B_h3": "PASS", "C_h4": "PASS", "solid": "BLOCK"},
    "ogre":     {"A_h2": "BLOCK", "B_h3": "?",    "C_h4": "PASS", "solid": "BLOCK"},
}


def post(path, body, timeout=90):
    req = urllib.request.Request(BASE + path, json.dumps(body).encode(),
                                 {"Content-Type": "application/json"})
    try:
        return json.load(urllib.request.urlopen(req, timeout=timeout))
    except Exception:
        return {}


def zpos(name):
    r = json.load(urllib.request.urlopen(BASE + "/api/npcs", timeout=30))
    for n in r["npcs"]:
        if n["name"] == name:
            return n["position"]["z"]
    return None


# One race per wave — simultaneous NPCs in a shared lane block each other
# and contaminate the measurement (learned the hard way).
min_z = {}
mismatches = []
for race, spec in RACES.items():
    for door, cx in DOORS.items():
        name = f"DM_{race}_{door}"
        body = {"name": name, "position": {"x": cx, "y": 18, "z": 142}}
        body.update(spec)
        post("/api/npc/spawn", body)
        time.sleep(1.5)
        post("/api/npc/behavior", {
            "name": name, "behavior": "patrol",
            "waypoints": [{"x": cx, "y": 17, "z": 134},
                          {"x": cx, "y": 17, "z": 142}],
            "walkSpeed": 2.2, "waitTime": 3.0,
        })

    time.sleep(12)
    for door in DOORS:
        key = f"DM_{race}_{door}"
        min_z[key] = 1e9
    for _ in range(5):
        for door in DOORS:
            key = f"DM_{race}_{door}"
            z = zpos(key)
            if z is not None:
                min_z[key] = min(min_z[key], z)
        time.sleep(2)
    # Remove this wave before the next race enters the lanes.
    for door in DOORS:
        post("/api/npc/remove", {"name": f"DM_{race}_{door}"})

print(f"{'race':10s}" + "".join(f"{d:>10s}" for d in DOORS))
for race in RACES:
    row = f"{race:10s}"
    for door in DOORS:
        got = "PASS" if min_z[f"DM_{race}_{door}"] < 136.5 else "BLOCK"
        want = EXPECT[race][door]
        mark = got if want in ("?", got) else f"{got}!"
        if want not in ("?", got):
            mismatches.append((race, door, want, got))
        row += f"{mark:>10s}"
    print(row)

print("\nborderline (ogre 2.96 vs door B 3.0):",
      "PASS" if min_z["DM_ogre_B_h3"] < 136.5 else "BLOCK")
print("RESULT:", "MATRIX MATCHES EXPECTATIONS" if not mismatches
      else f"MISMATCHES: {mismatches}")

# Clean up the 12 matrix NPCs.
for race in RACES:
    for door in DOORS:
        post("/api/npc/remove", {"name": f"DM_{race}_{door}"})
