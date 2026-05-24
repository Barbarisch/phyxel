import urllib.request, json, time, sys

BASE = "http://localhost:8090"
CHAIR_FRONT_Z = 13.333 + 0.334
SEAT_ANCHOR = {"x": 13.333, "y": 16.667, "z": 13.333}
OX = SEAT_ANCHOR["x"] + 0.5
OY = SEAT_ANCHOR["y"] + 0.5
OZ = SEAT_ANCHOR["z"] + 1.5

def post(path, body={}):
    data = json.dumps(body).encode()
    req = urllib.request.Request(BASE + path, data=data, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=15) as r:
        return json.loads(r.read())

CLIPS = [
    ("stand_to_sit", [0.0, 0.25, 0.5, 0.75, 1.0]),
    ("sitting_idle",  [0.0, 0.25, 0.5, 0.75, 1.0]),
    ("sit_to_stand",  [0.0, 0.25, 0.5, 0.75, 1.0]),
]

results = []

post("/api/interaction/ie/sit", {})
time.sleep(0.5)

for clip, times in CLIPS:
    for t in times:
        seek = post("/api/interaction/ie/seek", {"clip_name": clip, "normalized_time": t})
        if not seek.get("success"):
            print(f"SEEK FAILED: {clip} t={t}: {seek}", flush=True)
            continue
        wz = seek["world_pos"]["z"]
        clip_through = wz < CHAIR_FRONT_Z
        time.sleep(0.4)
        ss = post("/api/orbit-screenshots", {
            "x": OX, "y": OY, "z": OZ,
            "radius": 2,
            "views": ["east", "south"]
        })
        paths = {item["view"]: item["path"] for item in ss.get("screenshots", [])}
        r = {
            "clip": clip, "t": t,
            "world_pos_z": wz,
            "clip_through": clip_through,
            "east": paths.get("east", ""),
            "south": paths.get("south", ""),
        }
        results.append(r)
        ct = "CLIP-THROUGH" if clip_through else "ok"
        print(f"{ct:12s}  {clip:15s}  t={t:.2f}  z={wz:.4f}  e={paths.get('east','')}  s={paths.get('south','')}", flush=True)

out = "C:/Users/jack/AppData/Local/Temp/sit_validate_results.json"
with open(out, "w") as f:
    json.dump(results, f, indent=2)
print(f"DONE -> {out}")
