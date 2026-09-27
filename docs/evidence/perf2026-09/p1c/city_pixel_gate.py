"""GENERIC city pixel gate: toggle ANY boolean debug knob (route + key) instead of the structure skip.
Usage: city_pixel_gate.py '[x,y,z,yaw,pitch]' out.json <route> <key> [--freeze | --night-freeze]
   e.g. city_pixel_gate.py "$POSE" out.json /api/debug/depth_prepass enabled --freeze
Derived from the structure-LOD skip pixel gate below; A1 = knob ON, B = OFF, C = ON.

Structure-LOD skip pixel gate (docs/PerfProgram2026-09.md section 16.12), the gi_pixel_gate.py method:
with s_structureLodSkipInvisible ON and OFF the frame must be the same. The skip removes a proxy's
main-view draw only when the shader would discard every fragment of it (minFade 0, base inside the
fade start), so the prediction is IDENTICAL up to the timing-matched noise floor.

Control: C is a skip-ON capture after the SAME toggle + settle + wait as B (skip OFF), so diff(A1, C) is
the noise floor at matched timing (residents walk, grass sways). PASS = diff(A1, B) no larger than the
control at p99.9 by more than 1/255 and no more pixels over 8/255. Also records how many proxies the
skip removed in that frame (a gate that skipped nothing proves nothing). Linear + shipping tone curves.
--freeze removes the temporal sources first (render_pixel_diff.py methodology): grass + foliage off
and the GAME paused (residents stop walking; harmless at noon -- the emissive reconcile pause stops
only matters at night). Frozen, the control must read ~0 or the run is void.
Usage: structure_skip_pixel_gate.py '[x,y,z,yaw,pitch]' out.json [--freeze]"""
import json, os, sys, time, urllib.request

import numpy as np
from PIL import Image

B = 'http://127.0.0.1:8090'
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..'))
VIEW = (242, 43, 1197, 672)


def call(m, p, b=None, t=60):
    r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(r, timeout=t))


def settle():
    for _ in range(600):
        c = call('GET', '/api/debug/load_state')['chunks']
        if not (c['generation_pending'] or c['remesh_pending'] or c['remesh_idle_pending']):
            return
        time.sleep(0.1)


def cap():
    time.sleep(1.5)
    p = call('GET', '/api/screenshot')['path']
    a = np.array(Image.open(os.path.join(REPO, p)).convert('RGB')).astype(np.int32)
    x0, y0, x1, y1 = VIEW
    return a[y0:y1, x0:x1], p


ROUTE, KEY = sys.argv[3], sys.argv[4]


def skip(on):
    r = call('POST', ROUTE, {KEY: on})
    assert r.get('success') and r.get(KEY) == on, r   # the knob must echo the state it took
    settle()
    time.sleep(1.0)
    return r.get(KEY)


def stats(d):
    m = d.max(axis=2)
    return {'max': int(m.max()), 'p999': float(np.percentile(m, 99.9)), 'over8': int((m > 8).sum()),
            'mean': float(m.mean())}


pose = json.loads(sys.argv[1])
# FROZEN MODES (never the game pause -- 2026-09-27: pausing drew the pause MENU into the captures and
# made POST /api/camera a no-op, so every paused gate photographed the PREVIOUS gate's pose; it also
# stops the emissive reconcile). Both modes: grass/foliage off, residents + wildlife suspended, effect
# time + GI probe rotation held, the player parked out of view, then a convergence wait.
#   --freeze        noon (12:00)       --night-freeze  night (22:00)
NIGHT_TOD = '--night-freeze' in sys.argv
NIGHT = NIGHT_TOD or '--freeze' in sys.argv      # NIGHT == "frozen" from here on
FREEZE = False                                   # the old game-pause path, retired
TOD = 22.0 if NIGHT_TOD else 12.0


def residents_now():
    npcs = call('GET', '/api/npcs')
    lst = npcs.get('npcs', npcs if isinstance(npcs, list) else [])
    return len(lst)


if FREEZE or NIGHT:
    call('POST', '/api/debug/grass', {'enabled': False})
    call('POST', '/api/debug/foliage', {'enabled': False})
if FREEZE:
    call('POST', '/api/game/pause', {'paused': True})
# The PLAYER is the one moving thing the night freeze cannot stop (its idle animation, and at night
# its held torch light): measured as the residual in the first night controls (a figure in frame at
# the overview/rooftop poses, a moving light at street). Park it outside every S-3 pose's view --
# (0, ground, 185): behind the overview and square cameras, > 45 deg off-axis for the street, rooftop
# and outside cameras, inside the anchor's streamed area -- and put it back afterwards.
PARK = (0.0, 185.0)
player_home = None
if NIGHT:
    ents = call('GET', '/api/entities')
    for e in ents.get('entities', ents if isinstance(ents, list) else []):
        if e.get('id') == 'player':
            player_home = e.get('position')
    if player_home:
        gy = call('GET', f'/api/world/terrain_height?x={int(PARK[0])}&z={int(PARK[1])}', t=60).get('spawn_y')
        if gy is None:
            # The park spot is only resident around the site: stream it from the anchor, then retry.
            call('POST', '/api/camera', {'mode': 'free', 'position': {'x': 0, 'y': 100, 'z': 0},
                                         'yaw': -90, 'pitch': -89})
            time.sleep(2.0)
            settle()
            gy = call('GET', f'/api/world/terrain_height?x={int(PARK[0])}&z={int(PARK[1])}', t=60).get('spawn_y')
        assert gy is not None, 'park spot (0, 185) has no terrain even from the anchor'
        r = call('POST', '/api/entity/move', {'id': 'player', 'position': {'x': PARK[0], 'y': float(gy), 'z': PARK[1]}})
        assert r.get('success', True) and 'error' not in r, r
    r = call('POST', '/api/debug/residents', {'enabled': False, 'fauna': False})
    assert r.get('success') and r.get('enabled') is False and r.get('fauna') is False, r
    r = call('POST', '/api/debug/effect_time', {'frozen': True})
    assert r.get('success') and r.get('frozen') is True, r
call('POST', '/api/camera', {'mode': 'free', 'position': {'x': pose[0], 'y': pose[1], 'z': pose[2]},
                             'yaw': pose[3], 'pitch': pose[4]})
call('POST', '/api/daynight/set', {'timeOfDay': TOD, 'paused': True})
orig = call('POST', '/api/debug/tonemap', {})
out = {'pose': pose, 'frozen': NIGHT, 'freeze_method': 'no-pause (residents/fauna off, effect time held, player parked)',
       'time_of_day': TOD, 'route': ROUTE,
       'key': KEY, 'curves': {}}
cam = call('GET', '/api/camera')
cp = cam.get('position', {})
pose_err = max(abs(cp.get('x', 1e9) - pose[0]), abs(cp.get('y', 1e9) - pose[1]), abs(cp.get('z', 1e9) - pose[2]))
out['pose_readback_err'] = pose_err
assert pose_err <= 0.05, f'camera did not take the pose (err {pose_err}): {cam}'
if NIGHT:
    time.sleep(2.0)
    out['npcs_at_start'] = residents_now()
if FREEZE or NIGHT:
    # CONVERGENCE WAIT: the GI probe grid follows the camera and each probe blends toward its value
    # over many frames, so right after a pose change the ambient is still moving (measured: broad
    # wall/roof blotches up to 8/255 linear in the first rooftop night control). Wait until two
    # captures 2 s apart agree within 2/255, max 30 s, and record how long it took.
    t0 = time.time()
    prev, _ = cap()
    while True:
        cur, _ = cap()
        drift = int(np.abs(cur - prev).max())
        if drift <= 2 or time.time() - t0 > 30:
            break
        prev = cur
    out['converge_s'] = round(time.time() - t0, 1)
    out['converge_last_drift'] = drift
    out['player_parked_from'] = player_home
ok = True
try:
    for curve_name, tm in (('linear', {'exposure': 1.0, 'curve': 0}),
                           ('shipping', {'exposure': orig['exposure'], 'curve': orig['curve']})):
        call('POST', '/api/debug/tonemap', tm)
        skipped_on = skip(True)
        a1, p1 = cap()
        skipped_off = skip(False)
        b, pb = cap()
        skip(True)
        c, p2 = cap()
        ctrl, test = stats(np.abs(a1 - c)), stats(np.abs(a1 - b))
        good = test['p999'] <= ctrl['p999'] + 1 and test['over8'] <= ctrl['over8']
        # A frozen run whose control is not ~0 measured the noise, not the change: VOID (17.2 step 2:
        # control max <= 3/255 and 0 px over 8/255; the GI probe rotation is the known residual).
        void = (FREEZE or NIGHT) and (ctrl['max'] > 3 or ctrl['over8'] > 0)
        ok &= good and not void
        out['curves'][curve_name] = {'control_A1_C': ctrl, 'test_A1_B': test, 'pass': bool(good),
                                     'void': bool(void),
                                     'skipped_with_skip_on': skipped_on, 'skipped_with_skip_off': skipped_off,
                                     'captures': [p1, p2, pb]}
        print(f"{curve_name:8s} knob on={skipped_on} off={skipped_off}  control {ctrl}  test {test}  "
              f"{'VOID (control not frozen)' if void else ('PASS' if good else 'FAIL')}")
finally:
    call('POST', '/api/debug/tonemap', {'exposure': orig['exposure'], 'curve': orig['curve']})
    call('POST', ROUTE, {KEY: False})   # generic gate leaves the knob OFF (the shipped default)
    if NIGHT:
        out['npcs_at_end'] = residents_now()   # must still be the start count (no respawn mid-run)
        call('POST', '/api/debug/effect_time', {'frozen': False})
        call('POST', '/api/debug/residents', {'enabled': True, 'fauna': True})
        if player_home:
            call('POST', '/api/entity/move', {'id': 'player', 'position': player_home})
    if FREEZE:
        call('POST', '/api/game/pause', {'paused': False})
    if FREEZE or NIGHT:
        call('POST', '/api/debug/grass', {'enabled': True})
        call('POST', '/api/debug/foliage', {'enabled': True})
json.dump(out, open(sys.argv[2], 'w'), indent=1)
if NIGHT and out.get('npcs_at_end') != out.get('npcs_at_start'):
    ok = False
    print('VOID: resident count changed during the run', out.get('npcs_at_start'), '->', out.get('npcs_at_end'))
print('PIXEL GATE PASSED' if ok else 'PIXEL GATE FAILED')
sys.exit(0 if ok else 1)
