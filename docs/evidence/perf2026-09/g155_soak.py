"""G-155 soak (docs/PerfProgram2026-09.md, I2): pipeline statistics ON for 5 minutes at each pose.

Pre-fix, the same scene and street pose crashed within seconds of enabling stats
(g155_repro_prefix_crash.txt: 0xC0000005 in nvoglv64.dll). This run must survive both poses with
every slot, including CHARACTER, reporting real counters. Scene: M4DensityBench (Perlin seed 7,
heightScale 18) + POST /api/settlement/build medieval/town seed 7, 80x40, terrain:true
(engine-generated; 119 point lights at 36 positions)."""
import json, sys, time, urllib.request

B = 'http://127.0.0.1:8090'
OUT = sys.argv[1] if len(sys.argv) > 1 else 'g155_soak_postfix.jsonl'
SECONDS_PER_POSE = int(sys.argv[2]) if len(sys.argv) > 2 else 300
POSES = {'street': [23, 53, 24, -90, -6], 'overview': [0, 110, 70, -90, -40]}


def call(m, p, b=None, t=15):
    try:
        r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None,
                                   method=m, headers={'Content-Type': 'application/json'})
        return json.load(urllib.request.urlopen(r, timeout=t))
    except Exception as e:
        return {'err': str(e)[:200]}


with open(OUT, 'a') as f:
    for name, p in POSES.items():
        call('POST', '/api/camera', {'mode': 'free', 'position': {'x': p[0], 'y': p[1], 'z': p[2]},
                                     'yaw': p[3], 'pitch': p[4]})
        time.sleep(2)
        on = call('POST', '/api/debug/pipeline_stats', {'enabled': True})
        f.write(json.dumps({'event': 'stats_on', 'pose': name, 'resp': on}) + '\n'); f.flush()
        t0 = time.time()
        n = 0
        while time.time() - t0 < SECONDS_PER_POSE:
            time.sleep(2)
            s = call('GET', '/api/debug/gpu_scopes')
            alive = 'err' not in s
            row = {'pose': name, 't': round(time.time() - t0, 1), 'alive': alive}
            if alive:
                row.update({k: s.get(k) for k in ('serial', 'static_geometry_pipeline_stats',
                                                   'shadow_pipeline_stats', 'character_pipeline_stats')})
            else:
                row['err'] = s['err']
            f.write(json.dumps(row) + '\n'); f.flush()
            n += 1
            if not alive:
                print('DEAD at', name, row['t'], flush=True)
                sys.exit(2)
        print('survived', name, SECONDS_PER_POSE, 's,', n, 'samples', flush=True)
    call('POST', '/api/debug/pipeline_stats', {'enabled': False})
print('SOAK PASSED', flush=True)
