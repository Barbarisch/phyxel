"""S-3 growth table (PerfProgram 2026-09 section 16.2/16.7): per pose, each GPU pass's median cost at noon
across the ladder rungs, plus night - noon for the GPU frame. Reads attrib_<prefix>.jsonl per rung.
Usage: growth_table.py [suffix]   e.g. growth_table.py _rebase  (reads attrib_<prefix><suffix>.jsonl)"""
import json, statistics, sys
from collections import defaultdict

SUFFIX = sys.argv[1] if len(sys.argv) > 1 else ''
# The first C-100 run was written as attrib_C100.jsonl; later runs use the rung prefix.
RUNGS = [('C-25', 25, f'attrib_city_C25M{SUFFIX}.jsonl'), ('C-50', 59, f'attrib_city_C50{SUFFIX}.jsonl'),
         ('C-75', 72, f'attrib_city_C75{SUFFIX}.jsonl'),
         ('C-100', 104, f'attrib_city_C100b{SUFFIX}.jsonl' if SUFFIX else 'attrib_C100.jsonl')]
SCOPES = ['GPU Frame', 'Scene Pass/Far Terrain', 'Shadow Pass/Shadow Mid', 'Shadow Pass/Shadow Near',
          'Shadow Pass/Shadow Far', 'Scene Pass/Static Geometry', 'Scene Pass/Foliage', 'Scene Pass/Grass',
          'GI Probes', 'OIT', 'Scene Pass/Entities/Characters']


def load(path):
    cells = defaultdict(lambda: defaultdict(list))
    for line in open(path):
        r = json.loads(line)
        if 'pose' not in r or 'gpu_timing' not in r:
            continue
        g = r['gpu_timing']
        c = cells[(r['pose'], r['config'])]
        c['GPU Frame'].append((g.get('gpu_frame_ms') or {}).get('median_ms'))
        for s in g.get('scopes', []):
            c[s['key']].append(s['median_ms'])
    return {k: {s: statistics.median([x for x in v if x is not None]) for s, v in c.items()
                if any(x is not None for x in v)} for k, c in cells.items()}


data = {name: load(path) for name, _, path in RUNGS}
poses = ['street', 'square', 'rooftop', 'overview', 'outside']
for pose in poses:
    print(f'\n### {pose} (noon, ms)')
    print('| pass | ' + ' | '.join(f'{n} ({b})' for n, b, _ in RUNGS) + ' |')
    print('|---|' + '---|' * len(RUNGS))
    for s in SCOPES:
        vals = [data[n].get((pose, 'noon'), {}).get(s) for n, _, _ in RUNGS]
        print(f'| {s} | ' + ' | '.join('-' if v is None else f'{v:.1f}' for v in vals) + ' |')
    nd = []
    for n, _, _ in RUNGS:
        a = data[n].get((pose, 'noon'), {}).get('GPU Frame')
        b = data[n].get((pose, 'night'), {}).get('GPU Frame')
        nd.append('-' if a is None or b is None else f'{b - a:+.1f}')
    print('| night - noon (GPU frame) | ' + ' | '.join(nd) + ' |')
