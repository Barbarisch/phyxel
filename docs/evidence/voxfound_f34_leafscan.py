"""Where are the Leaf voxels in CityBench_C25_F34's saved chunks (PXB2)? Prints leaf clusters by metre cell."""
import sqlite3, struct, collections, sys

db = sys.argv[1]
c = sqlite3.connect("file:" + db + "?mode=ro", uri=True)
cells = collections.Counter()
for cx, cy, cz, d in c.execute("select chunk_x,chunk_y,chunk_z,data from chunk_blobs"):
    at = 4
    ver = d[at]; flags = d[at + 1]; at += 2
    wide = flags & 1
    npal, = struct.unpack_from("<H", d, at); at += 2
    cubes, subs, micros = struct.unpack_from("<III", d, at); at += 12
    pal = []
    for k in range(npal):
        n = d[at]; at += 1
        pal.append(d[at:at + n].decode()); at += n
    def idx():
        global at
        if wide:
            v, = struct.unpack_from("<H", d, at); at += 2
        else:
            v = d[at]; at += 1
        return v
    runs, = struct.unpack_from("<I", d, at); at += 4
    cell = 0
    for r in range(runs):
        ln, = struct.unpack_from("<H", d, at); at += 2
        m = idx()
        if pal[m] == "Leaf":
            for q in range(cell, cell + ln):
                x, y, z = q // 1024, (q // 32) % 32, q % 32
                cells[(cx * 32 + x, cy * 32 + y, cz * 32 + z)] += 729
        cell += ln
    for kind, n in (("sub", subs), ("micro", micros)):
        for k in range(n):
            ci, = struct.unpack_from("<H", d, at); at += 2
            at += 1 if kind == "sub" else 2
            m = idx()
            f = d[at]; at += 1
            if f & 0x80: at += 3
            if pal[m] == "Leaf":
                x, y, z = ci // 1024, (ci // 32) % 32, ci % 32
                cells[(cx * 32 + x, cy * 32 + y, cz * 32 + z)] += 27 if kind == "sub" else 1
print("leaf metre cells", len(cells), "ninth-volume", sum(cells.values()) / 729.0)
# clusters near the city centre
near = sorted((k for k in cells if abs(k[0]) < 40 and abs(k[2]) < 40), key=lambda k: (k[0], k[2], k[1]))
print("near centre:", len(near), near[:20])
for k in [(17, 51, 15), (18, 51, 15), (12, 52, -15)]: print(k, cells.get(k), "ninths of 729")
import statistics
v = list(cells.values()); print("ninths per leaf cell: median", statistics.median(v), "cells < 365 (less than half):", sum(1 for x in v if x < 365), "of", len(v))
