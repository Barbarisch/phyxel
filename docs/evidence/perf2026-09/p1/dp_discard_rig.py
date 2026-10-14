"""R-DP2 (docs/PerfProgram2026-09.md P-DP): the prepass must discard exactly what voxel.frag discards.

HAND-PLACED RIG inside ONE chunk (chunk (1,0,1), world x/z 32..63), in open air above the flat ground:
  - a Stone wall (x 40..46, y 17..21, z 45)                    the thing that must stay visible
  - a Glass pane in front of it (x 41..45, y 18..20, z 42)     transparent flag + cutout texels
  - a Mirror block beside the pane (x 47, y 18, z 42)          mirror flag
If the prepass wrote depth for glass or mirror fragments, the wall behind the pane would vanish from
the opaque pass and the mirror pass would lose its surface: a pixel difference at every angle.

Checked from 6 orbit angles (the fragile-winding rule), in debug mode 12 (albedo of the surviving
front-most fragment, after all three discards; grass/foliage/sky black) with the game paused, via
dp_pixel_gate.py (A1 off / B on / C off timing-matched control)."""
import json, math, os, subprocess, sys, time, urllib.request

B = 'http://127.0.0.1:8090'
HERE = os.path.dirname(os.path.abspath(__file__))


def call(m, p, b=None, t=60):
    r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(r, timeout=t))


def place(x, y, z, mat):
    return call('POST', '/api/world/voxel', {'x': x, 'y': y, 'z': z, 'material': mat})


placed = []
for x in range(40, 47):
    for y in range(17, 22):
        placed.append(place(x, y, 45, 'Stone'))
for x in range(41, 46):
    for y in range(18, 21):
        placed.append(place(x, y, 42, 'Glass'))
placed.append(place(47, 18, 42, 'Mirror'))
ok_place = sum(1 for p in placed if p.get('success', True) and 'error' not in p)
# Verify the world, not the API response.
check = {m: call('GET', f'/api/world/voxel?x={x}&y={y}&z={z}').get('material')
         for m, (x, y, z) in {'Stone': (43, 19, 45), 'Glass': (43, 19, 42), 'Mirror': (47, 18, 42)}.items()}
print('placed', ok_place, 'of', len(placed), '| read back', check)
for _ in range(300):
    c = call('GET', '/api/debug/load_state')['chunks']
    if not (c['generation_pending'] or c['remesh_pending'] or c['remesh_idle_pending']):
        break
    time.sleep(0.1)

centre = (44.0, 19.5, 43.5)
poses = {}
for i, (az, el) in enumerate([(0, 10), (60, 25), (120, 10), (180, 25), (240, 10), (300, 35)]):
    r = 7.0
    px = centre[0] + r * math.cos(math.radians(el)) * math.sin(math.radians(az))
    pz = centre[2] - r * math.cos(math.radians(el)) * math.cos(math.radians(az))
    py = centre[1] + r * math.sin(math.radians(el))
    dx, dy, dz = centre[0] - px, centre[1] - py, centre[2] - pz
    n = math.sqrt(dx * dx + dy * dy + dz * dz)
    yaw = math.degrees(math.atan2(dz / n, dx / n))      # free-cam: yaw -90 = -Z, 0 = +X
    pitch = math.degrees(math.asin(dy / n))
    poses[f'orbit{i}_az{az}'] = [round(px, 3), round(py, 3), round(pz, 3), round(yaw, 3), round(pitch, 3)]

call('POST', '/api/game/pause', {'paused': True})
call('POST', '/api/debug/shadow', {'mode': 12})
results = {}
try:
    for name, pose in poses.items():
        out = os.path.join(HERE, f'dp_rig_{name}.json')
        rc = subprocess.call([sys.executable, os.path.join(HERE, 'dp_pixel_gate.py'), json.dumps(pose), out])
        results[name] = {'pose': pose, 'pass': rc == 0}
        print(name, 'PASS' if rc == 0 else 'FAIL', flush=True)
finally:
    call('POST', '/api/debug/shadow', {'mode': 0})
    call('POST', '/api/game/pause', {'paused': False})
json.dump({'rig_readback': check, 'results': results}, open(os.path.join(HERE, 'dp_discard_rig.json'), 'w'), indent=1)
print('R-DP2 PASSED' if all(r['pass'] for r in results.values()) else 'R-DP2 FAILED')
