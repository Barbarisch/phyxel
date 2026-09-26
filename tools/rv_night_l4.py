"""Ravenmere night brightness L4: how dark is the night, across the lunar cycle, on the shipped exe?

User (2026-09-25): "i let the game run for a really long time ... now it is very very very dark. i
know it is supposed to be night, but this is too dark, and technically the approximation of the
moon should be generating light". For days 1..28 (step 3) at night hours, time paused: the moon's
elevation and lit fraction as the engine reports them, and the mean luma of the WORLD part of the
ordinary gameplay frame (HUD panels masked). A noon frame is the reference.

Writes docs/evidence/ravenmere/rv_night_<label>.json (+ a few PNGs).
Usage: python tools/rv_night_l4.py [label]
"""
import json
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
DAYS = [1, 4, 7, 10, 13, 16, 19, 22, 25]
HOURS = [21.0, 0.0, 3.0]
# settle mode: RV_NIGHT_SETTLE=<s> measures fixed (day, hour) cases, waiting <s> seconds after each
# jump and sampling luma over the wait, so a slowly converging term (probe field) shows as a curve.
SETTLE = float(os.environ.get('RV_NIGHT_SETTLE', '0'))
CASES = [(13, 0.0, 'new moon, midnight'), (1, 0.0, 'full moon high, midnight'), (13, 0.0, 'new moon again'), (1, 12.0, 'noon')]
# world regions of a 1280x720 gameplay frame that no HUD panel covers (right half, mid band)
BOXES = [(520, 90, 1270, 330), (905, 330, 1270, 560)]


def safe(m, p, b=None, t=60):
    try:
        r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None,
                                   method=m, headers={'Content-Type': 'application/json'})
        return json.load(urllib.request.urlopen(r, timeout=t))
    except Exception as e:
        return {'err': str(e)[:200]}


def world_luma(name, keep=False):
    time.sleep(1.2)
    r = safe('GET', '/api/screenshot')
    if not r.get('path'):
        return None
    src = REL / r['path']
    im = Image.open(src).convert('L')
    sx, sy = im.size[0] / 1280.0, im.size[1] / 720.0
    tot = n = 0
    for (x0, y0, x1, y1) in BOXES:
        c = im.crop((int(x0 * sx), int(y0 * sy), int(x1 * sx), int(y1 * sy)))
        h = c.histogram()
        tot += sum(i * v for i, v in enumerate(h)); n += sum(h)
    if keep:
        (OUT / ('rv_night_%s_%s.png' % (LABEL, name))).write_bytes(src.read_bytes())
    return round(tot / max(1, n), 1)


log = open(OUT / ('rv_night_%s.log' % LABEL), 'w', encoding='utf-8', errors='replace')
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
    if SETTLE > 0:
        for d, h, what in CASES:
            safe('POST', '/api/rpg/daynight_set', {'dayNumber': d, 'timeOfDay': h, 'paused': True})
            resp = safe('GET', '/api/rpg/daynight')
            moon = next((b for b in resp.get('skyBodies', []) if b.get('name') == 'moon'), {})
            curve = []
            t0 = time.time()
            while time.time() - t0 < SETTLE:
                curve.append((round(time.time() - t0, 1), world_luma('settle', keep=False)))
            last = world_luma('settled_d%02d_h%02d' % (d, int(h)), keep=True)
            rows.append({'case': what, 'day': d, 'hour': h, 'moon_elev': round(moon.get('elevationDeg', -99), 1),
                         'moon_lit': round(moon.get('litFraction', 0), 2), 'curve': curve, 'settled_luma': last})
            print('  %-26s moon elev %6.1f lit %.2f  luma over %ds: %s -> settled %s' % (
                what, moon.get('elevationDeg', -99), moon.get('litFraction', 0), int(SETTLE),
                [c[1] for c in curve[::max(1, len(curve) // 6)]], last), flush=True)
        noon = None
        raise SystemExit(0)
    safe('POST', '/api/rpg/daynight_set', {'dayNumber': 1, 'timeOfDay': 12.0, 'paused': True})
    noon = world_luma('noon', keep=True)
    print('  noon reference: world luma %s' % noon, flush=True)
    for d in DAYS:
        for h in HOURS:
            safe('POST', '/api/rpg/daynight_set', {'dayNumber': d, 'timeOfDay': h, 'paused': True})
            time.sleep(0.4)
            resp = safe('GET', '/api/rpg/daynight')
            moon = next((b for b in resp.get('skyBodies', []) if b.get('name') == 'moon'), {})
            lum = world_luma('d%02d_h%02d' % (d, int(h)), keep=(h == 0.0 and d in (1, 7, 13)))
            row = {'day': d, 'hour': h, 'moon_elev': round(moon.get('elevationDeg', -99), 1),
                   'moon_lit': round(moon.get('litFraction', 0), 2), 'world_luma': lum,
                   'vs_noon': round(lum / noon, 3) if lum and noon else None}
            rows.append(row)
            print('  day %2d %04.1fh  moon elev %6.1f lit %.2f  world luma %5s  (%s of noon)' % (
                d, h, row['moon_elev'], row['moon_lit'], lum, row['vs_noon']), flush=True)
finally:
    if SETTLE > 0:
        (OUT / ('rv_night_%s.json' % LABEL)).write_text(json.dumps({'label': LABEL, 'settle_s': SETTLE, 'rows': rows}, indent=1), encoding='utf-8')
    proc.terminate()
    try:
        proc.wait(timeout=10)
    except Exception:
        proc.kill()
    log.close()
(OUT / ('rv_night_%s.json' % LABEL)).write_text(json.dumps({'label': LABEL, 'noon': noon, 'rows': rows}, indent=1), encoding='utf-8')
print('wrote', OUT / ('rv_night_%s.json' % LABEL))
