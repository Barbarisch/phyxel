"""A (level-0 casters) | B (casters by size on screen) | where they differ (> 24/255, red over a dimmed A).
Usage: f214c_sheet.py pose P"""
import os, sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
BOX = (242, 45, 1196, 670)
pose, P = sys.argv[1], sys.argv[2]
A = Image.open(os.path.join(HERE, f"f214c_{pose}_P{P}_A.png")).convert("RGB").crop(BOX)
B = Image.open(os.path.join(HERE, f"f214c_{pose}_P{P}_B.png")).convert("RGB").crop(BOX)
a, b = np.asarray(A).astype(np.int16), np.asarray(B).astype(np.int16)
m = np.any(np.abs(a - b) > 24, axis=2)
d = (a * 0.4).astype(np.uint8)
d[m] = (255, 40, 40)
# grow the marks so single pixels are visible at sheet scale
mm = m.copy()
for dy in (-2, -1, 1, 2):
    mm |= np.roll(m, dy, axis=0)
for dx in (-2, -1, 1, 2):
    mm |= np.roll(m, dx, axis=1)
d[mm] = (255, 40, 40)
D = Image.fromarray(d)
w, h = A.size
sheet = Image.new("RGB", (w * 3, h + 24), (20, 20, 20))
dr = ImageDraw.Draw(sheet)
for i, (im, label) in enumerate(((A, "no handoff (sprigs everywhere)"), (B, "sprigs -> leaf mass at level 2"),
                                 (D, f"differ > 24/255: {m.mean() * 100:.3f}% (marks grown 2 px)"))):
    sheet.paste(im, (i * w, 24))
    dr.text((i * w + 8, 6), f"{pose} P={P}: {label}", fill=(255, 255, 255))
sheet.save(os.path.join(HERE, f"f214c_sheet_{pose}_P{P}.png"))

