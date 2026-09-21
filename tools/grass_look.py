"""grass_look.py <tag> — does the grass fast path CHANGE the picture, or only its cost?

Captures fixed Ravenmere town poses and measures the linear luminance of grass regions, tone map
OFF so the reading is the shading and not the curve. Run once with the fast path and once with the
general phxAmbient; outdoor grass must be identical, because for an up normal the dropped X and Z
lobes carry weight zero. Any difference is the one approximation: the missing visibility trace.
"""
import json, sys, time, urllib.request
from pathlib import Path
import numpy as np
from PIL import Image

B = 'http://127.0.0.1:8090'
TAG = sys.argv[1]
OUT = Path('docs/evidence')
POSES = {
    'spawn': {'x': -36.0, 'y': 19.0, 'z': 15.0, 'yaw': 0.0, 'pitch': -2.0},
    'west':  {'x': -45.0, 'y': 26.0, 'z': 21.5, 'yaw': 0.0, 'pitch': -4.0},
}
# Fractions of the frame. Chosen on the captured images to sit on open grass, not on buildings.
REGIONS = {'grass_near': [0.62, 0.62, 0.90, 0.78], 'grass_mid': [0.30, 0.58, 0.55, 0.68]}


def call(m, p, b=None, t=120):
    r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None,
                               method=m, headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(r, timeout=t))


def srgb_to_linear(x):
    return np.where(x <= 0.04045, x / 12.92, ((x + 0.055) / 1.055) ** 2.4)


call('POST', '/api/daynight/set', {'enabled': True, 'paused': True, 'timeOfDay': 12.0})
call('POST', '/api/debug/gi', {'enabled': True})
call('POST', '/api/debug/shadow', {'mode': 0})
call('POST', '/api/debug/tonemap', {'curve': 0, 'exposure': 8.0})
res = {}
for name, cam in POSES.items():
    call('POST', '/api/camera', {'mode': 'free', 'position': {k: cam[k] for k in 'xyz'},
                                 'yaw': cam['yaw'], 'pitch': cam['pitch']})
    time.sleep(8)
    src = Path(call('GET', '/api/screenshot')['path'])
    dst = OUT / ('grass_look_%s_%s.png' % (TAG, name))
    dst.write_bytes(src.read_bytes())
    a = np.asarray(Image.open(dst).convert('RGB'), dtype=np.float32) / 255.0
    lin = srgb_to_linear(a)
    lum = 0.2126 * lin[..., 0] + 0.7152 * lin[..., 1] + 0.0722 * lin[..., 2]
    h, w = lum.shape
    for rn, r in REGIONS.items():
        v = float(lum[int(r[1] * h):int(r[3] * h), int(r[0] * w):int(r[2] * w)].mean())
        res['%s/%s' % (name, rn)] = v
        print('  %-14s %-11s %.5f' % (name, rn, v), flush=True)
call('POST', '/api/debug/tonemap', {'curve': 1, 'exposure': 8.0})
(OUT / ('grass_look_%s.json' % TAG)).write_text(json.dumps(res, indent=2), encoding='utf-8')
print('  wrote', OUT / ('grass_look_%s.json' % TAG))
