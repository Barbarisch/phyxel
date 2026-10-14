"""Step 0 (docs/PerfProgram2026-09.md section 16.6): Release streaming throughput on the CityBench terrain,
to choose the route speed for THIS machine (the engine only clamps at 64 u/s).

Straight camera paths at y = 24 (inside chunk layer 0, which always holds ground on this terrain) along
+x, each run continuing east from where the last ended, so every run streams never-loaded terrain. For
each speed: fraction of chunk entries whose chunk was resident on entry, peak generation backlog, and
frame-time percentiles (Frame Interval). Prediction (written before running): 8-32 u/s keep every entry
resident; somewhere at or below 64 u/s entries start arriving unloaded and the backlog grows.
Usage: step0_streaming_throughput.py out.json"""
import json, math, statistics, sys

from rig_common import call, route, settle, wait_api

SPEEDS = [8, 16, 32, 48, 64]
LEG = 480.0
Y, Z = 24.0, -20.0

wait_api()
settle(300)
call('POST', '/api/debug/depth_prepass', {'enabled': True})
x0 = -40.0
out = {'world': 'CityBench (Perlin seed 7, streaming, default residency)', 'runs': []}
for sp in SPEEDS:
    wps = [{'x': x0, 'y': Y, 'z': Z, 'yaw': 0, 'pitch': -10}, {'x': x0 + LEG, 'y': Y, 'z': Z, 'yaw': 0, 'pitch': -10}]
    call('POST', '/api/camera', {'mode': 'free', 'position': {'x': x0, 'y': Y, 'z': Z}, 'yaw': 0, 'pitch': -10})
    settle(300)
    _, rec = route(wps, float(sp), stream_follow=True, max_frames=40000)
    rows = [r for r in rec['rows'] if r['path_progress'] >= 0]
    seen, entries = set(), []
    for r in rows:
        c = math.floor(r['camera'][0] / 32.0)
        if c not in seen:
            seen.add(c)
            entries.append(r['streaming']['camera_chunk_resident'])
    ft = sorted(r['frame_ms'] for r in rows if r['frame_ms'] > 0)
    res = {'speed_u_per_s': sp, 'from_x': x0, 'chunk_entries': len(entries),
           'entries_resident': sum(1 for e in entries if e), 'first_unresident_entry':
               next((i for i, e in enumerate(entries) if not e), None),
           'peak_pending_generation': max((r['streaming']['pending_generation'] for r in rows), default=0),
           'frames': len(rows), 'truncated': rec['truncated'],
           'frame_ms_p50': statistics.median(ft) if ft else None,
           'frame_ms_p99': ft[int(0.99 * (len(ft) - 1))] if ft else None,
           'frame_ms_max': ft[-1] if ft else None}
    out['runs'].append(res)
    print(json.dumps(res))
    x0 += LEG
ok_speeds = [r['speed_u_per_s'] for r in out['runs'] if r['entries_resident'] == r['chunk_entries']]
out['max_all_resident_speed'] = max(ok_speeds) if ok_speeds else None
json.dump(out, open(sys.argv[1], 'w'), indent=1)
print('fastest speed with every entry resident:', out['max_all_resident_speed'])
