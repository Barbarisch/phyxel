"""F2.14 gate, rescored from the saved frames with a margin (the first scoring used one threshold: a pixel ~10 from the
background straddled it -- f214_where.py showed every flagged pixel had M = L within 2 levels, i.e. no hole).
  empty   = within T of the background on every channel
  covered = more than 3T from the background on some channel; pixels in between are ambiguous and not scored
  hole      = empty in M, covered in EVERY single-level frame
  fattening = covered in M, empty in EVERY single-level frame
  detector  = empty in M, covered in L0 (coarse levels lose thin parts there: proof the detector finds empties)
Also: a SENSITIVITY check with a synthetic hole -- each M with a 6x6 block replaced by the background where every
level covers -- must report >= 36 holes."""
import json, os

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
BOX = (45, 670, 242, 1196)
T = 10


def load(n):
    a = np.asarray(Image.open(os.path.join(HERE, n)).convert("RGB")).astype(np.int16)
    return a[BOX[0]:BOX[1], BOX[2]:BOX[3]]


def d(a, e):
    return np.max(np.abs(a - e), axis=2)


out = {}
for pose in ("overview_south", "top_down", "forest_floor_open", "east_low", "canopy_close"):
    E = load(f"f214_{pose}_E.png")
    L = [load(f"f214_{pose}_L{k}.png") for k in range(4)]
    covAll = np.all([d(l, E) > 3 * T for l in L], axis=0)
    empAll = np.all([d(l, E) <= T for l in L], axis=0)
    for P in ("2", "4", "8"):
        M = load(f"f214_{pose}_P{P}.png")
        dm = d(M, E)
        hole = int(np.sum((dm <= T) & covAll))
        fat = int(np.sum((dm > 3 * T) & empAll))
        det = int(np.sum((dm <= T) & (d(L[0], E) > 3 * T)))
        ys, xs = np.nonzero(covAll)
        S = M.copy()
        sens = -1
        for y, x in zip(ys, xs):
            if y + 6 <= S.shape[0] and x + 6 <= S.shape[1] and covAll[y:y + 6, x:x + 6].all():
                S[y:y + 6, x:x + 6] = E[y:y + 6, x:x + 6]
                sens = int(np.sum((d(S, E) <= T) & covAll)) - hole
                break
        out[f"{pose} P{P}"] = {"hole": hole, "fattening": fat, "detector": det, "sensitivity_6x6": sens,
                               "covered_by_every_level_px": int(covAll.sum())}
        print(pose, P, out[f"{pose} P{P}"])
json.dump(out, open(os.path.join(HERE, "f214_gate_rescored.json"), "w"), indent=1)
