"""Ravenmere G-181 L4: scene load times on the SHIPPED exe.

User (2026-09-24): "load times are slow". This drives the shipped probe build through
main menu -> town -> farm -> town -> cellar -> town and reads the engine's own timing lines
from its log: `Scene '<id>' loaded in N ms` and `Built NavGraph (...) in N ms on T threads`.
Writes docs/evidence/ravenmere/rv_load_time_<label>.json.

Usage: python tools/rv_load_time_l4.py [label]
"""
import json
import os
import re
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

# RV_REL overrides the build dir (e.g. the pre-change build for a before/after pair).
REL = Path(os.environ.get("RV_REL", r"C:/Users/bpete/Documents/PhyxelProjects/Ravenmere/build_probe/Release"))
OUT = Path(__file__).resolve().parents[1] / 'docs' / 'evidence' / 'ravenmere'
PORT = 8123
B = 'http://127.0.0.1:%d' % PORT
LABEL = sys.argv[1] if len(sys.argv) > 1 else 'run'


def safe(m, p, b=None, t=60):
    try:
        r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None,
                                   method=m, headers={'Content-Type': 'application/json'})
        return json.load(urllib.request.urlopen(r, timeout=t))
    except Exception as e:
        return {'err': str(e)[:200]}


def wait_scene(scene, screen='playing', tries=360):
    for _ in range(tries):
        sc = safe('GET', '/api/screen/state')
        if sc.get('scene_id') == scene and sc.get('screen') == screen:
            return True
        time.sleep(0.25)
    return False


def shot_now(name):
    """The frame right after the loading screen dropped: what the player sees first."""
    r = safe('GET', '/api/screenshot')
    if r.get('path'):
        try:
            (OUT / ('rv_load_time_%s_%s.png' % (LABEL, name))).write_bytes((REL / r['path']).read_bytes())
        except Exception as e:
            print('  screenshot copy failed:', e)


log_path = OUT / ('rv_load_time_%s.log' % LABEL)
log = open(log_path, 'w', encoding='utf-8', errors='replace')
proc = subprocess.Popen([str(REL / 'Ravenmere.exe'), '--test', str(PORT)], cwd=str(REL),
                        stdout=log, stderr=subprocess.STDOUT)
visits = []
try:
    for _ in range(180):
        if 'err' not in safe('GET', '/api/state', t=3):
            break
        time.sleep(1)
    time.sleep(2)
    # main menu -> town (the New Game button)
    reached = False
    for _a in range(6):
        safe('POST', '/api/ui/click', {'x': 640, 'y': 374})
        for _ack in range(12):   # G-116: the intro card gates the first scene - Continue sits at (640, 628)
            if safe('GET', '/api/screen/state').get('screen') == 'narrative':
                safe('POST', '/api/ui/click', {'x': 640, 'y': 628}); break
            time.sleep(0.25)
        if wait_scene('town', tries=40):
            reached = True
            break
    assert reached, 'never reached the town'
    visits.append('town')
    shot_now('town_1')
    time.sleep(3)
    for trig, scene in (('to_farm', 'farm'), ('back_to_town', 'town'), ('to_cellar', 'cellar'), ('back_to_town', 'town')):
        r = safe('POST', '/api/rpg/fire_trigger', {'id': trig})
        ok = wait_scene(scene)
        if ok: shot_now('%s_%d' % (scene, len(visits) + 1))
        print('  %-13s -> %-7s %s %s' % (trig, scene, 'ok' if ok else 'NOT REACHED', json.dumps(r)[:80]), flush=True)
        visits.append(scene if ok else scene + ' (not reached)')
        time.sleep(3)
finally:
    proc.terminate()
    try:
        proc.wait(timeout=10)
    except Exception:
        proc.kill()
    log.close()

text = open(log_path, encoding='utf-8', errors='replace').read()
text = re.sub(r'\x1b\[[0-9;]*m', '', text)
loads = re.findall(r"Scene '([a-z_]+)' loaded in (\d+) ms", text)
navs = re.findall(r"Built NavGraph \((\w+) mode\): (\d+) columns, (\d+) surfaces in (\d+) ms(?: on (\d+) threads)?", text)
holds = re.findall(r"Loading screen dismissed \(settled after (\d+) ms, (\d+) dirty chunks left", text)
chunks = re.findall(r"World: loaded (\d+) saved chunks of the region from the DB", text)
print('\nscene loads (engine log):')
for s, ms in loads:
    print('  %-10s %6s ms' % (s, ms))
print('loading-screen holds after the load (settled after N ms, dirty chunks left):')
for ms, dirty in holds:
    print('  %6s ms  %s dirty' % (ms, dirty))
print('NavGraph builds:')
for mode, cols, surf, ms, thr in navs:
    print('  %s: %s columns, %s surfaces, %s ms on %s threads' % (mode, cols, surf, ms, thr or '?'))
result = {'label': LABEL, 'exe': str(REL / 'Ravenmere.exe'), 'visits': visits,
          'scene_loads_ms': [{'scene': s, 'ms': int(ms)} for s, ms in loads],
          'navgraph_builds': [{'mode': m, 'columns': int(c), 'surfaces': int(s), 'ms': int(ms), 'threads': int(t) if t else None}
                              for m, c, s, ms, t in navs],
          'chunk_loads': [int(c) for c in chunks],
          'loading_holds': [{'settled_ms': int(ms), 'dirty_left': int(d)} for ms, d in holds]}
(OUT / ('rv_load_time_%s.json' % LABEL)).write_text(json.dumps(result, indent=1), encoding='utf-8')
print('wrote', OUT / ('rv_load_time_%s.json' % LABEL))
