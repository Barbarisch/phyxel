"""One arm of the cross-process HEAD-vs-new probe-shader check (docs/PerfProgram2026-09.md section 15).
Run after: launch M4TavernBench (Release) -> this script rebuilds the S-1 tavern, settles, and samples
GI Probes + GPU Frame at both S-1 poses (3 windows x 240 frames each). The arm label says which
gi_probe.comp.spv was in shaders/ and build/shaders/ for this process. Appends one JSON line.
Usage: gi_process_arm.py <label> out.jsonl"""
import json, statistics, sys, time, urllib.request

B = 'http://127.0.0.1:8090'
POSES = {'interior': [12.5, 18.4, 3.5, 180, -8], 'exterior': [7, 21, 24, -90, -8]}


def call(m, p, b=None, t=60):
    r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(r, timeout=t))


for _ in range(120):
    try:
        call('GET', '/api/status'); break
    except Exception:
        time.sleep(2)
time.sleep(5)
try:
    call('POST', '/api/structure/build', {'schema': 'v2', 'type': 'house', 'typology': 'tavern', 'style': 'timber_cottage',
                                          'position': {'x': 0, 'y': 16, 'z': 0}, 'footprint': [14, 7],
                                          'stories': [{'height': 3}, {'height': 3}]}, t=120)
except Exception as e:
    print('build call:', e)   # the route times out client-side while the build completes (as for S-1)
time.sleep(15)
call('POST', '/api/debug/depth_prepass', {'enabled': True})
lights = call('GET', '/api/debug/light_stats')['enabled_point']
out = {'label': sys.argv[1], 'enabled_point_lights': lights, 'poses': {}}
for name, p in POSES.items():
    call('POST', '/api/camera', {'mode': 'free', 'position': dict(zip('xyz', p[:3])), 'yaw': p[3], 'pitch': p[4]})
    time.sleep(8)
    gi, fr = [], []
    for _ in range(3):
        g = call('GET', '/api/debug/gpu_timing?frames=240')
        gi.append(next(s for s in g['scopes'] if s['key'] == 'GI Probes')['median_ms'])
        fr.append(g['gpu_frame_ms']['median_ms'])
        time.sleep(4)
    out['poses'][name] = {'gi_probes_ms': statistics.median(gi), 'gpu_frame_ms': statistics.median(fr)}
print(json.dumps(out))
open(sys.argv[2], 'a').write(json.dumps(out) + '\n')
