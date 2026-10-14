#!/usr/bin/env bash
# P1 attribution run for one scene (docs/PerfProgram2026-09.md §4). Usage:
#   run_scene.sh <scene-tag> <poses.json> "<provenance>"
# For each pose: a baseline census (voxel_tiers, light_stats, gpu/cpu timing), then one
# counterbalanced A/B per component config (base vs component removed), 8 pairs of 240-frame windows.
set -u
TAG=$1; POSES=$2; PROV=$3
D=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$D/../../../.." && pwd)
cd "$R"
python - "$POSES" "$D/${TAG}_census.json" <<'PY'
import json, sys, time, urllib.request
B = 'http://127.0.0.1:8090'
def call(m, p, b=None):
    r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(r, timeout=60))
out = {}
for name, p in json.load(open(sys.argv[1])).items():
    call('POST', '/api/camera', {'mode': 'free', 'position': {'x': p[0], 'y': p[1], 'z': p[2]}, 'yaw': p[3], 'pitch': p[4]})
    time.sleep(6)
    out[name] = {k: call('GET', path) for k, path in (
        ('voxel_tiers', '/api/debug/voxel_tiers?covered=1'), ('light_stats', '/api/debug/light_stats'),
        ('gpu_timing', '/api/debug/gpu_timing?frames=240'), ('cpu_timing', '/api/debug/cpu_timing?frames=240'),
        ('engine_timing', '/api/debug/engine_timing'))}
json.dump(out, open(sys.argv[2], 'w'))
print('census written')
PY
for AB in lights_off main_no_micro main_no_sub shadow_no_micro shadow_no_sub foliage_off; do
  echo "=== $TAG $AB"
  python tools/perf_harness.py sample --poses "$POSES" --ab "$D/ab_$AB.json" --repeats 8 --frames 240 \
    --provenance "$PROV" --out "$D/${TAG}_${AB}.jsonl" > "$D/${TAG}_${AB}.log" 2>&1
  echo "exit $?"
done
echo "SCENE DONE"
