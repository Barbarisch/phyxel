"""GI-1/GI-2 pixel gate (docs/PerfProgram2026-09.md section 15), adapted from l1_pixel_gate.py: with a probe-pass
option (skip_buried or two_level_trace) ON and OFF the lit frame must be the same.

Control: C is a skip-ON capture taken after the SAME toggle, settle and wait as B (skip OFF), so
diff(A1, C) is the noise floor at matched timing (the tavern NPC walks, grass sways, the probe field's
rotated ray set jitters). PASS = diff(A1, B) is no larger than the control at p99.9 by more than 1/255
and has no more pixels over 8/255 than the control. Run with the tone curve linear (curve 0,
exposure 1) and with the shipping curve.
Usage: gi_pixel_gate.py '[x,y,z,yaw,pitch]' out.json <skip_buried|two_level_trace>"""
import json, os, sys, time, urllib.request

import numpy as np
from PIL import Image

B = 'http://127.0.0.1:8090'
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..'))
VIEW = (242, 43, 1197, 672)


def call(m, p, b=None, t=60):
    r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(r, timeout=t))


def settle():
    for _ in range(300):
        c = call('GET', '/api/debug/load_state')['chunks']
        if not (c['generation_pending'] or c['remesh_pending'] or c['remesh_idle_pending']):
            return
        time.sleep(0.1)


def cap(tag):
    time.sleep(1.5)
    p = call('GET', '/api/screenshot')['path']
    a = np.array(Image.open(os.path.join(REPO, p)).convert('RGB')).astype(np.int32)
    x0, y0, x1, y1 = VIEW
    return a[y0:y1, x0:x1], p


def skip(on):
    # 1.0 s is ~8 full probe-grid refreshes at ~60+ fps (1/8 of the grid per frame).
    r = call('POST', '/api/debug/gi_probe', {OPT: on})
    assert r[OPT] == on and r['gi_enabled'], r
    settle()
    time.sleep(1.0)
    return r[OPT]


def stats(d):
    m = d.max(axis=2)
    return {'max': int(m.max()), 'p999': float(np.percentile(m, 99.9)), 'over8': int((m > 8).sum()),
            'mean': float(m.mean())}


pose = json.loads(sys.argv[1])
OPT = sys.argv[3]
assert OPT in ('skip_buried', 'two_level_trace'), OPT
call('POST', '/api/camera', {'mode': 'free', 'position': {'x': pose[0], 'y': pose[1], 'z': pose[2]},
                             'yaw': pose[3], 'pitch': pose[4]})
orig = call('POST', '/api/debug/tonemap', {})
out = {'pose': pose, 'option': OPT, 'curves': {}}
ok = True
try:
    for curve_name, tm in (('linear', {'exposure': 1.0, 'curve': 0}),
                           ('shipping', {'exposure': orig['exposure'], 'curve': orig['curve']})):
        call('POST', '/api/debug/tonemap', tm)
        skip(True)
        a1, p1 = cap('A1')
        skip(False)
        b, pb = cap('B')
        skip(True)
        c, p2 = cap('C')
        ctrl, test = stats(np.abs(a1 - c)), stats(np.abs(a1 - b))
        good = test['p999'] <= ctrl['p999'] + 1 and test['over8'] <= ctrl['over8']
        ok &= good
        out['curves'][curve_name] = {'control_A1_C': ctrl, 'test_A1_B': test, 'pass': bool(good),
                                     'captures': [p1, p2, pb]}
        print(f"{curve_name:8s} control {ctrl}  test {test}  {'PASS' if good else 'FAIL'}")
finally:
    call('POST', '/api/debug/tonemap', {'exposure': orig['exposure'], 'curve': orig['curve']})
    skip(True)
json.dump(out, open(sys.argv[2], 'w'), indent=1)
print('PIXEL GATE PASSED' if ok else 'PIXEL GATE FAILED')
sys.exit(0 if ok else 1)
