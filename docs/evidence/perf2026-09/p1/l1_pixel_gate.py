"""L1 pixel gate (docs/PerfProgram2026-09.md §6 gate 2): with the SAME uploaded light set (S-1 tavern is
under the 32 cap), merged and unmerged emitters must render the same image.

Control: C is a merge-ON capture taken after the SAME re-mesh and the SAME wait as B (merge-OFF), so
diff(A1, C) is the noise floor at matched timing (the tavern NPC walks, grass sways). PASS = diff(A1, B)
is no larger than the control
at p99.9 by more than 1/255 and has no pixel over 8/255 that the control does not also have.
Run with the tone curve linear (curve 0, exposure 1) and with the shipping curve."""
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


def merge(on):
    call('POST', '/api/debug/emitter_merge', {'enabled': on})
    settle()
    time.sleep(1.0)
    return call('GET', '/api/debug/light_stats')['registered']


def stats(d):
    m = d.max(axis=2)
    return {'max': int(m.max()), 'p999': float(np.percentile(m, 99.9)), 'over8': int((m > 8).sum()),
            'mean': float(m.mean())}


pose = json.loads(sys.argv[1])
call('POST', '/api/camera', {'mode': 'free', 'position': {'x': pose[0], 'y': pose[1], 'z': pose[2]},
                             'yaw': pose[3], 'pitch': pose[4]})
orig = call('POST', '/api/debug/tonemap', {})
out = {'pose': pose, 'curves': {}}
ok = True
try:
    for curve_name, tm in (('linear', {'exposure': 1.0, 'curve': 0}),
                           ('shipping', {'exposure': orig['exposure'], 'curve': orig['curve']})):
        call('POST', '/api/debug/tonemap', tm)
        # CONTROL WITH MATCHED TIMING (v2). The first version compared A1-A2 (1.5 s apart) against
        # A1-B (a whole re-mesh + settle apart): the tavern NPC walks through the doorway in view, so
        # the longer gap alone produced a larger diff, all of it inside the NPC's box. C is taken
        # after the SAME re-mesh and the SAME wait as B, with the merge still ON, so C and B differ
        # only in the setting under test.
        n_on = merge(True)
        a1, p1 = cap('A1')
        n_off = merge(False)
        b, pb = cap('B')
        merge(True)
        c, p2 = cap('C')
        ctrl, test = stats(np.abs(a1 - c)), stats(np.abs(a1 - b))
        good = test['p999'] <= ctrl['p999'] + 1 and test['over8'] <= ctrl['over8']
        ok &= good
        out['curves'][curve_name] = {'lights_on': n_on, 'lights_off': n_off, 'control_A1_A2': ctrl,
                                     'test_A1_B': test, 'pass': bool(good), 'captures': [p1, p2, pb]}
        print(f"{curve_name:8s} lights on/off {n_on}/{n_off}  control {ctrl}  test {test}  {'PASS' if good else 'FAIL'}")
finally:
    call('POST', '/api/debug/tonemap', {'exposure': orig['exposure'], 'curve': orig['curve']})
    merge(True)
json.dump(out, open(sys.argv[2], 'w'), indent=1)
print('PIXEL GATE PASSED' if ok else 'PIXEL GATE FAILED')
sys.exit(0 if ok else 1)
