"""I6 check (docs/PerfProgram2026-09.md): a masked tier is NOT DRAWN, so its vertex work disappears.

Runs on the I5 rig (hand-placed, chunk (1,0,1)); camera aimed at the rig. Pipeline statistics ON.
PREDICTION, written before running: for each tier T masked in the MAIN pass, the STATIC slot's
vs_invocations falls by (T's faces_view_main) x (VS invocations per instance measured on the
all-on control, 4..6 depending on post-transform-cache reuse), and faces_view_main[T] reads 0.
A degenerate-vertex mask would leave vs_invocations unchanged; that is the failure this catches.
The same for the SHADOW mask against the SHADOW slot (mid cascade).
"""
import json, statistics, sys, time, urllib.request

B = 'http://127.0.0.1:8090'
TIERS = ['cube', 'sub', 'micro']


def call(m, p, b=None, t=60):
    r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(r, timeout=t))


def sample(n=12):
    """Median over n distinct frames (deduplicated by gpu_scopes serial)."""
    seen, rows = set(), []
    while len(rows) < n:
        time.sleep(0.05)
        s = call('GET', '/api/debug/gpu_scopes')
        if s.get('serial') in seen or not s.get('static_geometry_pipeline_stats'):
            continue
        seen.add(s['serial'])
        rows.append(s)
    vt = call('GET', '/api/debug/voxel_tiers')
    med = lambda slot: statistics.median(r[slot]['vs_invocations'] for r in rows if r.get(slot))
    return {'static_vs': med('static_geometry_pipeline_stats'), 'shadow_vs': med('shadow_pipeline_stats'),
            'main': {t: vt['tiers'][t]['faces_view_main'] for t in TIERS},
            'mid': {t: vt['tiers'][t]['shadow_faces_view']['mid'] for t in TIERS},
            'skipped': vt['masked_chunks_skipped'], 'overflow': vt['shadow_cmd_overflow']}


call('POST', '/api/camera', {'mode': 'free', 'position': {'x': 46, 'y': 23, 'z': 52}, 'yaw': -90, 'pitch': -15})
call('POST', '/api/debug/pipeline_stats', {'enabled': True})
time.sleep(1.5)

out = {}
out['all'] = base = sample()
ok = True
print('control all-on: static_vs', base['static_vs'], 'shadow_vs', base['shadow_vs'], 'main', base['main'], 'mid', base['mid'])
for i, tier in enumerate(TIERS):
    for which, slot, view in (('main', 'static_vs', 'main'), ('shadow', 'shadow_vs', 'mid')):
        mask = [True, True, True]
        mask[i] = False
        call('POST', '/api/debug/tier_mask', {which: mask})
        time.sleep(0.5)
        r = sample()
        out[f'{which}_no_{tier}'] = r
        call('POST', '/api/debug/tier_mask', {which: [True, True, True]})
        time.sleep(0.3)
        dropped_vs = base[slot] - r[slot]
        inst = base[view][tier]
        per = dropped_vs / inst if inst else float('nan')
        zeroed = r[view][tier] == 0
        good = zeroed and (inst == 0 or 3.5 <= per <= 36.5) and r['skipped'] == 0 and r['overflow'] == 0
        ok &= good
        print(f'{which:6s} mask -{tier:5s}: {view} instances {inst} -> {r[view][tier]}, {slot} {base[slot]} -> {r[slot]} '
              f'(dropped {dropped_vs}, {per:.2f} per instance) skipped={r["skipped"]} overflow={r["overflow"]} '
              f'{"OK" if good else "FAIL"}')
call('POST', '/api/debug/pipeline_stats', {'enabled': False})
json.dump(out, open(sys.argv[1] if len(sys.argv) > 1 else 'i6_check.json', 'w'))
print('I6 CHECK PASSED' if ok else 'I6 CHECK FAILED')
sys.exit(0 if ok else 1)
