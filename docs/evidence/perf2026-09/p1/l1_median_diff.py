"""Town (S-2, at the light cap) visual check for L1, robust to motion.

Single-pair diffs at the town street pose are swamped by 4 walking NPCs and grass wind (even the control
differs on 17-31k pixels). Pausing does not help: wind keeps animating, and the emissive-light reconcile
runs in the simulation update, so while paused the merge toggle never takes effect (a paused run compared
merged against merged - discarded). Here each condition is the per-pixel MEDIAN of N captures, unpaused.
Condition order is ON, OFF, ON2, so ON vs ON2 is the matched control for ON vs OFF.
Reports where merged and unmerged differ beyond the control, and in which direction."""
import json, os, sys, time, urllib.request

import numpy as np
from PIL import Image

B = 'http://127.0.0.1:8090'
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..'))
VIEW = (242, 43, 1197, 672)
N = int(sys.argv[3]) if len(sys.argv) > 3 else 5


def call(m, p, b=None, t=60):
    r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(r, timeout=t))


def merge(on):
    call('POST', '/api/debug/emitter_merge', {'enabled': on})
    for _ in range(300):
        c = call('GET', '/api/debug/load_state')['chunks']
        if not (c['generation_pending'] or c['remesh_pending'] or c['remesh_idle_pending']):
            break
        time.sleep(0.1)
    time.sleep(1.5)
    return call('GET', '/api/debug/light_stats')['registered']


def median_capture():
    imgs = []
    for _ in range(N):
        time.sleep(0.7)
        p = call('GET', '/api/screenshot')['path']
        a = np.array(Image.open(os.path.join(REPO, p)).convert('RGB')).astype(np.float32)
        x0, y0, x1, y1 = VIEW
        imgs.append(a[y0:y1, x0:x1])
    return np.median(np.stack(imgs), axis=0)


pose = json.loads(sys.argv[1])
call('POST', '/api/camera', {'mode': 'free', 'position': {'x': pose[0], 'y': pose[1], 'z': pose[2]},
                             'yaw': pose[3], 'pitch': pose[4]})
counts = {'on': merge(True)}
on = median_capture()
counts['off'] = merge(False)
off = median_capture()
counts['on2'] = merge(True)
on2 = median_capture()

lum = lambda x: x.mean(axis=2)
test = np.abs(on - off).max(axis=2)
ctrl = np.abs(on - on2).max(axis=2)
beyond = (test > 8) & (ctrl <= 8)
signed = (lum(on) - lum(off))[beyond]
res = {'pose': pose, 'n_per_condition': N, 'lights': counts,
       'control_px_over8': int((ctrl > 8).sum()), 'test_px_over8': int((test > 8).sum()),
       'beyond_control_px': int(beyond.sum()), 'beyond_control_frac': float(beyond.mean()),
       'beyond_brighter_px': int((signed > 0).sum()), 'beyond_darker_px': int((signed < 0).sum()),
       'beyond_mean_lum_change_merged_minus_unmerged': float(signed.mean()) if signed.size else None}
Image.fromarray(np.clip(test * 6, 0, 255).astype(np.uint8)).save(sys.argv[2].replace('.json', '_test_x6.png'))
Image.fromarray(np.clip(ctrl * 6, 0, 255).astype(np.uint8)).save(sys.argv[2].replace('.json', '_ctrl_x6.png'))
json.dump(res, open(sys.argv[2], 'w'), indent=1)
print(json.dumps(res, indent=1))
