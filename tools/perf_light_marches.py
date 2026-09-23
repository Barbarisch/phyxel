"""perf_measure6.py - inside the light loop: how many marches, and what does each cost?
(G-18 / G-52, run 6)

Run 5 put 79-91% of the frame in the forward point/spot light loops. Two very different
problems fit that, and they need opposite fixes:

  TOO MANY LIGHTS  - many lights pass the radius + facing gates, so many marches run.
                     Fixed by culling/limiting lights, not by making the march cheaper.
  TOO COSTLY EACH  - few marches, but phxDdaHitsSolid walks up to 512 cells each time.
                     Fixed in the march, not by culling.

  mode 17  every gate and the BRDF, but vis forced to 1.0 (no march)
           -> mode0 - mode17 = the MARCH cost alone
  mode 18  reports the number of marches this fragment ran, linear in R (count/32)
           -> read with the tone curve OFF, or exposure x8 + AgX makes it meaningless

Cost per march = (mode0 - mode17) / (pixels * mean marches per lit pixel).
"""
import json
import statistics
import subprocess
import time
import urllib.request
from pathlib import Path

REL = Path(r"C:/Users/bpete/Documents/PhyxelProjects/Ravenmere/build_probe/Release")
OUT = Path(r"C:/Users/bpete/Documents/GitHub/phyxel/docs/evidence/ravenmere")
B = 'http://127.0.0.1:8114'
SAMPLES = 12


def safe(m, p, b=None, t=120):
    try:
        r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None,
                                   method=m, headers={'Content-Type': 'application/json'})
        return json.load(urllib.request.urlopen(r, timeout=t))
    except Exception as e:
        return {'err': str(e)[:200]}


def med(xs):
    xs = [x for x in xs if isinstance(x, (int, float))]
    return round(statistics.median(xs), 3) if xs else None


def static_ms():
    rows = []
    for _ in range(SAMPLES):
        rows.append(safe('GET', '/api/rpg/get_gpu_scopes'))
        time.sleep(0.2)
    rows = [r for r in rows if 'scopes' in r]
    if not rows:
        return None, None
    sg = med([s['ms'] for r in rows for s in r['scopes']
              if s['name'] == 'Static Geometry' and s['depth'] == 1])
    return sg, med([r.get('fps') for r in rows])


def march_histogram(tag):
    """Read probe 18 with the tone curve OFF and recover marches-per-pixel from R."""
    safe('POST', '/api/rpg/set_tonemap', {'curve': 0, 'exposure': 1.0})
    safe('POST', '/api/rpg/debug_shadow_mode', {'mode': 18})
    time.sleep(2.0)
    r = safe('GET', '/api/screenshot')
    if not r.get('path'):
        return None
    dst = OUT / ('rv_perf6_marchcount_%s.png' % tag)
    dst.write_bytes((REL / r['path']).read_bytes())
    from PIL import Image
    im = Image.open(dst).convert('RGB')
    px = im.load()
    w, h = im.size
    counts, lit = [], 0
    for y in range(0, h, 4):
        for x in range(0, w, 4):
            rr, gg, _ = px[x, y]
            if gg > 128:                      # G marks a pixel where some light was in range
                lit += 1
                counts.append(rr / 255.0 * 32.0)
    safe('POST', '/api/rpg/set_tonemap', {'curve': 1, 'exposure': 8.0})
    safe('POST', '/api/rpg/debug_shadow_mode', {'mode': 0})
    sampled = (w // 4) * (h // 4)
    return {'sampled_pixels': sampled, 'lit_pixels': lit,
            'lit_fraction': round(lit / sampled, 4) if sampled else None,
            'mean_marches_per_lit_px': round(statistics.mean(counts), 2) if counts else 0.0,
            'max_marches': round(max(counts), 2) if counts else 0.0,
            'screen': [w, h]}


log = open(OUT / 'rv_perf6.log', 'w', encoding='utf-8', errors='replace')
proc = subprocess.Popen([str(REL / 'Ravenmere.exe'), '--test', '8114'], cwd=str(REL),
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

    POSES = [
        ('near_down', dict(detach=True, x=px_, y=py_ + 1.7, z=pz_, yaw=0.0, pitch=-89.0)),
        ('street',    dict(detach=True, x=px_, y=py_ + 1.7, z=pz_, yaw=0.0, pitch=-5.0)),
    ]
    for name, cam in POSES:
        safe('POST', '/api/rpg/set_camera', cam)
        time.sleep(3.0)
        out = {}
        for mode in (0, 17):
            safe('POST', '/api/rpg/debug_shadow_mode', {'mode': mode})
            time.sleep(2.0)
            sg, fps = static_ms()
            out['mode%d' % mode] = {'static_ms': sg, 'fps': fps}
        safe('POST', '/api/rpg/debug_shadow_mode', {'mode': 0})
        hist = march_histogram(name)
        out['march_histogram'] = hist
        m0 = out['mode0']['static_ms']
        m17 = out['mode17']['static_ms']
        if None not in (m0, m17) and hist:
            march_ms = m0 - m17
            # total marches on screen = lit pixels x mean marches, scaled back up from the
            # 1-in-16 sampling grid
            total = hist['lit_pixels'] * 16 * hist['mean_marches_per_lit_px']
            out['march_ms'] = round(march_ms, 3)
            out['brdf_and_gates_ms'] = round(m17, 3)
            out['total_marches_on_screen'] = int(total)
            out['ns_per_march'] = round(march_ms * 1e6 / total, 1) if total else None
        res[name] = out
        print('%-10s full=%-8s no-march=%-7s  march=%-8s  lit=%s%%  marches/px=%s (max %s)  ns/march=%s'
              % (name, m0, m17, out.get('march_ms'),
                 round(100 * hist['lit_fraction'], 1) if hist else '?',
                 hist['mean_marches_per_lit_px'] if hist else '?',
                 hist['max_marches'] if hist else '?', out.get('ns_per_march')), flush=True)
    safe('POST', '/api/rpg/set_camera', {'detach': False})
finally:
    proc.terminate()
    time.sleep(2)
    log.flush()

(OUT / 'rv_perf6.json').write_text(json.dumps(res, indent=2), encoding='utf-8')
print('\nwrote %s' % (OUT / 'rv_perf6.json'), flush=True)
