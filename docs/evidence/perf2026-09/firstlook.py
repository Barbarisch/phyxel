"""First-look attribution on the 4090: does G-18 (lights = 79-91% of frame) reproduce?
Engine-generated tavern (POST /api/structure/build, v2 tavern typology), Release editor.
Shader bisect ladder on the Static Geometry GPU scope via POST /api/debug/shadow {mode}.
gpu_scopes is a single ~2-frame-stale frame, so we take N reads per mode and report the median."""
import json, sys, time, statistics, urllib.request

B = 'http://127.0.0.1:8090'
N = 25
OUT = sys.argv[1] if len(sys.argv) > 1 else 'firstlook.jsonl'


def call(m, p, b=None, t=60):
    try:
        r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None,
                                   method=m, headers={'Content-Type': 'application/json'})
        return json.load(urllib.request.urlopen(r, timeout=t))
    except Exception as e:
        return {'err': str(e)[:200]}


def scopes():
    s = call('GET', '/api/debug/gpu_scopes')
    return {d['name']: d['ms'] for d in s.get('scopes', [])}, s


def sample(n=N):
    rows = []
    for _ in range(n):
        time.sleep(0.12)
        sc, raw = scopes()
        et = call('GET', '/api/debug/engine_timing')
        rows.append({'scopes': sc, 'fps': et.get('fps'), 'cpu': et.get('cpuFrameTime'),
                     'rec': (et.get('detailed') or {}).get('commandRecordTime')})
    med = lambda k: statistics.median([r['scopes'].get(k, 0.0) for r in rows])
    return {'static': med('Static Geometry'), 'scene': med('Scene Pass'), 'shadow': med('Shadow Pass'),
            'grass': med('Grass'), 'foliage': med('Foliage'), 'gi': med('GI Probes'),
            'fps': statistics.median([r['fps'] or 0 for r in rows]),
            'cpu': statistics.median([r['cpu'] or 0 for r in rows]),
            'rec': statistics.median([r['rec'] or 0 for r in rows]),
            'static_iqr': (lambda v: (v[len(v)//4], v[3*len(v)//4]))(sorted(r['scopes'].get('Static Geometry', 0) for r in rows))}


def pose(p):
    got = call('POST', '/api/camera', {'position': {'x': p[0], 'y': p[1], 'z': p[2]}, 'yaw': p[3], 'pitch': p[4]})
    time.sleep(1.5)
    back = call('GET', '/api/camera')
    return got, back


MODES = [0, 11, 12, 13, 14, 15, 16, 17, 0]
NAMES = {0: 'normal', 11: 'raster', 12: '+tex', 13: '+shadow', 14: '+ambient', 15: '+sun/moon',
         16: '+lights', 17: 'lights,no march'}

if __name__ == '__main__':
    poses = json.loads(sys.argv[2])
    with open(OUT, 'a') as f:
        lights = call('GET', '/api/lights')
        f.write(json.dumps({'lights_count': lights.get('point_light_count'),
                            'n_lights_listed': len(lights.get('lights', lights.get('point_lights', []))) if isinstance(lights, dict) else None}) + '\n')
        print('point lights registered:', lights.get('point_light_count'), flush=True)
        for name, p in poses.items():
            got, back = pose(p)
            print('\n== pose', name, p, '-> readback', json.dumps(back)[:160], flush=True)
            print('%-16s %9s %9s %9s %8s %7s %7s' % ('mode', 'static', 'scene', 'shadow', 'fps', 'cpu', 'rec'))
            for m in MODES:
                call('POST', '/api/debug/shadow', {'mode': m})
                time.sleep(0.8)
                r = sample()
                r.update({'pose': name, 'p': p, 'mode': m, 'cam': back})
                f.write(json.dumps(r) + '\n'); f.flush()
                print('%-16s %9.3f %9.3f %9.3f %8.1f %7.2f %7.2f  iqr=%s' % (
                    NAMES[m], r['static'], r['scene'], r['shadow'], r['fps'], r['cpu'], r['rec'],
                    tuple(round(x, 3) for x in r['static_iqr'])), flush=True)
            call('POST', '/api/debug/shadow', {'mode': 0})
