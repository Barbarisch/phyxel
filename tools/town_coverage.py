"""town_coverage.py — does a REAL scene contain geometry the ambient model has no answer for?

Ravenmere town spans x -38..56, about 94 u. The probe grid is 96 u wide and centred on the viewer,
so from anywhere but the exact middle, part of the town is outside it. This captures the town from
the player's own spawn pose and from a vantage down the main street, in the probe-coverage view
(debug mode 4: GREEN = the shader had an answer here) beside the normal view, and reports the
fraction of GEOMETRY pixels that had an answer. Sky is excluded by the blue-flag channel, which is
set on every lit fragment and nowhere else.
Tone map OFF for the coverage view (CLAUDE.md trap 1).
"""
import json, sys, time, urllib.request
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw

B = 'http://127.0.0.1:8090'
OUT = Path(sys.argv[1])
VIEW = (245, 45, 1195, 670)


def call(m, p, b=None, t=180):
    r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None,
                               method=m, headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(r, timeout=t))


POSES = {
    'spawn_looking_east': {'x': -36.0, 'y': 19.0, 'z': 15.0, 'yaw': 0.0, 'pitch': -2.0},
    'street_from_west':   {'x': -45.0, 'y': 26.0, 'z': 21.5, 'yaw': 0.0, 'pitch': -4.0},
}

call('POST', '/api/daynight/set', {'enabled': True, 'paused': True, 'timeOfDay': 12.0})
call('POST', '/api/debug/gi', {'enabled': True})
tiles = []
for name, cam in POSES.items():
    call('POST', '/api/camera', {'mode': 'free', 'position': {k: cam[k] for k in 'xyz'},
                                 'yaw': cam['yaw'], 'pitch': cam['pitch']})
    time.sleep(2)
    call('POST', '/api/debug/shadow', {'mode': 0}); call('POST', '/api/debug/tonemap', {'curve': 1, 'exposure': 8.0})
    time.sleep(5)
    p_n = Path(call('GET', '/api/screenshot')['path'])
    call('POST', '/api/debug/shadow', {'mode': 4}); call('POST', '/api/debug/tonemap', {'curve': 0, 'exposure': 1.0})
    time.sleep(3)
    p_c = Path(call('GET', '/api/screenshot')['path'])

    cov = np.asarray(Image.open(p_c).convert('RGB').crop(VIEW), dtype=np.float32)
    geom = cov[..., 2] > 128                      # blue flag: set on every lit fragment, never on sky
    answered = (cov[..., 0] > 128) & geom         # red: a visible valid probe was found
    ingrid = (cov[..., 1] > 128) & geom           # green: inside the grid at all
    frac_a = float(answered.sum()) / max(int(geom.sum()), 1)
    frac_g = float(ingrid.sum()) / max(int(geom.sum()), 1)
    print('  %-20s  geometry pixels %7d   inside the grid %.0f%%   with an answer %.0f%%'
          % (name, int(geom.sum()), 100 * frac_g, 100 * frac_a), flush=True)

    for lbl, pth in (('%s  —  normal view' % name, p_n),
                     ('%s  —  COVERAGE: green = the model has an answer (%.0f%% of geometry)' % (name, 100 * frac_g), p_c)):
        im = Image.open(pth).convert('RGB').crop(VIEW).resize((760, 500))
        ImageDraw.Draw(im).text((8, 8), lbl, fill=(255, 255, 0))
        tiles.append(im)

call('POST', '/api/debug/shadow', {'mode': 0}); call('POST', '/api/debug/tonemap', {'curve': 1, 'exposure': 8.0})
m = Image.new('RGB', (760 * 2, 500 * 2))
[m.paste(t, ((i % 2) * 760, (i // 2) * 500)) for i, t in enumerate(tiles)]
m.save(OUT / 'town_coverage.png'); print('\nmontage:', OUT / 'town_coverage.png')
