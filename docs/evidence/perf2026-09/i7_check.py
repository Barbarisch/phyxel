"""I7 check (docs/PerfProgram2026-09.md): the CPU scopes account for the frame; mesh timing sees micro.

PREDICTIONS, written before running:
  1 SUM RULE: the median of drawFrame's direct children sums to within 5% of drawFrame's median.
    (Medians do not add exactly, so the check is on per-frame means as well: sum of child means
    within 5% of the drawFrame mean.) A larger gap means untimed work inside drawFrame.
  2 MESH: after reset, placing 729 microcubes (one full cube cell, HAND-PLACED rig in chunk (1,0,1))
    produces >= 1 rebuild in the "100-999" microcube bucket, and the mean phase times sum to the mean
    rebuild time within 1% (the phases partition the rebuild by construction).
"""
import json, sys, time, urllib.request

B = 'http://127.0.0.1:8090'


def call(m, p, b=None, t=60):
    r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(r, timeout=t))


ok = True
time.sleep(5)   # let the ring fill with 240 steady frames
ct = call('GET', '/api/debug/cpu_timing?frames=240')
sc = {s['key']: s for s in ct['scopes']}
frame = sc['drawFrame']
kids = [s for s in ct['scopes'] if s['depth'] == 1]
sum_mean = sum(s['mean_ms'] for s in kids)
ratio = sum_mean / frame['mean_ms'] if frame['mean_ms'] else 0
print(f"drawFrame mean {frame['mean_ms']:.3f} ms, children sum {sum_mean:.3f} ms ({ratio*100:.1f}%)")
for s in sorted(kids, key=lambda s: -s['mean_ms']):
    print(f"   {s['key']:40s} mean {s['mean_ms']:.3f}  median {s['median_ms']:.3f}  p99 {s['p99_ms']:.3f}  n={s['n']}")
good = 0.95 <= ratio <= 1.0001
ok &= good
print('SUM RULE', 'OK' if good else 'FAIL')

call('GET', '/api/debug/mesh_timing?reset=1')
micros = [{'x': 48, 'y': 24, 'z': 44, 'sx': a, 'sy': b, 'sz': c, 'mx': d, 'my': e, 'mz': f, 'material': 'Stone'}
          for a in range(3) for b in range(3) for c in range(3) for d in range(3) for e in range(3) for f in range(3)]
call('POST', '/api/world/microcubes/batch', {'microcubes': micros}, t=120)
for _ in range(100):
    ls = call('GET', '/api/debug/load_state')['chunks']
    if not (ls['remesh_pending'] or ls['remesh_idle_pending'] or ls['generation_pending']):
        break
    time.sleep(0.1)
mt = call('GET', '/api/debug/mesh_timing')
print(json.dumps(mt, indent=1)[:1500])
bucket = {b['microcubes']: b for b in mt['by_microcube_count']}
got = bucket['100-999']['rebuilds']
phase_mean = sum(p['mean_ms'] for p in mt['phases'])
whole_mean = (sum(b['mean_ms'] * b['rebuilds'] for b in mt['by_microcube_count']) / mt['rebuilds']) if mt['rebuilds'] else 0
good2 = got >= 1 and whole_mean > 0 and abs(phase_mean - whole_mean) <= max(0.01 * whole_mean, 0.002)
ok &= good2
print(f'MESH: 100-999 bucket rebuilds {got}; phase-mean sum {phase_mean:.4f} vs rebuild mean {whole_mean:.4f} ms',
      'OK' if good2 else 'FAIL')
json.dump({'cpu_timing': ct, 'mesh_timing': mt}, open(sys.argv[1] if len(sys.argv) > 1 else 'i7_check.json', 'w'))
print('I7 CHECK PASSED' if ok else 'I7 CHECK FAILED')
sys.exit(0 if ok else 1)
