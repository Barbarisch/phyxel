"""A4 (doorway-lit wall) with GI-2 on vs off, ORDER-BALANCED (off,on,on,off,off,on,on,off) with a 20 s
settle after every toggle. Written because 4 on/off pairs, always run on-first with an 8 s settle, read
on 2.98/3.10/2.51/3.00 vs off 2.28/1.94/2.52: suggestive, but order and convergence were confounded.
Records numerator (in_door_wall) and denominator (in_sealed_wall) separately. Lighting Lab rig
(tools/ambient_model_check.py --build) must already be built.
Usage: gi_a4_abba.py out.json [two_level|skip_empty]   ('on' arm; 'off' = the plain micro march)"""
import json, os, subprocess, sys, time, urllib.request

B = 'http://127.0.0.1:8090'
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..'))


def call(m, p, b=None):
    r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(r, timeout=60))


MODE = sys.argv[2] if len(sys.argv) > 2 else 'two_level'
assert MODE in ('two_level', 'skip_empty'), MODE
order = ['off', 'on', 'on', 'off', 'off', 'on', 'on', 'off']
rows = []
for i, cfg in enumerate(order):
    on = cfg == 'on'
    call('POST', '/api/debug/gi_probe', {'skip_buried': on, 'two_level_trace': on and MODE == 'two_level',
                                         'skip_empty_trace': on and MODE == 'skip_empty'})
    time.sleep(20)
    tag = f'gi2_abba_{MODE}_{i}_{cfg}' if MODE != 'two_level' else f'gi2_abba_{i}_{cfg}'
    subprocess.run([sys.executable, 'tools/ambient_model_check.py', '--check', tag], cwd=REPO,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ev = json.load(open(os.path.join(REPO, 'docs', 'evidence', f'ambient_{tag}.json')))
    v = ev['values']
    a4 = next(r['measured'] for r in ev['results'] if r['check'].startswith('A4'))
    rows.append({'i': i, 'cfg': cfg, 'A4': a4, 'in_door_wall': v['in_door_wall'],
                 'in_sealed_wall': v['in_sealed_wall'], 'all_pass': all(r['pass'] for r in ev['results']),
                 'evidence': f'docs/evidence/ambient_{tag}.json'})
    print(rows[-1])
call('POST', '/api/debug/gi_probe', {'skip_buried': True, 'two_level_trace': True})
json.dump(rows, open(sys.argv[1], 'w'), indent=1)
