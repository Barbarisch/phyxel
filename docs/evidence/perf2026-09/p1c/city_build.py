"""Build one rung of the S-3 city ladder with the ENGINE's generator and fingerprint it
(docs/PerfProgram2026-09.md section 16.1). Provenance: POST /api/settlement/build tier:"city" -- nothing
is hand-placed; the raw request, submit response and job record are saved beside the fingerprint.

Fingerprint = {buildings (generator report), placed objects, resident NPCs, voxel_tiers per-tier counts,
light_stats registered/uploaded by source}, taken at a FIXED anchor: camera straight above the
site centre (the editor anchors streaming on the camera; default residency 256 u covers the site), after
the settle gate.
Usage: city_build.py <rung> <width> <depth> <out_prefix>     e.g. city_build.py C-100 192 192 city_C100
       city_build.py --fingerprint-only <rung> <width> <depth> <out_prefix>   (persistence check)
       city_build.py --attach=<job_id> <rung> <width> <depth> <out_prefix>    (adopt a job already running
                                                    from the same RECIPE: wait, save, fingerprint)"""
import json, sys, time

from rig_common import call, settle, wait_api

args = [a for a in sys.argv[1:] if not a.startswith('--')]
FP_ONLY = '--fingerprint-only' in sys.argv
ATTACH = next((int(a.split('=', 1)[1]) for a in sys.argv[1:] if a.startswith('--attach=')), None)
rung, W, D, prefix = args[0], int(args[1]), int(args[2]), args[3]
# Site: the S-2 town's area of the seed-7 terrain, grown around the same centre (x -0, z 0).
CX, CZ = 0, 0
RECIPE = {'era': 'medieval', 'tier': 'city', 'seed': 7,
          'position': {'x': CX - W // 2, 'y': 16, 'z': CZ - D // 2},
          'width': W, 'depth': D, 'terrain': True, 'density': 1.5}
OVERVIEW = {'x': CX, 'y': 190, 'z': CZ + D * 0.9, 'yaw': -90, 'pitch': -45}
# ANCHOR for the pre-build settle and the fingerprint: straight above the site centre, so every
# corner is inside the default 256 u residency at every rung (C-100 far corner ~143 u). The
# overview pose is a MEASUREMENT pose only: from it the C-75 far corner is ~274 u away, unstreamed,
# and the generator (correctly) refused the build as ungrounded (2,441 of 25,600 columns).
ANCHOR = {'x': CX, 'y': 100, 'z': CZ, 'yaw': -90, 'pitch': -89}


def fingerprint():
    call('POST', '/api/camera', {'mode': 'free', 'position': {k: ANCHOR[k] for k in 'xyz'},
                                 'yaw': ANCHOR['yaw'], 'pitch': ANCHOR['pitch']})
    time.sleep(3)
    settle(900)
    time.sleep(3)
    tiers = call('GET', '/api/debug/voxel_tiers', t=120)
    lights = call('GET', '/api/debug/light_stats')
    placed = call('GET', '/api/placed_objects', t=120)
    npcs = call('GET', '/api/npcs')
    # Per-chunk voxel counts (cube/sub/micro), saved beside the fingerprint so a build-vs-reload
    # difference can be located to the chunk (the C-25 reload gained +7,644 micro / +2,075 sub).
    try:
        chunks = call('GET', '/api/world/chunks?detail=1', t=120).get('chunks', [])
    except Exception as e:
        chunks = [{'error': str(e)}]
    json.dump(chunks, open(prefix + ('_chunks_reload.json' if FP_ONLY else '_chunks.json'), 'w'))
    return {'placed_objects': placed.get('count'),
            'npcs': len(npcs.get('npcs', npcs if isinstance(npcs, list) else [])),
            'voxel_tiers': {k: v for k, v in tiers.items() if k in ('tiers', 'totals')},
            'lights': {k: lights.get(k) for k in ('registered', 'uploaded', 'enabled_point', 'by_source', 'by_source_uploaded')}}


wait_api()
# The site must be STREAMED before the generator runs on it (the editor anchors streaming on the
# camera): stand the camera at ANCHOR above the site centre and settle first.
def settled_steadily(timeout_s, need=5, gap_s=2.0):
    """settle() can return during a momentary lull while the site is still streaming (C-100 was
    refused as ungrounded that way). Require `need` consecutive settled polls `gap_s` apart."""
    t0, streak = time.time(), 0
    while streak < need:
        if time.time() - t0 > timeout_s:
            raise RuntimeError('world did not settle steadily within %d s' % timeout_s)
        settle(timeout_s)
        streak += 1
        time.sleep(gap_s)
        try:
            c = call('GET', '/api/debug/load_state', t=60).get('chunks') or {}
            if c.get('generation_pending') or c.get('remesh_pending') or c.get('remesh_idle_pending'):
                streak = 0
        except Exception:
            streak = 0


out = {'rung': rung, 'recipe': RECIPE, 'overview_pose': OVERVIEW, 'anchor_pose': ANCHOR}
if ATTACH is None:
    call('POST', '/api/camera', {'mode': 'free', 'position': {k: ANCHOR[k] for k in 'xyz'},
                                 'yaw': ANCHOR['yaw'], 'pitch': ANCHOR['pitch']})
    settled_steadily(1800)
if not FP_ONLY:
    if ATTACH is not None:
        job = ATTACH
        out['submit'] = {'attached_job': job, 'note': 'job submitted from this exact RECIPE by a manual '
                         'probe (the first scripted submit was refused as ungrounded mid-stream)'}
        print(rung, 'attached to job', job)
    else:
        refusals = []
        for attempt in range(6):
            sub = call('POST', '/api/settlement/build', RECIPE, t=120)
            if sub.get('job_id') is not None:
                break
            refusals.append(sub)   # an honest generator refusal (e.g. ungrounded): re-settle, retry
            print(rung, 'REFUSED:', json.dumps(sub)[:300])
            settled_steadily(1800)
        out['submit'] = sub
        out['refusals_before_submit'] = refusals
        job = sub.get('job_id')
        if job is None:
            json.dump(out, open(prefix + '_build_REFUSED.json', 'w'), indent=1)
            raise SystemExit('generator refused %d times; see %s_build_REFUSED.json' % (len(refusals), prefix))
        print(rung, 'submitted job', job, 'buildings', sub.get('buildings'), 'queued', len(sub.get('queued_builds', [])))
    t0 = time.time()
    j = None
    while time.time() - t0 < 7200:
        try:
            jobs = call('GET', '/api/jobs', t=120)['jobs']
            j = next(x for x in jobs if x['id'] == job)
            if j['state'] in ('complete', 'failed', 'cancelled'):
                break
        except Exception as e:
            print('poll:', e)   # the job runs on the main thread; the API can be slow while it works
        time.sleep(15)
    out['job'] = j
    out['build_wall_s'] = time.time() - t0
    # Persist explicitly (section 16.1 option 1): dirty chunks + the placed-object registry
    # (which now carries the structure light records). The reload fingerprint is compared
    # against the fingerprint taken right after this save.
    out['save'] = call('POST', '/api/world/save', {}, t=600)
    print(rung, 'save', out['save'])
    print(rung, 'job', j and j['state'], 'in', round(out['build_wall_s']), 's')
    json.dump(out, open(prefix + '_build.json', 'w'), indent=1)
out['fingerprint'] = fingerprint()
print(json.dumps(out['fingerprint'])[:1200])
json.dump(out, open(prefix + ('_fingerprint_reload.json' if FP_ONLY else '_fingerprint.json'), 'w'), indent=1)
