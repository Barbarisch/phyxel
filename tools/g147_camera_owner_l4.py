"""g147_l4.py — L4 on the SHIPPED build: the player's own body is dropped from the main pass while
the camera is inside it.

WHAT THIS DOES AND DOES NOT COVER. The shipped test API can inject keys but not a mouse WHEEL, and
exposes no rig-zoom command, so this cannot drive the zoom-to-first-person input path. It drives
the camera to the player's eye point directly, which exercises the exact same geometric test the
rig's collapsed boom produces. The input path is covered by the rig's own unit tests; the gap in
the harness is logged.

ASSERTIONS, not a screenshot read:
  - third-person default: the owner is drawn (camera_owner_hidden false)
  - camera at the player's eyes: suppressed (camera_owner_hidden true)
  - camera back out: drawn again, so this gates rather than deletes
Screenshots are captured alongside as the human-checkable record.
"""
import json, subprocess, time, urllib.request
from pathlib import Path

REL = Path(r"C:/Users/bpete/Documents/PhyxelProjects/Ravenmere/build_probe/Release")
OUT = Path(r"C:/Users/bpete/Documents/GitHub/phyxel/docs/evidence/ravenmere")
B = 'http://127.0.0.1:8104'


def call(m, p, b=None, t=120):
    r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None,
                               method=m, headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(r, timeout=t))


def safe(m, p, b=None):
    try:
        return call(m, p, b)
    except Exception as e:
        return {'err': str(e)[:160]}


def stats():
    return safe('GET', '/api/rpg/get_render_stats')


def shot(tag):
    r = safe('GET', '/api/screenshot')
    p = r.get('path')
    if p:
        (OUT / ('rv_g147_%s.png' % tag)).write_bytes((REL / p).read_bytes())


log = open(OUT / 'rv_g147_l4.log', 'w', encoding='utf-8', errors='replace')
proc = subprocess.Popen([str(REL / 'Ravenmere.exe'), '--test', '8104'], cwd=str(REL),
                        stdout=log, stderr=subprocess.STDOUT)
res = {}
try:
    for _ in range(180):
        try:
            call('GET', '/api/state', t=3); break
        except Exception:
            time.sleep(1)
    safe('POST', '/api/ui/click', {'x': 640, 'y': 374})
    for _ in range(120):
        sc = safe('GET', '/api/screen/state')
        if sc.get('scene_id') == 'town' and sc.get('screen') == 'playing':
            break
        time.sleep(1)
    time.sleep(4)

    res['third_person'] = stats(); shot('third_person')
    print('third person     :', json.dumps(res['third_person'])[:210], flush=True)

    st = safe('GET', '/api/state')
    pl = next((e for e in st.get('entities', []) if e.get('id') == 'player'), {})
    p = pl.get('position', {'x': 0.0, 'y': 17.0, 'z': 0.0})
    print('player at', p, flush=True)
    # The eye point the collapsed boom would put the camera at: inside the head.
    safe('POST', '/api/rpg/set_camera',
         {'detach': True, 'x': p['x'], 'y': p['y'] + 1.4, 'z': p['z'], 'yaw': 90.0, 'pitch': 0.0})
    time.sleep(3)
    res['camera_at_eyes'] = stats(); shot('camera_at_eyes')
    print('camera at eyes   :', json.dumps(res['camera_at_eyes'])[:210], flush=True)

    # Well clear of the body again.
    safe('POST', '/api/rpg/set_camera',
         {'detach': True, 'x': p['x'] - 6.0, 'y': p['y'] + 2.0, 'z': p['z'], 'yaw': 0.0, 'pitch': -10.0})
    time.sleep(3)
    res['camera_backed_off'] = stats(); shot('camera_backed_off')
    print('camera backed off:', json.dumps(res['camera_backed_off'])[:210], flush=True)
    safe('POST', '/api/rpg/set_camera', {'detach': False})

    (OUT / 'rv_g147_l4.json').write_text(json.dumps(res, indent=2), encoding='utf-8')
    a, b_, c = res['third_person'], res['camera_at_eyes'], res['camera_backed_off']
    # NOT characters_drawn_main: moving the camera changes which OTHER characters are in frustum,
    # so the count is confounded and says nothing about the owner. The flag is the engine's own
    # statement that it suppressed the owner this frame, and the screenshots are the human record.
    ok = (a.get('camera_owner_hidden') is False
          and b_.get('camera_owner_hidden') is True
          and c.get('camera_owner_hidden') is False)
    print('\nRESULT:', 'PASS' if ok else 'FAIL', flush=True)
finally:
    proc.terminate()
    log.flush()
