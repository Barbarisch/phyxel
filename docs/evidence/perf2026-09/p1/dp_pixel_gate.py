"""P-DP pixel gate (docs/PerfProgram2026-09.md): the depth prepass must not change the image.

A1 = prepass OFF, B = prepass ON, C = prepass OFF again after the SAME wait as B (the timing-matched
control: the tavern NPC walks and grass sways, so frames differ over time even with nothing changed).
PASS = diff(A1, B) is no larger than diff(A1, C) at p99.9 (+1/255 tolerance) and has no more pixels
over 8/255. Run with the linear curve and the shipping curve.
Usage: dp_pixel_gate.py '[x,y,z,yaw,pitch]' out.json"""
import json, os, sys, time, urllib.request

import numpy as np
from PIL import Image

B = 'http://127.0.0.1:8090'
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..'))
VIEW = (242, 43, 1197, 672)
WAIT = 1.5


def call(m, p, b=None, t=60):
    r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(r, timeout=t))


def prepass(on):
    r = call('POST', '/api/debug/depth_prepass', {'enabled': on})
    time.sleep(WAIT)
    ran = call('POST', '/api/debug/depth_prepass', {})['ran_last_frame']
    assert ran == on, f'prepass requested {on} but ran_last_frame={ran}'
    return r


def cap():
    p = call('GET', '/api/screenshot')['path']
    a = np.array(Image.open(os.path.join(REPO, p)).convert('RGB')).astype(np.int32)
    x0, y0, x1, y1 = VIEW
    return a[y0:y1, x0:x1], p


def stats(d):
    m = d.max(axis=2)
    return {'max': int(m.max()), 'p999': float(np.percentile(m, 99.9)), 'over8': int((m > 8).sum()),
            'mean': float(m.mean())}


if __name__ == '__main__':
    pose = json.loads(sys.argv[1])
    call('POST', '/api/camera', {'mode': 'free', 'position': {'x': pose[0], 'y': pose[1], 'z': pose[2]},
                                 'yaw': pose[3], 'pitch': pose[4]})
    time.sleep(2)
    orig = call('POST', '/api/debug/tonemap', {})
    out = {'pose': pose, 'curves': {}}
    ok = True
    try:
        for name, tm in (('linear', {'exposure': 1.0, 'curve': 0}),
                         ('shipping', {'exposure': orig['exposure'], 'curve': orig['curve']})):
            call('POST', '/api/debug/tonemap', tm)
            prepass(False); a1, p1 = cap()
            prepass(True);  b, pb = cap()
            prepass(False); c, pc = cap()
            ctrl, test = stats(np.abs(a1 - c)), stats(np.abs(a1 - b))
            good = test['p999'] <= ctrl['p999'] + 1 and test['over8'] <= ctrl['over8']
            ok &= good
            out['curves'][name] = {'control_A1_C': ctrl, 'test_A1_B': test, 'pass': bool(good),
                                   'captures': [p1, pb, pc]}
            print(f"{name:8s} control {ctrl}  test {test}  {'PASS' if good else 'FAIL'}")
    finally:
        call('POST', '/api/debug/tonemap', {'exposure': orig['exposure'], 'curve': orig['curve']})
        call('POST', '/api/debug/depth_prepass', {'enabled': False})
    json.dump(out, open(sys.argv[2], 'w'), indent=1)
    print('PIXEL GATE PASSED' if ok else 'PIXEL GATE FAILED')
    sys.exit(0 if ok else 1)
