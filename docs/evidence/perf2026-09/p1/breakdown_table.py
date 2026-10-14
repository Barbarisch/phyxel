"""Per-scope GPU breakdown from a perf_harness `sample` jsonl (no A/B): median over reps of each scope's
240-frame median, per pose. Usage: breakdown_table.py run.jsonl"""
import json, statistics, sys
from collections import defaultdict

rows = [json.loads(l) for l in open(sys.argv[1])]
rows = [r for r in rows if 'pose' in r]
by = defaultdict(lambda: defaultdict(list))
frame = defaultdict(list)
order = []
for r in rows:
    g = r['gpu_timing']
    frame[r['pose']].append(g['gpu_frame_ms']['median_ms'])
    for s in g['scopes']:
        if s['key'] not in order:
            order.append(s['key'])
        # scopes that do not run every frame (Shadow Far) are reported at their per-frame share
        by[r['pose']][s['key']].append(s['median_ms'] * s['n'] / g['frames_used'])
poses = list(frame)
print('%-52s' % 'scope' + ''.join('%14s' % p for p in poses))
print('%-52s' % 'GPU Frame' + ''.join('%14.3f' % statistics.median(frame[p]) for p in poses))
for k in order:
    vals = [statistics.median(by[p][k]) if by[p][k] else float('nan') for p in poses]
    if max(vals) < 0.05:
        continue
    print('%-52s' % ('  ' * k.count('/') + k.split('/')[-1]) + ''.join('%14.3f' % v for v in vals))
