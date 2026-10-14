"""V4 -- the streaming focus follows the camera path, on R-P2 (docs/PerfProgram2026-09.md section 16.4). L4.

Rig R-P2: project PerfRigStream (Flat, streaming:true, loadRadius 2 chunks = 64 u, no flora), fresh
process. Player at spawn (16, 18, 40). Path: a straight line along +x at y=22, z=40 from x=40 to x=200,
8 u/s -- it enters chunks x=64, 96, 128, 160 (chunk coords 2..6), the last three more than 64 u from the
player, so they are loaded only if streaming follows the camera.
Prediction (written before running):
  stream_follow:true  -> the chunk under the camera is resident on EVERY chunk entry;
  stream_follow:false -> chunk entries at x >= 96 (coords 3, 4, 5, 6) are NOT resident (control).
Also checked: after the path ends, the focus is released (holder empty).
Usage: v4_stream_follow.py out.json"""
import json, math, sys, time

from rig_common import call, route, settle, wait_api

WAYPOINTS = [{'x': 40, 'y': 22, 'z': 40, 'yaw': 0, 'pitch': -10},
             {'x': 200, 'y': 22, 'z': 40, 'yaw': 0, 'pitch': -10}]
SPEED = 8.0


def entries(rows):
    """(chunk_x, resident) at the first recorded frame inside each new chunk along the path."""
    seen, out = set(), []
    for r in rows:
        if r['path_progress'] < 0:
            continue
        cx = math.floor(r['camera'][0] / 32.0)
        if cx not in seen:
            seen.add(cx)
            out.append((cx, r['streaming']['camera_chunk_resident']))
    return out


wait_api()
settle()
out = {'rig': 'R-P2 (PerfRigStream, Flat streaming, loadRadius 2)', 'waypoints': WAYPOINTS, 'speed': SPEED}
ok = True
# CONTROL FIRST on the fresh world: chunks the follow arm streams in stay resident inside the
# 128 u unload radius afterwards and would make the control falsely read resident.
for follow in (False, True):
    start, rec = route(WAYPOINTS, SPEED, stream_follow=follow)
    time.sleep(0.5)
    holder_after = call('GET', '/api/camera/path')['focus_holder']
    e = entries(rec['rows'])
    if follow:
        good = len(e) >= 5 and all(res is True for _, res in e)
    else:
        far = [res for cx, res in e if cx >= 3]
        good = len(far) >= 3 and all(res is False for res in far)
    good = good and holder_after == '' and not rec['truncated']
    ok &= good
    r = {'stream_follow': follow, 'entries': e, 'focus_holder_after': holder_after,
         'frames': rec['frames'], 'truncated': rec['truncated'], 'pass': good}
    out['follow' if follow else 'control'] = r
    print(json.dumps(r))
    settle(240)
out['pass'] = ok
json.dump(out, open(sys.argv[1], 'w'), indent=1)
print('V4', 'PASS' if ok else 'FAIL')
sys.exit(0 if ok else 1)
