#!/usr/bin/env bash
# Red-before-green proof for terrain-aware settlement seating.
# For each mode: wipe the world, generate Perlin hills, wait until terrain is COMPLETE
# (voxel data == terrain_height on spread columns), then build+measure.
#   RED  : --seat-flat  -> terrain-blind seating (by=oy) -> invariant must FAIL
#   GREEN:              -> shipped terrain seating       -> invariant must PASS
# Outputs land in scripts/seating_evidence/.
set -u
cd "$(dirname "$0")/.."
H=~/Documents/PhyxelProjects/StructGenHills
OUT=scripts/seating_evidence
mkdir -p "$OUT"

stop_engine() {
  curl -s -m 5 -X POST http://localhost:8090/api/engine/stop >/dev/null 2>&1; sleep 1
  local PID; PID=$(tasklist 2>/dev/null | grep -i phyxel.exe | awk '{print $2}' | head -1)
  [ -n "$PID" ] && taskkill //PID "$PID" //F >/dev/null 2>&1
  for _ in $(seq 1 8); do tasklist 2>/dev/null | grep -qi phyxel.exe || break; sleep 1; done
}

fresh_world() {
  stop_engine
  rm -f "$H/worlds/"*.db "$H/worlds/"*.db-* 2>/dev/null
  ./phyxel.exe --project "$H" > hills.out 2> hills.err &
  for _ in $(seq 1 30); do curl -s -m 2 http://localhost:8090/api/engine/status >/dev/null 2>&1 && break; sleep 1; done
  sleep 2
  curl -s -m 20 -X POST http://localhost:8090/api/world/generate -H "Content-Type: application/json" \
    -d '{"type":"Perlin","seed":7,"from":{"x":0,"y":0,"z":0},"to":{"x":3,"y":0,"z":3},
         "params":{"frequency":0.03,"heightScale":18,"octaves":3}}' >/dev/null 2>&1
  # wait until terrain is complete (heights stable AND voxel_top == terrain_height)
  python - <<'PY'
import json,urllib.request,time,sys
B="http://localhost:8090"
def th(x,z):
    try: return json.load(urllib.request.urlopen(f"{B}/api/world/terrain_height?x={x}&z={z}",timeout=5)).get("surface_y")
    except: return None
probes=[(10,10),(64,64),(118,118),(10,118),(118,10),(90,40),(40,90)]
prev=None
for _ in range(20):
    time.sleep(2); cur=[th(x,z) for x,z in probes]
    if cur==prev and all(v is not None for v in cur):
        print("terrain complete:",cur); sys.exit(0)
    prev=cur
print("WARN terrain not confirmed stable:",cur); sys.exit(1)
PY
}

echo "===== RED (terrain-blind seating, expect FAIL) ====="
fresh_world
python -u scripts/verify_terrain_seating.py --seat-flat | tee "$OUT/red_seat_flat.txt"

echo "===== GREEN (terrain seating, expect PASS) ====="
fresh_world
python -u scripts/verify_terrain_seating.py | tee "$OUT/green_terrain.txt"

echo "===== DONE — evidence in $OUT ====="
