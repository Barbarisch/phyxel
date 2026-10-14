"""Per-pose, per-time-of-day GPU pass attribution from a perf_harness.py sample run (jsonl).
Prints, for every pose and config, the median over repeats of the GPU frame and of every scope whose
median is >= MIN_MS, sorted by cost. Usage: attrib_table.py <run.jsonl> [min_ms=0.5]"""
import json, statistics, sys
from collections import defaultdict

path = sys.argv[1]
MIN_MS = float(sys.argv[2]) if len(sys.argv) > 2 else 0.5
cells = defaultdict(lambda: defaultdict(list))   # (pose, config) -> scope -> [median per window]
for line in open(path):
    r = json.loads(line)
    if 'pose' not in r or 'gpu_timing' not in r:
        continue
    key = (r['pose'], r['config'])
    g = r['gpu_timing']
    cells[key]['GPU Frame'].append((g.get('gpu_frame_ms') or {}).get('median_ms'))
    for s in g.get('scopes', []):
        cells[key][s['key']].append(s['median_ms'])
    ls = r.get('light_stats') or {}
    cells[key]['#lights uploaded'].append(ls.get('uploaded') or ls.get('uploaded_total'))
    cells[key]['#visible instances'].append(r.get('visible_instances'))

for (pose, cfg), scopes in cells.items():
    med = {k: statistics.median([v for v in vs if v is not None]) for k, vs in scopes.items()
           if any(v is not None for v in vs)}
    print(f'\n== {pose} / {cfg}: GPU frame {med.get("GPU Frame", float("nan")):.1f} ms  '
          f'(lights uploaded {med.get("#lights uploaded")}, visible instances {med.get("#visible instances")})')
    rows = sorted(((k, v) for k, v in med.items() if not k.startswith('#') and k != 'GPU Frame' and v >= MIN_MS),
                  key=lambda kv: -kv[1])
    for k, v in rows:
        print(f'   {v:8.2f}  {k}')
