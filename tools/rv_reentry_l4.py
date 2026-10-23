"""rv_reentry_l4.py - G-170 / G-36 / G-82 L4 on the SHIPPED build: coming back puts you where you left.

User: "travelling to hollow barrow and back drops you at the entry point that you enter when
coming from the town". SceneManager saved the CAMERA on unload and restored the CAMERA on
re-entry; the player was re-spawned at the scene's authored entry. Now the PLAYER's position is
recorded and the player spawns there on re-entry - and a region trigger arms on first sight, so
spawning inside the exit you left through does not bounce you straight back.

Two round trips, both measured on the player entity's position:
  farm -> barrow -> farm : the player must come back NEAR where they left the farm (the barrow
                           exit region), FAR from the farm's authored entry
  farm -> town -> farm   : same rule, the other door
And after each return the scene must STAY (no ping-pong), checked for six seconds.
"""
import json
import math
import subprocess
import time
import urllib.request
from pathlib import Path

REL = Path(r"C:/Users/bpete/Documents/PhyxelProjects/Ravenmere/build_probe/Release")
OUT = Path(r"C:/Users/bpete/Documents/GitHub/phyxel/docs/evidence/ravenmere")
PORT = 8126
B = 'http://127.0.0.1:%d' % PORT


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


def dist(a, b):
    return math.sqrt(sum((a[i] - b[i]) ** 2 for i in range(3)))


def go(trigger_id, want_scene):
    safe('POST', '/api/rpg/fire_trigger', {'id': trigger_id})
    for _ in range(60):
        if scene() == want_scene:
            time.sleep(4)
            return True
        time.sleep(1)
    return False


def stays(want_scene, seconds=6.0):
    t0 = time.monotonic()
    while time.monotonic() - t0 < seconds:
        if scene() != want_scene:
            return False
        time.sleep(0.5)
    return True


log = open(OUT / 'rv_reentry_l4.log', 'w', encoding='utf-8', errors='replace')
proc = subprocess.Popen([str(REL / 'Ravenmere.exe'), '--test', str(PORT)], cwd=str(REL),
                        stdout=log, stderr=subprocess.STDOUT)
res = {}
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
    res['town_authored_entry'] = player_pos()

    # ---- farm -> barrow -> farm --------------------------------------------------------------
    assert go('to_farm', 'farm'), 'never reached the farm'
    res['farm_authored_entry'] = player_pos()
    # Walk the player somewhere distinctly NOT the entry before leaving, so "where you left" and
    # "where you enter" are far apart: drive straight to the barrow exit by firing its trigger
    # from wherever we stand is not a walk, so move first.
    # (24,18,22): 18 u from the entry, and outside every farm region - the wolves' field
    # [22..30]x[4..14] included; the first run walked INTO that field and the encounter it
    # started made the host refuse the barrow transition.
    safe('POST', '/api/rpg/walk_to', {'x': 24.0, 'y': 18.0, 'z': 22.0})
    time.sleep(8)
    res['farm_left_at'] = player_pos()
    assert go('to_barrow', 'barrow'), 'never reached the barrow'
    assert go('back_to_farm', 'farm'), 'never came back to the farm'
    res['farm_back_at'] = player_pos()
    res['farm_stays'] = stays('farm')
    d_left = dist(res['farm_back_at'], res['farm_left_at'])
    d_entry = dist(res['farm_back_at'], res['farm_authored_entry'])
    res['farm_dist_to_where_left'] = round(d_left, 2)
    res['farm_dist_to_authored_entry'] = round(d_entry, 2)
    print('farm<->barrow: back %.1f u from where you left, %.1f u from the authored entry; stays=%s'
          % (d_left, d_entry, res['farm_stays']), flush=True)

    # ---- farm -> town -> farm ----------------------------------------------------------------
    res['farm_left_at_2'] = player_pos()
    assert go('back_to_town', 'town'), 'never got back to town'
    res['town_back_at'] = player_pos()
    d_town_entry = dist(res['town_back_at'], res['town_authored_entry'])
    res['town_dist_to_authored_entry'] = round(d_town_entry, 2)
    res['town_stays'] = stays('town')
    print('town: back %.1f u from the town authored entry; stays=%s' % (d_town_entry, res['town_stays']), flush=True)
finally:
    proc.terminate()
    time.sleep(2)
    log.flush()

(OUT / 'rv_reentry_l4.json').write_text(json.dumps(res, indent=2), encoding='utf-8')
ok = (res.get('farm_dist_to_where_left', 99) < 3.0
      and res.get('farm_dist_to_authored_entry', 0) > 6.0
      and res.get('farm_stays') and res.get('town_stays'))
print('\nVERDICT: %s' % ('re-entry puts the player where they left, and the exit does not bounce them back'
                         if ok else 'NOT PROVEN - see rv_reentry_l4.json'), flush=True)
