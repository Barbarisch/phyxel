"""Build one rung of the S-3 city ladder with the ENGINE's generator and fingerprint it
(docs/PerfProgram2026-09.md section 16.1). Provenance: POST /api/settlement/build tier:"city" -- nothing
is hand-placed; the raw request, submit response and job record are saved beside the fingerprint.

Fingerprint = {buildings (generator report), placed objects, resident NPCs, voxel_tiers per-tier counts,
light_stats registered/uploaded by source}, taken at a FIXED anchor: camera at the overview pose above the
site centre (the editor anchors streaming on the camera; default residency 256 u covers the site), after
the settle gate.
Usage: city_build.py <rung> <width> <depth> <out_prefix>     e.g. city_build.py C-100 192 192 city_C100
       city_build.py --fingerprint-only <rung> <width> <depth> <out_prefix>   (persistence check)"""
import json, sys, time

from rig_common import call, settle, wait_api

args = [a for a in sys.argv[1:] if not a.startswith('--')]
FP_ONLY = '--fingerprint-only' in sys.argv
rung, W, D, prefix = args[0], int(args[1]), int(args[2]), args[3]
# Site: the S-2 town's area of the seed-7 terrain, grown around the same centre (x -0, z 0).
CX, CZ = 0, 0
RECIPE = {'era': 'medieval', 'tier': 'city', 'seed': 7,
          'position': {'x': CX - W // 2, 'y': 16, 'z': CZ - D // 2},
          'width': W, 'depth': D, 'terrain': True, 'density': 1.5}
OVERVIEW = {'x': CX, 'y': 190, 'z': CZ + D * 0.9, 'yaw': -90, 'pitch': -45}


def fingerprint():
    call('POST', '/api/camera', {'mode': 'free', 'position': {k: OVERVIEW[k] for k in 'xyz'},
                                 'yaw': OVERVIEW['yaw'], 'pitch': OVERVIEW['pitch']})
    time.sleep(3)
    settle(900)
    time.sleep(3)
    tiers = call('GET', '/api/debug/voxel_tiers', t=120)
    lights = call('GET', '/api/debug/light_stats')
    placed = call('GET', '/api/placed_objects', t=120)
    npcs = call('GET', '/api/npcs')
    return {'placed_objects': placed.get('count'),
            'npcs': len(npcs.get('npcs', npcs if isinstance(npcs, list) else [])),
            'voxel_tiers': {k: v for k, v in tiers.items() if k in ('tiers', 'totals')},
            'lights': {k: lights.get(k) for k in ('registered', 'uploaded', 'enabled_point', 'by_source', 'by_source_uploaded')}}


wait_api()
# The site must be STREAMED before the generator runs on it (the editor anchors streaming on the
# camera): stand the camera at the overview pose above the site centre and settle first.
call('POST', '/api/camera', {'mode': 'free', 'position': {k: OVERVIEW[k] for k in 'xyz'},
                             'yaw': OVERVIEW['yaw'], 'pitch': OVERVIEW['pitch']})
settle(1800)
out = {'rung': rung, 'recipe': RECIPE, 'overview_pose': OVERVIEW}
if not FP_ONLY:
    sub = call('POST', '/api/settlement/build', RECIPE, t=120)
    out['submit'] = sub
    job = sub.get('job_id')
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
