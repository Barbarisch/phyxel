"""rv_wander_l4.py - streamed-surround L4 on the SHIPPED build: nobody ends up past the wall.

With terrain now continuing past the play area, the invariant is that every character stays
inside the play bounds over time - the player (clamped), and the NPCs (wander targets clipped).
Samples every entity's position at the farm and in the town for a while and reports the count
of samples outside the effective bounds, plus each entity's closest approach to the wall.
Writes rv_wander_l4.json.
"""
import json
import subprocess
import time
import urllib.request
from pathlib import Path

REL = Path(r"C:/Users/bpete/Documents/PhyxelProjects/Ravenmere/build_probe/Release")
OUT = Path(r"C:/Users/bpete/Documents/GitHub/phyxel/docs/evidence/ravenmere")
PORT = 8129
B = 'http://127.0.0.1:%d' % PORT
WATCH_SECONDS = 40.0


def safe(m, p, b=None, t=60):
    try:
        r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None,
                                   method=m, headers={'Content-Type': 'application/json'})
        return json.load(urllib.request.urlopen(r, timeout=t))
    except Exception as e:
        return {'err': str(e)[:200]}


def scene():
    return safe('GET', '/api/screen/state').get('scene_id')


def go(trigger_id, want_scene):
    safe('POST', '/api/rpg/fire_trigger', {'id': trigger_id})
    for _ in range(60):
        if scene() == want_scene:
            time.sleep(4)
            return True
        time.sleep(1)
    return False


def watch(label):
    pb = safe('POST', '/api/rpg/play_bounds', {})
    b = pb.get('bounds') or {}
    lo = (b.get('min', {}).get('x', -1e9), b.get('min', {}).get('z', -1e9))
    hi = (b.get('max', {}).get('x', 1e9), b.get('max', {}).get('z', 1e9))
    rows = {}
    outside = 0
    samples = 0
    t0 = time.monotonic()
    while time.monotonic() - t0 < WATCH_SECONDS:
        for e in safe('GET', '/api/state').get('entities', []):
            p = e.get('position') or {}
            x, z = float(p.get('x', 0)), float(p.get('z', 0))
            eid = e.get('id', '?')
            margin = min(x - lo[0], hi[0] - x, z - lo[1], hi[1] - z)   # distance to the nearest wall
            r = rows.setdefault(eid, {'min_margin': 1e9, 'outside': 0, 'n': 0})
            r['n'] += 1
            r['min_margin'] = round(min(r['min_margin'], margin), 2)
            samples += 1
            if margin < 0:
                r['outside'] += 1
                outside += 1
        time.sleep(0.5)
    return {'scene': label, 'bounds': pb, 'entities': rows, 'samples': samples, 'outside': outside}


log = open(OUT / 'rv_wander_l4.log', 'w', encoding='utf-8', errors='replace')
proc = subprocess.Popen([str(REL / 'Ravenmere.exe'), '--test', str(PORT)], cwd=str(REL),
                        stdout=log, stderr=subprocess.STDOUT)
res = {}
try:
    for _ in range(180):
        if 'err' not in safe('GET', '/api/state', t=3):
            break
        time.sleep(1)
    reached = False
    for _a in range(6):
        safe('POST', '/api/ui/click', {'x': 640, 'y': 374})
        for _ack in range(12):   # G-116: the intro card gates the first scene - Continue sits at (640, 628)
            if safe('GET', '/api/screen/state').get('screen') == 'narrative':
                safe('POST', '/api/ui/click', {'x': 640, 'y': 628}); break
            time.sleep(0.25)
        for _ in range(40):
            sc = safe('GET', '/api/screen/state')
            if sc.get('scene_id') == 'town' and sc.get('screen') == 'playing':
                reached = True
                break
            time.sleep(1)
        if reached:
            break
    assert reached, 'never reached the town'
    time.sleep(6)
    res['town'] = watch('town')
    print('town: %d samples, %d outside; bounds %s' % (res['town']['samples'], res['town']['outside'],
                                                     res['town']['bounds'].get('source')), flush=True)
    assert go('to_farm', 'farm'), 'never reached the farm'
    time.sleep(6)
    res['farm'] = watch('farm')
    print('farm: %d samples, %d outside; bounds %s' % (res['farm']['samples'], res['farm']['outside'],
                                                     res['farm']['bounds'].get('source')), flush=True)
    for sc in ('town', 'farm'):
        for eid, r in sorted(res[sc]['entities'].items()):
            print('   %-6s %-22s n=%3d min_margin=%6.2f outside=%d' % (sc, eid, r['n'], r['min_margin'], r['outside']))
finally:
    proc.terminate()
    time.sleep(2)
    log.flush()
(OUT / 'rv_wander_l4.json').write_text(json.dumps(res, indent=2), encoding='utf-8')
total_out = sum(res[s]['outside'] for s in res)
total = sum(res[s]['samples'] for s in res)
print('\nVERDICT: %s' % ('every character stayed inside the play bounds (%d samples)' % total if total_out == 0 and total > 0
                         else 'NOT PROVEN - %d of %d samples outside' % (total_out, total)), flush=True)
