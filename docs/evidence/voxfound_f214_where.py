"""Where are the F2.14 gate's hole / fattening pixels? Prints each with its colours in E, M and L0..L3, and writes a
crop sheet around them. Usage: f214_where.py pose P"""
import os, sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
BOX = (45, 670, 242, 1196)
T = 10
pose, P = sys.argv[1], sys.argv[2]


def load(n):
    a = np.asarray(Image.open(os.path.join(HERE, f"f214_{pose}_{n}.png")).convert("RGB")).astype(np.int16)
    return a[BOX[0]:BOX[1], BOX[2]:BOX[3]]


E, M = load("E"), load(f"P{P}")
L = [load(f"L{k}") for k in range(4)]
emp = lambda a: np.all(np.abs(a - E) <= T, axis=2)
hole = emp(M) & np.all([~emp(l) for l in L], axis=0)
fat = ~emp(M) & np.all([emp(l) for l in L], axis=0)
for tag, m in (("hole", hole), ("fat", fat)):
    ys, xs = np.nonzero(m)
    for y, x in list(zip(ys, xs))[:12]:
        print(tag, (int(x), int(y)), "E", E[y, x].tolist(), "M", M[y, x].tolist(),
              "L", [l[y, x].tolist() for l in L], "maxdiff M-E", int(np.max(np.abs(M[y, x] - E[y, x]))))
