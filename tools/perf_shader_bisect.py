"""perf_measure5.py - OVERDRAW or SHADER COST? (G-18 / G-52, run 5)

Static Geometry is fragment-bound at ~285 ms per megapixel near the ground. Two causes fit
that equally well and need completely different fixes:

  OVERDRAW / quad overshading - a huge NUMBER of fragment invocations, each cheap. Fixed by
      reducing near-field face density, a depth prepass, better culling.
  SHADER COST - a modest number of invocations, each expensive. Fixed in voxel.frag.

THE PROBE: debugShadowMode 11 makes voxel.frag return a flat colour as its FIRST statement,
so the pass still rasterises every triangle and still runs a fragment invocation for every
covered sample - it simply does no shading. Therefore:

  T(mode 11)              ~ rasterisation + invocation overhead   (the "how many" bill)
  T(mode 0) - T(mode 11)  ~ shading work                          (the "how expensive" bill)

If mode 11 is still very large, the invocation COUNT is the problem. If mode 11 collapses,
the shader body is.

CAVEAT recorded up front: mode 11 is a LOWER BOUND on the non-shader cost, because the
driver may also skip interpolating varyings this path never reads.
"""
import json
import statistics
import subprocess
import time
import urllib.request
from pathlib import Path

REL = Path(r"C:/Users/bpete/Documents/PhyxelProjects/Ravenmere/build_probe/Release")
OUT = Path(r"C:/Users/bpete/Documents/GitHub/phyxel/docs/evidence/ravenmere")
B = 'http://127.0.0.1:8113'
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
    for key in ('0|Scene Pass', '1|Static Geometry', '1|Grass', '0|Shadow Pass'):
        d, nm = key.split('|')
        out[nm] = med([s['ms'] for r in rows for s in r['scopes']
                       if s['name'] == nm and s['depth'] == int(d)])
    return out


log = open(OUT / 'rv_perf5.log', 'w', encoding='utf-8', errors='replace')
proc = subprocess.Popen([str(REL / 'Ravenmere.exe'), '--test', '8113'], cwd=str(REL),
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
    chunks = safe('GET', '/api/rpg/get_render_stats').get('visible_chunk_count', 0)
    assert chunks and chunks > 2, 'world did not stream (%s chunks)' % chunks

    st = safe('GET', '/api/state')
    pl = next((e for e in st.get('entities', []) if e.get('id') == 'player'), {})
    p = pl.get('position', {'x': 0.0, 'y': 17.0, 'z': 0.0})
    px, py, pz = p['x'], p['y'], p['z']

    POSES = [
        ('near_down', dict(detach=True, x=px, y=py + 1.7,  z=pz, yaw=0.0, pitch=-89.0)),
        ('street',    dict(detach=True, x=px, y=py + 1.7,  z=pz, yaw=0.0, pitch=-5.0)),
        ('far_down',  dict(detach=True, x=px, y=py + 45.0, z=pz, yaw=0.0, pitch=-89.0)),
        ('sky_up',    dict(detach=True, x=px, y=py + 1.7,  z=pz, yaw=0.0, pitch=80.0)),
    ]

    print('%-10s %-9s %12s %10s %s' % ('pose', 'mode', 'StaticGeom', 'fps', 'note'), flush=True)
    for name, cam in POSES:
        res[name] = {}
        safe('POST', '/api/rpg/set_camera', cam)
        time.sleep(3.0)
        for mode in (11, 12, 13, 14, 15, 16, 0):
            got = safe('POST', '/api/rpg/debug_shadow_mode', {'mode': mode})
            if got.get('mode') != mode:
                print('  mode set FAILED: %s' % json.dumps(got)[:90], flush=True)
            time.sleep(2.0)
            r = collect()
            res[name]['mode%d' % mode] = r
            if r:
                print('%-10s %-9s %12s %10s' %
                      (name, {0:'normal',11:'raster',12:'+tex',13:'+shadow',14:'+ambient',15:'+sun/moon',16:'+lights'}[mode],
                       r.get('Static Geometry'), r.get('fps')), flush=True)
            if mode == 99:
                sh = safe('GET', '/api/screenshot')
                if sh.get('path'):
                    (OUT / ('rv_perf5_probe_%s.png' % name)).write_bytes((REL / sh['path']).read_bytes())
        safe('POST', '/api/rpg/debug_shadow_mode', {'mode': 0})
        g=lambda m:(res[name].get('mode%d'%m) or {}).get('Static Geometry')
        m11,m12,m13,m14,m15,m16,m0 = g(11),g(12),g(13),g(14),g(15),g(16),g(0)
        if None not in (m11,m12,m13,m14,m15,m16,m0):
            steps=[('rasterise',m11),('textureGrad x2',m12-m11),('shadow PCSS',m13-m12),
                   ('ambient probe',m14-m13),('sun+moon PBR',m15-m14),
                   ('POINT/SPOT LIGHTS',m16-m15),('fog/haze/tonemap',m0-m16)]
            res[name]['breakdown']={k:round(v,3) for k,v in steps}
            print('   BREAKDOWN of %.1f ms:' % m0, flush=True)
            for k,v in steps:
                print('      %-22s %8.2f ms  %5.1f%%' % (k,v,100.0*v/m0 if m0 else 0), flush=True)
    safe('POST', '/api/rpg/set_camera', {'detach': False})
finally:
    proc.terminate()
    time.sleep(2)
    log.flush()

(OUT / 'rv_perf5.json').write_text(json.dumps(res, indent=2), encoding='utf-8')
print('\nwrote %s' % (OUT / 'rv_perf5.json'), flush=True)
