"""I5 rig (docs/PerfProgram2026-09.md): voxel_tiers must report ANALYTIC counts.

HAND-PLACED RIG (not generator output), inside ONE chunk: chunk (1,0,1), world x/z 32..63, placed at
y=20 in open air above the flat world's ground (top y=15). Every number is a DELTA against a control
census taken before anything is placed, so the ground and anything else in the world cancel.

PREDICTIONS, written before running:
  A  1 Stone cube at (40,20,40), isolated:       cube  stored +1,   faces_merged +6, unit_faces +6
  B  27 Stone subcubes filling cell (44,20,40):  sub   stored +27,  faces_merged +6, unit_faces +54
     (each face plane is a 3x3 of one material, so the greedy merge makes it one face)
  C  729 Stone microcubes filling (48,20,40):    micro stored +729, faces_merged +6, unit_faces +486
     (micro merge works within one cube, and this block is one cube)
  D  1 Stone cube at (52,20,40) + 9 Stone subcubes forming the sx=0 slab of cell (53,20,40):
     covered_cube_faces.covered +1 (the cube's +X face is fully hidden by the opaque slab)
If B or C comes out as a cube instead, the placement path coarsens full cells, and that is a
finding, not a rig error.
"""
import json, sys, time, urllib.request

B = 'http://127.0.0.1:8090'


def call(m, p, b=None, t=60):
    r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(r, timeout=t))


def settle():
    for _ in range(200):
        ls = call('GET', '/api/debug/load_state')['chunks']
        if not (ls['generation_pending'] or ls['remesh_pending'] or ls['remesh_idle_pending']):
            return
        time.sleep(0.1)
    raise SystemExit('never settled')


def census():
    settle()
    time.sleep(0.5)
    return call('GET', '/api/debug/voxel_tiers?covered=1')


def delta(a, b, tier, key):
    return b['tiers'][tier][key] - a['tiers'][tier][key]


c0 = census()
steps = []

call('POST', '/api/world/voxel', {'x': 40, 'y': 20, 'z': 40, 'material': 'Stone'})
c1 = census()
steps.append(('A cube', c0, c1, {'cube': (1, 6, 6)}))

subs = [{'x': 44, 'y': 20, 'z': 40, 'sx': a, 'sy': b, 'sz': c, 'material': 'Stone'}
        for a in range(3) for b in range(3) for c in range(3)]
print('subcubes batch:', json.dumps(call('POST', '/api/world/subcubes/batch', {'subcubes': subs}))[:160])
c2 = census()
steps.append(('B 27 subcubes', c1, c2, {'sub': (27, 6, 54)}))

micros = [{'x': 48, 'y': 20, 'z': 40, 'sx': a, 'sy': b, 'sz': c, 'mx': d, 'my': e, 'mz': f, 'material': 'Stone'}
          for a in range(3) for b in range(3) for c in range(3)
          for d in range(3) for e in range(3) for f in range(3)]
print('microcubes batch:', json.dumps(call('POST', '/api/world/microcubes/batch', {'microcubes': micros}, t=120))[:160])
c3 = census()
steps.append(('C 729 microcubes', c2, c3, {'micro': (729, 6, 486)}))

call('POST', '/api/world/voxel', {'x': 52, 'y': 20, 'z': 40, 'material': 'Stone'})
slab = [{'x': 53, 'y': 20, 'z': 40, 'sx': 0, 'sy': b, 'sz': c, 'material': 'Stone'} for b in range(3) for c in range(3)]
call('POST', '/api/world/subcubes/batch', {'subcubes': slab})
c4 = census()

ok = True
for name, a, b, expect in steps:
    for tier, (stored, merged, units) in expect.items():
        got = (delta(a, b, tier, 'stored'), delta(a, b, tier, 'faces_merged'), delta(a, b, tier, 'unit_faces_premerge'))
        good = got == (stored, merged, units)
        ok &= good
        print(f'{name:18s} {tier:5s} stored/merged/units: predicted {(stored, merged, units)} got {got} '
              f'{"OK" if good else "MISMATCH"}')
cov = c4['covered_cube_faces']['covered'] - c3['covered_cube_faces']['covered']
print(f'D covered skin      covered_cube_faces delta: predicted 1 got {cov} {"OK" if cov == 1 else "MISMATCH"}')
ok &= cov == 1
json.dump({'c0': c0, 'c1': c1, 'c2': c2, 'c3': c3, 'c4': c4}, open(sys.argv[1] if len(sys.argv) > 1 else 'i5_rig_census.json', 'w'))
print('RIG PASSED' if ok else 'RIG FAILED')
sys.exit(0 if ok else 1)
