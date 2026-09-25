"""Ravenmere G-116 L4: the story card before the first scene, on the SHIPPED exe.

User (2026-09-23): "when first starting the game, I expect an intro text window to be open that a
player must click 'ack' to or something similar before actually getting to start. it should give
story context". Two runs of the exe:
  phase A  New Game -> the screen must be `narrative` within 3 s, the town must NOT load while the
           card is up (5 s), a screenshot of the card, then the Continue button -> town.
  phase B  New Game -> the card -> Enter -> town (the keyboard acknowledgement).
Red on a build without the card: New Game goes straight to loading / the town.
Writes docs/evidence/ravenmere/rv_intro_<label>.json (+ _card.png).

Usage: python tools/rv_intro_l4.py [label]
"""
import json
import os
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

REL = Path(os.environ.get("RV_REL", r"C:/Users/bpete/Documents/PhyxelProjects/Ravenmere/build_probe/Release"))
OUT = Path(__file__).resolve().parents[1] / 'docs' / 'evidence' / 'ravenmere'
PORT = 8123
B = 'http://127.0.0.1:%d' % PORT
LABEL = sys.argv[1] if len(sys.argv) > 1 else 'run'


def safe(m, p, b=None, t=60):
    try:
        r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None,
                                   method=m, headers={'Content-Type': 'application/json'})
        return json.load(urllib.request.urlopen(r, timeout=t))
    except Exception as e:
        return {'err': str(e)[:200]}


def state():
    s = safe('GET', '/api/screen/state')
    return s.get('screen'), s.get('scene_id')


def run_phase(name, acknowledge):
    log = open(OUT / ('rv_intro_%s_%s.log' % (LABEL, name)), 'w', encoding='utf-8', errors='replace')
    proc = subprocess.Popen([str(REL / 'Ravenmere.exe'), '--test', str(PORT)], cwd=str(REL), stdout=log, stderr=subprocess.STDOUT)
    res = {'phase': name}
    try:
        for _ in range(180):
            if 'err' not in safe('GET', '/api/state', t=3):
                break
            time.sleep(1)
        time.sleep(2)
        res['before'] = state()
        safe('POST', '/api/ui/click', {'x': 640, 'y': 374})   # the menu's New Game
        seen = []
        t0 = time.time()
        card_at = None
        while time.time() - t0 < 3.0:
            sc, scene = state()
            seen.append((round(time.time() - t0, 2), sc, scene))
            if sc == 'narrative' and card_at is None:
                card_at = round(time.time() - t0, 2)
            time.sleep(0.1)
        res['card_shown_at_s'] = card_at
        res['states_first_3s'] = seen[::5]
        # while the card is up the world must not load
        held = []
        t1 = time.time()
        while time.time() - t1 < 5.0:
            held.append(state())
            time.sleep(0.5)
        res['states_while_held'] = sorted(set(held))
        res['town_loaded_while_held'] = any(scene == 'town' for _, scene in held)
        if name == 'A':
            shot = safe('GET', '/api/screenshot')
            if shot.get('path'):
                (OUT / ('rv_intro_%s_card.png' % LABEL)).write_bytes((REL / shot['path']).read_bytes())
        acknowledge()
        reached = False
        t2 = time.time()
        while time.time() - t2 < 60:
            sc, scene = state()
            if scene == 'town' and sc == 'playing':
                reached = True
                break
            time.sleep(0.5)
        res['town_after_ack_s'] = round(time.time() - t2, 1) if reached else None
        res['ok'] = bool(card_at is not None and not res['town_loaded_while_held'] and reached)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except Exception:
            proc.kill()
        log.close()
    print('  phase %s: card at %s s, held states %s, town while held %s, town after ack %s s -> %s' % (
        name, res.get('card_shown_at_s'), res.get('states_while_held'), res.get('town_loaded_while_held'),
        res.get('town_after_ack_s'), 'OK' if res.get('ok') else 'FAIL'), flush=True)
    return res


results = [
    run_phase('A', lambda: safe('POST', '/api/ui/click', {'x': 640, 'y': 628})),        # the Continue button
    run_phase('B', lambda: safe('POST', '/api/rpg/inject_input', {'keys': ['Enter'], 'hold': 0.15})),
]
ok = all(r.get('ok') for r in results)
(OUT / ('rv_intro_%s.json' % LABEL)).write_text(json.dumps({'label': LABEL, 'exe': str(REL / 'Ravenmere.exe'), 'ok': ok, 'phases': results}, indent=1), encoding='utf-8')
print('RESULT', 'OK' if ok else 'FAIL', '->', OUT / ('rv_intro_%s.json' % LABEL))
