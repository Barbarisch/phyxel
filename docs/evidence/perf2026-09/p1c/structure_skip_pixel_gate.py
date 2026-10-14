"""Structure-LOD skip pixel gate (docs/PerfProgram2026-09.md section 16.12), the gi_pixel_gate.py method:
with s_structureLodSkipInvisible ON and OFF the frame must be the same. The skip removes a proxy's
main-view draw only when the shader would discard every fragment of it (minFade 0, base inside the
fade start), so the prediction is IDENTICAL up to the timing-matched noise floor.

Control: C is a skip-ON capture after the SAME toggle + settle + wait as B (skip OFF), so diff(A1, C) is
the noise floor at matched timing (residents walk, grass sways). PASS = diff(A1, B) no larger than the
control at p99.9 by more than 1/255 and no more pixels over 8/255. Also records how many proxies the
skip removed in that frame (a gate that skipped nothing proves nothing). Linear + shipping tone curves.
--freeze removes the temporal sources first (render_pixel_diff.py methodology): grass + foliage off
and the GAME paused (residents stop walking; harmless at noon -- the emissive reconcile pause stops
only matters at night). Frozen, the control must read ~0 or the run is void.
Usage: structure_skip_pixel_gate.py '[x,y,z,yaw,pitch]' out.json [--freeze]"""
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
    for _ in range(600):
        c = call('GET', '/api/debug/load_state')['chunks']
        if not (c['generation_pending'] or c['remesh_pending'] or c['remesh_idle_pending']):
            return
        time.sleep(0.1)


def cap():
    time.sleep(1.5)
    p = call('GET', '/api/screenshot')['path']
    a = np.array(Image.open(os.path.join(REPO, p)).convert('RGB')).astype(np.int32)
    x0, y0, x1, y1 = VIEW
    return a[y0:y1, x0:x1], p


def skip(on):
    r = call('POST', '/api/debug/far_terrain', {'structures': True, 'structures_skip_invisible': on})
    assert r.get('success'), r
    settle()
    time.sleep(1.0)
    return call('GET', '/api/debug/lod_report', t=60)['structures'].get('skipped_invisible_last_frame')


def stats(d):
    m = d.max(axis=2)
    return {'max': int(m.max()), 'p999': float(np.percentile(m, 99.9)), 'over8': int((m > 8).sum()),
            'mean': float(m.mean())}


pose = json.loads(sys.argv[1])
FREEZE = '--freeze' in sys.argv
if FREEZE:
    call('POST', '/api/debug/grass', {'enabled': False})
    call('POST', '/api/debug/foliage', {'enabled': False})
    call('POST', '/api/game/pause', {'paused': True})
call('POST', '/api/camera', {'mode': 'free', 'position': {'x': pose[0], 'y': pose[1], 'z': pose[2]},
                             'yaw': pose[3], 'pitch': pose[4]})
call('POST', '/api/daynight/set', {'timeOfDay': 12.0, 'paused': True})
orig = call('POST', '/api/debug/tonemap', {})
out = {'pose': pose, 'frozen': FREEZE, 'curves': {}}
ok = True
try:
    for curve_name, tm in (('linear', {'exposure': 1.0, 'curve': 0}),
                           ('shipping', {'exposure': orig['exposure'], 'curve': orig['curve']})):
        call('POST', '/api/debug/tonemap', tm)
        skipped_on = skip(True)
        a1, p1 = cap()
        skipped_off = skip(False)
        b, pb = cap()
        skip(True)
        c, p2 = cap()
        ctrl, test = stats(np.abs(a1 - c)), stats(np.abs(a1 - b))
        good = test['p999'] <= ctrl['p999'] + 1 and test['over8'] <= ctrl['over8']
        ok &= good
        out['curves'][curve_name] = {'control_A1_C': ctrl, 'test_A1_B': test, 'pass': bool(good),
                                     'skipped_with_skip_on': skipped_on, 'skipped_with_skip_off': skipped_off,
                                     'captures': [p1, p2, pb]}
        print(f"{curve_name:8s} skipped {skipped_on} (off: {skipped_off})  control {ctrl}  test {test}  "
              f"{'PASS' if good else 'FAIL'}")
finally:
    call('POST', '/api/debug/tonemap', {'exposure': orig['exposure'], 'curve': orig['curve']})
    skip(True)
    if FREEZE:
        call('POST', '/api/game/pause', {'paused': False})
        call('POST', '/api/debug/grass', {'enabled': True})
        call('POST', '/api/debug/foliage', {'enabled': True})
json.dump(out, open(sys.argv[2], 'w'), indent=1)
print('PIXEL GATE PASSED' if ok else 'PIXEL GATE FAILED')
sys.exit(0 if ok else 1)
