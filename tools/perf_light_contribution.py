"""perf_measure7.py - IN RANGE vs ACTUALLY CONTRIBUTING (G-18 / G-52, run 7)

Run 6: 31.3 of the 32-light cap march at 91.6% of pixels, ~59M marches a frame, each cheap.
The gate is `dist < radius`. But the attenuation curve is
    1/(1 + 4.5 d/r + 75 d^2/r^2) * (1 - d/r)
which is 2.3% of peak at HALF the radius and 0.38% at 80% of it. So "in range" and
"contributes something a human can see" are very different questions.

Probe 19 counts, per fragment and BEFORE any march, how many in-range lights clear
1/255, 1/1024 and 1/4096 of a unit contribution (R, G, B).

WHY IT DECIDES THE PLAN:
  if contributing << in-range  -> a contribution cutoff before the march is the fix, and it
                                  is a few lines that change no visible pixel
  if contributing ~= in-range  -> the lights genuinely all matter; clustering or a different
                                  algorithm is needed, and the cheap gate would dim the scene

Also dumps the light buffer, so radii and intensities are facts rather than assumptions.
"""
import json
import statistics
import sys
import subprocess
import time
import urllib.request
from pathlib import Path

REL = Path(r"C:/Users/bpete/Documents/PhyxelProjects/Ravenmere/build_probe/Release")
OUT = Path(r"C:/Users/bpete/Documents/GitHub/phyxel/docs/evidence/ravenmere")
B = 'http://127.0.0.1:8115'
TAG = (sys.argv[1] if len(sys.argv) > 1 else 'rv_perf7').strip()


def safe(m, p, b=None, t=120):
    try:
        r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None,
                                   method=m, headers={'Content-Type': 'application/json'})
        return json.load(urllib.request.urlopen(r, timeout=t))
    except Exception as e:
        return {'err': str(e)[:200]}


def probe_counts(tag, mode):
    """Read a count probe with the tone curve OFF. Returns per-channel means over pixels
    that had any light in range (alpha/green marks those)."""
    safe('POST', '/api/rpg/set_tonemap', {'curve': 0, 'exposure': 1.0})
    safe('POST', '/api/rpg/debug_shadow_mode', {'mode': mode})
    time.sleep(2.0)
    r = safe('GET', '/api/screenshot')
    if not r.get('path'):
        return None
    dst = OUT / (TAG + '_%s_mode%d.png' % (tag, mode))
    dst.write_bytes((REL / r['path']).read_bytes())
    from PIL import Image
    im = Image.open(dst).convert('RGBA')
    px = im.load()
    w, h = im.size
    rs, gs, bs, lit, tot = [], [], [], 0, 0
    for y in range(0, h, 4):
        for x in range(0, w, 4):
            R, G, Bc, A = px[x, y]
            tot += 1
            if mode == 18:
                if G > 128:
                    lit += 1
                    rs.append(R / 255.0 * 32.0)
            else:
                if A > 128:
                    lit += 1
                    rs.append(R / 255.0 * 32.0)
                    gs.append(G / 255.0 * 32.0)
                    bs.append(Bc / 255.0 * 32.0)
    safe('POST', '/api/rpg/set_tonemap', {'curve': 1, 'exposure': 8.0})
    safe('POST', '/api/rpg/debug_shadow_mode', {'mode': 0})
    out = {'sampled': tot, 'lit': lit, 'lit_frac': round(lit / tot, 4) if tot else 0}
    if rs:
        out['mean_over_255'] = round(statistics.mean(rs), 2)
        out['max_over_255'] = round(max(rs), 2)
    if gs:
        out['mean_over_1024'] = round(statistics.mean(gs), 2)
    if bs:
        out['mean_over_4096'] = round(statistics.mean(bs), 2)
    return out


log = open(OUT / (TAG + '.log'), 'w', encoding='utf-8', errors='replace')
proc = subprocess.Popen([str(REL / 'Ravenmere.exe'), '--test', '8115'], cwd=str(REL),
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
    assert safe('GET', '/api/rpg/get_render_stats').get('visible_chunk_count', 0) > 2

    st = safe('GET', '/api/state')
    pl = next((e for e in st.get('entities', []) if e.get('id') == 'player'), {})
    p = pl.get('position', {'x': 0.0, 'y': 17.0, 'z': 0.0})
    px_, py_, pz_ = p['x'], p['y'], p['z']

    for name, cam in (('near_down', dict(detach=True, x=px_, y=py_ + 1.7, z=pz_, yaw=0.0, pitch=-89.0)),
                      ('street',    dict(detach=True, x=px_, y=py_ + 1.7, z=pz_, yaw=0.0, pitch=-5.0))):
        safe('POST', '/api/rpg/set_camera', cam)
        time.sleep(3.0)
        lights = safe('GET', '/api/rpg/list_uploaded_lights')
        pts = lights.get('points', [])
        radii = [q['radius'] for q in pts]
        inten = [q['intensity'] for q in pts]
        dists = [q['dist_to_viewer'] for q in pts]
        entry = {
            'registered': lights.get('registered_point_lights'),
            'uploaded': lights.get('num_point_lights'),
            'radius': {'min': round(min(radii), 2), 'median': round(statistics.median(radii), 2),
                       'max': round(max(radii), 2)} if radii else None,
            'intensity': {'min': round(min(inten), 2), 'median': round(statistics.median(inten), 2),
                          'max': round(max(inten), 2)} if inten else None,
            'dist_to_viewer': {'min': round(min(dists), 2), 'median': round(statistics.median(dists), 2),
                               'max': round(max(dists), 2)} if dists else None,
            'in_range': probe_counts(name, 18),
            'contributing': probe_counts(name, 19),
        }
        res[name] = entry
        ir = entry['in_range'] or {}
        cc = entry['contributing'] or {}
        print('%-10s registered=%s uploaded=%s  radius med=%s  dist med=%s' %
              (name, entry['registered'], entry['uploaded'],
               entry['radius']['median'] if entry['radius'] else '?',
               entry['dist_to_viewer']['median'] if entry['dist_to_viewer'] else '?'), flush=True)
        print('           IN RANGE  %s/px   CONTRIBUTING  >1/255: %s   >1/1024: %s   >1/4096: %s'
              % (ir.get('mean_over_255'), cc.get('mean_over_255'),
                 cc.get('mean_over_1024'), cc.get('mean_over_4096')), flush=True)
    safe('POST', '/api/rpg/set_camera', {'detach': False})
finally:
    proc.terminate()
    time.sleep(2)
    log.flush()

(OUT / (TAG + '.json')).write_text(json.dumps(res, indent=2), encoding='utf-8')
print('\nwrote %s' % (OUT / (TAG + '.json')), flush=True)
