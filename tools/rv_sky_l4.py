"""Ravenmere G-164 / T-02 L4: can the player SEE the sun and the moon, on the shipped exe?

User (2026-09-23): "i dont see a sun being rendered in the sky". For each stated time of day
(time paused): read the sun direction from the engine, aim a detached camera straight at the
body, capture, and measure the disc - the brightest pixels in the centre of the frame against
the sky ring around them. Also capture the ordinary gameplay camera at the same time, to answer
the other half of the question: is the body ever in the frame a player actually sees?

Writes docs/evidence/ravenmere/rv_sky_<label>.json and a PNG per shot.
Usage: python tools/rv_sky_l4.py [label]
"""
import json
import math
import os
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

from PIL import Image

REL = Path(os.environ.get("RV_REL", r"C:/Users/bpete/Documents/PhyxelProjects/Ravenmere/build_probe/Release"))
OUT = Path(__file__).resolve().parents[1] / 'docs' / 'evidence' / 'ravenmere'
PORT = 8123
B = 'http://127.0.0.1:%d' % PORT
LABEL = sys.argv[1] if len(sys.argv) > 1 else 'run'
HOURS = [7.0, 12.0, 17.5, 23.0]   # morning, noon, evening, night (the moon)


def safe(m, p, b=None, t=60):
    try:
        r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None,
                                   method=m, headers={'Content-Type': 'application/json'})
        return json.load(urllib.request.urlopen(r, timeout=t))
    except Exception as e:
        return {'err': str(e)[:200]}


def shot(name):
    time.sleep(1.0)
    r = safe('GET', '/api/screenshot')
    if not r.get('path'):
        return None
    out = OUT / ('rv_sky_%s_%s.png' % (LABEL, name))
    out.write_bytes((REL / r['path']).read_bytes())
    return out


def disc_metric(path):
    """Centre disc vs the sky ring around it: the mean of the brightest 1% of pixels within 40 px
    of the frame centre, against the median of a ring 80-140 px out. A visible disc is a bright
    blob on a darker ring; a missing one reads ~1.0."""
    im = Image.open(path).convert('L')
    w, h = im.size
    cx, cy = w // 2, h // 2
    inner, ring = [], []
    for y in range(cy - 140, cy + 141, 2):
        for x in range(cx - 140, cx + 141, 2):
            d = math.hypot(x - cx, y - cy)
            v = im.getpixel((x, y))
            if d <= 40:
                inner.append(v)
            elif 80 <= d <= 140:
                ring.append(v)
    inner.sort(reverse=True)
    top = inner[:max(1, len(inner) // 100)]
    ring.sort()
    med = ring[len(ring) // 2]
    return {'disc_peak': round(sum(top) / len(top), 1), 'sky_ring_median': med,
            'contrast': round((sum(top) / len(top)) / max(1, med), 2)}


def yaw_pitch_toward(d):
    # free camera: yaw 0 = +X, yaw 90 = +Z; pitch up positive
    yaw = math.degrees(math.atan2(d['z'], d['x']))
    pitch = math.degrees(math.asin(max(-1.0, min(1.0, d['y']))))
    return yaw, pitch


log = open(OUT / ('rv_sky_%s.log' % LABEL), 'w', encoding='utf-8', errors='replace')
proc = subprocess.Popen([str(REL / 'Ravenmere.exe'), '--test', str(PORT)], cwd=str(REL), stdout=log, stderr=subprocess.STDOUT)
rows = []
try:
    for _ in range(180):
        if 'err' not in safe('GET', '/api/state', t=3):
            break
        time.sleep(1)
    time.sleep(2)
    reached = False
    for _a in range(6):
        safe('POST', '/api/ui/click', {'x': 640, 'y': 374})
        for _ack in range(12):
            if safe('GET', '/api/screen/state').get('screen') == 'narrative':
                safe('POST', '/api/ui/click', {'x': 640, 'y': 628}); break
            time.sleep(0.25)
        for _ in range(60):
            sc = safe('GET', '/api/screen/state')
            if sc.get('scene_id') == 'town' and sc.get('screen') == 'playing':
                reached = True
                break
            time.sleep(1)
        if reached:
            break
    assert reached, 'never reached the town'
    time.sleep(4)
    ps = safe('GET', '/api/state')
    player = None
    for e in ps.get('entities', []) if isinstance(ps.get('entities'), list) else []:
        if e.get('type') in ('animated', 'player') or e.get('id') == 'player':
            player = e.get('position'); break
    px, py, pz = (player['x'], player['y'], player['z']) if isinstance(player, dict) else (0.0, 20.0, 0.0)
    for hr in HOURS:
        safe('POST', '/api/rpg/daynight_set', {'timeOfDay': hr, 'paused': True})
        time.sleep(0.5)
        resp = safe('GET', '/api/rpg/daynight')
        bodies = resp.get('skyBodies')
        if not bodies:   # an older exe: only the sun, from the cycle (the moon assumed opposite)
            sd = resp.get('daynight', {}).get('sunDirection', {'x': 0, 'y': -1, 'z': 0})
            bodies = [{'name': 'sun', 'toward': {'x': -sd['x'], 'y': -sd['y'], 'z': -sd['z']}},
                      {'name': 'moon(assumed)', 'toward': {'x': sd['x'], 'y': sd['y'], 'z': sd['z']}}]
        safe('POST', '/api/rpg/set_camera', {'detach': False})
        g = shot('%02d_gameplay' % int(hr))
        for bd in bodies:
            t = bd['toward']
            elev = math.degrees(math.asin(max(-1, min(1, t['y']))))
            row = {'hour': hr, 'body': bd['name'], 'elevation_deg': round(elev, 1), 'litFraction': bd.get('litFraction'),
                   'gameplay': disc_metric(g) if g else None}
            if elev > 2.0:
                yaw, pitch = yaw_pitch_toward(t)
                safe('POST', '/api/rpg/set_camera', {'detach': True, 'x': px, 'y': py + 3.0, 'z': pz, 'yaw': yaw, 'pitch': pitch})
                a_ = shot('%02d_%s_aimed' % (int(hr), bd['name'].split('(')[0]))
                row['aimed'] = disc_metric(a_) if a_ else None
            else:
                row['aimed'] = 'below horizon'
            rows.append(row)
            print('  %5.1f h  %-13s elev %6.1f  lit %s  aimed %s' % (hr, bd['name'], elev, bd.get('litFraction'), row['aimed']), flush=True)
    safe('POST', '/api/rpg/set_camera', {'detach': False})
finally:
    proc.terminate()
    try:
        proc.wait(timeout=10)
    except Exception:
        proc.kill()
    log.close()
(OUT / ('rv_sky_%s.json' % LABEL)).write_text(json.dumps({'label': LABEL, 'rows': rows}, indent=1), encoding='utf-8')
print('wrote', OUT / ('rv_sky_%s.json' % LABEL))
