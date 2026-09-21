"""ceiling_build.py — build the vertical-sweep rig and prove it from the world.

A large flat CEILING with a short skirt, high above the treeline, over clear ground. The camera
flies straight up beneath it looking at it, so the ceiling's underside stays huge and centred in
frame at every sample while its distance from the camera falls through the probe grid's vertical
reach (about 22 u above the viewer). No occluders, no clearing, no projection maths needed.
"""
import sys, time
sys.path.insert(0, 'tools'); import lighting_lab as lab

CX0, CX1 = 50, 70        # ceiling extent in X
CZ0, CZ1 = -25, -5       # ceiling extent in Z
CY = 50                  # ceiling plane
SKIRT = 4                # walls hanging down from the ceiling edge, to enclose the underside


def cells():
    c = [(x, CY, z) for x in range(CX0, CX1 + 1) for z in range(CZ0, CZ1 + 1)]
    for y in range(CY - SKIRT, CY):
        c += [(x, y, CZ0) for x in range(CX0, CX1 + 1)]
        c += [(x, y, CZ1) for x in range(CX0, CX1 + 1)]
        c += [(CX0, y, z) for z in range(CZ0, CZ1 + 1)]
        c += [(CX1, y, z) for z in range(CZ0, CZ1 + 1)]
    return sorted(set(c))


def scan():
    out = {}
    for y0 in range(CY - SKIRT, CY + 1, 8):
        r = lab.post('/api/world/scan_micro', {'x1': CX0, 'y1': y0, 'z1': CZ0,
                                               'x2': CX1, 'y2': min(y0 + 7, CY), 'z2': CZ1})
        for c in r.get('cells', []):
            out[(c['x'], c['y'], c['z'])] = sum(c.get('counts', {}).values())
    return out


def main():
    lab.require_engine()
    print('generating vertical chunk y=1 (world y 32..63) over x0..3, z=-1 ...', flush=True)
    print('  ', lab.job('generate_world', {'type': 'Flat', 'seed': 1,
                                           'from': {'x': 0, 'y': 1, 'z': -1},
                                           'to': {'x': 3, 'y': 1, 'z': -1}}, 300), flush=True)
    time.sleep(2)
    lab.post('/api/world/voxel', {'x': CX0, 'y': CY, 'z': CZ0, 'material': 'StoneBricks'})
    time.sleep(1.5)
    if not lab.get('/api/world/voxel', {'x': CX0, 'y': CY, 'z': CZ0}).get('exists'):
        print('BLOCKED: voxels above y=31 do not persist, so this rig is impossible here'); return 2
    print('  a voxel at y=%d persists, so the high chunk is real' % CY)

    lab.fill(CX0, CY, CZ0, CX1, CY, CZ1, 'StoneBricks')
    for y in range(CY - SKIRT, CY):
        lab.fill(CX0, y, CZ0, CX1, y, CZ0, 'StoneBricks')
        lab.fill(CX0, y, CZ1, CX1, y, CZ1, 'StoneBricks')
        lab.fill(CX0, y, CZ0, CX0, y, CZ1, 'StoneBricks')
        lab.fill(CX1, y, CZ0, CX1, y, CZ1, 'StoneBricks')
    time.sleep(3)
    want = cells()
    for attempt in range(4):
        have = scan()
        missing = [c for c in want if have.get(c, 0) == 0]
        print('  attempt %d: %d/%d solid' % (attempt, len(want) - len(missing), len(want)), flush=True)
        if not missing:
            print('RIG COMPLETE'); return 0
        for x, y, z in missing:
            lab.post('/api/world/voxel', {'x': x, 'y': y, 'z': z, 'material': 'StoneBricks'})
        time.sleep(2)
    print('RIG INCOMPLETE'); return 2


if __name__ == '__main__':
    sys.exit(main())
