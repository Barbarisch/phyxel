"""perf_town_profile.py - attribute the town's frame time on the SHIPPED build (G-18/G-52).

Keep this rig. The measurement phase it came from is written up in the Ravenmere gap
ledger under G-18; re-run it before and after any render change so a claim about the
frame rate is a measurement rather than an argument.

It answers FRAGMENT-bound vs DRAW-bound by scaling the render resolution:

Established so far: Scene Pass is the frame; Static Geometry is ~all of it; it is not
per-chunk (263 ms with 2 chunks visible vs 1.7 ms with 17); the ambient/GI lookup is only
15-22 ms of it.

THE TEST: scale the render resolution and watch Static Geometry.
  * If cost is proportional to pixel count -> FRAGMENT bound (shader work per pixel, or
    overdraw). Halving the pixels should roughly halve the time.
  * If cost barely moves -> VERTEX or DRAW bound, and resolution is irrelevant.
This replaces the pipeline-statistics query, which crashes the NVIDIA driver in this scene
(0xC0000005 in nvoglv64.dll).

The window resize path is the one verified under G-50, so the swapchain really does change
size; each size is read back from a screenshot rather than assumed.
"""
import ctypes
import ctypes.wintypes as wt
import json
import statistics
import subprocess
import time
import urllib.request
from pathlib import Path

REL = Path(r"C:/Users/bpete/Documents/PhyxelProjects/Ravenmere/build_probe/Release")
OUT = Path(r"C:/Users/bpete/Documents/GitHub/phyxel/docs/evidence/ravenmere")
B = 'http://127.0.0.1:8112'
SAMPLES = 12
user32 = ctypes.windll.user32


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


def find_window(pid):
    found = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)
    def cb(hwnd, _):
        wpid = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
        if wpid.value == pid and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    user32.EnumWindows(cb, 0)
    return found[0] if found else None


def collect():
    rows = []
    for _ in range(SAMPLES):
        rows.append(safe('GET', '/api/rpg/get_gpu_scopes'))
        time.sleep(0.2)
    rows = [r for r in rows if 'scopes' in r]
    if not rows:
        return None
    out = {'fps': med([r.get('fps') for r in rows]),
           'visible_chunks': med([r.get('visible_chunks') for r in rows])}
    for key in ('0|Scene Pass', '1|Static Geometry', '1|Grass', '0|GI Probes', '0|Shadow Pass'):
        d, nm = key.split('|')
        out[nm] = med([s['ms'] for r in rows for s in r['scopes']
                       if s['name'] == nm and s['depth'] == int(d)])
    return out


log = open(OUT / 'rv_perf4.log', 'w', encoding='utf-8', errors='replace')
proc = subprocess.Popen([str(REL / 'Ravenmere.exe'), '--test', '8112'], cwd=str(REL),
                        stdout=log, stderr=subprocess.STDOUT)
res = {}
try:
    for _ in range(180):
        if 'err' not in safe('GET', '/api/state', t=3):
            break
        time.sleep(1)
    reached = False
    for _attempt in range(6):
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
    chunks = safe('GET', '/api/rpg/get_render_stats').get('visible_chunk_count', 0)
    assert chunks and chunks > 2, 'world did not stream (%s chunks)' % chunks

    st = safe('GET', '/api/state')
    pl = next((e for e in st.get('entities', []) if e.get('id') == 'player'), {})
    p = pl.get('position', {'x': 0.0, 'y': 17.0, 'z': 0.0})
    px, py, pz = p['x'], p['y'], p['z']
    hwnd = find_window(proc.pid)
    assert hwnd

    POSES = [
        ('near_down', dict(detach=True, x=px, y=py + 1.7, z=pz, yaw=0.0, pitch=-89.0)),
        ('street',    dict(detach=True, x=px, y=py + 1.7, z=pz, yaw=0.0, pitch=-5.0)),
    ]
    SIZES = [(1600, 900), (1100, 640), (760, 460)]

    print('%-10s %-11s %10s %12s %10s %9s' %
          ('pose', 'swapchain', 'Mpixels', 'StaticGeom', 'ms/Mpix', 'fps'), flush=True)
    for name, cam in POSES:
        res[name] = []
        safe('POST', '/api/rpg/set_camera', cam)
        time.sleep(2.0)
        for (ww, wh) in SIZES:
            user32.SetWindowPos(hwnd, 0, 80, 50, ww, wh, 0x0004 | 0x0010)
            time.sleep(3.0)
            safe('POST', '/api/rpg/set_camera', cam)   # re-assert after the resize
            time.sleep(2.5)
            sh = safe('GET', '/api/screenshot')
            sw = shh = 0
            if sh.get('path'):
                from PIL import Image
                sw, shh = Image.open(REL / sh['path']).size
            r = collect()
            if not r:
                print('  %-10s %-11s no data' % (name, '%dx%d' % (ww, wh)), flush=True)
                continue
            mpix = (sw * shh) / 1e6 if sw else None
            sg = r.get('Static Geometry')
            per = round(sg / mpix, 1) if (mpix and sg) else None
            r.update({'window': [ww, wh], 'swapchain': [sw, shh], 'mpixels': round(mpix, 3) if mpix else None,
                      'ms_per_mpixel': per})
            res[name].append(r)
            print('%-10s %-11s %10s %12s %10s %9s'
                  % (name, '%dx%d' % (sw, shh), round(mpix, 3) if mpix else '-', sg, per, r['fps']),
                  flush=True)
    safe('POST', '/api/rpg/set_camera', {'detach': False})
finally:
    proc.terminate()
    time.sleep(2)
    log.flush()

(OUT / 'rv_perf4.json').write_text(json.dumps(res, indent=2), encoding='utf-8')
print('\nIf ms/Mpixel is roughly CONSTANT across sizes, the pass is fragment-bound.', flush=True)
