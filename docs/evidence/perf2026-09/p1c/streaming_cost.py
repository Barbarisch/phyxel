"""Where does the frame go while terrain streams in? (docs/PerfProgram2026-09.md section 16.10)

Step 0 found frame p50 49-87 ms / p99 up to 344 ms while flying into never-loaded CityBench terrain at
8-48 u/s. One recorded route at 8 u/s east into fresh terrain (continuing past step 0's legs), raw
recording saved; per-phase median / p95 / max over the route, and the phase that dominates the worst
frames. Usage: streaming_cost.py out_prefix [start_x]"""
import json, statistics, sys

from rig_common import call, route, settle, wait_api

prefix = sys.argv[1]
x0 = float(sys.argv[2]) if len(sys.argv) > 2 else 2400.0
wps = [{'x': x0, 'y': 24, 'z': -20, 'yaw': 0, 'pitch': -10}, {'x': x0 + 320, 'y': 24, 'z': -20, 'yaw': 0, 'pitch': -10}]
wait_api()
call('POST', '/api/camera', {'mode': 'free', 'position': {'x': x0, 'y': 24, 'z': -20}, 'yaw': 0, 'pitch': -10})
settle(600)
_, rec = route(wps, 8.0, stream_follow=True, max_frames=20000)
json.dump(rec, open(prefix + '_raw.json', 'w'))
keys = rec['phase_keys']
rows = [r for r in rec['rows'] if r['path_progress'] >= 0 and r['frame_ms'] > 0]
per = {k: [r['phases'][i] for r in rows if r['phases'][i] is not None] for i, k in enumerate(keys)}
stats = []
for k, v in per.items():
    if not v:
        continue
    s = sorted(v)
    stats.append((statistics.median(s), s[int(0.95 * (len(s) - 1))], s[-1], len(s), k))
print('frames', len(rows), 'frame_ms p50', statistics.median(r['frame_ms'] for r in rows))
print('%8s %8s %8s %6s  %s' % ('p50', 'p95', 'max', 'n', 'phase'))
for p50, p95, mx, n, k in sorted(stats, reverse=True)[:25]:
    print('%8.2f %8.2f %8.2f %6d  %s' % (p50, p95, mx, n, k))
worst = sorted(rows, key=lambda r: -r['frame_ms'])[:10]
tops = []
for r in worst:
    cand = [(ms, k) for k, ms in zip(keys, r['phases']) if ms is not None and k not in ('Frame', 'Frame Interval', 'drawFrame', 'Frame/render', 'Frame/update', 'Frame/update/Update')]
    tops.append((round(r['frame_ms'], 1), sorted(cand, reverse=True)[:3]))
print('worst frames:')
for t in tops:
    print(' ', t)
json.dump({'frames': len(rows), 'stats': [dict(zip(('p50', 'p95', 'max', 'n', 'phase'), s)) for s in stats],
           'worst': tops}, open(prefix + '_summary.json', 'w'), indent=1)
