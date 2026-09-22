"""perf_light_ab.py - the SAME measurement before and after the G-158 merge.

Usage:  python tools/perf_light_ab.py before
        python tools/perf_light_ab.py after

Captures, at two fixed poses derived from the deterministic player spawn:
  * a NORMAL render (shipped tone curve) - the visual claim needs the change in frame
  * the raw uploaded light table          - rows vs DISTINCT positions (the bug itself)
  * probe 18 marches per lit pixel        - the per-fragment cost driver
  * Static Geometry ms at mode 0          - what it is actually worth

Everything lands in docs/evidence/ravenmere/g158_<tag>.json plus PNGs, so the two runs can be
diffed field by field instead of by recollection.
"""
import json
import statistics
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

TAG = (sys.argv[1] if len(sys.argv) > 1 else 'before').strip()
REL = Path(r"C:/Users/bpete/Documents/PhyxelProjects/Ravenmere/build_probe/Release")
OUT = Path(r"C:/Users/bpete/Documents/GitHub/phyxel/docs/evidence/ravenmere")
PORT = 8117
B = 'http://127.0.0.1:%d' % PORT
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


def shot(name, curve, exposure):
    safe('POST', '/api/rpg/set_tonemap', {'curve': curve, 'exposure': exposure})
    time.sleep(1.5)
    r = safe('GET', '/api/screenshot')
    if not r.get('path'):
        return None
    dst = OUT / ('g158_%s_%s.png' % (TAG, name))
    dst.write_bytes((REL / r['path']).read_bytes())
    return dst.name


def marches(name):
    """Probe 18, tone curve OFF - R carries the march count, G marks a lit pixel."""
    safe('POST', '/api/rpg/set_tonemap', {'curve': 0, 'exposure': 1.0})
    safe('POST', '/api/rpg/debug_shadow_mode', {'mode': 18})
    time.sleep(2.0)
    r = safe('GET', '/api/screenshot')
    out = None
    if r.get('path'):
        dst = OUT / ('g158_%s_%s_marches.png' % (TAG, name))
        dst.write_bytes((REL / r['path']).read_bytes())
        from PIL import Image
        im = Image.open(dst).convert('RGB')
        px = im.load()
        w, h = im.size
        counts, lit = [], 0
        for y in range(0, h, 4):
            for x in range(0, w, 4):
                rr, gg, _ = px[x, y]
                if gg > 128:
                    lit += 1
                    counts.append(rr / 255.0 * 32.0)
        sampled = (w // 4) * (h // 4)
        out = {'lit_fraction': round(lit / sampled, 4) if sampled else None,
               'mean_marches_per_lit_px': round(statistics.mean(counts), 2) if counts else 0.0,
               'max_marches': round(max(counts), 2) if counts else 0.0}
    safe('POST', '/api/rpg/debug_shadow_mode', {'mode': 0})
    safe('POST', '/api/rpg/set_tonemap', {'curve': 1, 'exposure': 8.0})
    return out


log = open(OUT / ('g158_%s.log' % TAG), 'w', encoding='utf-8', errors='replace')
proc = subprocess.Popen([str(REL / 'Ravenmere.exe'), '--test', str(PORT)], cwd=str(REL),
                        stdout=log, stderr=subprocess.STDOUT)
res = {'tag': TAG, 'exe_mtime': time.strftime('%Y-%m-%d %H:%M:%S',
                                              time.localtime((REL / 'Ravenmere.exe').stat().st_mtime))}
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
    ch = safe('GET', '/api/rpg/get_render_stats').get('visible_chunk_count', 0)
    assert ch > 2, 'world did not stream (%s chunks)' % ch
    res['visible_chunks'] = ch

    st = safe('GET', '/api/state')
    pl = next((e for e in st.get('entities', []) if e.get('id') == 'player'), {})
    p = pl.get('position', {'x': 0.0, 'y': 17.0, 'z': 0.0})
    px_, py_, pz_ = p['x'], p['y'], p['z']
    res['player'] = {'x': px_, 'y': py_, 'z': pz_}

    POSES = [
        ('near_down', dict(detach=True, x=px_, y=py_ + 1.7, z=pz_, yaw=0.0, pitch=-89.0)),
        ('street',    dict(detach=True, x=px_, y=py_ + 1.7, z=pz_, yaw=0.0, pitch=-5.0)),
    ]
    for name, cam in POSES:
        safe('POST', '/api/rpg/set_camera', cam)
        time.sleep(3.0)
        lights = safe('GET', '/api/rpg/list_uploaded_lights')
        pts = lights.get('points', [])
        distinct = len({(round(q['rel_x'], 3), round(q['rel_y'], 3), round(q['rel_z'], 3))
                        for q in pts})
        sg, fps = static_ms()
        entry = {
            'camera': {k: v for k, v in cam.items() if k != 'detach'},
            'registered_point_lights': lights.get('registered_point_lights'),
            'uploaded': lights.get('num_point_lights'),
            'rows_returned': len(pts),
            'distinct_positions': distinct,
            'radius_median': round(statistics.median([q['radius'] for q in pts]), 2) if pts else None,
            'static_geometry_ms': sg,
            'fps': fps,
            'render': shot(name, 1, 8.0),
            'marches': marches(name),
        }
        res[name] = entry
        m = entry['marches'] or {}
        print('%-10s registered=%-5s uploaded=%-3s rows=%-3s DISTINCT=%-3s | StaticGeom=%-8s fps=%-6s | marches/px=%-6s lit=%s'
              % (name, entry['registered_point_lights'], entry['uploaded'], entry['rows_returned'],
                 distinct, sg, fps, m.get('mean_marches_per_lit_px'), m.get('lit_fraction')),
              flush=True)
    safe('POST', '/api/rpg/set_camera', {'detach': False})
finally:
    proc.terminate()
    time.sleep(2)
    log.flush()

(OUT / ('g158_%s.json' % TAG)).write_text(json.dumps(res, indent=2), encoding='utf-8')
print('wrote %s' % (OUT / ('g158_%s.json' % TAG)), flush=True)
