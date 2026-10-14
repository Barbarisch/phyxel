"""Structure-proxy ladder rig (PerfProgram 17.2 step 3; prediction written in the plan BEFORE running).
One engine-built tavern (proxyrig_build.json) in CityBench_ProxyRig. The only variable is camera distance
from the building centre: the camera sits on ONE bearing, elevated 20 degrees, looking at the building.
Per distance: T1 = today's ladder, A = candidate ladder, T2 = today's ladder again (control).
test = |T1 - A|, control = |T1 - T2|, both over the viewport; plus the bounding box of changed pixels (must
sit on the building, near screen centre) and the proxy's lod_report state for every capture.
Freeze (no game pause): grass/foliage off, residents + wildlife off, effect time held, player parked.
Usage: proxy_ladder_rig.py out.json"""
import json, math, os, sys, time, urllib.request

import numpy as np
from PIL import Image

B = 'http://127.0.0.1:8090'
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..'))
VIEW = (242, 43, 1197, 672)
TODAY = [360.0, 500.0, 700.0, 900.0, 1200.0]
CAND = [256.0, 500.0, 700.0, 900.0, 1200.0]
DISTANCES = [280.0, 320.0, 346.0, 400.0, 500.0]
CENTRE = np.array([0.0, 57.5, -0.5])        # tavern bbox (-4,52,-7)-(4,63,6)
BEARING = math.radians(45.0)                 # toward +X,+Z from the building
ELEV = math.radians(20.0)


def call(m, p, b=None, t=60):
    r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(r, timeout=t))


def settle():
    for _ in range(900):
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


def ladder(v):
    r = call('POST', '/api/debug/far_terrain', {'structure_ladder': v})
    assert r.get('structure_ladder') == v, r
    time.sleep(0.5)


def proxy_state():
    e = call('GET', '/api/debug/lod_report', t=60)['structures']['entries'][0]
    return {k: e[k] for k in ('last_dist', 'last_level', 'last_min_fade', 'readiness', 'state')}


def stats(d):
    m = d.max(axis=2)
    ys, xs = np.nonzero(m > 2)
    box = None if len(ys) == 0 else [int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())]
    return {'max': int(m.max()), 'p999': float(np.percentile(m, 99.9)), 'px_over2': int(len(ys)),
            'px_over8': int((m > 8).sum()), 'changed_bbox_in_viewport': box}


# ---- freeze (no pause) --------------------------------------------------------------------------
call('POST', '/api/daynight/set', {'timeOfDay': 12.0, 'paused': True})
call('POST', '/api/debug/grass', {'enabled': False})
call('POST', '/api/debug/foliage', {'enabled': False})
call('POST', '/api/debug/residents', {'enabled': False, 'fauna': False})
home = next((e.get('position') for e in call('GET', '/api/entities').get('entities', [])
             if e.get('id') == 'player'), None)
if home:   # park the player far behind every camera position (opposite bearing)
    px, pz = -60.0, -60.0
    gy = call('GET', f'/api/world/terrain_height?x={int(px)}&z={int(pz)}', t=60).get('spawn_y')
    if gy is not None:
        call('POST', '/api/entity/move', {'id': 'player', 'position': {'x': px, 'y': float(gy), 'z': pz}})
call('POST', '/api/debug/effect_time', {'frozen': True})

out = {'today': TODAY, 'candidate': CAND, 'centre': CENTRE.tolist(), 'bearing_deg': 45, 'elev_deg': 20,
       'distances': {}}
try:
    ladder(TODAY)
    for d in DISTANCES:
        off = np.array([math.cos(ELEV) * math.cos(BEARING), math.sin(ELEV), math.cos(ELEV) * math.sin(BEARING)]) * d
        cam = CENTRE + off
        to = CENTRE - cam
        yaw = math.degrees(math.atan2(to[2], to[0]))
        pitch = math.degrees(math.asin(to[1] / np.linalg.norm(to)))
        call('POST', '/api/camera', {'mode': 'free', 'position': {'x': float(cam[0]), 'y': float(cam[1]), 'z': float(cam[2])},
                                     'yaw': yaw, 'pitch': pitch})
        time.sleep(1.0)
        rb = call('GET', '/api/camera').get('position', {})
        assert abs(rb['x'] - cam[0]) < 0.05 and abs(rb['z'] - cam[2]) < 0.05, rb
        settle()
        # convergence: two captures 2 s apart within 2/255, max 30 s
        t0 = time.time(); prev, _ = cap()
        while True:
            cur, _ = cap()
            if int(np.abs(cur - prev).max()) <= 2 or time.time() - t0 > 30:
                break
            prev = cur
        ladder(TODAY); t1, p1 = cap(); s1 = proxy_state()
        ladder(CAND);  a, pa = cap();  sa = proxy_state()
        ladder(TODAY); t2, p2 = cap(); s2 = proxy_state()
        row = {'camera': [float(x) for x in cam], 'yaw': yaw, 'pitch': pitch,
               'converge_s': round(time.time() - t0, 1),
               'proxy_today': s1, 'proxy_candidate': sa, 'proxy_today_again': s2,
               'test_today_vs_candidate': stats(np.abs(t1 - a)), 'control_today_vs_today': stats(np.abs(t1 - t2)),
               'captures': {'today': p1, 'candidate': pa, 'today_again': p2}}
        out['distances'][str(int(d))] = row
        print(int(d), 'level today', s1['last_level'], 'cand', sa['last_level'], 'minFade', round(s1['last_min_fade'], 3),
              'test', row['test_today_vs_candidate'], 'control', row['control_today_vs_today'], flush=True)
finally:
    ladder(TODAY)
    call('POST', '/api/debug/effect_time', {'frozen': False})
    call('POST', '/api/debug/residents', {'enabled': True, 'fauna': True})
    call('POST', '/api/debug/grass', {'enabled': True})
    call('POST', '/api/debug/foliage', {'enabled': True})
    if home:
        call('POST', '/api/entity/move', {'id': 'player', 'position': home})
json.dump(out, open(sys.argv[1], 'w'), indent=1)
