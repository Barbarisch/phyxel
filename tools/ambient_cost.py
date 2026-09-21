"""ambient_cost.py — where does the ambient model's GPU time actually go?

Reads GET /api/debug/gpu_scopes, which the engine already fills every frame with per-pass GPU
timings, including a scope named "GI Probes" wrapping the probe compute dispatch.

At a fixed pose, with the probe field ON and OFF:
  "GI Probes"                         = the probe compute pass, in isolation
  frame delta (ON minus OFF) minus it = the PER-FRAGMENT cost of ambient, i.e. the probe lookups
                                        and the visibility test inside the receiving shaders
Everything else (shadows, grass, foliage) is reported alongside, so the ambient cost can be judged
against what the frame already spends rather than in a vacuum.

PREDICTION, written before running (2026-09-20): probe compute 2-3 ms, per-fragment 7-8 ms. If the
compute pass turns out to be the larger share, my reasoning about where the work sits was wrong.

Usage: ambient_cost.py <label> [samples]
"""
import json, sys, time, urllib.request
from statistics import median

B = 'http://127.0.0.1:8090'
LABEL = sys.argv[1] if len(sys.argv) > 1 else 'run'
N = int(sys.argv[2]) if len(sys.argv) > 2 else 20


def call(m, p, b=None, t=60):
    r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None,
                               method=m, headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(r, timeout=t))


def sample(n):
    """Median ms per top-level scope over n frames, plus the median total GPU frame time."""
    acc, frames = {}, []
    for _ in range(n):
        time.sleep(0.25)
        for s in call('GET', '/api/debug/gpu_scopes').get('scopes', []):
            # Every scope, at any depth. Scopes NEST (Static Geometry / Grass / Foliage sit inside
            # the scene pass at depth 1), so these must never be summed; the total comes from
            # engine_timing's gpuFrameTime instead.
            acc.setdefault((int(s.get('depth', 0)), s['name']), []).append(float(s['ms']))
        frames.append(float(call('GET', '/api/debug/engine_timing').get('gpuFrameTime', 0.0)))
    return {k: median(v) for k, v in acc.items() if v}, median(frames)


def main():
    runs = {True: [], False: []}
    for on in (True, False, True):
        call('POST', '/api/debug/gi', {'enabled': on})
        time.sleep(6.0)
        scopes, frame = sample(N)
        runs[on].append((scopes, frame))
        print('  sampled field %s: frame %.2f ms' % ('ON ' if on else 'OFF', frame), flush=True)

    on_s = runs[True][0][0]
    on_f = median([f for _, f in runs[True]])
    off_s, off_f = runs[False][0]
    names = sorted(set(on_s) | set(off_s), key=lambda k: -max(on_s.get(k, 0.0), off_s.get(k, 0.0)))

    print('\n%-26s %10s %10s %10s' % ('GPU scope', 'field ON', 'field OFF', 'delta'))
    for k in names:
        a, b = on_s.get(k, 0.0), off_s.get(k, 0.0)
        if max(a, b) < 0.05:
            continue
        print('%-26s %9.2f  %9.2f  %+9.2f' % (k[:26], a, b, a - b))
    print('%-26s %9.2f  %9.2f  %+9.2f' % ('TOTAL GPU FRAME', on_f, off_f, on_f - off_f))

    probe = max([v for k, v in on_s.items() if k[1] == 'GI Probes'] or [0.0])
    per_frag = (on_f - off_f) - probe
    print('\n  probe compute pass        %6.2f ms' % probe)
    print('  per-fragment ambient      %6.2f ms   (frame delta %.2f, minus the probe pass)'
          % (per_frag, on_f - off_f))
    tot = probe + per_frag
    if tot > 0.01:
        print('  split: compute %.0f%%  /  per-fragment %.0f%%' % (100 * probe / tot, 100 * per_frag / tot))

    call('POST', '/api/debug/gi', {'enabled': True})
    out = {'label': LABEL, 'samples_per_state': N,
           'field_on_scopes': {'%d:%s' % k: v for k, v in on_s.items()},
           'field_off_scopes': {'%d:%s' % k: v for k, v in off_s.items()},
           'frame_on_ms': on_f, 'frame_off_ms': off_f,
           'probe_compute_ms': probe, 'per_fragment_ms': per_frag}
    p = 'docs/evidence/ambient_cost_%s.json' % LABEL
    open(p, 'w', encoding='utf-8').write(json.dumps(out, indent=2))
    print('\n  wrote', p)


if __name__ == '__main__':
    main()
