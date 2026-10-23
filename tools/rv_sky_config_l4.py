"""Sky block L4 (2026-09-26): do the per-game sky knobs reach the SHIPPED game?

User: "as long as the length of things like day/night, moon cycle, and whatever is easily
configurable in a game file". Runs the shipped Ravenmere exe twice:
  config - build_probe/Release/game.json gets ONLY {"sky": {"latitudeDeg": 10,
           "lunarCycleDays": 8, "nightFill": 0}} (the knobs on their own, no body list)
  control - the game.json as shipped (no sky block)
and reads back, through the game's own test API (/api/rpg/daynight skyBodies):
  noon sun elevation   prediction: config ~80 deg (90 - 10), control ~50 deg
  moon lit at midnight prediction: config full day 8, new day 12, full day 16;
                                    control new on day 14, full again only on day 28
  new-moon night luma  prediction: config (nightFill 0) darker than control (fill 1), both at a
                                    NEW moon (config day 12, control day 14), same pose, settled 75 s
The game.json copy is restored in `finally`.
Writes docs/evidence/ravenmere/rv_sky_config.json.
"""
import json
import os
import shutil
import subprocess
import time
import urllib.request
from pathlib import Path

from PIL import Image

REL = Path(os.environ.get("RV_REL", r"C:/Users/bpete/Documents/PhyxelProjects/Ravenmere/build_probe/Release"))
OUT = Path(__file__).resolve().parents[1] / 'docs' / 'evidence' / 'ravenmere'
PORT = 8123
B = 'http://127.0.0.1:%d' % PORT
BOXES = [(520, 90, 1270, 330), (905, 330, 1270, 560)]   # world regions no HUD panel covers
SKY = {"latitudeDeg": 10, "lunarCycleDays": 8, "nightFill": 0}


def safe(m, p, b=None, t=60):
    try:
        r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None,
                                   method=m, headers={'Content-Type': 'application/json'})
        return json.load(urllib.request.urlopen(r, timeout=t))
    except Exception as e:
        return {'err': str(e)[:200]}


def world_luma(name):
    time.sleep(1.2)
    r = safe('GET', '/api/screenshot')
    if not r.get('path'):
        return None
    src = REL / r['path']
    im = Image.open(src).convert('L')
    sx, sy = im.size[0] / 1280.0, im.size[1] / 720.0
    tot = n = 0
    for (x0, y0, x1, y1) in BOXES:
        h = im.crop((int(x0 * sx), int(y0 * sy), int(x1 * sx), int(y1 * sy))).histogram()
        tot += sum(i * v for i, v in enumerate(h)); n += sum(h)
    (OUT / ('rv_sky_config_%s.png' % name)).write_bytes(src.read_bytes())
    return round(tot / max(1, n), 2)


def bodies():
    r = safe('GET', '/api/rpg/daynight')
    return {b.get('name'): b for b in r.get('skyBodies', [])}


def run(label, newMoonDay):
    log = open(OUT / ('rv_sky_config_%s.log' % label), 'w', encoding='utf-8', errors='replace')
    proc = subprocess.Popen([str(REL / 'Ravenmere.exe'), '--test', str(PORT)], cwd=str(REL), stdout=log, stderr=subprocess.STDOUT)
    res = {'label': label}
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
        safe('POST', '/api/rpg/daynight_set', {'dayNumber': 8, 'timeOfDay': 12.0, 'paused': True})
        time.sleep(0.5)
        res['noon_sun_elev'] = round(bodies().get('sun', {}).get('elevationDeg', -99), 1)
        res['moon_lit_midnight'] = {}
        for d in (8, 10, 12, 14, 16):
            safe('POST', '/api/rpg/daynight_set', {'dayNumber': d, 'timeOfDay': 0.0, 'paused': True})
            time.sleep(0.4)
            res['moon_lit_midnight'][d] = round(bodies().get('moon', {}).get('litFraction', -1), 3)
        safe('POST', '/api/rpg/daynight_set', {'dayNumber': newMoonDay, 'timeOfDay': 0.0, 'paused': True})
        time.sleep(0.4)
        m = bodies().get('moon', {})
        res['new_moon_day'] = newMoonDay
        res['new_moon_lit'] = round(m.get('litFraction', -1), 3)
        time.sleep(75)   # the probe field settles ~60 s after a clock jump (G-186)
        res['new_moon_luma'] = world_luma('%s_newmoon' % label)
        print('  %-8s noon sun %.1f deg | moon lit at midnight %s | new moon (day %d, lit %.3f) world luma %s' % (
            label, res['noon_sun_elev'], res['moon_lit_midnight'], newMoonDay, res['new_moon_lit'], res['new_moon_luma']), flush=True)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except Exception:
            proc.kill()
        log.close()
    return res


gj = REL / 'game.json'
backup = REL / 'game.json.skyl4_backup'
shutil.copyfile(gj, backup)
out = {'sky': SKY}
try:
    d = json.loads(gj.read_text(encoding='utf-8'))
    assert 'sky' not in d, 'the probe game.json already has a sky block; the control would not be a control'
    d['sky'] = SKY
    gj.write_text(json.dumps(d, indent=2), encoding='utf-8')
    out['config'] = run('config', newMoonDay=12)
finally:
    shutil.copyfile(backup, gj)
    backup.unlink()
out['control'] = run('control', newMoonDay=14)
(OUT / 'rv_sky_config.json').write_text(json.dumps(out, indent=1), encoding='utf-8')
print('wrote', OUT / 'rv_sky_config.json')
