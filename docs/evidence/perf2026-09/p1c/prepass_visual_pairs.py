"""Real-look comparison captures for the user's P-DP default decision (PerfProgram 17.2 step 2).
For each S-3 pose and time of day: prepass OFF and ON, shipping tone curve, NOTHING frozen (residents,
grass, wind, lamps all live -- what a player sees), captured back to back after the same settle.
Saved under p1c/prepass_visual/<tod>_<pose>_{off,on}.png plus the GPU frame median for each (240
frames), so the page can show the cost next to the look. The pixel-exact differences come from the
frozen gates (prepass_gate_*), not from these (live scenes move between captures).
Usage: prepass_visual_pairs.py <poses.json>"""
import json, os, shutil, sys, time
import urllib.request

B = 'http://127.0.0.1:8090'
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..', '..', '..'))
OUT = os.path.join(HERE, 'prepass_visual')


def call(m, p, b=None, t=60):
    r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(r, timeout=t))


def settle():
    for _ in range(600):
        c = call('GET', '/api/debug/load_state')['chunks']
        if not (c['generation_pending'] or c['remesh_pending'] or c['remesh_idle_pending']):
            return
        time.sleep(0.1)


def gpu_median(frames=240):
    start = call('GET', '/api/debug/gpu_timing?frames=1').get('frames_accepted', 0)
    while call('GET', '/api/debug/gpu_timing?frames=1').get('frames_accepted', 0) - start < frames:
        time.sleep(0.2)
    return (call('GET', f'/api/debug/gpu_timing?frames={frames}').get('gpu_frame_ms') or {}).get('median_ms')


os.makedirs(OUT, exist_ok=True)
poses = json.load(open(sys.argv[1]))
index = []
for tod_name, tod in (('noon', 12.0), ('night', 22.0)):
    call('POST', '/api/daynight/set', {'timeOfDay': tod, 'paused': True})
    for name, (x, y, z, yaw, pitch) in poses.items():
        call('POST', '/api/camera', {'mode': 'free', 'position': {'x': x, 'y': y, 'z': z}, 'yaw': yaw, 'pitch': pitch})
        settle()
        row = {'tod': tod_name, 'pose': name}
        for state in ('off', 'on'):
            r = call('POST', '/api/debug/depth_prepass', {'enabled': state == 'on'})
            assert r.get('enabled') == (state == 'on'), r
            time.sleep(1.5)
            row[f'gpu_ms_{state}'] = gpu_median()
            src = call('GET', '/api/screenshot')['path']
            dst = os.path.join(OUT, f'{tod_name}_{name}_{state}.png')
            shutil.copyfile(os.path.join(REPO, src), dst)
            row[state] = os.path.basename(dst)
        index.append(row)
        print(json.dumps(row))
call('POST', '/api/debug/depth_prepass', {'enabled': False})   # shipped default
json.dump(index, open(os.path.join(OUT, 'index.json'), 'w'), indent=1)
