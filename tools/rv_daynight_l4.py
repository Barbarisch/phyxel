"""rv_daynight_l4.py - G-165 L4 on the SHIPPED build: does the clock actually move now?

The user reported "i dont see day/night cycle going". Root cause: DayNightCycle is OFF by
default and nothing in a shipped game could turn it on - only the editor's ImGui panel and its
HTTP route. Ravenmere's game.json now carries a top-level "dayNight" block, the shell applies
it once at load, and the shipped test API mirrors /api/daynight so the clock can be READ.

The objective measurement is the clock delta over a timed wait, compared against what the
authored day length predicts. Two captures bracket it so the sky/shadow change can be seen.
"""
import json
import subprocess
import time
import urllib.request
from pathlib import Path

REL = Path(r"C:/Users/bpete/Documents/PhyxelProjects/Ravenmere/build_probe/Release")
OUT = Path(r"C:/Users/bpete/Documents/GitHub/phyxel/docs/evidence/ravenmere")
PORT = 8123
B = 'http://127.0.0.1:%d' % PORT
WAIT_S = 45.0


def safe(m, p, b=None, t=60):
    try:
        r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None,
                                   method=m, headers={'Content-Type': 'application/json'})
        return json.load(urllib.request.urlopen(r, timeout=t))
    except Exception as e:
        return {'err': str(e)[:200]}


def clock():
    r = safe('GET', '/api/rpg/daynight')
    return r.get('daynight') or {}


def shot(name):
    r = safe('GET', '/api/screenshot')
    if not r.get('path'):
        return None
    dst = OUT / ('rv_daynight_l4_%s.png' % name)
    dst.write_bytes((REL / r['path']).read_bytes())
    return dst.name


log = open(OUT / 'rv_daynight_l4.log', 'w', encoding='utf-8', errors='replace')
proc = subprocess.Popen([str(REL / 'Ravenmere.exe'), '--test', str(PORT)], cwd=str(REL),
                        stdout=log, stderr=subprocess.STDOUT)
res = {'exe_mtime': time.strftime('%Y-%m-%d %H:%M:%S',
                                  time.localtime((REL / 'Ravenmere.exe').stat().st_mtime))}
try:
    for _ in range(180):
        if 'err' not in safe('GET', '/api/state', t=3):
            break
        time.sleep(1)
    reached = False
    for _a in range(6):
        safe('POST', '/api/ui/click', {'x': 640, 'y': 374})
        for _ in range(40):
            sc = safe('GET', '/api/screen/state')
            if sc.get('scene_id') == 'town' and sc.get('screen') == 'playing':
                reached = True
                break
            time.sleep(1)
        if reached:
            break
    assert reached, 'never reached the town'
    time.sleep(4)

    # Park the camera on the sky so the two captures compare sun position, not NPCs walking.
    st = safe('GET', '/api/state')
    pl = next((e for e in st.get('entities', []) if e.get('id') == 'player'), {})
    p = pl.get('position', {'x': 0.0, 'y': 17.0, 'z': 0.0})
    safe('POST', '/api/rpg/set_camera', dict(detach=True, x=p['x'], y=p['y'] + 1.7, z=p['z'],
                                            yaw=0.0, pitch=20.0))
    time.sleep(2)

    c0 = clock()
    t0 = time.monotonic()
    res['clock_first'] = c0
    # FPS at this pose. The pre-fix defect predicts a clock rate of 0.1 s x FPS per real second
    # (a stale frame-start stamp clamps every frame's dt to the 0.1 s cap), so the FPS is what
    # turns "2.86x too fast" from a coincidence into a cause.
    fps_reads = []
    for _ in range(8):
        rs = safe('GET', '/api/rpg/get_gpu_scopes')   # carries the per-frame fps
        if isinstance(rs.get('fps'), (int, float)): fps_reads.append(rs['fps'])
        time.sleep(0.25)
    res['fps_at_pose'] = round(sum(fps_reads) / len(fps_reads), 1) if fps_reads else None
    res['predicted_ratio_if_clamped'] = round(0.1 * res['fps_at_pose'], 2) if res['fps_at_pose'] else None
    print('fps at pose = %s  -> if dt is clamped to 0.1 s every frame the clock would run %sx'
          % (res['fps_at_pose'], res['predicted_ratio_if_clamped']), flush=True)
    res['shot_first'] = shot('1_first')
    print('first read: enabled=%s timeOfDay=%s dayLength=%s'
          % (c0.get('enabled'), c0.get('timeOfDay'), c0.get('dayLengthSeconds')), flush=True)
    assert c0.get('enabled') is True, 'the authored block did not enable the cycle: %s' % c0

    time.sleep(WAIT_S)
    c1 = clock()
    elapsed = time.monotonic() - t0
    res['clock_second'] = c1
    res['elapsed_s'] = round(elapsed, 2)
    res['shot_second'] = shot('2_after_wait')

    day_len = float(c0.get('dayLengthSeconds') or 0.0)
    scale = float(c0.get('timeScale') or 1.0)
    predicted = 24.0 * elapsed / day_len * scale if day_len > 0 else None
    advanced = (float(c1.get('timeOfDay')) - float(c0.get('timeOfDay'))) % 24.0
    res['predicted_advance_h'] = round(predicted, 3) if predicted is not None else None
    res['measured_advance_h'] = round(advanced, 3)
    res['measured_ratio'] = round(advanced / predicted, 3) if predicted else None
    print('after %.1f s: timeOfDay %s -> %s  advanced %.3f h  (predicted %.3f h from a %s s day)'
          % (elapsed, c0.get('timeOfDay'), c1.get('timeOfDay'), advanced, predicted or -1, day_len),
          flush=True)

    # Control: pause it and confirm the clock STOPS, so "advanced" above means the cycle and not
    # some other drift.
    safe('POST', '/api/rpg/daynight_set', {'paused': True})
    c2 = clock()
    time.sleep(6)
    c3 = clock()
    res['paused_drift_h'] = round((float(c3.get('timeOfDay')) - float(c2.get('timeOfDay'))) % 24.0, 4)
    print('paused control: drift over 6 s = %.4f h' % res['paused_drift_h'], flush=True)
    safe('POST', '/api/rpg/daynight_set', {'paused': False})
    safe('POST', '/api/rpg/set_camera', {'detach': False})
finally:
    proc.terminate()
    time.sleep(2)
    log.flush()

(OUT / 'rv_daynight_l4.json').write_text(json.dumps(res, indent=2), encoding='utf-8')
ok = (res.get('predicted_advance_h') is not None
      and abs(res['measured_advance_h'] - res['predicted_advance_h']) < 0.15
      and res.get('paused_drift_h', 1.0) < 0.01)
print('\nVERDICT: %s' % ('the cycle runs on the shipped exe at the authored rate, and pausing stops it'
                         if ok else 'NOT PROVEN - see rv_daynight_l4.json'), flush=True)
