"""S-2 setup (docs/PerfProgram2026-09.md §4.1): engine-generated town on M4DensityBench.

Engine must already be running with --project .../M4DensityBench. Builds the town through the
engine's own generator and REFUSES to continue unless the result matches the recorded recipe
(21/21 units complete, 119 point lights at 36 unique positions), so P1 never measures a
different scene by accident."""
import json, sys, time, urllib.request

B = 'http://127.0.0.1:8090'
RECIPE = {"era": "medieval", "tier": "town", "seed": 7, "position": {"x": -40, "y": 16, "z": -20},
          "width": 80, "depth": 40, "terrain": True, "density": 1.0}


def call(m, p, b=None, t=60):
    r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(r, timeout=t))


sub = call('POST', '/api/settlement/build', RECIPE)
job = sub.get('job_id')
print('submitted job', job, 'queued builds', len(sub.get('queued_builds', [])))
for _ in range(240):
    j = [x for x in call('GET', '/api/jobs')['jobs'] if x['id'] == job][0]
    if j['state'] in ('complete', 'failed', 'cancelled'):
        break
    time.sleep(3)
json.dump({'recipe': RECIPE, 'submit': sub, 'job': j}, open(sys.argv[1], 'w'))
# The recipe's 119/36 was recorded BEFORE L1 (duplicate-emitter merge, default ON since 2026-09-25).
# Verify it with the merge OFF, the recorded condition, then restore the merge and report both counts.
def settled_census(merge_on):
    call('POST', '/api/debug/emitter_merge', {'enabled': merge_on})
    for _ in range(300):
        c = call('GET', '/api/debug/load_state')['chunks']
        if not (c['remesh_pending'] or c['remesh_idle_pending'] or c['generation_pending']):
            break
        time.sleep(0.1)
    time.sleep(1.5)
    return call('GET', '/api/debug/light_stats')
merged = settled_census(True)
ls = settled_census(False)
settled_census(True)
print('with L1 merge ON: lights', merged['registered'], 'unique', merged['unique_positions_registered'])
print('job', j['state'], j.get('units_done'), '/', j.get('units_total'),
      '| lights', ls['registered'], 'unique', ls['unique_positions_registered'])
ok = (j['state'] == 'complete' and j.get('units_done') == j.get('units_total') == 21
      and ls['registered'] == 119 and ls['unique_positions_registered'] == 36)
print('S-2 MATCHES RECIPE' if ok else 'S-2 DOES NOT MATCH THE RECORDED RECIPE - do not measure')
sys.exit(0 if ok else 1)
