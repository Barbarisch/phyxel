"""R-DP2 re-run with the noise source removed (docs/PerfProgram2026-09.md P-DP).

The first R-DP2 run (dp_discard_rig.py) failed at 2 of 6 angles, each on ONE tone curve only (the
failing curve flipped between the two poses). The diff images showed every differing pixel - test and
control alike - in the swaying grass on the ground, and none on the rig (wall, glass pane, mirror).
Grass wind keeps animating while paused, so the timing-matched control cannot cancel it. Here grass and
foliage are switched OFF (the rig floats above the ground and does not depend on either), the game is
paused and debug mode 12 is on, so a correct prepass must give a near-zero control AND test.
Same 6 orbit poses as the first run (read from dp_discard_rig.json)."""
import json, os, subprocess, sys, urllib.request

B = 'http://127.0.0.1:8090'
HERE = os.path.dirname(os.path.abspath(__file__))


def call(m, p, b=None, t=60):
    r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(r, timeout=t))


poses = {k: v['pose'] for k, v in json.load(open(os.path.join(HERE, 'dp_discard_rig.json')))['results'].items()}
call('POST', '/api/debug/grass', {'enabled': False})
call('POST', '/api/debug/foliage', {'enabled': False})
call('POST', '/api/game/pause', {'paused': True})
call('POST', '/api/debug/shadow', {'mode': 12})
results = {}
try:
    for name, pose in poses.items():
        out = os.path.join(HERE, f'dp_rig_nograss_{name}.json')
        rc = subprocess.call([sys.executable, os.path.join(HERE, 'dp_pixel_gate.py'), json.dumps(pose), out])
        d = json.load(open(out))
        results[name] = {'pose': pose, 'pass': rc == 0,
                         'curves': {c: {'control_over8': v['control_A1_C']['over8'], 'test_over8': v['test_A1_B']['over8'],
                                        'control_p999': v['control_A1_C']['p999'], 'test_p999': v['test_A1_B']['p999']}
                                    for c, v in d['curves'].items()}}
        print(name, 'PASS' if rc == 0 else 'FAIL', results[name]['curves'], flush=True)
finally:
    call('POST', '/api/debug/shadow', {'mode': 0})
    call('POST', '/api/game/pause', {'paused': False})
    call('POST', '/api/debug/grass', {'enabled': True})
    call('POST', '/api/debug/foliage', {'enabled': True})
json.dump(results, open(os.path.join(HERE, 'dp_discard_rig_nograss.json'), 'w'), indent=1)
print('R-DP2 (no grass) PASSED' if all(r['pass'] for r in results.values()) else 'R-DP2 (no grass) FAILED')
