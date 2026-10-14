"""All four GI probe-pass option combos in ONE process, interleaved twice (ABCD DCBA), at one pose:
GI Probes + GPU Frame medians over 240 frames each. Written because the GI-1 A/B (one process) read
the probe pass at 3.98/3.87 ms and the GI-2 A/B (another process) read its GI-1-on baseline at
2.84 ms - either drift between processes or an effect of the shader rewrite itself.
Usage: gi_combo_check.py out.json"""
import json, statistics, sys, time, urllib.request

B = 'http://127.0.0.1:8090'
POSE = [12.5, 18.4, 3.5, 180, -8]   # S-1 interior


def call(m, p, b=None):
    r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(r, timeout=60))


call('POST', '/api/camera', {'mode': 'free', 'position': dict(zip('xyz', POSE[:3])), 'yaw': POSE[3], 'pitch': POSE[4]})
call('POST', '/api/debug/depth_prepass', {'enabled': True})
combos = [(False, False), (True, False), (False, True), (True, True)]
order = combos + combos[::-1]
rows = []
for skip, two in order:
    call('POST', '/api/debug/gi_probe', {'skip_buried': skip, 'two_level_trace': two})
    time.sleep(6)   # > 240 frames at ~100 fps, so the window holds only this config
    g = call('GET', '/api/debug/gpu_timing?frames=240')
    gi = next(s for s in g['scopes'] if s['key'] == 'GI Probes')['median_ms']
    rows.append({'skip_buried': skip, 'two_level': two, 'gi_probes_ms': gi, 'gpu_frame_ms': g['gpu_frame_ms']['median_ms']})
    print(rows[-1])
call('POST', '/api/debug/gi_probe', {'skip_buried': True, 'two_level_trace': True})
summary = {}
for skip, two in combos:
    k = f'skip={int(skip)} two={int(two)}'
    rs = [r for r in rows if r['skip_buried'] == skip and r['two_level'] == two]
    summary[k] = {'gi_probes_ms': statistics.mean(r['gi_probes_ms'] for r in rs),
                  'gpu_frame_ms': statistics.mean(r['gpu_frame_ms'] for r in rs)}
    print(k, summary[k])
json.dump({'pose': POSE, 'rows': rows, 'summary': summary}, open(sys.argv[1], 'w'), indent=1)
