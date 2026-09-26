"""Where are the >8/255 pixels of a gi_pixel_gate run? Bounding box + connected blobs of the test
(A1 vs B) and control (A1 vs C) masks, per curve. A difference confined to the moving NPC's region
in BOTH masks is timing noise, not the option under test.
Usage: gi_diff_where.py gate.json"""
import json, os, sys

import numpy as np
from PIL import Image
from scipy import ndimage

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..'))
VIEW = (242, 43, 1197, 672)


def load(p):
    a = np.array(Image.open(os.path.join(REPO, p)).convert('RGB')).astype(np.int32)
    x0, y0, x1, y1 = VIEW
    return a[y0:y1, x0:x1]


d = json.load(open(sys.argv[1]))
for curve, c in d['curves'].items():
    a1, cc, b = (load(p) for p in c['captures'])   # captures = [A1, C, B]
    for name, other in (('test A1-B', b), ('control A1-C', cc)):
        m = np.abs(a1 - other).max(axis=2) > 8
        lab, n = ndimage.label(m)
        sizes = ndimage.sum(m, lab, range(1, n + 1)) if n else []
        blobs = sorted(((int(s), ndimage.find_objects(lab)[i]) for i, s in enumerate(sizes)), reverse=True)[:3]
        desc = [(s, (sl[1].start, sl[0].start, sl[1].stop, sl[0].stop)) for s, sl in blobs]
        ys, xs = np.nonzero(m)
        bb = (int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())) if len(xs) else None
        print(f'{curve:8s} {name:13s} px {int(m.sum()):5d} bbox {bb} top blobs (px, x0,y0,x1,y1) {desc}')
