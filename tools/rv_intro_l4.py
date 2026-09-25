"""Ravenmere G-116 L4: the story card before the first scene, on the SHIPPED exe.

User (2026-09-23): "when first starting the game, I expect an intro text window to be open that a
player must click 'ack' to or something similar before actually getting to start. it should give
story context". Two runs of the exe:
  phase A  New Game -> the screen must be `narrative` within 3 s, the town must NOT load while the
           card is up (5 s), a screenshot of the card, then the Continue button -> town.
  phase B  New Game -> the card -> Enter -> town (the keyboard acknowledgement).
  phase C  a 2,640-char intro (probe game.json, restored after): the wheel is consumed over the
           text and the TEXT moves after the wheel, Down and PageDown; a no-input control does not.
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


def phase_scroll():
    """Phase C (the scroll follow-up): the probe build's game.json gets a 2,400-char intro for this
    run only (restored after). On the card: the wheel over the text box must be CONSUMED, and the
    frame must change after the wheel, after holding Down, and after PageDown."""
    import hashlib
    gj = REL / 'game.json'
    orig = gj.read_bytes()
    try:
        g = json.loads(orig.decode('utf-8-sig'))
        para = ("The seal on the Hollow Barrow broke the night the first child went missing, and the "
                "town has not slept since. ") * 3
        g['intro'] = {'title': 'A Long Intro', 'text': [para] * 8, 'button': 'Begin'}
        gj.write_text(json.dumps(g, indent=2), encoding='utf-8')
        log = open(OUT / ('rv_intro_%s_C.log' % LABEL), 'w', encoding='utf-8', errors='replace')
        proc = subprocess.Popen([str(REL / 'Ravenmere.exe'), '--test', str(PORT)], cwd=str(REL), stdout=log, stderr=subprocess.STDOUT)
        res = {'phase': 'C', 'intro_chars': sum(len(t) for t in g['intro']['text'])}
        try:
            for _ in range(180):
                if 'err' not in safe('GET', '/api/state', t=3):
                    break
                time.sleep(1)
            time.sleep(2)
            safe('POST', '/api/ui/click', {'x': 640, 'y': 374})
            time.sleep(1.5)
            res['screen'] = state()[0]

            def frame(name):
                """The TEXT in the card's text box: pixels brighter than 160 (text is ~220+,
                the dimmed world behind the card stays under ~100 and orbits, so whole-frame
                hashes always differ and prove nothing). Returns the set of bright pixels."""
                time.sleep(0.6)
                r = safe('GET', '/api/screenshot')
                if not r.get('path'):
                    return None
                data = (REL / r['path']).read_bytes()
                out = OUT / ('rv_intro_%s_scroll_%s.png' % (LABEL, name))
                out.write_bytes(data)
                from PIL import Image
                im = Image.open(out).convert('L')
                sx, sy = im.size[0] / 1280.0, im.size[1] / 720.0
                box = im.crop((int(140 * sx), int(130 * sy), int(1140 * sx), int(590 * sy)))
                w = box.size[0]
                return frozenset(i for i, p in enumerate(box.getdata()) if p > 160)
            h0 = frame('top')
            sc = safe('POST', '/api/rpg/ui_scroll', {'x': 640, 'y': 360, 'delta': -3})
            res['wheel_consumed'] = bool(sc.get('consumed'))
            h1 = frame('wheel')
            safe('POST', '/api/rpg/inject_input', {'keys': ['Down'], 'hold': 0.6})
            h2 = frame('down')
            safe('POST', '/api/rpg/inject_input', {'keys': ['PageDown'], 'hold': 0.15})
            h3 = frame('pagedown')
            # the text moved = its bright pixels are not the same set (a control: two captures with
            # no input in between must be identical - recorded as 'control_same')
            hc = frame('control')

            def frac(a, b):   # share of text pixels that differ; the orbiting world adds ~0.1%
                return len(a ^ b) / max(1, len(a)) if a is not None and b is not None else 1.0
            res['diff'] = {'wheel': round(frac(h0, h1), 3), 'down': round(frac(h1, h2), 3),
                           'pagedown': round(frac(h2, h3), 3), 'control': round(frac(h3, hc), 3)}
            res['control_same'] = res['diff']['control'] < 0.01
            res['frame_changed'] = {k: res['diff'][k] > 0.05 for k in ('wheel', 'down', 'pagedown')}
            res['text_pixels'] = len(h0) if h0 else 0
            res['still_on_card'] = state()[0] == 'narrative'
            res['ok'] = bool(res['screen'] == 'narrative' and res['wheel_consumed'] and all(res['frame_changed'].values())
                             and res['control_same'] and res['still_on_card'])
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=10)
            except Exception:
                proc.kill()
            log.close()
    finally:
        gj.write_bytes(orig)
    print('  phase C: %d chars, screen %s, wheel consumed %s, text diff %s, control unchanged %s, still on card %s -> %s' % (
        res['intro_chars'], res.get('screen'), res.get('wheel_consumed'), res.get('diff'), res.get('control_same'), res.get('still_on_card'),
        'OK' if res.get('ok') else 'FAIL'), flush=True)
    return res


results = [
    run_phase('A', lambda: safe('POST', '/api/ui/click', {'x': 640, 'y': 628})),        # the Continue button
    run_phase('B', lambda: safe('POST', '/api/rpg/inject_input', {'keys': ['Enter'], 'hold': 0.15})),
    phase_scroll(),
]
ok = all(r.get('ok') for r in results)
(OUT / ('rv_intro_%s.json' % LABEL)).write_text(json.dumps({'label': LABEL, 'exe': str(REL / 'Ravenmere.exe'), 'ok': ok, 'phases': results}, indent=1), encoding='utf-8')
print('RESULT', 'OK' if ok else 'FAIL', '->', OUT / ('rv_intro_%s.json' % LABEL))
