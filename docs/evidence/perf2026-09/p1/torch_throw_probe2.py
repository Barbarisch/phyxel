"""L4 check for the thrown-torch fixes (2026-09-25), default inventory (no creative toggle).

Predictions, written before the run:
  1. /api/inventory reports creative == false with no call to /api/inventory/creative.
  2. After the throw, slot 0 is empty and the held torch light is gone (the item left the hand).
  3. A thrown-torch light exists in EVERY sample after the throw (no dark gap; the old code
     left the prop dark ~0.23 s), and it moves along the arc before settling.
Torch lights are identified by the torch's items.json light (intensity 2.5, radius 10)."""
import json, os, time, urllib.request

B = 'http://127.0.0.1:8090'


def call(m, p, b=None):
    r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(r, timeout=30))


def torch_lights():
    return [(l['id'], [round(l['position'][k], 2) for k in 'xyz'])
            for l in call('GET', '/api/lights')['point_lights']
            if abs(l.get('radius', 0) - 10.0) < 1e-3 and abs(l.get('intensity', 0) - 2.5) < 1e-3]


inv = call('GET', '/api/inventory')
print('creative:', inv.get('creative'))
print('set_slot:', call('POST', '/api/inventory/set_slot', {'slot': 0, 'material': 'torch', 'count': 1}))
print('select:', call('POST', '/api/inventory/select', {'slot': 0}))
time.sleep(1.0)
before = torch_lights()
print('held lights before throw:', before)
held_ids = {i for i, _ in before}

t0 = time.time()
print('drop:', call('POST', '/api/items/drop', {}))
samples = []
while time.time() - t0 < 2.5:
    samples.append((round(time.time() - t0, 3), torch_lights()))
    time.sleep(0.03)
slot0 = call('GET', '/api/inventory')['hotbar'][0]

gaps = [t for t, ls in samples if not ls]
held_left = [t for t, ls in samples if any(i in held_ids for i, _ in ls)]
thrown = [(t, p) for t, ls in samples for i, p in ls if i not in held_ids]
result = {
    'creative_default': inv.get('creative'),
    'slot0_after': slot0,
    'held_light_ids': sorted(held_ids),
    'samples_with_no_torch_light': gaps,
    'samples_still_showing_held_light': held_left,
    'first_thrown_light_t': thrown[0][0] if thrown else None,
    'first_sample_t': samples[0][0],
    'thrown_path': thrown,
}
for k, v in result.items():
    if k != 'thrown_path':
        print(k, '=', v)
print('thrown path:', thrown[:6], '...', thrown[-2:])
json.dump(result, open(os.path.splitext(__file__)[0] + '.json', 'w'), indent=1)
