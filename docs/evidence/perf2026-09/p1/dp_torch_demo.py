"""P-DP demonstration for the user: a HELD torch moving through the tavern at night, depth prepass ON.

Scene: M4TavernBench + engine-generated tavern (POST /api/structure/build v2 tavern). Night via
POST /api/daynight/set {timeOfDay: 22, paused: true}. The player holds a torch (inventory slot 0 =
"torch", which is what the editor's Equip button does); its light is an item-effect light that
follows the hand every frame. The player is moved to 3 spots across the taproom under a FIXED
camera; at each spot we capture and read back the torch light's position, so "the light moves with
the object" is shown by numbers as well as pictures. Then the middle spot again with the prepass OFF.
Usage: dp_torch_demo.py <torch_light_id>"""
import json, os, shutil, sys, time, urllib.request

B = 'http://127.0.0.1:8090'
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..'))
HERE = os.path.dirname(os.path.abspath(__file__))
LIGHT_ID = int(sys.argv[1]) if len(sys.argv) > 1 else 17


def call(m, p, b=None, t=60):
    r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(r, timeout=t))


def torch_light():
    for l in call('GET', '/api/lights')['point_lights']:
        if l['id'] == LIGHT_ID:
            return {k: round(v, 2) for k, v in l['position'].items()}
    return None


def shot(tag):
    time.sleep(1.2)
    p = call('GET', '/api/screenshot')['path']
    dst = os.path.join(HERE, f'dp_torch_{tag}.png')
    shutil.copy(os.path.join(REPO, p), dst)
    return os.path.basename(dst)


call('POST', '/api/camera', {'mode': 'free', 'position': {'x': 13.2, 'y': 19.4, 'z': 3.5}, 'yaw': 180, 'pitch': -14})
spots = {'A_far': (2.5, 17.0, 3.5), 'B_mid': (6.5, 17.0, 3.5), 'C_near': (10.5, 17.0, 3.5)}
out = {'light_id': LIGHT_ID, 'frames': []}
call('POST', '/api/debug/depth_prepass', {'enabled': True})
for tag, (x, y, z) in spots.items():
    call('POST', '/api/entity/move', {'id': 'player', 'position': {'x': x, 'y': y, 'z': z}})
    time.sleep(1.0)
    pp = call('POST', '/api/debug/depth_prepass', {})
    f = {'tag': tag, 'player': [x, y, z], 'torch_light': torch_light(), 'prepass_ran': pp['ran_last_frame'],
         'capture': shot(f'prepass_on_{tag}')}
    out['frames'].append(f)
    print(f)
# Same middle spot, prepass OFF, for a side-by-side.
call('POST', '/api/entity/move', {'id': 'player', 'position': {'x': 6.5, 'y': 17.0, 'z': 3.5}})
call('POST', '/api/debug/depth_prepass', {'enabled': False})
time.sleep(1.0)
f = {'tag': 'B_mid_prepass_off', 'torch_light': torch_light(),
     'prepass_ran': call('POST', '/api/debug/depth_prepass', {})['ran_last_frame'],
     'capture': shot('prepass_off_B_mid')}
out['frames'].append(f)
print(f)
call('POST', '/api/debug/depth_prepass', {'enabled': True})
json.dump(out, open(os.path.join(HERE, 'dp_torch_demo.json'), 'w'), indent=1)
