"""Locate a build-vs-reload voxel difference to the chunk: compares <prefix>_chunks.json (taken at build
time, after save) with <prefix>_chunks_reload.json (after relaunch), both from
GET /api/world/chunks?detail=1. Prints every chunk whose cube/sub/micro counts differ, plus totals.
Usage: chunk_diff.py <prefix>"""
import json, sys


def load(path):
    out = {}
    for c in json.load(open(path)):
        if 'position' not in c:
            continue
        p = c['position']
        out[(p['x'], p['y'], p['z'])] = (c['cubeCount'], c.get('subcubeCount'), c.get('microcubeCount'))
    return out


prefix = sys.argv[1]
a, b = load(prefix + '_chunks.json'), load(prefix + '_chunks_reload.json')
print(f'chunks: build {len(a)}, reload {len(b)}; only at build {len(set(a) - set(b))}, only at reload {len(set(b) - set(a))}')
tot = [0, 0, 0]
for k in sorted(set(a) & set(b)):
    if a[k] != b[k]:
        d = [y - x for x, y in zip(a[k], b[k])]
        tot = [t + x for t, x in zip(tot, d)]
        print(f'  chunk origin {k}: build {a[k]} reload {b[k]} delta cube/sub/micro {tuple(d)}')
print('total delta cube/sub/micro over shared chunks:', tuple(tot))
