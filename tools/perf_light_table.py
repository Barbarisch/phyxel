"""perf_measure8.py - WHAT ARE THE 143 LIGHTS? (G-18/G-52, run 8)

Run 7 summarised the uploaded light table as min==median==max on radius, intensity AND
distance-to-viewer. Identical distance to 2dp across 32 lights is not a plausible
measurement, so run 7's light table is treated as UNVERIFIED until this dumps the raw rows.

The question that decides the plan: ChunkRenderManager.cpp:505 emits ONE point light per
emissive voxel CELL (e.worldPos = worldCell + 0.5), with no merging. If a torch is several
emissive voxels, the 143 registered lights are really a few dozen fixtures counted many
times over, and merging co-located emitters is a large, safe win. If they are one-per-
fixture, merging buys nothing and the cost must be attacked structurally.

Reports, from the RAW rows: distinct positions, nearest-neighbour spacing, and a clustering
pass at the cell/subcube/microcube scales.
"""
import json, math, statistics, subprocess, time, urllib.request
from pathlib import Path

REL = Path(r"C:/Users/bpete/Documents/PhyxelProjects/Ravenmere/build_probe/Release")
OUT = Path(r"C:/Users/bpete/Documents/GitHub/phyxel/docs/evidence/ravenmere")
B = 'http://127.0.0.1:8116'

def safe(m, p, b=None, t=120):
    try:
        r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None,
                                   method=m, headers={'Content-Type': 'application/json'})
        return json.load(urllib.request.urlopen(r, timeout=t))
    except Exception as e:
        return {'err': str(e)[:200]}

def cluster(pts, eps):
    """Single-link clustering at radius eps - how many DISTINCT fixtures these rows represent."""
    n = len(pts); seen = [False]*n; groups = []
    for i in range(n):
        if seen[i]: continue
        stack, grp = [i], []
        seen[i] = True
        while stack:
            k = stack.pop(); grp.append(k)
            for j in range(n):
                if not seen[j]:
                    d = math.dist(pts[k], pts[j])
                    if d <= eps:
                        seen[j] = True; stack.append(j)
        groups.append(grp)
    return groups

log = open(OUT / 'rv_perf8.log', 'w', encoding='utf-8', errors='replace')
proc = subprocess.Popen([str(REL / 'Ravenmere.exe'), '--test', '8116'], cwd=str(REL),
                        stdout=log, stderr=subprocess.STDOUT)
res = {}
try:
    for _ in range(180):
        if 'err' not in safe('GET', '/api/state', t=3): break
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
                reached = True; break
            time.sleep(1)
        if reached: break
    assert reached, 'never reached the town'
    time.sleep(6)
    ch = safe('GET', '/api/rpg/get_render_stats').get('visible_chunk_count', 0)
    assert ch > 2, 'world did not stream (%s chunks)' % ch

    st = safe('GET', '/api/state')
    pl = next((e for e in st.get('entities', []) if e.get('id') == 'player'), {})
    p = pl.get('position', {'x': 0.0, 'y': 17.0, 'z': 0.0})
    px_, py_, pz_ = p['x'], p['y'], p['z']
    res['player'] = {'x': px_, 'y': py_, 'z': pz_}
    res['visible_chunks'] = ch

    safe('POST', '/api/rpg/set_camera',
         dict(detach=True, x=px_, y=py_+1.7, z=pz_, yaw=0.0, pitch=-89.0))
    time.sleep(3.0)
    lights = safe('GET', '/api/rpg/list_uploaded_lights')
    pts = lights.get('points', [])
    res['raw'] = lights
    print('registered=%s  uploaded=%s  rows_returned=%d'
          % (lights.get('registered_point_lights'), lights.get('num_point_lights'), len(pts)), flush=True)
    if pts:
        print('\nfirst 8 raw rows:', flush=True)
        for q in pts[:8]:
            print('   rel=(%8.3f %8.3f %8.3f) r=%.2f i=%.2f d=%.3f'
                  % (q['rel_x'], q['rel_y'], q['rel_z'], q['radius'], q['intensity'],
                     q['dist_to_viewer']), flush=True)
        P = [(q['rel_x'], q['rel_y'], q['rel_z']) for q in pts]
        uniq = sorted(set(P))
        print('\nrows=%d  DISTINCT positions=%d' % (len(P), len(uniq)), flush=True)
        ds = sorted(q['dist_to_viewer'] for q in pts)
        print('dist_to_viewer  min=%.3f  median=%.3f  max=%.3f  distinct=%d'
              % (ds[0], statistics.median(ds), ds[-1], len(set(round(d,3) for d in ds))), flush=True)
        rs = sorted(set(round(q['radius'],3) for q in pts))
        its = sorted(set(round(q['intensity'],3) for q in pts))
        print('distinct radii=%s   distinct intensities=%s' % (rs[:8], its[:8]), flush=True)
        # nearest-neighbour spacing between DISTINCT emitter positions
        if len(uniq) > 1:
            nn = []
            for i, a in enumerate(uniq):
                nn.append(min(math.dist(a, b) for j, b in enumerate(uniq) if j != i))
            print('nearest-neighbour between distinct emitters: min=%.3f median=%.3f max=%.3f'
                  % (min(nn), statistics.median(nn), max(nn)), flush=True)
        for eps, label in ((0.34, 'subcube 1/3'), (1.01, 'one cube'), (2.01, 'two cubes')):
            g = cluster(uniq, eps)
            print('cluster @ %-12s eps=%.2f -> %d fixtures (largest %d emitters)'
                  % (label, eps, len(g), max(len(x) for x in g)), flush=True)
            res.setdefault('clusters', {})[label] = {'eps': eps, 'fixtures': len(g),
                                                     'largest': max(len(x) for x in g)}
        res['distinct_positions'] = len(uniq)
        res['rows'] = len(P)
    safe('POST', '/api/rpg/set_camera', {'detach': False})
finally:
    proc.terminate(); time.sleep(2); log.flush()

(OUT / 'rv_perf8.json').write_text(json.dumps(res, indent=2), encoding='utf-8')
print('\nwrote %s' % (OUT / 'rv_perf8.json'), flush=True)
