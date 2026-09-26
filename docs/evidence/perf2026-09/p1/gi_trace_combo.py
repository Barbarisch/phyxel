"""The three probe primary traces in ONE process, interleaved ABC CBA ABC CBA at both S-1 poses:
micro march (phxDdaTrace), cube walk (GI-2, phxDdaTraceTwoLevel), skip-empty march (GI-2b,
phxDdaTraceSkipEmpty). GI Probes + GPU Frame medians over 240 frames per visit. skip_buried on in all.
The S-1 tavern must already be built in this process. Usage: gi_trace_combo.py out.json"""
import json, statistics, sys, time, urllib.request

B = 'http://127.0.0.1:8090'
POSES = {'interior': [12.5, 18.4, 3.5, 180, -8], 'exterior': [7, 21, 24, -90, -8]}
MODES = {'micro': {'two_level_trace': False, 'skip_empty_trace': False},
         'cube_walk': {'two_level_trace': True, 'skip_empty_trace': False},
         'skip_empty': {'two_level_trace': False, 'skip_empty_trace': True}}


def call(m, p, b=None):
    r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(r, timeout=60))


call('POST', '/api/debug/depth_prepass', {'enabled': True})
abc = list(MODES)
order = abc + abc[::-1] + abc + abc[::-1]
out = {'order': order, 'poses': {}}
for pname, p in POSES.items():
    call('POST', '/api/camera', {'mode': 'free', 'position': dict(zip('xyz', p[:3])), 'yaw': p[3], 'pitch': p[4]})
    time.sleep(6)
    rows = []
    for mode in order:
        call('POST', '/api/debug/gi_probe', dict(MODES[mode], skip_buried=True))
        time.sleep(5)
        g = call('GET', '/api/debug/gpu_timing?frames=240')
        rows.append({'mode': mode, 'gi_probes_ms': next(s for s in g['scopes'] if s['key'] == 'GI Probes')['median_ms'],
                     'gpu_frame_ms': g['gpu_frame_ms']['median_ms']})
    summ = {m: {k: statistics.median(r[k] for r in rows if r['mode'] == m) for k in ('gi_probes_ms', 'gpu_frame_ms')}
            for m in MODES}
    out['poses'][pname] = {'rows': rows, 'median': summ}
    print(pname, json.dumps(summ))
call('POST', '/api/debug/gi_probe', {'skip_buried': True, 'two_level_trace': True, 'skip_empty_trace': False})
json.dump(out, open(sys.argv[1], 'w'), indent=1)
