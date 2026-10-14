"""Does a thrown torch's light follow the torch? Drops (tosses) the player's held torch via drop_item and
samples every item-effect light (source ItemEffect) plus the prop's position every ~50 ms for 3 s.
Prints the light count per sample so a hand-off gap (held light removed, prop light not yet created)
shows up as a 0."""
import json, time, urllib.request

B = 'http://127.0.0.1:8090'


def call(m, p, b=None):
    r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(r, timeout=30))


def torch_lights():
    out = []
    for l in call('GET', '/api/lights')['point_lights']:
        if abs(l.get('radius', 0) - 10.0) < 1e-3 and abs(l.get('intensity', 0) - 2.5) < 1e-3:
            out.append((l['id'], [round(l['position'][k], 2) for k in 'xyz']))
    return out


before = torch_lights()
print('before drop:', before)
t0 = time.time()
print('drop:', call('POST', '/api/items/drop', {}))
samples = []
while time.time() - t0 < 3.0:
    samples.append((round(time.time() - t0, 3), torch_lights()))
    time.sleep(0.05)
for s in samples:
    print(s)
json.dump({'before': before, 'samples': samples}, open(__file__.replace('.py', '.json'), 'w'), indent=1)
