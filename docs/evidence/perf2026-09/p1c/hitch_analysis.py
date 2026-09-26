"""Hitch detection for route recordings (docs/PerfProgram2026-09.md section 16.2).

A HITCH is a SPIKE against its local neighbourhood, not a frame slower than the route's global median:
a route legitimately passes from cheap views to expensive ones, and V5's control run (no fill) showed a
global-median rule flagging 210 frames of a sustained 3.7 -> 8.5 ms GPU-bound view change as "hitches".
Rule: frame_ms > 2 x the median of the SAME-PARITY frames within +-0.5 s (the frame itself excluded;
parity because the streaming pump alternates frames by design) AND at least 4 ms above that median
(so a 1 ms -> 2.1 ms blip at very high fps is not a hitch).
The cause is the largest non-root phase of the hitch frame (main-loop scopes + render-path scopes)."""
import statistics

ROOTS = ('Frame', 'Frame Interval', 'drawFrame')


def frame_times(rows):
    t, out = 0.0, []
    for r in rows:
        t += (r['frame_ms'] or 0.0) / 1000.0
        out.append(t)
    return out


def hitches(rec, window=None, half_window_s=0.5, ratio=2.0, min_excess_ms=4.0):
    keys = rec['phase_keys']
    rows = [r for r in rec['rows'] if r['frame_ms'] and r['frame_ms'] > 0]
    times = frame_times(rows)
    out = []
    lo = 0
    for i, r in enumerate(rows):
        if window and not (window[0] <= times[i] <= window[1]):
            continue
        while times[lo] < times[i] - half_window_s:
            lo += 1
        hi = i
        while hi + 1 < len(rows) and times[hi + 1] <= times[i] + half_window_s:
            hi += 1
        neigh = [rows[j]['frame_ms'] for j in range(lo, hi + 1) if j != i and j % 2 == i % 2]
        if len(neigh) < 5:
            continue
        med = statistics.median(neigh)
        if r['frame_ms'] > ratio * med and r['frame_ms'] - med >= min_excess_ms:
            cand = [(ms, k) for k, ms in zip(keys, r['phases']) if ms is not None and k not in ROOTS]
            out.append({'t_s': round(times[i], 3), 'frame_ms': round(r['frame_ms'], 2),
                        'local_median_ms': round(med, 2),
                        'top_phases': [(k, round(ms, 2)) for ms, k in sorted(cand, reverse=True)[:4]]})
    return out
