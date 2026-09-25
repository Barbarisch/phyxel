"""rv_edge_l4.py - G-169 L4 on the SHIPPED build: you cannot walk off the edge of the world.

User: "characters can walk off the edge of the map". Ravenmere's scenes are baked 3x3-chunk
worlds (farm: x/z -32..63); past the last chunk there is no ground and no wall, so the player
walked into the void and fell forever.

The probe: reach the farm, walk (NavGraph) to 2 u inside the SOUTH edge (z = -32) - straight
south from the entry (6, 16), which crosses no exit region (the west approach at z=16 walked
through the back_to_town exit at x 1..3 and measured the TOWN) - then hold the forward key the
way a player does, sampling the player entity for 12 s.
  RED  (before): z goes below the edge and y plummets (no ground).
  GREEN (after): z never passes the edge (minus the capsule half width) and y never drops below
                 the ground the player stood on.
Usage: python tools/rv_edge_l4.py [red|green]   (label only; the numbers decide)
"""
import json
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

REL = Path(r"C:/Users/bpete/Documents/PhyxelProjects/Ravenmere/build_probe/Release")
OUT = Path(r"C:/Users/bpete/Documents/GitHub/phyxel/docs/evidence/ravenmere")
PORT = 8127
B = 'http://127.0.0.1:%d' % PORT
LABEL = sys.argv[1] if len(sys.argv) > 1 else 'run'
SOUTH_EDGE_Z = -32.0
HOLD_SECONDS = 8.0


def safe(m, p, b=None, t=60):
    try:
        r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None,
                                   method=m, headers={'Content-Type': 'application/json'})
        return json.load(urllib.request.urlopen(r, timeout=t))
    except Exception as e:
        return {'err': str(e)[:200]}


def scene():
    return safe('GET', '/api/screen/state').get('scene_id')


def player_pos():
    st = safe('GET', '/api/state')
    for e in st.get('entities', []):
        if e.get('id') == 'player':
            p = e.get('position') or {}
            return (float(p.get('x', 0)), float(p.get('y', 0)), float(p.get('z', 0)))
    return None


def go(trigger_id, want_scene):
    safe('POST', '/api/rpg/fire_trigger', {'id': trigger_id})
    for _ in range(60):
        if scene() == want_scene:
            time.sleep(4)
            return True
        time.sleep(1)
    return False


log = open(OUT / ('rv_edge_l4_%s.log' % LABEL), 'w', encoding='utf-8', errors='replace')
proc = subprocess.Popen([str(REL / 'Ravenmere.exe'), '--test', str(PORT)], cwd=str(REL),
                        stdout=log, stderr=subprocess.STDOUT)
res = {'label': LABEL}
try:
    for _ in range(180):
        if 'err' not in safe('GET', '/api/state', t=3):
            break
        time.sleep(1)
    reached = False
    for _a in range(6):
        safe('POST', '/api/ui/click', {'x': 640, 'y': 374})
        for _ack in range(12):   # G-116: the intro card gates the first scene - Continue sits at (640, 628)
            if safe('GET', '/api/screen/state').get('screen') == 'narrative':
                safe('POST', '/api/ui/click', {'x': 640, 'y': 628}); break
            time.sleep(0.25)
        for _ in range(40):
            sc = safe('GET', '/api/screen/state')
            if sc.get('scene_id') == 'town' and sc.get('screen') == 'playing':
                reached = True
                break
            time.sleep(1)
        if reached:
            break
    assert reached, 'never reached the town'
    time.sleep(3)
    assert go('to_farm', 'farm'), 'never reached the farm'
    res['farm_entry'] = player_pos()

    # 1. NavGraph walk to 2 u inside the south edge (the graph ends at the chunk rectangle).
    w = safe('POST', '/api/rpg/walk_to', {'x': 6.0, 'y': 18.0, 'z': SOUTH_EDGE_Z + 2.0})
    res['walk_to'] = w
    for _ in range(90):
        ws = safe('POST', '/api/rpg/walk_status', {})
        if not ws.get('active'):
            break
        time.sleep(0.5)
    assert scene() == 'farm', 'left the farm during the walk: %s' % scene()
    res['walk_result'] = safe('POST', '/api/rpg/walk_status', {}).get('result')
    res['at_edge'] = player_pos()
    print('walked to %s (%s)' % (res['at_edge'], res['walk_result']), flush=True)
    time.sleep(1.0)
    res['bounds_before'] = safe('POST', '/api/rpg/play_bounds', {})

    # 2. Hold the forward key, as a player does, and watch the entity.
    inj = safe('POST', '/api/rpg/inject_input', {'keys': ['MoveForward'], 'hold': HOLD_SECONDS})
    res['inject'] = inj
    samples = []
    t0 = time.monotonic()
    while time.monotonic() - t0 < HOLD_SECONDS + 4.0:
        p = player_pos()
        if p:
            samples.append([round(time.monotonic() - t0, 2)] + [round(v, 3) for v in p])
        time.sleep(0.5)
    res['samples'] = samples
    zs = [s[3] for s in samples]
    ys = [s[2] for s in samples]
    res['min_z'] = min(zs)
    res['min_y'] = min(ys)
    res['final'] = samples[-1][1:] if samples else None
    res['moved'] = round(abs(samples[-1][1] - res['at_edge'][0]) + abs(samples[-1][3] - res['at_edge'][2]), 3) if samples else None
    res['scene_after'] = scene()
    print('hold W %.0fs: min_z=%.2f (edge %.0f)  min_y=%.2f  final=%s  moved=%s  scene=%s' % (
        HOLD_SECONDS, res['min_z'], SOUTH_EDGE_Z, res['min_y'], res['final'], res['moved'], res['scene_after']), flush=True)
finally:
    proc.terminate()
    time.sleep(2)
    log.flush()

(OUT / ('rv_edge_l4_%s.json' % LABEL)).write_text(json.dumps(res, indent=2), encoding='utf-8')
ground_y = res.get('at_edge', (0, 17.0, 0))[1]
stayed_in = res.get('min_z', -999) >= SOUTH_EDGE_Z - 0.01 and res.get('scene_after') == 'farm'
stayed_up = res.get('min_y', -999) >= ground_y - 0.5
fell = res.get('min_y', 0) < ground_y - 5.0
print('\nVERDICT: %s' % (
    'the edge holds - the player stayed inside z>=%.0f and on the ground' % SOUTH_EDGE_Z if (stayed_in and stayed_up)
    else 'FELL OFF THE WORLD (min_y %.1f) - the defect is live' % res.get('min_y', 0) if fell
    else 'NOT PROVEN - the player neither held the edge nor fell (did the key move them west at all? moved=%s)' % res.get('moved')),
    flush=True)
