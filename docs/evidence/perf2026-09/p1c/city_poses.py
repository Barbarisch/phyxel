"""Derive the S-3 fixed poses (docs/PerfProgram2026-09.md section 16.2) from the GENERATOR's reported
layout, so they land in the same relative place on every rung: the main street (build response
program.main_street), the market square (location registry, if the generator registered one), the
site bounds, and the live surface height. Writes <prefix>_poses.json in perf_harness.py's format
{"name": [x, y, z, yaw, pitch]} plus <prefix>_poses_provenance.json with how each pose was derived.
Run against the live engine with the rung loaded. Yaw convention: 0 = +X, -90 = -Z.
Usage: city_poses.py <prefix> <width> <depth>          e.g. city_poses.py city_C75 160 160"""
import json, sys

from rig_common import call

prefix, W, D = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
CX, CZ = 0, 0
build = json.load(open(prefix + '_build.json'))
ms = build['submit']['program']['main_street']          # {x, z, w, d}: the street's lot rectangle
street_z = ms['z'] + ms['d'] / 2.0                        # centreline


def surface(x, z):
    r = call('GET', f'/api/world/terrain_height?x={int(round(x))}&z={int(round(z))}', t=60)
    if r.get('surface_y') is None:
        raise SystemExit(f'no surface at ({x},{z}): {r}')
    return float(r['surface_y'])


prov, poses = {}, {}
# 1. Street level in the core: eye height 1.7 above the paving, a quarter of the way along the main
#    street from its west end, looking east down it.
sx = ms['x'] + ms['w'] * 0.25
poses['street'] = [sx, surface(sx, street_z) + 1.7, street_z, 0.0, -5.0]
prov['street'] = f'main_street centreline z={street_z}, x = west end + w/4, eye 1.7 u, looking +X'

# 2. Market square: the generator's own location if registered, else the site centre.
sq = None
try:
    locs = call('GET', '/api/locations', t=60)
    items = locs.get('locations', locs if isinstance(locs, list) else [])
    for loc in items:
        tag = (str(loc.get('type', '')) + ' ' + str(loc.get('name', '')) + ' ' + str(loc.get('id', ''))).lower()
        if 'market' in tag or 'square' in tag or 'plaza' in tag:
            p = loc.get('position') or loc.get('center') or {}
            if 'x' in p and 'z' in p:
                sq = (float(p['x']), float(p['z']), tag.strip())
                break
except Exception as e:
    prov['square_lookup_error'] = str(e)
if sq:
    qx, qz = sq[0], sq[1]
    prov['square'] = f'location registry: {sq[2]} at ({qx},{qz}); eye 1.7 u, looking -Z'
else:
    qx, qz = CX, CZ
    prov['square'] = 'NO market/square location registered: site centre used; eye 1.7 u, looking -Z'
poses['square'] = [qx, surface(qx, qz) + 1.7, qz, -90.0, -5.0]

# 3. Rooftop height along the main street: same x as the street pose, 14 u above the paving.
poses['rooftop'] = [sx, surface(sx, street_z) + 14.0, street_z, 0.0, -12.0]
prov['rooftop'] = 'street pose raised 14 u (above 2-3 storey eaves), looking +X'

# 4. Elevated overview of the whole city: the fingerprint anchor pose.
ov = build['overview_pose']
poses['overview'] = [ov['x'], ov['y'], ov['z'], ov['yaw'], ov['pitch']]
prov['overview'] = 'city_build.py OVERVIEW (the fingerprint anchor)'

# 5. The city seen from outside its wall: 40 u west of the site edge on the street's line, 6 u up.
ox = CX - W / 2.0 - 40.0
poses['outside'] = [ox, surface(ox, street_z) + 6.0, street_z, 0.0, -4.0]
prov['outside'] = f'40 u west of the site edge (x={CX - W / 2.0}) on the main-street line, 6 u up, looking +X'

json.dump(poses, open(prefix + '_poses.json', 'w'), indent=1)
json.dump(prov, open(prefix + '_poses_provenance.json', 'w'), indent=1)
print(json.dumps(poses, indent=1))
print(json.dumps(prov, indent=1))
