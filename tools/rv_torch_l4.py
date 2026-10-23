"""Ravenmere torches L4 (T-73): do townsfolk carry a lit torch after dark, on the SHIPPED exe?

User (2026-09-26): "if we are going to have such dark nights, then npc AI needs to compensate in
some cases and learn to carry torches". Chosen default: townsfolk outdoors at night.

Phases, clock paused (daynight_set):
  noon    - POST /api/rpg/torches: nobody carries one (prediction: carrying 0).
  23:00   - every non-hostile NPC OUTDOORS carries a torch, the torch is DRAWN in the hand, and
            its flame light sits at the hand (within 1.2 m horizontally of the NPC, 0.3..2.2 m
            above its feet). Hostiles and NPCs indoors carry none.
  control - the same pose and hour, frames with the game default switched never -> night ->
            never (the test API's torches {"default"}): the torch-on frame must be brighter in a
            box around the bearer, and the two torch-off frames must agree.
  budget  - dropped_point_lights and frame time, noon vs night.

Writes docs/evidence/ravenmere/rv_torch_<label>.json (+ PNGs).
Usage: python tools/rv_torch_l4.py [label]
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
    src = REL / r['path']
    dst = OUT / ('rv_torch_%s_%s.png' % (LABEL, name))
    dst.write_bytes(src.read_bytes())
    return dst


def luma(path, box=None):
    im = Image.open(path).convert('L')
    if box:
        w, h = im.size
        im = im.crop((int(box[0] * w), int(box[1] * h), int(box[2] * w), int(box[3] * h)))
    hist = im.histogram()
    return round(sum(i * v for i, v in enumerate(hist)) / max(1, sum(hist)), 2)


def fps():
    vals = []
    for _ in range(6):
        r = safe('GET', '/api/debug/engine_timing')
        if r.get('cpuFrameTime'):
            vals.append(r['cpuFrameTime'])
        time.sleep(0.25)
    vals.sort()
    return round(vals[len(vals) // 2], 2) if vals else None


def check_night(t):
    """Per-NPC verdicts for the 23:00 snapshot."""
    bad = []
    for n in t.get('list', []):
        night, r, ins = bool(t.get('night')), n['resolved'], n['indoors']
        want = r == 'always' or (r == 'night' and night and not ins) or (r == 'dark' and (night or ins))
        has = n['carrying'] == 'torch'
        drawn = n['held'] == 'torch'
        if want != has:
            bad.append('%s: policy %s indoors %s but carrying=%r' % (n['name'], n['resolved'], n['indoors'], n['carrying']))
        if has and not drawn:
            bad.append('%s carries a torch that is not drawn (held=%r)' % (n['name'], n['held']))
        if drawn:
            lights = n.get('lights', [])
            if len(lights) != 1 or 'position' not in lights[0]:
                bad.append('%s: torch drawn but lights=%s' % (n['name'], lights))
            else:
                lp, p = lights[0]['position'], n['position']
                horiz = math.hypot(lp[0] - p[0], lp[2] - p[2])
                dy = lp[1] - p[1]
                n['light_offset'] = [round(horiz, 2), round(dy, 2)]
                if not (horiz < 1.2 and 0.3 < dy < 2.2):
                    bad.append('%s: flame light off the hand (horiz %.2f, dy %.2f)' % (n['name'], horiz, dy))
    return bad


log = open(OUT / ('rv_torch_%s.log' % LABEL), 'w', encoding='utf-8', errors='replace')
proc = subprocess.Popen([str(REL / 'Ravenmere.exe'), '--test', str(PORT)], cwd=str(REL), stdout=log, stderr=subprocess.STDOUT)
res = {'label': LABEL}
ok = False
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
    time.sleep(5)

    # --- noon
    safe('POST', '/api/rpg/daynight_set', {'dayNumber': 1, 'timeOfDay': 12.0, 'paused': True})
    time.sleep(3)
    noon = safe('POST', '/api/rpg/torches', {})
    res['noon'] = {k: noon.get(k) for k in ('night', 'npcs', 'carrying', 'drawn', 'lit', 'dropped_point_lights')}
    res['noon']['frame_ms'] = fps()
    print('  noon : %s' % res['noon'], flush=True)

    # --- 23:00
    safe('POST', '/api/rpg/daynight_set', {'dayNumber': 1, 'timeOfDay': 23.0, 'paused': True})
    time.sleep(5)
    night = safe('POST', '/api/rpg/torches', {})
    bad = check_night(night)
    res['night'] = {k: night.get(k) for k in ('night', 'default', 'npcs', 'carrying', 'drawn', 'lit', 'dropped_point_lights')}
    res['night']['frame_ms'] = fps()
    res['night']['list'] = night.get('list', [])
    res['night']['violations'] = bad
    print('  23:00: %s' % {k: v for k, v in res['night'].items() if k != 'list'}, flush=True)
    for n in night.get('list', []):
        print('     %-18s %-7s hostile=%-5s indoors=%-5s carrying=%-6r held=%-6r light=%s' % (
            n['name'], n['resolved'], n['hostile'], n['indoors'], n['carrying'], n['held'], n.get('light_offset')), flush=True)

    # --- control frames at one bearer. Top-down, 7 m above the bearer: outdoors there is no roof
    # between, and the torch's light pool on the ground around its feet is in the middle of the
    # frame. The probe field needs ~60 s to settle after a clock jump (G-186), so settle FIRST at
    # the town, THEN pick the bearer nearest the player among those outdoors right now (at 23:00
    # the schedules send people home; a bearer picked before the settle may be indoors, and
    # legitimately torchless, by the time the frames are taken - 2026-10-14). Per frame the
    # bearer's shelter and carried item are recorded, so a torchless frame is attributable.
    if night.get('list'):
        print('  settling the probe field 70 s before the control', flush=True)
        time.sleep(70)
        now = safe('POST', '/api/rpg/torches', {})
        bearers = [n for n in now.get('list', []) if n['held'] == 'torch' and not n['indoors']]
    else:
        bearers = []
    if bearers:
        ps = safe('GET', '/api/state')
        pl = None
        for e in ps.get('entities', []) if isinstance(ps.get('entities'), list) else []:
            if e.get('type') in ('animated', 'player') or e.get('id') == 'player':
                pl = e.get('position'); break
        px, py, pz = (pl['x'], pl['y'], pl['z']) if isinstance(pl, dict) else (0.0, 20.0, 0.0)
        b = min(bearers, key=lambda n: math.hypot(n['position'][0] - px, n['position'][2] - pz))
        bx, by, bz = b['position']
        cx, cy, cz = bx + 0.05, by + 7.0, bz
        yaw, pitch = 0.0, -89.0
        res['bearer'] = {'name': b['name'], 'position': b['position']}
        print('  control bearer %s' % b['name'], flush=True)
        safe('POST', '/api/rpg/set_camera', {'detach': True, 'x': cx, 'y': cy, 'z': cz, 'yaw': yaw, 'pitch': pitch})
        time.sleep(8)
        frames = []
        for i, pol in enumerate(('never', 'night', 'never', 'night', 'never')):
            safe('POST', '/api/rpg/torches', {'default': pol})
            time.sleep(2.0)
            safe('POST', '/api/rpg/set_camera', {'detach': True, 'x': cx, 'y': cy, 'z': cz, 'yaw': yaw, 'pitch': pitch})
            time.sleep(1.0)
            state = safe('POST', '/api/rpg/torches', {})
            me = next((n for n in state.get('list', []) if n['name'] == b['name']), {})
            p = shot('control_%d_%s' % (i, 'on' if pol == 'night' else 'off'))
            moved = math.hypot(me.get('position', [bx, 0, bz])[0] - bx, me.get('position', [bx, 0, bz])[2] - bz)
            f = {'i': i, 'torch': pol == 'night', 'held': me.get('held'), 'carrying': me.get('carrying'),
                 'indoors': me.get('indoors'), 'moved_m': round(moved, 2),
                 'luma_centre': luma(p, (0.4, 0.3, 0.6, 0.7)) if p else None,
                 'luma_frame': luma(p) if p else None}
            frames.append(f)
            print('  control %d default=%-6s %s held=%-7r carrying=%-7r indoors=%-5s moved %.1f m  centre luma %s frame luma %s' % (
                i, pol, b['name'], me.get('held'), me.get('carrying'), me.get('indoors'), moved, f['luma_centre'], f['luma_frame']), flush=True)
        offs = [f['luma_centre'] for f in frames if not f['torch'] and f['luma_centre'] is not None]
        ons = [f['luma_centre'] for f in frames if f['torch'] and f['luma_centre'] is not None]
        if offs and ons:
            res['control_summary'] = {'off_mean': round(sum(offs) / len(offs), 2), 'off_spread': round(max(offs) - min(offs), 2),
                                      'on_mean': round(sum(ons) / len(ons), 2)}
            res['control_summary']['on_minus_off'] = round(res['control_summary']['on_mean'] - res['control_summary']['off_mean'], 2)
            print('  control summary: %s' % res['control_summary'], flush=True)
        res['control'] = frames
        safe('POST', '/api/rpg/set_camera', {'detach': False})
        safe('POST', '/api/rpg/torches', {'default': 'default'})
    ok = True
finally:
    proc.terminate()
    try:
        proc.wait(timeout=10)
    except Exception:
        proc.kill()
    log.close()
    (OUT / ('rv_torch_%s.json' % LABEL)).write_text(json.dumps(res, indent=1), encoding='utf-8')
    print('wrote', OUT / ('rv_torch_%s.json' % LABEL), 'ok' if ok else 'INCOMPLETE')
